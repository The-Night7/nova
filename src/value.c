#include "value.h"

#include <math.h>

#include "natives.h"

#define ROOTS_MAX 65536
#define GC_MIN_BYTES (1024 * 1024)

static struct {
    Obj *objects;        /* liste chaînée de tous les objets vivants */
    size_t bytes, next_gc;
    Value roots[ROOTS_MAX];
    int nroots;
    ObjEnv *globals;
    Value result;        /* dernière valeur renvoyée par une fonction */
    bool stress;
} heap;

/* ------------------------------------------------------------ racines */
void root_push(Value v) {
    if (heap.nroots == ROOTS_MAX) {
        fputs("nova : trop de valeurs temporaires (pile des racines pleine)\n", stderr);
        exit(70);
    }
    heap.roots[heap.nroots++] = v;
}
int root_mark(void) { return heap.nroots; }
void root_reset(int mark) { heap.nroots = mark; }
Value *root_at(int index) { return &heap.roots[index]; }
void heap_set_result(Value v) { heap.result = v; }
Value heap_result(void) { return heap.result; }

/* ------------------------------------------------------------ mark & sweep */
static void mark_obj(Obj *o);

static void mark_value(Value v) {
    if (v.type == V_OBJ) mark_obj(v.as.o);
}

static void mark_obj(Obj *o) {
    if (o == NULL || o->marked) return;
    o->marked = true;
    switch (o->type) {
    case O_LIST: {
        ObjList *l = (ObjList *)o;
        for (int i = 0; i < l->count; i++) mark_value(l->items[i]);
        break;
    }
    case O_ENV: {
        ObjEnv *e = (ObjEnv *)o;
        mark_obj((Obj *)e->parent);
        for (int i = 0; i < e->count; i++) mark_value(e->values[i]);
        break;
    }
    case O_FUNCTION:
        mark_obj((Obj *)((ObjFunction *)o)->closure);
        break;
    case O_STRING:
    case O_NATIVE:
        break;
    }
}

static size_t obj_size(Obj *o) {
    switch (o->type) {
    case O_STRING: return sizeof(ObjString) + (size_t)((ObjString *)o)->len + 1;
    case O_LIST: return sizeof(ObjList) + sizeof(Value) * (size_t)((ObjList *)o)->cap;
    case O_ENV: return sizeof(ObjEnv) + (sizeof(Value) + sizeof(char *)) * (size_t)((ObjEnv *)o)->cap;
    case O_FUNCTION: return sizeof(ObjFunction);
    case O_NATIVE: return sizeof(ObjNative);
    }
    return 0;
}

static void free_obj(Obj *o) {
    heap.bytes -= obj_size(o);
    if (o->type == O_LIST) free(((ObjList *)o)->items);
    if (o->type == O_ENV) {
        free(((ObjEnv *)o)->names);
        free(((ObjEnv *)o)->values);
    }
    free(o);
}

static void collect(void) {
    for (int i = 0; i < heap.nroots; i++) mark_value(heap.roots[i]);
    mark_obj((Obj *)heap.globals);
    mark_value(heap.result);

    Obj **link = &heap.objects;
    while (*link) {
        Obj *o = *link;
        if (o->marked) {
            o->marked = false;
            link = &o->next;
        } else {
            *link = o->next;
            free_obj(o);
        }
    }
    heap.next_gc = heap.bytes * 2 > GC_MIN_BYTES ? heap.bytes * 2 : GC_MIN_BYTES;
}

static Obj *allocate(size_t size, OType type) {
    if (heap.stress || heap.bytes > heap.next_gc) collect();
    Obj *o = xmalloc(size);
    o->type = type;
    o->marked = false;
    o->next = heap.objects;
    heap.objects = o;
    heap.bytes += size;
    return o;
}

void heap_init(ObjEnv **globals_out) {
    heap.next_gc = GC_MIN_BYTES;
    heap.stress = getenv("NOVA_GC_STRESS") != NULL;
    heap.result = NIL_VAL;
    heap.globals = new_env(NULL);
    for (int i = 0; i < N_NATIVES; i++)
        env_define(heap.globals, NATIVES[i].name, OBJ_VAL(new_native(&NATIVES[i])));
    *globals_out = heap.globals;
}

/* ------------------------------------------------------------ objets */
ObjString *new_string(const char *chars, int len) {
    ObjString *s = (ObjString *)allocate(sizeof(ObjString) + (size_t)len + 1, O_STRING);
    s->len = len;
    memcpy(s->chars, chars, (size_t)len);
    s->chars[len] = '\0';
    return s;
}

ObjString *new_string_concat(ObjString *a, ObjString *b) {
    /* a et b doivent être des racines : l'allocation peut déclencher le GC */
    int len = a->len + b->len;
    ObjString *s = (ObjString *)allocate(sizeof(ObjString) + (size_t)len + 1, O_STRING);
    s->len = len;
    memcpy(s->chars, a->chars, (size_t)a->len);
    memcpy(s->chars + a->len, b->chars, (size_t)b->len);
    s->chars[len] = '\0';
    return s;
}

ObjList *new_list(void) {
    ObjList *l = (ObjList *)allocate(sizeof(ObjList), O_LIST);
    l->items = NULL;
    l->count = l->cap = 0;
    return l;
}

ObjEnv *new_env(ObjEnv *parent) {
    ObjEnv *e = (ObjEnv *)allocate(sizeof(ObjEnv), O_ENV);
    e->parent = parent;
    e->names = NULL;
    e->values = NULL;
    e->count = e->cap = 0;
    return e;
}

ObjFunction *new_function(Node *decl, ObjEnv *closure) {
    ObjFunction *f = (ObjFunction *)allocate(sizeof(ObjFunction), O_FUNCTION);
    f->decl = decl;
    f->closure = closure;
    return f;
}

ObjNative *new_native(const NativeDef *def) {
    ObjNative *n = (ObjNative *)allocate(sizeof(ObjNative), O_NATIVE);
    n->def = def;
    return n;
}

/* Les tableaux internes grandissent sans déclencher de GC (pas de nouvel objet). */
void list_append(ObjList *l, Value v) {
    if (l->count == l->cap) {
        int cap = l->cap ? l->cap * 2 : 8;
        l->items = xrealloc(l->items, sizeof(Value) * (size_t)cap);
        heap.bytes += sizeof(Value) * (size_t)(cap - l->cap);
        l->cap = cap;
    }
    l->items[l->count++] = v;
}

void env_define(ObjEnv *env, const char *name, Value v) {
    for (int i = 0; i < env->count; i++) {
        if (strcmp(env->names[i], name) == 0) { /* redéfinition (REPL) */
            env->values[i] = v;
            return;
        }
    }
    if (env->count == env->cap) {
        int cap = env->cap ? env->cap * 2 : 4;
        env->names = xrealloc(env->names, sizeof(char *) * (size_t)cap);
        env->values = xrealloc(env->values, sizeof(Value) * (size_t)cap);
        heap.bytes += (sizeof(Value) + sizeof(char *)) * (size_t)(cap - env->cap);
        env->cap = cap;
    }
    env->names[env->count] = name;
    env->values[env->count++] = v;
}

Value *env_find(ObjEnv *env, const char *name) {
    for (; env; env = env->parent)
        for (int i = 0; i < env->count; i++)
            if (strcmp(env->names[i], name) == 0) return &env->values[i];
    return NULL;
}

/* ------------------------------------------------------------ descriptions */
bool is_truthy(Value v) { return !(v.type == V_NIL || (v.type == V_BOOL && !v.as.b)); }

static bool equal_depth(Value a, Value b, int depth) {
    if (a.type != b.type) return false;
    switch (a.type) {
    case V_NIL: return true;
    case V_BOOL: return a.as.b == b.as.b;
    case V_NUM: return a.as.n == b.as.n;
    case V_OBJ: break;
    }
    if (a.as.o == b.as.o) return true;
    if (a.as.o->type != b.as.o->type || depth > 64) return false;
    if (IS_STRING(a)) {
        ObjString *x = AS_STRING(a), *y = AS_STRING(b);
        return x->len == y->len && memcmp(x->chars, y->chars, (size_t)x->len) == 0;
    }
    if (IS_LIST(a)) {
        ObjList *x = AS_LIST(a), *y = AS_LIST(b);
        if (x->count != y->count) return false;
        for (int i = 0; i < x->count; i++)
            if (!equal_depth(x->items[i], y->items[i], depth + 1)) return false;
        return true;
    }
    return false;
}

bool values_equal(Value a, Value b) { return equal_depth(a, b, 0); }

const char *type_name(Value v) {
    switch (v.type) {
    case V_NIL: return "nil";
    case V_BOOL: return "booléen";
    case V_NUM: return "nombre";
    case V_OBJ: break;
    }
    switch (v.as.o->type) {
    case O_STRING: return "texte";
    case O_LIST: return "liste";
    case O_FUNCTION:
    case O_NATIVE: return "fonction";
    case O_ENV: return "portée";
    }
    return "?";
}

const char *a_type(Value v) {
    const char *t = type_name(v);
    if (strcmp(t, "nil") == 0) return "la valeur nil";
    if (strcmp(t, "liste") == 0) return "une liste";
    if (strcmp(t, "fonction") == 0) return "une fonction";
    if (strcmp(t, "texte") == 0) return "un texte";
    if (strcmp(t, "nombre") == 0) return "un nombre";
    if (strcmp(t, "booléen") == 0) return "un booléen";
    return t;
}

const char *function_name(Value v) {
    if (IS_OBJ(v, O_NATIVE)) return AS_NATIVE(v)->def->name;
    const char *name = AS_FUNCTION(v)->decl->as.func.name;
    return name ? name : "<fonction anonyme>";
}

static void num_repr(StrBuf *sb, double n) {
    if (isnan(n)) sb_puts(sb, "nan");
    else if (isinf(n)) sb_puts(sb, n > 0 ? "inf" : "-inf");
    else if (n == 0) sb_puts(sb, "0");
    else if (n == floor(n) && fabs(n) < 1e15) sb_printf(sb, "%.0f", n);
    else sb_printf(sb, "%.15g", n);
}

static void repr_rec(StrBuf *sb, Value v, const ObjList **seen, int nseen) {
    switch (v.type) {
    case V_NIL: sb_puts(sb, "nil"); return;
    case V_BOOL: sb_puts(sb, v.as.b ? "true" : "false"); return;
    case V_NUM: num_repr(sb, v.as.n); return;
    case V_OBJ: break;
    }
    switch (v.as.o->type) {
    case O_STRING: {
        ObjString *s = AS_STRING(v);
        sb_puts(sb, "\"");
        for (int i = 0; i < s->len; i++) {
            char c = s->chars[i];
            if (c == '"') sb_puts(sb, "\\\"");
            else if (c == '\\') sb_puts(sb, "\\\\");
            else if (c == '\n') sb_puts(sb, "\\n");
            else if (c == '\t') sb_puts(sb, "\\t");
            else sb_append(sb, &c, 1);
        }
        sb_puts(sb, "\"");
        return;
    }
    case O_LIST: {
        ObjList *l = AS_LIST(v);
        for (int i = 0; i < nseen; i++)
            if (seen[i] == l) { sb_puts(sb, "[...]"); return; }
        if (nseen >= 64) { sb_puts(sb, "[...]"); return; }
        seen[nseen] = l;
        sb_puts(sb, "[");
        for (int i = 0; i < l->count; i++) {
            if (i) sb_puts(sb, ", ");
            repr_rec(sb, l->items[i], seen, nseen + 1);
        }
        sb_puts(sb, "]");
        return;
    }
    case O_FUNCTION:
    case O_NATIVE:
        sb_printf(sb, "<fn %s>", function_name(v));
        return;
    case O_ENV:
        sb_puts(sb, "<portée>");
        return;
    }
}

void value_repr(StrBuf *sb, Value v) {
    const ObjList *seen[64];
    repr_rec(sb, v, seen, 0);
}

void value_stringify(StrBuf *sb, Value v) {
    if (IS_STRING(v)) sb_append(sb, AS_STRING(v)->chars, (size_t)AS_STRING(v)->len);
    else value_repr(sb, v);
}

char *describe(Value v) {
    if (v.type == V_NIL) return fmt("la valeur `nil`");
    StrBuf sb = { 0 };
    value_repr(&sb, v);
    char *preview = sb_take(&sb);
    int len = (int)strlen(preview);
    if (utf8_len(preview, len) > 30) { /* coupe proprement à 29 caractères */
        int cut = utf8_offset(preview, len, 29);
        preview[cut] = '\0';
        char *longer = fmt("%s…", preview);
        free(preview);
        preview = longer;
    }
    char *r = fmt("%s (%s)", a_type(v), preview);
    free(preview);
    return r;
}
