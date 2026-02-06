# ProtoScript2 — Module Math (Specification v2)

## Statut

Cette spécification définit le module **Math** pour **ProtoScript2**.

Elle décrit **uniquement** le comportement attendu dans ProtoScript2. Aucune version antérieure, aucun autre langage et aucune spécification externe ne font autorité.

Cette spécification est **normative**.

---

## 1. Principes de conception

- Fonctions **pures** (pas d’état, pas d’effets de bord).
- Types **statiques** : paramètres et retours en `float`.
- Promotion implicite `int → float` autorisée à l’appel.
- Aucune autre conversion implicite.
- Sémantique **IEEE‑754** (NaN, ±Infinity, −0).

---

## 2. Interface du module

```ps
import Math;
```

Le module expose :

- des fonctions globales,
- des constantes (`Math.PI`, `Math.E`, etc.).

---

## 3. Constantes (normatives)

Toutes les constantes sont de type `float`.

| Nom | Valeur |
| --- | --- |
| `Math.PI` | π |
| `Math.E` | e |
| `Math.LN2` | ln(2) |
| `Math.LN10` | ln(10) |
| `Math.LOG2E` | log2(e) |
| `Math.LOG10E` | log10(e) |
| `Math.SQRT1_2` | √(1/2) |
| `Math.SQRT2` | √2 |

La précision est celle du runtime, conforme à `double` IEEE‑754.

---

## 4. Signatures (normatives)

Toutes les fonctions retournent `float`.

| Fonction | Signature |
| --- | --- |
| `abs` | `abs(float x) -> float` |
| `min` | `min(float a, float b) -> float` |
| `max` | `max(float a, float b) -> float` |
| `floor` | `floor(float x) -> float` |
| `ceil` | `ceil(float x) -> float` |
| `round` | `round(float x) -> float` |
| `trunc` | `trunc(float x) -> float` |
| `sign` | `sign(float x) -> float` |
| `fround` | `fround(float x) -> float` |
| `sqrt` | `sqrt(float x) -> float` |
| `cbrt` | `cbrt(float x) -> float` |
| `pow` | `pow(float a, float b) -> float` |
| `sin` | `sin(float x) -> float` |
| `cos` | `cos(float x) -> float` |
| `tan` | `tan(float x) -> float` |
| `asin` | `asin(float x) -> float` |
| `acos` | `acos(float x) -> float` |
| `atan` | `atan(float x) -> float` |
| `atan2` | `atan2(float y, float x) -> float` |
| `sinh` | `sinh(float x) -> float` |
| `cosh` | `cosh(float x) -> float` |
| `tanh` | `tanh(float x) -> float` |
| `asinh` | `asinh(float x) -> float` |
| `acosh` | `acosh(float x) -> float` |
| `atanh` | `atanh(float x) -> float` |
| `exp` | `exp(float x) -> float` |
| `expm1` | `expm1(float x) -> float` |
| `log` | `log(float x) -> float` |
| `log1p` | `log1p(float x) -> float` |
| `log2` | `log2(float x) -> float` |
| `log10` | `log10(float x) -> float` |
| `hypot` | `hypot(float a, float b) -> float` |
| `clz32` | `clz32(float x) -> float` |
| `imul` | `imul(float a, float b) -> float` |
| `random` | `random() -> float` |

Paramètres :

- Les arguments `int` sont acceptés et convertis implicitement en `float`.
- Tout autre type **doit** provoquer une erreur runtime.

---

## 5. Contrat NaN / ±Infinity / −0 (normatif)

Les fonctions `Math` suivent la sémantique IEEE‑754 (double précision) :

- Les valeurs **NaN**, **+Infinity** et **−Infinity** peuvent être produites.
- Aucune exception implicite n’est levée pour des valeurs hors domaine ; les fonctions retournent **NaN** ou **±Infinity** selon IEEE‑754.
- `-0` est **préservé** lorsque le résultat IEEE‑754 est `-0`.
- Les comparaisons avec **NaN** suivent IEEE‑754 : `NaN != NaN` est `true` et toutes les comparaisons ordonnées avec `NaN` sont `false`.

`Math.sign` :

- retourne `NaN` si l’argument est `NaN`,
- retourne `-0` si l’argument est `-0`,
- retourne `-1` si l’argument est négatif,
- retourne `1` si l’argument est positif,
- retourne `0` si l’argument est `+0`.

`Math.random` :

- retourne un `float` dans l’intervalle **[0, 1)**,
- distribution et graine **implémentation‑dépendantes**,
- non cryptographiquement sûr.

---

## 6. Exemples

```ps
import Math;

float a = Math.abs(-3.5);
float b = Math.sqrt(9.0);     // 3.0
float c = Math.log(Math.E);   // 1.0
float d = Math.pow(2.0, 3.0); // 8.0
```

