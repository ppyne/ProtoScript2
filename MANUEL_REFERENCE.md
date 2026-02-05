# Manuel de reference ProtoScript V2

Ce document est le manuel utilisateur de ProtoScript V2.
Il est descriptif. La specification `specification.md` reste la source normative.

Philosophie directrice :
**La magie cache les couts. ProtoScript les rend visibles.**

Public cible :
Ce manuel s'adresse a des developpeurs ayant deja une experience de langages imperatifs (C, JS, PHP, Java), et suppose une familiarite avec les notions de typage statique et de compilation.

---

## 1. Introduction

### 1.1 Qu'est-ce que ProtoScript V2

ProtoScript V2 est un langage statiquement type, deterministic, prototype-based, concu pour une compilation bas niveau (notamment vers C) sans semantique cachee.

### 1.2 Specification vs manuel

- Specification (`specification.md`) : regles normatives (ce qui est autorise/interdit).
- Manuel (ce document) : guide pratique (comment ecrire du code correct et lisible).

### 1.3 Programme minimal

```c
function main() : void {
    Sys.print("Hello");
}
```

### 1.4 Contre-exemple

```c
function main() {
    Sys.print("Hello");
}
```

Ce code est invalide : le type de retour est obligatoire.

### 1.5 Pourquoi ?

Le langage prefere une surface explicite des le premier exemple : type de retour visible, point d'entree explicite, aucun comportement implicite.

---

## 2. Syntaxe de base

### 2.1 Structure d'un fichier

Un fichier contient des declarations (imports, prototypes, fonctions, declarations autorisees par la grammaire).

### 2.2 Instructions et blocs

- Chaque instruction se termine par `;`.
- Les blocs utilisent `{ ... }`.
- La portee est lexicale.

Exemple :

```c
function main() : void {
    int x = 1;
    {
        int y = 2;
        Sys.print(y.toString());
    }
    Sys.print(x.toString());
}
```

### 2.3 Commentaires

```c
// commentaire ligne
/* commentaire bloc */
```

### 2.4 Ce que le langage ne fait pas

- Pas de balises.
- Pas d'HTML embarque (contrairement a l'usage historique de PHP).

### 2.5 Erreur frequente

Oublier `;` en fin d'instruction. ProtoScript n'a pas d'insertion automatique de point-virgule.

---

## 3. Types

### 3.1 Systeme de types

Le typage est statique et explicite. Les types sont resolus a la compilation.

### 3.2 Types primitifs

- `bool`
- `byte`
- `int`
- `float`
- `glyph`
- `string`

Exemples :

```c
bool ok = true;
byte b = 255;
int n = 42;
float f = 3.14;
glyph g = "A"[0];
string s = "abc";
```

### 3.3 Absence de null

Il n'y a pas de nullite universelle.

Contre-exemple :

```c
// invalide : `null` n'est pas une valeur du langage
// string s = null;
```

### 3.4 Valeurs par defaut

Une variable locale doit etre assignee avant lecture.

Contre-exemple :

```c
function main() : void {
    int x;
    Sys.print(x.toString()); // invalide : x non initialise
}
```

### 3.5 Conversions explicites

```c
int n = 12;
string s = n.toString();
float f = s.toFloat();
```

### 3.6 Erreur frequente

Supposer qu'un `int` se convertit implicitement en `string` dans un appel. Les conversions restent explicites.

### 3.7 Pourquoi ?

L'absence de null et de conversions implicites reduit les branches cachees et rend les erreurs plus locales.

---

## 4. Litteraux

### 4.1 Entiers

- Decimal, hexadecimal (`0x`), binaire (`0b`), octal (`0...`).
- Le signe `-` est un operateur unaire.

```c
int a = 10;
int b = 0x2A;
int c = 0b1010;
int d = -5; // unaire '-' applique a 5
```

### 4.2 Flottants

```c
float f1 = 1.5;
float f2 = 1e-3;
```

### 4.3 Chaines

```c
string s = "Bonjour";
```

### 4.4 Listes et maps

```c
list<int> xs = [1, 2, 3];
map<string, int> mm = {"a": 1, "b": 2};
```

### 4.5 Litteraux vides et typage contextuel

```c
list<int> xs = [];
map<string, int> mm = {};
```

Contre-exemple :

```c
var x = []; // invalide sans contexte de type
var m = {}; // invalide sans contexte de type
```

### 4.6 Erreur frequente

Confondre `{}` map vide avec un bloc vide. Dans une expression, `{}` est un litteral de map.

---

## 5. Variables

### 5.1 Declaration

```c
var n = 10;
int x = 20;
```

### 5.2 Portee lexicale et shadowing

```c
function main() : void {
    int x = 1;
    {
        int x = 2; // shadowing local
        Sys.print(x.toString()); // 2
    }
    Sys.print(x.toString()); // 1
}
```

### 5.3 Initialisation obligatoire

Une variable non assignee ne peut pas etre lue.

### 5.4 Ce qui n'existe pas

- Pas de variables dynamiques nommees a l'execution.
- Pas de superglobales.

### 5.5 Comparaison utile (PHP/JS)

En JS/PHP, des acces a des noms dynamiques peuvent exister. Ici, la resolution est compile-time.

---

## 6. Expressions

### 6.1 Expressions de base

Litteraux, identifiants, appels, acces indexes, acces membres, operations unaires/binaires, ternaire.

### 6.2 Ordre d'evaluation

L'evaluation est de gauche a droite.
`&&` et `||` court-circuitent.

```c
function left() : bool { Sys.print("L"); return false; }
function right() : bool { Sys.print("R"); return true; }

function main() : void {
    bool v = left() && right(); // affiche seulement L
}
```

### 6.3 Ternaire

```c
int a = 1;
int b = 2;
int m = (a < b) ? a : b;
```

### 6.4 Affectation

- L'affectation est une instruction.
- Elle n'a pas de valeur de retour.
- L'affectation chainee est invalide.

Contre-exemple :

```c
// invalide
// int x = (a = 1);
// a = b = c;
```

### 6.5 Pourquoi ?

Interdire l'affectation en expression supprime une source classique d'effets de bord implicites.

---

## 7. Operateurs

### 7.1 Categories

- Arithmetiques : `+ - * / %`
- Comparaison : `== != < <= > >=`
- Logiques : `&& || !`
- Bitwise : `& | ^ ~ << >>`
- Affectation : `= += -= *= /=`
- Increment/decrement : `++ --`

### 7.2 Exemples

```c
int a = 4;
int b = 2;
int c = a + b;
bool k = (a > b) && (b != 0);
int s = a << 1;
```

### 7.3 Chaines : pas de concatenation implicite

Contre-exemple :

```c
// invalide selon la spec
// string s = "a" + "b";
```

Utiliser la concatenation explicite disponible par API/methode.

### 7.4 Erreur frequente

Traiter `+` comme concatenation universelle (reflexe JS/PHP). En ProtoScript V2, le code doit rester explicite.

---

## 8. Structures de controle

### 8.1 if / else

```c
if (x > 0) {
    Sys.print("pos");
} else {
    Sys.print("non-pos");
}
```

### 8.2 Boucles

```c
while (cond) { ... }
do { ... } while (cond);
for (int i = 0; i < 10; i = i + 1) { ... }
for (int v of xs) { ... }
for (string k in mm) { ... }
```

### 8.3 break / continue

Supportes dans les boucles.

### 8.4 switch sans fallthrough implicite

```c
switch (x) {
case 1:
    Sys.print("one");
    break;
default:
    Sys.print("other");
    break;
}
```

Contre-exemple :

```c
switch (x) {
case 1:
    Sys.print("one"); // invalide sans terminaison explicite
default:
    Sys.print("other");
    break;
}
```

### 8.5 Erreur frequente

Reproduire un style C classique avec fallthrough implicite. ProtoScript V2 le refuse.

---

## 9. Fonctions

### 9.1 Declaration

```c
function add(int a, int b) : int {
    return a + b;
}
```

### 9.2 Parametres et retour

- Parametres explicitement types.
- Type de retour explicite.
- Pas de parametres optionnels implicites.

### 9.3 Variadique

```c
function sum(list<int> values...) : int {
    int acc = 0;
    for (int v of values) {
        acc = acc + v;
    }
    return acc;
}
```

Appel valide :

```c
int r = sum(1, 2, 3);
```

Contre-exemple :

```c
// invalide : variadique vide
// int r = sum();
```

### 9.4 Ce qui n'existe pas

- Pas de fonctions comme valeurs.
- Pas de generiques de fonctions.

### 9.5 Comparaison utile (JS/PHP)

Pas de closures/fonctions anonymes comme valeurs de premier ordre. Les appels sont resolus statiquement.

---

## 10. Prototypes et objets

### 10.1 Modele prototype-based

Pas de classes.
Les objets sont crees par clonage de prototypes.

### 10.2 Declaration, champs, methodes, self

```c
prototype Point {
    int x;
    int y;

    function move(int dx, int dy) : void {
        self.x = self.x + dx;
        self.y = self.y + dy;
    }
}
```

### 10.3 Substitution parent / enfant

```c
prototype ColoredPoint : Point {
    int color;
}
```

Un `ColoredPoint` peut etre utilise la ou `Point` est attendu, selon les regles statiques.

### 10.4 Override

L'override conserve une signature compatible selon la specification.

### 10.5 Ce qui n'existe pas

- Pas de classes, interfaces, traits.
- Pas de cast dynamique.
- Pas de RTTI utilisateur.

### 10.6 Pourquoi ?

Le modele prototype-based de ProtoScript V2 conserve un layout stable et une resolution statique des acces.

---

## 11. Collections

### 11.1 `list<T>`

- Mutable et possedante.
- `list[i] = x` est une ecriture stricte : l'index doit exister.
- Pas de redimensionnement implicite via indexation.

Exemple :

```c
list<int> xs = [10, 20];
xs[1] = 30; // mise a jour
xs.push(40);
int v = xs.pop();
```

Contre-exemple :

```c
list<int> xs = [1];
// xs[3] = 10; // runtime OOB
```

### 11.2 `map<K,V>` : lecture stricte, ecriture constructive

```c
map<string, int> m = {};
m["a"] = 1;    // insertion (cle absente)
m["a"] = 2;    // mise a jour (cle presente)
int x = m["a"]; // lecture valide
```

Contre-exemple :

```c
map<string, int> m = {};
int x = m["absent"]; // runtime missing key
```

### 11.3 Erreur frequente

Supposer que `map[k]` en lecture cree automatiquement une entree. Ce n'est vrai qu'en ecriture (`map[k] = v`).

### 11.4 Pourquoi ?

La distinction lecture stricte / ecriture constructive rend les effets de bord visibles.

### 11.5 Iteration

```c
for (int v of xs) { ... }
for (string k in m) { ... } // cles
for (int v of m) { ... }    // valeurs
```

---

## 12. Slices et vues

### 12.1 `slice<T>` vs `view<T>`

- `slice<T>` : vue mutable, non possedante.
- `view<T>` : vue lecture seule, non possedante.

### 12.2 Creation

```c
list<int> xs = [1, 2, 3, 4];
slice<int> s = xs.slice(1, 2);
view<int> v = xs.view(0, 3);
```

### 12.3 Ecriture

```c
s[0] = 99; // autorise
// v[0] = 99; // invalide (view en lecture seule)
```

### 12.4 Duree de vie et invalidation

Une vue ne doit pas survivre au stockage source.
Les mutations structurelles du stockage source peuvent invalider des vues.

### 12.5 Erreur frequente

Traiter `view<T>` comme un `list<T>` leger. `view<T>` n'est pas possedante et interdit l'ecriture.

---

## 13. Chaines (`string`)

### 13.1 Modele

- `string` est immuable.
- Semantique en glyphes Unicode.
- `string` n'est pas un `byte[]`.

### 13.2 Longueur et indexation glyphique

```c
string s = "a😀b";
int n = s.length(); // 3 glyphes
glyph g = s[1];     // 😀
```

Index hors bornes :

```c
// runtime OOB
// glyph g = s[99];
```

### 13.3 Combining marks

`string` suit les glyphes/scalaires definis par le langage, pas une indexation brute par octet.

### 13.4 Immutabilite

```c
string s = "abc";
// s[0] = "x"[0]; // invalide
```

### 13.5 Comparaison utile (PHP/JS/C)

- JS/PHP/C confondent souvent octets, code units et caracteres utilisateurs.
- ProtoScript V2 impose une semantique glyphique explicite pour eviter ces ambigu ites.

### 13.6 Erreur frequente

Supposer que `string[i]` modifie la chaine. Toute mutation indexee de `string` est interdite.

### 13.7 Pourquoi ?

Immutabilite + semantique glyphique = comportement stable, couts visibles, pas de magie cachant des copies.

---

## 14. Modules

### 14.1 Imports

```c
import std.io as io;
import math.core.{abs, clamp as clip};
```

### 14.2 Visibilite et noms

- Import explicite des symboles.
- Aliases explicites.
- Pas de wildcard import.

Contre-exemple :

```c
// invalide
// import std.io.*;
```

### 14.3 Resolution statique

Les symboles de module sont resolus a la compilation.
Aucun chargement dynamique.

### 14.4 Modules natifs

Les modules natifs etendent l'environnement de noms, pas la semantique du langage.

### 14.5 Ce que les modules ne peuvent pas faire

- introduire de nouveaux operateurs
- changer les regles de typage
- activer de la RTTI/reflection
- modifier la grammaire

### 14.6 Pourquoi ?

L'extension est un mecanisme d'integration, pas un mecanisme de mutation du langage.

---

## 15. Erreurs et exceptions

### 15.1 Erreurs statiques

Diagnostics avec code, categorie, position `file:line:column`.

### 15.2 Exceptions runtime

Les violations runtime normatives levent des exceptions categories.
Toute exception derive du prototype racine `Exception`.
Aucune autre valeur ne peut etre levee avec `throw`.

### 15.3 `try / catch / finally`

```c
try {
    risky();
} catch (Exception e) {
    Sys.print("handled");
} finally {
    Sys.print("cleanup");
}
```

### 15.4 Contre-exemple

```c
// invalide : throw d'une valeur non Exception
// throw 42;
```

### 15.5 Erreur frequente

Confondre absence de RTTI utilisateur et mecanisme `catch` par type : `catch` utilise une metadonnee interne d'exception, non exposable.

---

## 16. Execution

### 16.1 Modele

Execution deterministe selon l'ordre d'evaluation defini.

### 16.2 Absences volontaires

- Pas de RTTI utilisateur.
- Pas de reflection.
- Pas de comportement implicite dependant de l'environnement runtime.

### 16.3 Comparaison utile (JS/PHP)

Pas d'ajout dynamique de membres/fonctions a chaud. L'execution suit un contrat statique.

---

## 17. Performance et couts

### 17.1 Principe

Les couts doivent rester visibles dans le code et predictibles.

### 17.2 Checks runtime

Les checks normatifs font partie de l'execution normale.
Ils ne sont elidables que si leur inutilite est prouvee.

### 17.3 Exceptions

Le cout "zero-cost" concerne le mecanisme d'unwind/dispatch quand aucune exception n'est levee.
Il ne signifie pas "absence de checks runtime normatifs".

### 17.4 Debug vs release

- Meme semantique observable.
- Differences autorisees : instrumentation et qualite des diagnostics.

### 17.5 Pourquoi ?

Le langage privilegie des garanties defendables plutot que des promesses de performance implicites.

---

## 18. Annexes

### 18.0 Cheat sheet (1 page)

Types (base) :

- `bool`, `byte`, `int`, `float`, `glyph`, `string`
- pas de `null` universel
- conversions explicites seulement

Collections :

- `list<T>` : mutable, `list[i] = x` strict, `push/pop` explicites
- `map<K,V>` : lecture stricte (`map[k]` exige cle presente), ecriture constructive (`map[k] = v` insere/met a jour)
- `slice<T>` : vue mutable non possedante
- `view<T>` : vue lecture seule non possedante
- `string` : immuable, indexation glyphique

Erreurs frequentes :

- oublier le type de retour d'une fonction
- tenter `a = b = c` (affectation chainee interdite)
- supposer `sum()` valide avec variadique (la sequence variadique doit etre non vide)
- ecrire dans `string[i]` ou `view[i]`
- lire `map[k]` sur une cle absente en pensant obtenir une valeur par defaut

Differences cles vs JS/PHP :

- pas de typage dynamique
- pas de fonctions comme valeurs
- pas de null universel
- pas de chargement dynamique des modules
- pas de concatenation implicite de chaines

### 18.1 Table de correspondance (Concept -> Section)

| Concept | Ou lire |
|---|---|
| Unicode / glyphes | §13 |
| Exceptions | §15 |
| map lecture stricte / ecriture constructive | §11.2 |
| Variadique | §9.3 |
| slice / view | §12 |
| Prototypes et substitution parent/enfant | §10 |
| Modules et imports | §14 |
| Ordre d'evaluation | §6.2 |
| switch sans fallthrough implicite | §8.4 |
| Absence de null | §3.3 |

### 18.2 Table rapide des operateurs

| Famille | Operateurs |
|---|---|
| Unaires | `! ~ - ++ --` |
| Multiplicatifs | `* / % &` |
| Additifs | `+ - | ^` |
| Shifts | `<< >>` |
| Comparaison | `== != < <= > >=` |
| Logiques | `&& ||` |
| Conditionnel | `?:` |
| Affectation | `= += -= *= /=` |

### 18.3 Exemple complet

```c
function sum(list<int> values...) : int {
    int acc = 0;
    for (int v of values) {
        acc = acc + v;
    }
    return acc;
}

function main() : void {
    int r = sum(1, 2, 3);
    Sys.print(r.toString());
}
```

### 18.4 Notes de comparaison (clarification)

- Par rapport a JavaScript : pas de typage dynamique, pas de fonctions comme valeurs, pas de metaprogrammation runtime.
- Par rapport a PHP : pas d'HTML embarque, pas de superglobales, pas de variables dynamiques.
- Par rapport a C : semantique de surete normative (checks/diagnostics), tout en gardant un modele de compilation bas niveau.

---

## Rappel final

Ce manuel decrit l'usage quotidien.
La specification `specification.md` definit la loi du langage.
