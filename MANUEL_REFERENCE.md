![ProtoScript2](header.png)

> If two data structures are different, they should be written as different.  
> *– Niklaus Wirth, inventeur du langage Pascal*

# Manuel de référence ProtoScript V2

Ce document est le manuel utilisateur de ProtoScript V2.
Il est descriptif. La spécification [`SPECIFICATION.md`](SPECIFICATION.md) reste la source normative.

Philosophie directrice :
**La magie cache les coûts. ProtoScript les rend visibles.**

Public cible :
Ce manuel s'adresse à des développeurs ayant déjà une expérience de langages impératifs (C, JS, PHP, Java), et suppose une familiarité avec les notions de typage statique et de compilation.

---

## 1. Introduction

### 1.1 Qu'est-ce que ProtoScript V2

ProtoScript V2 est un langage statiquement typé, déterministe, prototype-based, conçu pour une compilation bas niveau (notamment vers C) sans sémantique cachée.

### 1.2 Spécification vs manuel

- Spécification ([`SPECIFICATION.md`](SPECIFICATION.md)) : règles normatives (ce qui est autorisé/interdit).
- Manuel (ce document) : guide pratique (comment écrire du code correct et lisible).

### 1.3 Programme minimal

```c
import Io;

function main() : void {
    Io.printLine("Hello world");

    Io.print("Hello world".concat(Io.EOL));
    
    Io.print(["Hello", " ", "world", Io.EOL].concat());
}
```
Ref: EX-001

### 1.4 Contre-exemple

```c
function main() {
    Io.printLine("Hello");
}
```

Ce code est invalide : le type de retour est obligatoire.

### 1.5 Pourquoi ?

Le langage préfère une surface explicite dès le premier exemple : type de retour visible, point d'entrée explicite, aucun comportement implicite.

---

## 2. Syntaxe de base

### 2.1 Structure d'un fichier

Un fichier contient des déclarations (imports, prototypes, fonctions, déclarations autorisées par la grammaire).

### 2.2 Instructions et blocs

- Chaque instruction se termine par `;`.
- Les blocs utilisent `{ ... }`.
- La portée est lexicale.

Exemple :

```c
import Io;

function main() : void {
    int x = 1;
    {
        int y = 2;
        Io.printLine(y.toString());
    }
    Io.printLine(x.toString());
}
```
Ref: EX-002

### 2.3 Commentaires

```c
// commentaire ligne
/* commentaire bloc */
```
Ref: EX-003

### 2.4 Ce que le langage ne fait pas

- Pas de balises.
- Pas d'HTML embarqué (contrairement à l'usage historique de PHP).

### 2.5 Erreur fréquente

Oublier `;` en fin d'instruction. ProtoScript n'a pas d'insertion automatique de point-virgule.

---

## 3. Types

### 3.1 Système de types

Le typage est statique et explicite. Les types sont résolus à la compilation.

### 3.2 Types scalaires fondamentaux

- `bool`
- `byte`
- `int`
- `float`
- `glyph`
- `string`

Ces types sont immuables au niveau langage, manipulés par valeur, sans héritage ni champs utilisateur.

Exemples :

```c
bool ok = true;
byte b = 255;
int n = 42;
float f = 3.14;
glyph g = "A"[0];
string s = "abc";
```
Ref: EX-004

### 3.2.1 Conversions numériques explicites

ProtoScript2 distingue `byte`, `int`, `float`.  
Les littéraux numériques sont non typés et **leur type est déterminé par le contexte**.  
En l’absence de contexte, le type par défaut est `int`.

La conversion explicite se fait avec la forme :

```c
(T)expr
```

où `T` est un type numérique (`byte`, `int`, `float`).

Règles :

- aucune conversion implicite n’est autorisée entre types numériques ;
- une conversion explicite est **autorisée uniquement si** la valeur est **exactement représentable** dans `T` ;
- toute conversion entraînant dépassement, troncature, wrap ou saturation est une **erreur statique**.

Exemples valides :

```c
byte a = 255;
byte b = (byte)255;
int  c = (int)3.0;
float d = (float)42;
```

Exemples invalides :

```c
byte a = 256;
byte b = (byte)256;
int  c = (int)3.14;
```

### 3.3 Absence de null

Il n'y a pas de nullité universelle.

Contre-exemple :

```c
// invalide : `null` n'est pas une valeur du langage
string s = null; // Erreur : E2001 UNRESOLVED_NAME
```
Ref: EX-005

### 3.3.1 Pourquoi ?

Cette approche rend l'absence explicite et statiquement typée, sans introduire de nullité implicite.

### 3.3.2 Alternative idiomatique : prototype "nullable"

Quand un type "vide" est nécessaire, on utilise un prototype explicite avec un indicateur statique.

Exemple (chaîne nullable) :

```c
import Io;

prototype NullableString {
    bool is_null;
    string value;

    function isNull() : bool {
        return self.is_null;
    }
}

function main() : void {
    NullableString a = NullableString.clone();
    a.is_null = true;

    NullableString b = NullableString.clone();
    b.is_null = false;
    b.value = "ok";

    if (a.isNull()) {
        Io.printLine("empty");
    }
    if (!b.isNull()) {
        Io.printLine(b.value);
    }
}
```
Ref: EX-006

### 3.3.3 Cas standard : `JSONValue` et `null` JSON

Le prototype standard `JSONValue` représente un JSON complet, y compris `null`.
On utilise les constructeurs explicites du module `JSON`.

Exemple :

```c
import JSON;
import Io;

function main() : void {
    JSONValue v = JSON.null();
    if (v.isNull()) {
        Io.printLine("json null");
    }

    JSONValue obj = JSON.object({
        "a": JSON.null(),
        "b": JSON.number(1)
    });
    string s = JSON.encode(obj);
    Io.printLine(s);
}
```
Ref: EX-007

### 3.4 Valeurs par défaut

Toute variable, champ ou valeur allouée est implicitement initialisée au moment de sa création.
Il n’existe pas d’état non initialisé observable dans le langage.

Valeurs par défaut :

- bool → false
- byte → 0
- int → 0
- float → 0.0
- glyph → U+0000
- string → ""

Exemple :

```c
function main() : void {
    int x;
    Io.printLine(x.toString()); // affiche "0"
}
```
Ref: EX-008

### 3.5 Conversions explicites

```c
int n = 12;
string s = n.toString();
float f = s.toFloat();
```
Ref: EX-010

### 3.6 Erreur fréquente

Supposer qu'un `int` se convertit implicitement en `string` dans un appel. Les conversions restent explicites.

### 3.7 Pourquoi ?

L'absence de null et de conversions implicites réduit les branches cachées et rend les erreurs plus locales.

---

## 4. Littéraux

### 4.1 Entiers

- Décimal, hexadécimal (`0x`), binaire (`0b`), octal (`0...`).
- Le signe `-` est un opérateur unaire.

```c
int a = 10;
int b = 0x2A;
int c = 0b1010;
int d = -5; // unaire '-' appliqué à 5
```
Ref: EX-011

### 4.2 Flottants

```c
float f1 = 1.5;
float f2 = 1e-3;
```
Ref: EX-012

### 4.3 Chaînes

```c
string s = "Bonjour";
```
Ref: EX-013

Échappements reconnus dans les littéraux de chaîne :

```c
string q = "\"";     // guillemet
string b = "\\";     // antislash
string n = "\n";     // LF
string t = "\t";     // TAB
string r = "\r";     // CR
string bs = "\b";    // BS
string ff = "\f";    // FF
string u = "\u263A"; // Unicode (☺)
```
Ref: EX-014

### 4.4 Listes et maps

```c
list<int> xs = [1, 2, 3];
map<string, int> mm = {"a": 1, "b": 2};
```
Ref: EX-015

### 4.5 Littéraux vides et typage contextuel

```c
list<int> xs = [];
map<string, int> mm = {};
```
Ref: EX-016

Contre-exemple :

```c
var x = []; // Erreur : E3006 MISSING_TYPE_CONTEXT
var m = {}; // Erreur : E3006 MISSING_TYPE_CONTEXT
```
Ref: EX-017

### 4.6 Erreur fréquente

Confondre `{}` map vide avec un bloc vide. Dans une expression, `{}` est un littéral de map.

---

## 5. Variables

### 5.1 Déclaration

```c
var n = 10;
int x = 20;
```
Ref: EX-018

`var` déclenche une inférence locale du type à partir de l'initialiseur.
Le type reste statique et connu à la compilation.
Une déclaration `var` doit donc toujours avoir une valeur d'initialisation.

Exemple :

```c
var s = "ok";  // s : string
var n = 12;    // n : int
```
Ref: EX-019

Contre-exemple :

```c
var x; // Erreur : E1001 PARSE_UNEXPECTED_TOKEN
```
Ref: EX-020

### 5.1.1 Déclaration `const`

`const` est réservé aux **types scalaires fondamentaux** et impose une initialisation immédiate.
La valeur est ensuite **non réassignable** (affectations simples, composées, `++/--`).

Exemple :

```c
const int Max = 10;
const string Name = "ps2";
```

Contre-exemples :

```c
const int x;     // Erreur : E3130 CONST_REASSIGNMENT
const int y = 1;
y = 2;           // Erreur : E3130 CONST_REASSIGNMENT
```

### 5.2 Portée lexicale et shadowing

```c
function main() : void {
    int x = 1;
    {
        int x = 2; // shadowing local
        Io.printLine(x.toString()); // 2
    }
    Io.printLine(x.toString()); // 1
}
```
Ref: EX-021

### 5.3 Initialisation implicite et valeurs par défaut

Dans ProtoScript2, une variable est toujours initialisée à une valeur par défaut déterministe au moment de sa création.
Il n’existe pas d’état non initialisé observable.

Exemple :

```c
function main() : void {
    int x;
    Io.printLine(x.toString()); // affiche "0"
}
```
Ref: EX-022

Exemple (champ de prototype) :

```c
prototype P { int n; }

function main() : void {
    P p = P.clone();
    Io.printLine(p.n.toString()); // affiche "0"
}
```
Ref: EX-023

### 5.4 Ce qui n'existe pas

- Pas de variable dynamique nommée à l'exécution.
- Pas de superglobale (variable globale prédéfinie, accessible partout sans déclaration explicite).

### 5.5 Comparaison utile (PHP/JS)

En JS/PHP, des accès à des noms dynamiques peuvent exister. Ici, la résolution est réalisée à la compilation (compile-time).

### 5.6 Groupes de constantes (`T group`)

Un `group` définit un **ensemble de constantes nommées** d’un **type scalaire fondamental**.
Il ne crée **aucun type nominal** et **aucune entité runtime**.

Forme :

```c
T group Nom {
    Member = ExprConstante,
    ...
}
```

Règles pratiques :

- `T` est un type scalaire fondamental (`bool`, `byte`, `glyph`, `int`, `float`, `string`).
- chaque membre est typé `T`.
- l’expression doit être constante (réduite par le folding existant).
- un membre de `group` n’est **jamais assignable**.

Exemple :

```c
int group Colors {
    Red = 1,
    Blue = Red + 3
}

function main() : void {
    int x = Colors.Blue;
}
```

Erreurs fréquentes :

- type non scalaire (`E3120`)
- membre non assignable à `T` ou expression non constante (`E3121`)
- tentative de mutation (`E3122`)

---

## 6. Expressions

### 6.1 Expressions de base

Littéraux, identifiants, appels, accès indexés, accès membres, opérations unaires/binaires, ternaire.

### 6.2 Ordre d'évaluation

L'évaluation est de gauche à droite.
`&&` et `||` court-circuitent.

```c
function left() : bool { Io.printLine("L"); return false; }
function right() : bool { Io.printLine("R"); return true; }

function main() : void {
    bool v = left() && right(); // affiche seulement L
}
```
Ref: EX-024

### 6.3 Ternaire

```c
int a = 1;
int b = 2;
int m = (a < b) ? a : b;
```
Ref: EX-025

### 6.4 Affectation

- L'affectation est une instruction.
- Elle n'a pas de valeur de retour.
- L'affectation chaînée est invalide.

Contre-exemple :

```c
// invalide
// int x = (a = 1); // Erreur : E1001 PARSE_UNEXPECTED_TOKEN
// a = b = c;
```
Ref: EX-026

### 6.5 Pourquoi ?

Interdire l'affectation en expression supprime une source classique d'effets de bord implicites.

---

## 7. Opérateurs

```c
int a = 4;
int b = 2;
int c = a + b;
bool k = (a > b) && (b != 0);
int s = a << 1;
```
Ref: EX-027

### 7.1 Catégories

- Arithmétiques : `+ - * / %`
- Comparaison : `== != < <= > >=`
- Logiques : `&& || !`
- Bitwise : `& | ^ ~ << >>`
- Affectation : `= += -= *= /=`
- Incrémentation / décrémentation : `++ --`

#### 7.1.1 Opérateurs arithmétiques

| Exemple | Nom | Résultat |
|---|---|---|
| `a + b` | Addition | Somme de `a` et `b`. |
| `a - b` | Soustraction | Différence de `a` et `b`. |
| `a * b` | Multiplication | Produit de `a` et `b`. |
| `a / b` | Division | Quotient de `a` et `b`. |
| `a % b` | Modulo | Reste de `a / b`. |

Notes :

- opérateurs arithmétiques valides sur `int`, `float`, `byte` (pas sur `string`).
- division par zéro → **exception runtime** (`R1004` / `RUNTIME_DIVIDE_BY_ZERO`).
- overflow sur `int`/`byte` → **exception runtime** (`R1001` / `RUNTIME_INT_OVERFLOW`).
- pas de promotion implicite : `int + float` est **interdit** → erreur statique `E3001` (`TYPE_MISMATCH_ASSIGNMENT`).
  Pour mélanger les types, il faut convertir explicitement l’un des deux (ex. `x.toFloat()`).
- `a / b` sur `int` est une division entière ; sur `float`, division flottante.
- `a % b` est défini pour `int`/`byte` uniquement.
- `-x` est un opérateur unaire ; `-INT_MIN` déclenche une **exception runtime** (`R1001` / `RUNTIME_INT_OVERFLOW`).
- précédence (rappel) : `* / %` avant `+ -` ; les opérateurs unaires ont une précédence plus haute. La table complète est en annexe.

#### 7.1.2 Opérateurs de comparaison

| Exemple | Nom | Résultat |
|---|---|---|
| `a == b` | Égal | `true` si `a` est égal à `b` (types compatibles, pas de conversion implicite). |
| `a != b` | Différent | `true` si `a` est différent de `b` (types compatibles, pas de conversion implicite). |
| `a < b` | Plus petit que | `true` si `a` est strictement plus petit que `b`. |
| `a > b` | Plus grand que | `true` si `a` est strictement plus grand que `b`. |
| `a <= b` | Inférieur ou égal | `true` si `a` est plus petit ou égal à `b`. |
| `a >= b` | Supérieur ou égal | `true` si `a` est plus grand ou égal à `b`. |

Notes :

- comparaisons autorisées uniquement entre types identiques.
- comparer des types différents est invalide → erreur statique `E3001` (`TYPE_MISMATCH_ASSIGNMENT`).
- pour comparer des types différents, il faut convertir explicitement l’un des deux.
- pour `string` : `==` / `!=` comparent le contenu exact ; `<` / `<=` / `>` / `>=` comparent lexicographiquement la séquence UTF‑8 (pas de locale, pas de normalisation).
- pour les types structurés (objets/prototypes, `list`, `map`, `slice`, `view`), la comparaison porte sur l’identité de valeur (pas de deep compare implicite).

#### 7.1.3 Opérateurs logiques

| Exemple | Nom | Résultat |
|---|---|---|
| `!a` | Not (Non) | `true` si `a` n'est pas `true`. |
| `a && b` | And (Et) | `true` si `a` ET `b` sont `true` (court-circuit). |
| `a || b` | Or (Ou) | `true` si `a` OU `b` est `true` (court-circuit). |

Notes :

- opérandes **obligatoirement** de type `bool`.
- aucun transtypage implicite (`int`, `string`, etc. sont interdits ici).
- type invalide → erreur statique `E3001` (`TYPE_MISMATCH_ASSIGNMENT`).
- évaluation court‑circuitée pour `&&` et `||`.
- précédence (rappel) : `!` avant `&&`, puis `||`.

#### 7.1.4 Opérateurs sur les bits

| Exemple | Nom | Résultat |
|---|---|---|
| `a & b` | And (Et) | Bits à 1 dans `a` ET dans `b` restent à 1. |
| `a | b` | Or (Ou) | Bits à 1 dans `a` OU dans `b` restent à 1. |
| `a ^ b` | Xor (Ou exclusif) | Bits à 1 dans `a` OU dans `b` mais pas dans les deux. |
| `~a` | Not (Non) | Inversion bit à bit de `a`. |
| `a << b` | Décalage à gauche | Décale les bits de `a` de `b` positions vers la gauche. |
| `a >> b` | Décalage à droite | Décale les bits de `a` de `b` positions vers la droite. |

Notes :

- opérandes **int/byte** uniquement (pas de `float`, pas de `string`).
- type invalide → erreur statique `E3001` (`TYPE_MISMATCH_ASSIGNMENT`).
- décalage hors plage (`b < 0` ou `b >= 64`) pour `a << b` / `a >> b` → **exception runtime** `R1005` (`RUNTIME_SHIFT_RANGE`).
- précédence : `~` > `<<`/`>>` > `&` > `^` > `|`.

#### 7.1.5 Opérateurs d'affectation

| Exemple | Équivalent | Opération |
|---|---|---|
| `a = b` | `a = b` | Affectation simple. |
| `a += b` | `a = a + b` | Addition. |
| `a -= b` | `a = a - b` | Soustraction. |
| `a *= b` | `a = a * b` | Multiplication. |
| `a /= b` | `a = a / b` | Division. |

Notes :

- opérandes numériques uniquement (`int`, `float`, `byte`) selon l’opération sous‑jacente.
- type invalide → erreur statique `E3001` (`TYPE_MISMATCH_ASSIGNMENT`).
- pas de conversion implicite (ex. `int += float` interdit ; conversion explicite requise).
- les mêmes erreurs runtime que pour les opérations arythmétiques s’appliquent (overflow, division par zéro).

#### 7.1.6 Incrémentation et décrémentation

| Exemple | Équivalent | Opération |
|---|---|---|
| `++a` | Pré-incrémente | Incrémente `a` de 1, puis retourne `a`. |
| `a++` | Post-incrémente | Retourne `a`, puis incrémente `a` de 1. |
| `--a` | Pré-décrémente | Décrémente `a` de 1, puis retourne `a`. |
| `a--` | Post-décrémente | Retourne `a`, puis décrémente `a` de 1. |

En contexte d'expression, la forme pré/post indique si la modification intervient avant ou après l'utilisation de la valeur.

### 7.2 Chaînes : pas de concaténation implicite

Contre-exemple :

```c
// invalide selon la spec
// string s = "a" + "b"; // Erreur : E3001 TYPE_MISMATCH_ASSIGNMENT
```
Ref: EX-028

Utiliser la concaténation explicite disponible par API/méthode.

Exemple correct de concaténation explicite :

```c
string a = "Hello ";
string b = "world";
string c = a.concat(b);

// Astuce
string d = ["Hello", " ", "world"].concat();
```
Ref: EX-029

### 7.4 Erreur fréquente

Traiter `+` ou `.` comme concaténation universelle (réflexe JS/PHP). En ProtoScript V2, le code doit rester explicite.

---

## 8. Structures de contrôle

### 8.1 if / else

```c
if (x > 0) {
    Io.printLine("pos");
} else {
    Io.printLine("non-pos");
}
```
Ref: EX-030

Le bloc est optionnel si la branche contient une seule instruction.

```c
if (x > 0)
    Io.printLine("pos");
```
Ref: EX-031

Exemple avec `else if` :

```c
if (x > 0) {
    Io.printLine("pos");
} else if (x < 0) {
    Io.printLine("neg");
} else {
    Io.printLine("zero");
}
```
Ref: EX-032

### 8.2 Boucles

ProtoScript V2 propose des boucles classiques et des boucles d'itération.

#### 8.2.1 while

```c
while (cond) {
    // ...
}
```
Ref: EX-033

#### 8.2.2 do / while

```c
do {
    // ...
} while (cond);
```
Ref: EX-034

#### 8.2.3 for classique

```c
for (int i = 0; i < 10; i++) {
    // ...
}
```
Ref: EX-035

Exemples d'itération indexée :

```c
list<int> xs = [10, 20, 30];
for (int i = 0; i < xs.length(); i = i + 1) {
    Io.printLine(xs[i].toString());
}
```
Ref: EX-036

```c
string s = "abc";
for (int i = 0; i < s.length(); ++i) {
    glyph g = s[i];
    Io.printLine(g.toString());
}
```
Ref: EX-037

Note :

`map<K,V>` ne s'itère pas par index. Utiliser `for ... of` (valeurs) ou `for ... in` (clés).
Alternative explicite : récupérer les clés puis itérer sur la liste de clés.

```c
map<string, int> m = {"a": 1, "b": 2};
list<string> ks = m.keys();
for (int i = 0; i < ks.length(); i++) {
    int v = m[ks[i]];
    Io.printLine(v.toString());
}
```
Ref: EX-038

#### 8.2.4 for ... of (itération sur les valeurs)

`for ... of` itère sur les valeurs d'une structure itérable.

```c
list<int> xs = [1, 2, 3];
for (int v of xs) {
    Io.printLine(v.toString());
}
```
Ref: EX-039

Sur `string`, `for ... of` itère sur les glyphes :

```c
string s = "a😀b";
for (glyph g of s) {
    Io.printLine(g.toString());
}
```
Ref: EX-040

Sur `map<K,V>`, `for ... of` itère sur les valeurs `V` :

```c
map<string, int> m = {"a": 1, "b": 2};
for (int v of m) {
    Io.printLine(v.toString());
}
```
Ref: EX-041

#### 8.2.5 for ... in (itération sur les clés)

`for ... in` itère sur les clés d'une map (et uniquement une map).

```c
map<string, int> m = {"a": 1, "b": 2};
for (string k in m) {
    Io.printLine(k);
}
```
Ref: EX-042

Contre-exemple :

```c
list<int> xs = [1, 2, 3];
// invalide : `for ... in` ne s'applique pas à `list<T>`
// for (int v in xs) { ... } // Erreur : E2001 UNRESOLVED_NAME

string s = "abc";
// invalide : `for ... in` ne s'applique pas à `string`
// for (glyph g in s) { ... }
```
Ref: EX-043

Erreur fréquente :

Confondre `for ... of` (valeurs) et `for ... in` (clés).

### 8.3 break / continue

`break` et `continue` sont disponibles dans les boucles :

- `break` sort immédiatement de la boucle courante.
- `continue` passe directement à l'itération suivante.

Exemple `break` :

```c
list<int> xs = [1, 2, 3, 4];
for (int v of xs) {
    if (v == 3) {
        break;
    }
    Io.printLine(v.toString());
}
```
Ref: EX-044

Exemple `continue` :

```c
for (int i = 0; i < 5; i++) {
    if (i == 2) {
        continue;
    }
    Io.printLine(i.toString());
}
```
Ref: EX-045

### 8.4 switch sans fallthrough implicite

```c
switch (x) {
case 1:
    Io.printLine("one");
    break;
default:
    Io.printLine("other");
    break;
}
```
Ref: EX-046

Chaque `case` / `default` doit se terminer par une instruction de terminaison explicite :

- `break` : quitte le `switch`
- `return` : quitte la fonction
- `throw` : lève une exception

Contre-exemple :

```c
switch (x) {
case 1:
    Io.printLine("one"); // invalide sans terminaison explicite
default:
    Io.printLine("other");
    break;
} // Erreur : E2001 UNRESOLVED_NAME
```
Ref: EX-047

Contre-exemple (fallthrough implicite) :

```c
switch (x) {
case 1:
case 2:
    Io.printLine("one and two"); // invalide : fallthrough implicite
    break;
default:
    Io.printLine("other");
    break;
} // Erreur : E2001 UNRESOLVED_NAME
```
Ref: EX-048

### 8.5 Erreur fréquente

Reproduire un style C classique avec fallthrough implicite. ProtoScript V2 le refuse pour éviter les effets de bord implicites et imposer des branches de contrôle lisibles et explicitement terminées.

---

## 9. Fonctions

### 9.1 Déclaration

```c
function add(int a, int b) : int {
    return a + b;
}
```
Ref: EX-049

### 9.2 Paramètres et retour

- Paramètres explicitement typés.
- Type de retour explicite (même pour `void`).
- Pas de paramètres optionnels implicites.

Contre-exemple :

```c
// invalide : paramètres par défaut non supportés
// function greet(string name = "world") : void { // Erreur : E1001 PARSE_UNEXPECTED_TOKEN
//     Io.printLine(name);
// }
```
Ref: EX-050

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
Ref: EX-051

Appels valides :

```c
int r = sum(1, 2, 3);
int r = sum(); // variadique vide => liste vide
```
Ref: EX-052, EX-053

Note :

Un appel variadique peut être vide. Dans ce cas, la liste capturée est vide (`view<T>` de longueur 0).

### 9.4 Ce qui n'existe pas

- Pas de fonctions comme valeurs.
- Pas de génériques de fonctions.

### 9.5 Comparaison utile (JS/PHP)

Pas de closures/fonctions anonymes comme valeurs de premier ordre. Les appels sont résolus statiquement.

---

## 10. Prototypes et objets

ProtoScript V2 adopte un **modèle orienté objet prototype-based**, et rejette explicitement le modèle *class-based*.

Il n’existe dans ProtoScript V2 :

- ni classes,

- ni instances de classes,

- ni hiérarchie d’héritage dynamique,

- ni mécanisme de construction implicite.

Le choix fondamental est celui du **prototype concret**.

Un prototype n’est pas une abstraction ni un type théorique :  
c’est un **objet gabarit explicite**, entièrement défini à la compilation, servant de base à la création d’objets par clonage.

---

### Principes fondamentaux

Le modèle objet de ProtoScript V2 repose sur les principes suivants :

- un objet est créé par **clonage d’un prototype explicite**

- la structure d’un prototype (champs, méthodes, relations) est **figée à la compilation**

- la résolution des champs et des méthodes est **statique**

- les relations entre prototypes sont **déclaratives et non dynamiques**

Ce modèle élimine toute forme de magie d’héritage ou de résolution tardive.  
Il privilégie des structures **lisibles, prédictibles et compilables efficacement**.

Conceptuellement, ProtoScript V2 met en œuvre une **délégation statique**, et non un héritage dynamique.

---

### 10.1 Modèle prototype-based

Il n’existe pas de classes dans ProtoScript V2.  
Les objets sont créés exclusivement par clonage de prototypes.

Exemple :

```c
prototype Point {
    int x;
    int y;
}

function main() : void {
    Point p = Point.clone();
    p.x = 1;
    p.y = 2;
}
```
Ref: EX-054

Le prototype `Point` définit :

- un layout mémoire,

- un ensemble de champs,

- un ensemble de méthodes.

Le clonage produit une instance conforme à cette définition, sans mécanisme implicite supplémentaire.

---

### 10.2 Déclaration, champs, méthodes et `self`

Un prototype peut définir des champs et des méthodes.  
À l’intérieur d’une méthode, le mot-clé `self` désigne l’objet courant.

Règles :

- `self` ne peut pas être retourné (`return self;` est invalide).
- les méthodes mutantes doivent retourner `void`.
- aucun style fluent/chaîné basé sur mutation n’est supporté.

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
Ref: EX-055

`self` est **lexicalement et statiquement résolu**.  
Il n’existe aucune ambiguïté liée à un contexte d’appel dynamique.

---

### 10.3 Relation parent / enfant (substitution statique)

Un prototype peut être défini comme une **extension statique** d’un autre prototype :

```c
prototype ColoredPoint : Point {
    int color;
}
```
Ref: EX-056

Un `ColoredPoint` peut être utilisé là où un `Point` est attendu, **selon les règles de substitution statiques** définies par le langage.

Cette relation :

- n’implique aucune chaîne dynamique de prototypes,

- ne repose sur aucun mécanisme de lookup tardif,

- garantit la compatibilité structurelle à la compilation.

### 10.3.1 `sealed prototype`

`sealed prototype` interdit **uniquement** l’héritage.
La création d’objets via `Type.clone()` reste inchangée.

Exemple valide :

```c
sealed prototype Box {
    int v;
}

function main() : void {
    Box b = Box.clone();
    b.v = 1;
}
```

Contre-exemple :

```c
sealed prototype Base {}
prototype Child : Base {} // Erreur : E3140 SEALED_INHERITANCE
```

---

### 10.4 Override de méthodes

Une méthode peut être redéfinie dans un prototype enfant **à condition de conserver une signature strictement compatible**.

Règles normatives d’override :

- le nom doit être identique

- la liste des paramètres doit être identique

- le type de retour doit être identique

- aucune surcharge par nombre ou type de paramètres n’est autorisée

Exemple valide :

```c
prototype Point {
    function move(int dx, int dy) : void {
        self.x = self.x + dx;
        self.y = self.y + dy;
    }
}

prototype ColoredPoint : Point {
    function move(int dx, int dy) : void {
        self.x = self.x + dx;
        self.y = self.y + dy;
    }
}
```
Ref: EX-057

Contre-exemples :

```c
prototype Bad1 : Point {
    function move(int dx) : void { }
}

prototype Bad2 : Point {
    function move(int dx, int dy) : int { return 0; }
} // Erreur : E2001 UNRESOLVED_NAME
```
Ref: EX-058

Les champs ne peuvent pas être redéfinis avec un type différent.  
Il n’existe aucun mécanisme de surcharge structurelle implicite.

Erreurs attendues :

- `E3001` (`TYPE_MISMATCH_ASSIGNMENT`) pour une signature de méthode incompatible ou un champ redéfini avec un type différent.

---

### 10.5 Absence de `super` et appels hérités

ProtoScript V2 ne définit **aucun mécanisme `super` implicite**.

Une méthode héritée mais non redéfinie reste disponible dans le prototype enfant.  
Un appel comme `self.jump()` est valide si `jump()` est défini dans un prototype parent.

L’absence de `super` :

- élimine les dépendances implicites à l’implémentation parente

- renforce l’indépendance des prototypes enfants

- garantit une résolution simple et prédictible

---

### 10.6 Ce qui n’existe pas volontairement

ProtoScript V2 exclut explicitement :

- les classes

- les interfaces

- les traits ou mixins

- les casts dynamiques

- la RTTI utilisateur

- la modification dynamique des prototypes

Ces exclusions sont **des choix de conception**, et non des limitations accidentelles.

---

### 10.7 Prototypes et compilation

Le modèle prototype-based de ProtoScript V2 est conçu pour être **pleinement compilable**.

Il permet :

- un layout mémoire déterministe

- une résolution des champs et méthodes à la compilation

- l’absence de tables virtuelles dynamiques

- une génération directe de structures C stables

Chaque prototype correspond à une structure concrète connue à la compilation.  
La délégation est **résolue statiquement**, sans coût d’indirection dynamique.

---

### 10.8 Note de positionnement : Self et JavaScript

Le modèle de ProtoScript V2 s’inscrit dans la lignée conceptuelle du langage [**Self: The Power of Simplicity**](https://bibliography.selflanguage.org/_static/self-power.pdf)  (Ungar & Smith, 1987), qui a posé les bases du prototype-based programming :

- objets sans classes

- clonage explicite

- délégation comme mécanisme fondamental

Cependant, ProtoScript V2 s’en distingue par un choix assumé :

> **la délégation est statique et non dynamique**.

Contrairement à JavaScript (cf. [**ECMAScript Language Specification**](https://tc39.es/ecma262/)), ProtoScript V2 :

- ne permet pas la mutation des chaînes de prototypes,

- ne mélange pas prototypes et classes syntaxiques,

- n’introduit aucune ambiguïté liée à `this`.

Les confusions historiques mises en lumière par [**JavaScript: The Good Parts**](https://openlibrary.org/works/OL9486352W/JavaScript?edition=key:/books/OL23093977M) (Crockford, 2008) sont volontairement évitées.

ProtoScript V2 adopte ainsi un modèle que l’on peut qualifier de :

> **prototype-based statique à layout figé**

Ce positionnement vise la clarté conceptuelle, la sûreté sémantique et l’efficacité de compilation.

### 10.9 Filiation conceptuelle : Self → Io → ProtoScript V2

Le langage Self (Ungar & Smith, 1987) a introduit le modèle prototype-based en supprimant toute notion de classe au profit d’objets clonés et de délégation.
Cette approche a démontré qu’un modèle orienté objet pouvait être à la fois plus simple et plus expressif qu’un système class-based traditionnel.
Le langage Io, conçu par Steve Dekorte, s’inscrit directement dans cette lignée en radicalisant la simplicité syntaxique et la réflexivité du modèle.
Io adopte une délégation entièrement dynamique et une métaprogrammation étendue.
ProtoScript V2 reprend le principe fondamental du prototype comme objet concret.
Il s’en distingue par un choix volontairement opposé sur le plan technique.
Les relations de prototypes y sont figées à la compilation.
La résolution des champs et méthodes est strictement statique.
Aucune mutation dynamique des prototypes n’est autorisée.
Ce positionnement vise la clarté sémantique, la sûreté et l’efficacité de compilation.

### 10.10 Comparaison des modèles prototype-based

*(Self / Io / JavaScript / ProtoScript V2)*

| Critère                       | **Self**               | **Io**               | **JavaScript**             | **ProtoScript V2**              |
| ----------------------------- | ---------------------- | -------------------- | -------------------------- | ------------------------------- |
| Modèle objet                  | Prototype-based pur    | Prototype-based pur  | Prototype-based hybride    | Prototype-based statique        |
| Classes                       | Absentes               | Absentes             | Introduites syntaxiquement | Absentes                        |
| Création d’objets             | Clonage                | Clonage              | Constructeurs / prototypes | Clonage explicite               |
| Délégation                    | Dynamique              | Dynamique            | Dynamique                  | **Statique**                    |
| Chaîne de prototypes          | Dynamique              | Dynamique            | Dynamique mutable          | **Figée à la compilation**      |
| Mutation des prototypes       | Autorisée              | Autorisée            | Autorisée                  | **Interdite**                   |
| Lookup des méthodes           | Tardif (runtime)       | Tardif (runtime)     | Tardif (runtime)           | **Compilation**                 |
| Résolution de `self` / `this` | Dynamique              | Dynamique            | Dynamique et contextuelle  | **Statique (`self`)**           |
| `super`                       | Non                    | Non                  | Oui (syntaxe class)        | **Absent**                      |
| Override                      | Dynamique              | Dynamique            | Dynamique                  | **Statique, signature stricte** |
| Surcharge                     | Possible dynamiquement | Possible             | Possible                   | **Interdite**                   |
| RTTI utilisateur              | Présente               | Présente             | Présente                   | **Absente**                     |
| Métaprogrammation             | Étendue                | Très étendue         | Étendue                    | **Absente**                     |
| Typage                        | Dynamique              | Dynamique            | Dynamique                  | **Statique**                    |
| Layout mémoire                | Non garanti            | Non garanti          | Non garanti                | **Déterministe**                |
| Compilation native            | Non prioritaire        | Non prioritaire      | Secondaire                 | **Objectif central**            |
| Objectif principal            | Recherche conceptuelle | Simplicité réflexive | Généraliste                | **Clarté, sûreté, performance** |

---

## 11. Collections

### 11.1 `list<T>`

- Mutable et possédante.
- `list[i] = x` est une écriture stricte : l'index doit exister.
- Pas de redimensionnement implicite via indexation.
- `T` est un type explicite ; il peut aussi désigner un type prototype (objet), la substitution parent/enfant est validée statiquement.

Exemple :

```c
list<int> xs = [10, 20];
xs[1] = 30; // mise à jour
xs.push(40);
int v = xs.pop();
```
Ref: EX-059

Contre-exemple :

```c
list<int> xs = [1];
// xs[3] = 10; // runtime OOB // Erreur : R1002 RUNTIME_INDEX_OOB
```
Ref: EX-060

### 11.1.1 API `list<T>`

Méthodes disponibles :

| Méthode | Signature | Résultat | Erreurs |
|---|---|---|---|
| `length()` | `() -> int` | longueur | — |
| `isEmpty()` | `() -> bool` | vrai si vide | — |
| `push(x)` | `(T) -> int` | nouvelle longueur | type si `T` incompatible |
| `pop()` | `() -> T` | dernier élément | erreur statique si liste prouvée vide, sinon runtime si vide |
| `contains(x)` | `(T) -> bool` | présence | type si `T` incompatible |
| `sort()` | `() -> int` | longueur | erreur statique si `T` non comparable ou si `compareTo` absent/invalide |
| `reverse()` | `() -> int` | longueur | — |
| `join(sep)` | `(string) -> string` | concat avec séparateur | runtime si liste non `list<string>` |
| `concat()` | `() -> string` | concat sans séparateur | runtime si liste non `list<string>` |

Notes sur `sort()` :

- tri en place, **stable** et déterministe.
- aucune variante `sort(cmp)` n’existe.

Exemple (types primitifs) :

```c
list<int> xs = [3, 1, 2];
xs.sort();
```
Ref: EX-064

Exemple (prototype avec `compareTo`) :

```c
prototype Item {
    int key;
    int id;

    function compareTo(Item other) : int {
        if (self.key < other.key) return -1;
        if (self.key > other.key) return 1;
        return 0;
    }
}

function main() : void {
    Item a = Item.clone();
    a.key = 2;
    a.id = 10;

    Item b = Item.clone();
    b.key = 1;
    b.id = 20;

    list<Item> xs = [a, b];
    xs.sort();
}
```
Ref: EX-065

`reverse()` inverse l’ordre des éléments en place, sans créer de nouveaux éléments.

Exemple (liste d’entiers) :

```c
list<int> xs = [1, 2, 3];
xs.reverse();
```
Ref: EX-066

Exemple (prototype utilisateur) :

```c
prototype Item {
    int id;
}

function main() : void {
    Item a = Item.clone();
    a.id = 1;
    Item b = Item.clone();
    b.id = 2;

    list<Item> xs = [a, b];
    xs.reverse();
}
```
Ref: EX-067

### 11.2 `map<K,V>` : lecture stricte, écriture constructive

- `K` et `V` sont des types explicites ; ils peuvent aussi désigner des types prototypes (objets), la substitution parent/enfant est validée statiquement.

```c
map<string, int> m = {};
m["a"] = 1;    // insertion (clé absente)
m["a"] = 2;    // mise à jour (clé présente)
int x = m["a"]; // lecture valide
```
Ref: EX-061

Littéral direct :

```c
map<string, int> m = {"a": 3, "b": 2, "c": 1};
```
Ref: EX-062

Contre-exemple :

```c
map<string, int> m = {};
int x = m["absent"]; // runtime missing key // Erreur : R1003 RUNTIME_MISSING_KEY
```
Ref: EX-063

### 11.2.1 API `map<K,V>`

| Méthode | Signature | Résultat | Erreurs |
|---|---|---|---|
| `length()` | `() -> int` | nombre d’entrées | — |
| `isEmpty()` | `() -> bool` | vrai si vide | — |
| `containsKey(k)` | `(K) -> bool` | présence clé | type si clé incompatible |
| `remove(k)` | `(K) -> bool` | supprime la clé si présente | ne lève jamais pour clé absente |
| `keys()` | `() -> list<K>` | liste des clés | — |
| `values()` | `() -> list<V>` | liste des valeurs | — |

### 11.2.2 Trier une map par clé ou par valeur

Une `map` conserve l’ordre d’insertion et **ne** définit **aucune** méthode `sort()`.
Pour trier, il faut extraire une `list` (par exemple via `keys()`) puis utiliser `list.sort()`.

Exemple (tri par clé) :

```c
map<string, int> m = {"b": 2, "a": 1, "c": 3};
list<string> ks = m.keys();
ks.sort();
for (string k of ks) {
    int v = m[k];
    string line = k.concat(":").concat(v.toString());
    Io.printLine(line);
}
```
Ref: EX-097

Exemple (tri par valeur, valeurs uniques) :

```c
map<string, int> m = {"a": 3, "b": 1, "c": 2};
list<int> vals = m.values();
vals.sort();
list<string> ks = m.keys();
for (int v of vals) {
    for (string k of ks) {
        if (m[k] == v) {
            string line = k.concat(":").concat(v.toString());
            Io.printLine(line);
            break;
        }
    }
}
```
Ref: EX-098

### 11.3 Erreur fréquente

Supposer que `map[k]` en lecture crée automatiquement une entrée. Ce n'est vrai qu'en écriture (`map[k] = v`).

### 11.4 Pourquoi ?

La distinction lecture stricte / écriture constructive rend les effets de bord visibles.

### 11.5 Itération

```c
for (int v of xs) { ... }
for (string k in m) { ... } // clés
for (int v of m) { ... }    // valeurs
```
Ref: EX-064

Notes :

- L’ordre d’itération des maps est l’ordre d’insertion courant.
- Si une clé est supprimée puis ré‑insérée, elle apparaît **en fin** d’ordre.

### 11.6 Erreur fréquente

Confondre les erreurs statiques et runtime :

- `list.pop()` : erreur statique si la liste est **prouvée vide**, sinon exception runtime si elle est vide à l’exécution.

---

## 12. Slices et vues

### 12.1 `slice<T>` vs `view<T>`

- `slice<T>` : vue mutable, non possédante.
- `view<T>` : vue lecture seule, non possédante.

### 12.2 Création

```c
list<int> xs = [1, 2, 3, 4];
slice<int> s = xs.slice(1, 2);
view<int> v = xs.view(0, 3);
```
Ref: EX-065

### 12.3 Écriture

```c
s[0] = 99; // autorisé
// v[0] = 99; // invalide (view en lecture seule)
```
Ref: EX-066

### 12.3.1 API `slice<T>` / `view<T>`

| Méthode | Signature | Résultat | Erreurs |
|---|---|---|---|
| `length()` | `() -> int` | longueur | — |
| `isEmpty()` | `() -> bool` | vrai si vide | — |
| `slice(start, len)` | `(int,int) -> slice<T>` | sous‑vue mutable | runtime si hors bornes |
| `view(start, len)` | `(int,int) -> view<T>` | sous‑vue readonly | runtime si hors bornes |

`string.view(start,len)` retourne `view<glyph>` avec indexation glyphique.

### 12.4 Durée de vie et invalidation

Une vue ne doit pas survivre au stockage source.
Les mutations structurelles du stockage source peuvent invalider des vues.

Concrètement :

- `slice`/`view` référencent le **même stockage** que la `list` source.
- une mutation structurelle (`push`, `pop`, réallocation interne) peut déplacer le buffer.
- toute vue créée avant cette mutation peut devenir invalide.
- une vue invalidée **ne doit plus être utilisée** : tout accès après invalidation est une **erreur runtime**.

Comment s’assurer qu’une vue reste valide :

- vous pouvez **intercepter l’exception** si un accès est invalide.
- en dehors de cela, il n’existe **pas** d’API de validation runtime.
- la règle est **discipline de code** : ne pas muter structurellement la source tant que la vue est utilisée.
- cela concerne **toutes** les vues : `view<T>` **et** `slice<T>`.

En cas d’accès après invalidation, une **exception runtime** peut être levée et **interceptée** :

```c
list<int> xs = [1, 2, 3];
view<int> v = xs.view(0, 2);
xs.push(4); // peut invalider v

try {
    int a = v[0]; // accès potentiellement invalide
} catch (Exception e) {
    Io.printLine("vue invalidée");
}
```
Ref: EX-067

Exemple :

```c
list<int> xs = [1, 2, 3];
view<int> v = xs.view(0, 2); // v -> [1,2]

xs.push(4); // mutation structurelle : v peut être invalidée

// l’accès ci‑dessous est invalide si le buffer a bougé
// int a = v[0];
```
Ref: EX-068

### 12.5 Erreur fréquente

Traiter `view<T>` comme un `list<T>` léger. `view<T>` n'est pas possédante et interdit l'écriture.

---

## 13. Chaînes (`string`)

### 13.1 Modèle

- `string` est immuable.
- Sémantique en glyphes Unicode.
- `string` n'est pas un `byte[]`.

### 13.2 Longueur et indexation glyphique

```c
string s = "a😀b";
int n = s.length(); // 3 glyphes
glyph g = s[1];     // 😀
```
Ref: EX-069

Index hors bornes :

```c
// runtime OOB
// glyph g = s[99];
```
Ref: EX-070

### 13.3 Combining marks

`string` suit les glyphes/scalaires définis par le langage, pas une indexation brute par octet.

### 13.4 Immutabilité

```c
string s = "abc";
// s[0] = "x"[0]; // invalide
```
Ref: EX-071

Exemple d'approche correcte (création d'une nouvelle chaîne) :

```c
string s = "abc";
string t = s.concat("x"); // s reste inchangée
```
Ref: EX-072

### 13.5 Comparaison utile (PHP/JS/C)

- JS/PHP/C confondent souvent octets, code units et caractères utilisateurs.
- ProtoScript V2 impose une sémantique glyphique explicite pour éviter ces ambiguïtés.

### 13.6 Erreur fréquente

Supposer que `string[i]` modifie la chaîne. Toute mutation indexée de `string` est interdite.

### 13.7 Méthodes principales

Exemples :

```c
string s = "  abc  ";
bool a = s.startsWith("  ");
bool b = s.endsWith("  ");
int p = s.indexOf("bc"); // index en glyphes
string t = s.trim();     // retire espaces ASCII en début/fin
string u = s.replace("a", "A"); // première occurrence
list<string> parts = "a,b,c".split(",");
```
Ref: EX-073

Notes :

- `split` ne fait aucun traitement regex
- les indices de `indexOf` sont exprimés en glyphes
- `trim`, `trimStart`, `trimEnd` retirent uniquement `' '`, `'\t'`, `'\n'`, `'\r'`

### 13.8 Pourquoi ?

Immutabilité + sémantique glyphique = comportement stable, coûts visibles, pas de magie cachant des copies.

### 13.9 UTF-8 explicite (bytes)

Si vous devez manipuler des octets, utilisez une `list<byte>`.
La conversion est explicite et strictement validée.

```c
string s = "Le cœur déçu mais l'âme plutôt naïve, Louÿs rêva de crapaüter en canoë au delà des îles, près du mälström où brûlent les novæ.";
list<byte> bytes = s.toUtf8Bytes();
string back = bytes.toUtf8String();
```
Ref: EX-074

Si la liste de bytes n'est pas un UTF-8 valide, `toUtf8String()` lève une exception runtime.

### 13.10 Sous-chaînes (substring)

`substring(start, length)` extrait une sous-chaîne en indices de glyphes.
Elle retourne une **nouvelle** chaîne et ne crée pas de vue partagée.

```c
string s = "a😀b";
string t = s.substring(1, 1); // "😀"
```
Ref: EX-075

Erreurs :

- `start` ou `length` hors bornes lève une exception runtime

Note :

Il n'existe pas d'API de slicing/view pour `string`. L'extraction est explicite et copie la sous-chaîne.

---

## 14. Modules

### 14.1 Imports

Deux formes existent :

- import de module avec alias (espace de noms)
- import explicite de symboles
- import par chemin (string literal)

```c
import Io;
import Io as io;
import Math.{abs, sqrt as racine};
import JSON.{encode, decode};
import "./datastruct/Stack.pts";
import "/abs/path/collections/Stack.pts".{push, pop};
```
Ref: EX-076

Mini‑grammaire (EBNF) :

```ebnf
ImportStmt   = "import" ImportTarget ";" ;
ImportTarget =
    ImportByName
  | ImportByPath ;
ImportByName =
    ModulePath [ "as" Identifier ]
  | ModulePath "." "{" ImportItem { "," ImportItem } "}" ;
ImportByPath =
    StringLiteral [ "as" Identifier ]
  | StringLiteral "." "{" ImportItem { "," ImportItem } "}" ;
ModulePath   = Identifier { "." Identifier } ;
ImportItem   = Identifier [ "as" Identifier ] ;
```

Exemples d'usage :

```c
float x = abs(-9.0);
float y = racine(x);
string s = encode(decode("{\"value\":1}"));
```
Ref: EX-077

Règles supplémentaires pour l’import par chemin :

- le chemin doit référencer un fichier `.pts`
- chemin relatif : résolu par rapport au fichier courant
- chemin absolu : utilisé tel quel
- aucune recherche via `search_paths` n’est effectuée

Erreurs statiques dédiées :

- `E2002` : `IMPORT_PATH_NOT_FOUND`
- `E2003` : `IMPORT_PATH_BAD_EXTENSION`
- `E2004` : `IMPORT_PATH_NO_ROOT_PROTO`

### 14.2 Visibilité et noms

- Import explicite des symboles.
- Aliases explicites.
- Pas de wildcard import.

Contre-exemple :

```c
// invalide
// import std.io.*; // Erreur : E1001 PARSE_UNEXPECTED_TOKEN
```
Ref: EX-078

### 14.3 Résolution statique

Les symboles de module sont résolus à la compilation.
Aucun chargement dynamique.

### 14.4 Modules natifs

Les modules natifs étendent l'environnement de noms, pas la sémantique du langage.
Documentation officielle : `docs/native-modules.md`.

Note :
un prototype **non exporté** par un module natif est simplement **inaccessible** depuis l’extérieur.
Cela ne modifie pas le mécanisme d’instanciation par `Type.clone()`.

### 14.5 Registry des modules standards

Le chargeur utilise un registry JSON pour résoudre `import Io`, `import Math`, `import JSON`.

- Surcharge possible via `PS_MODULE_REGISTRY` (chemin absolu ou relatif).
- Ordre de recherche par défaut :
`PS_MODULE_REGISTRY`,
`registry.json` à côté du binaire `ps`,
`./registry.json`,
`/etc/ps/registry.json`,
`/usr/local/etc/ps/registry.json`,
`/opt/local/etc/ps/registry.json`,
`./modules/registry.json`.

### 14.4.1 Module standard : Io

**Constantes**

| Nom | Type | Description |
|---|---|---|
| `Io.EOL` | `string` | fin de ligne (`"\n"`) |
| `Io.stdin` | `TextFile` | flux standard d’entrée (texte) |
| `Io.stdout` | `TextFile` | flux standard de sortie (texte) |
| `Io.stderr` | `TextFile` | flux standard d’erreur (texte) |

**Fonctions globales**

| Fonction | Signature | Description | Erreurs |
|---|---|---|---|
| `Io.openText` | `(string path, string mode) -> TextFile` | ouvre un fichier texte | `InvalidModeException`, `InvalidPathException`, `FileNotFoundException`, `PermissionDeniedException`, `FileOpenException` |
| `Io.openBinary` | `(string path, string mode) -> BinaryFile` | ouvre un fichier binaire | `InvalidModeException`, `InvalidPathException`, `FileNotFoundException`, `PermissionDeniedException`, `FileOpenException` |
| `Io.tempPath` | `() -> string` | chemin temporaire unique (non créé) | `IOException` |
| `Io.print` | `(any value) -> void` | écrit sans fin de ligne | `InvalidArgumentException`, `WriteFailureException` |
| `Io.printLine` | `(any value) -> void` | écrit + `Io.EOL` | `InvalidArgumentException`, `WriteFailureException` |

Notes :

- `Io.openText(...)` / `Io.openBinary(...)` lèvent une exception runtime si l’ouverture échoue.
- en cas d’échec, **aucun handle n’est retourné**.
- `Io.tempPath()` retourne un chemin **inexistant** et **ne crée pas le fichier**.
- `Io.tempPath()` utilise `$TMPDIR` sinon `/tmp` et ne protège pas contre une race condition externe.
- `Io.print(...)` / `Io.printLine(...)` : si `value` n’est pas une `string`, `toString()` est appelé et doit retourner une `string`, sinon `InvalidArgumentException`.
- Exceptions Io (toutes `RuntimeException`) : `InvalidModeException`, `FileOpenException`, `FileNotFoundException`, `PermissionDeniedException`, `InvalidPathException`, `FileClosedException`, `InvalidArgumentException`, `InvalidGlyphPositionException`, `ReadFailureException`, `WriteFailureException`, `Utf8DecodeException`, `StandardStreamCloseException`, `IOException`.

**Méthodes sur `TextFile`**

| Méthode | Signature | Description | Erreurs |
|---|---|---|---|
| `read(size)` | `(int) -> string` | lit `size` glyphes | `InvalidArgumentException`, `FileClosedException`, `Utf8DecodeException`, `ReadFailureException` |
| `write(text)` | `(string) -> void` | écrit du texte | `InvalidArgumentException`, `FileClosedException`, `WriteFailureException` |
| `tell()` | `() -> int` | position en glyphes | `FileClosedException`, `ReadFailureException` |
| `seek(pos)` | `(int) -> void` | positionne en glyphes | `InvalidArgumentException`, `InvalidGlyphPositionException`, `FileClosedException`, `ReadFailureException` |
| `size()` | `() -> int` | taille en glyphes | `FileClosedException`, `ReadFailureException` |
| `name()` | `() -> string` | nom/chemin | `FileClosedException` |
| `close()` | `() -> void` | ferme le fichier | `StandardStreamCloseException` si stdin/stdout/stderr |

**Méthodes sur `BinaryFile`**

| Méthode | Signature | Description | Erreurs |
|---|---|---|---|
| `read(size)` | `(int) -> list<byte>` | lit `size` octets | `InvalidArgumentException`, `FileClosedException`, `ReadFailureException` |
| `write(bytes)` | `(list<byte>) -> void` | écrit des octets | `InvalidArgumentException`, `FileClosedException`, `WriteFailureException` |
| `tell()` | `() -> int` | position en octets | `FileClosedException`, `ReadFailureException` |
| `seek(pos)` | `(int) -> void` | positionne en octets | `InvalidArgumentException`, `FileClosedException`, `ReadFailureException` |
| `size()` | `() -> int` | taille en octets | `FileClosedException`, `ReadFailureException` |
| `name()` | `() -> string` | nom/chemin | `FileClosedException` |
| `close()` | `() -> void` | ferme le fichier | `StandardStreamCloseException` si stdin/stdout/stderr |

Notes :

- en texte, `read(size)` retourne une `string` dont la longueur est le nombre de **glyphes** lus.
- en binaire, `read(size)` retourne un `list<byte>`.
- `read(size)` retourne une **valeur de longueur nulle** (`length == 0`) si EOF.
- les écritures sont atomiques : aucune écriture partielle ne doit être observable et en cas d’échec la position du curseur est inchangée.

Exemple :

Écriture de texte :

```c
TextFile f = Io.openText("out.txt", "w");
f.write("hello");
f.close();
Io.printLine("done");
```
Ref: EX-079

Lecture de texte :

```c
TextFile f = Io.openText("in.txt", "r");
int n = f.size();
string data = f.read(n);
f.close();
```
Ref: EX-080

Lecture binaire et écriture binaire :

```c
BinaryFile f = Io.openBinary("in.bin", "r");
list<byte> bytes = f.read(1024);
f.close();

BinaryFile g = Io.openBinary("out.bin", "w");
g.write(bytes);
g.close();
```
Ref: EX-081

Chemin temporaire :

```c
string p = Io.tempPath();
TextFile f = Io.openText(p, "w");
f.write("temp");
f.close();
```
Ref: EX-081A

Écrire sur `Io.stderr` :

```c
Io.stderr.write("error\n");
```
Ref: EX-082

### 14.4.2 Module standard : Math

**Constantes** (toutes `float`)

| Nom | Valeur | Description |
|---|---|---|
| `Math.PI` | π | constante π |
| `Math.E` | e | constante e |
| `Math.LN2` | ln(2) | logarithme naturel de 2 |
| `Math.LN10` | ln(10) | logarithme naturel de 10 |
| `Math.LOG2E` | log2(e) | log base 2 de e |
| `Math.LOG10E` | log10(e) | log base 10 de e |
| `Math.SQRT1_2` | √(1/2) | racine de 1/2 |
| `Math.SQRT2` | √2 | racine de 2 |

**Fonctions** (toutes pures, retour `float`)

| Fonction | Signature | Unités / domaine | Résultat / notes |
|---|---|---|---|
| `abs` | `abs(float x) -> float` | tout réel | \|x\| |
| `min` | `min(float a, float b) -> float` | tout réel | plus petit des deux |
| `max` | `max(float a, float b) -> float` | tout réel | plus grand des deux |
| `floor` | `floor(float x) -> float` | tout réel | ⌊x⌋ |
| `ceil` | `ceil(float x) -> float` | tout réel | ⌈x⌉ |
| `round` | `round(float x) -> float` | tout réel | arrondi au plus proche |
| `trunc` | `trunc(float x) -> float` | tout réel | troncature vers 0 |
| `sign` | `sign(float x) -> float` | tout réel (NaN inclus) | −1, +1, 0, −0, NaN (voir contrat) |
| `fround` | `fround(float x) -> float` | tout réel | arrondi float (IEEE‑754) |
| `sqrt` | `sqrt(float x) -> float` | x ≥ 0 | √x, sinon NaN |
| `cbrt` | `cbrt(float x) -> float` | tout réel | ∛x |
| `pow` | `pow(float a, float b) -> float` | tout réel | a^b (IEEE‑754) |
| `sin` | `sin(float x) -> float` | radians | sin(x) |
| `cos` | `cos(float x) -> float` | radians | cos(x) |
| `tan` | `tan(float x) -> float` | radians | tan(x) |
| `asin` | `asin(float x) -> float` | x ∈ [−1, 1] | arcsin(x) en radians, sinon NaN |
| `acos` | `acos(float x) -> float` | x ∈ [−1, 1] | arccos(x) en radians, sinon NaN |
| `atan` | `atan(float x) -> float` | tout réel | arctan(x) en radians |
| `atan2` | `atan2(float y, float x) -> float` | tout réel | arctan(y/x) en radians, quadrant correct |
| `sinh` | `sinh(float x) -> float` | tout réel | sinh(x) |
| `cosh` | `cosh(float x) -> float` | tout réel | cosh(x) |
| `tanh` | `tanh(float x) -> float` | tout réel | tanh(x) |
| `asinh` | `asinh(float x) -> float` | tout réel | asinh(x) |
| `acosh` | `acosh(float x) -> float` | x ≥ 1 | acosh(x), sinon NaN |
| `atanh` | `atanh(float x) -> float` | x ∈ (−1, 1) | atanh(x), sinon NaN |
| `exp` | `exp(float x) -> float` | tout réel | e^x |
| `expm1` | `expm1(float x) -> float` | tout réel | e^x − 1 |
| `log` | `log(float x) -> float` | x > 0 | ln(x), sinon NaN |
| `log1p` | `log1p(float x) -> float` | x > −1 | ln(1 + x), sinon NaN |
| `log2` | `log2(float x) -> float` | x > 0 | log2(x), sinon NaN |
| `log10` | `log10(float x) -> float` | x > 0 | log10(x), sinon NaN |
| `hypot` | `hypot(float a, float b) -> float` | tout réel | √(a² + b²) |
| `clz32` | `clz32(float x) -> float` | entier 32 bits (float) | count leading zeros (JS‑like) |
| `imul` | `imul(float a, float b) -> float` | entiers 32 bits (float) | multiplication 32 bits (JS‑like) |
| `random` | `random() -> float` | — | uniforme dans `[0.0, 1.0)` |

Paramètres :

- Les arguments `int` sont acceptés et convertis implicitement en `float`.
- Tout autre type **doit** provoquer une erreur runtime de type.
- Les fonctions trigonométriques (`sin`, `cos`, `tan`, `asin`, `acos`, `atan`, etc.) utilisent des **radians**.

### 14.4.2.0 Domaine et valeurs limites

Les fonctions Math suivent IEEE‑754 :

- pour les cas **hors domaine mathématique**, le résultat est **NaN** (aucune exception implicite).
- pour les débordements, le résultat peut être **+Infinity** ou **−Infinity**.
- `-0` est préservé si le résultat IEEE‑754 est `-0`.

Exemples typiques :

```c
float a = Math.log(-1.0); // NaN
float b = Math.sqrt(-1.0); // NaN
float c = Math.exp(1000.0); // +Infinity (overflow)
```
Ref: EX-083

### 14.4.2.1 Contrat NaN / ±Infinity / −0

- Sémantique IEEE‑754 (double précision).
- `NaN`, `+Infinity`, `−Infinity` peuvent être produits.
- Aucune exception implicite pour valeurs hors domaine : retour `NaN` ou `±Infinity`.
- `-0` est préservé si le résultat IEEE‑754 est `-0`.
- Comparaisons avec `NaN` : `NaN != NaN` est `true`, comparaisons ordonnées avec `NaN` sont `false`.

`Math.sign` :

- retourne `NaN` si l’argument est `NaN`,
- retourne `-0` si l’argument est `-0`,
- retourne `-1` si l’argument est négatif,
- retourne `1` si l’argument est positif,
- retourne `0` si l’argument est `+0`.

### 14.4.2.2 `Math.random()`

- PRNG interne au runtime (pas de `rand()` libc).
- aucun seed exposé au langage.
- déterministe à état initial identique.
- retourne un `float` dans `[0.0, 1.0)`.
- pas de dépendance système.

### 14.4.2.3 Erreurs runtime

Format : code + catégorie + message, ex. `R1010 RUNTIME_TYPE_ERROR`.

Erreurs typiques :

- type invalide → `R1010 RUNTIME_TYPE_ERROR`

Exemples :

```c
float a = Math.abs(-3.5);
float b = Math.sqrt(9.0);      // 3.0
float c = Math.log(Math.E);    // 1.0
float d = Math.pow(2.0, 3.0);  // 8.0
```
Ref: EX-084

Exemple trigonométrique (radians) :

```c
float s = Math.sin(Math.PI / 2.0); // ~1.0
```
Ref: EX-085

### 14.4.3 Module standard : JSON

**Fonctions**

| Fonction | Signature | Description | Erreurs |
|---|---|---|---|
| `encode` | `(any) -> string` | sérialise | runtime si valeur non sérialisable |
| `decode` | `(string) -> JSONValue` | parse JSON | runtime si JSON invalide |
| `isValid` | `(string) -> bool` | valide sans exception | runtime si argument non string |

Notes :

- `encode` accepte un `JSONValue` ou des valeurs récursivement sérialisables : `bool`, `int`, `float`, `string`, `list<T>`, `map<string,T>`.
- `NaN`, `+Infinity`, `-Infinity` sont **interdits** à l’encode → exception runtime.
- `-0` est préservé.
- `decode` parse du JSON UTF‑8 strict et retourne un `JSONValue`.

**Type `JSONValue` (scellé)**

`JSONValue` est un type somme standard **scellé**.  
Il ne peut pas être étendu par l’utilisateur.

Sous‑types : `JsonNull`, `JsonBool`, `JsonNumber`, `JsonString`, `JsonArray`, `JsonObject`.

Constructeurs explicites (immutables) :

| Fonction | Signature | Description |
|---|---|---|
| `JSON.null` | `() -> JSONValue` | null JSON |
| `JSON.bool` | `(bool) -> JSONValue` | bool JSON |
| `JSON.number` | `(float) -> JSONValue` | nombre JSON |
| `JSON.string` | `(string) -> JSONValue` | chaîne JSON |
| `JSON.array` | `(list<JSONValue>) -> JSONValue` | tableau JSON |
| `JSON.object` | `(map<string, JSONValue>) -> JSONValue` | objet JSON |

Méthodes d’accès :

| Méthode | Résultat | Erreurs |
|---|---|---|
| `isNull()/isBool()/isNumber()/isString()/isArray()/isObject()` | `bool` | — |
| `asBool()/asNumber()/asString()/asArray()/asObject()` | type correspondant | runtime si mauvais type |

Exemple :

```c
string s = JSON.encode({"a": 1, "b": [true, false]});
JSONValue v = JSON.decode(s);
bool ok = JSON.isValid("{\"x\":1}");
```
Ref: EX-086

### 14.4.4 Module standard : Time

**Fonctions**

| Fonction | Signature | Description |
|---|---|---|
| `nowEpochMillis` | `() -> int` | epoch UTC en millisecondes (non déterministe) |
| `nowMonotonicNanos` | `() -> int` | horloge monotone en nanosecondes (non déterministe) |
| `sleepMillis` | `(int ms) -> void` | suspend l’exécution au moins `ms` millisecondes |

Notes :

- `nowEpochMillis` retourne un epoch **UTC** (millisecondes).
- `nowMonotonicNanos` est monotone, indépendante de l’horloge murale.
- `sleepMillis` peut dépasser la durée demandée.

Exemple :

```c
import Io;
import Time;

function main() : void {
    int start = Time.nowEpochMillis();
    Time.sleepMillis(10);
    int end = Time.nowEpochMillis();
    Io.printLine((end >= start).toString());
}
```
Ref: EX-087

### 14.4.5 Module standard : TimeCivil

**Prototype standard : `CivilDateTime`**

Champs (tous `int`) :

- `year`, `month`, `day`
- `hour`, `minute`, `second`, `millisecond`

**Constantes DST**

| Nom | Valeur | Usage |
|---|---|---|
| `TimeCivil.DST_EARLIER` | `0` | choisit l’occurrence la plus tôt |
| `TimeCivil.DST_LATER` | `1` | choisit l’occurrence la plus tard |
| `TimeCivil.DST_ERROR` | `2` | lève une exception en cas d’ambiguïté |

**Fonctions**

| Fonction | Signature | Description | Exceptions |
|---|---|---|---|
| `fromEpochUTC` | `(int) -> CivilDateTime` | epoch → civil UTC | `InvalidDateException` |
| `toEpochUTC` | `(CivilDateTime) -> int` | civil UTC → epoch | `InvalidDateException` |
| `fromEpoch` | `(int, string tz) -> CivilDateTime` | epoch → civil dans `tz` | `InvalidTimeZoneException`, `InvalidDateException` |
| `toEpoch` | `(CivilDateTime, string tz, int strategy) -> int` | civil → epoch dans `tz` | `InvalidTimeZoneException`, `InvalidDateException`, `DSTNonExistentTimeException`, `DSTAmbiguousTimeException` |
| `isDST` | `(int, string tz) -> bool` | vrai si offset ≠ standard | `InvalidTimeZoneException`, `InvalidDateException` |
| `offsetSeconds` | `(int, string tz) -> int` | offset UTC total | `InvalidTimeZoneException`, `InvalidDateException` |
| `standardOffsetSeconds` | `(string tz) -> int` | offset hors DST | `InvalidTimeZoneException` |
| `dayOfWeek` | `(int, string tz) -> int` | 1=lundi … 7=dimanche | `InvalidTimeZoneException`, `InvalidDateException` |
| `dayOfYear` | `(int, string tz) -> int` | 1–365/366 | `InvalidTimeZoneException`, `InvalidDateException` |
| `weekOfYearISO` | `(int, string tz) -> int` | semaine ISO 8601 | `InvalidTimeZoneException`, `InvalidDateException` |
| `weekYearISO` | `(int, string tz) -> int` | année ISO 8601 | `InvalidTimeZoneException`, `InvalidDateException` |
| `isLeapYear` | `(int year) -> bool` | année bissextile | — |
| `daysInMonth` | `(int year, int month) -> int` | nombre de jours | `InvalidDateException` |
| `parseISO8601` | `(string s) -> int` | parse ISO strict → epoch UTC | `InvalidISOFormatException` |
| `formatISO8601` | `(int epoch) -> string` | format UTC `YYYY-MM-DDTHH:MM:SS.sssZ` | `InvalidDateException` |

**Validation `TimeZone`**

- identifiant IANA strict, sensible à la casse ;
- aucun whitespace (leading/trailing ou interne) ;
- pas de normalisation de locale ;
- pas de fallback implicite vers UTC ;
- alias acceptés **uniquement** s’ils existent dans la base IANA du système ;
- `"UTC"` accepté seulement si la base système le supporte.

**Règles DST**

- heure inexistante (spring forward) → **toujours** `DSTNonExistentTimeException`
- heure ambiguë (fall back) → stratégie obligatoire (`DST_EARLIER`, `DST_LATER`, `DST_ERROR`)

**ISO 8601**

- parsing strict : `YYYY-MM-DD`, `YYYY-MM-DDTHH:MM:SS`, `YYYY-MM-DDTHH:MM:SS.sss`, suffixe `Z` ou offset `±HH:MM`
- sans suffixe `Z`/offset, l’interprétation est **UTC**
- pas d’autre format accepté
- `formatISO8601` retourne toujours en UTC avec `Z`

Exemples :

```c
import Io;
import TimeCivil;

function main() : void {
    int epoch = 0;
    CivilDateTime dt = TimeCivil.fromEpochUTC(epoch);
    int round = TimeCivil.toEpochUTC(dt);
    Io.printLine((round == 0).toString());
}
```
Ref: EX-088

```c
import Io;
import TimeCivil;

function main() : void {
    int epoch = TimeCivil.parseISO8601("1970-01-01T00:00:00.000Z");
    string s = TimeCivil.formatISO8601(epoch);
    Io.printLine(s);
}
```
Ref: EX-089

### 14.4.6 Module standard : Fs

Le module `Fs` fournit des primitives synchrones pour le système de fichiers POSIX.

**Exceptions Fs** (toutes `RuntimeException`)

- `FileNotFoundException`
- `NotADirectoryException`
- `NotAFileException`
- `PermissionDeniedException`
- `DirectoryNotEmptyException`
- `InvalidPathException`
- `IOException`

**Fonctions**

| Fonction | Signature | Description | Exceptions |
|---|---|---|---|
| `exists` | `(string path) -> bool` | vrai si le chemin existe | `InvalidPathException`, `IOException` |
| `isFile` | `(string path) -> bool` | vrai si fichier régulier | `InvalidPathException`, `IOException` |
| `isDir` | `(string path) -> bool` | vrai si répertoire | `InvalidPathException`, `IOException` |
| `isSymlink` | `(string path) -> bool` | vrai si lien symbolique | `InvalidPathException`, `IOException` |
| `isReadable` | `(string path) -> bool` | vérifie la lisibilité | `InvalidPathException`, `IOException` |
| `isWritable` | `(string path) -> bool` | vérifie l’écriture | `InvalidPathException`, `IOException` |
| `isExecutable` | `(string path) -> bool` | vérifie l’exécution | `InvalidPathException`, `IOException` |
| `size` | `(string path) -> int` | taille en octets d’un fichier | `FileNotFoundException`, `NotAFileException`, `PermissionDeniedException`, `IOException` |
| `mkdir` | `(string path) -> void` | crée un répertoire | `FileNotFoundException`, `NotADirectoryException`, `PermissionDeniedException`, `InvalidPathException`, `IOException` |
| `rmdir` | `(string path) -> void` | supprime un répertoire vide | `FileNotFoundException`, `NotADirectoryException`, `DirectoryNotEmptyException`, `PermissionDeniedException`, `InvalidPathException`, `IOException` |
| `rm` | `(string path) -> void` | supprime un fichier | `FileNotFoundException`, `NotAFileException`, `PermissionDeniedException`, `InvalidPathException`, `IOException` |
| `cp` | `(string src, string dst) -> void` | copie un fichier | `FileNotFoundException`, `NotAFileException`, `PermissionDeniedException`, `InvalidPathException`, `IOException` |
| `mv` | `(string src, string dst) -> void` | déplace un fichier | `FileNotFoundException`, `PermissionDeniedException`, `InvalidPathException`, `IOException` |
| `chmod` | `(string path, int mode) -> void` | change les permissions POSIX | `FileNotFoundException`, `PermissionDeniedException`, `InvalidPathException`, `IOException` |
| `cwd` | `() -> string` | répertoire courant | `IOException` |
| `cd` | `(string path) -> void` | change de répertoire | `FileNotFoundException`, `NotADirectoryException`, `PermissionDeniedException`, `IOException` |
| `pathInfo` | `(string path) -> PathInfo` | découpe un chemin sans normalisation | `InvalidPathException`, `IOException` |
| `openDir` | `(string path) -> Dir` | ouvre un itérateur de répertoire | `FileNotFoundException`, `NotADirectoryException`, `PermissionDeniedException`, `IOException` |
| `walk` | `(string path, int maxDepth, bool followSymlinks) -> Walker` | parcours récursif itératif | `FileNotFoundException`, `NotADirectoryException`, `PermissionDeniedException`, `IOException` |

Notes :

- Les requêtes de capacité (`isReadable`, `isWritable`, `isExecutable`) renvoient `false` si l’accès est refusé.
- En cas d’erreur système inattendue, les requêtes lèvent `IOException` ou `InvalidPathException`.
- Les opérations mutantes sont atomiques : en cas d’exception, aucune modification partielle n’est visible.
- Les liens symboliques cassés : `exists` retourne `true`, `isFile` et `isDir` retournent `false`.
- Le module Fs.walk fournit un itérateur récursif streaming de l’arborescence des fichiers. Comparé à une implémentation récursive côté utilisateur, il évite les débordements de pile et gère efficacement les arborescences profondes, tout en restant synchrone, déterministe et sans allocation massive.

**Prototype `PathInfo`** (champs en lecture seule)

- `dirname : string`
- `basename : string`
- `filename : string`
- `extension : string`

**Prototype `Dir`**

Méthodes :

| Méthode | Signature | Description | Erreurs |
|---|---|---|---|
| `hasNext` | `() -> bool` | vrai si un `next()` est possible | — |
| `next` | `() -> string` | retourne l’entrée suivante | `IOException` si fin |
| `close` | `() -> void` | ferme le handle | — |
| `reset` | `() -> void` | rembobine le flux | `IOException` en cas d’échec |

Les entrées `.` et `..` sont filtrées.

**Prototype `Walker`**

Méthodes :

| Méthode | Signature | Description | Erreurs |
|---|---|---|---|
| `hasNext` | `() -> bool` | vrai si une entrée suivante existe | — |
| `next` | `() -> PathEntry` | entrée suivante | `IOException` si fin |
| `close` | `() -> void` | libère les ressources | — |

**Prototype `PathEntry`** (champs en lecture seule)

- `path : string`
- `name : string`
- `depth : int`
- `isDir : bool`
- `isFile : bool`
- `isSymlink : bool`

Exemple : listing simple

```c
import Fs;
import Io;

function main() : void {
    Dir d = Fs.openDir(".");
    while (d.hasNext()) {
        Io.printLine(d.next());
    }
    d.close();
}
```
Ref: EX-099

Exemple : walk récursif

```c
import Fs;
import Io;

function main() : void {
    Walker w = Fs.walk(".", -1, false);
    while (w.hasNext()) {
        PathEntry e = w.next();
        Io.printLine(e.path);
    }
    w.close();
}
```
Ref: EX-100

### 14.4.7 Module standard : Sys

Le module `Sys` expose un accès minimal **en lecture seule** à l'environnement du processus et une exécution contrôlée de processus.

**Exceptions Sys** (toutes `RuntimeException`)

- `InvalidArgumentException`
- `EnvironmentAccessException`
- `InvalidEnvironmentNameException`
- `IOException`
- `ProcessCreationException`
- `ProcessExecutionException`
- `ProcessPermissionException`
- `InvalidExecutableException`

**Fonctions**

| Fonction | Signature | Description | Exceptions |
|---|---|---|---|
| `hasEnv` | `(string name) -> bool` | vrai si la variable d'environnement existe | `InvalidEnvironmentNameException`, `EnvironmentAccessException`, `IOException` |
| `env` | `(string name) -> string` | valeur de la variable d'environnement | `InvalidEnvironmentNameException`, `EnvironmentAccessException`, `IOException` |
| `execute` | `(string program, list<string> args, list<byte> input, bool captureStdout, bool captureStderr) -> ProcessResult` | exécution synchrone d'un programme POSIX | `InvalidExecutableException`, `ProcessPermissionException`, `ProcessCreationException`, `ProcessExecutionException`, `InvalidArgumentException`, `IOException` |

Notes :

- Accès **lecture seule** : aucune mutation ni énumération de l'environnement.
- Pas de cache : chaque appel reflète l'état courant du processus.
- Nom invalide si chaîne vide ou si le caractère `=` est présent.
- Les valeurs doivent être du UTF-8 valide ; sinon `EnvironmentAccessException`.
- Une variable existante peut avoir une valeur vide.
- `execute` n'invoque **aucun shell** ; `program` est exécuté tel quel et `args` sont passés verbatim.
- `input` est écrit intégralement sur stdin puis stdin est fermé (EOF).
- Si `captureStdout`/`captureStderr` est `false`, le flux hérite du processus parent.

**Prototype `ProcessResult`** (champs en lecture seule)

- `exitCode : int`
- `events : list<ProcessEvent>`

**Prototype `ProcessEvent`** (champs en lecture seule)

- `stream : int` (`1` = stdout, `2` = stderr)
- `data : list<byte>`

**Ordonnancement**

- `events` est **chronologique** : l'ordre correspond à l'ordre d'observation des lectures multiplexées stdout/stderr.
- La taille des chunks est dépendante de l'implémentation.
- Si le processus est terminé par un signal, `exitCode` est mappé à `128 + signal`.

Exemple : environnement

```c
import Sys;
import Io;

function main() : void {
    if (Sys.hasEnv("HOME")) {
        Io.printLine(Sys.env("HOME"));
    }
}
```
Ref: EX-101

Exemple : exécution

```c
import Sys;
import Io;

function main() : void {
    ProcessResult r = Sys.execute("/bin/echo", ["hello"], [], true, true);
    for (ProcessEvent e in r.events) {
        if (e.stream == 1) {
            Io.printLine(e.data.toUtf8String());
        }
    }
}
```
Ref: EX-102

Le comportement complet est normatif et défini dans :

- `docs/module_io_specification.md`
- `docs/module_math_specification.md`
- `docs/module_json_specification.md`
- `docs/module_fs_specification.md`
- `docs/module_sys_specification.md`
- `docs/module_sys_execute_specification.md`

### 14.5 Ce que les modules ne peuvent pas faire

- introduire de nouveaux opérateurs
- changer les règles de typage
- activer de la RTTI/réflexion
- modifier la grammaire

### 14.6 Pourquoi ?

L'extension est un mécanisme d'intégration, pas un mécanisme de mutation du langage.

---


## 15. Erreurs et exceptions

### 15.1 Erreurs statiques

Diagnostics avec code, catégorie, position `file:line:column`.

### 15.2 Exceptions runtime

Les violations runtime normatives lèvent des exceptions catégorisées.
Toute exception dérive du prototype racine `Exception`.
Aucune autre valeur ne peut être levée avec `throw`.
Les exceptions runtime standard dérivent de `RuntimeException`.
Vous pouvez définir des prototypes dérivés de `Exception`.
Les exceptions s’instancient exclusivement via `clone()` ; `Exception(...)` et `RuntimeException(...)` sont interdits.

### 15.2.1 Codes runtime (résumé)

| Code | Catégorie | Exemple |
|---|---|---|
| `R1001` | `RUNTIME_INT_OVERFLOW` | overflow int |
| `R1002` | `RUNTIME_INDEX_OOB` | index hors bornes |
| `R1003` | `RUNTIME_MISSING_KEY` | map clé absente |
| `R1004` | `RUNTIME_DIVIDE_BY_ZERO` | division par zéro |
| `R1005` | `RUNTIME_SHIFT_RANGE` | décalage invalide |
| `R1006` | `RUNTIME_EMPTY_POP` | pop sur liste vide |
| `R1007` | `RUNTIME_UTF8_INVALID` | UTF‑8 invalide |
| `R1010` | `RUNTIME_TYPE_ERROR` | type runtime incompatible |

### 15.2.2 Exemple de `throw`

```c
import Io;

function main() : void {
    try {
        Exception e = Exception.clone();
        e.message = "Quelque chose s'est mal passe";
        throw e;
    } catch (Exception e) {
        Io.printLine(e.message);
    }
}
```
Ref: EX-087A

### 15.2.3 Exemple d’exception dérivée

```c
import Io;

prototype MyError : Exception {
    string details;
}

function main() : void {
    try {
        MyError e = MyError.clone();
        e.message = "Erreur metier";
        e.details = "code:42";
        throw e;
    } catch (MyError ex) {
        Io.printLine(ex.details);
    }
}
```
Ref: EX-087B

### 15.2.4 Exemples de diagnostics runtime

**Exception non catchée**

```c
function main() : void {
    Exception e = Exception.clone();
    e.message = "boom";
    throw e;
}
```

Sortie attendue :

```
script.pts:4:5 R1011 UNHANDLED_EXCEPTION: unhandled exception. got Exception("boom"); expected matching catch
```

**Division par zéro**

```c
function main() : void {
    int a = 1;
    int b = 0;
    int c = a / b;
}
```

Sortie attendue :

```
script.pts:4:17 R1004 RUNTIME_DIVIDE_BY_ZERO: division by zero. got 0; expected non-zero divisor
```

**Clé manquante dans un map**

```c
function main() : void {
    map<string,int> m = {};
    int v = m["absent"];
}
```

Sortie attendue :

```
script.pts:3:13 R1003 RUNTIME_MISSING_KEY: missing key. got "absent"; expected present key
```

### 15.3 `try / catch / finally`

```c
try {
    risky();
} catch (Exception e) {
    Io.printLine("handled");
} finally {
    Io.printLine("cleanup");
}
```
Ref: EX-087

**Sémantique de filtrage `catch`**

- les clauses `catch` sont évaluées dans l’ordre d’écriture.
- une clause `catch (T e)` correspond si le type dynamique de l’exception est `T` ou dérive de `T`.
- la première clause qui correspond est exécutée ; les suivantes sont ignorées.
- si aucune clause ne correspond, l’exception est propagée après exécution du `finally` (s’il existe).
- `catch (Exception e)` est un **catch‑all**.

### 15.4 Contre-exemple

```c
// invalide : throw d'une valeur non Exception
// throw 42;
```

### 15.5 Erreur fréquente

Confondre absence de RTTI utilisateur et mécanisme `catch` par type : `catch` utilise une métadonnée interne d'exception, non exposable.

---

## 16. Exécution

### 16.1 Modèle

Exécution déterministe selon l'ordre d'évaluation défini.

### 16.2 CLI `ps` (usage pratique)

Le CLI `ps` exécute un fichier ProtoScript V2, ou du code inline.

Exemples :

```bash
ps run fichier.pts
ps -e "Io.printLine(42);"
ps check fichier.pts
ps emit-c fichier.pts
ps test
```
Ref: EX-088

Options courantes :

```
--help
--version
--trace
--trace-ir
--time
```
Ref: EX-089

Position des options :

- les options peuvent apparaître **avant ou après** la commande.
- exemples équivalents :
  - `ps --trace run fichier.pts`
  - `ps run fichier.pts --trace`

Détails des commandes :

- `ps run fichier.pts` : exécute le programme (runtime C).
- `ps -e "code"` : exécute un extrait inline (wrap dans un `main` implicite).
- `ps check fichier.pts` : parse + analyse statique uniquement (aucune exécution).
- `ps ast fichier.pts` : affiche l’AST (arbre de syntaxe) en JSON stable pour inspection.
- `ps ir fichier.pts` : affiche l’IR (intermédiaire) en JSON stable pour inspection.
- `ps emit-c fichier.pts` : génère du C via l’oracle `protoscriptc` (Node).
- `ps test` : lance la suite de conformité (tests normatifs).

Détails des options :

- `--trace` : journalisation des étapes d’exécution (runtime). Sorties préfixées par `[trace]`.
- `--trace-ir` : journalisation des instructions IR au moment de l’exécution. Sorties préfixées par `[ir]`.
- `--time` : affiche le temps d’exécution total (ms).

### 16.2.1 CLI `ps` : commande `test`

`ps test` exécute la suite de conformité complète (tests normatifs).

### 16.3 Absences volontaires

- Pas de RTTI utilisateur.
- Pas de réflexion.
- Pas de comportement implicite dépendant de l'environnement runtime.

### 16.4 Entrée `main` et codes de sortie

Signatures autorisées :

```c
function main() : void { }
function main() : int { return 0; }
function main(list<string> args) : void { }
function main(list<string> args) : int { return 0; }
```
Ref: EX-090

`args` reçoit **tous** les arguments tels que fournis par le système, sans filtrage, y compris le binaire et la sous‑commande.
Exemple avec le CLI :

```
./ps run fichier.pts a b
```
Ref: EX-091

`args` vaut :

```
["./ps", "run", "fichier.pts", "a", "b"]
```
Ref: EX-092

Codes de sortie par défaut :

- `0` : succès
- `2` : erreur utilisateur (syntaxique, statique, runtime)
- `1` : erreur interne (assert/bug/OOM)

Si `main` retourne un `int`, cette valeur devient le code de sortie.

### 16.5 Modules et compilation C (note pratique)

Quand vous compilez du ProtoScript V2 vers du C avec **le compilateur `protoscriptc`** (option `--emit-c`), le code C généré s’appuie sur le runtime C **et** sur les modules natifs nécessaires.
Cela signifie que :

- `import Math...`, `import Io...`, `import JSON...` exigent que ces modules soient présents au link/chargement.
- il n’y a **aucun** fallback implicite : si le module n’est pas fourni, l’exécution échoue.

Exemple minimal :

```c
import Math.{sqrt};

function main() : void {
    float x = sqrt(9.0);
    Io.printLine(x);
}
```
Ref: EX-093

Le binaire C généré doit être exécuté avec le runtime et les modules natifs disponibles.

Exemple concret (compilation + édition de liens) :

```bash
# Générer le C depuis un fichier ProtoScript
bin/protoscriptc --emit-c hello.pts > hello.c

# Compiler et lier contre le runtime C de ProtoScript
cc -std=c11 -O2 -I./include \
  hello.c \
  c/runtime/ps_api.c c/runtime/ps_errors.c c/runtime/ps_heap.c c/runtime/ps_value.c \
  c/runtime/ps_string.c c/runtime/ps_list.c c/runtime/ps_object.c c/runtime/ps_map.c \
  c/runtime/ps_dynlib_posix.c c/runtime/ps_json.c c/runtime/ps_modules.c c/runtime/ps_vm.c \
  -ldl -o hello

# Exécuter (les modules natifs requis doivent être accessibles)
./hello
```
Ref: EX-094

### 16.5 CLI `pscc` (frontend C)

`pscc` fournit un frontend C partiel et peut rediriger vers l’oracle Node pour certaines sorties.

Commandes principales :

```bash
./c/pscc --check file.pts
./c/pscc --check-c file.pts
./c/pscc --check-c-static file.pts
./c/pscc --ast-c file.pts
./c/pscc --emit-ir-c-json file.pts
./c/pscc --emit-ir file.pts      # forward vers bin/protoscriptc
./c/pscc --emit-c file.pts       # forward vers bin/protoscriptc
```
Ref: EX-095

### 16.6 Comparaison utile (JS/PHP)

Pas d'ajout dynamique de membres/fonctions à chaud. L'exécution suit un contrat statique.

---

## 17. Performance et coûts

### 17.1 Principe

Les coûts doivent rester visibles dans le code et prévisibles.

### 17.2 Checks runtime

Les checks normatifs font partie de l'exécution normale.
Ils ne sont élidables que si leur inutilité est prouvée.

### 17.3 Exceptions

Le coût "zéro-cost" concerne le mécanisme d'unwind/dispatch quand aucune exception n'est levée.
Il ne signifie pas "absence de checks runtime normatifs".

### 17.4 Debug vs release

- Même sémantique observable.
- Différences autorisées : instrumentation et qualité des diagnostics.

### 17.5 Pourquoi ?

Le langage privilégie des garanties défendables plutôt que des promesses de performance implicites.

---

## 18. Annexes

### 18.0 Cheat sheet (1 page)

Types (base) :

- `bool`, `byte`, `int`, `float`, `glyph`, `string`
- pas de `null` universel
- conversions explicites seulement

Collections :

- `list<T>` : mutable, `list[i] = x` strict, `push/pop` explicites
- `map<K,V>` : lecture stricte (`map[k]` exige clé présente), écriture constructive (`map[k] = v` insère/met à jour)
- `slice<T>` : vue mutable non possédante
- `view<T>` : vue lecture seule non possédante
- `string` : immuable, indexation glyphique

Erreurs fréquentes :

- oublier le type de retour d'une fonction
- tenter `a = b = c` (affectation chaînée interdite)
- supposer `sum()` valide avec variadique (la séquence variadique doit être non vide)
- écrire dans `string[i]` ou `view[i]`
- lire `map[k]` sur une clé absente en pensant obtenir une valeur par défaut

Différences clés vs JS/PHP :

- pas de typage dynamique
- pas de fonctions comme valeurs
- pas de null universel
- pas de chargement dynamique des modules
- pas de concaténation implicite de chaînes

### 18.1 Table de correspondance (Concept -> Section)

| Concept | Où lire |
|---|---|
| Unicode / glyphes | §13 |
| Exceptions | §15 |
| map lecture stricte / écriture constructive | §11.2 |
| Variadique | §9.3 |
| slice / view | §12 |
| Prototypes et substitution parent/enfant | §10 |
| Modules et imports | §14 |
| Ordre d'évaluation | §6.2 |
| switch sans fallthrough implicite | §8.4 |
| Absence de null | §3.3 |

### 18.2 Table rapide des opérateurs

| Famille | Opérateurs |
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
    Io.printLine(r.toString());
}
```
Ref: EX-096

### 18.4 Notes de comparaison (clarification)

- Par rapport à JavaScript : pas de typage dynamique, pas de fonctions comme valeurs, pas de métaprogrammation runtime.
- Par rapport à PHP : pas d'HTML embarqué, pas de superglobales, pas de variables dynamiques.
- Par rapport à C : sémantique de sûreté normative (checks/diagnostics), tout en gardant un modèle de compilation bas niveau.

---

## Rappel final

Ce manuel décrit l'usage quotidien.
La spécification [`SPECIFICATION.md`](SPECIFICATION.md) définit la loi du langage.
