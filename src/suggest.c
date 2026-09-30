/* « Vouliez-vous dire… ? » : distance d'édition (Levenshtein) entre deux mots. */
#include "suggest.h"

#include <ctype.h>

/* Nombre minimal d'insertions, suppressions ou substitutions pour passer de a à b
 * (sans tenir compte des majuscules). */
static int levenshtein(const char *a, const char *b) {
    int la = (int)strlen(a), lb = (int)strlen(b);
    int *prev = xmalloc(sizeof(int) * (size_t)(lb + 1));
    int *cur = xmalloc(sizeof(int) * (size_t)(lb + 1));
    for (int j = 0; j <= lb; j++) prev[j] = j;
    for (int i = 1; i <= la; i++) {
        cur[0] = i;
        for (int j = 1; j <= lb; j++) {
            int cost = tolower((unsigned char)a[i - 1]) != tolower((unsigned char)b[j - 1]);
            int best = prev[j] + 1;
            if (cur[j - 1] + 1 < best) best = cur[j - 1] + 1;
            if (prev[j - 1] + cost < best) best = prev[j - 1] + cost;
            cur[j] = best;
        }
        int *tmp = prev;
        prev = cur;
        cur = tmp;
    }
    int result = prev[lb];
    free(prev);
    free(cur);
    return result;
}

const char *closest(const char *name, const char **candidates, int n) {
    int limit = (int)strlen(name) / 3; /* nom très court : seulement une différence de majuscules */
    const char *best = NULL;
    int best_dist = limit + 1;
    for (int i = 0; i < n; i++) {
        if (strcmp(candidates[i], name) == 0) continue;
        int d = levenshtein(name, candidates[i]);
        if (d < best_dist) {
            best = candidates[i];
            best_dist = d;
        }
    }
    return best;
}
