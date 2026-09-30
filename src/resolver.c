#include "resolver.h"

#include "natives.h"
#include "suggest.h"

typedef enum { K_VARIABLE, K_PARAM, K_FUNCTION, K_NATIVE } VarKind;
static const char *KIND[] = { "variable", "paramètre", "fonction", "native" };
static const char *USED[] = { "utilisée", "utilisé", "utilisée", "" };

typedef struct {
    const char *name;
    VarKind kind;
    Span span;
    bool defined, used;
} VarInfo;

typedef struct {
    const char *name;
    Span span;
} Later;

typedef struct {
    int depth; /* profondeur de fonction de cette portée */
    VarInfo *names;
    int nnames, capnames;
    Later *later; /* déclarés plus loin dans ce bloc (pour expliquer E0205) */
    int nlater, caplater;
    const char **later_used;
    int nlu, caplu;
} Scope;

typedef struct {
    const char *name;
    Span span;
    Diag *unused; /* avertissement W0201 émis pour ce nom, s'il y en a un */
} Gone;           /* nom d'un bloc refermé */

struct Resolver {
    bool repl;
    Scope *scopes;
    int nscopes, capscopes;
    int function_depth;
    Gone *gone;
    int ngone, capgone;
    DiagList *diags;
};

/* Noms venus d'autres langages */
static const struct {
    const char *name, *help;
} FOREIGN_NAMES[] = {
    { "null", "en Nova, l'absence de valeur s'écrit `nil`" },
    { "NULL", "en Nova, l'absence de valeur s'écrit `nil`" },
    { "None", "en Nova, l'absence de valeur s'écrit `nil`" },
    { "undefined", "en Nova, l'absence de valeur s'écrit `nil`" },
    { "True", "les booléens s'écrivent en minuscules : `true`" },
    { "False", "les booléens s'écrivent en minuscules : `false`" },
    { "not", "la négation s'écrit `!` : `!condition`" },
    { "printf", "pour afficher, utilise `print(...)`" },
    { "println", "pour afficher, utilise `print(...)`" },
    { "echo", "pour afficher, utilise `print(...)`" },
    { "console", "pour afficher, utilise `print(...)`" },
    { "append", "pour ajouter à une liste, utilise `push(liste, element)`" },
    { "length", "pour la longueur, utilise `len(valeur)`" },
    { "size", "pour la longueur, utilise `len(valeur)`" },
    { "strlen", "pour la longueur, utilise `len(valeur)`" },
};

static void add(Resolver *r, Diag *d) { diaglist_push(r->diags, d); }
static Scope *top(Resolver *r) { return &r->scopes[r->nscopes - 1]; }

static int find_name(const Scope *s, const char *name) {
    for (int i = s->nnames - 1; i >= 0; i--)
        if (strcmp(s->names[i].name, name) == 0) return i;
    return -1;
}

static int find_later(const Scope *s, const char *name) {
    for (int i = 0; i < s->nlater; i++)
        if (strcmp(s->later[i].name, name) == 0) return i;
    return -1;
}

static bool is_later_used(const Scope *s, const char *name) {
    for (int i = 0; i < s->nlu; i++)
        if (strcmp(s->later_used[i], name) == 0) return true;
    return false;
}

static void push_scope(Resolver *r) {
    Scope s = { 0 };
    s.depth = r->function_depth;
    DA_PUSH(r->scopes, r->nscopes, r->capscopes, s);
}

Resolver *resolver_new(bool repl) {
    Resolver *r = xmalloc(sizeof *r);
    memset(r, 0, sizeof *r);
    r->repl = repl;
    push_scope(r);
    for (int i = 0; i < N_NATIVES; i++) {
        VarInfo v = { NATIVES[i].name, K_NATIVE, { 0 }, true, true };
        Scope *g = top(r);
        DA_PUSH(g->names, g->nnames, g->capnames, v);
    }
    return r;
}

/* ------------------------------------------------------------ portées */
static int declare(Resolver *r, const char *name, Span span, VarKind kind) {
    Scope *s = top(r);
    int old = find_name(s, name);
    if (old >= 0) {
        VarInfo *o = &s->names[old];
        if (o->kind == K_NATIVE) {
            Diag *d = diag_new(LEVEL_WARNING, "W0202", "`%s` masque la fonction native du même nom", name);
            diag_primary(d, span, "la fonction native `%s` ne sera plus accessible", name);
            add(r, diag_help(d, "choisis un autre nom si tu veux garder l'accès à la fonction native"));
        } else if (!(r->repl && r->nscopes == 1)) {
            Diag *d = diag_new(LEVEL_ERROR, "E0201", "`%s` est déjà déclaré dans cette portée", name);
            diag_primary(d, span, "nouvelle déclaration");
            diag_secondary(d, o->span, "première déclaration");
            if (kind == K_VARIABLE && (o->kind == K_VARIABLE || o->kind == K_PARAM))
                diag_help(d, "pour changer sa valeur, écris simplement `%s = ...` (sans `let`)", name);
            else
                diag_help(d, "donne un nom différent à chacun");
            add(r, d);
        }
    }
    VarInfo info = { name, kind, span, kind != K_VARIABLE, is_later_used(s, name) };
    int li = find_later(s, name);
    if (li >= 0) s->later[li] = s->later[--s->nlater];
    if (old >= 0) {
        s->names[old] = info;
        return old;
    }
    DA_PUSH(s->names, s->nnames, s->capnames, info);
    return s->nnames - 1;
}

/* Les fonctions sont « remontées » (utilisables avant leur déclaration) ;
 * les variables sont notées pour expliquer une utilisation trop tôt. */
static void prescan(Resolver *r, NodeList *stmts) {
    for (int i = 0; i < stmts->count; i++) {
        Node *s = stmts->items[i];
        if (s->kind == N_FNDECL) {
            Node *f = s->as.fndecl.func;
            declare(r, f->as.func.name, f->as.func.name_span, K_FUNCTION);
        } else if (s->kind == N_LET) {
            Scope *sc = top(r);
            if (find_name(sc, s->as.let.name) < 0 && find_later(sc, s->as.let.name) < 0) {
                Later l = { s->as.let.name, s->as.let.name_span };
                DA_PUSH(sc->later, sc->nlater, sc->caplater, l);
            }
        }
    }
}

static void end_scope(Resolver *r) {
    Scope s = r->scopes[--r->nscopes];
    for (int i = 0; i < s.nnames; i++) {
        VarInfo *v = &s.names[i];
        Gone g = { v->name, v->span, NULL };
        if (!v->used && v->name[0] != '_' && v->kind != K_NATIVE) {
            Diag *d = diag_new(LEVEL_WARNING, "W0201", "%s `%s` jamais %s", KIND[v->kind], v->name, USED[v->kind]);
            diag_primary(d, v->span, "déclaration ici");
            diag_help(d, "si c'est inutile, supprime la déclaration ; si c'est volontaire, "
                         "préfixe le nom par `_` : `_%s`", v->name);
            add(r, d);
            g.unused = d;
        }
        DA_PUSH(r->gone, r->ngone, r->capgone, g);
    }
    free(s.names);
    free(s.later);
    free(s.later_used);
}

static void resolve_stmt(Resolver *r, Node *s);

static void scoped(Resolver *r, NodeList *stmts, Param *params, int nparams) {
    push_scope(r);
    for (int i = 0; i < nparams; i++) declare(r, params[i].name, params[i].span, K_PARAM);
    prescan(r, stmts);
    for (int i = 0; i < stmts->count; i++) resolve_stmt(r, stmts->items[i]);
    end_scope(r);
}

static void function(Resolver *r, Node *f) {
    r->function_depth++;
    scoped(r, &f->as.func.body, f->as.func.params, f->as.func.nparams);
    r->function_depth--;
}

/* ------------------------------------------------------------ noms */
static void unknown_name(Resolver *r, const char *name, Span span, bool assigning) {
    Diag *d = diag_new(LEVEL_ERROR, "E0200", "`%s` n'est pas défini", name);
    diag_primary(d, span, "nom inconnu ici");

    const char *foreign = NULL;
    for (size_t i = 0; i < sizeof FOREIGN_NAMES / sizeof FOREIGN_NAMES[0]; i++)
        if (strcmp(FOREIGN_NAMES[i].name, name) == 0) foreign = FOREIGN_NAMES[i].help;
    int gone = -1;
    for (int i = r->ngone - 1; i >= 0 && gone < 0; i--)
        if (strcmp(r->gone[i].name, name) == 0) gone = i;

    if (foreign) {
        diag_help(d, "%s", foreign);
    } else if (gone >= 0) {
        Gone *g = &r->gone[gone];
        if (g->unused) g->unused->suppressed = true; /* cette erreur l'explique déjà */
        diag_secondary(d, g->span, "un `%s` existe ici, mais seulement dans son bloc", name);
        diag_note(d, "une variable déclarée dans un bloc `{ ... }` disparaît à la fin de ce bloc");
        diag_help(d, "déclare `%s` avant le bloc pour pouvoir l'utiliser après", name);
    } else {
        const char **cands = NULL;
        int n = 0, cap = 0;
        for (int si = 0; si < r->nscopes; si++)
            for (int i = 0; i < r->scopes[si].nnames; i++) DA_PUSH(cands, n, cap, r->scopes[si].names[i].name);
        const char *guess = closest(name, cands, n);
        free(cands);
        for (int si = r->nscopes - 1; guess && si >= 0; si--) {
            int i = find_name(&r->scopes[si], guess);
            if (i < 0) continue;
            VarInfo *v = &r->scopes[si].names[i];
            if (v->kind == K_NATIVE) {
                diag_help(d, "vouliez-vous dire la fonction native `%s` ?", guess);
            } else {
                char *where = v->span.src == span.src ? fmt("ligne %d", span_line(v->span)) : span_location(v->span);
                diag_help(d, "vouliez-vous dire `%s` ? (%s, déclaration %s)", guess, KIND[v->kind], where);
                free(where);
            }
            break;
        }
    }
    if (assigning) diag_help(d, "pour créer une nouvelle variable, écris `let %s = ...`", name);
    add(r, d);
}

static void lookup(Resolver *r, const char *name, Span span, bool assigning) {
    for (int si = r->nscopes - 1; si >= 0; si--) {
        Scope *s = &r->scopes[si];
        int i = find_name(s, name);
        if (i >= 0) {
            VarInfo *v = &s->names[i];
            if (!v->defined && s->depth == r->function_depth) {
                Diag *d = diag_new(LEVEL_ERROR, "E0203", "`%s` est utilisée dans sa propre déclaration", name);
                diag_primary(d, span, "lue ici");
                diag_secondary(d, v->span, "alors que sa déclaration n'est pas terminée");
                add(r, diag_help(d, "donne-lui d'abord une valeur qui ne dépend pas d'elle-même"));
            }
            if (!assigning) v->used = true;
            return;
        }
        int li = find_later(s, name);
        if (li >= 0) {
            if (!assigning && !is_later_used(s, name)) DA_PUSH(s->later_used, s->nlu, s->caplu, name);
            /* Dans une fonction, la variable existera au moment de l'appel : c'est permis. */
            if (s->depth == r->function_depth) {
                Diag *d = diag_new(LEVEL_ERROR, "E0205", "`%s` est utilisée avant sa déclaration", name);
                diag_primary(d, span, "utilisée ici");
                diag_secondary(d, s->later[li].span, "déclarée plus loin");
                add(r, diag_help(d, "le programme s'exécute de haut en bas : déplace `let %s = ...` "
                                    "avant cette ligne", name));
            }
            return;
        }
    }
    unknown_name(r, name, span, assigning);
}

/* ------------------------------------------------------------ parcours */
static void resolve_expr(Resolver *r, Node *e) {
    switch (e->kind) {
    case N_NUMBER: case N_STRING: case N_TRUE: case N_FALSE: case N_NIL:
        break;
    case N_VAR:
        lookup(r, e->as.var.name, e->span, false);
        break;
    case N_ASSIGN:
        resolve_expr(r, e->as.assign.value);
        lookup(r, e->as.assign.name, e->as.assign.name_span, true);
        break;
    case N_UNARY:
        resolve_expr(r, e->as.unary.operand);
        break;
    case N_BINARY: case N_LOGICAL:
        resolve_expr(r, e->as.binary.left);
        resolve_expr(r, e->as.binary.right);
        break;
    case N_CALL:
        resolve_expr(r, e->as.call.callee);
        for (int i = 0; i < e->as.call.args.count; i++) resolve_expr(r, e->as.call.args.items[i]);
        break;
    case N_LIST:
        for (int i = 0; i < e->as.list.items.count; i++) resolve_expr(r, e->as.list.items.items[i]);
        break;
    case N_INDEX_ASSIGN:
        resolve_expr(r, e->as.index.value);
        /* fallthrough */
    case N_INDEX:
        resolve_expr(r, e->as.index.target);
        resolve_expr(r, e->as.index.index);
        break;
    case N_FUNC:
        function(r, e);
        break;
    default:
        break;
    }
}

static bool is_useless(Node *e) {
    switch (e->kind) {
    case N_NUMBER: case N_STRING: case N_TRUE: case N_FALSE: case N_NIL: case N_VAR: case N_BINARY:
    case N_UNARY: case N_LOGICAL: case N_INDEX: case N_LIST: case N_FUNC:
        return true;
    default:
        return false;
    }
}

static void resolve_stmt(Resolver *r, Node *s) {
    switch (s->kind) {
    case N_LET: {
        int si = r->nscopes - 1;
        int i = declare(r, s->as.let.name, s->as.let.name_span, K_VARIABLE);
        resolve_expr(r, s->as.let.init);
        r->scopes[si].names[i].defined = true;
        break;
    }
    case N_FNDECL:
        function(r, s->as.fndecl.func); /* le nom a déjà été déclaré par prescan */
        break;
    case N_RETURN:
        if (r->function_depth == 0) {
            Diag *d = diag_new(LEVEL_ERROR, "E0202", "`return` en dehors d'une fonction");
            diag_primary(d, s->span, "ce `return` n'est dans aucune fonction");
            add(r, diag_help(d, "`return` renvoie une valeur depuis une fonction ; ici, il n'a pas de sens"));
        }
        if (s->as.ret.value) resolve_expr(r, s->as.ret.value);
        break;
    case N_IF:
        resolve_expr(r, s->as.if_.cond);
        resolve_stmt(r, s->as.if_.then);
        if (s->as.if_.other) resolve_stmt(r, s->as.if_.other);
        break;
    case N_WHILE:
        resolve_expr(r, s->as.while_.cond);
        resolve_stmt(r, s->as.while_.body);
        break;
    case N_BLOCK:
        scoped(r, &s->as.block.stmts, NULL, 0);
        break;
    case N_EXPR_STMT: {
        Node *e = s->as.expr.expr;
        resolve_expr(r, e);
        if (!r->repl && is_useless(e)) {
            Diag *d = diag_new(LEVEL_WARNING, "W0203", "le résultat de cette expression n'est pas utilisé");
            diag_primary(d, e->span, "calculé puis aussitôt oublié");
            if (e->kind == N_BINARY && e->as.binary.op == T_EQEQ)
                diag_help(d, "pour affecter une valeur, utilise un seul `=`");
            else
                diag_help(d, "garde-le avec `let`, affiche-le avec `print(...)`, ou supprime cette ligne");
            add(r, d);
        }
        break;
    }
    default:
        break;
    }
}

void resolver_run(Resolver *r, NodeList *program, DiagList *diags) {
    r->diags = diags;
    int errors_before = diaglist_count(diags, LEVEL_ERROR);
    Scope *g = &r->scopes[0];
    int saved_n = g->nnames;
    VarInfo *saved = xmalloc(sizeof(VarInfo) * (size_t)(saved_n ? saved_n : 1));
    memcpy(saved, g->names, sizeof(VarInfo) * (size_t)saved_n);

    prescan(r, program);
    for (int i = 0; i < program->count; i++) resolve_stmt(r, program->items[i]);

    g = &r->scopes[0];
    g->nlater = 0;
    g->nlu = 0;
    if (diaglist_count(diags, LEVEL_ERROR) > errors_before) { /* REPL : une ligne refusée ne laisse pas de trace */
        memcpy(g->names, saved, sizeof(VarInfo) * (size_t)saved_n);
        g->nnames = saved_n;
    }
    free(saved);
}
