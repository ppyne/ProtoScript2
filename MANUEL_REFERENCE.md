![ProtoScript2](header.png)

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
function main() : void {
    Io.printLine("Hello world");
}

function main() : void {
    Io.print("Hello world".concat(Io.EOL));
}

function main() : void {
    Io.print(["Hello", " ", "world", Io.EOL].concat());
}
```

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
function main() : void {
    int x = 1;
    {
        int y = 2;
        Io.printLine(y.toString());
    }
    Io.printLine(x.toString());
}
```

### 2.3 Commentaires

```c
// commentaire ligne
/* commentaire bloc */
```

### 2.4 Ce que le langage ne fait pas

- Pas de balises.
- Pas d'HTML embarqué (contrairement à l'usage historique de PHP).

### 2.5 Erreur fréquente

Oublier `;` en fin d'instruction. ProtoScript n'a pas d'insertion automatique de point-virgule.

---

## 3. Types

### 3.1 Système de types

Le typage est statique et explicite. Les types sont résolus à la compilation.

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

Il n'y a pas de nullité universelle.

Contre-exemple :

```c
// invalide : `null` n'est pas une valeur du langage
// string s = null;
```

### 3.3.1 Alternative idiomatique : prototype "nullable"

Quand un type "vide" est nécessaire, on utilise un prototype explicite avec un indicateur statique.

Exemple (chaîne nullable) :

```c
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

Pourquoi ?

Cette approche rend l'absence explicite et statiquement typée, sans introduire de nullité implicite.

### 3.4 Valeurs par défaut

Une variable locale doit être assignée avant lecture.

Exemple :

```c
function main() : void {
    int x = 1;
    Io.printLine(x.toString());
}
```

Contre-exemple :

```c
function main() : void {
    int x;
    Io.printLine(x.toString()); // invalide : x non initialisée
}
```

### 3.5 Conversions explicites

```c
int n = 12;
string s = n.toString();
float f = s.toFloat();
```

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

### 4.2 Flottants

```c
float f1 = 1.5;
float f2 = 1e-3;
```

### 4.3 Chaînes

```c
string s = "Bonjour";
```

### 4.4 Listes et maps

```c
list<int> xs = [1, 2, 3];
map<string, int> mm = {"a": 1, "b": 2};
```

### 4.5 Littéraux vides et typage contextuel

```c
list<int> xs = [];
map<string, int> mm = {};
```

Contre-exemple :

```c
var x = []; // invalide sans contexte de type
var m = {}; // invalide sans contexte de type
```

### 4.6 Erreur fréquente

Confondre `{}` map vide avec un bloc vide. Dans une expression, `{}` est un littéral de map.

---

## 5. Variables

### 5.1 Déclaration

```c
var n = 10;
int x = 20;
```

`var` déclenche une inférence locale du type à partir de l'initialiseur.
Le type reste statique et connu à la compilation.
Une déclaration `var` doit donc toujours avoir une valeur d'initialisation.

Exemple :

```c
var s = "ok";  // s : string
var n = 12;    // n : int
```

Contre-exemple :

```c
// invalide : `var` sans initialiseur
// var x;
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

### 5.3 Initialisation obligatoire

Une variable non assignée ne peut pas être lue.

Exemple :

```c
function main() : void {
    int x = 1;
    int y = x + 1;
    Io.printLine(y.toString());
}
```

Contre-exemple :

```c
function main() : void {
    int x;
    Io.printLine(x.toString()); // invalide : x non initialisée
}
```

Erreur attendue :

- erreur statique (famille `E4xxx`) liée à l'absence d'assignation définitive

### 5.4 Ce qui n'existe pas

- Pas de variable dynamique nommée à l'exécution.
- Pas de superglobale (variable globale prédéfinie, accessible partout sans déclaration explicite).

### 5.5 Comparaison utile (PHP/JS)

En JS/PHP, des accès à des noms dynamiques peuvent exister. Ici, la résolution est réalisée à la compilation (compile-time).

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

### 6.3 Ternaire

```c
int a = 1;
int b = 2;
int m = (a < b) ? a : b;
```

### 6.4 Affectation

- L'affectation est une instruction.
- Elle n'a pas de valeur de retour.
- L'affectation chaînée est invalide.

Contre-exemple :

```c
// invalide
// int x = (a = 1);
// a = b = c;
```

### 6.5 Pourquoi ?

Interdire l'affectation en expression supprime une source classique d'effets de bord implicites.

---

## 7. Opérateurs

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

#### 7.1.2 Opérateurs de comparaison

| Exemple | Nom | Résultat |
|---|---|---|
| `a == b` | Égal | `true` si `a` est égal à `b` (types compatibles, pas de conversion implicite). |
| `a != b` | Différent | `true` si `a` est différent de `b` (types compatibles, pas de conversion implicite). |
| `a < b` | Plus petit que | `true` si `a` est strictement plus petit que `b`. |
| `a > b` | Plus grand que | `true` si `a` est strictement plus grand que `b`. |
| `a <= b` | Inférieur ou égal | `true` si `a` est plus petit ou égal à `b`. |
| `a >= b` | Supérieur ou égal | `true` si `a` est plus grand ou égal à `b`. |

#### 7.1.3 Opérateurs logiques

| Exemple | Nom | Résultat |
|---|---|---|
| `!a` | Not (Non) | `true` si `a` n'est pas `true`. |
| `a && b` | And (Et) | `true` si `a` ET `b` sont `true` (court-circuit). |
| `a || b` | Or (Ou) | `true` si `a` OU `b` est `true` (court-circuit). |

#### 7.1.4 Opérateurs sur les bits

| Exemple | Nom | Résultat |
|---|---|---|
| `a & b` | And (Et) | Bits à 1 dans `a` ET dans `b` restent à 1. |
| `a | b` | Or (Ou) | Bits à 1 dans `a` OU dans `b` restent à 1. |
| `a ^ b` | Xor (Ou exclusif) | Bits à 1 dans `a` OU dans `b` mais pas dans les deux. |
| `~a` | Not (Non) | Inversion bit à bit de `a`. |
| `a << b` | Décalage à gauche | Décale les bits de `a` de `b` positions vers la gauche. |
| `a >> b` | Décalage à droite | Décale les bits de `a` de `b` positions vers la droite. |

#### 7.1.5 Opérateurs d'affectation

| Exemple | Équivalent | Opération |
|---|---|---|
| `a = b` | `a = b` | Affectation simple. |
| `a += b` | `a = a + b` | Addition. |
| `a -= b` | `a = a - b` | Soustraction. |
| `a *= b` | `a = a * b` | Multiplication. |
| `a /= b` | `a = a / b` | Division. |

#### 7.1.6 Incrémentation et décrémentation

| Exemple | Équivalent | Opération |
|---|---|---|
| `++a` | Pré-incrémente | Incrémente `a` de 1, puis retourne `a`. |
| `a++` | Post-incrémente | Retourne `a`, puis incrémente `a` de 1. |
| `--a` | Pré-décrémente | Décrémente `a` de 1, puis retourne `a`. |
| `a--` | Post-décrémente | Retourne `a`, puis décrémente `a` de 1. |

En contexte d'expression, la forme pré/post indique si la modification intervient avant ou après l'utilisation de la valeur.

### 7.2 Exemples

```c
int a = 4;
int b = 2;
int c = a + b;
bool k = (a > b) && (b != 0);
int s = a << 1;
```

### 7.3 Chaînes : pas de concaténation implicite

Contre-exemple :

```c
// invalide selon la spec
// string s = "a" + "b";
```

Utiliser la concaténation explicite disponible par API/méthode.

Exemple correct de concaténation explicite :

```c
string a = "Hello ";
string b = "world";
string c = a.concat(b);
```

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

Le bloc est optionnel si la branche contient une seule instruction.

```c
if (x > 0)
    Io.printLine("pos");
```

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

### 8.2 Boucles

ProtoScript V2 propose des boucles classiques et des boucles d'itération.

#### 8.2.1 while

```c
while (cond) {
    // ...
}
```

#### 8.2.2 do / while

```c
do {
    // ...
} while (cond);
```

#### 8.2.3 for classique

```c
for (int i = 0; i < 10; i++) {
    // ...
}
```

Exemples d'itération indexée :

```c
list<int> xs = [10, 20, 30];
for (int i = 0; i < xs.length(); i = i + 1) {
    Io.printLine(xs[i].toString());
}
```

```c
string s = "abc";
for (int i = 0; i < s.length(); ++i) {
    glyph g = s[i];
    Io.printLine(g.toString());
}
```

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

#### 8.2.4 for ... of (itération sur les valeurs)

`for ... of` itère sur les valeurs d'une structure itérable.

```c
list<int> xs = [1, 2, 3];
for (int v of xs) {
    Io.printLine(v.toString());
}
```

Sur `string`, `for ... of` itère sur les glyphes :

```c
string s = "a😀b";
for (glyph g of s) {
    Io.printLine(g.toString());
}
```

Sur `map<K,V>`, `for ... of` itère sur les valeurs `V` :

```c
map<string, int> m = {"a": 1, "b": 2};
for (int v of m) {
    Io.printLine(v.toString());
}
```

#### 8.2.5 for ... in (itération sur les clés)

`for ... in` itère sur les clés d'une map (et uniquement une map).

```c
map<string, int> m = {"a": 1, "b": 2};
for (string k in m) {
    Io.printLine(k);
}
```

Contre-exemple :

```c
list<int> xs = [1, 2, 3];
// invalide : `for ... in` ne s'applique pas à `list<T>`
// for (int v in xs) { ... }

string s = "abc";
// invalide : `for ... in` ne s'applique pas à `string`
// for (glyph g in s) { ... }
```

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

Exemple `continue` :

```c
for (int i = 0; i < 5; i++) {
    if (i == 2) {
        continue;
    }
    Io.printLine(i.toString());
}
```

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
}
```

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
}
```

### 8.5 Erreur fréquente

Reproduire un style C classique avec fallthrough implicite. ProtoScript V2 le refuse.

---

## 9. Fonctions

### 9.1 Déclaration

```c
function add(int a, int b) : int {
    return a + b;
}
```

### 9.2 Paramètres et retour

- Paramètres explicitement typés.
- Type de retour explicite.
- Pas de paramètres optionnels implicites.

Contre-exemple :

```c
// invalide : paramètres par défaut non supportés
// function greet(string name = "world") : void {
//     Io.printLine(name);
// }
```

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
- Pas de génériques de fonctions.

### 9.5 Comparaison utile (JS/PHP)

Pas de closures/fonctions anonymes comme valeurs de premier ordre. Les appels sont résolus statiquement.

---

## 10. Prototypes et objets

ProtoScript V2 n'est pas un langage class-based. Il n'y a pas de classes, d'instances de classes, ni de mécanisme de construction dynamique.

Le modèle est prototype-based :

- un objet est créé par clonage d'un prototype explicite
- la structure est figée à la compilation
- la résolution des champs et méthodes est statique

Conceptuellement, un prototype est un gabarit concret, pas une classe abstraite.
On parle donc de **délégation statique** plutôt que d'héritage dynamique.

### 10.1 Modèle prototype-based

Pas de classes.
Les objets sont créés par clonage de prototypes.

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

### 10.2 Déclaration, champs, méthodes, self

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

Un `ColoredPoint` peut être utilisé là où `Point` est attendu, selon les règles statiques.

### 10.4 Override

L'override conserve une signature compatible selon la spécification.

En pratique :

- le nom et la liste des paramètres doivent être identiques
- le type de retour doit être identique
- il n'y a pas de surcharge par nombre ou type de paramètres

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
        // spécialisation avec même signature
        self.x = self.x + dx;
        self.y = self.y + dy;
    }
}
```

Contre-exemples :

```c
prototype Bad1 : Point {
    // invalide : signature différente (paramètres)
    function move(int dx) : void { }
}

prototype Bad2 : Point {
    // invalide : type de retour différent
    function move(int dx, int dy) : int { return 0; }
}
```

Note :

Les propriétés (champs) ne se "surchargent" pas et ne peuvent pas être redéfinies avec un autre type.
Il n'existe pas de mécanisme `super` implicite dans ProtoScript V2.
Un appel explicite au parent n'est pas normativement défini à ce stade.
En revanche, une méthode héritée non redéfinie reste disponible : un enfant peut appeler `self.jump()` si `jump()` est défini dans un parent.

### 10.5 Ce qui n'existe pas

- Pas de classes, interfaces, traits.
- Pas de cast dynamique.
- Pas de RTTI utilisateur.

### 10.6 Pourquoi ?

Le modèle prototype-based de ProtoScript V2 conserve un layout stable et une résolution statique des accès.

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

Contre-exemple :

```c
list<int> xs = [1];
// xs[3] = 10; // runtime OOB
```

### 11.2 `map<K,V>` : lecture stricte, écriture constructive

- `K` et `V` sont des types explicites ; ils peuvent aussi désigner des types prototypes (objets), la substitution parent/enfant est validée statiquement.

```c
map<string, int> m = {};
m["a"] = 1;    // insertion (clé absente)
m["a"] = 2;    // mise à jour (clé présente)
int x = m["a"]; // lecture valide
```

Contre-exemple :

```c
map<string, int> m = {};
int x = m["absent"]; // runtime missing key
```

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

### 12.3 Écriture

```c
s[0] = 99; // autorisé
// v[0] = 99; // invalide (view en lecture seule)
```

### 12.4 Durée de vie et invalidation

Une vue ne doit pas survivre au stockage source.
Les mutations structurelles du stockage source peuvent invalider des vues.

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

Index hors bornes :

```c
// runtime OOB
// glyph g = s[99];
```

### 13.3 Combining marks

`string` suit les glyphes/scalaires définis par le langage, pas une indexation brute par octet.

### 13.4 Immutabilité

```c
string s = "abc";
// s[0] = "x"[0]; // invalide
```

Exemple d'approche correcte (création d'une nouvelle chaîne) :

```c
string s = "abc";
string t = s.concat("x"); // s reste inchangée
```

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

Si la liste de bytes n'est pas un UTF-8 valide, `toUtf8String()` lève une exception runtime.

### 13.10 Sous-chaînes (substring)

`substring(start, length)` extrait une sous-chaîne en indices de glyphes.
Elle retourne une **nouvelle** chaîne et ne crée pas de vue partagée.

```c
string s = "a😀b";
string t = s.substring(1, 1); // "😀"
```

Erreurs :

- `start` ou `length` hors bornes lève une exception runtime

Note :

Il n'existe pas d'API de slicing/view pour `string`. L'extraction est explicite et copie la sous-chaîne.

---

## 14. Modules

### 14.1 Imports

```c
import std.io as io;
import math.core.{abs, clamp as clip};
```

### 14.2 Visibilité et noms

- Import explicite des symboles.
- Aliases explicites.
- Pas de wildcard import.

Contre-exemple :

```c
// invalide
// import std.io.*;
```

### 14.3 Résolution statique

Les symboles de module sont résolus à la compilation.
Aucun chargement dynamique.

### 14.4 Modules natifs

Les modules natifs étendent l'environnement de noms, pas la sémantique du langage.
Documentation officielle : `docs/native-modules.md`.

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

### 16.2 Absences volontaires

- Pas de RTTI utilisateur.
- Pas de réflexion.
- Pas de comportement implicite dépendant de l'environnement runtime.

### 16.3 Comparaison utile (JS/PHP)

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

### 18.4 Notes de comparaison (clarification)

- Par rapport à JavaScript : pas de typage dynamique, pas de fonctions comme valeurs, pas de métaprogrammation runtime.
- Par rapport à PHP : pas d'HTML embarqué, pas de superglobales, pas de variables dynamiques.
- Par rapport à C : sémantique de sûreté normative (checks/diagnostics), tout en gardant un modèle de compilation bas niveau.

---

## Rappel final

Ce manuel décrit l'usage quotidien.
La spécification [`SPECIFICATION.md`](SPECIFICATION.md) définit la loi du langage.
