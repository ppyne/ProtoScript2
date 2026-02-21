# ProtoScript2 Baseline Freeze Report (Phase 1)

Date: 2026-02-21  
Baseline command: `./tools/test-orchestrator --full --summary`  
Machine snapshot: `docs/test-baseline.json`

## Result

- Overall status: `pass`
- Total flows executed: `8`
- Total counted tests: `1773`
- Sanitizer status: requested and executed (`all_passed=true`)
- emit-c compilation status: `pass`

## Baseline Validation

All orchestrated flows now pass:

1. `run_all_robust`
2. `conformance_modules`
3. `node_c_crosscheck_strict`
4. `runtime_crosscheck`
5. `cli_runtime_parity`
6. `runtime_triangle_parity`
7. `wasm_runtime_parity`
8. `robustness`

## Resolved Drift

- `tests/run_all.sh --robust` includes `tests/run_robustness.sh`.
- `tests/run_robustness.sh` ends with:
  - `make -C c clean`
- This cleanup invalidates `c/ps` / `c/pscc` for later flows.
- `tools/test-orchestrator` now repairs this deterministically:
  - preflow invariant: dependent flows ensure `c/ps` and `c/pscc` exist and are executable;
  - auto-rebuild when missing;
  - postflow note if a sub-script invalidates binaries.

## Corrective action added in Phase 1

- `tools/test-orchestrator` now enforces C binary prerequisites before flows that depend on `c/ps` / `c/pscc` (rebuild via `make -C c` when needed).
- `run_all_robust` summary parsing is disabled in orchestrator to avoid mis-attributing nested `Summary:` lines from child scripts.
- Regression check added:
  - `tests/run_orchestrator_prereq_guard.sh`
  - invokes `tools/test-orchestrator --selftest-prereq-guard` to verify recovery if `c/ps` and `c/pscc` disappear mid-run.

## Reproducibility / Auditability

- Per-flow logs are stored under:
  - `reports/test-orchestrator/*.log`
- The structured baseline snapshot is versioned:
  - `docs/test-baseline.json`
- Normalization rules extracted and versioned:
  - `tools/test-normalization`
  - `docs/normalization-policy.md`
