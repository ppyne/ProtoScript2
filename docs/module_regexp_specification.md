# Module standard `RegExp` — Spécification&#x20;

> Objectif : fournir un module regex **natif**, **déterministe**, **UTF‑8 strict**, **sans backtracking catastrophique**, avec captures et quantificateurs greedy / non‑greedy, et une API de remplacement permettant la concaténation/réorganisation via références de captures.

---

## 1. Portée

### 1.1 Objectifs

- Moteur regex **linéaire** en temps dans le cas général (style Thompson NFA / DFA hybride) : pas d’explosion exponentielle.
- Indices et longueurs exprimés en **glyphes** (et non en bytes), cohérents avec les règles `string`.
- Chaînes en **UTF‑8 strict**.
- API **claire** et **typée**, compatible avec la discipline des modules standards.
- Fonctionnalités :
  - concaténation et réorganisation de portions de chaînes via remplacement `$1..$99`.
  - captures simples (numérotées), groupes non‑capturants.
  - quantificateurs `* + ? {m,n}` **greedy** et variantes **non‑greedy** `*? +? ?? {m,n}?`.

### 1.2 Non‑objectifs (interdits en V1)

Ces fonctionnalités **ne doivent pas exister** dans cette version :

- **Backreferences dans le motif** : `\1`, `\k<name>`.
- **Lookaround** (lookahead / lookbehind) : `(?=...)`, `(?!...)`, `(?<=...)`, `(?<!...)`.
- Conditions, recursion, atomic groups, possessive quantifiers, etc.

> Note : les “backreferences” autorisées ici concernent **uniquement le remplacement** (ex. `$1`), pas le motif.

---

## 2. Import et surface API

### 2.1 Nom du module

- Nom logique : `RegExp`
- Import recommandé :

```ps
import RegExp;
```

### 2.2 Prototypes exposés

Le module expose deux prototypes :

- `RegExp` : expression compilée.
- `RegExpMatch` : résultat d’une recherche (une occurrence).

---

## 3. Prototype `RegExp`

```c
sealed prototype RegExp {
    function test(string input, int start) : bool {}
    function find(string input, int start) : RegExpMatch {}
    function findAll(string input, int start, int max) : list<RegExpMatch> {}
    function replaceFirst(string input, string replacement, int start) : string {}
    function replaceAll(string input, string replacement, int start, int max) : string {}
    function split(string input, int start, int maxParts) : list<string> {}
    function pattern() : string {}
    function flags() : string {}
}
```

### 3.1 Construction

`RegExp.compile(string pattern, string flags) : RegExp`

- Compile `pattern` avec `flags`.
- `flags` est une chaîne contenant zéro ou plusieurs caractères (ordre indifférent, doublons ignorés).
- Toute erreur de syntaxe doit lever une exception runtime.

Flags supportés :

- `i` : case‑insensitive (sur `glyph` via folding Unicode simple, voir 8.2)
- `m` : multiline (`^` et `$` agissent par ligne)
- `s` : dotall (`.` inclut le saut de ligne)

Flags **non supportés** (erreur) : `u` (le moteur est déjà UTF‑8 strict), `g` (géré par API via `findAll`), `y`, etc.

### 3.2 Méthodes

#### 3.2.1 Règle unifiée des paramètres de limite (normatif)

Cette règle s’applique à tous les paramètres de limite du module :

- `max` dans `findAll` et `replaceAll`
- `maxParts` dans `split`

Sémantique commune :

- `-1` : illimité
- `0` : comportement spécifique à la méthode (défini ci-dessous)
- `> 0` : limite explicite
- `< -1` : erreur runtime `RegExpRange`

#### `test`

`RegExp.test(string input, int start) : bool`

- Retourne `true` si une occurrence existe dans `input` à partir de `start` (en glyphes).
- `start` doit être dans `[0..input.length()]`.

#### `find`

`RegExp.find(string input, int start) : RegExpMatch`

- Retourne le **premier** match à partir de `start`.
- Si aucun match, retourne un `RegExpMatch` avec `ok=false`.

#### `findAll`

`RegExp.findAll(string input, int start, int max) : list<RegExpMatch>`

- Retourne une liste de matches **non chevauchants**, de gauche à droite.
- Le paramètre `max` suit la règle unifiée (3.2.1).
- Cas spécifique `max == 0` : retourne `[]`.

Règle anti‑boucle pour match vide :

- Si un match a une longueur nulle (`end == start`), la recherche suivante reprend à `end + 1` (en glyphes), si possible. Si `end == input.length()`, stop.

#### `replaceFirst`

`RegExp.replaceFirst(string input, string replacement, int start) : string`

- Remplace la première occurrence trouvée à partir de `start`.
- Si aucun match, retourne `input` inchangée.

#### `replaceAll`

`RegExp.replaceAll(string input, string replacement, int start, int max) : string`

- Remplace toutes les occurrences (non chevauchantes) à partir de `start`, limitées à `max` si `max > 0`.
- Le paramètre `max` suit la règle unifiée (3.2.1).
- Cas spécifique `max == 0` : retourne `input`.

#### `split`

`RegExp.split(string input, int start, int maxParts) : list<string>`

- Découpe `input` selon les matches.
- Le paramètre `maxParts` suit la règle unifiée (3.2.1).
- Cas spécifique `maxParts == 0` : `[]`.
- Cas spécifique `maxParts == 1` : `[input.subString(start, input.length()-start)]`.
- Pour `maxParts > 1`, la sortie contient au plus `maxParts` éléments.

Règle match vide : identique à `findAll` (avance d’un glyphe pour éviter boucle).

#### `pattern`

`RegExp.pattern() : string`

- Retourne le motif original.

#### `flags`

`RegExp.flags() : string`

- Retourne une chaîne canonique (ordre fixé) des flags effectifs, ex. `"ims"`.

---

## 4. Prototype `RegExpMatch`

```c
sealed prototype RegExpMatch {
    function ok() : bool {}
    function start() : int {}
    function end() : int {}
    function groups() : list<string> {}
}
```

### 4.1 Méthodes

`RegExpMatch` expose les méthodes suivantes :

- `ok() : bool`
- `start() : int` (index en glyphes dans `input`, 0 si `ok=false`)
- `end() : int` (index en glyphes, 0 si `ok=false`)
- `groups() : list<string>`

### 4.2 Sémantique des groupes

- `groups()[0]` est **toujours** la sous‑chaîne matchée (le match complet) si `ok=true`.
- `groups()[i]` (i>=1) correspond au i‑ème groupe capturant.
- Un groupe optionnel non matché doit produire une chaîne vide `""`.

> Note : cette V1 ne fournit pas les offsets des captures. Seuls les substrings sont exposés (simples, stables, faciles à utiliser). Une V2 pourra ajouter offsets sans casser l’existant.

---

## 5. Syntaxe du motif (V1)

Cette section est normative.

### 5.1 Littéraux et échappements

- Le motif est une `string` ProtoScript2 (donc déjà avec ses propres échappements de chaîne).
- Dans le moteur regex :
  - `\n`, `\r`, `\t`, `\\` échappent les glyphes standards.
  - `\xHH` : byte hex (00–FF) converti en glyphe Unicode U+00HH.
  - `\u{...}` : code point hex (1 à 6 hex), ex `\u{1F600}`.

Si un échappement est invalide (syntaxe ou valeur hors plage Unicode), erreur.

### 5.2 Atomes

Supportés :

- Glyphes littéraux (hors métacaractères) : `a`, `é`, `λ`, etc.
- `.` : tout glyphe, sauf `\n` si flag `s` absent.
- `^` : début (selon `m`)
- `$` : fin (selon `m`)
- Classes :
  - `[abc]`, `[a-z]`, `[\u{0400}-\u{04FF}]`
  - négation : `[^...]`
  - échappements valides à l’intérieur

Raccourcis de classes :

- `\d` digits ASCII `[0-9]`
- `\D` non‑digits ASCII
- `\w` word ASCII `[A-Za-z0-9_]`
- `\W` non‑word ASCII
- `\s` whitespace ASCII `[ \t\r\n\f\v]`
- `\S` non‑whitespace ASCII

### 5.3 Groupes et alternance

- Groupes capturants : `( ... )`
- Groupes non capturants : `(?: ... )`
- Alternance : `A|B|C` (associativité gauche)

### 5.4 Quantificateurs

- `*` : 0..∞
- `+` : 1..∞
- `?` : 0..1
- `{m}` : exactement m
- `{m,}` : au moins m
- `{m,n}` : entre m et n inclus

Greedy par défaut.

Non‑greedy :

- `*?`, `+?`, `??`, `{m}?`, `{m,}?`, `{m,n}?`

Contraintes :

- `m` et `n` sont des entiers décimaux, `0 <= m <= n`.
- Valeurs trop grandes doivent lever une exception runtime (`RegExpLimit`).

### 5.5 Erreurs de syntaxe

Le compilateur regex doit rejeter :

- crochets non fermés, parenthèses non fermées
- quantificateur sans atome précédent
- intervalle invalide `{m,n}`
- classes vides `[]`
- ranges inversés `[z-a]`
- métasyntaxe interdite : `(?=`, `(?!`, `(?<=`, `(?<!`, `\1`, etc.

---

## 6. Remplacement (concat / réorganisation)

Les méthodes `replaceFirst` et `replaceAll` interprètent `replacement` avec une mini‑syntaxe.

### 6.1 Références de groupes

- `$0` : match complet
- `$1`..`$99` : groupes capturants

Si un index de groupe n’existe pas, il est remplacé par `""`.

### 6.2 Échappement

- `$$` → `$` littéral
- Tout autre `$` suivi d’un non‑chiffre est un `$` littéral.

### 6.3 Exemples

```ps
import RegExp;
import Io;

function main() : void {
  RegExp r = RegExp.compile("(\\w+)-(\\w+)", "");
  Io.printLine(r.replaceAll("alpha-beta gamma-delta", "$2:$1", 0, -1));
  // => "beta:alpha delta:gamma"
}
```

---

## 7. Déterminisme et complexité

### 7.1 Déterminisme

- Pour une entrée donnée, un motif donné, et des flags donnés, les résultats doivent être identiques :
  - mêmes matches (positions),
  - mêmes `groups`.

### 7.2 Complexité (engagement)

- Pour les motifs de cette V1 (sans backreferences/lookaround), l’implémentation doit garantir qu’un appel `find` / `findAll` / `replace*` s’exécute en **O(n)** en fonction de la longueur (en glyphes) de la partie analysée, avec un facteur dépendant du motif compilé.

---

## 8. Unicode / UTF‑8

### 8.1 UTF‑8 strict

- Les `string` ProtoScript2 sont UTF‑8 valides.
- Toute construction interne à partir de bytes doit valider l’UTF‑8 strictement.

### 8.2 Case‑insensitive (`i`)

Pour V1, `i` effectue un **folding simple** :

- ASCII : `A-Z` ↔ `a-z`.
- Hors ASCII : l’implémentation peut limiter le folding à un sous‑ensemble déterministe et documenté (ex. Latin‑1 supplement), mais doit rester stable et reproductible.

> Une V2 “Unicode complet” remplacera cela par un casefold complet.

---

## 9. Erreurs runtime

Le module doit lever des exceptions runtime avec un message `string` clair et stable.

Catégories recommandées (dans le message) :

- `RegExpSyntax` : motif invalide
- `RegExpLimit` : limites dépassées (quantificateur trop grand, profondeur de compilation, etc.)
- `RegExpRange` : `start`, `max`, `maxParts` invalides

Règles :

- `start < 0` ou `start > input.length()` → `RegExpRange`
- pour tout paramètre de limite (`max`, `maxParts`) : valeur `< -1` → `RegExpRange`

---

## 10. Exemples (référence)

### 10.1 Greedy vs non‑greedy

```ps
import RegExp;
import Io;

function main() : void {
  string s = "<a>1</a><a>2</a>";

  RegExp g  = RegExp.compile("<a>.*</a>", "s");
  RegExp ng = RegExp.compile("<a>.*?</a>", "s");

  Io.printLine(g.find(s, 0).groups[0]);  // "<a>1</a><a>2</a>"
  Io.printLine(ng.find(s, 0).groups[0]); // "<a>1</a>"
}
```

### 10.2 Captures et réorganisation

```ps
import RegExp;
import Io;

function main() : void {
  RegExp r = RegExp.compile("(\\d+)/(\\d+)/(\\d+)", "");
  Io.printLine(r.replaceAll("Date: 31/12/2026", "$3-$2-$1", 0, -1));
  // => "Date: 2026-12-31"
}
```

---

## 11. Conventions module natif (ABI)

Cette section décrit les exigences d’intégration module standard.

### 11.1 Registry

Le module doit être déclaré dans `modules/registry.json` avec :

- module `RegExp`
- fonctions listées et typées
- méthodes exposées via prototypes selon la convention du runtime (si applicable dans votre implémentation)

### 11.2 Nom du binaire

- `psmod_RegExp.(so|dylib)` (selon convention de chargement)

---

## 12. Notes d’implémentation (non normatives)

- Moteur recommandé : Thompson NFA avec priorité déterministe pour greedy/non‑greedy.
- Les groupes capturants nécessitent un suivi d’offsets internes; V1 ne les expose pas mais les utilise pour extraire les substrings.
- Anti‑boucle match vide : indispensable pour `findAll`, `replaceAll`, `split`.
