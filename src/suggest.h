#ifndef NOVA_SUGGEST_H
#define NOVA_SUGGEST_H

#include "common.h"

/* Le candidat assez proche de `name` pour être une faute de frappe, ou NULL. */
const char *closest(const char *name, const char **candidates, int n);

#endif
