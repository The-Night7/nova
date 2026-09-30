#ifndef NOVA_PARSER_H
#define NOVA_PARSER_H

#include "ast.h"

/* Construit l'AST. Les erreurs vont dans `diags` ; le parser se resynchronise
 * après chacune pour tout signaler en une seule passe. */
NodeList parse_program(const TokenList *tokens, DiagList *diags);

#endif
