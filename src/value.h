/* Valeurs d'exécution et gestion de la mémoire.
 *
 * Une Value est petite et copiée partout (nil, booléen, nombre, ou pointeur
 * vers un objet). Les objets (textes, listes, fonctions, portées) vivent sur
 * le tas et sont libérés par un ramasse-miettes « mark & sweep » :
 *   1. mark  : on part des racines (variables globales, valeurs temporaires
 *              en cours de calcul) et on marque tout ce qui est atteignable ;
 *   2. sweep : on libère tout objet non marqué.
 *
 * Règle d'or de l'interpréteur : une Value tenue dans une variable C pendant
 * qu'on crée un nouvel objet DOIT être poussée sur la pile des racines
 * (root_push), sinon le GC peut la libérer sous nos pieds. Pour traquer les
 * oublis : NOVA_GC_STRESS=1 déclenche un GC à chaque allocation.
 */
#ifndef NOVA_VALUE_H
#define NOVA_VALUE_H

#include "ast.h"

typedef struct Obj Obj;

typedef enum { V_NIL, V_BOOL, V_NUM, V_OBJ } VType;

typedef struct {
    VType type;
    union {
        bool b;
        double n;
        Obj *o;
    } as;
} Value;

typedef enum { O_STRING, O_LIST, O_FUNCTION, O_NATIVE, O_ENV } OType;

struct Obj {
    OType type;
    bool marked;
    Obj *next; /* tous les objets sont chaînés pour le sweep */
};

typedef struct {
    Obj obj;
    int len;      /* en octets */
    char chars[]; /* terminé par \0 */
} ObjString;

typedef struct {
    Obj obj;
    Value *items;
    int count, cap;
} ObjList;

typedef struct ObjEnv {
    Obj obj;
    struct ObjEnv *parent;
    const char **names;
    Value *values;
    int count, cap;
} ObjEnv;

typedef struct {
    Obj obj;
    Node *decl; /* nœud N_FUNC */
    ObjEnv *closure;
} ObjFunction;

typedef struct NativeDef NativeDef;

typedef struct {
    Obj obj;
    const NativeDef *def;
} ObjNative;

#define NIL_VAL ((Value){ V_NIL, { .b = false } })
#define BOOL_VAL(x) ((Value){ V_BOOL, { .b = (x) } })
#define NUM_VAL(x) ((Value){ V_NUM, { .n = (x) } })
#define OBJ_VAL(x) ((Value){ V_OBJ, { .o = (Obj *)(x) } })

#define IS_OBJ(v, t) ((v).type == V_OBJ && (v).as.o->type == (t))
#define IS_STRING(v) IS_OBJ(v, O_STRING)
#define IS_LIST(v) IS_OBJ(v, O_LIST)
#define AS_STRING(v) ((ObjString *)(v).as.o)
#define AS_LIST(v) ((ObjList *)(v).as.o)
#define AS_FUNCTION(v) ((ObjFunction *)(v).as.o)
#define AS_NATIVE(v) ((ObjNative *)(v).as.o)

/* ---- tas et GC ---- */
void heap_init(ObjEnv **globals_out);
void root_push(Value v);
int root_mark(void);        /* hauteur actuelle de la pile des racines */
void root_reset(int mark);  /* revient à cette hauteur */
Value *root_at(int index);
void heap_set_result(Value v); /* garde la valeur de retour en vie */
Value heap_result(void);

ObjString *new_string(const char *chars, int len);
ObjString *new_string_concat(ObjString *a, ObjString *b);
ObjList *new_list(void);
ObjEnv *new_env(ObjEnv *parent);
ObjFunction *new_function(Node *decl, ObjEnv *closure);
ObjNative *new_native(const NativeDef *def);

void list_append(ObjList *l, Value v);
void env_define(ObjEnv *env, const char *name, Value v);
Value *env_find(ObjEnv *env, const char *name);

/* ---- description des valeurs, pour les humains ---- */
bool is_truthy(Value v);
bool values_equal(Value a, Value b);
const char *type_name(Value v);
const char *a_type(Value v);                 /* « un texte », « une liste »… */
void value_repr(StrBuf *sb, Value v);        /* forme « code » : "texte" entre guillemets */
void value_stringify(StrBuf *sb, Value v);   /* forme affichée : texte brut */
char *describe(Value v);                     /* « un texte ("17") » */
const char *function_name(Value v);

#endif
