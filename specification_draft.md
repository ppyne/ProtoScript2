# ProtoScript V2 — Spécification (Draft 0.1)

## 1. Objectifs

ProtoScript V2 est un langage :

- à syntaxe familière (C / JavaScript)
- à sémantique stricte
- object-oriented, prototype-based, sans classes
- typé statiquement par inférence ou déclaration
- conçu pour des performances structurelles
- compilable vers un IR simple, du C, ou du natif

---

## 2. Types primitifs

Types scalaires intégrés :

- bool
- byte (unsigned char)
- int        (entier signé **64 bits**, type entier par défaut du langage)
- float      (flottant double précision, IEEE-754)
- string

### 2.1 Littéraux numériques

#### Entiers (`int`, `byte`)

Les littéraux entiers peuvent être écrits sous les formes suivantes :

- **décimale** : `12`, `0`, `-5`
- **hexadécimale** : `0xFF`, `0xab`
- **octale** : `0644`
- **binaire** : `0b01100110`

Règles :

- le signe (`-`) s’applique au littéral entier
- les littéraux entiers non suffixés sont de type `int`
- l’affectation à `byte` exige que la valeur soit dans l’intervalle valide

#### Flottants (`float`)

Les littéraux flottants supportent les notations suivantes :

- notation décimale : `.2`, `4.`, `-.14`, `-2.5`
- notation scientifique : `1e3`, `-2.5e-4`, `3.14E2`

Règles :

- tout littéral contenant un point décimal ou un exposant est de type `float`
- les opérations sur `float` suivent la sémantique **IEEE-754 double précision**
- les valeurs `NaN`, `+Infinity` et `-Infinity` peuvent être produites
- aucune exception implicite n’est levée lors des opérations arithmétiques
- la détection de ces valeurs est **explicite** via des méthodes dédiées

### 2.2 Absence de nullité universelle

ProtoScript V2 ne définit **aucune valeur ****\`\`**** universelle**.

Règles :

- les types scalaires (`int`, `float`, `bool`, `byte`) ont toujours une valeur valide
- `string` n’est jamais `null` (la chaîne vide `""` représente l’absence de contenu)
- `list<T>` et `map<K,V>` ne sont jamais `null` (une collection vide est valide)
- toute situation d’absence ou d’erreur est gérée explicitement par des **exceptions**

---

## 3. Modules standards et extensibilité

ProtoScript V2 fournit un ensemble de **modules natifs standards**, accessibles globalement, ainsi qu’un mécanisme permettant à des tiers d’ajouter leurs propres modules.

### 3.1 Modules natifs

Les modules suivants sont fournis par défaut par l’environnement ProtoScript V2 :

- **Math** : fonctions mathématiques générales
- **Io** : entrées / sorties standards
- **Fs** : accès au système de fichiers
- **Sys** : informations et services système

Ces modules sont des **objets utilitaires**, non instanciables, à la manière de JavaScript.

Exemples :

```c
float x = Math.sin(a);
float y = Math.sqrt(b);

Io.print("hello
");

Fs.readFile("data.txt");

Sys.exit(0);
```

Règles :

- les modules natifs sont toujours disponibles
- leurs APIs sont **statiques et documentées**
- ils ne peuvent pas être modifiés dynamiquement

---

### 3.2 Modules tiers

ProtoScript V2 permet l’ajout de modules externes fournis par des tiers.

Principes :

- un module expose un **ensemble de fonctions et/ou prototypes**
- un module est lié statiquement au moment du chargement
- un module ne peut pas altérer les types primitifs ni l’API standard

Interface conceptuelle :

```c
module MyModule {
    function foo(int x): int;
}
```

Utilisation :

```c
include "MyModule";

MyModule.foo(42);
```

Règles :

- le mécanisme exact de liaison (native, C, IR) est dépendant de l’implémentation
- les modules tiers respectent les mêmes règles de typage et d’erreurs
- aucun module ne peut introduire de sémantique implicite

---

## 3. Structures de données

### 3.0 Méthodes associées aux valeurs

ProtoScript V2 autorise l’appel de méthodes sur les valeurs scalaires et structurées, sans les considérer comme dynamiquement extensibles.

Ces méthodes font partie de l’**API standard** du langage et sont **résolues statiquement** en fonction du type.

#### 3.0.1 Méthodes par type

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

**int**

```c
i.toByte();
i.toFloat();
i.toString();
i.abs();
i.sign();
```

**float**

```c
f.isNaN();
f.isInfinite();
f.isFinite();

f.toInt();
f.toString();
f.abs();
```

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

string.format("%02i %.2f %x
", 5, .2, 10);
```

**list**

```c
list.length();
list.isEmpty();

list.push(x);
list.pop();

list.join(",");      // si T est string
list.sort();         // si T est comparable
list.contains(x);
```

**map\<K,V>**

```c
map.length();
map.isEmpty();

map.keys();          // list<K>
map.values();        // list<V>
map.containsKey(k);
```

Règles générales :

- les méthodes disponibles dépendent strictement du type
- certaines méthodes sont conditionnelles (ex. `sort()`)
- aucune méthode ne peut être ajoutée dynamiquement
- l’appel de méthode n’implique pas que « tout est objet »

---

## 3. Structures de données

### 3.1 list

```c
list<int> a;
list<int> b = [1, 2, 3];
list<byte> bytes = [0xFF, 0xab, 0x14, 17, 0b00101111, 04];
int x = b[0];
```

Règles :

- le type `T` est obligatoire
- stockage contigu en mémoire
- type invariant après déclaration
- accès indexé via l’opérateur `[]` avec un index de type `int`
- initialisation possible par **littéral de liste** avec `[]`
- les littéraux numériques supportent les bases :
  - décimale
  - hexadécimale (`0x`)
  - binaire (`0b`)
  - octale (`0`)
- l’ordre des éléments est celui du littéral

---

### 3.2 map\<K,V>

```c
map<string, int> values = {"a": 12, "b": 8, "c": 0x45};
int v = values["a"];
```

Règles :

- les types `K` et `V` sont obligatoires
- initialisation possible par **littéral de map** avec `{ key : value }`
- les clés doivent être de type `K`
- les valeurs doivent être de type `V`
- l’ordre d’itération est l’ordre d’insertion des paires dans le littéral
- les clés dupliquées dans un littéral sont interdites (erreur de compilation)
- accès par clé via l’opérateur `[]`

---

### 7.2 Boucles d’itération (`for … of` / `for … in`)

ProtoScript V2 fournit des formes d’itération explicites inspirées de JavaScript, avec une sémantique strictement définie.

#### 7.2.1 `for … of`

```c
for (var elt of elts) {
    // elt est la valeur
}
```

ou avec type explicite :

```c
for (int elt of elts) {
    // elt est la valeur
}
```

Règles :

- `for … of` itère sur les **valeurs**
- la variable d’itération peut être **typée explicitement** ou \*\*inférée avec \*\*\`\`
- utilisable sur :
  - `list<T>` (valeurs de type `T`)
  - `string` (glyphes Unicode de type `char`)
  - `map<K,V>` (valeurs de type `V`)
- l’ordre d’itération est **l’ordre d’insertion**

---

#### 7.2.2 `for … in`

```c
for (int key in elts) {
    // key est la clé ou l’index
}
```

Règles :

- `for … in` itère sur les **clés**
- la variable d’itération peut être **typée explicitement** ou \*\*inférée avec \*\*\`\`
- sémantique selon le type de l’itérable :
  - `list<T>` → index (`int`)
  - `string` → index (`int`, en termes de glyphes)
  - `map<K,V>` → clé de type `K`
- l’ordre d’itération est **l’ordre d’insertion**

---

#### 7.2.2 `for … in`

```c
for (string key in elts) {
    // key est la clé ou l’index
}
```

Règles :

- `for … in` itère sur les **clés**
- le type de la variable est **obligatoire**
- sémantique selon le type de l’itérable :
  - `list<T>` → index (`int`)
  - `string` → index (`int`, en termes de glyphes)
  - `map<K,V>` → clé de type `K`

---

## 8. Sémantique d’exécution

- pas de garbage collection obligatoire
- allocations explicites et visibles
- aucune sémantique JS implicite conservée
- ordre d’évaluation strict

---

## 8.1 Objectifs performance non négociables

ProtoScript V2 vise des performances de **première catégorie**, sans dépendre de spéculation dynamique ni de magie runtime.

Les objectifs suivants sont considérés comme **non négociables** :

- aucune allocation implicite dans les boucles critiques
- aucune conversion de type implicite
- accès direct en temps constant aux données structurées (`list`, champs d’objets)
- représentation mémoire stable et déterministe
- performances prévisibles, indépendantes du contenu des données
- capacité à traiter efficacement des charges lourdes (image, signal, données textuelles)

Le langage doit permettre d’écrire des algorithmes intensifs sans pénalité structurelle par rapport à du C bien écrit.

---

## 8.2 Représentations mémoire et invariants runtime

Les structures fondamentales du langage respectent les invariants suivants :

- `int` : entier signé 64 bits (i64)
- `float` : flottant double précision IEEE-754 (f64)
- `byte` : entier non signé 8 bits (u8)

### list

- stockage contigu en mémoire
- représentation minimale : `(ptr, length, capacity)`
- éléments monomorphes de type `T`
- aucun boxing

### slice / view

ProtoScript V2 définit des **types de vue non possédants** permettant de référencer des sous-parties de données sans allocation ni copie.

- `slice<T>` est une vue mutable
- `view<T>` est une vue en lecture seule

Représentation mémoire :

- `(ptr, length)`
- aucun ownership
- aucune allocation

Règles :

- une `slice<T>` ou `view<T>` ne prolonge pas la durée de vie des données sous-jacentes
- elles peuvent être créées à partir de `list<T>`, `string` (en `view<char>`), ou de buffers issus de modules (`Fs`, `Io`)
- toute écriture via `slice<T>` modifie les données sous-jacentes
- `view<T>` interdit toute mutation

Ces types sont destinés aux traitements intensifs (image, signal, parsing, data processing).

### map\<K,V>

- table de hachage avec accès O(1) amorti
- conservation stricte de l’ordre d’insertion
- aucune mutation structurelle implicite

### Objets / prototypes

- layout mémoire figé par prototype
- offsets de champs déterministes
- résolution statique des méthodes
- aucun ajout ou suppression dynamique de champs

### string

- stockage principal en UTF-8
- sémantique texte (glyphes Unicode)
- index glyphes construit paresseusement si nécessaire
- itération séquentielle rapide sans allocation

Ces invariants sont garantis par le compilateur et exploités par l’IR et le runtime.

---

## 9. Représentation intermédiaire (IR) et compilation C

Le compilateur ProtoScript V2 produit un IR typé et normalisé servant de base à toutes les stratégies d’exécution.

### 9.1 Objectifs de l’IR

- chaque variable a un type concret
- chaque accès mémoire est explicite
- chaque champ et élément a un offset connu
- chaque appel est résolu statiquement
- aucune sémantique implicite

### 9.2 Compilation vers C

ProtoScript V2 vise explicitement la **génération de code C lisible et optimisable** comme backend principal.

Objectifs :

- produire du C simple, sans dépendance exotique
- permettre l’optimisation par les compilateurs C existants (clang, gcc)
- faciliter l’intégration avec des bibliothèques natives (image, signal, crypto)

Correspondances directes :

- `int` → `int64_t`
- `float` → `double`
- `byte` → `uint8_t`
- `list<T>` → structure C `(T*, len, cap)`
- `slice<T>` / `view<T>` → structure C `(T*, len)`

La compilation vers C est considérée comme une **cible de premier rang**, non comme un simple backend expérimental.

---

## 9. Représentation intermédiaire (IR)

Le compilateur doit produire un IR où :

- chaque variable a un type
- chaque accès a un offset
- chaque appel est résolu
- chaque boucle est explicite

L’IR est la forme canonique du programme.

---

## 10. Gestion des erreurs et diagnostics

ProtoScript V2 impose des diagnostics d’erreur **clairs, précis et exploitables** dès les premières phases de développement.

### 10.1 Messages d’erreur

Les messages d’erreur doivent :

- inclure systématiquement le **nom du fichier**
- indiquer le **numéro de ligne** et le **numéro de colonne**
- préciser la **nature exacte de l’erreur** (parse, type, runtime)
- mettre en évidence l’élément fautif et ce qui était attendu
- rester courts, directs et non ambigus

Format recommandé :

```
script.pts:4:1 Uncaught TypeError: Call of non-object: obj.thing
script.pts:6:11 Parse error: unexpected token "," expecting ";"
```

Les diagnostics ne doivent jamais masquer une erreur ni tenter de la corriger implicitement.

---

### 10.2 Exceptions

ProtoScript V2 fournit un mécanisme d’exceptions structuré et typé.

```c
try {
    riskyCall();
} catch (Exception e) {
    log(e.message);
} finally {
    cleanup();
}
```

Règles :

- toutes les exceptions levées doivent être des **instances de ****\`\`**** ou de ses dérivées**
- `Exception` est la racine unique de la hiérarchie des erreurs levables
- un `catch (Exception e)` permet de **rattraper toute exception**
- `throw` n’accepte que des objets dérivés de `Exception`

### 10.2.1 Contenu de `Exception`

Une instance de `Exception` contient implicitement les informations suivantes :

- `file` : nom du fichier source
- `line` : numéro de ligne
- `column` : numéro de colonne

Ces informations sont **automatiquement renseignées** lorsque l’exception est levée par le runtime.

Les champs suivants sont optionnels :

- `message` : description textuelle de l’erreur
- `cause` : exception sous-jacente éventuelle

Règles associées :

- lorsque l’exception est levée par le runtime, `message` et `cause` sont renseignés
- lorsque l’exception est levée par l’utilisateur, `message` et `cause` sont laissés à son initiative
- les informations de localisation (`file`, `line`, `column`) sont toujours présentes
- `finally` est toujours exécuté

---

## 11. Non-objectifs explicites

ProtoScript V2 ne vise pas :

- la compatibilité ES
- le navigateur
- la métaprogrammation dynamique
- la réflexion runtime
- le « tout est objet »

---

## 11. Principe directeur

ProtoScript V2 est conçu avec une exigence centrale : **servir simultanément l’humain et la machine**.

La lisibilité humaine n’est pas considérée comme un effet de style, mais comme une contrainte fondamentale du langage. Un programme ProtoScript V2 doit pouvoir être lu, compris et analysé sans ambiguïté, sans connaissance implicite ni règles cachées.

En parallèle, cette lisibilité doit se traduire directement par des **invariants exploitables par la machine** : typage figé, structures fermées, contrôle de flux explicite, modèle objet déterministe. Ce qui est clair pour l’humain doit être directement exploitable par le compilateur ou l’interpréteur.

ProtoScript V2 refuse toute sémantique qui améliorerait artificiellement le confort d’écriture au détriment de la compréhension ou de l’analyse statique.

> Ce qui n’est pas lisible par un humain ne doit pas exister pour la machine.

---

## 12. Positionnement conceptuel (par rapport à Self)

ProtoScript V2 s’inspire du **modèle prototype-based** popularisé par Self, mais s’en distingue volontairement sur des points essentiels.

- ProtoScript V2 reprend l’idée centrale de **l’objet comme prototype clonable**, sans classes ni hiérarchie rigide.
- En revanche, il rejette le **typage dynamique**, la **mutabilité structurelle** et le **message-passing généralisé** qui caractérisent Self.

Self est conçu comme un langage d’exploration, fortement dépendant d’optimisations JIT spéculatives et d’un environnement interactif riche.

ProtoScript V2 est conçu comme un langage de construction :

- typage figé et exploitable statiquement
- structures fermées et lisibles
- sémantique prévisible
- performance obtenue par les invariants du langage, non par la spéculation

On peut ainsi résumer la filiation de la manière suivante :

> ProtoScript V2 reprend l’intuition saine de Self tout en rejetant son coût cognitif et sémantique.

