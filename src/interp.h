/* Interpréteur : parcourt l'AST et l'exécute. */
#ifndef NOVA_INTERP_H
#define NOVA_INTERP_H

#include "value.h"

typedef struct Interp Interp;

Interp *interp_new(void);

/* Exécute un programme. Renvoie NULL si tout s'est bien passé, sinon le
 * diagnostic de l'erreur d'exécution. *has_last indique si la dernière
 * instruction était une expression, dont la valeur est dans *last (REPL). */
Diag *interp_run(Interp *in, NodeList *program, Value *last, bool *has_last);

#endif
