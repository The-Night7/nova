/* Explications longues de chaque code (nova --explain CODE).
 * Chaque entrée explique POURQUOI l'erreur existe, avec un exemple faux (✗)
 * et corrigé (✓). Ajouter un code = ajouter une ligne ici. */
#include "diag.h"

const Explanation EXPLANATIONS[] = {
    /* ---------------------------------------------------------------- lexer */
    { "E0001", "caractère inconnu",
      "Le lexer (première étape : il découpe le texte en « mots ») a rencontré un\n"
      "caractère qui ne correspond à aucun élément du langage.\n\n"
      "  ✗  if a && b { ... }\n"
      "  ✓  if a and b { ... }\n\n"
      "Les textes s'écrivent entre guillemets doubles droits (\"), les opérateurs\n"
      "logiques sont `and`, `or` et `!`, les blocs sont délimités par `{ }`." },
    { "E0002", "texte non fermé",
      "Un texte commence par \" et doit se terminer par \" sur la même ligne.\n\n"
      "  ✗  print(\"bonjour);\n"
      "  ✓  print(\"bonjour\");\n\n"
      "Pour un retour à la ligne à l'intérieur d'un texte, écris \\n." },
    { "E0003", "nombre mal formé",
      "Un nombre a au plus un point décimal.\n\n"
      "  ✗  let prix = 1.2.5;\n"
      "  ✓  let prix = 1.25;" },
    { "E0004", "séquence d'échappement inconnue",
      "Dans un texte, \\ introduit un caractère spécial. Seuls \\n, \\t, \\\" et \\\\\n"
      "existent.\n\n"
      "  ✗  print(\"C:\\dossier\");\n"
      "  ✓  print(\"C:\\\\dossier\");" },
    { "E0005", "nom commençant par un chiffre",
      "Un nom (variable, fonction) commence par une lettre ou `_` : sinon le lexer\n"
      "ne pourrait pas le distinguer d'un nombre.\n\n"
      "  ✗  let 2eme = 5;\n"
      "  ✓  let deuxieme = 5;" },

    /* ---------------------------------------------------------------- parser */
    { "E0100", "symbole attendu",
      "Le parser (deuxième étape : il reconnaît la structure du programme)\n"
      "attendait un symbole précis. Le plus fréquent : le `;` oublié.\n\n"
      "  ✗  let x = 3\n"
      "  ✓  let x = 3;\n\n"
      "Astuce : l'erreur est souvent juste AVANT l'endroit signalé, car le parser\n"
      "ne remarque le problème qu'en lisant le mot suivant." },
    { "E0101", "expression attendue",
      "À cet endroit, il faut une valeur : un nombre, un texte, une variable, un\n"
      "appel de fonction, une liste…\n\n"
      "  ✗  let x = 3 + ;\n"
      "  ✓  let x = 3 + 4;" },
    { "E0102", "affectation impossible",
      "Le côté gauche d'un `=` doit être un endroit où ranger une valeur : une\n"
      "variable ou un élément de liste. Un calcul n'est pas un endroit.\n\n"
      "  ✗  a + b = 10;\n"
      "  ✓  let somme = a + b;\n"
      "  ✓  liste[0] = 10;" },
    { "E0103", "délimiteur jamais fermé",
      "Chaque `(`, `[` ou `{` doit avoir son `)`, `]` ou `}`. Le fichier s'est\n"
      "terminé alors que l'un d'eux était encore ouvert ; le diagnostic montre où.\n\n"
      "Astuce : indente ton code, un bloc mal fermé se voit alors immédiatement." },
    { "E0104", "affectation dans une condition",
      "`=` range une valeur ; `==` compare deux valeurs. Dans un `if` ou un `while`,\n"
      "tu veux presque toujours comparer.\n\n"
      "  ✗  if age = 18 { ... }\n"
      "  ✓  if age == 18 { ... }\n\n"
      "Nova refuse ce cas : il cache un bug très courant dans d'autres langages." },
    { "E0106", "accolade fermante en trop",
      "Une `}` ferme un bloc ouvert par `{`. Celle-ci ne correspond à aucun bloc\n"
      "ouvert : elle est en trop, ou une `{` a été oubliée plus haut." },
    { "E0107", "variable sans valeur",
      "En Nova, une variable reçoit toujours une valeur à sa déclaration : on ne\n"
      "peut donc jamais lire une variable « vide » par accident.\n\n"
      "  ✗  let resultat;\n"
      "  ✓  let resultat = nil;" },
    { "E0108", "paramètre en double",
      "Deux paramètres d'une même fonction ne peuvent pas porter le même nom.\n\n"
      "  ✗  fn aire(a, a) { ... }\n"
      "  ✓  fn aire(largeur, hauteur) { ... }" },
    { "E0109", "mot-clé d'un autre langage",
      "Ce mot existe dans d'autres langages mais pas en Nova :\n"
      "  var / const / val     ->  let\n"
      "  def / function / func ->  fn\n"
      "  elif / elsif          ->  else if\n"
      "  for / foreach         ->  while (pour l'instant)" },

    /* ---------------------------------------------------------------- resolver */
    { "E0200", "nom inconnu",
      "Avant l'exécution, Nova vérifie que chaque nom a été déclaré (avec `let`,\n"
      "`fn`, ou comme paramètre). Causes fréquentes :\n"
      "  - une faute de frappe (Nova propose alors le nom le plus proche) ;\n"
      "  - une variable déclarée dans un bloc `{ }` et utilisée après ce bloc ;\n"
      "  - une variable jamais déclarée : il faut `let x = ...` avant `x = ...`.\n\n"
      "  ✗  if ok { let msg = \"oui\"; }\n"
      "     print(msg);\n"
      "  ✓  let msg = \"non\";\n"
      "     if ok { msg = \"oui\"; }\n"
      "     print(msg);" },
    { "E0201", "déclaration en double",
      "Un nom ne peut être déclaré deux fois dans la même portée : la seconde\n"
      "déclaration écraserait silencieusement la première.\n\n"
      "  ✗  let x = 1;\n"
      "     let x = 2;\n"
      "  ✓  let x = 1;\n"
      "     x = 2;\n\n"
      "Dans un bloc imbriqué, redéclarer est permis : cela crée une nouvelle\n"
      "variable qui masque l'ancienne jusqu'à la fin du bloc." },
    { "E0202", "return hors d'une fonction",
      "`return` termine une fonction et renvoie une valeur à qui l'a appelée.\n"
      "Au niveau principal du programme, personne n'attend cette valeur." },
    { "E0203", "variable utilisée dans sa propre déclaration",
      "Pendant `let x = ...`, `x` n'existe pas encore : on ne peut pas s'en servir\n"
      "pour calculer sa propre valeur.\n\n"
      "  ✗  let total = total + 1;\n"
      "  ✓  let total = 0;\n"
      "     total = total + 1;" },
    { "E0205", "utilisation avant déclaration",
      "Le programme s'exécute de haut en bas : une variable n'existe qu'à partir\n"
      "de la ligne qui la déclare.\n\n"
      "  ✗  print(x);\n"
      "     let x = 5;\n\n"
      "Les fonctions déclarées avec `fn` font exception : elles sont utilisables\n"
      "partout dans leur bloc, même avant leur déclaration." },
    { "E0206", "variable pas encore initialisée",
      "Une fonction lit une variable déclarée plus loin, et elle a été appelée\n"
      "avant que cette déclaration ne s'exécute.\n\n"
      "  ✗  fn f() { return x; }\n"
      "     print(f());\n"
      "     let x = 1;\n"
      "  ✓  let x = 1;\n"
      "     print(f());" },
    { "W0201", "déclaration inutilisée",
      "Une variable, un paramètre ou une fonction jamais lu est souvent le signe\n"
      "d'un oubli ou d'une faute de frappe. Si c'est volontaire (paramètre imposé),\n"
      "préfixe le nom par `_` pour faire taire l'avertissement." },
    { "W0202", "fonction native masquée",
      "Ce nom est déjà celui d'une fonction fournie par Nova ; le redéclarer la\n"
      "rend inaccessible.\n\n"
      "  ✗  let print = 3;   (plus moyen d'afficher quoi que ce soit)" },
    { "W0203", "résultat inutilisé",
      "Cette expression est calculée puis perdue. Souvent, c'est un `==` écrit à\n"
      "la place d'un `=`.\n\n"
      "  ✗  x == 5;\n"
      "  ✓  x = 5;" },

    /* ---------------------------------------------------------------- exécution */
    { "E0300", "types incompatibles",
      "Un opérateur a reçu des valeurs qu'il ne sait pas combiner. Nova ne\n"
      "convertit jamais en cachette : \"3\" + 4 est une erreur, ni \"34\" ni 7,\n"
      "car deviner produit des bugs qui apparaissent loin de leur cause.\n\n"
      "  +          nombre + nombre, texte + texte, liste + liste\n"
      "  - * / %    nombres uniquement\n"
      "  < <= > >=  deux nombres, ou deux textes\n\n"
      "Conversions : str(n) (nombre -> texte), num(t) (texte -> nombre)." },
    { "E0301", "division par zéro",
      "Diviser par 0 n'a pas de sens. Vérifie le diviseur avant :\n\n"
      "  if nb != 0 { print(total / nb); }" },
    { "E0302", "appel d'une valeur qui n'est pas une fonction",
      "Des parenthèses après une valeur signifient « appelle cette fonction ».\n\n"
      "  ✗  let notes = [1, 2];  notes(0);\n"
      "  ✓  notes[0];" },
    { "E0303", "mauvais nombre d'arguments",
      "Une fonction reçoit exactement autant d'arguments qu'elle a de paramètres.\n\n"
      "  fn aire(l, h) { return l * h; }\n"
      "  ✗  aire(3);\n"
      "  ✓  aire(3, 4);" },
    { "E0304", "récursion trop profonde",
      "Une fonction s'appelle elle-même trop de fois. Il manque presque toujours\n"
      "le cas de base, ou il n'est jamais atteint.\n\n"
      "  ✗  fn f(n) { return n + f(n - 1); }\n"
      "  ✓  fn f(n) { if n <= 0 { return 0; } return n + f(n - 1); }" },
    { "E0305", "conversion en nombre impossible",
      "num ne convertit que des textes qui représentent un nombre : \"42\", \"3.14\",\n"
      "\"-7\". Attention aux virgules décimales et aux espaces internes." },
    { "E0307", "indice hors limites",
      "Les éléments d'une liste de taille n sont numérotés de 0 à n - 1.\n\n"
      "  let l = [10, 20, 30];\n"
      "  ✗  l[3]\n"
      "  ✓  l[2]            (le dernier)\n"
      "  ✓  l[len(l) - 1]   (le dernier, quelle que soit la taille)" },
    { "E0308", "indexation impossible",
      "[...] ne s'applique qu'aux listes (lecture et écriture) et aux textes\n"
      "(lecture seulement)." },
    { "E0309", "indice non entier",
      "Un indice est une position : 0, 1, 2… Ni un texte, ni un nombre à virgule.\n"
      "Attention : 7 / 2 vaut 3.5." },
    { "E0310", "argument de mauvais type",
      "Une fonction native a reçu une valeur d'un type qu'elle ne gère pas. Sa\n"
      "signature est rappelée dans le diagnostic." },
    { "E0311", "liste vide",
      "On ne peut pas retirer un élément d'une liste qui n'en contient aucun.\n\n"
      "  if len(pile) > 0 { let haut = pop(pile); }" },
    { "E0312", "exécution interrompue",
      "Tu as appuyé sur Ctrl+C. Si le programme ne s'arrêtait pas, vérifie que la\n"
      "condition de ta boucle finit bien par devenir fausse." },
};

const int N_EXPLANATIONS = (int)(sizeof EXPLANATIONS / sizeof EXPLANATIONS[0]);

const Explanation *explanation_find(const char *code) {
    for (int i = 0; i < N_EXPLANATIONS; i++)
        if (strcmp(EXPLANATIONS[i].code, code) == 0) return &EXPLANATIONS[i];
    return NULL;
}
