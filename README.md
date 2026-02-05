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

La spécification de référence est dans `specification.md`.

## État actuel du dépôt

Implémenté :

- frontend minimal (lexer + parser + AST + analyse statique)  
  - `src/frontend.js`
- diagnostics normatifs `file:line:column` avec codes `E1xxx–E4xxx`
- IR normatif minimal (`Module`, `Function`, `Block`, `Instr`, `Type`)  
  - `src/ir.js`
- backend C de référence non optimisé (oracle sémantique)  
  - `src/c_backend.js`
- CLI compilateur  
  - `bin/protoscriptc`
- conformance kit + runners  
  - `tests/`

Non implémenté à ce stade :

- exécution runtime complète (`--run`)
- backend natif/IR finalisé pour tous les cas du langage

## Commandes principales

Vérification statique :

```bash
bin/protoscriptc --check path/to/file.pts
```

Affichage IR :

```bash
bin/protoscriptc --emit-ir path/to/file.pts
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

Règle d’or du projet : le compilateur est considéré correct uniquement s’il passe 100 % des tests normatifs.
