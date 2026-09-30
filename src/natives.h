/* Fonctions natives (fournies par l'interpréteur, écrites en C).
 *
 * Ajouter une native = écrire une fonction au format NativeFn dans natives.c
 * et l'ajouter au tableau NATIVES. Pour signaler une erreur : remplir
 * `err` (code, message, étiquette, aide, argument fautif) et renvoyer false ;
 * l'interpréteur en fera un diagnostic complet pointant sur l'argument.
 */
#ifndef NOVA_NATIVES_H
#define NOVA_NATIVES_H

#include "value.h"

typedef struct {
    const char *code;
    char *message;
    char *label;
    char *help;    /* peut rester NULL */
    int arg_index; /* argument à souligner, -1 = tout l'appel */
} NativeErr;

typedef bool (*NativeFn)(int argc, Value *argv, Value *out, NativeErr *err);

struct NativeDef {
    const char *name;
    int min_args, max_args; /* max_args = -1 : nombre variable */
    const char *signature, *doc;
    NativeFn fn;
};

extern const NativeDef NATIVES[];
extern const int N_NATIVES;

#endif
