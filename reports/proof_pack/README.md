# Proof Pack

1. `01_cli_autonomy_guard.log`
- Résultat du guard anti-délégation (`execv/system/popen/posix_spawn/protoscriptc/node//bin/`) sur `c/cli/ps.c`.

2. `02_no_local_protoscriptc_run.log`
- Preuve d’exécution de `c/ps run` depuis `/tmp` sans `./bin/protoscriptc`.

3. `03_cli_runtime_parity.log`
- Parité runtime réelle `bin/protoscriptc --run` vs `c/ps run` sur tous les cas runtime du manifest (avec diff minimal).

4. `04_make_web.log`
- Rebuild WASM (`make web`) et code de retour.

5. `05_wasm_runtime_parity.log`
- Parité ciblée Node oracle vs runtime C en WASM (subset incluant clone/super).

6. `06_run_all.log`
- Exécution complète `tests/run_all.sh` sur l’état courant.

7. `07_run_all_robust.log`
- Exécution complète `tests/run_all.sh --robust` sur l’état courant.
