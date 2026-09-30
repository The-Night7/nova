#include "common.h"

static void out_of_memory(void) {
    fputs("nova : mémoire épuisée\n", stderr);
    exit(70);
}

void *xmalloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) out_of_memory();
    return p;
}

void *xrealloc(void *p, size_t n) {
    p = realloc(p, n ? n : 1);
    if (!p) out_of_memory();
    return p;
}

char *xstrndup(const char *s, size_t n) {
    char *r = xmalloc(n + 1);
    memcpy(r, s, n);
    r[n] = '\0';
    return r;
}

char *vfmt(const char *f, va_list ap) {
    va_list copy;
    va_copy(copy, ap);
    int n = vsnprintf(NULL, 0, f, copy);
    va_end(copy);
    char *buf = xmalloc((size_t)n + 1);
    vsnprintf(buf, (size_t)n + 1, f, ap);
    return buf;
}

char *fmt(const char *f, ...) {
    va_list ap;
    va_start(ap, f);
    char *r = vfmt(f, ap);
    va_end(ap);
    return r;
}

void sb_append(StrBuf *sb, const char *s, size_t n) {
    if (sb->len + n + 1 > sb->cap) {
        size_t cap = sb->cap ? sb->cap : 64;
        while (cap < sb->len + n + 1) cap *= 2;
        sb->data = xrealloc(sb->data, cap);
        sb->cap = cap;
    }
    memcpy(sb->data + sb->len, s, n);
    sb->len += n;
    sb->data[sb->len] = '\0';
}

void sb_puts(StrBuf *sb, const char *s) { sb_append(sb, s, strlen(s)); }

void sb_printf(StrBuf *sb, const char *f, ...) {
    va_list ap;
    va_start(ap, f);
    char *s = vfmt(f, ap);
    va_end(ap);
    sb_puts(sb, s);
    free(s);
}

char *sb_take(StrBuf *sb) {
    if (!sb->data) sb_append(sb, "", 0);
    char *r = sb->data;
    sb->data = NULL;
    sb->len = sb->cap = 0;
    return r;
}

int utf8_char_len(unsigned char c) {
    if (c < 0x80) return 1;
    if ((c >> 5) == 0x6) return 2;
    if ((c >> 4) == 0xE) return 3;
    if ((c >> 3) == 0x1E) return 4;
    return 1;
}

int utf8_len(const char *s, int n) {
    int count = 0;
    for (int i = 0; i < n; i++)
        if (((unsigned char)s[i] & 0xC0) != 0x80) count++;
    return count;
}

int utf8_offset(const char *s, int n, int index) {
    int i = 0;
    for (int k = 0; k < index; k++) {
        if (i >= n) return -1;
        i += utf8_char_len((unsigned char)s[i]);
    }
    return i < n ? i : -1;
}
