#include "natives.h"

#include <ctype.h>
#include <math.h>
#include <time.h>

/* Remplit err et renvoie false : raccourci pour signaler une erreur. */
static bool fail(NativeErr *err, const char *code, int arg, char *message, char *label, char *help) {
    err->code = code;
    err->message = message;
    err->label = label;
    err->help = help;
    err->arg_index = arg;
    return false;
}

static bool wrong_type(NativeErr *err, const char *fname, const char *expected, int arg, Value got) {
    return fail(err, "E0310", arg, fmt("`%s` attend %s comme argument n°%d", fname, expected, arg + 1),
                fmt("ceci est %s", describe(got)), NULL);
}

/* ---------------------------------------------------------------- entrées / sorties */
static bool n_print(int argc, Value *argv, Value *out, NativeErr *err) {
    (void)err;
    StrBuf sb = { 0 };
    for (int i = 0; i < argc; i++) {
        if (i) sb_puts(&sb, " ");
        value_stringify(&sb, argv[i]);
    }
    sb_puts(&sb, "\n");
    fputs(sb.data, stdout);
    free(sb.data);
    *out = NIL_VAL;
    return true;
}

static bool n_input(int argc, Value *argv, Value *out, NativeErr *err) {
    if (argc == 1) {
        if (!IS_STRING(argv[0])) return wrong_type(err, "input", "un texte", 0, argv[0]);
        fputs(AS_STRING(argv[0])->chars, stdout);
    }
    fflush(stdout);
    StrBuf sb = { 0 };
    int c;
    while ((c = getchar()) != EOF && c != '\n') {
        char ch = (char)c;
        sb_append(&sb, &ch, 1);
    }
    if (c == EOF && sb.len == 0) {
        *out = NIL_VAL;
        return true;
    }
    if (sb.len && sb.data[sb.len - 1] == '\r') sb.len--;
    *out = OBJ_VAL(new_string(sb.data ? sb.data : "", (int)sb.len));
    free(sb.data);
    return true;
}

/* ---------------------------------------------------------------- conversions */
static bool n_str(int argc, Value *argv, Value *out, NativeErr *err) {
    (void)argc, (void)err;
    StrBuf sb = { 0 };
    value_stringify(&sb, argv[0]);
    *out = OBJ_VAL(new_string(sb.data, (int)sb.len));
    free(sb.data);
    return true;
}

/* Accepte uniquement : [espaces] [+-] chiffres [. chiffres] [espaces] */
static bool parse_number(const char *s, double *result) {
    const char *p = s;
    while (isspace((unsigned char)*p)) p++;
    const char *start = p;
    if (*p == '+' || *p == '-') p++;
    if (!isdigit((unsigned char)*p)) return false;
    while (isdigit((unsigned char)*p)) p++;
    if (*p == '.') {
        p++;
        if (!isdigit((unsigned char)*p)) return false;
        while (isdigit((unsigned char)*p)) p++;
    }
    const char *end = p;
    while (isspace((unsigned char)*p)) p++;
    if (*p) return false;
    char *copy = xstrndup(start, (size_t)(end - start));
    *result = strtod(copy, NULL);
    free(copy);
    return true;
}

static bool n_num(int argc, Value *argv, Value *out, NativeErr *err) {
    (void)argc;
    if (argv[0].type == V_NUM) {
        *out = argv[0];
        return true;
    }
    if (!IS_STRING(argv[0])) return wrong_type(err, "num", "un texte", 0, argv[0]);
    ObjString *s = AS_STRING(argv[0]);
    double n;
    if (parse_number(s->chars, &n)) {
        *out = NUM_VAL(n);
        return true;
    }
    char *help = NULL;
    if (strchr(s->chars, ',')) {
        char *fixed = xstrndup(s->chars, (size_t)s->len);
        for (char *c = fixed; *c; c++)
            if (*c == ',') *c = '.';
        help = fmt("les décimales s'écrivent avec un point : \"%s\"", fixed);
        free(fixed);
    } else {
        bool blank = true;
        for (int i = 0; i < s->len; i++)
            if (!isspace((unsigned char)s->chars[i])) blank = false;
        if (blank) help = fmt("le texte est vide : vérifie la valeur avant de la convertir");
    }
    StrBuf sb = { 0 };
    value_repr(&sb, argv[0]);
    return fail(err, "E0305", 0, fmt("impossible de convertir %s en nombre", sb.data),
                fmt("ce texte ne représente pas un nombre"), help);
}

static bool n_type(int argc, Value *argv, Value *out, NativeErr *err) {
    (void)argc, (void)err;
    const char *t = type_name(argv[0]);
    *out = OBJ_VAL(new_string(t, (int)strlen(t)));
    return true;
}

/* ---------------------------------------------------------------- listes et textes */
static bool n_len(int argc, Value *argv, Value *out, NativeErr *err) {
    (void)argc;
    if (IS_STRING(argv[0])) *out = NUM_VAL(utf8_len(AS_STRING(argv[0])->chars, AS_STRING(argv[0])->len));
    else if (IS_LIST(argv[0])) *out = NUM_VAL(AS_LIST(argv[0])->count);
    else return wrong_type(err, "len", "un texte ou une liste", 0, argv[0]);
    return true;
}

static bool n_push(int argc, Value *argv, Value *out, NativeErr *err) {
    (void)argc;
    if (!IS_LIST(argv[0])) return wrong_type(err, "push", "une liste", 0, argv[0]);
    list_append(AS_LIST(argv[0]), argv[1]);
    *out = argv[0];
    return true;
}

static bool n_pop(int argc, Value *argv, Value *out, NativeErr *err) {
    (void)argc;
    if (!IS_LIST(argv[0])) return wrong_type(err, "pop", "une liste", 0, argv[0]);
    ObjList *l = AS_LIST(argv[0]);
    if (l->count == 0)
        return fail(err, "E0311", 0, fmt("`pop` appelée sur une liste vide"), fmt("cette liste est vide"),
                    fmt("vérifie que `len(liste) > 0` avant d'appeler `pop`"));
    *out = l->items[--l->count];
    return true;
}

/* ---------------------------------------------------------------- divers */
static bool n_clock(int argc, Value *argv, Value *out, NativeErr *err) {
    (void)argc, (void)argv, (void)err;
    struct timespec ts;
    timespec_get(&ts, TIME_UTC);
    *out = NUM_VAL((double)ts.tv_sec + (double)ts.tv_nsec / 1e9);
    return true;
}

const NativeDef NATIVES[] = {
    { "print", 0, -1, "print(...valeurs)", "affiche les valeurs, séparées par des espaces", n_print },
    { "input", 0, 1, "input(invite?)", "affiche l'invite, puis renvoie la ligne tapée (un texte)", n_input },
    { "str", 1, 1, "str(valeur)", "convertit n'importe quelle valeur en texte", n_str },
    { "num", 1, 1, "num(texte)", "convertit un texte en nombre : num(\"42\") vaut 42", n_num },
    { "type", 1, 1, "type(valeur)", "nom du type : \"nombre\", \"texte\", \"liste\"…", n_type },
    { "len", 1, 1, "len(valeur)", "nombre d'éléments d'une liste, ou de caractères d'un texte", n_len },
    { "push", 2, 2, "push(liste, element)", "ajoute un élément à la fin de la liste, et la renvoie", n_push },
    { "pop", 1, 1, "pop(liste)", "retire et renvoie le dernier élément de la liste", n_pop },
    { "clock", 0, 0, "clock()", "nombre de secondes écoulées (pour mesurer des durées)", n_clock },
};

const int N_NATIVES = (int)(sizeof NATIVES / sizeof NATIVES[0]);
