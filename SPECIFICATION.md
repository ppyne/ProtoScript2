![ProtoScript2](header.png)

# ProtoScript V2 — Spécification

# 1. Objectifs

ProtoScript V2 est un langage :

- à syntaxe familière (C / JavaScript)
- à sémantique stricte
- object-oriented, prototype-based, sans classes
- typé statiquement par inférence ou déclaration
- conçu pour des performances structurelles
- compilable vers un IR simple, du C, ou du natif

---

## Intention générale

ProtoScript V2 est conçu comme un **langage de construction**, destiné à l’écriture de logiciels durables, lisibles et performants.

Il ne cherche ni à masquer la complexité réelle des programmes, ni à offrir un confort syntaxique au prix d’ambiguïtés sémantiques. Chaque règle du langage vise à rendre explicites :

- les types
- les coûts
- les allocations
- les dépendances

L’objectif est de permettre à l’humain de raisonner correctement sur le code, et à la machine de l’analyser, l’optimiser et le compiler efficacement.

---

## Objectifs principaux

ProtoScript V2 vise explicitement :

- une **lisibilité stricte** du code source
- une **prévisibilité totale** du comportement à l’exécution
- des **performances structurelles** comparables à du C bien écrit
- une **compilation hors ligne** (AOT) possible sans heuristique spéculative
- une séparation nette entre texte, données binaires et structures

Le langage doit permettre l’écriture d’algorithmes intensifs (traitement d’images, de données, de texte) sans pénalité sémantique ou structurelle.

---

## Ce que ProtoScript V2 n’est pas

ProtoScript V2 ne vise pas :

- la compatibilité avec JavaScript ou ECMAScript
- l’exécution dans un navigateur
- le typage dynamique
- la métaprogrammation réflexive
- les conversions implicites

Ces choix sont assumés afin de préserver la cohérence globale du langage et la confiance de l’utilisateur dans ce qu’il écrit.

---

## Principe fondateur

ProtoScript V2 est guidé par un principe central :

> **Ce qui est lisible pour l’humain doit être directement exploitable par la machine.**

Toute fonctionnalité qui améliorerait artificiellement le confort d’écriture au détriment de la lisibilité, de l’analyse statique ou des performances est volontairement exclue.

## Runtime Authority and Execution Independence

Règles normatives :

- le runtime C de ProtoScript2 est l’implémentation normative d’exécution ;
- le CLI runtime (`c/ps run`) doit être autonome et exécuter localement via le runtime C ;
- le CLI runtime ne doit déléguer ni directement ni indirectement vers un compilateur/référence externe ;
- les mécanismes de délégation de type `exec`, `system`, `popen` ou l’invocation d’un runtime Node.js sont interdits pour l’exécution de `c/ps run`.

Cadre de conformité :

- Node.js peut être utilisé pour la génération d’IR, la génération C et le crosscheck/oracle de conformité ;
- la parité Node ↔ C doit être validée par les tests, jamais obtenue par redirection d’exécution.

# 2. Types scalaires fondamentaux

ProtoScript V2 définit un ensemble restreint de **types scalaires fondamentaux**, dont la sémantique est fixe, explicite et exploitable statiquement.

Types scalaires intégrés :

- `bool`
- `byte` — entier non signé 8 bits (donnée binaire brute)
- `glyph` — scalaire Unicode, représentant un **glyphe**
- `int` — entier signé **64 bits**, type entier par défaut du langage
- `float` — flottant double précision IEEE‑754
- `string` — séquence de glyphes, stockée en UTF‑8

---

## 2.0 Types scalaires fondamentaux (normatif)

Les **types scalaires fondamentaux** sont strictement :

- `bool`
- `byte`
- `glyph`
- `int`
- `float`
- `string`

Propriétés normatives :

- ils sont manipulés **par valeur**
- ils ne participent **pas** au mécanisme d’héritage
- ils ne possèdent **aucun** champ utilisateur
- ils sont **immuables** au niveau du langage
- toute mutation indexée de `string` est **interdite**

Les types composés (`list<T>`, `map<K,V>`) et les `prototype` ne sont **pas** des types scalaires fondamentaux.

---

## 2.1 Littéraux numériques

### Entiers (`int`, `byte`)

Les littéraux entiers peuvent être écrits sous les formes suivantes :

- **décimale** : `12`, `0`, `5`
- **hexadécimale** : `0xFF`, `0xab`
- **octale** : `0644`
- **binaire** : `0b01100110`

Règles :

- les littéraux entiers ne portent pas de signe
- toute valeur négative résulte de l’application de l’opérateur unaire `-`
- les littéraux entiers non suffixés sont de type `int`
- l’affectation à `byte` exige que la valeur soit dans l’intervalle valide

---

### Flottants (`float`)

Les littéraux flottants supportent les notations suivantes :

- notation décimale : `.2`, `4.`, `-.14`, `-2.5`
- notation scientifique : `1e3`, `-2.5e-4`, `3.14E2`

Règles :

- tout littéral contenant un point décimal ou un exposant est de type `float`
- les opérations sur `float` suivent la sémantique **IEEE‑754 double précision**
- les valeurs `NaN`, `+Infinity`, `-Infinity` et `-0` peuvent être produites
- aucune exception implicite n’est levée lors des opérations arithmétiques sur `float`
- les comparaisons avec `NaN` suivent IEEE‑754 : `NaN != NaN` est `true` et toute comparaison ordonnée avec `NaN` est `false`
- `+0` et `-0` sont distincts au niveau IEEE‑754, mais restent égaux via `==`

---

## 2.2 Absence de nullité universelle

ProtoScript V2 ne définit **aucune valeur ****null**** universelle**.

Règles :

- les types scalaires fondamentaux (`int`, `float`, `bool`, `byte`, `glyph`, `string`) ont toujours une valeur valide
- `string` n’est jamais `null` (la chaîne vide `""` représente l’absence de contenu)
- `list<T>` et `map<K,V>` ne sont jamais `null` (une collection vide est valide)
- toute situation d’absence, d’échec ou d’erreur est gérée explicitement par des **exceptions**

---

## 2.3 Type `glyph`

### Représentation runtime

Un `glyph` est une **valeur scalaire Unicode logique**, représentant un caractère Unicode abstrait.

Règles normatives :

- un `glyph` représente **un point de code Unicode valide** (Unicode scalar value)
- sa représentation runtime est **fixe** et indépendante du nombre d’octets UTF‑8
- un `glyph` est représenté par un entier non signé de **32 bits** (`uint32`)
- aucun `glyph` ne correspond à un surrogate UTF‑16
- un `glyph` n’est pas propriétaire de mémoire

Conceptuellement, un `glyph` correspond à :

```c
uint32_t glyph;
```

La correspondance entre les bytes UTF‑8 d’une `string` et les `glyph` est assurée par le runtime lors de l’itération ou de l’indexation.

---

### Méthodes standard de `glyph`

Les méthodes suivantes sont définies pour le type `glyph` :

```c
g.isLetter();
g.isDigit();
g.isWhitespace();

g.isUpper();
g.isLower();

g.toUpper();
g.toLower();

g.toString();
g.toInt();
g.toUtf8Bytes();
```

Règles :

- les méthodes Unicode utilisent les tables Unicode standards
- `toUpper()` / `toLower()` retournent un `glyph`
- `toInt()` retourne le code point Unicode sous forme d’`int`
- `toUtf8Bytes()` retourne une `list<byte>` en UTF‑8 strict (exactement les octets du glyph)
- les opérations arithmétiques/unaires (`+`, `-`, `*`, `/`, `%`, `<<`, `>>`, `&`, `|`, `^`, `~`, `++`, `--`) sur `glyph` sont **interdites** et doivent lever une exception runtime
- les méthodes sont **pures** (sans effet de bord)
- aucune allocation implicite n’est effectuée
- les méthodes sont résolues statiquement

---

## 2.4 Construction `T group` (normatif)

Un `group` est une déclaration **statique** d’un ensemble fermé de constantes homogènes.

Syntaxe normative (EBNF) :

```ebnf
GroupDecl = ScalarType "group" Identifier "{" GroupMemberList "}" ;
```

Contraintes normatives :

- `T` doit être un **type scalaire fondamental**
- chaque membre doit être **assignable** à `T`
- les membres sont **implicitement constants**
- aucun membre ne peut être réassigné
- aucun doublon d’`Identifier` n’est autorisé dans un même `group`
- un `group` ne définit pas un nouveau type
- un `group` ne peut pas être instancié
- un `group` ne peut pas être utilisé comme type
- le nom du `group` désigne un **descripteur nominal** accessible comme valeur (`Color`)
- le descripteur n’est pas un type et ne définit pas d’opérations
- les membres sont substitués comme constantes dans l’IR

Contraintes sur les membres :

- chaque membre est de la forme `Identifier = Expression`
- chaque `Expression` doit être une **expression constante**, sans effet de bord
- aucun `MethodDecl`, aucun modificateur ni annotation n’est autorisé dans un `group`

Erreurs statiques associées :

- `E3120` — `GROUP_NON_SCALAR_TYPE` : `group` exige un type scalaire fondamental
- `E3121` — `GROUP_TYPE_MISMATCH` : un membre n’est pas assignable à `T`
- `E3122` — `GROUP_MUTATION` : tentative d’affectation à un membre de `group`

# 3. Structures de données

Cette section définit les structures de données fondamentales de ProtoScript V2, leurs règles de typage, leur sémantique d’accès et leurs garanties de complexité.

Les structures décrites ici sont **fermées**, **statiquement typées** et conçues pour offrir des performances structurelles prévisibles.

---

## 3.0 Méthodes associées aux valeurs

ProtoScript V2 autorise l’appel de méthodes sur les valeurs scalaires et structurées, sans adopter un modèle « tout est objet ».

Les méthodes font partie de l’**API standard** du langage et sont **résolues statiquement** en fonction du type.

### 3.0.1 Méthodes par type

**bool**

```c
b.toString();
```

**byte**

```c
b.toInt();
b.toFloat();
b.toString();
```

Règle normative :

- `byte.toInt()` retourne un `int` de même valeur numérique (0..255), sans conversion implicite.

**int**

```c
i.toByte();
i.toFloat();
i.toString();
i.toBytes();
i.abs();
i.sign();
```

**int.toBytes()**

`int.toBytes()` retourne une `list<byte>` de **8 octets**, copie brute de la représentation mémoire de l’entier.

Règles normatives :

- l’ordre des octets est **celui de la mémoire** sur la machine exécutant le runtime ;
- aucune normalisation d’endianness n’est effectuée ;
- le contenu de la liste est une **copie** (pas de vue).

**float**

```c
f.isNaN();
f.isInfinite();
f.isFinite();

f.toInt();
f.toString();
f.toBytes();
f.abs();
```

**float.toBytes()**

`float.toBytes()` retourne une `list<byte>` de **8 octets**, copie brute de la représentation mémoire IEEE‑754 de l’entier.

Règles normatives :

- l’ordre des octets est **celui de la mémoire** sur la machine exécutant le runtime ;
- aucune normalisation d’endianness n’est effectuée ;
- le contenu de la liste est une **copie** (pas de vue).

### 3.0.2 Littéraux numériques

Un littéral numérique est **non typé**.

- Son type est déterminé par le **contexte d’utilisation**.
- En l’absence de contexte, le type par défaut est `int`.

### 3.0.3 Conversions numériques explicites

La forme `(T)expr` est une conversion explicite vers le type numérique `T`.

Une conversion explicite est autorisée **uniquement** si la valeur de `expr` est **exactement représentable** dans `T`.

Toute conversion entraînant :

- un dépassement de plage,
- une perte d’information,
- une troncature,
- un enroulement (wrap),
- une saturation implicite

est une **erreur statique**.

### 3.0.4 Règles de représentabilité

- `byte` : entier ∈ `[0, 255]`
- `int` : entier signé de l’implémentation cible
- `float` : nombre réel IEEE‑754

### 3.0.5 Table normative des conversions

| Source → Cible | byte | int | float |
|---|---|---|---|
| **byte** | OK | OK | OK |
| **int** | OK si ∈ `[0,255]` | OK | OK |
| **float** | OK si entier exact ∈ `[0,255]` | OK si entier exact | OK |

### 3.0.6 Interdictions explicites

```c
(byte)256;   // erreur statique
(byte)-1;    // erreur statique
(int)3.14;   // erreur statique
(byte)3.0;   // OK
(byte)3.5;   // erreur statique
```

Aucune conversion implicite n’est autorisée entre types numériques.

**string**

```c
s.length();          // nombre de glyphes
s.isEmpty();

s.toString();        // identité
s.toInt();
s.toFloat();

s.toUpper();
s.toLower();

s.concat(parts);     // concaténation explicite
s.toUtf8Bytes();     // list<byte> (UTF-8 strict)
s.length();                 // longueur en glyphes
s.isEmpty();                // bool
s.subString(start, length); // sous-chaîne (glyphes)
s.indexOf(needle);   // position en glyphes (ou -1)
s.contains(needle);  // bool
s.lastIndexOf(needle); // dernière position en glyphes (ou -1)
s.startsWith(prefix);
s.endsWith(suffix);
s.split(sep);        // list<string>
s.trim();
s.trimStart();
s.trimEnd();
s.replace(old, new); // remplace la première occurrence
s.replaceAll(old, new); // remplace toutes les occurrences (non-chevauchantes)
s.glyphAt(index);    // équivalent à s[index]
s.repeat(count);
s.padStart(targetLength, pad);
s.padEnd(targetLength, pad);

```

Règles normatives d’accès indexé sur `string` :

- `string[index]` en lecture retourne un `glyph`
- l’index est exprimé en glyphes (et non en bytes UTF-8)
- si l’index est hors bornes, une exception runtime est levée (`RUNTIME_INDEX_OOB`)
- toute écriture `string[index] = value` est une erreur statique (`IMMUTABLE_INDEX_WRITE`)
- `string` est immuable et ne constitue pas une collection mutable
- `string.toUtf8Bytes()` retourne une `list<byte>` en UTF-8 strict
- `list<byte>.toUtf8String()` retourne une `string` en UTF-8 strict
- `string.length()` retourne le nombre de glyphes (Unicode scalar values)
- `string.isEmpty()` retourne `true` si la longueur en glyphes est `0`
- `string.subString(start, length)` retourne une nouvelle `string` extraite en glyphes
- `string.subString` n’expose ni vue ni référence partagée
- les indices et positions retournés par les méthodes de recherche sont exprimés en **glyphes**
- `string.indexOf(needle)` retourne l’index (en glyphes) de la première occurrence, ou `-1` si absent
- `string.contains(needle)` est équivalent à `string.indexOf(needle) >= 0` ; si `needle == ""`, retourne `true`
- `string.lastIndexOf(needle)` retourne l’index (en glyphes) de la dernière occurrence, ou `-1` si absent ; si `needle == ""`, retourne `string.length()`
- `string.startsWith(prefix)` et `string.endsWith(suffix)` retournent `bool`
- `string.split(sep)` retourne une `list<string>` et ne fait **aucun** traitement regex
- si `sep` est une chaîne vide, `split` retourne une liste de chaînes d’un glyphe chacune
- `string.trim*` retire uniquement les espaces ASCII (`' '`, `'\t'`, `'\n'`, `'\r'`) en début/fin selon la variante
- `string.replace(old, new)` remplace la **première** occurrence exacte (pas de regex) et retourne une nouvelle `string`
- `string.replaceAll(old, new)` remplace **toutes** les occurrences exactes non-chevauchantes (pas de regex)
- `string.replaceAll("", new)` doit lever une exception runtime `RUNTIME_INVALID_ARGUMENT`
- `string.glyphAt(index)` retourne le `glyph` à `index` (même règle d’erreur que `string[index]`)
- `string.repeat(count)` retourne `count` répétitions ; `count < 0` doit lever `RUNTIME_INVALID_ARGUMENT`
- `string.padStart(targetLength, pad)` et `string.padEnd(targetLength, pad)` paddent en glyphes ; si padding requis et `pad == ""`, lever `RUNTIME_INVALID_ARGUMENT`

**list<T>**

```c
list.length();
list.isEmpty();

list.push(x);
list.pop();

list.join(",");      // si T est string
list.concat();       // si T est string (concaténation sans séparateur)
list.sort();         // si T est comparable
list.contains(x);
list.toUtf8String(); // si T est byte (UTF-8 strict)
```

**map<K,V>**

```c
map.length();
map.isEmpty();

map.keys();          // list<K>
map.values();        // list<V>
map.containsKey(k);
```

### Arité (normative)

**bool**

| méthode | arité |
| --- | --- |
| `toString()` | 0 |

**byte**

| méthode | arité |
| --- | --- |
| `toInt()` | 0 |
| `toFloat()` | 0 |
| `toString()` | 0 |

**int**

| méthode | arité |
| --- | --- |
| `toByte()` | 0 |
| `toFloat()` | 0 |
| `toString()` | 0 |
| `toBytes()` | 0 |
| `abs()` | 0 |
| `sign()` | 0 |

**float**

| méthode | arité |
| --- | --- |
| `toInt()` | 0 |
| `toString()` | 0 |
| `toBytes()` | 0 |
| `abs()` | 0 |
| `isNaN()` | 0 |
| `isInfinite()` | 0 |
| `isFinite()` | 0 |

**string**

| méthode | arité |
| --- | --- |
| `length()` | 0 |
| `isEmpty()` | 0 |
| `toString()` | 0 |
| `toInt()` | 0 |
| `toFloat()` | 0 |
| `toUpper()` | 0 |
| `toLower()` | 0 |
| `toUtf8Bytes()` | 0 |
| `trim()` | 0 |
| `trimStart()` | 0 |
| `trimEnd()` | 0 |
| `concat(x)` | 1 |
| `indexOf(x)` | 1 |
| `contains(x)` | 1 |
| `lastIndexOf(x)` | 1 |
| `startsWith(x)` | 1 |
| `endsWith(x)` | 1 |
| `split(x)` | 1 |
| `glyphAt(index)` | 1 |
| `repeat(count)` | 1 |
| `subString(start, length)` | 2 |
| `replace(old, new)` | 2 |
| `replaceAll(old, new)` | 2 |
| `padStart(targetLength, pad)` | 2 |
| `padEnd(targetLength, pad)` | 2 |

**list<T>**

| méthode | arité |
| --- | --- |
| `length()` | 0 |
| `isEmpty()` | 0 |
| `pop()` | 0 |
| `sort()` | 0 |
| `concat()` | 0 |
| `toUtf8String()` | 0 |
| `push(x)` | 1 |
| `contains(x)` | 1 |
| `join(sep)` | 1 |

**map<K,V>**

| méthode | arité |
| --- | --- |
| `length()` | 0 |
| `isEmpty()` | 0 |
| `keys()` | 0 |
| `values()` | 0 |
| `containsKey(k)` | 1 |

**Modules standards (Io, Math, JSON)**

- l’arité des fonctions est strictement celle de `modules/registry.json`
- aucune arité variable n’est implicite : tout cas d’arité variable doit être explicitement listé (ex. `TextFile.read(size)` et `TextFile.tell()` sont des méthodes distinctes)

Note (non-normative) :

- L’implémentation de référence charge ce registry depuis `modules/registry.json` ou via la variable d’environnement `PS_MODULE_REGISTRY`.

Règles générales :

- les méthodes disponibles dépendent strictement du type
- certaines méthodes sont conditionnelles (ex. `sort()`)
- aucune méthode ne peut être ajoutée dynamiquement
- l’appel de méthode n’implique pas que « tout est objet »
- l’arité des méthodes/fonctions **est stricte** : tout appel avec trop ou pas assez d’arguments est une **erreur statique** (`E1003` `ARITY_MISMATCH`)
- les signatures indiquées dans cette section font foi pour l’arité (ex. `string.concat(x)` → 1 argument, `list.concat()` → 0, `map.keys()` → 0)
- l’exactitude fonctionnelle, la sécurité et les performances sont évaluées conjointement sur l’ensemble des chemins d’exécution

---

## 3.1 list<T>

```c
list<int> a;
list<int> b = [1, 2, 3];
list<byte> bytes = [0xFF, 0xab, 0x14, 17, 0b00101111, 04];
int x = b[0];
```

### Règles

- le type `T` est obligatoire
- stockage contigu en mémoire
- type invariant après déclaration
- accès indexé via l’opérateur `[]` avec un index de type `int`
- initialisation possible par **littéral de liste** avec `[]`
- l’ordre des éléments est celui du littéral
- le littéral vide `[]` doit être typé par le contexte ; hors contexte typé explicite, il est invalide

---

### Garanties de complexité des opérations `list<T>`

Les garanties suivantes sont normatives et indépendantes de l’implémentation.

| Opération                     | Complexité      | Notes                    |
| ----------------------------- | --------------- | ------------------------ |
| `list.isEmpty()`              | **O(1)**        | test de longueur         |
| `list.length()`               | **O(1)**        | longueur stockée         |
| accès `list[i]`               | **O(1)**        | accès direct mémoire     |
| affectation `list[i] = x`     | **O(1)**        | écriture stricte, sans resize implicite |
| itération `for (T v of list)` | **O(n)**        | parcours séquentiel      |
| `list.push(x)`                | **O(1)** amorti | réallocation possible    |
| `list.pop()`                  | **O(1)**        | suppression en fin       |
| `list.contains(x)`            | **O(n)**        | comparaison séquentielle |
| `list.sort()`                 | **O(n log n)**  | si `T` satisfait les conditions définies ci-dessous |
| `list.slice(offset, len)`     | **O(1)**        | création de vue          |

Règles complémentaires :

- aucune opération `list` ne cache une allocation non documentée
- la réallocation ne peut se produire que lors de `push`
- toute opération **O(n)** est proportionnelle au nombre d’éléments
- l’accès indexé est toujours en temps constant
- `list[i] = x` est une écriture stricte : l’index doit exister
- `list[i] = x` ne doit jamais redimensionner la liste
- `list.pop()` : erreur statique si la vacuité est prouvée, sinon exception runtime si la liste est vide

---

### Tri des listes : `list<T>.sort()` (normatif)

`list<T>.sort()` **est défini** comme un tri en place des éléments de la liste.  
Il **ne modifie que l’ordre** des éléments, sans changer leur identité ni leur valeur.  
Sa complexité **doit être** **O(n log n)**.

**Types autorisés** : `list<T>.sort()` **est autorisé** uniquement si `T` est :

- `int`, `float`, `byte`, `string`, ou
- un prototype qui **déclare** une méthode de comparaison conforme.

**Ordre de tri pour les types scalaires fondamentaux** :

- `int`, `byte` : ordre numérique croissant.
- `float` : ordre numérique croissant ; `NaN` est **classé supérieur** à toute valeur numérique ; deux `NaN` sont **équivalents**.
- `string` : ordre lexicographique UTF‑8 binaire, indépendant de la locale.

**Tri des prototypes utilisateur** :

- `list<T>.sort()` **est autorisé** uniquement si le prototype de `T` déclare une méthode `compareTo(T other) : int`.
- La méthode **doit** retourner une valeur **< 0** si `self < other`, **0** si `self` est égal à `other`, **> 0** si `self > other`.
- En absence de cette méthode, l’appel de `list<T>.sort()` **est une erreur statique**.

Clarification normative de résolution :

- `compareTo` **est** une méthode d’instance.
- La comparaison de deux éléments `a` et `b` **est effectuée** par l’appel `a.compareTo(b)`.
- La résolution **est strictement statique** selon le type `T` de la liste.
- Aucun dispatch dynamique supplémentaire ni recherche RTTI **n’est** effectué.
- Si `compareTo` est héritée d’un prototype parent de `T`, elle **est considérée valide** selon les règles générales de résolution des méthodes.

**Interdictions explicites** :

- Aucune variante `sort(cmpFunction)` n’est définie.
- Aucun comparateur externe (lambda/callback) n’est autorisé.
- Aucune généricité implicite ni fallback dynamique/RTTI n’est autorisé.

**Propriétés supplémentaires** :

- Le tri **doit être déterministe**.
- Le tri **doit être** stable.
- La complexité **doit être** O(n log n).
- Aucun comportement ne doit dépendre de la locale.

---

### Inversement de liste : `list<T>.reverse()` (normatif)

`list<T>.reverse()` **inverse** l’ordre des éléments de la liste **en place**.  
La complexité **doit être** **O(n)**.  
Cette opération **est autorisée** pour tout `list<T>`, sans contrainte sur `T`.

Propriétés normatives :

- L’opération **est déterministe**.
- Aucun nouvel élément n’est créé : l’inversion est **en place**.
- Aucune relation d’ordre nouvelle n’est introduite : il s’agit d’une inversion structurelle.
- Aucun comportement ne doit dépendre de la locale.
- Aucun dispatch dynamique ni comparateur n’est impliqué.

---

## 3.2 map<K,V>

```c
map<string, int> values = {"a": 12, "b": 8, "c": 0x45};
int v = values["a"];
```

### Règles

- les types `K` et `V` sont obligatoires
- initialisation possible par **littéral de map** avec `{ key : value }`
- les clés doivent être de type `K`
- les valeurs doivent être de type `V`
- le littéral vide `{}` doit être typé par le contexte ; hors contexte typé explicite, il est invalide
- l’ordre d’itération est l’ordre d’insertion
- les clés dupliquées dans un littéral sont interdites
- accès par clé via l’opérateur `[]`
- en lecture, `map[k]` doit lever une exception runtime si `k` est absente
- en écriture, `map[k] = v` doit insérer une nouvelle entrée si `k` est absente
- en écriture, `map[k] = v` doit mettre à jour la valeur associée si `k` est présente
- `map.remove(k)` doit supprimer la clé si elle existe et retourner `true`, sinon `false`
- `map.remove(k)` ne doit jamais lever pour clé absente
- supprimer puis ré‑insérer une clé doit la placer **en fin** de l’ordre d’itération

### Principe normatif : lecture stricte / écriture constructive

- la **lecture** `map[k]` est stricte : la clé doit exister au moment de l’accès
- la **lecture** d’une clé absente doit lever une exception runtime (`RUNTIME_MISSING_KEY`)
- l’**écriture** `map[k] = v` est constructive : elle doit produire un état valide après l’opération
- pour `map`, cela impose : insertion si la clé est absente, mise à jour si elle est présente

---

### Garanties de complexité des opérations `map<K,V>`

Les garanties suivantes sont normatives et indépendantes de l’implémentation.

| Opération                    | Complexité      | Notes                    |
| ---------------------------- | --------------- | ------------------------ |
| `map.isEmpty()`              | **O(1)**        | test de taille           |
| `map.length()`               | **O(1)**        | taille stockée           |
| accès `map[k]`               | **O(1)** amorti | table de hachage         |
| affectation `map[k] = v`     | **O(1)** amorti | insertion ou mise à jour |
| `map.containsKey(k)`         | **O(1)** amorti | lookup                   |
| `map.remove(k)`              | **O(1)** amorti | suppression              |
| itération `for (V v of map)` | **O(n)**        | ordre d’insertion        |
| itération `for (K k in map)` | **O(n)**        | ordre d’insertion        |
| `map.keys()`                 | **O(n)**        | création d’une liste     |
| `map.values()`               | **O(n)**        | création d’une liste     |

Règles complémentaires :

- les collisions de hachage sont gérées par l’implémentation
- aucune opération ne dépend de la taille des valeurs
- le coût du calcul de hash dépend uniquement du type `K`
- aucune mutation structurelle implicite

---

## 3.3 Structures contenant des objets (prototypes)

Les structures `list<T>` et `map<K,V>` peuvent contenir des **valeurs de type prototype**, c’est-à-dire des instances clonées à partir d’un prototype.

Cette capacité fait partie intégrante du modèle de données de ProtoScript V2 et ne constitue pas un cas particulier.

### Règles de typage

- `T` (ou `V` pour `map<K,V>`) peut être un **type prototype**
- les collections sont **strictement monomorphes**
- aucune collection hétérogène n’est autorisée
- le type statique de la collection gouverne les accès et les méthodes disponibles

Exemples :

```c
prototype Point { int x; int y; }
prototype ColoredPoint : Point { int color; }

list<Point> points;
points.push(Point.clone());
points.push(ColoredPoint.clone());   // autorisé (substitution)

map<string, Point> table;
table["a"] = ColoredPoint.clone();   // autorisé
```

---

### Substitution parent / enfant dans les collections

Les règles de **substitution structurelle** définies en section 4 s’appliquent intégralement aux collections.

- une instance d’un prototype enfant peut être stockée dans une collection typée par le prototype parent
- la validité de la substitution est vérifiée **à la compilation**
- aucun test de type runtime n’est requis

L’accès aux éléments est limité par le type statique de la collection :

```c
Point p = points[0];

p.x = 1;        // autorisé
p.color = 3;    // erreur de compilation
```

---

### Représentation et performances

- les collections stockent des **références structurées** vers les instances
- aucun boxing dynamique n’est introduit
- aucun surcoût runtime lié au type réel des éléments
- les garanties de complexité (`O(1)`, `O(n)`) restent inchangées

---

### Restrictions explicites

ProtoScript V2 ne permet pas, dans les collections :

- de tests dynamiques de type (`instanceof`, RTTI)
- de downcast explicite ou implicite
- de mutation structurelle dépendante du type réel

Tout comportement doit être exprimé via le **type statique** et la délégation structurelle.

---

### Encadré — Invariant global : objets + collections + vues

ProtoScript V2 repose sur un **invariant global unique**, valable pour les objets, les collections et les vues :

> **Le type statique gouverne toujours les accès, indépendamment du type réel stocké.**

Conséquences normatives :

- les **prototypes**, les **collections** (`list`, `map`) et les **vues** (`slice`, `view`) partagent exactement les mêmes règles de typage
- la **substitution parent / enfant** est autorisée partout, mais uniquement de manière **statique et structurelle**
- aucun mécanisme dynamique (RTTI, `instanceof`, downcast) n’est jamais introduit
- les garanties de performance et de layout mémoire sont **préservées à tous les niveaux**

Ainsi, les constructions suivantes sont toutes équivalentes du point de vue des règles de typage :

```c
Point p;
list<Point> lp;
view<Point> vp;
```

Dans tous les cas :

- seuls les champs et méthodes de `Point` sont accessibles
- le comportement ne dépend jamais du type réel (`Point` ou `ColoredPoint`)
- le compilateur peut raisonner de manière uniforme et déterministe

Cet invariant garantit la **cohérence globale du langage** et constitue l’un des piliers de sa compilabilité et de ses performances.

---

## 3.4 Vues : `slice<T>` et `view<T>`

ProtoScript V2 définit des **types de vue non possédants** permettant de référencer des sous-parties de données sans allocation ni copie.

- `slice<T>` est une vue mutable
- `view<T>` est une vue en lecture seule

### Principe général (objets et vues)

Les types `slice<T>` et `view<T>` suivent **exactement les mêmes règles de typage et de substitution** que `list<T>` et `map<K,V>`.

Ils peuvent donc référencer des **instances de prototypes**, y compris dans un contexte de **substitution parent / enfant**, sans introduire de mécanisme dynamique supplémentaire.

Règles fondamentales :

- `T` peut être un **type prototype**
- les vues sont **monomorphes** et statiquement typées
- aucune information de type runtime n’est conservée ou exposée
- le type statique de la vue gouverne les accès possibles

Exemple :

```c
list<Point> points;
points.push(ColoredPoint.clone());

view<Point> v = points.view();
Point p = v[0];      // autorisé
p.color = 1;         // erreur de compilation
```

---

### Représentation conceptuelle

- `(ptr, length)`
- aucun ownership
- aucune allocation

---

### Création des vues

#### À partir de `list<T>`

```c
slice<T> s = list.slice(offset, length);
view<T>  v = list.view(offset, length);
```

- bornes vérifiées
- création en **O(1)**

#### À partir d’une vue

```c
slice<T> sub = s.slice(offset, length);
view<T>  sub = v.view(offset, length);
```

- aucune allocation
- même buffer sous-jacent

#### À partir de `string`

```c
view<glyph> chars = s.view();
view<glyph> part  = s.view(start, length);
```

- indexation en glyphes
- aucune copie UTF-8

---

### Méthodes disponibles

```c
slice.length();
slice.isEmpty();

slice.slice(offset, length);
view.view(offset, length);
```

Règles :

- aucune méthode ne peut provoquer d’allocation implicite
- aucune méthode ne peut modifier la taille sous-jacente
- `slice<T>[i] = x` est autorisé, écrit dans le stockage sous-jacent, sans mutation structurelle
- `view<T>` interdit toute écriture

# 4. Modèle objet & prototypes

ProtoScript V2 adopte un **modèle objet prototype-based**, sans classes, inspiré de Self, tout en imposant des contraintes fortes de typage statique et de structure afin de garantir lisibilité, analyse statique et performances.

Les objets sont des **structures fermées**, définies à partir de prototypes explicites, avec un layout mémoire déterministe.

---

## 4.1 Principes fondamentaux

- il n’existe **aucune notion de classe**
- un objet est créé par **clonage d’un prototype**
- les prototypes sont définis explicitement
- la structure d’un objet est **figée** après sa définition
- aucun champ ne peut être ajouté ou supprimé dynamiquement

Ce modèle permet :

- une sémantique simple et lisible
- une résolution statique des accès
- des performances comparables à des structures C

---

## 4.2 Définition d’un prototype

Un prototype définit :

- un ensemble de **champs** typés
- un ensemble de **méthodes**
- éventuellement un prototype parent

Exemple conceptuel :

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

Règles :

- les types des champs sont obligatoires
- les méthodes ont des signatures statiquement typées
- `self` désigne l’instance courante
- la résolution des champs et méthodes est statique
- `const` n’est **pas** autorisé sur les déclarations de champs

---

## 4.2.1 `sealed` prototype (normatif)

Le modificateur `sealed` appliqué à un `prototype` interdit **l’héritage depuis** ce prototype.

Règles :

- un `sealed prototype` peut déclarer un parent
- un `sealed prototype` reste instanciable
- `sealed` n’interdit pas `clone()`
- `sealed` n’affecte pas les règles d’instanciation

Erreur statique associée :

- `E3140` — `SEALED_INHERITANCE` : tentative d’héritage **depuis** un `sealed prototype`

---

## 4.3 Instanciation et clonage

Les objets sont créés par **clonage d’un prototype**.

```c
Point p = Point.clone();
p.x = 10;
p.y = 20;
```

Règles :

- Tous les prototypes héritent implicitement d’un prototype racine `Object`.
- `clone()` est une méthode normale résolue par lookup d’héritage.
- `Object.clone()` est la définition par défaut.
- `Self` pour `clone()` se spécialise au type statique du receveur.

---

## 4.3.1 Clarification `clone()` (normatif)

`clone()` n’est pas une primitive spéciale.

Règles normatives :

1. `P.clone()` est un appel de méthode normal sur un receveur prototype.
2. `p.clone()` est un appel de méthode normal sur un receveur instance.
3. La résolution suit la chaîne d’héritage (lookup normal).
4. Si un ancêtre redéfinit `clone()` et qu’aucun descendant ne redéfinit, cette redéfinition s’applique.
5. `Object.clone()` alloue un objet `o` et fixe :
   - `o.__proto = P` si receveur prototype `P`
   - `o.__proto = p.__proto` si receveur instance `p`
6. Aucune logique implicite supplémentaire n’est autorisée.
7. Toute forme interne de lowering (ex. `__clone_static`) est un détail d’implémentation :
   - elle ne doit jamais contourner le lookup d’héritage de `clone()`,
   - elle ne peut pas modifier l’owner effectivement résolu pour `clone()`.

Un module natif ProtoScript en C peut restreindre l’accès à certains prototypes non exportés.

Cette restriction relève du mécanisme de module et n’altère pas la règle générale du langage concernant l’instanciation des prototypes.

## 4.3.2 Initialisation explicite des champs (normatif)

Un champ de prototype peut déclarer un initialiseur explicite :

```c
Type name = Expr;
```

Règles :

- un champ peut être déclaré `const` sous la forme `const Type name = Expr;`
- `const` sur champ est autorisé uniquement pour les types scalaires fondamentaux
- un champ `const` doit obligatoirement avoir un initialiseur explicite
- l’expression d’initialisation est vérifiée statiquement et doit être assignable au type du champ
- l’expression d’initialisation est évaluée au clonage (`clone()`), dans l’ordre parent → enfant puis ordre de déclaration
- sans initialiseur explicite, la valeur par défaut de 15.3 s’applique
- un initialiseur de champ ne peut pas référencer `self`
- après instanciation, un champ `const` ne peut pas être réassigné

Erreurs statiques associées :

- `E3150` — `INVALID_FIELD_INITIALIZER`
- `E3151` — `CONST_FIELD_MISSING_INITIALIZER`
- `E3130` — `CONST_REASSIGNMENT`

## 4.3.3 Visibilite `internal` (normatif)

ProtoScript V2 définit un modèle de visibilité à deux niveaux :

- `public` (par defaut)
- `internal`

Règles normatives :

1. La visibilité `internal` est une frontière lexicale attachée au prototype de définition.
2. Un membre `internal` est accessible uniquement depuis :
   - les méthodes du prototype de définition ;
   - les méthodes de ses prototypes enfants.
3. La regle d'accès est determinée à la compilation par l'appartenance lexicale de l'expression d'accès à une méthode d'un prototype `Q`. L'accès a un membre `internal` défini dans `P` est autorisé si `Q == P` ou `Q` est un enfant de `P`.
4. Un acces `internal` est interdit depuis toute fonction globale, même située dans le même fichier/module que le prototype.
5. Un acces `internal` est interdit depuis un autre prototype non-enfant, même situé dans le même fichier/module.
6. La possession d'une instance ne confère aucun droit d'accès supplémentaire.
7. Tout accès hors de cette frontière est interdit.
8. La vérification de visibilité est strictement statique.
9. La visibilité n'affecte pas le layout mémoire, l'ordre des champs, `clone()`, ni la substitution parent/enfant.
10. `internal` ne modifie pas la résolution statique des méthodes.
11. Il est interdit de redéfinir une methode `public` en `internal` dans un prototype enfant.
12. Le mot-cle `internal` est autorisé uniquement sur les membres de prototype (champs/méthodes) et interdit ailleurs (fonctions globales, `group`, blocs).

Erreurs statiques associées :

- `E3200` — `INVALID_VISIBILITY_LOCATION`
- `E3201` — `VISIBILITY_VIOLATION`

Diagnostic minimal valide :

`error[E3201]: member 'state' is internal to prototype 'Core'`

---

## 4.4 Héritage par délégation

ProtoScript V2 supporte un **héritage par délégation statique**, inspiré de Self, mais fortement restreint afin de préserver la lisibilité et les performances.

### Déclaration d’un prototype avec parent

Un prototype peut déclarer explicitement un **prototype parent unique**.

```c
prototype ColoredPoint : Point {
    int color;
}
```

Règles :

- un prototype peut avoir **au plus un parent**
- la relation parent → enfant est définie **à la compilation**
- la chaîne de délégation est **figée et linéaire**
- aucune modification dynamique de la délégation n’est autorisée

---

### Résolution des champs et méthodes

La résolution suit un ordre strict et déterministe :

1. champs et méthodes définis dans le prototype courant
2. champs et méthodes du prototype parent
3. remontée récursive dans la chaîne de délégation

Il n’existe **aucune ambiguïté possible**, car la délégation est linéaire.

---

### Redéfinition (override)

Un prototype enfant peut **redéfinir** une méthode du parent.

```c
prototype ColoredPoint : Point {
    function move(int dx, int dy) : void {
        // comportement spécialisé
        self.x = self.x + dx;
        self.y = self.y + dy;
    }
}
```

Règles :

- la signature doit être **strictement identique** à celle du parent
- aucune surcharge n’est autorisée
- la résolution est statique (pas de dispatch virtuel tardif)

---

### Accès au parent (`super`)

ProtoScript V2 définit `super` pour l’appel explicite au parent :

```c
super.methodName(args...);
```

Règles normatives :

1. `super` n’est valide que dans le corps d’une méthode.
2. `super` est lié au prototype déclarant la méthode courante.
3. La résolution de `super.m` commence au parent direct du prototype déclarant.
4. Le receveur effectif reste le même `self` que la méthode courante.

Diagnostics associés :

- `E3210` — `INVALID_SUPER_USAGE`
- `E3211` — `SUPER_NO_PARENT`
- `E3212` — `SUPER_METHOD_NOT_FOUND`

---

### Layout mémoire et héritage

Le layout mémoire d’un objet respecte l’ordre suivant :

1. champs du prototype parent
2. champs du prototype enfant

Conséquences :

- offsets connus à la compilation
- compatibilité ABI avec le parent
- passage sûr d’un objet enfant là où un parent est attendu (substitution structurelle)

---

### Schéma mémoire illustré (parent / enfant)

Exemple :

```c
prototype Point {
    int x;
    int y;
}

prototype ColoredPoint : Point {
    int color;
}
```

Représentation conceptuelle en mémoire (layout déterministe) :

```
Point (vu statiquement comme Point)

+0   int x
+8   int y

sizeof(Point) = 16

ColoredPoint (layout réel de l’objet)

+0   int x      (hérité de Point)
+8   int y      (hérité de Point)
+16  int color  (ajouté par ColoredPoint)

sizeof(ColoredPoint) = 24
```

Règles :

- le préfixe mémoire d’un enfant est **bit-à-bit compatible** avec le parent
- un `Point*` peut pointer sur un `ColoredPoint` sans adaptation
- l’accès aux champs du parent est identique quel que soit le prototype réel

---

### Exemple complet et compilation vers C

Code ProtoScript V2 :

```c
prototype Point {
    int x;
    int y;

    function move(int dx, int dy) : void {
        self.x = self.x + dx;
        self.y = self.y + dy;
    }

    function sum() : int {
        return self.x + self.y;
    }
}

prototype ColoredPoint : Point {
    int color;

    // override (signature identique)
    function move(int dx, int dy) : void {
        Point.move(self, dx, dy);
        self.color = self.color + 1;
    }
}

function demo() : int {
    Point p = ColoredPoint.clone();
    p.x = 10;
    p.y = 20;
    p.move(1, 2);
    return p.sum();
}
```

Traduction C conceptuelle (cible de premier rang) :

```c
// types
typedef struct {
    int64_t x;
    int64_t y;
} Point;

typedef struct {
    // préfixe compatible Point
    int64_t x;
    int64_t y;
    int64_t color;
} ColoredPoint;

// méthodes (appels statiques, self explicite)
static inline void Point_move(Point* self, int64_t dx, int64_t dy) {
    self->x = self->x + dx;
    self->y = self->y + dy;
}

static inline int64_t Point_sum(const Point* self) {
    return self->x + self->y;
}

static inline void ColoredPoint_move(ColoredPoint* self, int64_t dx, int64_t dy) {
    // appel parent explicite
    Point_move((Point*)self, dx, dy);
    self->color = self->color + 1;
}

// clone : allocation/initialisation (dépend runtime)
static inline ColoredPoint ColoredPoint_clone(void) {
    ColoredPoint v;
    v.x = 0;
    v.y = 0;
    v.color = 0;
    return v;
}

int64_t demo(void) {
    // substitution : stockage local en tant que ColoredPoint, vue statique Point*
    ColoredPoint cp = ColoredPoint_clone();
    Point* p = (Point*)&cp;

    p->x = 10;
    p->y = 20;

    // appel résolu statiquement selon le type connu du récepteur au point d’appel.
    // Ici, si le type statique est Point, l’appel est Point_move.
    // Pour appeler l’override, il faut un récepteur typé ColoredPoint.
    ColoredPoint_move(&cp, 1, 2);

    return Point_sum(p);
}
```

Notes normatives :

- aucun dispatch dynamique n’est requis pour exprimer l’héritage
- l’appel du parent est explicite et compilable en appel C direct
- la substitution repose sur la compatibilité de layout (préfixe mémoire)
- l’accès aux membres est gouverné par le type statique

---

---

## 4.5 Substitution et typage

ProtoScript V2 définit des règles strictes de **substitution structurelle**, permettant d’utiliser un objet enfant là où un objet parent est attendu, sans introduire de polymorphisme dynamique.

### Principe de substitution

Un objet de prototype `Child` peut être utilisé dans tout contexte où un objet de prototype `Parent` est attendu **si et seulement si** :

- `Child` délègue (directement ou indirectement) à `Parent`
- tous les champs et méthodes accessibles via `Parent` existent dans `Child`
- les signatures des méthodes héritées ou redéfinies sont strictement identiques

Ce principe est une forme contrôlée du principe de substitution de Liskov, appliqué de manière **structurelle et statique**.

---

### Comparaison avec Liskov et l’OO classique

Dans l’OO classique à classes (Java, C++, C#), le principe de substitution de Liskov est :

- **conceptuel**, mais rarement garanti structurellement
- dépendant du **polymorphisme dynamique**
- souvent violé par des effets de bord, des états internes ou des contrats implicites

ProtoScript V2 adopte une approche différente :

- la substitution est **vérifiée à la compilation**
- elle repose sur la **délégation structurelle**, pas sur des hiérarchies nominales complexes
- elle n’introduit **aucun dispatch dynamique**, aucune vtable

En conséquence :

- un objet enfant est substituable au parent **par construction**, non par convention
- la validité de la substitution ne dépend pas du comportement runtime
- le compilateur peut raisonner sur les objets comme sur des structures C compatibles

ProtoScript V2 privilégie ainsi une lecture **mécanique et vérifiable** de Liskov, là où l’OO classique en propose une lecture essentiellement **contractuelle et dynamique**.

---

### Encadré — Anti-exemples OO classiques

Les mécanismes suivants, courants dans l’OO à classes, sont **explicitement exclus** de ProtoScript V2, car ils introduisent ambiguïté, coûts cachés ou dépendances runtime.

**Vtables et dispatch virtuel**

- appel de méthode résolu au runtime
- indirection mémoire systématique
- comportement dépendant du type dynamique réel

→ Rejeté : ProtoScript V2 utilise uniquement des appels **statiquement résolus**, sans table virtuelle.

---

**Downcast et cast dynamique** (`dynamic_cast`, `(Child)x`)

- hypothèses runtime sur le type réel
- erreurs détectées tardivement
- nécessité de RTTI

→ Rejeté : aucun cast dynamique, aucun test runtime de type. Si le code compile, la substitution est valide.

---

**instanceof**** et tests de type**

- logique conditionnelle dépendante du type dynamique
- code fragile face à l’évolution des hiérarchies

→ Rejeté : ProtoScript V2 impose que le comportement soit déterminé par le type statique et la structure.

---

Ces mécanismes sont remplacés par :

- une délégation structurelle explicite
- un typage statique strict
- des layouts mémoire compatibles
- une substitution vérifiable à la compilation

---

### Mini tableau comparatif — ProtoScript V2 vs Java / C++

| Aspect                    | ProtoScript V2                          | Java / C++ (OO classique)             |
| ------------------------- | --------------------------------------- | ------------------------------------- |
| Modèle objet              | Prototypes, délégation statique         | Classes, héritage nominal             |
| Héritage multiple         | Non                                     | C++ : oui / Java : interfaces         |
| Dispatch de méthodes      | **Statique**                            | Dynamique (vtable)                    |
| Vtables                   | Aucune                                  | Centrale au runtime                   |
| Substitution              | Structurelle, vérifiée à la compilation | Nominale, partiellement contractuelle |
| Override                  | Signature strictement identique         | Polymorphe, parfois covariant         |
| Downcast                  | Interdit                                | Autorisé (souvent risqué)             |
| `instanceof` / RTTI       | Absent                                  | Présent                               |
| Ajout dynamique de champs | Impossible                              | Impossible (mais contournements RTTI) |
| Coût d’un appel méthode   | Équivalent appel C                      | Indirection + vtable                  |
| Compilation C             | Directe, naturelle                      | Indirecte / non idiomatique           |

Ce tableau met en évidence que ProtoScript V2 privilégie la **prévisibilité, la vérifiabilité et la performance**, là où l’OO classique privilégie la **flexibilité dynamique**, au prix d’une complexité runtime accrue.

---

### Mini tableau comparatif — ProtoScript V2 vs JavaScript

| Aspect                    | ProtoScript V2                     | JavaScript                           |
| ------------------------- | ---------------------------------- | ------------------------------------ |
| Modèle objet              | Prototypes **statiques** et fermés | Prototypes **dynamiques** et ouverts |
| Ajout de champs           | Interdit après définition          | Autorisé à tout moment               |
| Typage                    | Statique strict                    | Dynamique                            |
| Résolution des méthodes   | À la compilation                   | Au runtime                           |
| Dispatch                  | Statique                           | Dynamique                            |
| `this` / `self`           | `self` explicite, statique         | `this` dynamique et contextuel       |
| `prototype` / `__proto__` | Déclaration explicite, figée       | Chaîne mutable et observable         |
| `instanceof`              | Absent                             | Présent                              |
| Cast / tests de type      | Absents                            | Fréquents et nécessaires             |
| Performances              | Prévisibles, proches du C          | Dépendantes du moteur (JIT)          |
| Compilation C             | Naturelle                          | Non applicable                       |

Ce tableau montre que, bien que les deux langages soient *prototype-based*, ProtoScript V2 adopte une approche **déterministe et compilable**, tandis que JavaScript privilégie la **flexibilité dynamique**, au prix d’ambiguïtés sémantiques et de performances dépendantes du runtime.

---

### Typage statique

Le type statique d’une variable détermine :

- les champs accessibles
- les méthodes appelables
- les garanties de layout mémoire

Exemple :

```c
Point p = ColoredPoint.clone();

p.x = 1;        // autorisé
p.y = 2;        // autorisé
p.move(1, 1);   // autorisé

p.color = 3;    // erreur de compilation
```

Règles :

- l’accès est limité au type statique (`Point`)
- aucun downcast implicite n’est autorisé
- aucun test dynamique de type n’est fourni par le langage

---

### Absence de cast dynamique

ProtoScript V2 ne définit :

- ni `instanceof`
- ni `dynamic_cast`
- ni RTTI exposée au langage

Tout changement de vue typée doit être explicite et structurellement justifié au moment de la compilation.

---

### Conséquences sur les performances

Ces règles garantissent :

- absence totale de dispatch dynamique
- appels directs compilables en C
- aucune indirection supplémentaire
- performances identiques à des structures C équivalentes

---

### Schéma mémoire illustré — Parent / Enfant

Considérons les prototypes suivants :

```c
prototype Point {
    int x;
    int y;
}

prototype ColoredPoint : Point {
    int color;
}
```

La représentation mémoire d’une instance de `ColoredPoint` est strictement la suivante :

```
+-----------------+
| Point.x (int)   |  offset 0
+-----------------+
| Point.y (int)   |  offset 8
+-----------------+
| color (int)     |  offset 16
+-----------------+
```

Règles garanties :

- les champs du parent précèdent toujours ceux de l’enfant
- les offsets sont connus à la compilation
- un `ColoredPoint*` est **ABI-compatible** avec un `Point*`

Cela rend la substitution **sûre, triviale et sans coût**.

---

### Exemple complet — ProtoScript V2 → C

#### Code ProtoScript V2

```c
prototype Point {
    int x;
    int y;

    function move(int dx, int dy) : void {
        self.x = self.x + dx;
        self.y = self.y + dy;
    }
}

prototype ColoredPoint : Point {
    int color;
}

function translate(Point p) : void {
    p.move(1, 1);
}

ColoredPoint cp = ColoredPoint.clone();
cp.x = 10;
cp.y = 20;
cp.color = 3;

translate(cp);
```

---

#### Code C généré (conceptuel)

```c
typedef struct {
    int64_t x;
    int64_t y;
} Point;

typedef struct {
    Point base;   // layout compatible
    int64_t color;
} ColoredPoint;

static inline void Point_move(Point* self, int64_t dx, int64_t dy) {
    self->x += dx;
    self->y += dy;
}

void translate(Point* p) {
    Point_move(p, 1, 1);
}

void example(void) {
    ColoredPoint cp;
    cp.base.x = 10;
    cp.base.y = 20;
    cp.color  = 3;

    translate((Point*)&cp); // cast sûr et implicite côté ProtoScript
}
```

Points clés :

- aucun vtable
- aucun test runtime
- aucun coût caché
- substitution réalisée par compatibilité mémoire

Ce modèle verrouille définitivement la correspondance entre le modèle objet ProtoScript V2 et une compilation C directe et performante.

---

## 4.6 Méthodes et appels

Les méthodes :

- sont résolues **statiquement**
- ne sont pas des closures capturant implicitement l’environnement
- accèdent à l’instance via `self`

Exemple :

```c
p.move(1, 2);
```

Règles :

- l’appel de méthode est équivalent à un appel de fonction avec `self` explicite
- aucun dispatch dynamique tardif
- `self` ne peut pas être retourné ; les méthodes mutantes doivent retourner `void`
- aucun style fluent/chaîné basé sur mutation n’est supporté

---

## 4.7 Contraintes et garanties

Le modèle objet garantit :

- layout mémoire déterministe
- offsets de champs connus à la compilation
- absence de polymorphisme dynamique non contrôlé
- compatibilité directe avec une compilation vers C

Ce modèle permet d’exprimer une orientation objet sans introduire les coûts et ambiguïtés des systèmes à classes dynamiques.

# 5. Fonctions, typage et opérateurs

Cette section définit le modèle de fonctions de ProtoScript V2, la syntaxe des signatures, les règles de typage statique et les mécanismes d’inférence.

Le modèle vise à concilier **lisibilité humaine**, **simplicité sémantique** et **exploitabilité machine**.

---

## 5.1 Déclaration de fonction

Une fonction est déclarée par le mot-clé `function`, suivi de son nom, de sa liste de paramètres et de son type de retour.

```c
function add(int a, int b) : int {
    return a + b;
}
```

Règles :

- les paramètres sont typés explicitement
- le type de retour est obligatoire
- l’ordre des paramètres fait partie de la signature
- aucune surcharge de fonction n’est autorisée

---

## 5.2 Syntaxe des types

ProtoScript V2 utilise une syntaxe de type **postfixée** pour les signatures de fonctions.

```c
function f(int x, float y) : float { ... }
```

Règles :

- le séparateur `:` introduit le type de retour
- la syntaxe `->` n’est pas utilisée
- cette forme évite toute ambiguïté syntaxique

---

## 5.3 Inférence de type locale

ProtoScript V2 supporte une **inférence de type locale**, limitée et prévisible.

```c
var x = 42;        // x : int
var y = 3.14;      // y : float
var s = "hello";   // s : string
```

Règles :

- l’inférence s’applique uniquement à l’initialisation
- une variable déclarée avec `var` reçoit un type définitif
- le type ne peut pas changer ultérieurement
- aucune inférence globale ou polymorphe

---

## 5.3.1 Mot-clé `const` (normatif)

Portée :

- `const` est autorisé uniquement pour les variables de **types scalaires fondamentaux**

Sémantique :

- `const` interdit toute réaffectation
- `const` ne modifie pas le type
- `const` ne crée pas d’immuabilité profonde

Erreur statique associée :

- `E3130` — `CONST_REASSIGNMENT`

---

## 5.4 Paramètres et valeurs de retour

- les paramètres sont passés **par valeur**
- pour les structures (`list`, `map`, objets), la valeur est une référence structurée
- les fonctions retournent **une seule valeur**

```c
function length(list<int> v) : int {
    return v.length();
}
```

---

## 5.5 Fonctions sans retour

Une fonction sans valeur de retour explicite retourne `void`.

```c
function log(string s) : void {
    Io.printLine(s);
}
```

---

## 5.6 Règles de typage

- aucune conversion implicite entre types
- toute incompatibilité de type est une erreur de compilation
- les types sont connus et figés à la compilation
- les signatures de fonctions sont entièrement résolues statiquement

---

## 5.6.1 Non-généricité et absence de fonctions comme valeurs

ProtoScript V2 **ne définit pas de fonctions génériques**.

Il n’existe aucun mécanisme de paramétrage par type (`<T>`), d’inférence polymorphe ou de monomorphisation implicite.

ProtoScript V2 **ne définit pas de fonctions comme valeurs**.

Les fonctions ne peuvent pas être :

- stockées dans des variables
- passées comme paramètres
- retournées par d’autres fonctions

Les comportements paramétrables sont exprimés **exclusivement** via :

- les **prototypes**
- les **méthodes**
- l’override statique

Ce choix garantit :

- une résolution entièrement statique des appels
- l’absence totale de closures, lambdas ou callbacks dynamiques
- une compilation directe et transparente vers le C
- des performances prévisibles et optimisables

- aucune conversion implicite entre types
- toute incompatibilité de type est une erreur de compilation
- les types sont connus et figés à la compilation
- les signatures de fonctions sont entièrement résolues statiquement

---

## 5.7 Paramètres et arité

ProtoScript V2 ne définit **pas de paramètres optionnels** ni de **valeurs par défaut**.

Règles normatives :

- toute fonction doit être appelée avec **l’ensemble de ses paramètres explicitement fournis**
- l’arité d’une fonction fait partie intégrante de sa signature
- aucun mécanisme de surcharge implicite ou d’arguments omis n’est autorisé

Ce choix garantit :

- une lisibilité immédiate des appels de fonction
- une analyse statique simple et complète
- une traduction directe vers des appels C sans réécriture implicite

---

## 5.8 Fonctions variadiques

ProtoScript V2 supporte des **fonctions variadiques strictement typées**, homogènes et sans ambiguïté.

La forme variadique est conçue comme un **sucre syntaxique contrôlé** pour la capture d’un nombre variable de valeurs **d’un type unique**, sans introduire de mécanisme dynamique.

### Syntaxe

Un paramètre variadique est déclaré comme une **liste typée**, suivie de `...`.

```c
function sum(list<int> values...) : int {
    int acc = 0;
    for (int v of values)
        acc = acc + v;
    return acc;
}
```

Le type `T` de `list<T>` peut être :

- un type scalaire fondamental (`int`, `float`, `string`, etc.)
- un type prototype

---

### Règles normatives

- une fonction peut définir **au plus un paramètre variadique**
- le paramètre variadique doit être **le dernier** de la signature
- le paramètre variadique est **obligatoirement typé** sous la forme `list<T>`
- toutes les valeurs passées dans la partie variadique doivent être de type `T`
- aucune hétérogénéité n’est autorisée

Clarification normative :

- `list<T> ...` est une **syntaxe de déclaration** pour un paramètre variadique homogène
- le **type canonique interne** du paramètre variadique est `view<T>` (lecture seule)
- la transformation `list<T> ...` → `view<T>` est **implicite** et normativement définie
- `view_of(...)` est un **intrinsic de compilation** et n’est pas exposé comme API utilisateur
- la vue créée n’entraîne **aucune allocation**
- la vue créée a une durée de vie **strictement limitée à l’appel**
- la vue créée ne peut pas être stockée, retournée ni prolongée
- toute opération nécessitant une collection possédante (`push`, `pop`, redimensionnement, etc.) sur ce paramètre est une erreur de compilation

Exemples :

```c
sum(1, 2, 3);          // valide
sum(1);                // valide
sum();                 // valide (liste variadique vide)

sum(1, "x");          // erreur de compilation
```

---

### Sémantique d’appel

À l’appel, les arguments variadiques sont capturés comme une **vue non possédante** (`view<T>`), sans allocation implicite.

- la capture est **O(1)**
- aucune copie des éléments n’est effectuée
- le coût est équivalent à un passage explicite de `view<T>`

Conceptuellement, un appel :

```c
sum(1, 2, 3);
```

est équivalent à :

```c
view<int> tmp = view_of(1, 2, 3);
sum(tmp);
```

---

### Cohérence avec l’arité

Le paramètre variadique est considéré comme **un paramètre unique** du point de vue de la signature.

Ainsi :

- l’arité minimale de la fonction reste fixe
- la règle de la section 5.7 reste pleinement valable

Les paramètres non variadiques restent obligatoires ; la partie variadique peut être vide.

---

## 5.9 Méthodes variadiques sur prototypes

Les **méthodes définies sur des prototypes** peuvent être **variadiques**, selon exactement les mêmes règles que les fonctions variadiques définies en section 5.8.

Il n’existe **aucune règle spéciale** pour les méthodes : une méthode variadique est une fonction variadique dont le premier paramètre implicite est `self`.

---

### Syntaxe

Une méthode variadique est déclarée en utilisant un paramètre `list<T> ...` en dernière position.

```c
prototype Logger {
    function log(list<string> messages...) : void {
        for (string s of messages)
            Io.printLine(s);
    }
}
```

Appels valides :

```c
Logger l = Logger.clone();

l.log("start");
l.log("x=", "42", "done");
```

---

### Règles normatives

- une méthode peut définir **au plus un paramètre variadique**
- le paramètre variadique doit être **le dernier paramètre explicite** (après `self`)
- le type `T` peut être un type scalaire fondamental ou un type prototype
- toutes les valeurs passées doivent être du même type `T`
- aucune hétérogénéité n’est autorisée

### Règle normative — arité des méthodes variadiques

Pour toute signature se terminant par :

```c
list<T> ident...
```

les règles suivantes s’appliquent :

1. L’arité minimale est égale au nombre de paramètres non-variadiques explicites.
2. `self` est exclu du calcul d’arité utilisateur sur appel d’instance (`obj.m(...)`), et reste explicite sur appel statique (`Proto.m(self, ...)`).
3. Si aucun argument n’est fourni pour la partie variadique :
   - le paramètre reçoit une `view<T>` vide ;
   - aucune erreur `E1003` `ARITY_MISMATCH` ne doit être levée.
4. Si le nombre d’arguments est inférieur au nombre de paramètres non-variadiques, `E1003` `ARITY_MISMATCH` doit être produit.
   Cette règle ne concerne pas les signatures purement variadiques.
5. Ces règles sont identiques pour :
   - les fonctions globales ;
   - les méthodes de prototype.

Exemple valide (aucun paramètre fixe) :

```c
function f(list<int> xs...) : void { }
f(); // valide
```

Exemple en erreur :

```c
function f(int a, list<int> xs...) : void { }
f(); // ERREUR : paramètre fixe "a" manquant -> E1003 ARITY_MISMATCH
```

---

### Typage et accès

Le type statique du récepteur gouverne :

- les méthodes accessibles
- la signature exacte, y compris la partie variadique

```c
prototype Point {
    function moveAll(list<Point> points...) : void { ... }
}
```

L’appel :

```c
p.moveAll(p1, p2, p3);
```

est équivalent à :

```c
view<Point> tmp = view_of(p1, p2, p3);
Point.moveAll(p, tmp);
```

---

### Override de méthodes (généralités)

ProtoScript V2 autorise l’**override de méthodes** dans un prototype enfant **uniquement si la signature est strictement identique** à celle de la méthode du prototype parent.

Cela vaut pour **toutes les méthodes**, variadiques ou non.

Règles normatives :

- le nom de la méthode doit être identique
- le nombre de paramètres doit être identique
- le type de chaque paramètre doit être identique et dans le même ordre
- le type de retour doit être identique
- aucune surcharge par variation de paramètres n’est autorisée

Ainsi, ProtoScript V2 **ne supporte pas la surcharge de méthodes** au sens de Java ou C++.

---

### Override de méthodes variadiques

Une **méthode variadique peut être override** dans un prototype enfant **uniquement si la signature est strictement identique**, y compris la partie variadique.

Règles normatives spécifiques :

- le paramètre variadique (`list<T> ...`) doit être présent
- le type `T` doit être identique
- le paramètre variadique doit rester en dernière position
- aucune modification de l’arité minimale n’est autorisée

Exemple valide :

```c
prototype Logger {
    function log(list<string> messages...) : void {
        for (string s of messages)
            Io.printLine("[LOG] ".concat(s));
    }
}

prototype DebugLogger : Logger {
    function log(list<string> messages...) : void {
        Io.printLine("[DEBUG]");
        Logger.log(self, messages);
    }
}
```

Exemples interdits :

```c
prototype BadLogger1 : Logger {
    function log(string s) : void { }        // erreur : signature différente
}

prototype BadLogger2 : Logger {
    function log(list<string> messages..., int level) : void { } // erreur
}
```

Dans tous les cas :

- la résolution de la méthode reste **strictement statique**
- aucun dispatch dynamique n’est introduit
- les règles de substitution et de performance restent inchangées

---

### Sémantique et performances

- la capture variadique se fait sous forme de `view<T>`
- aucune allocation implicite
- aucun dispatch dynamique supplémentaire
- coût identique à un appel de méthode non variadique

Les méthodes variadiques ne modifient **en rien** les garanties de performance ni les invariants du modèle objet.

---

## 5.10 Fonctions et performances

Le modèle de fonction garantit :

- appels directs sans dispatch dynamique
- possibilité d’inlining agressif
- génération de code C simple et lisible

Les fonctions constituent l’unité fondamentale d’optimisation du compilateur.

---

## 5.11 Opérateurs

Cette section définit l’ensemble des opérateurs du langage, leurs catégories et leurs règles de typage. Les opérateurs sont **strictement typés**, **résolus statiquement** et **sans conversion implicite**.

### 5.11.1 Principes généraux

- aucun opérateur n’introduit de conversion implicite
- le type des opérandes détermine le type du résultat
- toute incompatibilité de type est une erreur de compilation
- les opérateurs sont des **primitifs du langage** (non surchargeables)

---

### 5.11.2 Opérateurs arithmétiques

Opérateurs supportés : `+`, `-`, `*`, `/`

Opérateurs arithmétiques additionnels :

| Opérateur | Description            |
| --------- | ---------------------- |
| `%`       | reste entier           |
| `++`      | incrémentation         |
| `--`      | décrémentation         |
| `-`       | négation unaire        |

Types autorisés :

- `int` avec `int` → `int`
- `float` avec `float` → `float`

Règles :

- aucune promotion automatique (`int + float` est interdit)
- la division entière `/` entre `int` est une division entière
- `%` est réservé aux types entiers

---

### 5.11.3 Opérateurs de comparaison

Opérateurs supportés : `==`, `!=`, `<`, `<=`, `>`, `>=`

Résultat : `bool`

Règles :

- comparaisons autorisées uniquement entre types identiques
- pour `string`, `==`/`!=` comparent le contenu exact ; `<`/`<=`/`>`/`>=` comparent lexicographiquement la séquence UTF‑8 (pas de locale, pas de normalisation)
- pour les types structurés (objets/prototypes, `list`, `map`, `slice`, `view`), la comparaison porte sur l’identité de valeur (pas de deep compare implicite)

---

### 5.11.4 Opérateurs logiques

Opérateurs supportés : `&&`, `||`, `!`

Types autorisés :

- opérandes de type `bool`

Règles :

- évaluation court-circuitée (`&&`, `||`)
- aucun autre type n’est accepté

---

### 5.11.5 Opérateurs d’affectation

Opérateur principal : `=`

Règles :

- le type de l’expression assignée doit être strictement identique au type de la variable
- l’affectation est une instruction, sans valeur de retour
- aucune affectation chaînée n’est autorisée

Opérateurs composés (`+=`, `-=`, `*=`, `/=`) :

- définis uniquement lorsque l’opérateur arithmétique correspondant est valide
- équivalents à une écriture explicite sans effet de bord

L’interdiction de l’affectation chaînée garantit l’absence d’effets de bord implicites, simplifie l’analyse statique et renforce la lisibilité.

---

### 5.11.6 Opérateurs d’accès et d’appel

- accès aux éléments : `[]`
- accès aux membres : `.`
- appel de fonction ou méthode : `()`

Règles :

- l’accès `[]` est défini pour `list`, `map` et `string`
- pour `string`, l’accès `[]` est strictement en lecture
- toute affectation via `string[index] = value` est interdite (erreur statique)
- l’opérateur `.` est résolu statiquement selon le type connu

---

### 5.11.7 Opérateurs binaires sur les bits

ProtoScript V2 définit des opérateurs binaires explicites pour les types entiers `int` et `byte`.

Opérateurs supportés : `&`, `|`, `^`, `~`, `<<`, `>>`

Types autorisés :

- `int` avec `int` → `int`
- `byte` avec `byte` → `byte`

Règles :

- aucun opérateur binaire n’est défini pour `float`
- aucun mélange de types (`int & byte` est interdit)
- les décalages sont effectués sur la largeur native du type

---

### 5.11.8 Absences volontaires

ProtoScript V2 ne définit pas :

- d’opérateur de concaténation de chaînes
- d’opérateur ternaire implicite sur types non booléens
- d’opérateurs de surcharge utilisateur
- d’opérateurs dépendants du runtime ou du type réel

Ces absences sont des **choix de conception**, visant à garantir lisibilité, prévisibilité et performances.

# 6. Contrôle de flux

Cette section définit les structures de contrôle de flux de ProtoScript V2. Elles sont volontairement **classiques**, explicites et sans ambiguïté, afin de garantir lisibilité, prévisibilité et performances.

ProtoScript V2 ne cherche pas à innover syntaxiquement dans ce domaine, mais à fournir un ensemble cohérent et suffisant.

---

## 6.1 Blocs et instructions

- un bloc est délimité par `{` et `}`
- un bloc est requis lorsqu’il contient plusieurs instructions
- lorsqu’une structure de contrôle ne contient **qu’une seule instruction**, les accolades sont optionnelles

```c
if (x > 0)
    y = x;
```

---

## 6.2 Conditionnelle `if / else`

```c
if (x > 0) {
    y = x;
} else if (x < 0) {
    y = -x;
} else {
    y = 0;
}
```

Règles :

- l’expression conditionnelle doit être de type `bool`
- aucune conversion implicite vers `bool`
- la chaîne `else if` est une simple composition syntaxique

---

## 6.3 Opérateur ternaire

ProtoScript V2 supporte l’opérateur ternaire classique.

```c
int y = (x > 0) ? x : -x;
```

Règles :

- la condition est de type `bool`
- les deux branches doivent avoir un type compatible
- l’expression résultante est typée statiquement
- l’opérateur conditionnel est une expression pure : il ne peut introduire ni affectation implicite, ni effet de bord non explicitement visible

---

## 6.4 Boucles `for`

### Boucle classique

```c
for (int i = 0; i < n; i++) {
    sum = sum + i;
}
```

- toutes les expressions sont optionnelles (`for (;;)` autorisé)

---

### Boucle `for … of`

```c
for (int v of list) {
    sum = sum + v;
}
```

Règles :

- `of` itère sur les **valeurs**
- l’ordre d’itération est l’ordre naturel de la structure
- applicable à `string`, `list<T>`, `map<K,V>` (valeurs)

---

### Boucle `for … in`

```c
for (string k in map) {
    print(k);
}
```

Règles :

- `in` itère sur les **clés**
- applicable uniquement aux structures associatives (`map`)

---

## 6.5 Boucles `while` et `do … while`

```c
while (cond) {
    work();
}

do {
    work();
} while (cond);
```

Règles :

- la condition est évaluée explicitement
- la condition doit être de type `bool`

---

## 6.6 Instructions de contrôle

### `break` et `continue`

```c
for (int i = 0; i < n; i++) {
    if (i == 5)
        continue;
    if (i == 8)
        break;
}
```

- `break` sort de la boucle courante
- `continue` passe à l’itération suivante

---

## 6.7 Instruction `switch`

```c
switch (x) {
    case 0:
        y = 0;
        break;
    case 1:
        y = 10;
        break;
    default:
        y = -1;
        break;
}
```

Règles :

- l’expression du `switch` est évaluée une seule fois
- les `case` sont comparés par égalité stricte
- le fallthrough implicite est interdit
- chaque clause `case` et `default` doit se terminer explicitement par `break`, `return`, `throw` ou toute autre instruction qui quitte le `switch`
- l’absence de terminaison explicite est une erreur statique
- `default` est optionnel

ProtoScript V2 rejette le fallthrough implicite afin que chaque branche de contrôle ait un effet local, explicite et immédiatement lisible.

---

## 6.8 Garanties

Les structures de contrôle de flux garantissent :

- absence d’effets implicites
- absence de conversions automatiques
- traduction directe vers des structures de contrôle C
- aucune allocation implicite liée au contrôle de flux

# 7. Itération

Cette section définit le **modèle d’itération** de ProtoScript V2. L’itération est conçue comme un mécanisme **simple, explicite et déterministe**, sans abstraction implicite ni allocation cachée.

Le modèle d’itération est volontairement séparé des structures de contrôle (section 6) afin de clarifier les responsabilités sémantiques.

---

## 7.1 Principe général

ProtoScript V2 définit l’itération comme une **capacité intrinsèque de certains types**, et non comme un protocole dynamique.

Sont itérables nativement :

- `string` (en `glyph`)
- `list<T>` (en `T`)
- `map<K,V>`

L’ordre d’itération est **strictement déterministe** et dépend uniquement de la structure sous-jacente.

---

## 7.2 Boucle `for … of`

La forme `for … of` permet d’itérer sur les **valeurs** d’une structure itérable.

```c
for (int v of list) {
    sum = sum + v;
}
```

Règles :

- la variable d’itération est typée explicitement ou par inférence locale
- `of` itère sur les **éléments**
- l’ordre d’itération est l’ordre naturel de la structure
- aucune allocation implicite n’est effectuée

Types supportés :

- `for (glyph g of string)`
- `for (T v of list<T>)`
- `for (V v of map<K,V>)`

---

## 7.3 Boucle `for … in`

La forme `for … in` permet d’itérer sur les **clés** d’une structure associative.

```c
for (string key in map) {
    print(key);
}
```

Règles :

- `in` itère sur les **clés**
- applicable uniquement à `map<K,V>`
- l’ordre d’itération est l’ordre d’insertion

---

## 7.4 Typage et inférence dans l’itération

```c
for (var v of list) {
    // v a le type T
}
```

Règles :

- l’inférence est locale à la variable d’itération
- le type inféré est fixe et non polymorphe
- aucune conversion implicite n’est autorisée

---

## 7.5 Itération et performances

Les garanties suivantes sont normatives :

- l’itération est **O(n)** sur le nombre d’éléments
- aucune allocation implicite liée à l’itération
- aucune création d’objet itérateur visible
- traduction directe vers une boucle `for` ou `while` en C

---

## 7.6 Absence de protocole itérateur dynamique

ProtoScript V2 ne définit :

- ni interface `Iterable`
- ni méthode `iterator()`
- ni objet itérateur exposé

Les types itérables sont connus **statiquement**.

Ce choix permet :

- une analyse statique complète
- des performances prévisibles
- une compilation directe vers du code bas niveau

---

## 7.7 Résumé

Le modèle d’itération de ProtoScript V2 privilégie :

- la simplicité
- la déterminisme
- la performance
- l’absence d’abstraction inutile

Il est conçu pour être compris immédiatement par l’humain et exploité efficacement par la machine.

# 8. Sémantique d’exécution & performance

Cette section définit les règles d’exécution de ProtoScript V2, les invariants runtime et les objectifs de performance considérés comme **non négociables**.

ProtoScript V2 est conçu pour produire un code **prévisible, analysable et performant**, sans dépendre de mécanismes dynamiques opaques.

---

## 8.1 Modèle d’exécution général

ProtoScript V2 adopte un modèle d’exécution **strict et déterministe**.

Règles fondamentales :

- l’ordre d’évaluation des expressions est strictement défini
- aucune évaluation paresseuse implicite
- aucune conversion implicite de type
- toute allocation mémoire est explicite ou documentée

Le comportement d’un programme est indépendant de l’environnement d’exécution (pas de dépendance à un moteur ou à un JIT).

---

## 8.2 Objectifs performance non négociables

ProtoScript V2 vise des performances de **première catégorie**, comparables à du C bien écrit.

Les objectifs suivants sont considérés comme **non négociables** :

- aucune allocation implicite dans les boucles critiques
- aucune conversion de type implicite
- accès direct en temps constant aux données structurées
- représentation mémoire stable et déterministe
- performances prévisibles, indépendantes du contenu des données
- capacité à traiter efficacement des charges lourdes (image, signal, texte, données)

Le langage doit permettre l’écriture d’algorithmes intensifs sans pénalité structurelle.

---

## 8.3 Représentations mémoire et invariants runtime

Les invariants suivants sont garantis par le compilateur et le runtime.

### Types scalaires

- `int` : entier signé 64 bits
- `float` : flottant double précision IEEE-754
- `byte` : entier non signé 8 bits
- `glyph` : entier non signé 32 bits (Unicode scalar value)

---

### Structures de données

**list<T>**

- stockage contigu en mémoire
- représentation `(ptr, length, capacity)`
- éléments monomorphes de type `T`
- aucun boxing

**map<K,V>**

- table de hachage
- accès O(1) amorti
- ordre d’insertion conservé

**slice<T> / view<T>**

- vues non possédantes
- représentation `(ptr, length)`
- aucune allocation
- aucune extension de durée de vie

**string**

- stockage UTF-8
- index glyphes construit paresseusement
- itération séquentielle sans allocation

---

### Objets et prototypes

- layout mémoire figé par prototype
- offsets de champs connus à la compilation
- aucune mutation structurelle dynamique

---

## 8.4 Allocation et durée de vie

ProtoScript V2 ne définit pas de garbage collector obligatoire.

Règles :

- les allocations sont explicites
- la durée de vie des objets est déterminée statiquement ou par le scope
- aucune allocation cachée liée au langage

Les stratégies d’allocation (heap, arena, stack étendue) sont des choix d’implémentation compatibles avec la spec.

---

## 8.5 Modes d’exécution

Une implémentation peut proposer plusieurs modes :

- **mode debug** :
  
  - vérifications de bornes
  - assertions actives
  - messages d’erreur détaillés

- **mode release** :
  
  - vérifications minimales
  - optimisations agressives
  - aucune surcharge inutile

Le passage d’un mode à l’autre ne modifie pas la sémantique observable du programme.

---

## 8.6 Erreurs runtime

Les erreurs runtime :

- sont levées sous forme d’exceptions dérivant de `Exception`
- incluent implicitement : fichier, ligne, colonne
- ne peuvent pas être silencieuses

Aucune erreur runtime n’est convertie implicitement en valeur.

---

## 8.7 Résumé

La sémantique d’exécution de ProtoScript V2 privilégie :

- la prévisibilité
- la performance
- la lisibilité
- l’absence de magie runtime

Elle est conçue pour être directement traduite vers du code bas niveau sans perte d’intention.

# 9. IR & compilation

Cette section définit la **représentation intermédiaire (IR)** de ProtoScript V2 et les stratégies de compilation associées.

L’IR est conçu comme un **pivot central**, simple, typé et déterministe, permettant :

- une analyse statique complète,
- des optimisations prévisibles,
- une compilation vers plusieurs backends,
- en particulier une **génération de code C de premier rang**.

---

## 9.1 Rôle de l’IR

L’IR de ProtoScript V2 constitue une forme normalisée du programme source.

Ses objectifs principaux sont :

- éliminer toute ambiguïté syntaxique ou sémantique,
- rendre explicites les types, les allocations et les accès mémoire,
- servir de base unique à l’optimisation et à la génération de code.

Chaque programme ProtoScript V2 valide peut être traduit **sans perte d’information** vers l’IR.

---

## 9.2 Propriétés fondamentales de l’IR

L’IR respecte les invariants suivants :

- typage **entièrement statique**
- aucune conversion implicite
- chaque variable possède un type concret
- chaque accès mémoire est explicite
- chaque champ et élément possède un offset connu
- aucun dispatch dynamique tardif

L’IR est volontairement **bas niveau**, proche d’une forme SSA, sans être lié à une implémentation particulière.

---

## 9.3 Correspondance source → IR

Les constructions du langage sont traduites de manière directe :

- fonctions → unités IR avec signature typée
- prototypes → structures IR avec layout figé
- appels de méthodes → appels de fonctions avec `self` explicite
- boucles → structures de contrôle explicites
- `slice` / `view` → vues mémoire sans allocation
- membres de `group` → constantes typées (substitution, aucune entité IR)

Aucune transformation spéculative n’est effectuée à ce stade.

---

## 9.4 Optimisations

Les optimisations s’effectuent **sur l’IR**, jamais directement sur le code source.

Optimisations attendues :

- inlining de fonctions
- élimination du code mort
- propagation de constantes
- suppression des vérifications inutiles en mode release

Toutes les optimisations doivent préserver strictement la sémantique observable du programme.

---

## 9.5 Compilation vers C

La compilation vers C est une **cible de premier rang** de ProtoScript V2.

Objectifs :

- produire du C lisible et standard
- s’appuyer sur les compilateurs existants (clang, gcc)
- bénéficier de leurs optimisations (vectorisation, register allocation)
- faciliter l’interopérabilité avec des bibliothèques natives

### Correspondances de types

| ProtoScript            | C                                            |
| ---------------------- | -------------------------------------------- |
| `int`                  | `int64_t`                                    |
| `float`                | `double`                                     |
| `byte`                 | `uint8_t`                                    |
| `glyph`                | `uint32_t`                                   |
| `list<T>`              | `struct { T* ptr; size_t len; size_t cap; }` |
| `slice<T>` / `view<T>` | `struct { T* ptr; size_t len; }`             |

---

## 9.6 Autres backends

L’IR permet également :

- une compilation directe vers du code natif
- une interprétation simple pour le debug
- des backends expérimentaux (WASM, etc.)

Ces backends partagent le même IR et les mêmes garanties sémantiques.

---

## 9.7 Résumé

L’IR de ProtoScript V2 est :

- simple
- typé
- déterministe
- conçu pour la performance

Il constitue le socle technique reliant la lisibilité du langage source à l’efficacité du code généré.

# 10. Erreurs & exceptions

Cette section définit le modèle d’erreurs et d’exceptions de ProtoScript V2. L’objectif est de fournir des **messages clairs**, **contextualisés**, et une gestion explicite des erreurs, sans ambiguïté sémantique ni coût caché.

ProtoScript V2 considère la qualité des erreurs comme un élément **fondamental de l’ergonomie du langage**.

---

## 10.1 Principes généraux

- toute erreur est **explicite**
- aucune erreur n’est silencieuse
- aucune erreur n’est convertie implicitement en valeur
- les erreurs sont soit **détectées à la compilation**, soit **levées à l’exécution**

Les erreurs runtime sont représentées par des **exceptions**.

---

## 10.2 Messages d’erreur

Les diagnostics statiques et runtime font partie du **contrat de stabilité du langage**.

Format normatif obligatoire :

```
<file>:<line>:<column> <ErrorCode> <ErrorName>: <message>
```

Règles normatives :

- une implémentation conforme **MUST** produire exactement ce format sur la ligne principale
- `file` **MUST** être le chemin logique source du programme
- `line` et `column` **MUST** pointer sur le premier caractère du lexème fautif
- `ErrorCode` **MUST** être un code canonique stable (ex. `E2001`, `R1004`)
- `ErrorName` **MUST** être la catégorie canonique stable associée au code
- pour les erreurs liées aux identifiants, le lexème fautif **MUST** être cité (`'name'`)
- le message **MUST** expliciter :
  - ce qui est fautif
  - ce qui est attendu (`expected ...`)
- les lignes de suggestion sont **optionnelles** et **MUST** apparaître uniquement après la ligne principale
- une suggestion **MUST NOT** modifier le code d’erreur

Format de suggestion (si présente) :

```
Did you mean 'X'?
Did you mean 'X' or 'Y'?
```

Une implémentation **MUST NOT** émettre d’autres formats de continuation.

Exemples conformes :

```
script.pts:15:16 E2001 UNRESOLVED_NAME: unknown identifier 'Colr' (expected value)
Did you mean 'Color'?
script.pts:10:5 E2001 UNRESOLVED_NAME: unknown group member 'Purple' in group 'Color' (expected member)
```

### 10.2.1 Diagnostic Code Classification

Les codes de diagnostic sont hiérarchiques et stables.

Plages réservées (normatives) :

| Domaine | Plage | Statut |
|---|---|---|
| Parse / lexing / arité | `E1xxx` | réservé |
| Résolution de noms | `E2xxx` | réservé |
| Typage | `E3xxx` | réservé |
| Sémantique statique | `E4xxx` | réservé |
| Modules / imports | `E5xxx` | réservé |
| Vérifications statiques runtime futures | `E6xxx` | réservé |
| Runtime | `R1xxx` | réservé |

Règles :

- un code **MUST** être unique
- un code canonique publié **MUST NOT** être repurposé
- l’association `ErrorCode -> ErrorName` **MUST** rester stable entre versions mineures
- les codes font partie du contrat de compatibilité de ProtoScript2

### 10.2.2 Objet Diagnostic (normatif)

Les frontends doivent construire les diagnostics via un objet structuré avant formatage.

Champs normatifs :

- `file`
- `line`
- `column`
- `code`
- `name`
- `message`
- `expected_kind` (optionnel)
- `actual_kind` (optionnel)
- `suggestions[]` (optionnel)

Règles :

- le rendu texte (10.2) **MUST** être produit par un formateur centralisé
- la logique sémantique **MUST** transmettre des données structurées (code, nom, position, contexte), pas uniquement une chaîne finale
- `suggestions[]` **MUST** être déterministe pour un même input

### 10.2.3 Suggestions `Did you mean ... ?` (normatif)

Pour les erreurs de résolution de noms (`E2xxx`), une implémentation peut ajouter une suggestion déterministe.

Règles :

- la distance utilisée **MUST** être une distance d’édition (Levenshtein)
- le seuil maximal **MUST** être `2`
- une suggestion est autorisée uniquement si un meilleur candidat minimal est trouvé
- si plusieurs candidats partagent la distance minimale :
  - un format à deux suggestions est autorisé (`X` ou `Y`)
  - en cas d’ambiguïté plus large, aucune suggestion ne doit être émise
- l’ordre des suggestions **MUST** être déterministe
- les suggestions **MUST NOT** modifier le code ou le nom du diagnostic principal

---

## 10.3 Prototype `Exception`

Toutes les exceptions dérivent du **prototype racine** `Exception`.

Caractéristiques :

- `Exception` est un **prototype standard** du langage
- elle contient implicitement :
  - le fichier
  - la ligne
  - la colonne
- elle peut contenir explicitement :
  - un message
  - une cause
- `Exception` et `RuntimeException` sont des **prototypes non appelables** : l’instanciation se fait exclusivement via `clone()`

Ces champs sont des **propriétés accessibles** de l’objet d’exception.

Prototype standard additionnel :

- `RuntimeException` est un prototype standard qui **dérive de `Exception`**
- toutes les erreurs runtime doivent lever une instance de `RuntimeException`
- seules les instances de `RuntimeException` exposent **obligatoirement** :
  - `code` (code canonique, ex. `R1004`)
  - `category` (catégorie canonique, ex. `RUNTIME_DIVIDE_BY_ZERO`)

Aucune valeur autre qu’une instance dérivée du prototype `Exception` ne peut être levée.

---

## 10.4 Lever une exception (`throw`)

Une exception peut être levée explicitement :

```c
Exception e = Exception.clone();
e.message = "invalid state";
throw e;
```

Règles :

- seul un objet dérivant de `Exception` peut être levé
- `throw` attend une **valeur d’exception** déjà construite, jamais un effet ni un constructeur implicite
- les champs de localisation sont automatiquement renseignés par le runtime
- le message et la cause sont à l’initiative du développeur

---

## 10.5 Gestion des exceptions (`try / catch / finally`)

ProtoScript V2 supporte la gestion structurée des exceptions.

```c
try {
    work();
} catch (Exception e) {
    log(e.toString());
} finally {
    cleanup();
}
```

Règles :

- `catch` intercepte une exception par type (prototype), avec substitution parent/enfant
- `catch (Exception e)` est un **catch‑all**
- la variable `e` est **une instance** d’un prototype dérivant de `Exception`
- `e` expose au minimum : `file`, `line`, `column`, `message`, `cause`
- si l’exception dérive de `RuntimeException`, `e.code` et `e.category` sont définis
- `finally` est toujours exécuté
- l’exception est propagée si elle n’est pas interceptée
- une exception non interceptée produit un diagnostic `R1011` / `UNHANDLED_EXCEPTION` incluant :
  - le type dynamique de l’exception
  - la localisation source
  - l’attente d’un `catch` correspondant

Clarification normative (RTTI et exceptions) :

- l’interdiction de RTTI concerne les valeurs utilisateur : objets/prototypes, collections et vues
- aucune forme de `instanceof`, de downcast dynamique ou d’introspection de type n’est autorisée pour les valeurs utilisateur
- le mécanisme d’exception repose sur une métadonnée de type interne au runtime, utilisée exclusivement pour la propagation et l’interception des exceptions
- cette métadonnée n’est pas introspectable par le langage et n’est accessible que dans le mécanisme `catch`
- `catch (T e)` intercepte une exception si son type dynamique est `T` ou dérive de `T`
- ce mécanisme n’introduit aucun RTTI général, ne permet aucun test de type dynamique hors exceptions et ne modifie pas le coût du chemin nominal

---

## 10.6 Exceptions et performances

Le modèle d’exception garantit :

- le mécanisme de propagation des exceptions (`unwind`, dispatch) n’introduit aucun coût tant qu’aucune exception n’est levée
- un coût explicite et localisé existe lors de la levée et de la propagation d’une exception
- les vérifications runtime exigées par la spécification font partie de l’exécution normale
- un backend peut éliminer une vérification runtime uniquement s’il prouve statiquement qu’elle est inutile

Les exceptions sont considérées comme un **mécanisme de contrôle de flux exceptionnel**, et non comme un substitut aux retours de fonction.

---

## 10.7 Résumé

Le modèle d’erreurs et d’exceptions de ProtoScript V2 privilégie :

- la clarté des diagnostics
- la cohérence sémantique
- la sécurité
- la prévisibilité des performances

Il est conçu pour faciliter le débogage et renforcer la confiance du développeur dans le langage.

# 11. Non-objectifs & principes

Cette section explicite ce que ProtoScript V2 **ne cherche volontairement pas à être**, ainsi que les principes directeurs qui guident ces refus.

Ces non-objectifs sont des **choix de conception assumés**, destinés à préserver la cohérence, la lisibilité et les performances du langage.

---

## 11.1 Non-objectifs explicites

ProtoScript V2 ne vise pas :

- la compatibilité avec JavaScript, ECMAScript ou leurs écosystèmes
- l’exécution dans un navigateur ou un moteur web
- le typage dynamique ou graduel
- la métaprogrammation réflexive ou l’introspection runtime
- la modification dynamique des structures de données ou des objets
- la surcharge d’opérateurs implicite
- les conversions implicites entre types
- la dissimulation des allocations mémoire
- les abstractions à coût imprévisible

Ces limitations sont intentionnelles.

---

## 11.2 Refus des abstractions opaques

ProtoScript V2 refuse les abstractions qui :

- masquent les coûts algorithmiques
- introduisent des effets de bord implicites
- rendent le comportement dépendant du runtime
- empêchent une analyse statique complète

En particulier, ProtoScript V2 n’introduit :

- ni garbage collector obligatoire
- ni mécanisme de dispatch dynamique tardif
- ni protocoles implicites (itérateurs, conversions, hooks)

---

## 11.3 Principe de lisibilité stricte

Tout code ProtoScript V2 doit permettre à un lecteur humain de comprendre :

- les types en jeu
- les allocations effectuées
- les coûts dominants
- les chemins d’erreur possibles

Si une fonctionnalité améliore artificiellement la concision au détriment de cette lisibilité, elle est rejetée.

---

## 11.4 Principe de déterminisme

ProtoScript V2 privilégie un comportement :

- déterministe
- reproductible
- indépendant de l’environnement d’exécution

Deux exécutions du même programme, avec les mêmes entrées, doivent produire le même comportement observable.

---

## 11.5 Principe de confiance développeur

ProtoScript V2 est conçu pour que le développeur puisse avoir **confiance** dans ce qu’il écrit.

Cela implique :

- pas de "magie" runtime
- pas de comportement implicite
- pas de règles contextuelles cachées

Le langage doit se comporter comme il est écrit.

---

## 11.6 Résumé

Les non-objectifs et principes de ProtoScript V2 servent un but unique :

> **permettre l’écriture de logiciels lisibles, fiables et performants, sans dette conceptuelle.**

Ils constituent le garde-fou qui empêche le langage de dériver vers des compromis incohérents.

# 12. Positionnement conceptuel & conclusion

Cette section situe ProtoScript V2 dans le paysage des langages de programmation et conclut la spécification par une synthèse de ses choix fondamentaux.

ProtoScript V2 n’est pas un langage généraliste permissif. C’est un **langage de construction**, conçu pour durer, être compris et être maîtrisé.

---

## 12.1 Positionnement conceptuel

ProtoScript V2 se positionne à l’intersection de plusieurs traditions :

- la **sobriété du C**, pour la maîtrise des coûts et des représentations
- le **modèle prototype-based de Self**, pour l’expressivité objet sans classes
- la **lisibilité syntaxique de JavaScript**, débarrassée de ses ambiguïtés
- la **rigueur des langages compilés**, sans dépendance à un runtime spéculatif

Il ne cherche pas à concurrencer les langages dynamiques sur la rapidité d’écriture, ni les langages ultra-abstraits sur la métaprogrammation.

Il vise un autre objectif : permettre d’écrire du code **clair, fiable et performant**, sans dette conceptuelle.

---

## 12.2 À qui s’adresse ProtoScript V2

ProtoScript V2 s’adresse principalement :

- aux développeurs souhaitant comprendre réellement ce que fait leur code
- aux ingénieurs sensibles aux coûts mémoire et algorithmiques
- aux projets nécessitant des performances prévisibles
- aux systèmes durables, maintenables sur le long terme

Il n’est pas conçu pour :

- le prototypage rapide jetable
- l’expérimentation syntaxique
- les environnements fortement dynamiques ou réflexifs

---

## 12.3 Choix assumés

ProtoScript V2 assume explicitement :

- l’absence de classes
- l’absence de typage dynamique
- l’absence de conversions implicites
- l’absence de garbage collector obligatoire
- l’absence de surcharge syntaxique trompeuse

Ces choix ne sont pas des limitations accidentelles, mais des **décisions structurantes**.

---

## 12.4 Continuité et évolution

ProtoScript V2 pose un socle volontairement strict.

Toute évolution future du langage devra :

- préserver les invariants existants
- respecter les garanties de complexité
- maintenir la lisibilité humaine
- ne jamais introduire de magie runtime

Les extensions possibles (nouveaux modules, backends, optimisations) devront s’inscrire dans ce cadre.

---

## 12.5 Conclusion

ProtoScript V2 est fondé sur une conviction simple :

> **un langage de programmation doit être lisible pour l’humain et honnête pour la machine.**

En rendant explicites les types, les coûts, les allocations et les erreurs, ProtoScript V2 vise à restaurer une relation de confiance entre le développeur et son outil.

Il ne promet pas la facilité immédiate, mais la maîtrise à long terme.

# Partie normative

Les sections 13 à 18 et les annexes A à C sont **normatives**.

Interprétation des termes normatifs :

- **doit** / **MUST** : exigence obligatoire
- **ne doit pas** / **MUST NOT** : interdiction absolue
- **devrait** / **SHOULD** : recommandation forte, dérogation motivée
- **peut** / **MAY** : option autorisée

En cas de conflit d’interprétation :

- les sections normatives prévalent sur les sections descriptives
- les annexes A, B, C prévalent sur les exemples informatifs

# 13. Grammaire formelle

Cette section définit la grammaire normative de ProtoScript V2.

- une implémentation conforme doit accepter tout programme valide selon l’annexe B
- une implémentation conforme doit rejeter tout programme invalide selon l’annexe B
- aucun parseur ne doit dépendre d’une interprétation implicite non décrite ici

## 13.1 Références normatives

- Annexe A : grammaire lexicale complète
- Annexe B : grammaire syntaxique EBNF complète
- Annexe C : précédence, associativité, ordre d’évaluation

## 13.2 Désambiguïsation syntaxique

Les règles suivantes sont obligatoires :

- le `else` est rattaché au `if` non fermé le plus proche
- une déclaration variadique est reconnue uniquement sous la forme `list<T> ident...`
- un paramètre variadique représente une séquence d’arguments (possiblement vide)
- `for (... of ...)` et `for (... in ...)` sont distincts de `for (init; cond; step)`
- `:` dans une signature de fonction introduit exclusivement le type de retour
- les littéraux `map` utilisent `{ key : value }` et ne sont jamais des blocs

## 13.3 Précédence, associativité et effets de bord

- la précédence et l’associativité sont définies exclusivement par l’annexe C
- l’ordre d’évaluation des sous-expressions est strictement de gauche à droite
- `&&` et `||` doivent court-circuiter
- les effets de bord doivent être observables dans l’ordre d’évaluation défini
- l’affectation est une instruction sans valeur ; `a = b = c` est grammaticalement invalide

## 13.4 Validité grammaticale minimale

Un programme est grammaticalement valide si et seulement si :

- son flux de tokens est valide selon l’annexe A
- il dérive de la règle `Program` de l’annexe B
- il respecte les règles de désambiguïsation de la section 13.2

# 14. Sémantique opérationnelle des cas limites

Cette section définit les comportements obligatoires sur cas limites.

- aucun comportement ne peut être laissé dépendant de l’implémentation
- tout cas non statiquement rejeté doit avoir un comportement runtime défini

## 14.1 Principes

- une violation détectable à la compilation doit produire une erreur statique
- une violation non détectable statiquement doit lever une exception runtime
- le mode release ne doit pas modifier le résultat observable d’un programme valide

## 14.2 Tableau normatif des cas limites

Principe général (normatif) :

- toute **lecture** par accès indexé doit être stricte (la donnée ciblée doit exister)
- toute **écriture** par accès indexé doit suivre la règle du type ciblé sans effet implicite non spécifié
- pour `map<K,V>`, l’écriture est constructive (insertion si absente, mise à jour si présente)

| Cas | Statut | Comportement normatif | Diagnostic minimal |
| --- | --- | --- | --- |
| overflow `int` (add/sub/mul, négation de la valeur minimale) | exception runtime | l’opération doit lever une exception avant publication d’un résultat invalide | catégorie `RUNTIME_INT_OVERFLOW`, `file:line:column` |
| overflow `byte` (hors `[0,255]`) | erreur statique si constant, sinon exception runtime | assignation/conversion doit échouer | catégorie `STATIC_BYTE_RANGE` ou `RUNTIME_BYTE_RANGE`, position |
| division entière `a / b` avec `b == 0` | exception runtime | l’évaluation doit lever une exception | catégorie `RUNTIME_DIVIDE_BY_ZERO`, position |
| reste entier `a % b` avec `b == 0` | exception runtime | l’évaluation doit lever une exception | catégorie `RUNTIME_DIVIDE_BY_ZERO`, position |
| division flottante par zéro | autorisé | comportement IEEE-754 (`+Inf`, `-Inf`, `NaN`) | pas d’exception |
| décalage `<<`/`>>` avec décalage négatif ou `>= largeur(type)` | exception runtime | l’évaluation doit lever une exception | catégorie `RUNTIME_SHIFT_RANGE`, position |
| index hors bornes `list`, `string`, `slice`, `view` | exception runtime | accès lecture/écriture doit lever une exception | catégorie `RUNTIME_INDEX_OOB`, position |
| écriture `string[i] = v` | erreur statique | toute mutation indexée de `string` doit être rejetée | catégorie `IMMUTABLE_INDEX_WRITE`, position |
| écriture `view[i] = v` | erreur statique | toute mutation indexée de `view<T>` doit être rejetée | catégorie `IMMUTABLE_INDEX_WRITE`, position |
| `list.pop()` sur liste vide | erreur statique si prouvée vide, sinon exception runtime | l’appel doit être rejeté statiquement quand prouvable ; sinon lever une exception runtime | catégorie `STATIC_EMPTY_POP` ou `RUNTIME_EMPTY_POP`, position |
| conversion UTF-8 invalide (`list<byte>.toUtf8String()`) | exception runtime | la conversion doit échouer si l’octetage n’est pas UTF-8 valide | catégorie `RUNTIME_INVALID_UTF8`, position |
| sous-chaîne hors bornes (`string.subString`) | exception runtime | l’extraction doit échouer si `start` ou `length` est invalide | catégorie `RUNTIME_INDEX_OOB`, position |
| argument invalide (`string.replaceAll("", ...)`, `string.repeat(count<0)`, `string.padStart/padEnd(..., "")` si padding requis) | exception runtime | l’appel doit échouer sur argument interdit | catégorie `RUNTIME_INVALID_ARGUMENT`, position |
| accès `map[k]` avec clé absente | exception runtime | l’accès doit lever une exception | catégorie `RUNTIME_MISSING_KEY`, position |
| écriture `map[k] = v` avec clé absente | autorisé | l’opération doit insérer une entrée `(k, v)` | pas d’exception |
| mutation structurelle pendant itération (`list`/`map`) | exception runtime | l’itération doit détecter la mutation et lever une exception au plus tard au prochain pas | catégorie `RUNTIME_CONCURRENT_MUTATION`, position |
| opération arithmétique/uniaire sur `glyph` | exception runtime | l’opération doit lever une exception | catégorie `RUNTIME_TYPE_ERROR`, position |

## 14.3 Debug / release

Règles obligatoires :

- le mode debug peut ajouter des vérifications supplémentaires
- le mode release peut supprimer certaines vérifications redondantes prouvées
- debug et release doivent préserver la même sémantique observable
- seule la qualité des diagnostics peut varier (message plus ou moins détaillé)

# 15. Sémantique statique

Cette section définit l’analyse statique normative.

## 15.1 Portée lexicale et résolution des noms

Règles :

- la portée est lexicale et hiérarchique par blocs
- la résolution d’un identifiant local suit l’ordre : bloc courant, blocs englobants, paramètres, champs via `self`, symboles globaux
- un nom doit être déclaré avant usage dans son bloc
- les types et prototypes doivent être résolus sans ambiguïté à la compilation

## 15.2 Shadowing

Règles :

- un identifiant local peut masquer un identifiant d’un bloc englobant
- un paramètre ne peut pas être redéclaré dans le même bloc
- deux déclarations homonymes dans le même bloc sont interdites
- un champ de prototype peut être masqué localement, mais l’accès au champ doit alors être explicite via `self.<field>`

## 15.3 Initialisation implicite et valeurs par défaut

Principe général :

Dans ProtoScript2, toute variable, champ ou valeur allouée est implicitement initialisée au moment de sa création.  
Il n’existe pas d’état non initialisé observable dans le langage.

Par conséquent :

- toute lecture d’une variable est toujours définie ;
- aucune vérification d’assignation préalable n’est requise ;
- l’utilisation d’une variable avant affectation explicite n’est pas une erreur statique.

Ce choix garantit une sémantique simple, déterministe et performante, et facilite l’implémentation des backends, notamment l’émission de code C.

Valeurs par défaut par type :

- `bool` → `false`
- `byte` → `0`
- `int` → `0`
- `float` → `0.0`
- `glyph` → `U+0000` (glyph nul)
- `string` → `""` (chaîne vide)

Ces valeurs s’appliquent uniformément :

- aux variables locales,
- aux champs de prototypes,
- aux objets clonés,
- aux valeurs retournées par allocation implicite.

Priorité d’initialisation des champs de prototype :

- si un champ possède un initialiseur explicite (section 4.3.2), cet initialiseur prévaut
- sinon, la valeur par défaut de cette section s’applique

## 15.4 Compatibilité des types

Règles :

- aucun typage implicite n’est autorisé hors règles explicitement définies
- les opérations binaires exigent des opérandes de types compatibles selon la section 5.11
- les collections sont invariantes sur leurs paramètres (`list<T>`, `map<K,V>`, `slice<T>`, `view<T>`)
- la substitution parent/enfant est autorisée pour les valeurs de prototypes selon les règles de la section 4
- toute conversion doit être explicite via une opération/méthode définie par la spec
- toute écriture indexée sur un type immuable (`string`, `view<T>`) est une erreur statique
- les littéraux vides `[]` et `{}` exigent un typage contextuel explicite

## 15.5 Contraintes statiques de contrôle et d’appel

Règles :

- un `case` ou `default` sans terminaison explicite (`break`, `return`, `throw` ou instruction équivalente quittant le `switch`) est invalide
- un paramètre variadique déclaré `list<T> ident...` peut être lié à une séquence vide
- une affectation ne peut pas être utilisée comme valeur d’expression
- l’affectation chaînée est invalide

Un paramètre variadique est une forme d’argumentation répétée, capturée en `view<T>` et pouvant être vide.

## 15.6 Erreurs statiques normatives

Une implémentation conforme doit classifier ses diagnostics statiques avec des codes stables.

Familles minimales :

- `E1xxx` : erreurs lexicales/syntaxiques **et arité invalide**
- `E2xxx` : erreurs de nommage/résolution
- `E3xxx` : erreurs de typage/compatibilité
- `E4xxx` : erreurs sémantiques statiques
- `E5xxx` : erreurs modules/imports
- `E6xxx` : réservée pour vérifications statiques runtime futures

Codes canoniques minimaux :

- `E1001` : `PARSE_UNEXPECTED_TOKEN`
- `E1002` : `PARSE_UNCLOSED_BLOCK`
- `E1003` : `ARITY_MISMATCH`
- `E2001` : `UNRESOLVED_NAME`
- `E2002` : `IMPORT_PATH_NOT_FOUND`
- `E2003` : `IMPORT_PATH_BAD_EXTENSION`
- `E2004` : `IMPORT_PATH_NO_ROOT_PROTO`
- `E3001` : `TYPE_MISMATCH_ASSIGNMENT`
- `E3003` : `SWITCH_CASE_NO_TERMINATION`
- `E3004` : `IMMUTABLE_INDEX_WRITE`
- `E3005` : `STATIC_EMPTY_POP`
- `E3007` : `INVALID_RETURN`
- `E3120` : `GROUP_NON_SCALAR_TYPE`
- `E3121` : `GROUP_TYPE_MISMATCH`
- `E3122` : `GROUP_MUTATION`
- `E3130` : `CONST_REASSIGNMENT`
- `E3140` : `SEALED_INHERITANCE`
- `E3150` : `INVALID_FIELD_INITIALIZER`
- `E3151` : `CONST_FIELD_MISSING_INITIALIZER`
- `E3200` : `INVALID_VISIBILITY_LOCATION`
- `E3201` : `VISIBILITY_VIOLATION`

Exigences minimales de diagnostic :

- code canonique (ex. `E3007`)
- nom canonique (`ErrorName`)
- position `file:line:column`
- message descriptif
- format strict conforme à 10.2

## 15.7 Compatibilité et non-régression (normatif)

Les modifications introduites par la présente spécification doivent respecter les contraintes suivantes :

1. Aucun comportement existant valide ne doit devenir invalide sauf si explicitement lié à l’introduction de `group`, `const` ou `sealed`.
2. Aucune règle implicite supplémentaire ne doit être ajoutée au système de types.
3. Aucun mécanisme d’inférence automatique ne doit être introduit.
4. `prototype` conserve intégralement sa sémantique actuelle.
5. `sealed` ne modifie pas les règles d’instanciation.
6. `const` n’introduit pas d’immuabilité profonde.
7. `group` n’introduit aucun nouveau type nominal.
8. L’IR existant pour les expressions constantes ne doit pas être modifié sauf pour la substitution explicite des accès `G.Member`.

# 16. Modèle mémoire normatif

Le modèle mémoire décrit ici est abstrait et observable.

- la spec ne fixe pas d’ABI
- la spec fixe les garanties comportementales visibles par le programmeur

## 16.1 Représentations abstraites

Représentations conceptuelles minimales :

- `list<T>` : `(ptr, len, cap)`
- `slice<T>` / `view<T>` : `(ptr, len)`
- `map<K,V>` : table associative avec ordre d’insertion stable
- objet de prototype : bloc mémoire à layout figé, offsets connus statiquement

## 16.2 Ownership et durées de vie

Règles :

- `list<T>` et `map<K,V>` sont possédants
- `slice<T>` et `view<T>` sont non possédants
- toute vue doit référencer un stockage vivant pendant toute sa durée d’utilisation
- une vue ne doit pas survivre au stockage source qu’elle référence

## 16.3 Invalidation des vues et itérateurs internes

Règles :

- toute réallocation de `list<T>` invalide les vues/pointeurs dérivés sur son buffer
- toute mutation structurelle de `map<K,V>` peut invalider les vues/états d’itération dérivés
- l’usage d’une vue invalidée est interdit : erreur statique si prouvable, sinon exception runtime

## 16.4 Aliasing

Règles :

- l’aliasing est autorisé tant qu’il ne viole pas les règles de validité mémoire
- l’implémentation doit préserver l’ordre observable des lectures/écritures défini par le langage
- aucune optimisation backend ne peut supposer l’absence d’aliasing non prouvée

## 16.5 Debug / release

Règles :

- debug peut instrumenter des contrôles supplémentaires (bornes, validité de vues, mutation durant itération)
- release peut retirer des contrôles démontrés redondants
- aucune différence de résultat observable n’est autorisée

# 17. IR normatif

Cette section définit le contrat normatif entre source, frontend IR et backend.

## 17.1 Entités IR

L’IR doit définir au minimum les entités :

- `Module`
- `Function`
- `Block`
- `Instruction`
- `Type`

## 17.2 Typage IR

Règles :

- chaque valeur IR doit avoir un type statique unique
- aucune conversion implicite n’est autorisée
- toute conversion explicite doit être représentée par une instruction IR dédiée

## 17.3 Invariants obligatoires

Une unité IR valide doit respecter :

- CFG valide (blocs terminés par une terminaison explicite)
- définition avant usage sur tous les chemins
- appels résolus statiquement (cible connue à la compilation)
- accès mémoire typés (lecture/écriture cohérentes avec le type adressé)
- absence d’instruction sémantiquement non définie par la spec

## 17.4 Obligations du frontend

Le frontend qui produit l’IR doit :

- rejeter tout programme source invalide selon sections 13 à 16
- matérialiser explicitement les contrôles runtime exigés par la spec
- préserver la correspondance source -> IR pour diagnostics (`file:line:column`)
- ne générer aucun comportement hors spec (pas d’UB caché)

## 17.5 Garanties backend

Un backend conforme peut supposer :

- IR bien typé et conforme aux invariants 17.3
- sémantique explicite de chaque instruction
- absence de dépendance à un RTTI utilisateur

Un backend conforme doit :

- préserver strictement la sémantique observable
- ne pas supprimer un contrôle runtime normatif non prouvé redondant

## 17.6 Correspondance normative source <-> IR

Règles :

- chaque construction source doit avoir une traduction IR définie
- chaque erreur runtime normative doit correspondre à un point IR identifiable
- la chaîne source -> IR -> backend doit conserver les garanties des sections 14, 15 et 16

## 17.7 `group` et substitution constante (normatif)

### 17.7.1 Descripteur nominal (runtime)

Un `group` ne doit générer :

- aucune entité `Type`
- aucune structure mémoire instanciable

Le nom du `group` doit toutefois être résolu comme **descripteur nominal** (valeur runtime) dont la seule vocation est la description et le diagnostic (ex. `Debug.dump(Color)`). Ce descripteur est **partagé** et rattaché à l’IR (propriété du module), sans copie par clone ni par frame.

### 17.7.2 Substitution constante

Pour chaque membre :

```
T group G { A = Expr }
```

le frontend doit :

1. vérifier que `Expr` est une expression constante valide
2. évaluer `Expr` à la compilation si requis par les règles existantes
3. enregistrer `G.A` comme constante typée dans la table des symboles

Toute occurrence de :

```
G.A
```

doit être remplacée dans l’IR par la valeur constante correspondante.

Il ne doit exister aucune instruction IR de type “GroupAccess”.

Toute occurrence de :

```
G
```

doit être abaissée en IR comme **constante de type `group`** (descripteur nominal), sans introduction d’un type nominal ni d’un objet instanciable.

### 17.7.3 Ordre d’évaluation

Les membres d’un `group` ne peuvent pas dépendre d’effets de bord.

Toute expression non constante ou dépendant d’un état runtime doit déclencher une erreur statique.

# 18. Conformité et test suite

La conformité d’une implémentation est définie par cette section et ses annexes normatives.

## 18.1 Structure normative de la suite

La suite de conformité doit contenir au minimum :

- `valid/` : programmes valides avec sortie/résultat attendu
- `invalid/parse` : programmes invalides lexicalement/syntaxiquement
- `invalid/type` : programmes invalides statiquement
- `invalid/runtime` : programmes valides qui déclenchent une exception normative
- `edge/` : cas limites obligatoires
- `invalid/type/switch-no-termination` : `case`/`default` sans terminaison explicite

## 18.2 Format minimal d’un test

Chaque test doit définir :

- le fichier source
- le statut attendu (`accept`, `reject-static`, `reject-runtime`)
- la sortie attendue ou l’exception attendue
- les exigences minimales de diagnostic :
  - catégorie d’erreur
  - position `file:line:column`
  - code d’erreur canonique

## 18.3 Exigence de conformité

Règles :

- une implémentation conforme doit réussir 100 % des tests normatifs
- un échec sur un test normatif rend l’implémentation non conforme
- des tests supplémentaires non normatifs peuvent exister, mais ne remplacent pas les tests normatifs

## 18.4 Stabilité des codes de diagnostic

Règles :

- les codes canoniques normatifs (familles `E*` et `R*`) doivent être stables entre versions mineures de la spec
- le texte du message peut évoluer, pas la signification du code
- un code existant ne doit jamais être réassigné à une autre signification
- les suggestions (`Did you mean ... ?`) n’altèrent pas le code ni la catégorie du diagnostic principal

## 18.5 Convention d’exécution (entrée `main` et codes de retour)

Règles normatives :

- le point d’entrée d’un programme exécutable est la fonction `main`
- seules les signatures suivantes sont autorisées :
  - `function main() : void`
  - `function main() : int`
  - `function main(list<string> args) : void`
  - `function main(list<string> args) : int`
- toute autre signature de `main` est une erreur statique
- si `main` retourne un `int`, cette valeur devient le code de sortie du processus
- si `main` retourne `void`, le code de sortie est déterminé par l’état d’exécution :
  - `0` si l’exécution se termine sans erreur
  - `2` pour une erreur utilisateur (syntaxique, statique, ou runtime)
  - `1` pour une erreur interne (assertion, panne interne, OOM)
- si `main` accepte `list<string> args`, il reçoit **l’argumentation brute du système** (argv), sans filtrage :
  - la liste contient le binaire (`argv[0]`) et tous les arguments suivants
  - aucun argument n’est consommé par l’outil avant transmission au langage

# 19. Versioning et gouvernance de la spécification

Cette spécification définit **ProtoScript Language Specification v2.0**.

Règles normatives :

- la version publiée de cette spécification constitue la référence normative unique pour ProtoScript V2
- la suite de conformité normative (section 18) fait partie intégrante du contrat de conformité
- aucun changement sémantique n’est autorisé sans incrément de version majeure de la spécification
- les modifications éditoriales non sémantiques peuvent être publiées sans changement de version majeure

Déclaration de statut :

> **ProtoScript V2 est un langage spécifié au sens de cette spécification.**

# 20. Modules et extensions natives

Cette section est normative.

Un module étend l’environnement de compilation (symboles disponibles) et ne modifie jamais la sémantique du langage.

## 20.1 Principes normatifs

- un module doit exporter uniquement des symboles statiquement typés (fonctions, constantes)
- un module ne doit pas définir de nouveaux mots-clés, opérateurs, règles de typage ou comportements runtime hors spec
- la résolution des modules et symboles est exclusivement statique (compilation)
- un programme doit être rejeté si un import ne peut pas être résolu de manière unique
- tout symbole importé doit avoir une signature complète connue à la compilation
- le linking des modules natifs est statique
- aucun chargement dynamique, aucune RTTI, aucune réflexion, aucune extension syntaxique

## 20.2 Syntaxe `import` (normative)

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

Règles de désambiguïsation :

- si le token suivant `import` est un `StringLiteral`, alors c’est un `ImportByPath` ;
- sinon, c’est un `ImportByName`.

## 20.2.1 Import par chemin (normatif)

Un `ImportByPath` référence un fichier `.pts` explicite.

Exemples :

```c
import "./datastruct/Stack.pts";
import "/abs/path/collections/Stack.pts".{push, pop};
```

Règles :

- le chemin **doit** se terminer par `.pts`
- chemin relatif : résolu par rapport au **fichier courant** (celui qui effectue l’import)
- chemin absolu : utilisé tel quel
- aucun parcours de `search_paths` n’est effectué pour un import par chemin
- le fichier doit contenir **exactement un** prototype racine public

Exemples :

```c
import std.io as io;
import math.core.{abs, clamp as clip};
```

## 20.3 Visibilité et espace de noms

- `import A.B as X;` introduit un espace de noms local `X`; l’accès se fait via `X.symbol`
- `import A.B.{s1, s2 as y};` introduit uniquement les symboles listés dans la portée locale
- les imports wildcard (`*`) sont interdits
- deux imports produisant le même nom local sans alias explicite sont une erreur statique
- les symboles importés suivent les règles normales de portée et shadowing de la section 15

## 20.4 Résolution à la compilation

- le compilateur résout `(module, symbole)` contre un registre de modules fourni au build
- toute ambiguïté, absence de symbole ou incompatibilité de type est une erreur statique
- aucun fallback runtime n’est autorisé
- les appels importés restent des appels statiques (`call_static` ou équivalent IR)

## 20.5 Utilisation avec `emit-c` (normatif)

- le code C généré par `emit-c` suppose la disponibilité du runtime C et des modules natifs requis
- un import de module (`Io`, `Math`, `JSON`, ou tout module du registre) doit échouer à l’édition de liens ou au chargement si le module n’est pas fourni
- aucun fallback implicite n’est autorisé : le build doit fournir explicitement le registre et les modules natifs

## 20.6 Diagnostics normatifs (imports par chemin)

Les imports par chemin (`import "..."`) doivent produire des diagnostics statiques dédiés :

- `E2002` : `IMPORT_PATH_NOT_FOUND`
- `E2003` : `IMPORT_PATH_BAD_EXTENSION`
- `E2004` : `IMPORT_PATH_NO_ROOT_PROTO`

## 20.7 Contrat backend minimal (API C normative)

```c
typedef enum {
  PS_T_INT, PS_T_FLOAT, PS_T_BOOL, PS_T_BYTE, PS_T_GLYPH, PS_T_STRING, PS_T_VOID
} ps_type_tag;

typedef struct {
  const char* name;
  ps_type_tag ret_type;
  size_t param_count;
  const ps_type_tag* param_types;
  const char* c_symbol;   // symbole C lié statiquement
} ps_native_fn_sig;

typedef struct {
  const char* module_name;      // ex: "std.io"
  size_t fn_count;
  const ps_native_fn_sig* fns;
} ps_native_module_desc;

// API du compilateur/backend (phase compilation)
int ps_register_native_module(const ps_native_module_desc* module);
```

Règles :

- `ps_register_native_module` est appelé avant compilation
- le backend doit émettre des appels vers `c_symbol` connus au linking statique
- aucune découverte dynamique de symboles n’est autorisée

## 20.7 Module standard `Io` (normatif)

Le module `Io` fait partie de l’environnement standard et doit être résolu statiquement via le registre de modules.

Symboles requis (minimum) :

- `Io.openText(path, mode)` : ouvre un fichier texte et retourne un `TextFile`.
- `Io.openBinary(path, mode)` : ouvre un fichier binaire et retourne un `BinaryFile`.
- `Io.print(value)` : écrit `value` sur la sortie standard, sans ajout implicite de fin de ligne.
- `Io.printLine(value)` : équivalent à `Io.print(value)` suivi de l’écriture de `Io.EOL`.
- `Io.EOL` : constante de fin de ligne (`"\n"`).
- `Io.stdin`, `Io.stdout`, `Io.stderr` : flux standards (types `TextFile`).

Prototypes scellés :

- `TextFile` et `BinaryFile` sont des prototypes scellés fournis par le module `Io`.
- Méthodes (noms identiques, types distincts) :
  - `read(size)`, `write(value)`, `tell()`, `seek(pos)`, `size()`, `name()`, `close()`.

Aucune conversion implicite texte ↔ binaire n’est autorisée. Le comportement complet du module `Io` est normatif et défini dans `docs/module_io_specification.md`.

## 20.7 Module standard `Math` (normatif)

Le module `Math` fait partie de l’environnement standard et doit être résolu statiquement via le registre de modules.

Symboles requis (minimum) :

- constantes : `PI`, `E`, `LN2`, `LN10`, `LOG2E`, `LOG10E`, `SQRT1_2`, `SQRT2`
- fonctions : `abs`, `min`, `max`, `floor`, `ceil`, `round`, `trunc`, `sign`, `fround`, `sqrt`, `cbrt`, `pow`,
  `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`,
  `sinh`, `cosh`, `tanh`, `asinh`, `acosh`, `atanh`,
  `exp`, `expm1`, `log`, `log1p`, `log2`, `log10`,
  `hypot`, `clz32`, `imul`, `random`

Règles :

- tous les paramètres et retours sont de type `float`
- la promotion implicite `int → float` est autorisée à l’appel
- les fonctions suivent la sémantique IEEE‑754 (NaN, ±Infinity, −0)
- aucune exception n’est levée pour des valeurs hors domaine : le résultat est `NaN` ou `±Infinity`
- `Math.random()` retourne un `float` uniforme dans **0.0 ≤ x < 1.0**, ne prend aucun argument, avance un PRNG interne, sans allocation ni dépendance système

Le comportement complet du module `Math` est normatif et défini dans `docs/module_math_specification.md`.

Encadré de cohérence :

- les modules étendent l’environnement de noms, pas le langage
- ProtoScript V2 conserve un modèle déterministe : tout est résolu, typé et vérifié à la compilation

## 20.8 Module standard `JSON` (normatif)

Le module `JSON` fait partie de l’environnement standard et doit être résolu statiquement via le registre de modules.

### Prototype standard `JSONValue` (scellé)

`JSONValue` est un prototype standard scellé représentant un type somme JSON fermé.
Il ne peut pas être étendu par l’utilisateur.

Sous‑types normatifs :

- `JsonNull`
- `JsonBool`
- `JsonNumber`
- `JsonString`
- `JsonArray`
- `JsonObject`

Méthodes d’accès (sur `JSONValue`) :

- `isNull()`, `isBool()`, `isNumber()`, `isString()`, `isArray()`, `isObject()` → `bool`
- `asBool()` → `bool`
- `asNumber()` → `float`
- `asString()` → `string`
- `asArray()` → `list<JSONValue>`
- `asObject()` → `map<string, JSONValue>`

`asX()` déclenche une erreur runtime si le type ne correspond pas.

### Constructeurs explicites (immutables)

Le module fournit des constructeurs explicites pour produire des valeurs JSON :

- `JSON.null()` → `JSONValue`
- `JSON.bool(bool b)` → `JSONValue`
- `JSON.number(float x)` → `JSONValue` (promotion implicite `int → float` autorisée)
- `JSON.string(string s)` → `JSONValue`
- `JSON.array(list<JSONValue> items)` → `JSONValue`
- `JSON.object(map<string, JSONValue> members)` → `JSONValue`

### Fonctions

- `JSON.encode(value) -> string`
- `JSON.decode(string text) -> JSONValue`
- `JSON.isValid(string text) -> bool`

Règles :

- `JSON.encode` accepte :
  - un `JSONValue`, ou
  - toute valeur récursivement sérialisable : `bool`, `int`, `float`, `string`, `list<T>`, `map<string,T>` (avec `T` sérialisable).
- `NaN`, `+Infinity`, `-Infinity` sont **interdits** à l’encode : erreur runtime.
- `-0` est préservé lorsqu’il est sérialisé.
- `JSON.decode` parse du JSON strict UTF‑8 et retourne un arbre `JSONValue` ; en cas d’échec, erreur runtime.
- `JSON.isValid` retourne `true/false` sans exception si le texte n’est pas un JSON valide (erreur runtime uniquement si argument non‑string).

Le comportement complet du module `JSON` est normatif et défini dans `docs/module_json_specification.md`.

# Annexe A (normative) — Grammaire lexicale

## A.1 Encodage et caractères

- le texte source doit être en UTF-8 valide
- les fins de ligne `LF` et `CRLF` sont acceptées
- un caractère invalide UTF-8 est une erreur lexicale

## A.2 Espaces et commentaires

- espaces, tabulations et fins de ligne sont des séparateurs
- commentaire ligne : `// ...` jusqu’à la fin de ligne
- commentaire bloc : `/* ... */` non imbriqué

## A.3 Tokens

Catégories :

- identifiants
- mots-clés réservés
- littéraux
- opérateurs
- ponctuation

## A.4 Mots-clés réservés

`prototype`, `sealed`, `function`, `var`, `const`, `internal`, `group`, `int`, `float`, `bool`, `byte`, `glyph`, `string`, `list`, `map`, `slice`, `view`, `void`, `if`, `else`, `for`, `of`, `in`, `while`, `do`, `switch`, `case`, `default`, `break`, `continue`, `return`, `try`, `catch`, `finally`, `throw`, `true`, `false`, `self`, `super`

## A.5 Littéraux

- entier décimal, hexadécimal (`0x`), binaire (`0b`), octal (`0` préfixé)
- flottant décimal/scientifique
- chaîne UTF-8 entre guillemets doubles
- échappements reconnus dans un littéral de chaîne :
  - `\"` → `"`
  - `\\` → `\`
  - `\n` → U+000A
  - `\t` → U+0009
  - `\r` → U+000D
  - `\b` → U+0008
  - `\f` → U+000C
  - `\uXXXX` → point de code Unicode (4 hexadécimaux), encodé en UTF-8
- toute séquence d’échappement invalide est une erreur lexicale
- les littéraux entiers sont non signés lexicalement ; le signe `-` est un opérateur unaire

## A.6 Identifiants

- regex normative : `[A-Za-z_][A-Za-z0-9_]*`
- un mot-clé réservé ne peut pas être utilisé comme identifiant

# Annexe B (normative) — EBNF complète

```ebnf
Program          = { TopDecl } ;

TopDecl          = PrototypeDecl | FunctionDecl | GroupDecl | VarDecl ";" ;

PrototypeDecl    = [ "sealed" ] "prototype" Identifier [ ":" TypeName ] "{" { ProtoMember } "}" ;
ProtoMember      = FieldDecl | MethodDecl ;
Visibility       = "internal" ;
FieldDecl        = [ Visibility ] [ "const" ] Type Identifier [ "=" Expr ] ";" ;
MethodDecl       = [ Visibility ] FunctionDecl ;

GroupDecl        = ScalarType "group" Identifier "{" GroupMemberList "}" ;

FunctionDecl     = "function" Identifier "(" [ ParamList ] ")" ":" Type Block ;
ParamList        = Param { "," Param } ;
Param            = Type Identifier | "list" "<" Type ">" Identifier "..." ;

VarDecl          = "var" Identifier "=" Expr
                 | Type Identifier [ "=" Expr ]
                 | ConstDecl ;

ConstDecl        = "const" ScalarType Identifier [ "=" Expr ] ;

Type             = PrimitiveType
                 | TypeName
                 | "list" "<" Type ">"
                 | "map" "<" Type "," Type ">"
                 | "slice" "<" Type ">"
                 | "view" "<" Type ">" ;

ScalarType       = "bool" | "byte" | "glyph" | "int" | "float" | "string" ;
PrimitiveType    = ScalarType | "void" ;
TypeName         = Identifier ;

Block            = "{" { Stmt } "}" ;

Stmt             = Block
                 | IfStmt
                 | ForStmt
                 | WhileStmt
                 | DoWhileStmt
                 | SwitchStmt
                 | TryStmt
                 | ReturnStmt
                 | BreakStmt
                 | ContinueStmt
                 | ThrowStmt
                 | VarDecl ";"
                 | AssignStmt
                 | ExprStmt ;

IfStmt           = "if" "(" Expr ")" Stmt [ "else" Stmt ] ;
ForStmt          = "for" "(" ( ForClassic | ForOf | ForIn ) ")" Stmt ;
ForClassic       = [ ForInit ] ";" [ Expr ] ";" [ ForStep ] ;
ForInit          = VarDecl | AssignNoValue | Expr ;
ForStep          = AssignNoValue | Expr ;
ForOf            = ( Type Identifier | "var" Identifier ) "of" Expr ;
ForIn            = ( Type Identifier | "var" Identifier ) "in" Expr ;
WhileStmt        = "while" "(" Expr ")" Stmt ;
DoWhileStmt      = "do" Stmt "while" "(" Expr ")" ";" ;

SwitchStmt       = "switch" "(" Expr ")" "{"
                   { "case" Expr ":" { Stmt } }
                   [ "default" ":" { Stmt } ]
                   "}" ;

TryStmt          = "try" Block { CatchClause } [ FinallyClause ] ;
CatchClause      = "catch" "(" Type Identifier ")" Block ;
FinallyClause    = "finally" Block ;

ReturnStmt       = "return" [ Expr ] ";" ;
BreakStmt        = "break" ";" ;
ContinueStmt     = "continue" ";" ;
ThrowStmt        = "throw" Expr ";" ;
AssignStmt       = AssignNoValue ";" ;
ExprStmt         = Expr ";" ;

Expr             = ConditionalExpr ;
AssignNoValue    = PostfixExpr AssignOp ConditionalExpr ;
AssignOp         = "=" | "+=" | "-=" | "*=" | "/=" ;

ConditionalExpr  = OrExpr [ "?" ConditionalExpr ":" ConditionalExpr ] ;
OrExpr           = AndExpr { "||" AndExpr } ;
AndExpr          = EqExpr { "&&" EqExpr } ;
EqExpr           = RelExpr { ( "==" | "!=" ) RelExpr } ;
RelExpr          = ShiftExpr { ( "<" | "<=" | ">" | ">=" ) ShiftExpr } ;
ShiftExpr        = AddExpr { ( "<<" | ">>" ) AddExpr } ;
AddExpr          = MulExpr { ( "+" | "-" | "|" | "^" ) MulExpr } ;
MulExpr          = UnaryExpr { ( "*" | "/" | "%" | "&" ) UnaryExpr } ;
UnaryExpr        = [ "!" | "~" | "-" | "++" | "--" ] PostfixExpr ;
PostfixExpr      = PrimaryExpr { PostfixPart } ;
PostfixPart      = "(" [ ArgList ] ")"
                 | "[" Expr "]"
                 | "." Identifier
                 | "++"
                 | "--" ;
ArgList          = Expr { "," Expr } ;

PrimaryExpr      = Literal
                 | Identifier
                 | "self"
                 | "super"
                 | "(" Expr ")"
                 | ListLiteral
                 | MapLiteral ;

ListLiteral      = "[" [ Expr { "," Expr } ] "]" ;
MapLiteral       = "{" [ MapPair { "," MapPair } ] "}" ;
MapPair          = Expr ":" Expr ;

Literal          = IntLiteral | FloatLiteral | StringLiteral | BoolLiteral ;
BoolLiteral      = "true" | "false" ;

GroupMemberList  = GroupMember { "," GroupMember } [ "," ] ;
GroupMember      = Identifier "=" Expression ;
Expression       = Expr ;
```

# Annexe C (normative) — Précédence, associativité, ordre d’évaluation

## C.1 Table de précédence (du plus fort au plus faible)

| Niveau | Opérateurs | Associativité |
| --- | --- | --- |
| 1 | postfix `()`, `[]`, `.`, `x++`, `x--` | gauche |
| 2 | préfixe `!`, `~`, `-`, `++x`, `--x` | droite |
| 3 | `*`, `/`, `%`, `&` | gauche |
| 4 | `+`, `-`, `|`, `^` | gauche |
| 5 | `<<`, `>>` | gauche |
| 6 | `<`, `<=`, `>`, `>=` | gauche |
| 7 | `==`, `!=` | gauche |
| 8 | `&&` | gauche |
| 9 | `||` | gauche |
| 10 | `?:` | droite |

## C.2 Ordre d’évaluation

Règles obligatoires :

- les opérandes sont évalués de gauche à droite
- les arguments d’appel sont évalués de gauche à droite
- le receveur d’un accès membre/index est évalué avant le membre/index
- pour `cond ? a : b`, la condition est évaluée avant la branche sélectionnée ; seule la branche sélectionnée est évaluée
- pour `a op= b`, `a` est évalué une seule fois
- `&&` et `||` court-circuitent

## C.3 Effets de bord

- les effets de bord doivent être visibles dans l’ordre défini en C.2
- une implémentation ne doit pas réordonner des effets de bord observables

# Annexe D (informative) — Exemples commentés

Cette annexe est informative et ne modifie pas les exigences normatives.

Exemples recommandés :

- exemples de parsing ambigu levé par les règles de la section 13.2
- exemples de diagnostics `E*` et `R*`
- exemples de cas limites de la section 14
- exemples de correspondance source -> IR de la section 17

# Annexe E — État de conformité

Cette spécification décrit le comportement normatif du langage.

L’annexe E ne liste que **ce qui reste à faire** (écarts à la spécification) et les éléments **partiellement** ou **non** implémentés selon les backends.

L’état détaillé est maintenu séparément dans le fichier docs/STATE_OF_CONFORMITY.md.
