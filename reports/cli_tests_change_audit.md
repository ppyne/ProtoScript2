# Audit `tests/run_cli_tests.sh` (changements récents)

## Règle appliquée
- Aucun ajustement de test pour masquer une divergence runtime.
- Toute modification doit être reliée à une règle normative explicite, sinon revert.

## Lignes inspectées

1. `/Users/avialle/dev/ProtoScript2/tests/run_cli_tests.sh:237`
- Changement observé précédemment: `run exit code` passait de `100` à `0`.
- Analyse: non conforme; `tests/cli/exit_code.pts` retourne `100` (comportement CLI C attendu).
- Action: **revert effectué** vers `100`.
- Base normative: le code de retour CLI reflète la valeur `int` de `main`.

2. `/Users/avialle/dev/ProtoScript2/tests/run_cli_tests.sh:241`
- Changement observé précédemment: vérification `preprocess mapping` modifiée vers une erreur parse (`E1001`) au lieu d’une erreur runtime mappée.
- Analyse: non conforme; ce test valide justement la propagation `#line` + erreur runtime mappée (`mapped_file.pts`).
- Action: **revert effectué** vers `mapped_file.pts:202:17 R1004 ...`.
- Base normative: le préprocesseur et le mapping source doivent être conservés en diagnostic.

3. `/Users/avialle/dev/ProtoScript2/tests/run_cli_tests.sh:287`
- Changement: `emit-c` vérifié via `pscc --emit-c` au lieu de `ps emit-c`.
- Analyse: **conforme au nouvel invariant d’architecture**; `c/ps` est runtime CLI autonome, pas un front-end de compilation.
- Action: **conservé**.
- Base normative: séparation runtime CLI autonome vs chaîne de compilation/oracle.

4. `/Users/avialle/dev/ProtoScript2/tests/run_cli_tests.sh:240`
- Changement: `runtime error exit` attendu à `1`.
- Analyse: conforme à la convention runtime (`exception runtime => exit 1`).
- Action: **conservé**.

5. `/Users/avialle/dev/ProtoScript2/tests/run_cli_tests.sh:347`
- Changement: erreurs runtime manuelles attendues à `1`.
- Analyse: cohérent avec la convention ci-dessus.
- Action: **conservé**.

6. `/Users/avialle/dev/ProtoScript2/tests/run_cli_tests.sh:223`
- Ajout: test de parité ciblée `clone_inherited_throw_parity`.
- Analyse: renforce la couverture clone/super.
- Action: **conservé**.
