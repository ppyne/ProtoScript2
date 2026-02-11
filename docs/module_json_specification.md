# Module standard JSON — Spécification normative (ProtoScript V2)

Ce document décrit le module standard `JSON` et le prototype standard `JSONValue`.
Il est **normatif**.

**Position conceptuelle**

Le module JSON n’introduit pas de généricité cachée ; il introduit une exception volontaire, contrôlée et normée pour interagir avec le monde extérieur.

---

## 1. Objectif

Le module `JSON` fournit :

- un parseur JSON strict (`JSON.decode`),
- un encodeur JSON strict (`JSON.encode`),
- un validateur (`JSON.isValid`),
- un type somme standard `JSONValue` pour représenter un JSON arbitraire.

---

## 2. Prototype standard `JSONValue` (scellé)

`JSONValue` est un prototype standard **scellé**.  
Il ne peut pas être étendu par l’utilisateur.

Sous‑types normatifs :

- `JsonNull`
- `JsonBool`   (valeur booléenne)
- `JsonNumber` (valeur float)
- `JsonString` (valeur string)
- `JsonArray`  (liste de JSONValue)
- `JsonObject` (map<string, JSONValue>)

---

## 3. Constructeurs explicites (immutables)

Le module fournit des constructeurs explicites (immutables) :

- `JSON.null() -> JSONValue`
- `JSON.bool(bool b) -> JSONValue`
- `JSON.number(float x) -> JSONValue`
- `JSON.string(string s) -> JSONValue`
- `JSON.array(list<JSONValue> items) -> JSONValue`
- `JSON.object(map<string, JSONValue> members) -> JSONValue`

Règles :

- promotion implicite `int → float` autorisée pour `JSON.number`.
- `JSON.number` rejette `NaN`, `+Infinity`, `-Infinity`.

---

## 4. Méthodes d’accès (sur JSONValue)

Méthodes de test :

- `isNull()`, `isBool()`, `isNumber()`, `isString()`, `isArray()`, `isObject()` → `bool`

Accès typé :

- `asBool() -> bool`
- `asNumber() -> float`
- `asString() -> string`
- `asArray() -> list<JSONValue>`
- `asObject() -> map<string, JSONValue>`

Règle :

- `asX()` lève une erreur runtime si le type ne correspond pas.

---

## 5. Fonctions JSON

### 5.1 `JSON.encode(value) -> string`

Accepte :

- un `JSONValue`, ou
- une valeur récursivement sérialisable :  
  `bool`, `int`, `float`, `string`, `list<T>`, `map<string, T>` (avec `T` sérialisable).

`JSON.encode` est volontairement plus permissif que le cœur du langage et agit comme une frontière de sérialisation vers un format externe normatif.

Contraintes :

- `NaN`, `+Infinity`, `-Infinity` sont **interdits** → erreur runtime.
- `-0` est préservé.

### 5.2 `JSON.decode(text: string) -> JSONValue`

Parse un JSON strict et retourne un arbre `JSONValue`.

Erreurs :

- JSON invalide → erreur runtime.
- UTF‑8 invalide → erreur runtime.

### 5.3 `JSON.isValid(text: string) -> bool`

Retourne `true` si `text` est un JSON valide, `false` sinon.  
Ne lève pas d’exception pour un JSON invalide.  
Si l’argument n’est pas une `string`, erreur runtime.

---

## 6. Conventions numériques

- Les nombres JSON sont représentés par `float`.
- `NaN`, `+Infinity`, `-Infinity` ne sont **jamais** produits par `decode`.
- `encode` refuse ces valeurs.
- `-0` est préservé lors de l’encodage.

---

## 7. UTF‑8

Toutes les chaînes JSON sont en UTF‑8 strict.  
Toute séquence invalide est une erreur runtime.

---

## 8. Exemples

### 8.1 Encode simple

```ps
import JSON;

map<string, int> m = {"a": 1, "b": 2};
string s = JSON.encode(m);
```

### 8.2 Decode + accès typé

```ps
import JSON;

JSONValue v = JSON.decode("{\"x\": [1, true, \"ok\", null]}");
if (v.isObject()) {
    map<string, JSONValue> obj = v.asObject();
    JSONValue arr = obj["x"];
    if (arr.isArray()) {
        list<JSONValue> items = arr.asArray();
        float n = items[0].asNumber();
    }
}
```

