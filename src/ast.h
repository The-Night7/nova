/* Arbre syntaxique (AST). Chaque nœud garde son Span pour les diagnostics.
 *
 * Ajouter une construction au langage = 4 endroits, toujours les mêmes :
 *   1. un NodeKind (et ses champs) ici
 *   2. une règle dans parser.c
 *   3. un cas dans resolver.c (resolve_stmt / resolve_expr)
 *   4. un cas dans interp.c (exec / eval)
 */
#ifndef NOVA_AST_H
#define NOVA_AST_H

#include "lexer.h"

typedef struct Node Node;

typedef struct {
    Node **items;
    int count, cap;
} NodeList;

typedef struct {
    const char *name;
    Span span;
} Param;

typedef enum {
    /* expressions */
    N_NUMBER, N_STRING, N_TRUE, N_FALSE, N_NIL, N_VAR, N_ASSIGN, N_UNARY, N_BINARY,
    N_LOGICAL, N_CALL, N_LIST, N_INDEX, N_INDEX_ASSIGN, N_FUNC,
    /* instructions */
    N_LET, N_FNDECL, N_RETURN, N_IF, N_WHILE, N_BLOCK, N_EXPR_STMT
} NodeKind;

struct Node {
    NodeKind kind;
    Span span;
    union {
        double num;
        struct { const char *chars; int len; } str;
        struct { const char *name; } var;
        struct { const char *name; Span name_span, eq_span; Node *value; } assign;
        struct { TokKind op; Span op_span; Node *operand; } unary;
        struct { TokKind op; Span op_span; Node *left, *right; } binary;   /* N_BINARY, N_LOGICAL */
        struct { Node *callee; NodeList args; } call;
        struct { NodeList items; } list;
        struct { Node *target, *index, *value; Span eq_span; } index;      /* N_INDEX, N_INDEX_ASSIGN */
        struct { const char *name; Span name_span; Param *params; int nparams; NodeList body; } func;
        struct { const char *name; Span name_span; Node *init; } let;
        struct { Node *func; } fndecl;
        struct { Node *value; } ret;
        struct { Node *cond, *then, *other; } if_;
        struct { Node *cond, *body; } while_;
        struct { NodeList stmts; } block;
        struct { Node *expr; } expr;
    } as;
};

#endif
