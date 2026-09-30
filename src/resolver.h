/* Analyse statique : vérifie le programme AVANT de l'exécuter.
 * Noms inconnus (avec suggestions), redéclarations, utilisation avant
 * déclaration, `return` hors fonction, variables inutilisées… */
#ifndef NOVA_RESOLVER_H
#define NOVA_RESOLVER_H

#include "ast.h"

typedef struct Resolver Resolver;

/* repl = true : les redéclarations globales sont permises, et une ligne
 * refusée ne laisse aucune trace dans les noms globaux. */
Resolver *resolver_new(bool repl);
void resolver_run(Resolver *r, NodeList *program, DiagList *diags);

#endif
