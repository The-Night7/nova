#define _POSIX_C_SOURCE 200809L
#include "diag.h"

#include <unistd.h>

/* ------------------------------------------------------------ construction */
Diag *diag_new(int level, const char *code, const char *f, ...) {
    Diag *d = xmalloc(sizeof *d);
    memset(d, 0, sizeof *d);
    d->level = level;
    d->code = code;
    va_list ap;
    va_start(ap, f);
    d->message = vfmt(f, ap);
    va_end(ap);
    return d;
}

static Diag *add_label(Diag *d, Span s, bool primary, const char *f, va_list ap) {
    Label l = { s, vfmt(f, ap), primary };
    DA_PUSH(d->labels, d->nlabels, d->caplabels, l);
    return d;
}

Diag *diag_primary(Diag *d, Span s, const char *f, ...) {
    va_list ap;
    va_start(ap, f);
    add_label(d, s, true, f, ap);
    va_end(ap);
    return d;
}

Diag *diag_secondary(Diag *d, Span s, const char *f, ...) {
    va_list ap;
    va_start(ap, f);
    add_label(d, s, false, f, ap);
    va_end(ap);
    return d;
}

Diag *diag_note(Diag *d, const char *f, ...) {
    va_list ap;
    va_start(ap, f);
    char *s = vfmt(f, ap);
    va_end(ap);
    DA_PUSH(d->notes, d->nnotes, d->capnotes, s);
    return d;
}

Diag *diag_help(Diag *d, const char *f, ...) {
    va_list ap;
    va_start(ap, f);
    char *s = vfmt(f, ap);
    va_end(ap);
    DA_PUSH(d->helps, d->nhelps, d->caphelps, s);
    return d;
}

void diaglist_push(DiagList *l, Diag *d) { DA_PUSH(l->items, l->count, l->cap, d); }

int diaglist_count(const DiagList *l, int level) {
    int n = 0;
    for (int i = 0; i < l->count; i++)
        if (!l->items[i]->suppressed && l->items[i]->level == level) n++;
    return n;
}

bool diaglist_has(const DiagList *l, const char *code) {
    for (int i = 0; i < l->count; i++)
        if (strcmp(l->items[i]->code, code) == 0) return true;
    return false;
}

static const Label *main_label(const Diag *d) {
    for (int i = 0; i < d->nlabels; i++)
        if (d->labels[i].primary) return &d->labels[i];
    return d->nlabels ? &d->labels[0] : NULL;
}

/* ------------------------------------------------------------ rendu */
#define BOLD "1"
#define DIM "2"
#define RED "1;31"
#define YELLOW "1;33"
#define BLUE "1;34"
#define CYAN "1;36"

static bool g_color;

bool color_enabled(FILE *f) { return isatty(fileno(f)) && getenv("NO_COLOR") == NULL; }

static void put(FILE *o, const char *style, const char *text) {
    if (g_color && style) fprintf(o, "\x1b[%sm%s\x1b[0m", style, text);
    else fputs(text, o);
}

void print_styled(FILE *o, bool color, const char *style, const char *text) {
    bool saved = g_color;
    g_color = color;
    put(o, style, text);
    g_color = saved;
}

static void spaces(FILE *o, int n) {
    for (int i = 0; i < n; i++) fputc(' ', o);
}

static void gutter(FILE *o, int width) { /* "   | " */
    spaces(o, width + 1);
    put(o, BLUE, "|");
    fputc(' ', o);
}

static void gutter_empty(FILE *o, int width) { /* "   |" */
    spaces(o, width + 1);
    put(o, BLUE, "|");
    fputc('\n', o);
}

static void bullet(FILE *o, int width) { /* "   = " */
    spaces(o, width + 1);
    put(o, BLUE, "=");
    fputc(' ', o);
}

/* Largeur affichée d'un préfixe de ligne : caractères UTF-8, tabulations = 4 espaces. */
static int display_width(const char *s, int nbytes) {
    int w = 0;
    for (int i = 0; i < nbytes; i++) {
        if (s[i] == '\t') w += 4;
        else if (((unsigned char)s[i] & 0xC0) != 0x80) w++;
    }
    return w;
}

static int label_cmp(const void *a, const void *b) {
    const Label *x = a, *y = b;
    if (x->span.start != y->span.start) return x->span.start - y->span.start;
    return (int)y->primary - (int)x->primary;
}

static int digits(int n) {
    int d = 1;
    while (n >= 10) { n /= 10; d++; }
    return d;
}

void diag_render(const Diag *d, FILE *o, bool color) {
    g_color = color;
    const char *level_style = d->level == LEVEL_ERROR ? RED : YELLOW;
    char *head = fmt("%s[%s]", d->level == LEVEL_ERROR ? "erreur" : "avertissement", d->code);
    char *msg = fmt(": %s", d->message);
    put(o, level_style, head);
    put(o, BOLD, msg);
    fputc('\n', o);
    free(head);
    free(msg);

    int n = d->nlabels;
    Label *labels = xmalloc(sizeof(Label) * (size_t)(n ? n : 1));
    if (n) memcpy(labels, d->labels, sizeof(Label) * (size_t)n);
    qsort(labels, (size_t)n, sizeof(Label), label_cmp);

    int width = 1;
    for (int i = 0; i < n; i++) {
        int w = digits(span_line(labels[i].span));
        if (w > width) width = w;
    }

    if (n > 0) {
        char *loc = span_location(main_label(d)->span);
        spaces(o, width);
        put(o, BLUE, "-->");
        fprintf(o, " %s\n", loc);
        free(loc);
        gutter_empty(o, width);

        const Source *last_src = NULL;
        int last_line = -1;
        for (int i = 0; i < n; i++) {
            const Label *l = &labels[i];
            int line, col, len;
            span_line_col(l->span, &line, &col);
            const char *text = source_line(l->span.src, line, &len);

            if (last_src == l->span.src && line > last_line + 1) {
                put(o, BLUE, "...");
                fputc('\n', o);
            }
            if (!(last_src == l->span.src && line == last_line)) {
                char *num = fmt("%*d", width, line);
                put(o, BLUE, num);
                free(num);
                fputc(' ', o);
                put(o, BLUE, "|");
                fputc(' ', o);
                for (int k = 0; k < len; k++) {
                    if (text[k] == '\t') fputs("    ", o);
                    else fputc(text[k], o);
                }
                fputc('\n', o);
            }

            /* Soulignement : limité à la première ligne du span */
            int prefix = col - 1 < len ? col - 1 : len;
            int line_end = l->span.src->line_starts[line - 1] + len;
            int end = l->span.end < line_end ? l->span.end : line_end;
            int ulen = end > l->span.start ? utf8_len(text + prefix, end - l->span.start) : 0;
            if (ulen < 1) ulen = 1;

            StrBuf sb = { 0 };
            for (int k = 0; k < ulen; k++) sb_append(&sb, l->primary ? "^" : "-", 1);
            if (l->msg[0]) {
                sb_puts(&sb, " ");
                sb_puts(&sb, l->msg);
            }
            char *underline = sb_take(&sb);
            gutter(o, width);
            spaces(o, display_width(text, prefix));
            put(o, l->primary ? level_style : BLUE, underline);
            fputc('\n', o);
            free(underline);
            last_src = l->span.src;
            last_line = line;
        }
        gutter_empty(o, width);
    }
    free(labels);

    for (int i = 0; i < d->nnotes; i++) {
        bullet(o, width);
        put(o, BOLD, "note");
        fprintf(o, " : %s\n", d->notes[i]);
    }
    for (int i = 0; i < d->nhelps; i++) {
        bullet(o, width);
        put(o, CYAN, "aide");
        fprintf(o, " : %s\n", d->helps[i]);
    }

    if (d->ntrace > 0) {
        bullet(o, width);
        put(o, BOLD, "pile d'appels");
        fputs(" (du plus ancien au plus récent) :\n", o);
        int number = 1;
        for (int i = 0; i < d->ntrace;) {
            int j = i; /* regroupe les appels récursifs consécutifs */
            while (j < d->ntrace && strcmp(d->trace[j].name, d->trace[i].name) == 0) j++;
            char *loc = span_location(d->trace[i].call);
            spaces(o, width + 7);
            if (j - i > 1) fprintf(o, "%d. `%s` ×%d (récursion), appelée depuis %s\n", number, d->trace[i].name, j - i, loc);
            else fprintf(o, "%d. `%s`, appelée depuis %s\n", number, d->trace[i].name, loc);
            free(loc);
            number++;
            i = j;
        }
    }

    if (explanation_find(d->code)) {
        char *line = fmt("%*s = pour comprendre en détail : nova --explain %s", width, "", d->code);
        put(o, DIM, line);
        fputc('\n', o);
        free(line);
    }
}

typedef struct {
    Diag *d;
    int index;
} Sortable;

static int sortable_cmp(const void *a, const void *b) {
    const Sortable *x = a, *y = b;
    const Label *lx = main_label(x->d), *ly = main_label(y->d);
    int sx = lx ? lx->span.start : 0, sy = ly ? ly->span.start : 0;
    if (sx != sy) return sx - sy;
    return x->index - y->index; /* tri stable */
}

void diaglist_report(const DiagList *l, FILE *out, bool color) {
    Sortable *items = xmalloc(sizeof(Sortable) * (size_t)(l->count ? l->count : 1));
    for (int i = 0; i < l->count; i++) items[i] = (Sortable){ l->items[i], i };
    qsort(items, (size_t)l->count, sizeof(Sortable), sortable_cmp);
    for (int i = 0; i < l->count; i++) {
        if (items[i].d->suppressed) continue;
        diag_render(items[i].d, out, color);
        fputc('\n', out);
    }
    free(items);
}
