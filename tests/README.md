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

- `status`: `reject-parse` | `reject-static` | `reject-runtime`
- `error_family`: `E1xxx` | `E2xxx` | `E3xxx` | `E4xxx` | `Rxxxx`
- `error_code`: code canonique stable
- `category`: catégorie d'erreur normative
- `position`: `file`, `line`, `column`

## Exécution

Runner fourni: `tests/run_conformance.sh`
Pré-requis: `jq`

Exemples :

- `tests/run_conformance.sh`
- `FRONTEND_ONLY=1 tests/run_conformance.sh`
- `COMPILER=./bin/protoscriptc tests/run_conformance.sh`
- `CONFORMANCE_CHECK_CMD=\"./myc check\" CONFORMANCE_RUN_CMD=\"./myc run\" tests/run_conformance.sh`

Le runner écrit `tests/.conformance_passed` uniquement si la suite complète passe sans skip.

## Opt Safety

Runner dédié: `tests/run_opt_safety.sh`

Préconditions :

- `tests/.conformance_passed` présent
- `BACKEND_C_STABLE=1`

Exemple :

- `BACKEND_C_STABLE=1 tests/run_opt_safety.sh`
