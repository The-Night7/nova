#include "lexer.h"

#include <ctype.h>

static const char *TEXTS[T__COUNT] = {
    [T_EOF] = "fin du fichier", [T_NUMBER] = "nombre", [T_STRING] = "texte", [T_IDENT] = "nom",
    [T_LET] = "let", [T_FN] = "fn", [T_RETURN] = "return", [T_IF] = "if", [T_ELSE] = "else",
    [T_WHILE] = "while", [T_TRUE] = "true", [T_FALSE] = "false", [T_NIL] = "nil",
    [T_AND] = "and", [T_OR] = "or",
    [T_PLUS] = "+", [T_MINUS] = "-", [T_STAR] = "*", [T_SLASH] = "/", [T_PERCENT] = "%",
    [T_EQ] = "=", [T_EQEQ] = "==", [T_BANG] = "!", [T_BANGEQ] = "!=",
    [T_LT] = "<", [T_LE] = "<=", [T_GT] = ">", [T_GE] = ">=",
    [T_LPAREN] = "(", [T_RPAREN] = ")", [T_LBRACE] = "{", [T_RBRACE] = "}",
    [T_LBRACKET] = "[", [T_RBRACKET] = "]", [T_COMMA] = ",", [T_SEMI] = ";",
};

const char *tok_text(TokKind k) { return TEXTS[k]; }
bool tok_is_keyword(TokKind k) { return k >= T_LET && k <= T_OR; }

typedef struct {
    const Source *src;
    const char *t;
    int n;
    TokenList *out;
    DiagList *diags;
} Lexer;

static Span sp(const Lexer *L, int a, int b) { return (Span){ L->src, a, b }; }

static Token *emit(Lexer *L, TokKind kind, int a, int b) {
    Token tok = { .kind = kind, .span = sp(L, a, b) };
    DA_PUSH(L->out->items, L->out->count, L->out->cap, tok);
    return &L->out->items[L->out->count - 1];
}

static void report(Lexer *L, Diag *d) { diaglist_push(L->diags, d); }

/* Caractères Unicode trompeurs (copier-coller depuis un traitement de texte…) */
static const struct {
    const char *bytes, *hint;
} SPECIAL[] = {
    { "\xe2\x80\x9c", "ce sont des guillemets typographiques : utilise des guillemets droits \"" },
    { "\xe2\x80\x9d", "ce sont des guillemets typographiques : utilise des guillemets droits \"" },
    { "\xe2\x80\x98", "ce sont des apostrophes typographiques : les textes utilisent des guillemets droits \"" },
    { "\xe2\x80\x99", "ce sont des apostrophes typographiques : les textes utilisent des guillemets droits \"" },
    { "\xc2\xab", "les guillemets français ne sont pas reconnus : utilise des guillemets droits \"" },
    { "\xc2\xbb", "les guillemets français ne sont pas reconnus : utilise des guillemets droits \"" },
    { "\xc2\xa0", "c'est une espace insécable : remplace-la par une espace normale" },
};

static int special_utf8(const char *t, int i, int n, const char **hint) {
    for (size_t k = 0; k < sizeof SPECIAL / sizeof SPECIAL[0]; k++) {
        int len = (int)strlen(SPECIAL[k].bytes);
        if (i + len <= n && memcmp(t + i, SPECIAL[k].bytes, (size_t)len) == 0) {
            if (hint) *hint = SPECIAL[k].hint;
            return len;
        }
    }
    return 0;
}

/* Les octets >= 0x80 (lettres accentuées en UTF-8) sont acceptés dans les noms. */
static bool ident_start(const char *t, int i, int n) {
    unsigned char c = (unsigned char)t[i];
    return !special_utf8(t, i, n, NULL) && (isalpha(c) || c == '_' || c >= 0x80);
}

static bool ident_char(const char *t, int i, int n) {
    unsigned char c = (unsigned char)t[i];
    return !special_utf8(t, i, n, NULL) && (isalnum(c) || c == '_' || c >= 0x80);
}

static void lex_number(Lexer *L, int *pi) {
    const char *t = L->t;
    int n = L->n, start = *pi, i = *pi;
    while (i < n && isdigit((unsigned char)t[i])) i++;
    if (i + 1 < n && t[i] == '.' && isdigit((unsigned char)t[i + 1])) {
        i++;
        while (i < n && isdigit((unsigned char)t[i])) i++;
    }
    if (i + 1 < n && t[i] == '.' && isdigit((unsigned char)t[i + 1])) { /* 1.2.3 */
        while (i < n && (isdigit((unsigned char)t[i]) || t[i] == '.')) i++;
        Diag *d = diag_new(LEVEL_ERROR, "E0003", "nombre mal formé");
        report(L, diag_primary(d, sp(L, start, i), "un nombre ne peut avoir qu'un seul point décimal"));
        emit(L, T_NUMBER, start, i)->num = 0;
    } else if (i < n && ident_start(t, i, n)) { /* 2eme */
        while (i < n && ident_char(t, i, n)) i++;
        Diag *d = diag_new(LEVEL_ERROR, "E0005", "un nom ne peut pas commencer par un chiffre");
        diag_primary(d, sp(L, start, i), "nom invalide");
        report(L, diag_help(d, "commence le nom par une lettre ou `_`, par exemple `_%.*s`", i - start, t + start));
        emit(L, T_IDENT, start, i)->str = xstrndup(t + start, (size_t)(i - start));
    } else {
        char *text = xstrndup(t + start, (size_t)(i - start));
        emit(L, T_NUMBER, start, i)->num = strtod(text, NULL);
        free(text);
    }
    *pi = i;
}

static void lex_string(Lexer *L, int *pi) {
    const char *t = L->t;
    int n = L->n, start = *pi, i = *pi + 1;
    StrBuf sb = { 0 };
    bool closed = false;
    while (i < n && t[i] != '\n') {
        char ch = t[i];
        if (ch == '"') {
            closed = true;
            i++;
            break;
        }
        if (ch == '\\' && i + 1 < n && t[i + 1] != '\n') {
            char e = t[i + 1];
            int elen = utf8_char_len((unsigned char)e);
            switch (e) {
            case 'n': sb_append(&sb, "\n", 1); break;
            case 't': sb_append(&sb, "\t", 1); break;
            case '"': sb_append(&sb, "\"", 1); break;
            case '\\': sb_append(&sb, "\\", 1); break;
            default: {
                Diag *d = diag_new(LEVEL_ERROR, "E0004", "séquence d'échappement inconnue `\\%.*s`", elen, t + i + 1);
                diag_primary(d, sp(L, i, i + 1 + elen), "inconnue");
                report(L, diag_help(d, "séquences valides : \\n (retour à la ligne), \\t (tabulation), "
                                       "\\\" (guillemet), \\\\ (antislash)"));
            }
            }
            i += 1 + elen;
            continue;
        }
        sb_append(&sb, &t[i], 1);
        i++;
    }
    if (!closed) {
        Diag *d = diag_new(LEVEL_ERROR, "E0002", "texte non fermé");
        diag_primary(d, sp(L, start, i), "ce texte n'est jamais fermé");
        report(L, diag_help(d, "ajoute un `\"` à la fin du texte, sur la même ligne"));
    }
    Token *tok = emit(L, T_STRING, start, i);
    tok->slen = (int)sb.len;
    tok->str = sb_take(&sb);
    *pi = i;
}

static void lex_single_quoted(Lexer *L, int *pi) {
    const char *t = L->t;
    int n = L->n, start = *pi, j = *pi + 1;
    while (j < n && t[j] != '\'' && t[j] != '\n') j++;
    if (j < n && t[j] == '\'') { /* 'texte' : une seule erreur, et on continue comme si c'était "texte" */
        int len = j - start - 1;
        Diag *d = diag_new(LEVEL_ERROR, "E0001", "guillemets simples `'` non reconnus");
        diag_primary(d, sp(L, start, j + 1), "ne fait pas partie du langage");
        report(L, diag_help(d, "les textes s'écrivent entre guillemets doubles : \"%.*s\"", len, t + start + 1));
        Token *tok = emit(L, T_STRING, start, j + 1);
        tok->str = xstrndup(t + start + 1, (size_t)len);
        tok->slen = len;
        *pi = j + 1;
    } else {
        Diag *d = diag_new(LEVEL_ERROR, "E0001", "`'` n'existe pas en Nova");
        diag_primary(d, sp(L, start, start + 1), "ne fait pas partie du langage");
        report(L, diag_help(d, "les textes s'écrivent entre guillemets doubles : \"bonjour\""));
        *pi = start + 1;
    }
}

static const struct {
    const char *word;
    TokKind kind;
} KEYWORDS[] = {
    { "let", T_LET }, { "fn", T_FN }, { "return", T_RETURN }, { "if", T_IF },
    { "else", T_ELSE }, { "while", T_WHILE }, { "true", T_TRUE }, { "false", T_FALSE },
    { "nil", T_NIL }, { "and", T_AND }, { "or", T_OR },
};

static TokKind symbol(const char *t, int i, int n, int *len) {
    *len = 2;
    if (i + 1 < n) {
        char a = t[i], b = t[i + 1];
        if (a == '=' && b == '=') return T_EQEQ;
        if (a == '!' && b == '=') return T_BANGEQ;
        if (a == '<' && b == '=') return T_LE;
        if (a == '>' && b == '=') return T_GE;
    }
    *len = 1;
    switch (t[i]) {
    case '+': return T_PLUS;
    case '-': return T_MINUS;
    case '*': return T_STAR;
    case '/': return T_SLASH;
    case '%': return T_PERCENT;
    case '=': return T_EQ;
    case '!': return T_BANG;
    case '<': return T_LT;
    case '>': return T_GT;
    case '(': return T_LPAREN;
    case ')': return T_RPAREN;
    case '{': return T_LBRACE;
    case '}': return T_RBRACE;
    case '[': return T_LBRACKET;
    case ']': return T_RBRACKET;
    case ',': return T_COMMA;
    case ';': return T_SEMI;
    default: return T_EOF; /* inconnu */
    }
}

static const char *char_hint(char c) {
    switch (c) {
    case '&': return "le « et » logique s'écrit `and` : `a and b`";
    case '|': return "le « ou » logique s'écrit `or` : `a or b`";
    case ':': return "Nova n'utilise pas `:` ; un bloc s'écrit entre accolades `{ ... }`";
    case '.': return "Nova n'a pas de méthodes : écris `len(liste)` plutôt que `liste.len()`";
    case '$': return "les variables n'ont pas de préfixe `$` en Nova";
    default: return NULL;
    }
}

void lex(const Source *src, TokenList *out, DiagList *diags) {
    Lexer L = { src, src->text, src->len, out, diags };
    const char *t = L.t;
    int n = L.n, i = 0;
    while (i < n) {
        unsigned char c = (unsigned char)t[i];
        int start = i, len;
        const char *hint = NULL;

        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            i++;
        } else if (c == '#') {
            while (i < n && t[i] != '\n') i++;
        } else if ((len = special_utf8(t, i, n, &hint)) > 0) {
            i += len;
            Diag *d = diag_new(LEVEL_ERROR, "E0001", "caractère `%.*s` non reconnu", len, t + start);
            diag_primary(d, sp(&L, start, i), "ne fait pas partie du langage");
            report(&L, diag_help(d, "%s", hint));
        } else if (isdigit(c)) {
            lex_number(&L, &i);
        } else if (c == '"') {
            lex_string(&L, &i);
        } else if (c == '\'') {
            lex_single_quoted(&L, &i);
        } else if (ident_start(t, i, n)) {
            while (i < n && ident_char(t, i, n)) i++;
            TokKind kind = T_IDENT;
            for (size_t k = 0; k < sizeof KEYWORDS / sizeof KEYWORDS[0]; k++)
                if ((int)strlen(KEYWORDS[k].word) == i - start && memcmp(KEYWORDS[k].word, t + start, (size_t)(i - start)) == 0)
                    kind = KEYWORDS[k].kind;
            emit(&L, kind, start, i)->str = xstrndup(t + start, (size_t)(i - start));
        } else {
            TokKind kind = symbol(t, i, n, &len);
            if (kind != T_EOF) {
                i += len;
                emit(&L, kind, start, i);
                continue;
            }
            len = utf8_char_len(c);
            if ((c == '&' || c == '|') && i + 1 < n && t[i + 1] == (char)c) len = 2; /* && ou || : une seule erreur */
            if (start + len > n) len = n - start;
            i += len;
            Diag *d = diag_new(LEVEL_ERROR, "E0001", "`%.*s` n'existe pas en Nova", len, t + start);
            diag_primary(d, sp(&L, start, i), "ne fait pas partie du langage");
            if ((hint = char_hint((char)c)) != NULL) diag_help(d, "%s", hint);
            report(&L, d);
        }
    }
    int end = n;
    while (end > 0 && isspace((unsigned char)t[end - 1])) end--;
    emit(&L, T_EOF, end, end);
}
