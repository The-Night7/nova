#include "source.h"

Source *source_new(const char *name, const char *text, int len) {
    Source *s = xmalloc(sizeof *s);
    s->name = name;
    s->text = text;
    s->len = len;
    s->line_starts = NULL;
    s->nlines = 0;
    int cap = 0;
    DA_PUSH(s->line_starts, s->nlines, cap, 0);
    for (int i = 0; i < len; i++)
        if (text[i] == '\n') DA_PUSH(s->line_starts, s->nlines, cap, i + 1);
    return s;
}

/* Recherche dichotomique de la ligne contenant l'octet `offset`. */
static int line_index(const Source *s, int offset) {
    int lo = 0, hi = s->nlines - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) / 2;
        if (s->line_starts[mid] <= offset) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

void span_line_col(Span s, int *line, int *col) {
    int li = line_index(s.src, s.start);
    *line = li + 1;
    *col = s.start - s.src->line_starts[li] + 1;
}

int span_line(Span s) {
    int line, col;
    span_line_col(s, &line, &col);
    return line;
}

int span_end_line(Span s) {
    if (s.end > s.start) s.start = s.end - 1;
    return span_line(s);
}

Span span_to(Span a, Span b) {
    Span r = a;
    r.start = a.start < b.start ? a.start : b.start;
    r.end = a.end > b.end ? a.end : b.end;
    return r;
}

Span span_at_end(Span s) {
    s.start = s.end;
    return s;
}

const char *source_line(const Source *src, int line, int *len) {
    int start = src->line_starts[line - 1];
    int end = start;
    while (end < src->len && src->text[end] != '\n') end++;
    if (end > start && src->text[end - 1] == '\r') end--;
    *len = end - start;
    return src->text + start;
}

char *span_location(Span s) {
    int line, col, len;
    span_line_col(s, &line, &col);
    const char *text = source_line(s.src, line, &len);
    int prefix = col - 1 < len ? col - 1 : len;
    return fmt("%s:%d:%d", s.src->name, line, utf8_len(text, prefix) + 1);
}
