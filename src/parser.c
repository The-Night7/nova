/* Parser : transforme les tokens en AST (descente récursive).
 *
 * Grammaire (du moins prioritaire au plus prioritaire) :
 *   instruction := let | fn | return | if | while | bloc | expression ";"
 *   expression  := affectation
 *   affectation := ou ( "=" affectation )?
 *   ou          := et ( "or" et )*
 *   et          := égalité ( "and" égalité )*
 *   égalité     := comparaison ( ("==" | "!=") comparaison )*
 *   comparaison := terme ( ("<" | "<=" | ">" | ">=") terme )*
 *   terme       := facteur ( ("+" | "-") facteur )*
 *   facteur     := unaire ( ("*" | "/" | "%") unaire )*
 *   unaire      := ("!" | "-") unaire | suffixe
 *   suffixe     := primaire ( "(" arguments ")" | "[" expression "]" )*
 *   primaire    := nombre | texte | true | false | nil | nom | "(" expression ")"
 *                | "[" éléments "]" | "fn" "(" paramètres ")" bloc
 *
 * Récupération d'erreur : chaque instruction est analysée sous un setjmp.
 * En cas d'erreur, fail() note le diagnostic et saute (longjmp) au début de
 * l'instruction, qui se resynchronise sur la suivante.
 */
#include "parser.h"

#include <setjmp.h>

#define MAX_ERRORS 25

typedef struct {
    const Token *t;
    int count, pos;
    DiagList *diags;
    int nerrors;
    jmp_buf *recover;
} Parser;

/* ------------------------------------------------------------ utilitaires */
static const Token *peek_at(Parser *p, int k) {
    int i = p->pos + k;
    return &p->t[i < p->count ? i : p->count - 1];
}
static const Token *peek(Parser *p) { return peek_at(p, 0); }
static const Token *previous(Parser *p) { return &p->t[p->pos > 0 ? p->pos - 1 : 0]; }
static bool check(Parser *p, TokKind k) { return peek(p)->kind == k; }

static const Token *advance(Parser *p) {
    const Token *tok = peek(p);
    if (tok->kind != T_EOF) p->pos++;
    return tok;
}

static const Token *match(Parser *p, TokKind k) { return check(p, k) ? advance(p) : NULL; }

static void report(Parser *p, Diag *d) {
    diaglist_push(p->diags, d);
    p->nerrors++;
}

static _Noreturn void fail(Parser *p, Diag *d) {
    report(p, d);
    longjmp(*p->recover, 1);
}

static char *lexeme(const Token *t) {
    return xstrndup(t->span.src->text + t->span.start, (size_t)(t->span.end - t->span.start));
}

static char *describe(const Token *t) {
    char *lx = lexeme(t), *r;
    switch (t->kind) {
    case T_EOF: r = fmt("la fin du fichier"); break;
    case T_NUMBER: r = fmt("le nombre `%s`", lx); break;
    case T_STRING: r = fmt("le texte %s", lx); break;
    case T_IDENT: r = fmt("le nom `%s`", lx); break;
    default: r = tok_is_keyword(t->kind) ? fmt("le mot-clé `%s`", lx) : fmt("`%s`", lx);
    }
    free(lx);
    return r;
}

static bool starts_expression(TokKind k) {
    switch (k) {
    case T_NUMBER: case T_STRING: case T_IDENT: case T_LPAREN: case T_LBRACKET:
    case T_TRUE: case T_FALSE: case T_NIL: case T_FN: case T_BANG: case T_MINUS:
        return true;
    default:
        return false;
    }
}

static Node *node_new(NodeKind kind, Span span) {
    Node *n = xmalloc(sizeof *n);
    memset(n, 0, sizeof *n);
    n->kind = kind;
    n->span = span;
    return n;
}

static void push_node(NodeList *l, Node *n) { DA_PUSH(l->items, l->count, l->cap, n); }

static Diag *err(const char *code, const char *f, ...) PRINTF_LIKE(2, 3);
static Diag *err(const char *code, const char *f, ...) {
    va_list ap;
    va_start(ap, f);
    char *m = vfmt(f, ap);
    va_end(ap);
    Diag *d = diag_new(LEVEL_ERROR, code, "%s", m);
    free(m);
    return d;
}

static const Token *expect(Parser *p, TokKind kind, const char *context, const char *help) {
    if (check(p, kind)) return advance(p);
    const Token *tok = peek(p), *prev = previous(p);
    if (kind == T_SEMI && (tok->kind == T_EOF || span_line(tok->span) > span_end_line(prev->span))) {
        /* Oubli classique en fin de ligne : on le signale, puis on continue
         * comme si le `;` était là (pas d'erreurs en cascade). */
        Diag *d = err("E0100", "il manque un `;` %s", context);
        report(p, diag_primary(d, span_at_end(prev->span), "ajoute `;` ici"));
        return prev;
    }
    char *desc = describe(tok);
    Diag *d = err("E0100", "`%s` attendu %s, mais trouvé %s", tok_text(kind), context, desc);
    free(desc);
    diag_primary(d, tok->span, "`%s` attendu ici", tok_text(kind));
    if (help) diag_help(d, "%s", help);
    else if (kind == T_SEMI) diag_note(d, "chaque instruction se termine par `;`");
    fail(p, d);
}

static const Token *expect_name(Parser *p, const char *what) {
    if (check(p, T_IDENT)) return advance(p);
    const Token *tok = peek(p);
    char *desc = describe(tok);
    Diag *d = err("E0100", "%s attendu, mais trouvé %s", what, desc);
    free(desc);
    diag_primary(d, tok->span, "%s attendu ici", what);
    if (tok_is_keyword(tok->kind))
        diag_help(d, "`%s` est un mot réservé du langage : choisis un autre nom, par exemple `%s_`",
                  tok_text(tok->kind), tok_text(tok->kind));
    else if (tok->kind == T_NUMBER)
        diag_help(d, "un nom commence par une lettre ou `_`");
    fail(p, d);
}

static TokKind closer_of(TokKind opener) {
    return opener == T_LPAREN ? T_RPAREN : opener == T_LBRACKET ? T_RBRACKET : T_RBRACE;
}

static const Token *close_delim(Parser *p, const Token *opener, const char *context) {
    TokKind closer = closer_of(opener->kind);
    if (check(p, closer)) return advance(p);
    const Token *tok = peek(p);
    Diag *d;
    if (tok->kind == T_EOF) {
        d = err("E0103", "`%s` jamais fermé", tok_text(opener->kind));
        diag_primary(d, tok->span, "fin du fichier atteinte sans `%s`", tok_text(closer));
        diag_secondary(d, opener->span, "ouvert ici");
        diag_help(d, "ajoute `%s` pour fermer %s", tok_text(closer), context);
    } else {
        char *desc = describe(tok);
        d = err("E0100", "`%s` attendu pour fermer %s, mais trouvé %s", tok_text(closer), context, desc);
        free(desc);
        diag_primary(d, tok->span, "`%s` attendu ici", tok_text(closer));
        diag_secondary(d, opener->span, "ouvert ici");
        if (closer != T_RBRACE && starts_expression(tok->kind))
            diag_help(d, "il manque peut-être une `,` entre deux éléments");
    }
    fail(p, d);
}

/* ------------------------------------------------------------ déclarations */
static Node *statement(Parser *p);
static Node *expression(Parser *p);
static Node *block(Parser *p, const char *context);

static void synchronize(Parser *p) {
    /* Saute les tokens jusqu'au début probable de l'instruction suivante. */
    int depth = 0;
    while (!check(p, T_EOF)) {
        TokKind k = peek(p)->kind;
        if (depth == 0 && (k == T_LET || k == T_FN || k == T_IF || k == T_WHILE || k == T_RETURN || k == T_RBRACE))
            return;
        advance(p);
        if (k == T_LBRACE) depth++;
        else if (k == T_RBRACE) {
            if (--depth == 0) return;
        } else if (k == T_SEMI && depth == 0) return;
    }
}

static Node *declaration(Parser *p) {
    jmp_buf here;
    jmp_buf *saved = p->recover;
    const int start = p->pos;
    Node *volatile result = NULL;
    p->recover = &here;
    if (setjmp(here) == 0) {
        result = statement(p);
    } else {
        if (p->pos == start) advance(p);
        synchronize(p);
        result = NULL;
    }
    p->recover = saved;
    return result;
}

/* Mots-clés d'autres langages : on guide plutôt que de dérouter. */
static const struct {
    const char *word, *help;
} FOREIGN[] = {
    { "var", "pour déclarer une variable, Nova utilise `let` : `let x = 1;`" },
    { "const", "pour déclarer une variable, Nova utilise `let` : `let x = 1;`" },
    { "val", "pour déclarer une variable, Nova utilise `let` : `let x = 1;`" },
    { "int", "Nova n'a pas de types à déclarer : écris simplement `let x = 1;`" },
    { "def", "pour déclarer une fonction, Nova utilise `fn` : `fn nom(a, b) { ... }`" },
    { "function", "pour déclarer une fonction, Nova utilise `fn` : `fn nom(a, b) { ... }`" },
    { "func", "pour déclarer une fonction, Nova utilise `fn` : `fn nom(a, b) { ... }`" },
    { "elif", "Nova écrit `else if`" },
    { "elsif", "Nova écrit `else if`" },
    { "for", "Nova n'a pas (encore) de boucle `for` : utilise `while`" },
    { "foreach", "Nova n'a pas (encore) de boucle `for` : utilise `while`" },
};

static Node *function_rest(Parser *p, const Token *fn_tok, bool named) {
    const Token *name = named ? advance(p) : NULL;
    const Token *opener = expect(p, T_LPAREN, "pour la liste des paramètres",
                                 "même sans paramètre, les parenthèses sont obligatoires : `fn nom() { ... }`");
    Param *params = NULL;
    int nparams = 0, cap = 0;
    if (!check(p, T_RPAREN)) {
        do {
            const Token *pt = expect_name(p, "un nom de paramètre");
            for (int i = 0; i < nparams; i++) {
                if (strcmp(params[i].name, pt->str) == 0) {
                    Diag *d = err("E0108", "paramètre `%s` déclaré deux fois", pt->str);
                    diag_primary(d, pt->span, "deuxième fois");
                    diag_secondary(d, params[i].span, "première fois");
                    report(p, diag_help(d, "chaque paramètre doit avoir un nom différent"));
                }
            }
            Param param = { pt->str, pt->span };
            DA_PUSH(params, nparams, cap, param);
        } while (match(p, T_COMMA));
    }
    close_delim(p, opener, "la liste des paramètres");
    Node *body = block(p, "le corps de la fonction");
    Node *f = node_new(N_FUNC, span_to(fn_tok->span, body->span));
    f->as.func.name = name ? name->str : NULL;
    f->as.func.name_span = name ? name->span : fn_tok->span;
    f->as.func.params = params;
    f->as.func.nparams = nparams;
    f->as.func.body = body->as.block.stmts;
    return f;
}

static Node *condition(Parser *p, const char *keyword) {
    if (check(p, T_LBRACE)) {
        Diag *d = err("E0101", "condition manquante après `%s`", keyword);
        diag_primary(d, peek(p)->span, "une condition est attendue avant ce bloc");
        fail(p, diag_help(d, "exemple : `%s x > 0 { ... }`", keyword));
    }
    Node *cond = expression(p);
    if (cond->kind == N_ASSIGN || cond->kind == N_INDEX_ASSIGN) {
        Span eq = cond->kind == N_ASSIGN ? cond->as.assign.eq_span : cond->as.index.eq_span;
        Diag *d = err("E0104", "affectation `=` utilisée comme condition");
        diag_primary(d, eq, "ceci modifie la valeur au lieu de la comparer");
        report(p, diag_help(d, "pour comparer, utilise `==`"));
    }
    return cond;
}

static Node *if_statement(Parser *p) {
    const Token *kw = advance(p);
    Node *cond = condition(p, "if");
    Node *then = block(p, "le bloc du `if`");
    Node *other = NULL;
    if (match(p, T_ELSE)) other = check(p, T_IF) ? if_statement(p) : block(p, "le bloc du `else`");
    Node *n = node_new(N_IF, span_to(kw->span, (other ? other : then)->span));
    n->as.if_.cond = cond;
    n->as.if_.then = then;
    n->as.if_.other = other;
    return n;
}

static Node *let_statement(Parser *p) {
    const Token *kw = advance(p);
    const Token *name = expect_name(p, "un nom de variable");
    if (check(p, T_SEMI)) {
        Diag *d = err("E0107", "la variable `%s` n'a pas de valeur initiale", name->str);
        diag_primary(d, name->span, "déclarée sans valeur");
        fail(p, diag_help(d, "donne-lui une valeur : `let %s = nil;` si tu n'en as pas encore", name->str));
    }
    expect(p, T_EQ, "après le nom de la variable", NULL);
    Node *init = expression(p);
    expect(p, T_SEMI, "à la fin de la déclaration", NULL);
    Node *n = node_new(N_LET, span_to(kw->span, previous(p)->span));
    n->as.let.name = name->str;
    n->as.let.name_span = name->span;
    n->as.let.init = init;
    return n;
}

static Node *statement(Parser *p) {
    const Token *tok = peek(p);
    if (tok->kind == T_IDENT && peek_at(p, 1)->kind == T_IDENT) {
        for (size_t i = 0; i < sizeof FOREIGN / sizeof FOREIGN[0]; i++) {
            if (strcmp(tok->str, FOREIGN[i].word) == 0) {
                advance(p);
                Diag *d = err("E0109", "`%s` n'est pas un mot-clé de Nova", tok->str);
                diag_primary(d, tok->span, "mot-clé inconnu");
                fail(p, diag_help(d, "%s", FOREIGN[i].help));
            }
        }
    }
    switch (tok->kind) {
    case T_LET:
        return let_statement(p);
    case T_FN:
        if (peek_at(p, 1)->kind == T_IDENT) {
            advance(p);
            Node *f = function_rest(p, tok, true);
            Node *n = node_new(N_FNDECL, f->span);
            n->as.fndecl.func = f;
            return n;
        }
        break;
    case T_RETURN: {
        advance(p);
        Node *value = check(p, T_SEMI) || check(p, T_RBRACE) ? NULL : expression(p);
        expect(p, T_SEMI, "après `return`", NULL);
        Node *n = node_new(N_RETURN, span_to(tok->span, previous(p)->span));
        n->as.ret.value = value;
        return n;
    }
    case T_IF:
        return if_statement(p);
    case T_WHILE: {
        advance(p);
        Node *cond = condition(p, "while");
        Node *body = block(p, "le corps du `while`");
        Node *n = node_new(N_WHILE, span_to(tok->span, body->span));
        n->as.while_.cond = cond;
        n->as.while_.body = body;
        return n;
    }
    case T_LBRACE:
        return block(p, "le bloc");
    default:
        break;
    }
    Node *e = expression(p);
    expect(p, T_SEMI, "à la fin de l'instruction", NULL);
    Node *n = node_new(N_EXPR_STMT, e->span);
    n->as.expr.expr = e;
    return n;
}

static Node *block(Parser *p, const char *context) {
    const Token *opener = expect(p, T_LBRACE, fmt("pour ouvrir %s", context), NULL);
    Node *n = node_new(N_BLOCK, opener->span);
    while (!check(p, T_RBRACE) && !check(p, T_EOF) && p->nerrors < MAX_ERRORS) {
        Node *s = declaration(p);
        if (s) push_node(&n->as.block.stmts, s);
    }
    const Token *closer = close_delim(p, opener, context);
    n->span = span_to(opener->span, closer->span);
    return n;
}

/* ------------------------------------------------------------ expressions */
static const TokKind LEVELS[][5] = {
    { T_EQEQ, T_BANGEQ },
    { T_LT, T_LE, T_GT, T_GE },
    { T_PLUS, T_MINUS },
    { T_STAR, T_SLASH, T_PERCENT },
};
#define NLEVELS ((int)(sizeof LEVELS / sizeof LEVELS[0]))

static bool in_level(int level, TokKind k) {
    for (int i = 0; i < 5 && LEVELS[level][i] != T_EOF; i++)
        if (LEVELS[level][i] == k) return true;
    return false;
}

static Node *unary(Parser *p);

static Node *binary(Parser *p, int level) {
    if (level == NLEVELS) return unary(p);
    Node *left = binary(p, level + 1);
    while (in_level(level, peek(p)->kind)) {
        const Token *op = advance(p);
        Node *right = binary(p, level + 1);
        Node *n = node_new(N_BINARY, span_to(left->span, right->span));
        n->as.binary.op = op->kind;
        n->as.binary.op_span = op->span;
        n->as.binary.left = left;
        n->as.binary.right = right;
        left = n;
    }
    return left;
}

static Node *logic(Parser *p, TokKind op_kind) {
    Node *left = op_kind == T_OR ? logic(p, T_AND) : binary(p, 0);
    while (check(p, op_kind)) {
        const Token *op = advance(p);
        Node *right = op_kind == T_OR ? logic(p, T_AND) : binary(p, 0);
        Node *n = node_new(N_LOGICAL, span_to(left->span, right->span));
        n->as.binary.op = op->kind;
        n->as.binary.op_span = op->span;
        n->as.binary.left = left;
        n->as.binary.right = right;
        left = n;
    }
    return left;
}

static Node *expression(Parser *p) {
    Node *target = logic(p, T_OR);
    if (!check(p, T_EQ)) return target;
    const Token *eq = advance(p);
    Node *value = expression(p);
    Span span = span_to(target->span, value->span);
    if (target->kind == N_VAR) {
        Node *n = node_new(N_ASSIGN, span);
        n->as.assign.name = target->as.var.name;
        n->as.assign.name_span = target->span;
        n->as.assign.eq_span = eq->span;
        n->as.assign.value = value;
        return n;
    }
    if (target->kind == N_INDEX) {
        Node *n = node_new(N_INDEX_ASSIGN, span);
        n->as.index.target = target->as.index.target;
        n->as.index.index = target->as.index.index;
        n->as.index.eq_span = eq->span;
        n->as.index.value = value;
        return n;
    }
    Diag *d = err("E0102", "on ne peut pas affecter une valeur à cette expression");
    diag_primary(d, target->span, "ceci n'est ni une variable ni un élément de liste");
    diag_secondary(d, eq->span, "affectation ici");
    report(p, diag_help(d, "seules les variables (`x = ...`) et les éléments de liste (`liste[0] = ...`) "
                           "peuvent recevoir une valeur"));
    return value;
}

static Node *primary(Parser *p);

static Node *postfix(Parser *p) {
    Node *e = primary(p);
    for (;;) {
        if (check(p, T_LPAREN)) {
            const Token *opener = advance(p);
            Node *call = node_new(N_CALL, e->span);
            call->as.call.callee = e;
            if (!check(p, T_RPAREN)) {
                do push_node(&call->as.call.args, expression(p));
                while (match(p, T_COMMA));
            }
            call->span = span_to(e->span, close_delim(p, opener, "l'appel de fonction")->span);
            e = call;
        } else if (check(p, T_LBRACKET)) {
            const Token *opener = advance(p);
            Node *index = expression(p);
            Node *n = node_new(N_INDEX, span_to(e->span, close_delim(p, opener, "l'accès par indice")->span));
            n->as.index.target = e;
            n->as.index.index = index;
            e = n;
        } else {
            return e;
        }
    }
}

static Node *unary(Parser *p) {
    if (check(p, T_BANG) || check(p, T_MINUS)) {
        const Token *op = advance(p);
        Node *operand = unary(p);
        Node *n = node_new(N_UNARY, span_to(op->span, operand->span));
        n->as.unary.op = op->kind;
        n->as.unary.op_span = op->span;
        n->as.unary.operand = operand;
        return n;
    }
    return postfix(p);
}

static Node *primary(Parser *p) {
    const Token *tok = peek(p);
    Node *n;
    switch (tok->kind) {
    case T_NUMBER:
        advance(p);
        n = node_new(N_NUMBER, tok->span);
        n->as.num = tok->num;
        return n;
    case T_STRING:
        advance(p);
        n = node_new(N_STRING, tok->span);
        n->as.str.chars = tok->str;
        n->as.str.len = tok->slen;
        return n;
    case T_TRUE: advance(p); return node_new(N_TRUE, tok->span);
    case T_FALSE: advance(p); return node_new(N_FALSE, tok->span);
    case T_NIL: advance(p); return node_new(N_NIL, tok->span);
    case T_IDENT:
        advance(p);
        n = node_new(N_VAR, tok->span);
        n->as.var.name = tok->str;
        return n;
    case T_LPAREN: {
        advance(p);
        Node *inner = expression(p);
        inner->span = span_to(tok->span, close_delim(p, tok, "la parenthèse")->span);
        return inner;
    }
    case T_LBRACKET: {
        advance(p);
        n = node_new(N_LIST, tok->span);
        if (!check(p, T_RBRACKET)) {
            do push_node(&n->as.list.items, expression(p));
            while (match(p, T_COMMA));
        }
        n->span = span_to(tok->span, close_delim(p, tok, "la liste")->span);
        return n;
    }
    case T_FN:
        advance(p);
        return function_rest(p, tok, check(p, T_IDENT));
    default:
        break;
    }
    char *desc = describe(tok);
    Diag *d = err("E0101", "expression attendue, mais trouvé %s", desc);
    free(desc);
    diag_primary(d, tok->span, "une valeur est attendue ici");
    switch (tok->kind) {
    case T_PLUS: case T_STAR: case T_SLASH: case T_PERCENT: case T_EQEQ: case T_BANGEQ:
    case T_LT: case T_LE: case T_GT: case T_GE: case T_AND: case T_OR:
        diag_help(d, "`%s` a besoin d'une valeur de chaque côté", tok_text(tok->kind));
        break;
    case T_ELSE:
        diag_help(d, "ce `else` n'est rattaché à aucun `if` : il doit suivre directement "
                     "l'accolade fermante du `if` (sans `;` entre les deux)");
        break;
    case T_SEMI: case T_RPAREN: case T_RBRACKET: case T_RBRACE: case T_EOF:
        diag_help(d, "l'expression semble incomplète");
        break;
    default:
        break;
    }
    fail(p, d);
}

/* ------------------------------------------------------------ programme */
NodeList parse_program(const TokenList *tokens, DiagList *diags) {
    Parser p = { tokens->items, tokens->count, 0, diags, 0, NULL };
    NodeList program = { 0 };
    while (!check(&p, T_EOF) && p.nerrors < MAX_ERRORS) {
        if (check(&p, T_RBRACE)) {
            const Token *tok = advance(&p);
            Diag *d = err("E0106", "`}` sans `{` correspondant");
            diag_primary(d, tok->span, "cette accolade ne ferme rien");
            report(&p, diag_help(d, "supprime-la, ou vérifie qu'il ne manque pas un `{` plus haut"));
            continue;
        }
        Node *s = declaration(&p);
        if (s) push_node(&program, s);
    }
    return program;
}
