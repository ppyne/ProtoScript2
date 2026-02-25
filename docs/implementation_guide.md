# Guide d’implémentation ProtoScript2 (auditable)

ProtoScript2 est un langage d’ingénierie conçu autour de trois principes structurants : déterminisme, auditabilité et séparation explicite des responsabilités entre frontend, représentation intermédiaire (IR) et runtime.

Ce document constitue un guide d’implémentation auditable. Il décrit, de manière progressive et structurée, comment le langage fonctionne de bout en bout : de la lecture du source `.pts` jusqu’à l’exécution dans le runtime JavaScript (oracle) ou dans la machine virtuelle C (implémentation normative).

L’architecture repose sur :

- un **frontend JavaScript** (analyse lexicale, syntaxique et sémantique),

- un **IR intermédiaire** servant de pivot multi-cibles,

- un **runtime JavaScript** jouant le rôle d’oracle rapide,

- une **VM C déterministe** constituant l’implémentation normative,

- une chaîne multi-cibles incluant emit-C et WASM.

Chaque affirmation technique du présent document est reliée à un point d’entrée concret dans le dépôt (fichier et fonction), afin de permettre une vérification directe. Les choix d’implémentation (absence de GC en C, refcount déterministe, parser fail-fast, séparation IR/runtime) sont explicitement justifiés et mis en perspective.

Le texte est structuré pour deux niveaux de lecture :

- **Niveau Licence** : compréhension globale du pipeline et des composants.

- **Niveau Master** : analyse détaillée des invariants, du modèle mémoire, du dispatch VM, des mécanismes d’erreur et des compromis architecturaux.

Enfin, le document précise le périmètre normatif du langage, expose ses limitations structurelles assumées, et identifie plusieurs axes de recherche et d’évolution possibles.

Ce guide ne constitue ni une spécification formelle ni un manuel utilisateur, mais une cartographie technique complète de l’implémentation actuelle de ProtoScript2, destinée aux développeurs, aux contributeurs et aux lecteurs souhaitant auditer ou faire évoluer le système.

## Table des matières

1. Principes architecturaux et justification des choix de conception
2. Objet et périmètre
3. Compilation et exécution de ProtoScript2 (prise en main CLI)
4. Vue d’ensemble de l’architecture
5. Architecture système globale (normative et de référence)
6. Pipeline d’exécution du langage
7. Analyse lexicale et analyse syntaxique
8. Modèle d’arbre syntaxique abstrait (AST)
9. Système de types et règles de sémantique statique
10. Modèle d’exécution
11. Analyse détaillée du runtime JS et de la VM C
12. Bibliothèque standard et modules natifs
13. Architecture multi-cibles et génération de code
14. Stratégie de tests et critères de validation
15. Analyse des performances et de l’impact énergétique
16. Recettes de développement courantes (guide du développeur)
17. Feuille de route et limitations connues (état actuel du dépôt)
18. Limitations structurelles et compromis de conception
19. Périmètre normatif
20. Perspectives de recherche et axes d’évolution

# 1. Principes architecturaux et justification des choix de conception

## 1.1 Positionnement général

ProtoScript2 n’est pas conçu comme un langage expérimental, mais comme un **langage d’ingénierie déterministe**, avec :

- sémantique statique stricte,

- runtime explicite,

- absence de comportements implicites,

- parité multi-cibles vérifiée automatiquement.

L’architecture reflète ces objectifs.

---

## 1.2 Dual runtime : Oracle JS vs VM C normative

ProtoScript2 possède deux exécutions principales :

- **Runtime JS** (`src/runtime.js`)

- **VM C** (`c/runtime/ps_vm.c`)

### Rôle du runtime JS

Le runtime JS :

- sert d’**oracle sémantique rapide**

- facilite le développement

- permet le crosscheck automatique

- accélère l’itération du frontend

Il interprète directement l’AST.

Ref :

- `src/runtime.js:runProgram`

- `bin/protoscriptc`

### Rôle de la VM C

La VM C :

- constitue l’implémentation normative bas niveau

- sert de base à la cible WASM

- expose explicitement la gestion mémoire

- garantit un comportement déterministe sans dépendance au GC hôte

Ref :

- `c/runtime/ps_vm.c:ps_vm_run_main`

- `c/runtime/ps_errors.c:ps_runtime_category`

### Garantie de parité

La parité est validée par :

- `tests/run_node_c_crosscheck.sh`

- `tests/run_runtime_crosscheck.sh`

- `tests/run_runtime_triangle_parity.sh`

Le modèle n’est donc pas “deux implémentations indépendantes”, mais :

> une implémentation de référence + une implémentation normative vérifiée.

---

## 1.3 Pourquoi un IR intermédiaire ?

Le pipeline est :

Source → AST → IR → (Runtime C / emit-C / WASM)

Ref :

- `src/ir.js:buildIR`

- `src/c_backend.js:generateC`

- `c/frontend.c:ps_emit_ir_json`

### Raisons :

1. Découplage frontend / backend

2. Pivot multi-cibles

3. Stabilisation des transformations

4. Validation structurée (`validateSerializedIR`)

5. Possibilité d’optimisations locales (`optimizeIR`)

L’IR agit comme contrat intermédiaire stable.

---

## 1.4 Pourquoi AST interprété en JS mais IR pour C ?

Le runtime JS interprète l’AST directement pour :

- simplicité

- rapidité d’itération

- facilité de debug

La VM C exécute l’IR pour :

- éviter de répliquer toute la sémantique AST en C

- centraliser la sémantique exécutable

- limiter la complexité du frontend C

Cela évite une duplication complète de logique complexe côté C.

---

## 1.5 Absence de Garbage Collector en C

Le runtime C utilise :

- refcount explicite

- libération déterministe

- pas de GC global

Ref :

- `c/runtime/ps_value.c:ps_value_release`

- `docs/RUNTIME_MEMORY_MODEL.md`

### Raisons :

1. Déterminisme fort

2. Contrôle explicite des coûts

3. Absence de pauses GC

4. Compatibilité WASM stricte

5. Auditabilité mémoire

Le coût énergétique est ainsi maîtrisable et prévisible.

---

## 1.6 Parser fail-fast

Le parser JS adopte une stratégie fail-fast :

Ref :

- `src/frontend.js:Parser.eat`

Pas de récupération d’erreur sophistiquée.

### Raisons :

- simplification de la logique

- réduction des chemins d’état

- clarté des diagnostics

- cohérence avec philosophie déterministe

Ce choix privilégie robustesse interne plutôt que confort IDE.

---

## 1.7 Déterminisme comme contrainte centrale

ProtoScript2 impose :

- pas d’interning global implicite

- pas de cache d’adresses

- pas d’effets globaux cachés

- seed RNG contrôlée (`PS_RNG_SEED`)

Ref :

- `docs/RUNTIME_MEMORY_MODEL.md`

- `src/runtime.js:mathRngSeed`

Les tests de déterminisme :

- `tests/run_determinism.sh`

- `tests/run_c_determinism.sh`

Le déterminisme n’est pas une propriété accidentelle, mais une exigence.

---

## 1.8 Modules natifs : registre explicite

Les modules sont déclarés dans :

- `modules/registry.json`

Cela permet :

- surface API stable

- contrôle explicite de ce qui est exposé

- audit clair entre déclaration et implémentation

Ref :

- `modules/registry.json`

- `src/runtime.js:buildModuleEnv`

- `c/runtime/ps_modules.c:ps_module_load`

---

## 1.9 Tests comme mécanisme de spécification

Les tests ne sont pas uniquement validation, mais :

- mécanisme de stabilisation sémantique

- preuve de parité multi-runtime

- garde d’optimisation

Ref :

- `tests/run_conformance.sh`

- `tests/run_node_c_crosscheck.sh`

- `bin/protoscriptc:enforceOptimizationGate`

Les optimisations ne sont activables qu’après validation complète.

---

## 1.10 Résumé des principes

ProtoScript2 repose sur :

- Déterminisme

- Auditabilité

- Séparation claire frontend / IR / runtime

- Multi-cible via pivot IR

- Contrôle mémoire explicite

- Parité vérifiée automatiquement

- Simplicité structurelle priorisée sur sophistication implicite

Ces principes structurent chaque décision d’implémentation visible dans le dépôt.

# 2. Objet et périmètre
Niveau L: Ce document est un guide de visite de l’implémentation de ProtoScript2. Il explique le pipeline complet, les composants (frontend, IR, runtime, modules) et donne des points d’audit précis (fichiers et fonctions). Les références de code sont indiquées sous forme `fichier:fonction`.

Niveau M: La cible est double: compréhension pédagogique et audit d’implémentation. Toute affirmation sur le comportement est reliee a un point d’entrée concret du code (ou a un extrait court). Les définitions normatives (spécification/manuel) sont utilisées uniquement pour aligner le vocabulaire.

# 3. Compilation et exécution de ProtoScript2 (prise en main CLI)
Niveau L: Les deux CLIs principales sont:
- CLI Node (oracle) `bin/protoscriptc` avec `--check`, `--run`, `--emit-ir`, `--emit-c` (réf : `bin/protoscriptc`).
- CLI C `c/ps` avec `run`, `check`, `ast`, `ir` (réf : `c/cli/ps.c:usage`, `c/cli/ps.c:main`).

Niveau M: Le binaire C se compile via `make -C c`, qui produit `c/ps` (runtime C) et `c/pscc` (frontend C / bootstrap) (réf : `c/Makefile`, `README.md`). La version WASM est produite par `make web-clean` puis `make web`, avec un stack size explicite (réf : `Makefile`, `docs/wasm-build.md`).

# 4. Vue d’ensemble de l’architecture
Niveau L: Vue d’ensemble des composants exécutables:
- Frontend JS (lex/parse/analyse) dans `src/frontend.js:Lexer.lex` et `src/frontend.js:Parser.parseProgram`.
- IR + optimisations dans `src/ir.js:buildIR` et `src/optimizer.js:optimizeIR`.
- Backend emit-C dans `src/c_backend.js:generateC`.
- Runtime JS (interpretation) dans `src/runtime.js:runProgram`.
- Frontend C dans `c/frontend.c:lex_file` et `c/frontend.c:parse_file_internal`.
- Runtime C (VM) dans `c/runtime/ps_vm.c:ps_vm_run_main`.
- CLI Node `bin/protoscriptc` orchestre frontend/IR/runtime (réf : `bin/protoscriptc`).
- CLI C `c/ps` orchestre frontend C + VM C (réf : `c/cli/ps.c:main`).
Refs: `src/frontend.js:Lexer.lex`, `src/ir.js:buildIR`, `src/optimizer.js:optimizeIR`, `src/c_backend.js:generateC`, `src/runtime.js:runProgram`, `c/frontend.c:parse_file_internal`, `c/runtime/ps_vm.c:ps_vm_run_main`.

Niveau M: Diagramme ASCII (pipeline principal):

```text
.pts
  |  (JS)  Lexer+Parser+Analyzer
  v
AST ----------------------> Runtime JS (interpretation)
  |                           (runProgram)
  | (IR builder)
  v
IR (in-memory / JSON)
  |           |
  |           +--> emit-C (generateC)
  |                     +--> compile C (hors repo)
  +--> VM C (ps_vm_run_main)
```
Refs: `src/frontend.js:Lexer.lex`, `src/frontend.js:Parser.parseProgram`, `src/frontend.js:Analyzer.analyze`, `src/ir.js:buildIR`, `src/runtime.js:runProgram`, `src/c_backend.js:generateC`, `c/runtime/ps_vm.c:ps_vm_run_main`.

## compiler front-end
Niveau L: Le frontend JS est un parseur descendant récursif + analyseur statique (réf : `src/frontend.js:Lexer`, `src/frontend.js:Parser`, `src/frontend.js:Analyzer`). Le frontend C implémente la même grammaire sous forme de lexer/parser C (réf : `c/frontend.c:lex_file`, `c/frontend.c:parse_file_internal`).

Niveau M: Les points d’entrée frontend sont `check`, `parseOnly`, `parseAndAnalyze` en JS (réf : `src/frontend.js:check`, `src/frontend.js:parseOnly`, `src/frontend.js:parseAndAnalyze`) et `ps_check_file_static`, `ps_parse_file_ast`, `ps_emit_ir_json` en C (réf : `c/frontend.c:ps_check_file_static`, `c/frontend.c:ps_parse_file_ast`, `c/frontend.c:ps_emit_ir_json`).

## runtime(s)
Niveau L: Le runtime JS interprète l’AST directement (réf : `src/runtime.js:runProgram`). Le runtime C exécute un IR charge depuis JSON dans une VM C (réf : `c/runtime/ps_vm.c:ps_vm_run_main`, `c/cli/ps.c:load_ir_from_file`).

Niveau M: Les erreurs runtime JS sont encapsulées par `RuntimeError` et `rdiag` (réf : `src/runtime.js:RuntimeError`, `src/runtime.js:rdiag`). Les erreurs runtime C sont mappées vers des codes `R****` via `ps_runtime_category` (réf : `c/runtime/ps_errors.c:ps_runtime_category`).

## module system
Niveau L: Les modules sont declarés via `modules/registry.json` et importés par le frontend (réf : `modules/registry.json`, `src/frontend.js:loadModuleRegistry`, `src/ir.js:loadModuleRegistry`).

Niveau M: Le runtime JS construit un environnement de modules en mémoire (réf : `src/runtime.js:buildModuleEnv`). Le runtime C charge des dynlibs POSIX ou des modules statiques en mode WASM (réf : `c/runtime/ps_modules.c:ps_module_load`).

## test strategy (goldens, parity, etc.)
Niveau L: Les tests normatifs se lancent via `tests/run_conformance.sh` et sont décrits dans `tests/README.md` (réf : `tests/README.md`).

Niveau M: Les tests incluent des goldens, c’est à dire des fichiers de référence contenant la sortie attendue (ex: `tests/debug/run_debug_tests.sh` et `tests/debug/golden/*`) et des parités Node/C (`tests/run_node_c_crosscheck.sh`, `tests/run_runtime_crosscheck.sh`, `tests/run_runtime_triangle_parity.sh`) (réf : `tests/debug/run_debug_tests.sh`, `tests/run_node_c_crosscheck.sh`, `tests/run_runtime_crosscheck.sh`, `tests/run_runtime_triangle_parity.sh`).

# 5. Architecture système globale (normative et de référence)

ProtoScript2 repose sur une architecture stratifiée avec séparation explicite des rôles :

```text
                SPECIFICATION.md  
                        |  
                +----------------+  
                | Frontend JS    |  
                | (reference)    |  
                +----------------+  
                        |  
               +--------+--------+  
               |                 |  
        Runtime JS          IR builder  
        (oracle)                 |  
                                  v  
                           +-------------+  
                           | IR (JSON)   |  
                           +-------------+  
                                  |  
               +------------------+------------------+  
               |                                     |  
          VM C (normative)                      emit-C backend  
               |                                     |  
             WASM                               C toolchain
```

### Rôles clairement distincts

| Composant   | Rôle                                |
| ----------- | ----------------------------------- |
| Frontend JS | Référence sémantique et validation  |
| Runtime JS  | Oracle rapide pour crosscheck       |
| IR          | Pivot contractuel multi-cibles      |
| VM C        | Implémentation normative bas niveau |
| emit-C      | Génération C portable               |
| WASM        | Cible déployable déterministe       |

# 6. Pipeline d’exécution du langage
Niveau L: Pipeline canonical (Node CLI):
- Lecture du source -> lexing -> parsing -> AST -> analyse statique -> execution AST (réf : `bin/protoscriptc`, `src/frontend.js:parseAndAnalyze`, `src/runtime.js:runProgram`).

Niveau M: Pipeline canonical (CLI C):
- `ps_check_file_static` (analyse statique) -> emission IR JSON -> chargement IR -> VM C (réf : `c/cli/ps.c:static_check_before_run`, `c/cli/ps.c:load_ir_from_file`, `c/frontend.c:ps_emit_ir_json`, `c/runtime/ps_vm.c:ps_vm_run_main`).

## Stage: input source
What it does: Lit un fichier `.pts` (Node) ou un fichier/ligne inline (CLI C `-e`).
Which files implement it: `bin/protoscriptc`, `c/cli/ps.c:main`.
Main entry points: `bin/protoscriptc` (lecture `fs.readFileSync`), `c/cli/ps.c:write_temp_source`.
Key invariants/failure modes: erreur d’entrée en Node -> sortie `error: cannot read file` (réf : `bin/protoscriptc`).

Extrait (lecture fichier Node):

```js
try {
  src = fs.readFileSync(full, "utf8");
} catch (e) {
  console.error(`error: cannot read file '${file}'`);
}
```

Ref: `bin/protoscriptc`.

## Stage: optional preprocessing
What it does: Pretraitement textuel via mcpp (map de lignes).
Which files implement it: `src/frontend.js:psPreprocessSource` (WASM wrapper mcpp), `c/preprocess.c:preprocess_source` (pipeline C).
Main entry points: `src/frontend.js:psPreprocessSource`, `src/frontend.js:psLoadMcppWasm`, `c/preprocess.c:preprocess_source`.
Key invariants/failure modes: `E0003 PREPROCESS_ERROR` si wrapper absent ou API invalide (réf : `src/frontend.js:psLoadMcppWasm`, `src/frontend.js:psPreprocessSource`).

## Stage: lexer
What it does: Transforme le texte en tokens (kw/id/num/str/sym/eof).
Which files implement it: `src/frontend.js:Lexer.lex` (JS), `c/frontend.c:lex_file` (C).
Main entry points: `src/frontend.js:Lexer.lex`, `c/frontend.c:lex_file`.
Key invariants/failure modes: profondeur générique doit revenir a 0 aux frontières d’instruction et en fin de fichier (réf : `src/frontend.js:Lexer.genericDepthAssert`, `src/frontend.js:Lexer.lex`). Erreur `E1001 PARSE_UNEXPECTED_TOKEN` sur caractère invalide (réf : `src/frontend.js:Lexer.lex`).

## Stage: parser
What it does: Construit l’AST par descente récursive.
Which files implement it: `src/frontend.js:Parser.parseProgram`, `c/frontend.c:parse_file_internal`.
Main entry points: `src/frontend.js:Parser.parseProgram`, `c/frontend.c:parse_file_internal`.
Key invariants/failure modes: erreur `E1001 PARSE_UNEXPECTED_TOKEN` sur token inattendu (réf : `src/frontend.js:Parser.eat`). Erreur `E3200 INVALID_VISIBILITY_LOCATION` si `internal` est utilisé au mauvais endroit (réf : `src/frontend.js:Parser.parseProgram`, `src/frontend.js:Parser.parsePrototypeDecl`).

## Stage: AST
What it does: Représentation intermédiaire structurée avec `kind`, `line`, `col`.
Which files implement it: `src/frontend.js:Parser.parseProgram` (AST JS), `c/frontend.c:ast_new` (AST C).
Main entry points: `src/frontend.js:Parser.parsePrimaryExpr` (exemples de nœuds), `c/frontend.c:ast_new`.
Key invariants/failure modes: AST C est une arborescence de `AstNode` et doit être liberée via `ast_free` (réf : `c/frontend.c:ast_new`, `c/frontend.c:ast_free`).

## Stage: type checking / static semantics
What it does: Résolution des noms, types, sous-typage, vérifications de règles (`sealed`, `super`, etc.).
Which files implement it: `src/frontend.js:Analyzer.analyze`.
Main entry points: `src/frontend.js:Analyzer.analyze`, `src/frontend.js:Analyzer.typeOfExpr`, `src/frontend.js:Analyzer.analyzePrototype`.
Key invariants/failure modes: erreurs `E2xxx` (noms) et `E3xxx` (types) émises via `addDiag` (réf : `src/frontend.js:Analyzer.addDiag`). Exemples: `E2001 UNRESOLVED_NAME`, `E3001 TYPE_MISMATCH_ASSIGNMENT`, `E3140 SEALED_INHERITANCE`, `E3210 INVALID_SUPER_USAGE`, `E3221 INVALID_OVERRIDE_RETURN_TYPE` (réf : `src/frontend.js:Analyzer.analyzePrototype`, `src/frontend.js:Analyzer.typeOfExpr`).

## Stage: IR generation
What it does: Abaisse l’AST en IR (graphes de blocs/instructions) et optionnellement sérialise en JSON.
Which files implement it: `src/ir.js:buildIR`, `IR_FORMAT.md`.
Main entry points: `src/ir.js:buildIR`, `src/ir.js:serializeIR`, `src/ir.js:validateSerializedIR`.
Key invariants/failure modes: validation IR JSON via `validateSerializedIR` (réf : `src/ir.js:validateSerializedIR`), et diagnostic `E1001 IR_INVALID` côté CLI (réf : `bin/protoscriptc`).

## Stage: optimization (optional)
What it does: Optimisations locales IR (propagation de constantes, inlining, réduction de checks).
Which files implement it: `src/optimizer.js:optimizeIR`.
Main entry points: `src/optimizer.js:optimizeIR`.
Key invariants/failure modes: exécution conditionnée par le gate d’optimisation (tests de conformance + crosscheck runtime + `BACKEND_C_STABLE=1`) (réf : `bin/protoscriptc:enforceOptimizationGate`).

## Stage: codegen / interpretation
What it does: Exécution directe (runtime JS) ou émission C (emit-C).
Which files implement it: `src/runtime.js:runProgram`, `src/c_backend.js:generateC`.
Main entry points: `src/runtime.js:runProgram`, `src/c_backend.js:generateC`.
Key invariants/failure modes: erreurs runtime `R****` en JS via `rdiag` et `RuntimeError` (réf : `src/runtime.js:rdiag`, `src/runtime.js:RuntimeError`).

## Stage: runtime (C VM)
What it does: Charge l’IR JSON et exécute `main` dans la VM C.
Which files implement it: `c/runtime/ps_vm.c:ps_vm_run_main`, `c/cli/ps.c:run_file`.
Main entry points: `c/runtime/ps_vm.c:ps_vm_run_main`, `c/cli/ps.c:run_file`.
Key invariants/failure modes: mapping d’erreurs C -> codes `R****` via `ps_runtime_category` (réf : `c/runtime/ps_errors.c:ps_runtime_category`).

# 7. Analyse lexicale et analyse syntaxique
Niveau L: Le lexer produit des tokens de types `kw`, `id`, `num`, `str`, `sym`, `eof` (réf : `src/frontend.js:Lexer.add`, `src/frontend.js:Lexer.lex`). Le parser est un descendant récursif et encode la précédence via des fonctions `parseOrExpr`, `parseAndExpr`, `parseEqExpr`, `parseRelExpr`, `parseShiftExpr`, `parseAddExpr`, `parseMulExpr`, `parseUnaryExpr` (réf : `src/frontend.js:Parser.parseOrExpr`, `src/frontend.js:Parser.parseUnaryExpr`).

Niveau M: Stratégie de recovery: fail-fast. Le parser leve `FrontendError` des qu’un token inattendu est rencontré, sans synchronisation (réf : `src/frontend.js:Parser.eat`, `src/frontend.js:Parser.parseProgram`).

Tokens et precedences (ordre croissant):
- `||` (réf : `src/frontend.js:Parser.parseOrExpr`).
- `&&` (réf : `src/frontend.js:Parser.parseAndExpr`).
- `==`/`!=` (réf : `src/frontend.js:Parser.parseEqExpr`).
- `< <= > >=` (réf : `src/frontend.js:Parser.parseRelExpr`).
- `<< >>` (réf : `src/frontend.js:Parser.parseShiftExpr`).
- `+ - | ^` (réf : `src/frontend.js:Parser.parseAddExpr`).
- `* / % &` (réf : `src/frontend.js:Parser.parseMulExpr`).
- unaires `! ~ - ++ --` et cast `(int|float|byte)` (réf : `src/frontend.js:Parser.parseUnaryExpr`).

Exemple d’erreur de parsing (token inattendu):
```js
if (!this.at(type, value)) {
  const tok = this.t();
  throw new FrontendError(
    diag(this.file, tok.line, tok.col, "E1001", "PARSE_UNEXPECTED_TOKEN",
    `unexpected token '${tok.value}', expecting '${expected}'`)
  );
}
```
Ref: `src/frontend.js:Parser.eat`.

Exemple d’erreur "expression attendue":
```js
throw new FrontendError(
  diag(this.file, tok.line, tok.col, "E1001", "PARSE_UNEXPECTED_TOKEN",
  `unexpected token '${tok.value}', expecting 'expression'`)
);
```
Ref: `src/frontend.js:Parser.parsePrimaryExpr`.

# 8. Modèle d’arbre syntaxique abstrait (AST)
Niveau L: En JS, l’AST est une arborescence d’objets avec `kind`, `line`, `col` et des champs spécifiques (ex: `BinaryExpr` a `op/left/right`) (réf : `src/frontend.js:Parser.parsePrimaryExpr`, `src/frontend.js:Parser.parseAddExpr`).

Niveau M: En C, chaque nœud est un `AstNode` avec `kind`, `text`, `line`, `col`, et un tableau `children`. La création/libération est manuelle via `ast_new` / `ast_free` (réf : `c/frontend.c:AstNode`, `c/frontend.c:ast_new`, `c/frontend.c:ast_free`). Les utilitaires de traversal/inspection existent cote JS pour l’IDE (ex: `psCollectLocalsFromNode`, `buildSemanticModel`) (réf : `src/frontend.js:psCollectLocalsFromNode`, `src/frontend.js:buildSemanticModel`).

Ou ajouter un nouveau nœud (étapes minimales):
1. Ajouter la construction dans le parser JS (ex: dans `Parser.parsePrimaryExpr` ou une fonction `parse*` appropriée) (réf : `src/frontend.js:Parser.parsePrimaryExpr`).
2. Propager le nœud dans l’analyseur statique (`Analyzer.typeOfExpr` ou `Analyzer.analyzeStmt`) (réf : `src/frontend.js:Analyzer.typeOfExpr`, `src/frontend.js:Analyzer.analyzeStmt`).
3. Baisser le nœud en IR dans `IRBuilder.lowerExpr` (réf : `src/ir.js:lowerExpr`).
4. Interpréter le nœud dans le runtime JS (`evalExpr` / `execStmt`) (réf : `src/runtime.js:evalExpr`, `src/runtime.js:execStmt`).
5. Si la fonctionnalité doit exister en C, ajouter le support dans le frontend C et/ou VM C (`c/frontend.c:parse_file_internal` et `c/runtime/ps_vm.c:exec_function`) (réf : `c/frontend.c:parse_file_internal`, `c/runtime/ps_vm.c:exec_function`).

# 9. Système de types et règles de sémantique statique
Niveau L: Le type `int` est traite comme un entier 64 bits, avec bornes explicites en JS (réf : `src/frontend.js:INT64_MIN`, `src/frontend.js:INT64_MAX`, `src/runtime.js:INT64_MIN`, `src/runtime.js:INT64_MAX`). Cote C, `int` est représenté par `int64_t` dans `PS_Value` (réf : `c/runtime/ps_value_impl.h:PS_Value`). Les `string` sont manipulées en glyphes Unicode, pas en bytes (réf : `src/runtime.js:glyphsOf`, `src/runtime.js:glyphStringsOf`, `src/runtime.js:glyphAt`).

Niveau M: Les règles d’assignabilité et de sous-typage sont centralisées dans `Analyzer.isSubtype`, `Analyzer.sameSignature`, `Analyzer.hasCovariantReturnType` (réf : `src/frontend.js:Analyzer.isSubtype`, `src/frontend.js:Analyzer.sameSignature`, `src/frontend.js:Analyzer.hasCovariantReturnType`). Les overrides vérifient la visibilité, la signature et la covariance de retour (réf : `src/frontend.js:Analyzer.analyzePrototype`).

Prototype/clone/super et ordre d’init:
- `super` est valide uniquement dans les méthodes de prototypes et vérifié par l’analyseur (`E3210`, `E3211`) (réf : `src/frontend.js:Analyzer.typeOfExpr`).
- La génération de `clone` est synthetisée en IR via `lowerCloneDefaultFunction` et `lowerCloneWrapperFunction` (réf : `src/ir.js:lowerCloneDefaultFunction`, `src/ir.js:lowerCloneWrapperFunction`).
- L’ordre d’initialisation des champs suit la chaîne de prototypes parent->enfant via `collectPrototypeFields` (réf : `src/ir.js:collectPrototypeFields`).
- Le runtime JS exécute `clone` via `objectCloneDefault` et `clonePrototype`, avec des erreurs `R1013` pour handles non clonables (réf : `src/runtime.js:objectCloneDefault`, `src/runtime.js:clonePrototype`).

Ou les erreurs de type sont émises:
- `E3001 TYPE_MISMATCH_ASSIGNMENT` et `E3006 MISSING_TYPE_CONTEXT` dans `Analyzer.analyzeStmt` (réf : `src/frontend.js:Analyzer.analyzeStmt`).
- `E3120 GROUP_NON_SCALAR_TYPE` et `E3121 GROUP_TYPE_MISMATCH` dans `Analyzer.collectGroups` (réf : `src/frontend.js:Analyzer.collectGroups`).

# 10. Modèle d’exécution
Niveau L: Représentation des valeurs runtime C via `PS_Value` et ses variantes (`PS_V_INT`, `PS_V_STRING`, `PS_V_LIST`, etc.) (réf : `c/runtime/ps_value_impl.h:PS_ValueTag`, `c/runtime/ps_value_impl.h:PS_Value`). En JS, les valeurs sont des types natifs JS et des wrappers (ex: `Glyph`, objets avec marqueurs `__object`, `__view`) (réf : `src/runtime.js:Glyph`, `src/runtime.js:isObjectInstance`, `src/runtime.js:isView`).

Niveau M: Mémoire et ownership cote C suivent un refcount sans GC, documenté dans `docs/RUNTIME_MEMORY_MODEL.md` et implémenté par `ps_value_release` / `ps_value_free` (réf : `docs/RUNTIME_MEMORY_MODEL.md`, `c/runtime/ps_value.c:ps_value_release`). Les vues `view<T>` retiennent la source et sont invalidables via versionning de listes (réf : `docs/RUNTIME_MEMORY_MODEL.md`, `c/runtime/ps_vm.c:view_is_valid`, `src/runtime.js:listVersion`, `src/runtime.js:getListVersion`).

Modèle d’erreur:
- Erreurs compile-time: diagnostics `E****` via `FrontendError` et `formatDiagnostic` (réf : `src/frontend.js:FrontendError`, `src/frontend.js:formatDiag`, `src/diagnostics.js:formatDiagnostic`).
- Erreurs runtime JS: `RuntimeError` avec `rdiag` et catégories `R****` (réf : `src/runtime.js:RuntimeError`, `src/runtime.js:rdiag`).
- Erreurs runtime C: mapping `PS_ERR_*` -> `R****` via `ps_runtime_category` (réf : `c/runtime/ps_errors.c:ps_runtime_category`).

Contraintes de déterminisme:
- Aucun cache global d’adresses et pas d’interning global des strings, explicitement documenté (réf : `docs/RUNTIME_MEMORY_MODEL.md`).
- Les tests de déterminisme et de non-divergence font partie du harness (`tests/run_determinism.sh`, `tests/run_c_determinism.sh`) (réf : `tests/run_determinism.sh`, `tests/run_c_determinism.sh`).

# 11. Analyse détaillée du runtime JS et de la VM C

## 11.1 Fonctionnement interne du runtime JS
​
Le runtime JS interprète directement l’AST.
​
Entrée :
​
- `src/runtime.js:runProgram`
​
### Modèle d’exécution
​
- Interprétation récursive
​
- Environnement lexical en objets JS
​
- Pas d’IR intermédiaire
​
Contrairement à la VM C :
​
- pas de dispatch switch
​
- pas de frame explicite C
​
- dépend du GC V8
​
### Pourquoi conserver ce runtime ?
​
1. Oracle rapide
​
2. Outil de développement
​
3. Debug facilité
​
4. Comparaison comportementale
​
Il n’est pas l’implémentation normative finale.
​
## 11.2 Fonctionnement interne de la VM C (niveau M2)

### 11.2.1 Vue d’ensemble de la VM

Point d’entrée principal :

- `c/runtime/ps_vm.c:ps_vm_run_main`

La VM C :

- charge un IR validé

- initialise un état d’exécution (`PS_VM`)

- exécute la fonction `main`

- retourne un code runtime

Schéma simplifié :

IR JSON  
   ↓  
Load → Build VM state → Push main frame → Execute → Exit

---

### 11.2.2 Structure centrale : PS_Value

Définition :

- `c/runtime/ps_value_impl.h:PS_ValueTag`

- `c/runtime/ps_value_impl.h:PS_Value`

Chaque valeur runtime est :

```c
typedef struct {  
    PS_ValueTag tag;  
    union {  
        int64_t i;  
        double f;  
        PS_String* str;  
        PS_List* list;  
        PS_Object* obj;  
        ...  
    } as;  
} PS_Value;
```

Caractéristiques :

- représentation tagged-union

- aucun boxing implicite

- pas de polymorphisme dynamique caché

- coût mémoire explicite

---

### 11.2.3 Gestion mémoire

Implémentation :

- `c/runtime/ps_value.c:ps_value_release`

- `c/runtime/ps_value.c:ps_value_free`

- `docs/RUNTIME_MEMORY_MODEL.md`

Modèle :

- refcount sur objets heap

- pas de GC global

- destruction déterministe

Cycle de vie simplifié :

create → retain → use → release → free (refcount == 0)

Avantages :

- pas de pauses GC

- prédictibilité énergétique

- audit mémoire simple

- compatible WASM

Limitation :

- cycles interdits ou à éviter explicitement

- discipline stricte nécessaire

### 11.2.4 Organisation mémoire de la VM C (schéma explicatif)

#### Vue globale

```text
+--------------------------------------------------+
|                    PROCESS                       |
+--------------------------------------------------+
|                                                  |
|  +------------------+                            |
|  |      PS_VM       |                            |
|  +------------------+                            |
|  | current_frame -->+----+                       |
|  | heap_objects ----+--+ |                       |
|  | ir_program  -----+  | |                       |
|  +------------------+  | |                       |
|                        | |                       |
|                        v v                       |
|                  +----------------+              |
|                  |   Call Frame   |              |
|                  +----------------+              |
|                  | locals[]       |              |
|                  | return_ip      |              |
|                  | prev_frame ----+----+         |
|                  +----------------+    |         |
|                                        |         |
|                  +----------------+    |         |
|                  |   Call Frame   |<---+         |
|                  +----------------+              |
|                                                  |
|  Heap (refcounted objects):                      |
|    - PS_String                                   |
|    - PS_List                                     |
|    - PS_Object                                   |
|    - Module handles                              |
|                                                  |
+--------------------------------------------------+
```

#### Structure PS_Value

Toutes les valeurs manipulées par la VM ont cette forme :

```text
PS_Value
+--------------------+
| tag                |
| union as {         |
|   int64_t i        |
|   double f         |
|   PS_String* str   |
|   PS_List* list    |
|   PS_Object* obj   |
| }                  |
+--------------------+
```

Important :

- Les scalaires (int, float) sont inline

- Les types heap sont des pointeurs refcountés

- Aucune allocation implicite cachée

#### Frame d’appel

À chaque appel :

```text
CallFrame
+----------------------+
| IR_Function* func    |
| size_t ip            |
| PS_Value locals[N]   |
| CallFrame* previous  |
+----------------------+
```

Propriétés :

- empilement strict LIFO

- pas de stack unwinding complexe

- pas d’exception C++

#### Heap refcounté

Objet typique :

```text
PS_List
+----------------------+
| refcount             |
| version              |
| size                 |
| capacity             |
| PS_Value* items      |
+----------------------+
```

Cycle de vie :

```text
create
  ↓
retain (refcount++)
  ↓
release (refcount--)
  ↓
free si refcount == 0
```

Pas de GC global.

#### Vue (view<T>)

Une vue contient :

```text
PS_View
+----------------------+
| refcount             |
| PS_List* source      |
| expected_version     |
| offset/length        |
+----------------------+
```

À chaque accès :

```text
if (source->version != expected_version)
    runtime error
```

→ empêche use-after-mutation.

---

### 11.2.5 Boucle d’exécution (dispatch)

La VM exécute un tableau d’instructions IR.

Entrée :

- `c/runtime/ps_vm.c:exec_function`

Modèle :

```c
while (ip < code_size) {  
    switch (instruction.opcode) {  
        case OP_ADD:  
        case OP_CALL:  
        case OP_RETURN:  
        ...  
    }  
}
```

Caractéristiques :

- dispatch par switch (pas de JIT)

- stack frame explicite

- registres virtuels indexés

C’est une VM simple à pile + registres logiques, volontairement non optimisée JIT.

---

### 11.2.6 Stack d’appels

Chaque appel crée une frame contenant :

- pointeur vers fonction IR

- tableau de slots locaux

- pointeur retour (ip)

- contexte d’exception éventuel

Création lors de :

- `call_function`

- `call_method`

- `call_method_static`

Ref :

- `c/runtime/ps_vm.c:exec_function`

L’empilement est strictement LIFO.

---

### 11.2.7 Appels de méthodes

Deux cas :

- call_function (fonction libre)

- call_method / call_method_static

Dispatch :

- résolution statique depuis IR

- vérification arité

- push frame

La résolution dynamique d’objet se fait via son prototype interne.

---

### 11.2.8 Gestion des erreurs runtime

Erreurs internes C :

- `PS_ERR_*`

Mapping public :

- `c/runtime/ps_errors.c:ps_runtime_category`

Conversion vers codes :

- `R****`

Exemple :

- erreur de division par zéro

- handle invalide

- RegExpRange

Le modèle :

- pas d’exception C++/setjmp globale

- propagation contrôlée via retours d’erreur

- catégorisation explicite

---

### 11.2.9 Vues (`view<T>`)

Les vues conservent :

- référence source

- version attendue

Validation :

- `view_is_valid`

- version incrémentée lors mutation

Ref :

- `c/runtime/ps_vm.c:view_is_valid`

Cela empêche :

- utilisation après mutation

- incohérences silencieuses

---

### 11.2.10 Clone et prototypes

Clone par défaut :

- `lowerCloneDefaultFunction` (IR)

- exécuté en VM comme fonction normale

Ordre d’initialisation :

- parent → enfant

- champs collectés via lowering IR

Ref :

- `src/ir.js:collectPrototypeFields`

La VM n’implémente pas de logique magique :  
elle exécute ce que l’IR encode explicitement.

---

### 11.2.11 Déterminisme VM

Contraintes :

- pas d’adresses exposées

- RNG seedée

- refcount déterministe

- pas de GC non déterministe

Tests :

- `tests/run_c_determinism.sh`

La VM est conçue pour être :

> stable, auditée, reproductible.

# 12. Bibliothèque standard et modules natifs
Source de vérité API: `modules/registry.json` (noms uniquement). Implementation C: modules dynamiques ou statiques; implémentation JS: `buildModuleEnv` (réf : `src/runtime.js:buildModuleEnv`, `c/runtime/ps_modules.c:ps_module_load`).

## Math
Purpose: fonctions mathematiques scalaires et constantes.
Public API surface: `abs`, `min`, `max`, `floor`, `ceil`, `round`, `sqrt`, `pow`, `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sinh`, `cosh`, `tanh`, `asinh`, `acosh`, `atanh`, `exp`, `expm1`, `log`, `log1p`, `log2`, `log10`, `cbrt`, `hypot`, `trunc`, `sign`, `fround`, `clz32`, `imul`, `random`, plus constantes (`PI`, `E`, etc.) (réf : `modules/registry.json`).
Implémentation mapping: JS dans `src/runtime.js:buildModuleEnv` (initialisation de `mathMod` et `mathMod.functions`), C via module natif `tests/modules_src/math.c:ps_module_init` compile par `c/Makefile` (réf : `src/runtime.js:buildModuleEnv`, `tests/modules_src/math.c:ps_module_init`, `c/Makefile`).
Notable edge cases + tests: `Math.random` seedée déterministiquement par `PS_RNG_SEED` et chemins (réf : `src/runtime.js:mathRngSeed`); tests `tests/edge/math_abs_min_max.pts`, `tests/edge/math_random_loop.pts` (réf : `tests/edge/math_abs_min_max.pts`, `tests/edge/math_random_loop.pts`).

## Debug
Purpose: inspection runtime (dump de structures).
Public API surface: `dump` (réf : `modules/registry.json`).
Implémentation mapping: JS via `debug_node.js:dump` (appele depuis `src/runtime.js:buildModuleEnv`), C via module `c/modules/debug.c:ps_module_init_Debug` (réf : `debug_node.js:dump`, `src/runtime.js:buildModuleEnv`, `c/modules/debug.c:ps_module_init_Debug`).
Notable edge cases + tests: goldens dans `tests/debug/golden/*` et runner `tests/debug/run_debug_tests.sh` (réf : `tests/debug/run_debug_tests.sh`, `tests/debug/golden/debug_expected.txt`).

## Io
Purpose: I/O texte et binaire, flux std.
Public API surface: `openText`, `openBinary`, `tempPath`, `print`, `printLine`, constants `EOL`, `stdin`, `stdout`, `stderr` (réf : `modules/registry.json`).
Implémentation mapping: JS dans `src/runtime.js:buildModuleEnv` (construction `ioMod` + classes `TextFile`/`BinaryFile`), C via module natif `tests/modules_src/io.c:ps_module_init` (dynlib) et API fichier dans la VM C (réf : `src/runtime.js:buildModuleEnv`, `src/runtime.js:TextFile`, `src/runtime.js:BinaryFile`, `tests/modules_src/io.c:ps_module_init`, `c/runtime/ps_vm.c:file_size_bytes`).
Notable edge cases + tests: invalid UTF-8 ou operations apres close -> runtime error (réf : `src/runtime.js:decodeUtf8Strict`, `src/runtime.js:writeAllAtomic`); tests `tests/invalid/runtime/io_after_close.pts`, `tests/edge/io_temp_path.pts`, `tests/cli/io_temp_path.pts` (réf : `tests/invalid/runtime/io_after_close.pts`, `tests/edge/io_temp_path.pts`, `tests/cli/io_temp_path.pts`).

## Fs
Purpose: systeme de fichiers (metadata et parcours).
Public API surface: `exists`, `isFile`, `isDir`, `isSymlink`, `isReadable`, `isWritable`, `isExecutable`, `size`, `mkdir`, `rmdir`, `rm`, `cp`, `mv`, `chmod`, `cwd`, `cd`, `pathInfo`, `openDir`, `walk` (réf : `modules/registry.json`).
Implémentation mapping: JS dans `src/runtime.js:buildModuleEnv` (construction `fsMod`), C via module natif `tests/modules_src/fs.c:ps_module_init` et handles `Dir`/`Walker` (réf : `src/runtime.js:buildModuleEnv`, `tests/modules_src/fs.c:ps_module_init`).
Notable edge cases + tests: tests `tests/fs/size.pts`, `tests/edge/handle_clone_dir_instance.pts`, `tests/edge/handle_clone_walker_instance.pts` (réf : `tests/fs/size.pts`, `tests/edge/handle_clone_dir_instance.pts`, `tests/edge/handle_clone_walker_instance.pts`).

## Sys
Purpose: environnement et execution de processus.
Public API surface: `hasEnv`, `env`, `execute` (réf : `modules/registry.json`).
Implémentation mapping: JS dans `src/runtime.js:buildModuleEnv` (construction `sysMod`, fonctions `sysInvalidName` et `sys.execute`), C via `c/modules/sys.c:ps_module_init` et `c/modules/sys_wasm.c:ps_module_init` (réf : `src/runtime.js:buildModuleEnv`, `c/modules/sys.c:ps_module_init`, `c/modules/sys_wasm.c:ps_module_init`).
Notable edge cases + tests: invalid env name / missing env / execute invalid args (réf : `src/runtime.js:sysInvalidName`, `src/runtime.js:makeIoException`); tests `tests/sys/has_env.pts`, `tests/sys/env.pts`, `tests/sys/import_sys.pts` (réf : `tests/sys/has_env.pts`, `tests/sys/env.pts`, `tests/sys/import_sys.pts`).

## JSON
Purpose: encodage/decodage JSON vers `JSONValue`.
Public API surface: `encode`, `decode`, `isValid`, `null`, `bool`, `number`, `string`, `array`, `object` (réf : `modules/registry.json`).
Implémentation mapping: JS dans `src/runtime.js:buildModuleEnv` (construction `jsonMod` + helpers `jsonEncodeValue`, `jsonDecodeValue`), C via module natif `tests/modules_src/json.c:ps_module_init` et runtime JSON `c/runtime/ps_json.c:ps_json_parse` (réf : `src/runtime.js:buildModuleEnv`, `tests/modules_src/json.c:ps_module_init`, `c/runtime/ps_json.c:ps_json_parse`).
Notable edge cases + tests: `JSON.encode` rejette `NaN`/inf (runtime error) (réf : `src/runtime.js:jsonEncodeValue`); tests `tests/invalid/runtime/json_encode_nan.pts`, `tests/edge/json_decode_basic.pts`, `tests/edge/json_isvalid.pts` (réf : `tests/invalid/runtime/json_encode_nan.pts`, `tests/edge/json_decode_basic.pts`, `tests/edge/json_isvalid.pts`).

## Time
Purpose: temps monotonic/epoch et sleep.
Public API surface: `nowEpochMillis`, `nowMonotonicNanos`, `sleepMillis` (réf : `modules/registry.json`).
Implémentation mapping: JS dans `src/runtime.js:buildModuleEnv` (construction `timeMod`), C via `c/modules/time.c:ps_module_init` (réf : `src/runtime.js:buildModuleEnv`, `c/modules/time.c:ps_module_init`).
Notable edge cases + tests: tests `tests/edge/time_utc_roundtrip.pts` (réf : `tests/edge/time_utc_roundtrip.pts`).

## TimeCivil
Purpose: conversions civil/epoch, timezone, DST.
Public API surface: `fromEpochUTC`, `toEpochUTC`, `fromEpoch`, `toEpoch`, `isDST`, `offsetSeconds`, `standardOffsetSeconds`, `dayOfWeek`, `dayOfYear`, `weekOfYearISO`, `weekYearISO`, `isLeapYear`, `daysInMonth`, `parseISO8601`, `formatISO8601`, constants `DST_EARLIER`, `DST_LATER`, `DST_ERROR` (réf : `modules/registry.json`).
Implémentation mapping: JS dans `src/runtime.js:buildModuleEnv` (construction `timeCivilMod`), C via `c/modules/time_civil.c:ps_module_init` (réf : `src/runtime.js:buildModuleEnv`, `c/modules/time_civil.c:ps_module_init`).
Notable edge cases + tests: timezone invalides / DST ambigu (réf : `src/runtime.js:getTimeZoneFormat`); tests `tests/edge/time_dst_paris.pts`, `tests/edge/time_timezone_validation.pts` (réf : `tests/edge/time_dst_paris.pts`, `tests/edge/time_timezone_validation.pts`).

## RegExp
Purpose: regex compile/test/find/replace/split.
Public API surface: `compile`, `test`, `find`, `findAll`, `replaceFirst`, `replaceAll`, `split`, `pattern`, `flags` (réf : `modules/registry.json`).
Implémentation mapping: JS dans `src/runtime.js:buildModuleEnv` (construction `rxMod` + helpers `rxFind`, `rxTranslatePattern`, `rxEnsure`), C via `c/modules/regexp.c:ps_module_init` (réf : `src/runtime.js:buildModuleEnv`, `src/runtime.js:rxFind`, `src/runtime.js:rxTranslatePattern`, `src/runtime.js:rxEnsure`, `c/modules/regexp.c:ps_module_init`).
Notable edge cases + tests: invalid patterns/arguments -> `RegExpRange` / `RegExpSyntax` (réf : `c/modules/regexp.c:rx_range`, `c/modules/regexp.c:rx_syntax`, `src/runtime.js:rxThrow`); tests `tests/regexp/range_replace_max_invalid.pts`, `tests/edge/manual_ex109.pts`, `tests/edge/handle_clone_regexp_direct.pts` (réf : `tests/regexp/range_replace_max_invalid.pts`, `tests/edge/manual_ex109.pts`, `tests/edge/handle_clone_regexp_direct.pts`).

# 13. Architecture multi-cibles et génération de code
Niveau L: Les cibles actives sont:
- Runtime JS (interpretation AST) via `bin/protoscriptc --run` (réf : `bin/protoscriptc`, `src/runtime.js:runProgram`).
- Runtime C (VM) via `c/ps run` (réf : `c/cli/ps.c:main`, `c/runtime/ps_vm.c:ps_vm_run_main`).
- emit-C via `bin/protoscriptc --emit-c` (réf : `bin/protoscriptc`, `src/c_backend.js:generateC`).
- WASM via `make web` (réf : `Makefile`, `docs/wasm-build.md`).

Niveau M: Ce qui est partagé vs spécifique:
- Le frontend JS produit IR et AST; le frontend C produit AST/IR C (réf : `src/frontend.js:parseAndAnalyze`, `c/frontend.c:ps_emit_ir_json`).
- La VM C est normative et sert de base a WASM (réf : `SPECIFICATION.md`, `c/runtime/ps_vm.c:ps_vm_run_main`, `docs/wasm-build.md`).
- Le backend emit-C utilise l’IR JS (`src/ir.js:buildIR` -> `src/c_backend.js:generateC`).
- La parité Node/C est enforcée par des scripts de crosscheck (réf : `tests/run_node_c_crosscheck.sh`, `tests/run_runtime_crosscheck.sh`, `tests/run_runtime_triangle_parity.sh`).

Known pitfalls (avec refs):
- Stack WASM explicite (`WEB_STACK_SIZE := 1048576`) pour éviter `memory access out of bounds` (réf : `Makefile`, `docs/wasm-build.md`).
- Gate d’optimisation obligatoire avant `--opt` (réf : `bin/protoscriptc:enforceOptimizationGate`).

# 14. Stratégie de tests et critères de validation
Niveau L: Les tests se trouvent dans `tests/` avec un runner principal `tests/run_conformance.sh` et un manifest (`tests/manifest.json`) (réf : `tests/README.md`, `tests/manifest.json`).

Niveau M: Types de tests:
- Goldens: `tests/debug/run_debug_tests.sh` + `tests/debug/golden/*` (réf : `tests/debug/run_debug_tests.sh`).
- Parite Node/C: `tests/run_node_c_crosscheck.sh`, `tests/run_runtime_crosscheck.sh`, `tests/run_cli_runtime_parity.sh` (réf : `tests/run_node_c_crosscheck.sh`, `tests/run_runtime_crosscheck.sh`, `tests/run_cli_runtime_parity.sh`).
- Robustesse/sanitizers: `tests/run_robustness.sh` et policy `docs/ROBUSTNESS_SANITIZER_POLICY.md` (réf : `tests/run_robustness.sh`, `docs/ROBUSTNESS_SANITIZER_POLICY.md`).
- Fuzzing: repertoire `tests/fuzz` (réf : `tests/fuzz/findings/README.md`, `tests/fuzz/grammars/protoscript2.ebnf`).

Comment ajouter un nouveau test de feature:
1. Ajouter un `*.pts` et un `*.expect.json` dans `tests/valid` ou `tests/invalid/*` (réf : `tests/README.md`).
2. Enregistrer le cas dans `tests/manifest.json` (réf : `tests/manifest.json`).
3. Lancer `tests/run_conformance.sh` pour valider (réf : `tests/run_conformance.sh`).

# 15. Analyse des performances et de l’impact énergétique

Cette section adopte une analyse technique, pas uniquement descriptive.

---

## 15.1 Complexité du parser

Type : descente récursive LL.

Caractéristiques :

- complexité linéaire O(n) pour source valide

- absence de backtracking

- fail-fast réduit exploration d’états

Impact énergétique : classe B (allocation AST proportionnelle à n).

---

## 15.2 Coût du dispatch VM C

Modèle :

while (...) {  
   switch(opcode) { ... }  
}

Coût :

- O(instructions)

- pénalité de branchement conditionnel

- pas de JIT

- pas de threaded dispatch

Impact énergétique : classe C/D selon intensité des appels.

---

## 15.3 Refcount vs GC

Refcount :

- libération immédiate

- pas de pauses globales

- surcoût incrément/décrément

GC :

- pauses imprévisibles

- coût amorti

ProtoScript2 privilégie :

- prédictibilité

- stabilité énergétique

- absence de latence imprévisible

---

## 15.4 Clone prototype

Clone :

- collecte champs parent→enfant

- copie structurée

- O(nombre de champs)

Impact énergétique : classe C.

---

## 15.5 view<T> invalidation

- versionnement de liste

- vérification constante O(1)

- empêche comportements indéterminés

Trade-off : coût léger à chaque accès.

# 16. Recettes de développement courantes (guide du développeur)
## Ajouter un mot-clé ou un opérateur
1. Mettre à jour la liste `KEYWORDS` si nouveau mot-clé (réf : `src/frontend.js:KEYWORDS`).
2. Gérer le parse dans `Parser` a l’endroit de precedence approprie (ex: `parseAddExpr`, `parseUnaryExpr`) (réf : `src/frontend.js:Parser.parseAddExpr`, `src/frontend.js:Parser.parseUnaryExpr`).
3. Ajouter la sémantique statique dans `Analyzer.typeOfExpr` ou `Analyzer.analyzeStmt` (réf : `src/frontend.js:Analyzer.typeOfExpr`, `src/frontend.js:Analyzer.analyzeStmt`).
4. Baisser en IR dans `IRBuilder.lowerExpr` (réf : `src/ir.js:lowerExpr`).
5. Implementer l’exécution dans le runtime JS (`evalExpr`) et, si necessaire, dans la VM C (`c/runtime/ps_vm.c:exec_function` pour le dispatch d’instructions) (réf : `src/runtime.js:evalExpr`, `c/runtime/ps_vm.c:exec_function`).
6. Ajouter des tests dans `tests/edge` ou `tests/valid` + manifest (réf : `tests/README.md`, `tests/manifest.json`).

## Ajouter une fonction builtin (methode d’un type)
1. Déclarer la méthode dans les prototypes si nécessaire (runtime + type checker) (réf : `src/runtime.js:buildPrototypeEnv`, `src/frontend.js:Analyzer.collectPrototypes`).
2. Valider l’usage dans l’analyseur (`Analyzer.typeOfCall`, `checkMethodArity`) (réf : `src/frontend.js:Analyzer.typeOfCall`, `src/frontend.js:Analyzer.checkMethodArity`).
3. Ajouter le lowering IR si la methode est intrinseque (réf : `src/ir.js:lowerExpr` pour `call_method`/`call_method_static`).
4. Implémenter le runtime JS (dispatch méthode dans `evalCall`) (réf : `src/runtime.js:evalCall`).
5. Implémenter la VM C pour la methode (dispatch dans `c/runtime/ps_vm.c:exec_function` sections `call_method*`) (réf : `c/runtime/ps_vm.c:exec_function`).

## Ajouter une methode de module natif
1. Mettre à jour `modules/registry.json` (réf : `modules/registry.json`).
2. Mettre à jour l’implémentation C du module (ex: `tests/modules_src/io.c:ps_module_init` ou `c/modules/regexp.c:ps_module_init`) et son `ps_module_init_*` (réf : `tests/modules_src/io.c:ps_module_init`, `c/modules/regexp.c:ps_module_init`).
3. Mettre à jour le runtime JS dans `buildModuleEnv` si parité Node requise (réf : `src/runtime.js:buildModuleEnv`).
4. Ajouter un test d’import + appel (réf : `tests/edge`, `tests/manifest.json`).

## Ajouter un nouveau code d’erreur
1. Côté frontend: émettre un diagnostic via `createDiagnostic` / `addDiag` avec un nouveau code `E****` (réf : `src/diagnostics.js:createDiagnostic`, `src/frontend.js:Analyzer.addDiag`).
2. Côté runtime JS: émettre via `rdiag` avec un nouveau code `R****` (réf : `src/runtime.js:rdiag`).
3. Côté runtime C: mettre à jour `ps_runtime_category` pour mapper `PS_ERR_*` vers le nouveau code (réf : `c/runtime/ps_errors.c:ps_runtime_category`).
4. Mettre à jour `docs/lexicon.json` et `docs/protoscript2_spec_lexical.md` pour la surface publique (réf : `docs/lexicon.json`, `docs/protoscript2_spec_lexical.md`).
5. Ajouter des tests dans `tests/invalid` ou `tests/edge` selon le cas (réf : `tests/README.md`).

# 17. Feuille de route et limitations connues (état actuel du dépôt)
As of current repo commit:
- Résolution des modules: le frontend utilise `modules/registry.json` + `search_paths`, mais le runtime C charge via `PS_MODULE_PATH`/`./modules`/`./lib` et ignore `search_paths`. Le runtime JS ne charge pas de modules dynamiques externes. (réf : `docs/STATE_OF_CONFORMITY.md`, `src/frontend.js:loadModuleRegistry`, `c/runtime/ps_modules.c:ps_module_load`, `src/runtime.js:buildModuleEnv`).
- Préprocesseur: pipeline C applique mcpp + mapping #line, tandis que le pipeline Node ne preprocess pas par défaut (réf : `docs/STATE_OF_CONFORMITY.md`, `c/preprocess.c:preprocess_source`, `src/frontend.js:psPreprocessSource`).

Mismatch note (doc vs code) and resolution:
- `README.md` mentionne `./c/ps emit-c`, mais `c/cli/ps.c:usage` et `c/cli/ps.c:main` ne supportent pas la commande `emit-c`. Résolution proposée: soit ajouter la commande dans `c/cli/ps.c:main`, soit corriger `README.md` pour supprimer `emit-c` de la CLI C. (réf : `README.md`, `c/cli/ps.c:usage`, `c/cli/ps.c:main`).

# 18. Limitations structurelles et compromis de conception

ProtoScript2 assume explicitement certaines limitations :

- pas de JIT

- pas de SSA global

- pas d’optimisations interprocédurales avancées

- pas de TCO (tail call optimization)

- pas de GC générationnel

- pas d’exception stack unwinding sophistiquée

- pas de spéculation dynamique

Ces choix sont cohérents avec :

- déterminisme

- auditabilité

- simplicité structurelle

- stabilité multi-cible

# 19. Périmètre normatif

Clarification des rôles :

| Élément          | Normatif         |
| ---------------- | ---------------- |
| SPECIFICATION.md | Oui              |
| VM C             | Oui              |
| IR format        | Oui              |
| Runtime JS       | Non (oracle)     |
| emit-C           | Cible secondaire |
| WASM             | Déploiement      |

# 20. Perspectives de recherche et axes d’évolution

ProtoScript2 adopte une architecture volontairement simple, déterministe et auditée.  
Cette base ouvre plusieurs axes d’évolution et de recherche formalisables.

Les pistes suivantes ne constituent pas des engagements, mais des directions techniquement cohérentes avec les principes établis.

---

## 20.1 Formalisation complète de l’IR

L’IR joue actuellement le rôle de pivot contractuel multi-cibles.

Axes possibles :

- spécification formelle du format IR (BNF + invariants)
  
- preuve de préservation sémantique AST → IR
  
- validation structurée des propriétés (absence de valeurs non initialisées, arité cohérente, etc.)
  
- élaboration d’un sous-ensemble typé de l’IR
  

Objectif :

> transformer l’IR en véritable artefact formel intermédiaire.

---

## 20.2 Introduction d’une représentation SSA optionnelle

L’IR actuel est impératif et structuré.

Une évolution possible :

- conversion vers une forme SSA interne
  
- propagation globale de constantes
  
- élimination de code mort interprocédurale
  
- simplification d’expressions arithmétiques
  

Contraintes :

- préserver le déterminisme
  
- ne pas introduire de non-reproductibilité
  
- garder l’IR lisible et auditable
  

---

## 20.3 Optimisation du dispatch VM

La VM C utilise actuellement un dispatch par `switch`.

Pistes explorables :

- threaded dispatch (computed goto)
  
- regroupement d’opcodes fréquents
  
- micro-optimisation des chemins chauds
  
- séparation hot/cold paths
  

Trade-off :

- gain potentiel de performance
  
- complexification du code
  
- perte partielle de lisibilité
  

---

## 20.4 Analyse d’échappement (escape analysis)

Une analyse statique pourrait :

- détecter les allocations confinées à une fonction
  
- permettre une allocation sur pile
  
- réduire le coût du refcount
  

Cela pourrait améliorer :

- performance
  
- consommation énergétique
  
- pression mémoire
  

---

## 20.5 Allocateur déterministe spécialisé

Au lieu du refcount pur :

- introduction d’arènes déterministes
  
- allocation régionale contrôlée
  
- libération groupée
  

Objectif :

- améliorer localité mémoire
  
- réduire le coût des incréments/décréments de refcount
  
- maintenir la reproductibilité
  

---

## 20.6 Étude d’un GC optionnel déterministe

Bien que le choix actuel soit sans GC, une piste de recherche serait :

- GC générationnel borné
  
- GC incrémental déterministe
  
- cycles gérés explicitement
  

Défi :

> conserver les garanties de reproductibilité.

---

## 20.7 Vérification formelle du déterminisme

ProtoScript2 revendique le déterminisme.

Un axe académique serait :

- formalisation du modèle mémoire
  
- preuve d’absence de dépendance aux adresses
  
- démonstration de reproductibilité inter-plateformes
  

---

## 20.8 Propriétés énergétiques mesurables

La classification énergétique (A–F) pourrait évoluer vers :

- métriques mesurables
  
- instrumentation automatique
  
- profils d’énergie reproductibles
  
- corrélation IR → consommation
  

---

## 20.9 Compilation native optimisée

L’emit-C actuel est fonctionnel.

Pistes :

- génération C structurée optimisée
  
- inlining contrôlé
  
- exploitation d’extensions compilateur
  
- génération LLVM IR directe
  

---

## 20.10 Extension vers un noyau vérifiable

Un axe ambitieux :

- définir un sous-ensemble minimal vérifiable
  
- kernel ProtoScript2
  
- preuve de propriétés fondamentales (type safety, absence d’UB)
  

---

# Conclusion des perspectives

Les axes ci-dessus montrent que ProtoScript2 peut évoluer :

- vers une plateforme expérimentale académique
  
- vers un langage industriel optimisé
  
- ou vers un noyau formel vérifiable
  

Toute évolution devra préserver :

- déterminisme
  
- auditabilité
  
- séparation frontend / IR / runtime
  
- stabilité multi-cible
