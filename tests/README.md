![ProtoScript2](../header.png)

# ProtoScript V2 Conformance Kit (WIP)

Règle d'or: le compilateur est correct uniquement s'il passe 100 % des tests normatifs.

## Arborescence

- `invalid/parse` : source invalide lexicalement/syntaxiquement
- `invalid/type` : source invalide statiquement
- `invalid/runtime` : source valide statiquement qui doit lever une exception runtime
- `edge` : cas limites normatifs obligatoires

## Format des cas

Chaque cas contient:

- un fichier source `*.pts`
- un fichier d'attentes `*.expect.json`

Champs attendus dans `*.expect.json`:

- `status`: `reject-parse` | `reject-static` | `reject-runtime` | `accept-runtime`
- `error_family`: `E1xxx` | `E2xxx` | `E3xxx` | `E4xxx` | `Rxxxx`
- `error_code`: code canonique stable
- `category`: catégorie d'erreur normative
- `position`: `file`, `line`, `column`

Pour les cas positifs runtime :

- `status`: `accept-runtime`
- `expected_stdout`: sortie standard exacte attendue

## Exécution

Runner fourni: `tests/run_conformance.sh`
Pré-requis: `jq`

Exemples :

- `tests/run_conformance.sh`
- `FRONTEND_ONLY=1 tests/run_conformance.sh`
- `COMPILER=./bin/protoscriptc tests/run_conformance.sh`
- `CONFORMANCE_CHECK_CMD=\"./myc check\" CONFORMANCE_RUN_CMD=\"./myc run\" tests/run_conformance.sh`

Le runner écrit `tests/.conformance_passed` uniquement si la suite complète passe sans skip.

Runner runtime crosscheck: `tests/run_runtime_crosscheck.sh`
Le runner écrit `tests/.runtime_crosscheck_passed` si la parité runtime Node/C est validée.

Runner de validation croisée complète (diagnostics + comportement): `tests/run_node_c_crosscheck.sh`
Option stricte AST intégrée: `tests/run_node_c_crosscheck.sh --strict-ast`
Option stricte statique C intégrée: `tests/run_node_c_crosscheck.sh --strict-static-c`

Runner dédié frontend C vs oracle Node (`--check`): `tests/run_c_frontend_oracle.sh`
Runner dédié statique C vs oracle Node (`--check-c-static`): `tests/run_c_static_oracle.sh`

Runner dédié comparaison structurelle AST (Node vs C): `tests/run_ast_structural_crosscheck.sh`

## Opt Safety

Runner dédié: `tests/run_opt_safety.sh`

Préconditions :

- `tests/.conformance_passed` présent
- `BACKEND_C_STABLE=1`

Exemple :

- `BACKEND_C_STABLE=1 tests/run_opt_safety.sh`
