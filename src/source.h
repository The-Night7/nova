/* Texte source et positions.
 *
 * Tout commence ici : chaque token, chaque nœud de l'AST et chaque erreur
 * porte un Span (début/fin, en octets, dans le texte). C'est ce qui permet
 * ensuite de souligner précisément le code fautif.
 */
#ifndef NOVA_SOURCE_H
#define NOVA_SOURCE_H

#include "common.h"

typedef struct {
    const char *name;
    const char *text;
    int len;
    int *line_starts; /* octet de début de chaque ligne */
    int nlines;
} Source;

typedef struct {
    const Source *src;
    int start, end;
} Span;

Source *source_new(const char *name, const char *text, int len);

void span_line_col(Span s, int *line, int *col_byte); /* à partir de 1 */
int span_line(Span s);
int span_end_line(Span s);
Span span_to(Span a, Span b);  /* span couvrant a et b */
Span span_at_end(Span s);      /* span vide juste après s */
char *span_location(Span s);   /* "fichier:ligne:colonne" (colonne en caractères) */
const char *source_line(const Source *src, int line, int *len);

#endif
