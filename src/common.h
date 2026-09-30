/* Utilitaires de base : allocation, tableaux dynamiques, chaînes, UTF-8. */
#ifndef NOVA_COMMON_H
#define NOVA_COMMON_H

#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PRINTF_LIKE(f, a) __attribute__((format(printf, f, a)))

void *xmalloc(size_t n);
void *xrealloc(void *p, size_t n);
char *xstrndup(const char *s, size_t n);
char *fmt(const char *f, ...) PRINTF_LIKE(1, 2);
char *vfmt(const char *f, va_list ap);

/* Ajoute un élément à un tableau dynamique (arr, count, cap), en doublant sa capacité si besoin. */
#define DA_PUSH(arr, count, cap, item)                                        \
    do {                                                                      \
        if ((count) == (cap)) {                                               \
            (cap) = (cap) ? (cap) * 2 : 8;                                    \
            (arr) = xrealloc((arr), sizeof(*(arr)) * (size_t)(cap));          \
        }                                                                     \
        (arr)[(count)++] = (item);                                            \
    } while (0)

/* Tampon de texte extensible */
typedef struct {
    char *data;
    size_t len, cap;
} StrBuf;

void sb_append(StrBuf *sb, const char *s, size_t n);
void sb_puts(StrBuf *sb, const char *s);
void sb_printf(StrBuf *sb, const char *f, ...) PRINTF_LIKE(2, 3);
char *sb_take(StrBuf *sb); /* rend le texte (terminé par \0) et vide le tampon */

/* UTF-8 : le texte source est en octets, mais l'humain compte en caractères */
int utf8_char_len(unsigned char lead);
int utf8_len(const char *s, int nbytes);              /* nombre de caractères */
int utf8_offset(const char *s, int nbytes, int index); /* octet du index-ième caractère, -1 si hors limites */

#endif
