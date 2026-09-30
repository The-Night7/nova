/* Ligne de commande.
 *
 *   nova fichier.nv        exécute un fichier
 *   nova                   lance le REPL
 *   nova --explain E0200   explique un code d'erreur (sans code : liste)
 *   nova --no-color ...    désactive les couleurs
 */
#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>

#include "interp.h"
#include "parser.h"
#include "resolver.h"
#include "suggest.h"

static bool g_color;

/* Lexer -> parser -> resolver. Chaque étape ne s'exécute que si la précédente
 * n'a pas trouvé d'erreur (sinon elle se tromperait en cascade). */
static bool compile(Source *src, Resolver *r, DiagList *diags, NodeList *program) {
    TokenList tokens = { 0 };
    lex(src, &tokens, diags);
    if (diaglist_count(diags, LEVEL_ERROR)) return false;
    *program = parse_program(&tokens, diags);
    if (diaglist_count(diags, LEVEL_ERROR)) return false;
    resolver_run(r, program, diags);
    return diaglist_count(diags, LEVEL_ERROR) == 0;
}

static void summary(const DiagList *diags, const char *filename) {
    int errors = diaglist_count(diags, LEVEL_ERROR), warnings = diaglist_count(diags, LEVEL_WARNING);
    if (errors) {
        print_styled(stderr, g_color, "1;31", "arrêt");
        char *extra = warnings ? fmt(" (et %d avertissement%s)", warnings, warnings > 1 ? "s" : "") : fmt("%s", "");
        char *msg = fmt(" : %s contient %d erreur%s%s, rien n'a été exécuté\n", filename, errors, errors > 1 ? "s" : "", extra);
        print_styled(stderr, g_color, "1", msg);
        free(extra);
        free(msg);
    } else if (warnings) {
        char *msg = fmt("%d avertissement%s", warnings, warnings > 1 ? "s" : "");
        print_styled(stderr, g_color, "1;33", msg);
        fputs(" ; exécution…\n\n", stderr);
        free(msg);
    }
}

static char *read_file(const char *path, int *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    StrBuf sb = { 0 };
    char chunk[8192];
    size_t n;
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0) sb_append(&sb, chunk, n);
    fclose(f);
    *len = (int)sb.len;
    return sb_take(&sb);
}

static int run_file(const char *path) {
    int len;
    char *text = read_file(path, &len);
    if (!text) {
        print_styled(stderr, g_color, "1;31", "erreur");
        fprintf(stderr, " : impossible d'ouvrir `%s` (%s)\n", path, strerror(errno));
        return 1;
    }
    Source *src = source_new(path, text, len);
    Interp *in = interp_new();
    Resolver *resolver = resolver_new(false);
    DiagList diags = { 0 };
    NodeList program = { 0 };
    bool ok = compile(src, resolver, &diags, &program);
    diaglist_report(&diags, stderr, g_color);
    summary(&diags, path);
    if (!ok) return 1;

    Value last;
    bool has_last;
    Diag *err = interp_run(in, &program, &last, &has_last);
    fflush(stdout);
    if (err) {
        diag_render(err, stderr, g_color);
        return 1;
    }
    return 0;
}

static int explain(const char *code, bool color) {
    if (!code) {
        for (int i = 0; i < N_EXPLANATIONS; i++) {
            fputs("  ", stdout);
            print_styled(stdout, color, "1", EXPLANATIONS[i].code);
            printf("  %s\n", EXPLANATIONS[i].title);
        }
        return 0;
    }
    char upper[16] = { 0 };
    for (int i = 0; code[i] && i < 15; i++) upper[i] = (char)toupper((unsigned char)code[i]);
    const Explanation *x = explanation_find(upper);
    if (!x) {
        const char *codes[128];
        int n = 0;
        for (int i = 0; i < N_EXPLANATIONS && n < 128; i++) codes[n++] = EXPLANATIONS[i].code;
        const char *guess = closest(upper, codes, n);
        fprintf(stderr, "code inconnu : %s", upper);
        if (guess) fprintf(stderr, " (vouliez-vous dire %s ?)", guess);
        fputs(" — `nova --explain` sans code liste tous les codes\n", stderr);
        return 1;
    }
    char *title = fmt("%s — %s", x->code, x->title);
    print_styled(stdout, color, "1", title);
    printf("\n\n%s\n", x->text);
    free(title);
    return 0;
}

static bool blank(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (!isspace((unsigned char)s[i])) return false;
    return true;
}

static int repl(void) {
    Interp *in = interp_new();
    Resolver *resolver = resolver_new(true);
    print_styled(stdout, color_enabled(stdout) && g_color, "1", "Nova");
    puts(" — REPL. Ctrl+D pour quitter, `--explain CODE` pour comprendre une erreur.");

    StrBuf buffer = { 0 };
    int count = 0;
    for (;;) {
        fputs(buffer.len ? "  ... " : "nova> ", stdout);
        fflush(stdout);
        char *line = NULL;
        size_t cap = 0;
        ssize_t n = getline(&line, &cap, stdin);
        if (n < 0) {
            free(line);
            putchar('\n');
            return 0;
        }
        if (buffer.len == 0 && strncmp(line, "--explain", 9) == 0) {
            char *arg = strtok(line + 9, " \t\r\n");
            explain(arg, color_enabled(stdout) && g_color);
            free(line);
            continue;
        }
        sb_append(&buffer, line, (size_t)n);
        free(line);
        if (blank(buffer.data, buffer.len)) {
            buffer.len = 0;
            continue;
        }

        /* Confort : au REPL, `1 + 2` suffit, le `;` final est ajouté. */
        size_t len = buffer.len;
        while (len > 0 && isspace((unsigned char)buffer.data[len - 1])) len--;
        StrBuf text = { 0 };
        sb_append(&text, buffer.data, len);
        if (buffer.data[len - 1] != ';' && buffer.data[len - 1] != '}') sb_puts(&text, ";");

        char *name = fmt("<entrée %d>", ++count);
        Source *src = source_new(name, text.data, (int)text.len); /* gardé en vie : l'AST pointe dedans */
        DiagList diags = { 0 };
        NodeList program = { 0 };
        bool ok = compile(src, resolver, &diags, &program);
        if (diaglist_has(&diags, "E0103")) { /* bloc encore ouvert : on continue la saisie */
            count--;
            continue;
        }
        buffer.len = 0;
        diaglist_report(&diags, stderr, g_color);
        if (!ok) continue;

        Value last;
        bool has_last;
        Diag *err = interp_run(in, &program, &last, &has_last);
        fflush(stdout);
        if (err) {
            diag_render(err, stderr, g_color);
            fputc('\n', stderr);
        } else if (has_last && last.type != V_NIL) {
            StrBuf sb = { 0 };
            value_repr(&sb, last);
            puts(sb.data);
            free(sb.data);
        }
    }
}

static void usage(void) {
    puts("usage : nova [fichier.nv] [--no-color]\n"
         "        nova --explain [CODE]\n\n"
         "Sans fichier, lance le REPL.");
}

int main(int argc, char **argv) {
    const char *file = NULL, *code = NULL;
    bool no_color = false, explain_mode = false;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "--no-color") == 0) {
            no_color = true;
        } else if (strcmp(a, "--explain") == 0) {
            explain_mode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') code = argv[++i];
        } else if (strcmp(a, "-h") == 0 || strcmp(a, "--help") == 0) {
            usage();
            return 0;
        } else if (a[0] == '-' && a[1]) {
            fprintf(stderr, "option inconnue : %s\n", a);
            usage();
            return 2;
        } else {
            file = a;
        }
    }
    g_color = !no_color && color_enabled(stderr);
    if (explain_mode) return explain(code, !no_color && color_enabled(stdout));
    if (file) return run_file(file);
    return repl();
}
