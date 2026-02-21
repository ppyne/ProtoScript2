# Audit structurel des builtins (alignement SPEC)

## Règles normatives appliquées
- Aucun mot-clé nouveau (`readonly` interdit).
- Interface publique modélisable en ProtoScript pur.
- `clone/super/Self` suivent les règles normales du langage.
- Builtins handle natif: `clone()` doit échouer en runtime avec `R1013 RUNTIME_CLONE_NOT_SUPPORTED`.
- Builtins données: clonables via lookup normal.

## Décision d’architecture utilisée dans cet audit
- Handles natifs non clonables: `TextFile`, `BinaryFile`, `Dir`, `Walker`, `RegExp`.
- Données clonables: `ProcessResult`, `ProcessEvent`, `RegExpMatch`, `JSONValue`, `CivilDateTime`, `PathInfo`, `PathEntry`.

---

## 1) ProcessResult
### Déclaration canonique (surface publique)
```c
sealed prototype ProcessResult {
    function exitCode() : int {}
    function events() : list<ProcessEvent> {}
}
```

### Vérification structurelle
- `clone()` : clonable (type données).
- `super.clone()` : applicable en sous-typage, lookup normal.
- `Self` : pas de spécialisation hors règles normatives.
- Bypass lookup : non observé côté API.

### Verdict
- ⚠ Non conforme (surface actuelle expose encore des champs bruts `exitCode`, `events` plutôt qu’un contrat par méthodes).

### Correctif proposé
- Exposer `exitCode()` et `events()` comme API canonique.

---

## 2) ProcessEvent
### Déclaration canonique (surface publique)
```c
sealed prototype ProcessEvent {
    function stream() : int {}
    function data() : list<byte> {}
}
```

### Vérification structurelle
- `clone()` : clonable (type données).
- `super.clone()` : lookup normal.
- `Self` : conforme au modèle.
- Bypass lookup : non observé.

### Verdict
- ⚠ Non conforme (surface actuelle expose des champs bruts `stream`, `data`).

### Correctif proposé
- Exposer `stream()` / `data()` en API publique canonique.

---

## 3) JSONValue
### Déclaration canonique
```c
sealed prototype JSONValue {
    function isNull() : bool {}
    function isBool() : bool {}
    function isNumber() : bool {}
    function isString() : bool {}
    function isArray() : bool {}
    function isObject() : bool {}

    function asBool() : bool {}
    function asNumber() : float {}
    function asString() : string {}
    function asArray() : list<JSONValue> {}
    function asObject() : map<string, JSONValue> {}
}
```

### Vérification structurelle
- `clone()` : clonable (type données).
- `super.clone()` : pas de bypass détecté.
- `Self` : conforme.

### Verdict
- ✔ Conforme (surface déjà par méthodes).

---

## 4) RegExp
### Déclaration canonique
```c
sealed prototype RegExp {
    static function compile(pattern: string, flags: string) : RegExp {}
    function test(input: string, start: int) : bool {}
    function find(input: string, start: int) : RegExpMatch {}
    function findAll(input: string, start: int, max: int) : list<RegExpMatch> {}
    function replaceFirst(input: string, replacement: string, start: int) : string {}
    function replaceAll(input: string, replacement: string, start: int, max: int) : string {}
    function split(input: string, start: int, maxParts: int) : list<string> {}
    function pattern() : string {}
    function flags() : string {}
}
```

### Vérification structurelle
- `clone()` : interdit en runtime (handle natif non clonable) via `R1013`.
- `super.clone()` : doit produire la même erreur `R1013`.
- `Self` : pas de chemin spécial hors règles.

### Verdict
- ✔ Conforme à la décision d’architecture handle non clonable.

---

## 5) RegExpMatch
### Déclaration canonique (surface publique)
```c
sealed prototype RegExpMatch {
    function ok() : bool {}
    function start() : int {}
    function end() : int {}
    function groups() : list<string> {}
}
```

### Vérification structurelle
- `clone()` : clonable (type données).
- `super.clone()` : lookup normal.
- `Self` : conforme.

### Verdict
- ⚠ Partiel (surface actuelle principalement par champs).

### Correctif proposé
- API canonique par getters (`ok()`, `start()`, `end()`, `groups()`).

---

## 6) CivilDateTime
### Déclaration canonique (surface publique)
```c
sealed prototype CivilDateTime {
    function year() : int {}
    function month() : int {}
    function day() : int {}
    function hour() : int {}
    function minute() : int {}
    function second() : int {}
    function millisecond() : int {}
}
```

### Verdict
- ⚠ Partiel (données encore exposées comme champs dans la surface pratique).

---

## 7) TextFile (handle natif)
### Déclaration canonique
```c
sealed prototype TextFile {
    function read(size: int) : string {}
    function write(text: string) : void {}
    function tell() : int {}
    function seek(pos: int) : void {}
    function size() : int {}
    function name() : string {}
    function close() : void {}
}
```

### Vérification structurelle
- `clone()` : doit échouer avec `R1013`.
- `super.clone()` : doit échouer avec `R1013`.
- `Self` : pas de bypass.

### Verdict
- ✔ Conforme (handle non clonable).

---

## 8) BinaryFile (handle natif)
### Déclaration canonique
```c
sealed prototype BinaryFile {
    function read(size: int) : list<byte> {}
    function write(bytes: list<byte>) : void {}
    function tell() : int {}
    function seek(pos: int) : void {}
    function size() : int {}
    function name() : string {}
    function close() : void {}
}
```

### Verdict
- ✔ Conforme (handle non clonable via `R1013`).

---

## 9) PathInfo
### Déclaration canonique (surface publique)
```c
sealed prototype PathInfo {
    function dirname() : string {}
    function basename() : string {}
    function filename() : string {}
    function extension() : string {}
}
```

### Verdict
- ⚠ Partiel (surface actuelle encore majoritairement champ-centric).

---

## 10) PathEntry
### Déclaration canonique (surface publique)
```c
sealed prototype PathEntry {
    function path() : string {}
    function name() : string {}
    function depth() : int {}
    function isDir() : bool {}
    function isFile() : bool {}
    function isSymlink() : bool {}
}
```

### Verdict
- ⚠ Partiel (surface actuelle encore majoritairement champ-centric).

---

## 11) Dir (handle natif)
### Déclaration canonique
```c
sealed prototype Dir {
    function hasNext() : bool {}
    function next() : string {}
    function close() : void {}
    function reset() : void {}
}
```

### Verdict
- ✔ Conforme (handle non clonable via `R1013`).

---

## 12) Walker (handle natif)
### Déclaration canonique
```c
sealed prototype Walker {
    function hasNext() : bool {}
    function next() : PathEntry {}
    function close() : void {}
}
```

### Verdict
- ✔ Conforme (handle non clonable via `R1013`).

---

## Résumé final
- Conformes: `JSONValue`, `RegExp`, `TextFile`, `BinaryFile`, `Dir`, `Walker`.
- Partiels / non conformes de surface (champ -> méthodes): `ProcessResult`, `ProcessEvent`, `RegExpMatch`, `CivilDateTime`, `PathInfo`, `PathEntry`.
- Aucune recommandation d’ajout de mot-clé (`readonly` exclu).
