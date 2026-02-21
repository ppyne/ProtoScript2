# ProtoScript2 Test/Validation Infrastructure Audit

Date: 2026-02-21  
Scope: static repository audit only (no code/test changes, no test execution in this audit pass).

## 1) Inventory of Existing Test Infrastructure

### 1.1 Global inventory snapshot

- `tests/` total files: `1062`
- `*.pts`: `502`
- `*.expect.json`: `460`
- `*.expect.txt`: `4`
- shell runners `*.sh`: `29`
- helper scripts `*.js`: `5`
- C harness/tests `*.c`: `18`
- normative manifest cases (`tests/manifest.json`): `411`

### 1.2 Test directories (discovered under `tests/`)

- `tests/cli`
- `tests/debug`
- `tests/edge`
- `tests/errors/diagnostics`
- `tests/fixtures`
- `tests/fs`
- `tests/invalid`
- `tests/invalid/parse`
- `tests/invalid/runtime`
- `tests/invalid/type`
- `tests/invalid/visibility_internal`
- `tests/ir-format`
- `tests/modules_src`
- `tests/opt-safety`
- `tests/regexp`
- `tests/robustness`
- `tests/sys`
- `tests/sys_execute`
- `tests/valid`
- `tests/valid/visibility_internal`
- `tests/wasm`

### 1.3 Primary runner files

- `tests/run_all.sh`
- `tests/run_conformance.sh`
- `tests/run_node_c_crosscheck.sh`
- `tests/run_runtime_crosscheck.sh`
- `tests/run_runtime_triangle_parity.sh`
- `tests/run_cli_runtime_parity.sh`
- `tests/run_wasm_runtime_parity.sh`
- `tests/run_static_diag_parity.sh`
- `tests/run_diagnostics_strict.sh`
- `tests/run_ast_structural_crosscheck.sh`
- `tests/run_c_frontend_oracle.sh`
- `tests/run_c_static_oracle.sh`
- `tests/run_ir_node_c_crosscheck.sh`
- `tests/run_ir_format.sh`
- `tests/run_ir_switch_lowering.sh`
- `tests/run_cli_tests.sh`
- `tests/run_robustness.sh`
- `tests/run_determinism.sh`
- `tests/run_c_determinism.sh`
- `tests/run_opt_safety.sh`
- `tests/run_builtin_contract_guard.sh`
- `tests/run_builtin_clone_instance_contract.sh`
- `tests/run_cli_autonomy_guard.sh`
- `tests/run_docs_proto_lint.sh`
- `tests/debug/run_debug_tests.sh`
- `tests/robustness/run_memory_audit.sh`
- `tests/robustness/run_asan_ubsan_emitc.sh`

### 1.4 Classification

- Unit tests (narrow-scope harness style):
  - `tests/ir-format/*` via `tests/run_ir_format.sh`
  - `tests/errors/diagnostics/*` via `tests/run_diagnostics_strict.sh`
  - `tests/debug/*` via `tests/debug/run_debug_tests.sh`
  - `tests/run_cli_autonomy_guard.sh` (static anti-delegation scan)
- Integration tests:
  - `tests/run_conformance.sh`
  - `tests/run_cli_tests.sh`
  - `tests/run_node_c_crosscheck.sh`
  - `tests/run_runtime_crosscheck.sh`
  - `tests/run_cli_runtime_parity.sh`
  - `tests/run_runtime_triangle_parity.sh`
  - `tests/run_wasm_runtime_parity.sh`
  - `tests/run_ast_structural_crosscheck.sh`
  - `tests/run_ir_node_c_crosscheck.sh`
  - `tests/run_opt_safety.sh`
- Conformance-like tests:
  - Manifest-driven suites in `tests/manifest.json`:
    - `invalid/parse` (7)
    - `invalid/type` (69)
    - `invalid/runtime` (37)
    - `invalid/visibility_internal` (3)
    - `valid/visibility_internal` (3)
    - `regexp` (27)
    - `edge` (242)
    - `fs` (8)
    - `sys` (4)
    - `sys_execute` (11)
- Manual/ad-hoc tests:
  - `tests/cli/*.pts` (scripted CLI checks, not manifest-normative)
  - `tests/edge/manual_ex*.pts` consumed by `tests/run_cli_tests.sh`
  - `tests/invalid/multiple_static_errors.pts` (duplicate standalone file outside manifest)
  - documentation syntax lint in `tests/run_docs_proto_lint.sh`
- Cross-runtime comparisons (explicit):
  - Node vs emit-c compiled: `tests/run_runtime_crosscheck.sh`, `tests/run_node_c_crosscheck.sh`
  - Node vs C CLI runtime: `tests/run_cli_runtime_parity.sh`
  - C native VM vs WASM VM vs emit-c compiled: `tests/run_runtime_triangle_parity.sh`
  - Node vs WASM runtime subset: `tests/run_wasm_runtime_parity.sh`
  - Node vs C diagnostics: `tests/run_static_diag_parity.sh`, `tests/run_diagnostics_strict.sh`, `tests/run_c_frontend_oracle.sh`, `tests/run_c_static_oracle.sh`
  - Node AST vs C AST shape: `tests/run_ast_structural_crosscheck.sh`
  - Node IR vs C IR invariants: `tests/run_ir_node_c_crosscheck.sh`

### 1.5 Required findings for requested checks

- Node and C already cross-validated: **Yes** (multiple dedicated parity/oracle runners).
- `--emit-c` output compiled and tested: **Yes** (`run_node_c_crosscheck`, `run_runtime_crosscheck`, `run_runtime_triangle_parity`, `run_opt_safety`, `run_asan_ubsan_emitc`).
- Fuzzing exists: **No evidence** (no afl/libFuzzer/honggfuzz/property-based harness).
- Performance tests exist: **No formal benchmark suite**.
- Sanitizer usage configured: **Yes**, via robustness path (`tests/run_robustness.sh`, `tests/robustness/run_asan_ubsan_emitc.sh`).

## 2) Execution Paths Coverage Matrix

| Feature / Area | Node | C runtime / CLI | emit-c compiled | Tested? | Notes |
|---|---:|---:|---:|---|---|
| Parse reject diagnostics | Yes | Yes | N/A | Yes | Conformance + static parity/oracle scripts |
| Static type diagnostics | Yes | Yes (`ps`, `pscc --check-c-static`) | N/A | Yes | Includes line/col/code parity checks |
| Runtime accept/reject semantics | Yes (`--run`) | Yes (`c/ps run`) | Yes (`gcc` compiled generated C) | Yes | Covered by runtime crosscheck + triangle |
| CLI behavior (`help`, flags, exit codes) | Partial | Yes (primary) | N/A | Yes | `tests/run_cli_tests.sh`; Node used as parity oracle in spots |
| AST emission parity | Yes | Yes (`--ast-c`) | N/A | Yes | Structural signature comparison |
| IR emission/validation parity | Yes | Yes (`--emit-ir-c-json`) | N/A | Yes | Signature + invariant checks |
| WASM runtime parity | Oracle | WASM runtime (C VM compiled to wasm) | N/A | Yes (subset + triangle full runtime set) | Dedicated wasm runner + triangle |
| Module import/runtime behavior | Yes (frontend/runtime, with caveats) | Yes | Yes (through runtime parity suites) | Partial | Known divergence documented in `docs/divergence_report.md` |
| Docs/manual syntax consistency | N/A | N/A | N/A | Yes | `tests/run_docs_proto_lint.sh` |
| Determinism | Yes (Node compile determinism) | Partial (single-case static twice + parity normalization) | Partial | Partial | Determinism tests are not broad corpus-wide |
| Sanitizer safety | N/A | Yes (ASan build path) | Yes (ASan/UBSan warning policy) | Optional path | Only enabled when running robustness flow |
| Performance regression control | No | No | No | No | No thresholds/baselines/bench harness |

### Coverage gaps (clear)

- No fuzzing.
- No benchmark/perf regression gate.
- No always-on sanitizer in default `tests/run_all.sh` (only with `--robust`).
- Determinism checks are targeted, not full-suite (few files / specific paths).
- WASM parity runner has a small explicit case list (`tests/run_wasm_runtime_parity.sh`), though triangle parity broadens runtime coverage.

## 3) Diagnostic Stability Analysis

- Structured vs free-form:
  - Mostly structured format: `<file>:<line>:<col> <CODE> <CATEGORY>: <message>`.
  - Node formatter: `src/diagnostics.js` (`formatDiagnostic`).
  - C formatter: `c/diag.c` (`ps_diag_write`).
- Error codes stable:
  - Strongly implied stable contract (`E****` / `R****`) in docs and tests.
  - Conformance expects `error_code` and `category` in `*.expect.json` for non-accept cases.
- Line/column determinism:
  - Enforced in conformance (expected position fields) and static parity scripts.
  - Explicit rerun determinism check exists for strict diagnostics (Node twice) and one C static case (`--check-c-static-twice`).
- Diagnostics compared across implementations:
  - Yes, extensively (`run_static_diag_parity.sh`, `run_diagnostics_strict.sh`, `run_c_frontend_oracle.sh`, `run_c_static_oracle.sh`, plus crosschecks).
- Important caveat:
  - Several parity scripts normalize volatile paths/tokens (`ps_tmp`, `__SRC__`) before compare, which improves reproducibility but weakens fully raw-output strictness.

## 4) Security & Robustness

- ASan/UBSan enabled anywhere:
  - Yes, in robustness flow and emit-c sanitizer policy script.
- Memory-safety testing:
  - Yes, targeted C harnesses in `tests/robustness/run_memory_audit.sh` and repeated run loops in `tests/run_robustness.sh`.
- Fuzzing:
  - No fuzzing harness found.
- Crash cases tracked:
  - No explicit crash corpus/tracker directory or structured crash triage workflow found.
  - Robustness scripts detect failures, but crash lifecycle tracking is not formalized.

## 5) Performance Handling

- Benchmarks: none found.
- Regression detection: none found for performance metrics.
- Acceptable performance budget/thresholds: none formalized.
- Existing related signals:
  - `run_node_c_crosscheck.sh` records/prints slow cases (observability only, not a gate).
  - CLI supports `--time` (manual timing aid, not automated regression control).

## 6) Contradictions / Constraints

### 6.1 Architectural decisions that may conflict with a future unified strict framework

- `c/pscc` is intentionally hybrid: native for some modes but forwards to Node reference for `--emit-ir`/`--emit-c`/`--check` final path (`c/pscc.c`).
  - Constraint: strict “independent implementation differential” is weakened for forwarded paths.
- Existing system has many specialized runners with overlapping scope.
  - Constraint: unifying into one runner needs careful retention of script-specific invariants (normalization rules, module bootstrap, exit-code semantics).
- Optional sanitizer path (`tests/run_all.sh --robust`) instead of default.
  - Constraint: sanitizer enforcement as hard gate will change current developer workflow expectations.
- Determinism currently mixes strict comparisons and normalized comparisons.
  - Constraint: fully deterministic raw output requirements may fail until temp-path/host-dependent output is redesigned.

### 6.2 Areas likely needing refactor before new plan

- Consolidate duplicated/overlapping parity runners to reduce drift and duplicate logic.
- Isolate normalization policy into shared utility (currently duplicated sed rules across scripts).
- Clarify Node-vs-C authority per mode (especially forwarded `pscc` modes) before strict differential compliance claims.
- Standardize diagnostic tuple extraction/comparison in one place.
- Add explicit ownership for non-manifest tests vs normative tests to prevent accidental contract drift.

## 7) Risk Assessment

### Most fragile parts

- Multi-runtime parity boundaries (Node runtime vs C runtime vs WASM vs emit-c compiled).
- Module/import behavior differences already documented as partial/divergent (`docs/divergence_report.md`).
- Forwarding boundaries in `pscc` where independence is reduced.
- Output normalization dependence for parity scripts (possible hidden divergence classes).

### Likely Node/C divergence zones

- Module loading/runtime module ecosystem (`search_paths`, dynamic loading behavior differences).
- Preprocessing and source mapping behavior.
- Error rendering details when not explicitly normalized/compared with fixed expectations.

### Is `--emit-c` mature enough as first-class execution path?

- Evidence for maturity:
  - Widely exercised through compile-and-run parity checks and sanitizer policy checks.
  - Included in triangle parity and optimization safety gates.
- Remaining cautions:
  - Sanitizer checks are not default in main path unless robustness mode is run.
  - No fuzzing/perf regimen.
- Audit conclusion:
  - `--emit-c` is **operationally mature for parity validation**, but **not yet fully hardened** for “expert-grade first-class” status without always-on sanitizers + fuzz/perf coverage.

## 8) Recommendation Summary

### Reuse as-is

- Manifest-driven conformance model (`tests/manifest.json` + `tests/run_conformance.sh`).
- Cross-runtime parity concepts (Node/C, Node/emit-c, triangle including WASM).
- Strict diagnostics suite (`tests/errors/diagnostics`) and static diag parity logic.
- Robustness sanitizer policy script (`tests/robustness/run_asan_ubsan_emitc.sh`).

### Must be refactored

- Runner sprawl and overlap into a coherent orchestrator with shared compare utilities.
- Explicit path normalization policy as a versioned artifact (currently embedded per script).
- Clear separation of normative vs non-normative tests and required CI gates.
- Clarify/retire `pscc` forwarding behavior for modes targeted by strict differential guarantees.

### Should be postponed

- Hard performance SLAs until benchmark harness + representative workloads exist.
- Full deterministic raw-output enforcement before reducing path/token normalization dependence.

### Suggested phased rollout

1. Baseline freeze  
   Record current pass matrix per runner and lock normative suite boundaries.
2. Unify orchestration  
   Introduce one top-level orchestrator that calls existing proven checks first (no semantic change).
3. Make robustness mandatory in CI tier  
   Move sanitizer and memory-audit steps from optional to required on protected branches.
4. Differential hardening  
   Expand deterministic and parity checks to reduce normalization exceptions; increase C-native independent coverage.
5. Add missing disciplines  
   Add fuzzing corpus/harness and performance benchmark gates.
6. Re-classify `--emit-c` as fully first-class  
   Only after sanitizer/fuzz/perf gates are consistently green.

## Assumptions and Limits

- This audit is repository-static and did not execute the suite.
- Conclusions about effectiveness are based on script logic and declared policies, not measured pass/fail results in this run.
- File inventory numbers are from current workspace state at audit time.
