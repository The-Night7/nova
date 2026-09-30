/* Lexer : découpe le texte en tokens (les « mots » du langage). */
#ifndef NOVA_LEXER_H
#define NOVA_LEXER_H

#include "diag.h"

typedef enum {
    T_EOF, T_NUMBER, T_STRING, T_IDENT,
    /* mots-clés (T_LET..T_OR, dans cet ordre) */
    T_LET, T_FN, T_RETURN, T_IF, T_ELSE, T_WHILE, T_TRUE, T_FALSE, T_NIL, T_AND, T_OR,
    /* symboles */
    T_PLUS, T_MINUS, T_STAR, T_SLASH, T_PERCENT,
    T_EQ, T_EQEQ, T_BANG, T_BANGEQ, T_LT, T_LE, T_GT, T_GE,
    T_LPAREN, T_RPAREN, T_LBRACE, T_RBRACE, T_LBRACKET, T_RBRACKET, T_COMMA, T_SEMI,
    T__COUNT
} TokKind;

typedef struct {
    TokKind kind;
    Span span;
    double num; /* T_NUMBER */
    char *str;  /* T_STRING (texte décodé) ou T_IDENT (nom) */
    int slen;
} Token;

typedef struct {
    Token *items;
    int count, cap;
} TokenList;

const char *tok_text(TokKind k);
bool tok_is_keyword(TokKind k);

/* Ne s'arrête pas à la première erreur : la note, saute le caractère, continue. */
void lex(const Source *src, TokenList *out, DiagList *diags);

#endif
