/* Diagnostics : erreurs et avertissements structurés.
 *
 * Un diagnostic n'est pas une simple chaîne : il a un code (pour --explain),
 * des étiquettes qui soulignent le code (^^^ principale, --- secondaire),
 * des notes (pourquoi) et des aides (quoi faire). On le construit par appels
 * chaînés :
 *
 *   Diag *d = diag_new(LEVEL_ERROR, "E0200", "`%s` n'est pas défini", nom);
 *   diag_primary(d, span, "nom inconnu ici");
 *   diag_help(d, "vouliez-vous dire `%s` ?", proche);
 */
#ifndef NOVA_DIAG_H
#define NOVA_DIAG_H

#include "common.h"
#include "source.h"

enum { LEVEL_ERROR, LEVEL_WARNING };

typedef struct {
    Span span;
    char *msg;
    bool primary;
} Label;

typedef struct {
    const char *name;
    Span call;
} Frame; /* une entrée de la pile d'appels */

typedef struct {
    int level;
    const char *code;
    char *message;
    Label *labels;
    int nlabels, caplabels;
    char **notes;
    int nnotes, capnotes;
    char **helps;
    int nhelps, caphelps;
    Frame *trace;
    int ntrace;
    bool suppressed; /* retiré car une autre erreur l'explique déjà */
} Diag;

typedef struct {
    Diag **items;
    int count, cap;
} DiagList;

Diag *diag_new(int level, const char *code, const char *f, ...) PRINTF_LIKE(3, 4);
Diag *diag_primary(Diag *d, Span s, const char *f, ...) PRINTF_LIKE(3, 4);
Diag *diag_secondary(Diag *d, Span s, const char *f, ...) PRINTF_LIKE(3, 4);
Diag *diag_note(Diag *d, const char *f, ...) PRINTF_LIKE(2, 3);
Diag *diag_help(Diag *d, const char *f, ...) PRINTF_LIKE(2, 3);

void diaglist_push(DiagList *l, Diag *d);
int diaglist_count(const DiagList *l, int level);
bool diaglist_has(const DiagList *l, const char *code);

bool color_enabled(FILE *f);
void print_styled(FILE *o, bool color, const char *style, const char *text);
void diag_render(const Diag *d, FILE *out, bool color);
void diaglist_report(const DiagList *l, FILE *out, bool color);

/* explain.c : explications détaillées de chaque code */
typedef struct {
    const char *code, *title, *text;
} Explanation;
extern const Explanation EXPLANATIONS[];
extern const int N_EXPLANATIONS;
const Explanation *explanation_find(const char *code);

#endif
