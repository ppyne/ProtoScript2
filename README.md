![ProtoScript2](header.png)

# ProtoScript2

ProtoScript2 est un projet de langage avec spécification normative et chaîne de compilation de référence.

## Positionnement

ProtoScript2 suit ces choix structurants :

- typage statique strict
- modèle objet prototype-based (sans classes)
- pas de RTTI utilisateur
- pas de fonctions comme valeurs
- pas de généricité des fonctions
- compilation possible vers C

La spécification de référence est dans `SPECIFICATION.md`.
Format IR sérialisé : `IR_FORMAT.md`.

![La magie cache les coûts. ProtoScript les rend visibles.](slogan.png)

## État actuel du dépôt

Implémenté :

- frontend minimal (lexer + parser + AST + analyse statique)  
  - `src/frontend.js`
- runtime de référence (interpréteur AST pour `--run`)
  - `src/runtime.js`
- diagnostics normatifs `file:line:column` avec codes `E1xxx–E4xxx`
- IR normatif minimal (`Module`, `Function`, `Block`, `Instr`, `Type`)  
  - `src/ir.js`
- backend C de référence non optimisé (oracle sémantique)  
  - `src/c_backend.js`
- CLI compilateur  
  - `bin/protoscriptc`
- CLI native C (bootstrap)  
  - `c/pscc` (lexer/parser C + oracle Node pour la sémantique)
- conformance kit + runners  
  - `tests/`

Non implémenté à ce stade :

- exécution runtime complète (`--run`)
- backend natif/IR finalisé pour tous les cas du langage

## Commandes principales

Vérification statique :

```bash
bin/protoscriptc --check path/to/file.pts

# ou via CLI native C
c/pscc --check path/to/file.pts
```

Exécution (runtime de référence) :

```bash
bin/protoscriptc --run path/to/file.pts
```

Affichage IR :

```bash
bin/protoscriptc --emit-ir path/to/file.pts
bin/protoscriptc --emit-ast-json path/to/file.pts

# IR JSON sérialisé (versionné)
bin/protoscriptc --emit-ir-json path/to/file.pts

# validation d'un IR JSON
bin/protoscriptc --validate-ir path/to/file.ir.json
```

Génération C de référence :

```bash
bin/protoscriptc --emit-c path/to/file.pts
```

Optimisations (gated) :

```bash
BACKEND_C_STABLE=1 bin/protoscriptc --emit-c path/to/file.pts --opt
```

## Tests

Conformance kit :

- `tests/invalid/parse`
- `tests/invalid/type`
- `tests/invalid/runtime`
- `tests/edge`

Runner principal :

```bash
tests/run_conformance.sh
```

Runner opt-safety :

```bash
BACKEND_C_STABLE=1 tests/run_opt_safety.sh
```

Validation du format IR sérialisé :

```bash
tests/run_ir_format.sh
```

Validation croisée runtime (oracle Node vs backend C compilé) :

```bash
tests/run_runtime_crosscheck.sh
```

Le runner écrit `tests/.runtime_crosscheck_passed` si la parité runtime est validée.

Validation structurelle AST (Node AST vs AST C) :

```bash
tests/run_ast_structural_crosscheck.sh
```

Validation IR Node/C (structure + invariants) :

```bash
tests/run_ir_node_c_crosscheck.sh
```

## Build de la CLI C

```bash
make -C c
./c/pscc --check path/to/file.pts
./c/pscc --check-c path/to/file.pts
./c/pscc --check-c-static path/to/file.pts
./c/pscc --ast-c path/to/file.pts
./c/pscc --emit-ir-c-json path/to/file.pts
```

Règle d’or du projet : le compilateur est considéré correct uniquement s’il passe 100 % des tests normatifs.
