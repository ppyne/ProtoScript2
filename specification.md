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

# 2. Types primitifs

ProtoScript V2 définit un ensemble restreint de **types scalaires primitifs**, dont la sémantique est fixe, explicite et exploitable statiquement.

Types scalaires intégrés :

- `bool`
- `byte` — entier non signé 8 bits (donnée binaire brute)
- `glyph` — scalaire Unicode, représentant un **glyphe**
- `int` — entier signé **64 bits**, type entier par défaut du langage
- `float` — flottant double précision IEEE‑754
- `string` — séquence de glyphes, stockée en UTF‑8

---

## 2.1 Littéraux numériques

### Entiers (`int`, `byte`)

Les littéraux entiers peuvent être écrits sous les formes suivantes :

- **décimale** : `12`, `0`, `-5`
- **hexadécimale** : `0xFF`, `0xab`
- **octale** : `0644`
- **binaire** : `0b01100110`

Règles :

- le signe (`-`) s’applique au littéral entier
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
- les valeurs `NaN`, `+Infinity` et `-Infinity` peuvent être produites
- aucune exception implicite n’est levée lors des opérations arithmétiques
- la détection de ces valeurs est **explicite** via des méthodes dédiées

---

## 2.2 Absence de nullité universelle

ProtoScript V2 ne définit **aucune valeur ****null**** universelle**.

Règles :

- les types scalaires (`int`, `float`, `bool`, `byte`) ont toujours une valeur valide
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
```

Règles :

- les méthodes Unicode utilisent les tables Unicode standards
- `toUpper()` / `toLower()` retournent un `glyph`
- les méthodes sont **pures** (sans effet de bord)
- aucune allocation implicite n’est effectuée
- les méthodes sont résolues statiquement

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

string.format("%02i %.2f %x\n", 5, .2, 10);
```

**list<T>**

```c
list.length();
list.isEmpty();

list.push(x);
list.pop();

list.join(",");      // si T est string
list.sort();         // si T est comparable
list.contains(x);
```

**map<K,V>**

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

---

### Garanties de complexité des opérations `list<T>`

Les garanties suivantes sont normatives et indépendantes de l’implémentation.

| Opération                     | Complexité      | Notes                    |
| ----------------------------- | --------------- | ------------------------ |
| `list.isEmpty()`              | **O(1)**        | test de longueur         |
| `list.length()`               | **O(1)**        | longueur stockée         |
| accès `list[i]`               | **O(1)**        | accès direct mémoire     |
| affectation `list[i] = x`     | **O(1)**        | écriture directe         |
| itération `for (T v of list)` | **O(n)**        | parcours séquentiel      |
| `list.push(x)`                | **O(1)** amorti | réallocation possible    |
| `list.pop()`                  | **O(1)**        | suppression en fin       |
| `list.contains(x)`            | **O(n)**        | comparaison séquentielle |
| `list.sort()`                 | **O(n log n)**  | si `T` est comparable    |
| `list.slice(offset, len)`     | **O(1)**        | création de vue          |

Règles complémentaires :

- aucune opération `list` ne cache une allocation non documentée
- la réallocation ne peut se produire que lors de `push`
- toute opération **O(n)** est proportionnelle au nombre d’éléments
- l’accès indexé est toujours en temps constant

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
- l’ordre d’itération est l’ordre d’insertion
- les clés dupliquées dans un littéral sont interdites
- accès par clé via l’opérateur `[]`

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

ProtoScript V2 ne permet pas, dans les collections : внутренних

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

slice.sub(offset, length);
view.sub(offset, length);
```

Règles :

- aucune méthode ne peut provoquer d’allocation implicite
- aucune méthode ne peut modifier la taille sous-jacente
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

    function move(int dx, int dy) {
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

---

## 4.3 Instanciation et clonage

Les objets sont créés par **clonage d’un prototype**.

```c
Point p = Point.clone();
p.x = 10;
p.y = 20;
```

Règles :

- `clone()` effectue une copie structurale de l’objet
- aucune allocation implicite autre que l’instance clonée
- le type statique de l’objet est celui du prototype

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
    function move(int dx, int dy) {
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

### Accès au parent

ProtoScript V2 **ne définit pas d’appel implicite au parent** (`super`).

Un appel explicite peut être autorisé sous une forme contrôlée (à spécifier), par exemple :

```c
Point.move(self, dx, dy);
```

Ce mécanisme :

- est explicite
- est résolu statiquement
- n’introduit aucun coût dynamique

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

    function move(int dx, int dy) {
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

---

## 4.6 Contraintes et garanties

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
    Sys.print(s);
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

- un type primitif (`int`, `float`, `string`, etc.)
- un type prototype

---

### Règles normatives

- une fonction peut définir **au plus un paramètre variadique**
- le paramètre variadique doit être **le dernier** de la signature
- le paramètre variadique est **obligatoirement typé** sous la forme `list<T>`
- toutes les valeurs passées dans la partie variadique doivent être de type `T`
- aucune hétérogénéité n’est autorisée

Exemples :

```c
sum(1, 2, 3);          // valide
sum(1);                // valide
sum();                 // erreur (aucune valeur variadique fournie)

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
- aucun paramètre n’est optionnel ou omis
- la règle de la section 5.7 reste pleinement valable

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
            Sys.print(s);
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
- le type `T` peut être un type primitif ou un type prototype
- toutes les valeurs passées doivent être du même type `T`
- aucune hétérogénéité n’est autorisée

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
            Sys.print("[LOG] " + s);
    }
}

prototype DebugLogger : Logger {
    function log(list<string> messages...) : void {
        Sys.print("[DEBUG]");
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

Opérateurs supportés : `+`, `-`, `*`, `/`, `%`

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
- pour les types structurés, la comparaison porte sur l’identité de valeur (pas de deep compare implicite)

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
- aucune affectation chaînée n’est autorisée

Opérateurs composés (`+=`, `-=`, `*=`, `/=`) :

- définis uniquement lorsque l’opérateur arithmétique correspondant est valide
- équivalents à une écriture explicite sans effet de bord

---

### 5.11.6 Opérateurs d’accès et d’appel

- accès aux éléments : `[]`
- accès aux membres : `.`
- appel de fonction ou méthode : `()`

Règles :

- l’accès `[]` est défini pour `list`, `map` et `string`
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
}
```

Règles :

- l’expression du `switch` est évaluée une seule fois
- les `case` sont comparés par égalité stricte
- `break` est obligatoire pour éviter la chute implicite
- `default` est optionnel

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

Les messages d’erreur doivent être **simples, précis et contextualisés**.

Format normatif :

```
<file>:<line>:<column> <ErrorType>: <message>
```

Exemples :

```
script.pts:4:1 Uncaught TypeError: Call of non-object: obj.thing
script.pts:6:11 Parse error: unexpected token "," expecting ";"
```

Règles :

- le fichier, la ligne et la colonne sont toujours fournis
- le message désigne explicitement :
  - ce qui est fautif
  - ce qui était attendu

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

Aucune valeur autre qu’une instance dérivée du prototype `Exception` ne peut être levée.

---

## 10.4 Lever une exception (`throw`)

Une exception peut être levée explicitement :

```c
throw Exception("invalid state");
```

Règles :

- seul un objet dérivant de `Exception` peut être levé
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

- `catch` intercepte une exception par type
- `finally` est toujours exécuté
- l’exception est propagée si elle n’est pas interceptée

---

## 10.6 Exceptions et performances

Le modèle d’exception garantit :

- aucun coût en l’absence d’erreur
- aucune allocation implicite sur le chemin nominal
- un coût explicite et localisé lors de la levée d’une exception

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
