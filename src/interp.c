/* Interpréteur : parcourt l'AST et l'exécute (tree-walking).
 *
 * Chaque erreur d'exécution pointe vers le code fautif, décrit les valeurs
 * réelles en jeu (« ceci est un texte ("17") ») et joint la pile d'appels.
 * Une erreur interrompt tout : runtime_fail() fait un longjmp jusqu'à
 * interp_run().
 */
#include "interp.h"

#include <math.h>
#include <setjmp.h>
#include <signal.h>

#include "natives.h"

#define MAX_DEPTH 400

struct Interp {
    ObjEnv *globals;
    Frame frames[MAX_DEPTH]; /* pile d'appels, pour les diagnostics */
    int nframes;
    jmp_buf on_error;
    Diag *error;
};

typedef enum { EX_NORMAL, EX_RETURN } Exec;

static volatile sig_atomic_t interrupted = 0;
static void on_sigint(int sig) {
    (void)sig;
    interrupted = 1;
}

static Exec exec(Interp *in, Node *s, ObjEnv *env);
static Value eval(Interp *in, Node *e, ObjEnv *env);

Interp *interp_new(void) {
    Interp *in = xmalloc(sizeof *in);
    memset(in, 0, sizeof *in);
    heap_init(&in->globals);
    return in;
}

/* ------------------------------------------------------------ erreurs */
static _Noreturn void runtime_fail(Interp *in, Diag *d) {
    d->ntrace = in->nframes;
    d->trace = xmalloc(sizeof(Frame) * (size_t)(in->nframes ? in->nframes : 1));
    memcpy(d->trace, in->frames, sizeof(Frame) * (size_t)in->nframes);
    in->error = d;
    longjmp(in->on_error, 1);
}

static char *plural(int n, const char *word) { return fmt("%d %s%s", n, word, n > 1 ? "s" : ""); }

static void check_interrupt(Interp *in, Span where) {
    if (!interrupted) return;
    interrupted = 0;
    Diag *d = diag_new(LEVEL_ERROR, "E0312", "exécution interrompue (Ctrl+C)");
    runtime_fail(in, diag_primary(d, where, "le programme était ici"));
}

/* ------------------------------------------------------------ blocs */
static void hoist(NodeList *stmts, ObjEnv *env) {
    for (int i = 0; i < stmts->count; i++) {
        Node *s = stmts->items[i];
        if (s->kind == N_FNDECL) {
            Node *f = s->as.fndecl.func;
            env_define(env, f->as.func.name, OBJ_VAL(new_function(f, env)));
        }
    }
}

static Exec run_block(Interp *in, NodeList *stmts, ObjEnv *env) {
    hoist(stmts, env);
    for (int i = 0; i < stmts->count; i++)
        if (exec(in, stmts->items[i], env) == EX_RETURN) return EX_RETURN;
    return EX_NORMAL;
}

/* ------------------------------------------------------------ opérateurs */
static _Noreturn void type_mismatch(Interp *in, Node *e, Value a, Value b) {
    TokKind op = e->as.binary.op;
    Diag *d = diag_new(LEVEL_ERROR, "E0300", "`%s` ne peut pas combiner %s et %s", tok_text(op), a_type(a), a_type(b));
    diag_primary(d, e->as.binary.op_span, "opération impossible");
    diag_secondary(d, e->as.binary.left->span, "ceci est %s", describe(a));
    diag_secondary(d, e->as.binary.right->span, "ceci est %s", describe(b));
    bool text_and_number = (IS_STRING(a) && b.type == V_NUM) || (a.type == V_NUM && IS_STRING(b));
    if (text_and_number) {
        if (op == T_PLUS)
            diag_help(d, "pour assembler du texte et un nombre, convertis le nombre : `\"total : \" + str(n)`");
        diag_help(d, "pour transformer un texte en nombre : `num(texte)`");
    } else if ((IS_LIST(a) || IS_LIST(b)) && op == T_PLUS) {
        diag_help(d, "pour ajouter un élément à une liste : `push(liste, element)`");
    } else if (op == T_LT || op == T_LE || op == T_GT || op == T_GE) {
        diag_note(d, "on ne peut comparer que deux nombres, ou deux textes");
    }
    if (a.type == V_NIL || b.type == V_NIL)
        diag_note(d, "un `nil` inattendu vient souvent d'une fonction qui se termine sans `return`");
    runtime_fail(in, d);
}

static int compare_strings(ObjString *a, ObjString *b) {
    int n = a->len < b->len ? a->len : b->len;
    int c = memcmp(a->chars, b->chars, (size_t)n);
    return c ? c : a->len - b->len;
}

static Value binary_op(Interp *in, Node *e, Value a, Value b) {
    TokKind op = e->as.binary.op;
    if (op == T_EQEQ) return BOOL_VAL(values_equal(a, b));
    if (op == T_BANGEQ) return BOOL_VAL(!values_equal(a, b));

    if (a.type == V_NUM && b.type == V_NUM) {
        double x = a.as.n, y = b.as.n;
        switch (op) {
        case T_PLUS: return NUM_VAL(x + y);
        case T_MINUS: return NUM_VAL(x - y);
        case T_STAR: return NUM_VAL(x * y);
        case T_SLASH:
        case T_PERCENT:
            if (y == 0) {
                Diag *d = diag_new(LEVEL_ERROR, "E0301", "division par zéro");
                diag_primary(d, e->as.binary.right->span, "ce diviseur vaut 0");
                diag_secondary(d, e->as.binary.op_span, "division ici");
                runtime_fail(in, diag_help(d, "vérifie le diviseur avant de diviser : `if d != 0 { ... }`"));
            }
            return NUM_VAL(op == T_SLASH ? x / y : fmod(x, y));
        case T_LT: return BOOL_VAL(x < y);
        case T_LE: return BOOL_VAL(x <= y);
        case T_GT: return BOOL_VAL(x > y);
        case T_GE: return BOOL_VAL(x >= y);
        default: break;
        }
    }
    if (IS_STRING(a) && IS_STRING(b)) {
        if (op == T_PLUS) return OBJ_VAL(new_string_concat(AS_STRING(a), AS_STRING(b)));
        int c = compare_strings(AS_STRING(a), AS_STRING(b));
        switch (op) {
        case T_LT: return BOOL_VAL(c < 0);
        case T_LE: return BOOL_VAL(c <= 0);
        case T_GT: return BOOL_VAL(c > 0);
        case T_GE: return BOOL_VAL(c >= 0);
        default: break;
        }
    }
    if (IS_LIST(a) && IS_LIST(b) && op == T_PLUS) {
        ObjList *l = new_list(); /* a et b sont des racines (cf. N_BINARY) */
        for (int i = 0; i < AS_LIST(a)->count; i++) list_append(l, AS_LIST(a)->items[i]);
        for (int i = 0; i < AS_LIST(b)->count; i++) list_append(l, AS_LIST(b)->items[i]);
        return OBJ_VAL(l);
    }
    type_mismatch(in, e, a, b);
}

/* ------------------------------------------------------------ indices */
static int checked_index(Interp *in, Node *e, Value target, Value index, bool writing) {
    Node *tnode = e->as.index.target, *inode = e->as.index.index;
    if (IS_STRING(target) && writing) {
        Diag *d = diag_new(LEVEL_ERROR, "E0308", "on ne peut pas modifier un texte caractère par caractère");
        diag_primary(d, tnode->span, "ceci est %s", describe(target));
        runtime_fail(in, diag_help(d, "les textes sont immuables : construis un nouveau texte avec `+`"));
    }
    if (!IS_STRING(target) && !IS_LIST(target)) {
        Diag *d = diag_new(LEVEL_ERROR, "E0308", "on ne peut pas utiliser `[...]` sur %s", a_type(target));
        diag_primary(d, tnode->span, "ceci est %s", describe(target));
        runtime_fail(in, diag_note(d, "seuls les textes et les listes s'indexent"));
    }
    if (index.type != V_NUM || index.as.n != floor(index.as.n)) {
        Diag *d = diag_new(LEVEL_ERROR, "E0309", "un indice doit être un nombre entier, pas %s", a_type(index));
        diag_primary(d, inode->span, "ceci est %s", describe(index));
        if (index.type == V_NUM)
            diag_help(d, "les indices sont 0, 1, 2… ; si le calcul donne un nombre à virgule, vérifie-le");
        runtime_fail(in, d);
    }
    int size = IS_STRING(target) ? utf8_len(AS_STRING(target)->chars, AS_STRING(target)->len) : AS_LIST(target)->count;
    double n = index.as.n;
    if (n < 0 || n >= size) {
        StrBuf sb = { 0 };
        value_repr(&sb, index);
        Diag *d = diag_new(LEVEL_ERROR, "E0307", "indice %s hors limites", sb.data);
        diag_primary(d, inode->span, "cet indice vaut %s", sb.data);
        const char *unit = IS_STRING(target) ? "caractère" : "élément";
        if (size == 0) diag_note(d, "%s est vide : aucun indice n'est valide", a_type(target));
        else diag_note(d, "%s contient %s : indices valides de 0 à %d", a_type(target), plural(size, unit), size - 1);
        if (n == size) diag_help(d, "les indices commencent à 0 : le dernier est `len(x) - 1`");
        else if (n < 0) diag_help(d, "les indices négatifs ne sont pas acceptés");
        free(sb.data);
        runtime_fail(in, d);
    }
    return (int)n;
}

/* ------------------------------------------------------------ appels */
static void check_arity(Interp *in, const char *name, int low, int high, int argc, Node *call,
                        const Span *decl, const char *signature) {
    if (argc >= low && (high < 0 || argc <= high)) return;
    char *expected;
    if (high < 0) expected = fmt("au moins %s", plural(low, "argument"));
    else if (low == high) expected = plural(low, "argument");
    else expected = fmt("entre %d et %s", low, plural(high, "argument"));
    Diag *d = diag_new(LEVEL_ERROR, "E0303", "`%s` attend %s, mais en reçoit %d", name, expected, argc);
    diag_primary(d, call->span, "%s donné%s", plural(argc, "argument"), argc > 1 ? "s" : "");
    if (decl) diag_secondary(d, *decl, "fonction définie ici");
    runtime_fail(in, diag_note(d, "signature : `%s`", signature));
}

static char *signature_of(Node *f) {
    StrBuf sb = { 0 };
    sb_printf(&sb, "%s(", f->as.func.name ? f->as.func.name : "fn");
    for (int i = 0; i < f->as.func.nparams; i++) sb_printf(&sb, "%s%s", i ? ", " : "", f->as.func.params[i].name);
    sb_puts(&sb, ")");
    return sb_take(&sb);
}

static Value call_function(Interp *in, ObjFunction *fn, int argc, Value *argv, Node *call) {
    Node *decl = fn->decl;
    const char *name = decl->as.func.name ? decl->as.func.name : "<fonction anonyme>";
    if (argc != decl->as.func.nparams)
        check_arity(in, name, decl->as.func.nparams, decl->as.func.nparams, argc, call, &decl->as.func.name_span,
                    signature_of(decl));
    if (in->nframes >= MAX_DEPTH) {
        Diag *d = diag_new(LEVEL_ERROR, "E0304", "récursion trop profonde (%d appels imbriqués)", MAX_DEPTH);
        diag_primary(d, call->span, "appel de trop");
        runtime_fail(in, diag_help(d, "une fonction récursive a besoin d'un cas de base qui s'arrête "
                                      "(ex. `if n <= 0 { return 0; }`) et chaque appel doit s'en rapprocher"));
    }
    check_interrupt(in, call->span);

    int mark = root_mark();
    ObjEnv *env = new_env(fn->closure); /* fn est une racine (le callee), donc sa closure aussi */
    root_push(OBJ_VAL(env));
    for (int i = 0; i < argc; i++) env_define(env, decl->as.func.params[i].name, argv[i]);

    in->frames[in->nframes++] = (Frame){ name, call->span };
    Exec r = run_block(in, &decl->as.func.body, env);
    in->nframes--;
    root_reset(mark);
    return r == EX_RETURN ? heap_result() : NIL_VAL;
}

static Value call_native(Interp *in, const NativeDef *def, int argc, Value *argv, Node *call) {
    check_arity(in, def->name, def->min_args, def->max_args, argc, call, NULL, def->signature);
    NativeErr err = { .arg_index = -1 };
    Value out = NIL_VAL;
    if (def->fn(argc, argv, &out, &err)) return out;
    Span span = err.arg_index >= 0 && err.arg_index < argc ? call->as.call.args.items[err.arg_index]->span : call->span;
    Diag *d = diag_new(LEVEL_ERROR, err.code, "%s", err.message);
    diag_primary(d, span, "%s", err.label ? err.label : "");
    if (err.help) diag_help(d, "%s", err.help);
    runtime_fail(in, diag_note(d, "`%s` : %s", def->signature, def->doc));
}

static Value eval_call(Interp *in, Node *e, ObjEnv *env) {
    int mark = root_mark();
    Value callee = eval(in, e->as.call.callee, env);
    root_push(callee);
    int argc = e->as.call.args.count;
    for (int i = 0; i < argc; i++) {
        Value v = eval(in, e->as.call.args.items[i], env);
        root_push(v);
    }
    Value *argv = root_at(mark + 1); /* les arguments restent des racines pendant l'appel */
    Value result;
    if (IS_OBJ(callee, O_FUNCTION)) {
        result = call_function(in, AS_FUNCTION(callee), argc, argv, e);
    } else if (IS_OBJ(callee, O_NATIVE)) {
        result = call_native(in, AS_NATIVE(callee)->def, argc, argv, e);
    } else {
        Diag *d = diag_new(LEVEL_ERROR, "E0302", "%s n'est pas une fonction : on ne peut pas l'appeler", a_type(callee));
        diag_primary(d, e->as.call.callee->span, "ceci est %s", describe(callee));
        if (IS_STRING(callee) || IS_LIST(callee)) diag_help(d, "pour lire un élément, utilise des crochets : `valeur[0]`");
        runtime_fail(in, d);
    }
    root_reset(mark);
    return result;
}

/* ------------------------------------------------------------ expressions */
static Value eval(Interp *in, Node *e, ObjEnv *env) {
    switch (e->kind) {
    case N_NUMBER: return NUM_VAL(e->as.num);
    case N_STRING: return OBJ_VAL(new_string(e->as.str.chars, e->as.str.len));
    case N_TRUE: return BOOL_VAL(true);
    case N_FALSE: return BOOL_VAL(false);
    case N_NIL: return NIL_VAL;

    case N_VAR: {
        Value *slot = env_find(env, e->as.var.name);
        if (!slot) {
            Diag *d = diag_new(LEVEL_ERROR, "E0206", "`%s` n'a pas encore de valeur", e->as.var.name);
            diag_primary(d, e->span, "lue ici avant que sa déclaration soit exécutée");
            runtime_fail(in, diag_help(d, "assure-toi que `let %s = ...` s'exécute avant cet endroit", e->as.var.name));
        }
        return *slot;
    }
    case N_ASSIGN: {
        Value v = eval(in, e->as.assign.value, env);
        Value *slot = env_find(env, e->as.assign.name);
        if (!slot) {
            Diag *d = diag_new(LEVEL_ERROR, "E0206", "`%s` n'a pas encore été déclarée", e->as.assign.name);
            runtime_fail(in, diag_primary(d, e->as.assign.name_span, "affectée ici avant sa déclaration"));
        }
        *slot = v;
        return v;
    }
    case N_UNARY: {
        Value v = eval(in, e->as.unary.operand, env);
        if (e->as.unary.op == T_BANG) return BOOL_VAL(!is_truthy(v));
        if (v.type != V_NUM) {
            Diag *d = diag_new(LEVEL_ERROR, "E0300", "`-` ne s'applique qu'aux nombres, pas à %s", a_type(v));
            diag_primary(d, e->as.unary.op_span, "négation impossible");
            runtime_fail(in, diag_secondary(d, e->as.unary.operand->span, "ceci est %s", describe(v)));
        }
        return NUM_VAL(-v.as.n);
    }
    case N_LOGICAL: { /* court-circuit : la droite n'est évaluée que si nécessaire */
        Value left = eval(in, e->as.binary.left, env);
        if (e->as.binary.op == T_OR) return is_truthy(left) ? left : eval(in, e->as.binary.right, env);
        return is_truthy(left) ? eval(in, e->as.binary.right, env) : left;
    }
    case N_BINARY: {
        int mark = root_mark();
        Value a = eval(in, e->as.binary.left, env);
        root_push(a);
        Value b = eval(in, e->as.binary.right, env);
        root_push(b);
        Value r = binary_op(in, e, a, b);
        root_reset(mark);
        return r;
    }
    case N_CALL:
        return eval_call(in, e, env);
    case N_LIST: {
        int mark = root_mark();
        ObjList *l = new_list();
        root_push(OBJ_VAL(l));
        for (int i = 0; i < e->as.list.items.count; i++) {
            Value v = eval(in, e->as.list.items.items[i], env);
            list_append(l, v);
        }
        root_reset(mark);
        return OBJ_VAL(l);
    }
    case N_INDEX: {
        int mark = root_mark();
        Value t = eval(in, e->as.index.target, env);
        root_push(t);
        Value i = eval(in, e->as.index.index, env);
        int k = checked_index(in, e, t, i, false);
        Value r;
        if (IS_LIST(t)) {
            r = AS_LIST(t)->items[k];
        } else {
            ObjString *s = AS_STRING(t);
            int off = utf8_offset(s->chars, s->len, k);
            r = OBJ_VAL(new_string(s->chars + off, utf8_char_len((unsigned char)s->chars[off])));
        }
        root_reset(mark);
        return r;
    }
    case N_INDEX_ASSIGN: {
        int mark = root_mark();
        Value t = eval(in, e->as.index.target, env);
        root_push(t);
        Value i = eval(in, e->as.index.index, env);
        root_push(i);
        Value v = eval(in, e->as.index.value, env);
        int k = checked_index(in, e, t, i, true);
        AS_LIST(t)->items[k] = v;
        root_reset(mark);
        return v;
    }
    case N_FUNC:
        return OBJ_VAL(new_function(e, env));
    default:
        return NIL_VAL;
    }
}

/* ------------------------------------------------------------ instructions */
static Exec exec(Interp *in, Node *s, ObjEnv *env) {
    switch (s->kind) {
    case N_LET: {
        Value v = eval(in, s->as.let.init, env);
        env_define(env, s->as.let.name, v);
        return EX_NORMAL;
    }
    case N_FNDECL: /* déjà définie par hoist() */
        return EX_NORMAL;
    case N_EXPR_STMT:
        eval(in, s->as.expr.expr, env);
        return EX_NORMAL;
    case N_BLOCK: {
        int mark = root_mark();
        ObjEnv *inner = new_env(env);
        root_push(OBJ_VAL(inner));
        Exec r = run_block(in, &s->as.block.stmts, inner);
        root_reset(mark);
        return r;
    }
    case N_IF:
        if (is_truthy(eval(in, s->as.if_.cond, env))) return exec(in, s->as.if_.then, env);
        if (s->as.if_.other) return exec(in, s->as.if_.other, env);
        return EX_NORMAL;
    case N_WHILE:
        while (is_truthy(eval(in, s->as.while_.cond, env))) {
            check_interrupt(in, s->as.while_.cond->span);
            if (exec(in, s->as.while_.body, env) == EX_RETURN) return EX_RETURN;
        }
        return EX_NORMAL;
    case N_RETURN:
        heap_set_result(s->as.ret.value ? eval(in, s->as.ret.value, env) : NIL_VAL);
        return EX_RETURN;
    default:
        return EX_NORMAL;
    }
}

Diag *interp_run(Interp *in, NodeList *program, Value *last, bool *has_last) {
    const int mark = root_mark();
    in->nframes = 0;
    *has_last = false;
    interrupted = 0;
    void (*const previous)(int) = signal(SIGINT, on_sigint);
    if (setjmp(in->on_error) != 0) {
        signal(SIGINT, previous);
        root_reset(mark);
        in->nframes = 0;
        *has_last = false;
        return in->error;
    }
    hoist(program, in->globals);
    for (int i = 0; i < program->count; i++) {
        Node *s = program->items[i];
        NodeKind k = s->kind == N_EXPR_STMT ? s->as.expr.expr->kind : s->kind;
        if (s->kind == N_EXPR_STMT && k != N_ASSIGN && k != N_INDEX_ASSIGN) {
            *last = eval(in, s->as.expr.expr, in->globals);
            *has_last = true;
        } else {
            exec(in, s, in->globals);
            *has_last = false;
        }
    }
    signal(SIGINT, previous);
    return NULL;
}
