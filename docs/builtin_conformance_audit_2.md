# Audit builtin conformance (snapshot actuel)

## Résumé exécutif

| Builtin | Family | sealed | clonable | API exposée (3-8 mots) | Verdict | Tests de preuve |
|---|---|---|---|---|---|---|
| CivilDateTime | Data type | N | Y (partiel) | getters/setters date civile | Partiel | `tests/edge/time_utc_roundtrip.pts`, `tests/edge/civildatetime_subtype_runtime.pts` |
| TextFile | Native handle | Y | N (`R1013`) | read/write/seek/tell texte | OK | `tests/edge/handle_clone_textfile_direct.pts`, `tests/edge/handle_clone_textfile_super.pts` |
| BinaryFile | Native handle | Y | N (`R1013`) | read/write/seek/tell binaire | OK | `tests/edge/handle_clone_binaryfile_direct.pts`, `tests/edge/handle_clone_binaryfile_super.pts` |
| PathInfo | Data type | N | Y (partiel) | dirname/basename/filename/extension | Partiel | `tests/edge/clone_pathinfo_uninitialized.pts`, `tests/fs/queries.pts` |
| PathEntry | Data type | N | Y (partiel) | path/name/depth/type entry | Partiel | `tests/edge/clone_pathentry_uninitialized.pts`, `tests/fs/walker.pts` |
| Dir | Native handle | Y | N (`R1013`) | itérateur de répertoire | OK | `tests/edge/handle_clone_dir_direct.pts`, `tests/edge/handle_clone_dir_super.pts` |
| Walker | Native handle | Y | N (`R1013`) | walker récursif de fichiers | OK | `tests/edge/handle_clone_walker_direct.pts`, `tests/edge/handle_clone_walker_super.pts` |
| JSONValue | Data type | N | Y (partiel) | union JSON + as*/is* | Partiel | `tests/edge/clone_jsonvalue_loses_surface.pts`, `tests/edge/json_decode_basic.pts` |
| RegExp | Native handle | Y | N (`R1013`) | compile/find/replace/split regex | OK | `tests/edge/handle_clone_regexp_direct.pts`, `tests/edge/handle_clone_regexp_super.pts` |
| RegExpMatch | Data type | N | Y (partiel) | ok/start/end/groups | Partiel | `tests/edge/clone_regexpmatch_uninitialized.pts`, `tests/regexp/find_groups.pts` |
| ProcessResult | Data type | N | Y (partiel) | exitCode/events process | Partiel | `tests/edge/clone_processresult_uninitialized.pts`, `tests/sys_execute/exit_code.pts` |
| ProcessEvent | Data type | N | Y (partiel) | stream/data événement process | Partiel | `tests/edge/clone_processevent_uninitialized.pts`, `tests/sys_execute/stdout_only.pts` |

## Méthode et preuves

Commandes utilisées pour établir la surface réelle:

- `rg` sur frontend/runtime/tests: signatures, dispatch, clone/super, erreurs.
- `cat modules/registry.json`: fonctions statiques exposées par modules.
- exécution probes runtime `c/ps run` pour vérifier le comportement clone réel sur data builtins.
- ajout de tests dédiés clone data (voir sections E/F).

Règle d’interprétation appliquée:

- priorité aux comportements observables (tests/runtime) sur la documentation textuelle.
- pas de mot-clé inventé.

---

## 1) CivilDateTime

### A) Statut de type

- Family: Data type
- sealed: no
- clonable: yes (mais clone d’instance ne conserve pas l’état; clone statique sert de base à initialiser)
- instanciable: `CivilDateTime.clone()` ou factories `TimeCivil.*`

### B) Prototype canonique côté langage

```c
prototype CivilDateTime {
    function year(): int {}
    function month(): int {}
    function day(): int {}
    function hour(): int {}
    function minute(): int {}
    function second(): int {}
    function millisecond(): int {}

    function setYear(int v): void {}
    function setMonth(int v): void {}
    function setDay(int v): void {}
    function setHour(int v): void {}
    function setMinute(int v): void {}
    function setSecond(int v): void {}
    function setMillisecond(int v): void {}
}
```

### C) Surface exposée

- Méthodes statiques:
- `CivilDateTime.clone(): CivilDateTime` (hérité de `Object.clone`)
- factories via module `TimeCivil`:
- `fromEpochUTC(int): CivilDateTime`
- `toEpochUTC(CivilDateTime): int`
- `fromEpoch(int, string): CivilDateTime`
- `toEpoch(CivilDateTime, string, int): int`
- `isDST(int, string): bool`
- `offsetSeconds(int, string): int`
- `standardOffsetSeconds(string): int`
- `dayOfWeek(int, string): int`
- `dayOfYear(int, string): int`
- `weekOfYearISO(int, string): int`
- `weekYearISO(int, string): int`
- `isLeapYear(int): bool`
- `daysInMonth(int, int): int`
- `parseISO8601(string): int`
- `formatISO8601(int): string`
- Constantes `TimeCivil`: `DST_EARLIER`, `DST_LATER`, `DST_ERROR`.
- Méthodes d’instance: getters/setters listés en B.
- Erreurs runtime possibles:
- exceptions temporelles (`InvalidDateException`, `InvalidTimeZoneException`, `DST*`) si non capturées -> `R1011 UNHANDLED_EXCEPTION`.
- accès champ interne manquant -> `R1010` (`missing builtin field`) sur objets non initialisés.

### D) Encapsulation

- Accès interne direct: non.
- Barrière: pas de champs publics accessibles (`tests/invalid/type/civildatetime_field_assignment_forbidden.pts`).
- Surface observable: getters/setters uniquement.

### E) Clone / super / Self

- `P.clone()`: lookup normal (règle globale validée par `tests/edge/clone_super_initial_divergent.pts` et `tests/edge/override_chain_order_self_specialization.pts`).
- `super.clone()`: résolution normale, `self` propagé sur la chaîne (mêmes tests).
- `Self`: pas de bypass (tests override multi-niveau).
- Preuve builtin:
- `tests/edge/time_dst_paris.pts` et `tests/edge/time_utc_roundtrip.pts` utilisent `CivilDateTime.clone()` puis setters.
- `tests/edge/civildatetime_subtype_runtime.pts` prouve sous-typage runtime.

### F) Verdict

- Partiel.
- Actions:
- clarifier/normaliser la sémantique de clone d’instance (`copy` vs `nouvel objet vide`) pour `CivilDateTime`.
- ajouter test explicite “clone conserve/ignore état” (normatif).

---

## 2) TextFile

### A) Statut de type

- Family: Native handle
- sealed: yes
- clonable: no -> `R1013 RUNTIME_CLONE_NOT_SUPPORTED`
- instanciable: `Io.openText(path, mode)` + constantes `Io.stdin/stdout/stderr`

### B) Prototype canonique côté langage

```c
sealed prototype TextFile {
    function read(int size): string {}
    function write(string text): void {}
    function tell(): int {}
    function seek(int pos): void {}
    function size(): int {}
    function name(): string {}
    function close(): void {}
}
```

### C) Surface exposée

- Méthodes statiques:
- `TextFile.clone(): TextFile` existe syntaxiquement mais échoue à l’exécution avec `R1013`.
- Méthodes d’instance: `read/write/tell/seek/size/name/close`.
- Factories/constantes:
- `Io.openText(string, string): TextFile`
- `Io.stdin`, `Io.stdout`, `Io.stderr` (`TextFile`)
- Erreurs runtime possibles:
- clone: `R1013`.
- opérations I/O invalides: exceptions I/O (si non capturées -> `R1011`).

### D) Encapsulation

- Accès interne direct: non.
- Barrière: handle opaque; pas de champs publics.

### E) Clone / super / Self

- `TextFile.clone()` direct/inherited/super: toujours `R1013`.
- preuves:
- `tests/edge/handle_clone_textfile_direct.pts`
- `tests/edge/handle_clone_textfile_inherited.pts`
- `tests/edge/handle_clone_textfile_super.pts`

### F) Verdict

- Conforme.

---

## 3) BinaryFile

### A) Statut de type

- Family: Native handle
- sealed: yes
- clonable: no -> `R1013`
- instanciable: `Io.openBinary(path, mode)`

### B) Prototype canonique côté langage

```c
sealed prototype BinaryFile {
    function read(int size): list<byte> {}
    function write(list<byte> bytes): void {}
    function tell(): int {}
    function seek(int pos): void {}
    function size(): int {}
    function name(): string {}
    function close(): void {}
}
```

### C) Surface exposée

- Méthodes statiques:
- `BinaryFile.clone(): BinaryFile` existe syntaxiquement mais runtime `R1013`.
- Méthodes d’instance: `read/write/tell/seek/size/name/close`.
- Factory: `Io.openBinary(string, string): BinaryFile`.
- Erreurs runtime:
- clone `R1013`.
- erreurs I/O via exceptions runtime (non capturées -> `R1011`).

### D) Encapsulation

- Accès interne direct: non.
- Barrière: handle opaque.

### E) Clone / super / Self

- preuves:
- `tests/edge/handle_clone_binaryfile_direct.pts`
- `tests/edge/handle_clone_binaryfile_inherited.pts`
- `tests/edge/handle_clone_binaryfile_super.pts`

### F) Verdict

- Conforme.

---

## 4) PathInfo

### A) Statut de type

- Family: Data type
- sealed: no
- clonable: yes (partiel: clone crée un objet non initialisé)
- instanciable: `Fs.pathInfo(path)`; `PathInfo.clone()` existe

### B) Prototype canonique côté langage

```c
prototype PathInfo {
    function dirname(): string {}
    function basename(): string {}
    function filename(): string {}
    function extension(): string {}
}
```

### C) Surface exposée

- Méthodes statiques:
- `PathInfo.clone(): PathInfo`.
- Méthodes d’instance: `dirname/basename/filename/extension`.
- Factory: `Fs.pathInfo(string): PathInfo`.
- Erreurs runtime:
- clone + getter sur clone vide: `R1010 RUNTIME_TYPE_ERROR` (missing builtin field).

### D) Encapsulation

- Accès interne direct: non.
- Surface observable: getters uniquement.

### E) Clone / super / Self

- lookup/super/self global: couvert par tests clone/super multi-niveaux.
- preuve builtin:
- `tests/edge/clone_pathinfo_uninitialized.pts` (clone appelle la voie normale mais objet vide).

### F) Verdict

- Partiel.
- Actions:
- définir sémantique clone de `PathInfo` (copie des champs internes ou interdiction explicite).

---

## 5) PathEntry

### A) Statut de type

- Family: Data type
- sealed: no
- clonable: yes (partiel: clone non initialisé)
- instanciable: obtenu via `Walker.next()`; `PathEntry.clone()` existe

### B) Prototype canonique côté langage

```c
prototype PathEntry {
    function path(): string {}
    function name(): string {}
    function depth(): int {}
    function isDir(): bool {}
    function isFile(): bool {}
    function isSymlink(): bool {}
}
```

### C) Surface exposée

- Méthodes statiques: `PathEntry.clone(): PathEntry`.
- Méthodes d’instance: `path/name/depth/isDir/isFile/isSymlink`.
- Factory implicite: `Walker.next(): PathEntry`.
- Erreurs runtime:
- clone + getter sur clone vide -> `R1010` missing builtin field.

### D) Encapsulation

- Accès interne direct: non.
- Surface observable: getters uniquement.

### E) Clone / super / Self

- preuves:
- `tests/fs/walker.pts` (surface d’instance)
- `tests/edge/clone_pathentry_uninitialized.pts` (clone runtime actuel)

### F) Verdict

- Partiel.
- Actions:
- normaliser clone de `PathEntry` (copie ou interdiction explicite).

---

## 6) Dir

### A) Statut de type

- Family: Native handle
- sealed: yes
- clonable: no -> `R1013`
- instanciable: `Fs.openDir(path)`

### B) Prototype canonique côté langage

```c
sealed prototype Dir {
    function hasNext(): bool {}
    function next(): string {}
    function close(): void {}
    function reset(): void {}
}
```

### C) Surface exposée

- Méthodes statiques: `Dir.clone(): Dir` (runtime `R1013`).
- Méthodes d’instance: `hasNext/next/close/reset`.
- Factory: `Fs.openDir(string): Dir`.
- Erreurs runtime: clone `R1013`; I/O exceptions sinon.

### D) Encapsulation

- Accès interne direct: non.
- Barrière: handle opaque.

### E) Clone / super / Self

- preuves:
- `tests/edge/handle_clone_dir_direct.pts`
- `tests/edge/handle_clone_dir_inherited.pts`
- `tests/edge/handle_clone_dir_super.pts`

### F) Verdict

- Conforme.

---

## 7) Walker

### A) Statut de type

- Family: Native handle
- sealed: yes
- clonable: no -> `R1013`
- instanciable: `Fs.walk(path, maxDepth, followSymlinks)`

### B) Prototype canonique côté langage

```c
sealed prototype Walker {
    function hasNext(): bool {}
    function next(): PathEntry {}
    function close(): void {}
}
```

### C) Surface exposée

- Méthodes statiques: `Walker.clone(): Walker` (runtime `R1013`).
- Méthodes d’instance: `hasNext/next/close`.
- Factory: `Fs.walk(string, int, bool): Walker`.
- Erreurs runtime: clone `R1013`; exceptions FS/IO sinon.

### D) Encapsulation

- Accès interne direct: non.
- Barrière: handle opaque.

### E) Clone / super / Self

- preuves:
- `tests/edge/handle_clone_walker_direct.pts`
- `tests/edge/handle_clone_walker_inherited.pts`
- `tests/edge/handle_clone_walker_super.pts`

### F) Verdict

- Conforme.

---

## 8) JSONValue

### A) Statut de type

- Family: Data type
- sealed: no (côté langage actuel)
- clonable: yes (partiel: clone peut perdre la surface JSONValue)
- instanciable: `JSON.decode`, `JSON.null/bool/number/string/array/object`, `JSONValue.clone()`

### B) Prototype canonique côté langage

```c
prototype JSONValue {
    function isNull(): bool {}
    function isBool(): bool {}
    function isNumber(): bool {}
    function isString(): bool {}
    function isArray(): bool {}
    function isObject(): bool {}
    function asBool(): bool {}
    function asNumber(): float {}
    function asString(): string {}
    function asArray(): list<JSONValue> {}
    function asObject(): map<string, JSONValue> {}
}
```

### C) Surface exposée

- Méthodes statiques:
- `JSONValue.clone(): JSONValue`
- module `JSON`:
- `encode(JSONValue): string`
- `decode(string): JSONValue`
- `isValid(string): bool`
- `null(): JSONValue`
- `bool(bool): JSONValue`
- `number(float): JSONValue`
- `string(string): JSONValue`
- `array(list<JSONValue>): JSONValue`
- `object(map<string,JSONValue>): JSONValue`
- Méthodes d’instance: `is*/as*` listées en B.
- Erreurs runtime:
- JSON invalide: `R1010 RUNTIME_JSON_ERROR`.
- clone actuel: `R1010 RUNTIME_TYPE_ERROR` (`unknown method` sur clone dans test dédié).

### D) Encapsulation

- Accès interne direct: non.
- Barrière: valeur JSON opaque + méthodes `is*/as*`.

### E) Clone / super / Self

- preuves:
- surface JSON: `tests/edge/json_decode_basic.pts`, `tests/edge/json_decode_object_basic.pts`
- clone actuel: `tests/edge/clone_jsonvalue_loses_surface.pts`
- lookup/super/self global: `tests/edge/clone_super_initial_divergent.pts`

### F) Verdict

- Partiel.
- Actions:
- corriger clone pour préserver délégation/surface JSONValue, ou interdire clone explicitement.

---

## 9) RegExp

### A) Statut de type

- Family: Native handle
- sealed: yes
- clonable: no -> `R1013`
- instanciable: `RegExp.compile(pattern, flags)`

### B) Prototype canonique côté langage

```c
sealed prototype RegExp {
    function compile(string pattern, string flags): RegExp {}
    function test(string input, int start): bool {}
    function find(string input, int start): RegExpMatch {}
    function findAll(string input, int start, int max): list<RegExpMatch> {}
    function replaceFirst(string input, string replacement, int start): string {}
    function replaceAll(string input, string replacement, int start, int max): string {}
    function split(string input, int start, int maxParts): list<string> {}
    function pattern(): string {}
    function flags(): string {}
}
```

### C) Surface exposée

- Méthodes statiques:
- `RegExp.clone(): RegExp` (runtime `R1013`)
- `RegExp.compile(string,string): RegExp`
- Méthodes d’instance: `test/find/findAll/replaceFirst/replaceAll/split/pattern/flags`.
- Erreurs runtime:
- clone `R1013`.
- erreurs regex -> `R1010 RUNTIME_MODULE_ERROR` (`RegExpSyntax/RegExpRange/RegExpLimit` encapsulés dans message).

### D) Encapsulation

- Accès interne direct: non.
- Barrière: handle opaque.

### E) Clone / super / Self

- preuves:
- `tests/edge/handle_clone_regexp_direct.pts`
- `tests/edge/handle_clone_regexp_inherited.pts`
- `tests/edge/handle_clone_regexp_super.pts`

### F) Verdict

- Conforme.

---

## 10) RegExpMatch

### A) Statut de type

- Family: Data type
- sealed: no
- clonable: yes (partiel: clone non initialisé)
- instanciable: via `RegExp.find` / `RegExp.findAll`; `RegExpMatch.clone()` existe

### B) Prototype canonique côté langage

```c
prototype RegExpMatch {
    function ok(): bool {}
    function start(): int {}
    function end(): int {}
    function groups(): list<string> {}
}
```

### C) Surface exposée

- Méthodes statiques: `RegExpMatch.clone(): RegExpMatch`.
- Méthodes d’instance: `ok/start/end/groups`.
- Factory implicite: résultats de `RegExp.find`/`findAll`.
- Erreurs runtime:
- clone + accès méthode -> `R1010` missing builtin field.

### D) Encapsulation

- Accès interne direct: non.
- Surface observable: getters (`ok/start/end/groups`).

### E) Clone / super / Self

- preuves:
- `tests/regexp/find_groups.pts`
- `tests/regexp/match_semantics.pts`
- `tests/edge/clone_regexpmatch_uninitialized.pts`

### F) Verdict

- Partiel.
- Actions:
- définir clone RegExpMatch (copie complète) ou blocage explicite.

---

## 11) ProcessResult

### A) Statut de type

- Family: Data type
- sealed: no
- clonable: yes (partiel: clone non initialisé)
- instanciable: `Sys.execute(...)` ; `ProcessResult.clone()` existe

### B) Prototype canonique côté langage

```c
prototype ProcessResult {
    function exitCode(): int {}
    function events(): list<ProcessEvent> {}
}
```

### C) Surface exposée

- Méthodes statiques: `ProcessResult.clone(): ProcessResult`.
- Méthodes d’instance: `exitCode()`, `events()`.
- Factory: `Sys.execute(string, list<string>, list<byte>, bool, bool): ProcessResult`.
- Erreurs runtime:
- clone + getter -> `R1010 RUNTIME_TYPE_ERROR` missing builtin field.

### D) Encapsulation

- Accès interne direct: non.
- Surface observable: `exitCode` et `events` via méthodes.

### E) Clone / super / Self

- preuves:
- `tests/sys_execute/exit_code.pts`
- `tests/sys_execute/stdout_only.pts`
- `tests/edge/clone_processresult_uninitialized.pts`

### F) Verdict

- Partiel.
- Actions:
- normaliser clone de `ProcessResult` (copie snapshot) ou interdiction explicite.

---

## 12) ProcessEvent

### A) Statut de type

- Family: Data type
- sealed: no
- clonable: yes (partiel: clone non initialisé)
- instanciable: via `ProcessResult.events()` ; `ProcessEvent.clone()` existe

### B) Prototype canonique côté langage

```c
prototype ProcessEvent {
    function stream(): int {}
    function data(): list<byte> {}
}
```

### C) Surface exposée

- Méthodes statiques: `ProcessEvent.clone(): ProcessEvent`.
- Méthodes d’instance: `stream()`, `data()`.
- Factory implicite: éléments de `ProcessResult.events()`.
- Erreurs runtime:
- clone + getter -> `R1010 RUNTIME_IO_ERROR` missing builtin field.

### D) Encapsulation

- Accès interne direct: non.
- Surface observable: `stream/data` via méthodes.

### E) Clone / super / Self

- preuves:
- `tests/sys_execute/stdout_only.pts`
- `tests/sys_execute/both_order.pts`
- `tests/edge/clone_processevent_uninitialized.pts`

### F) Verdict

- Partiel.
- Actions:
- normaliser clone de `ProcessEvent` (copie snapshot) ou interdiction explicite.

---

## Notes transverses clone/super/Self

Preuves globales (sémantique lookup/super/Self):

- `tests/edge/clone_super_initial_divergent.pts`
- `tests/edge/override_multilevel_super_nested_self_deep.pts`
- `tests/edge/override_chain_order_self_specialization.pts`

Ces tests valident la règle globale du langage (lookup d’héritage, `super` lié au proto déclarant, propagation de `self`).

Ce qui reste partiel concerne surtout l’initialisation interne de certains builtins data après clone, pas la règle de lookup clone/super/Self elle-même.
