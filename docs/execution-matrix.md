# ProtoScript2 Execution Matrix (Phase 1)

Date: 2026-02-21

This matrix documents current execution paths and which existing scripts exercise them. It is descriptive only (no redesign).

## Legend

- `Independent? = Yes`: path executes with its own implementation/runtime.
- `Independent? = Partial`: path exists but includes forwarding/delegation.
- `Independent? = No`: path is effectively executed by another implementation path.

## Matrix

| Feature / Area | Node | C runtime | emit-c compiled | Independent? | Tested by which script(s)? |
|---|---:|---:|---:|---|---|
| Parse/static check (`--check`) | Yes (`bin/protoscriptc`) | Partial (`c/pscc --check` may forward) | N/A | Partial | `tests/run_conformance.sh`, `tests/run_node_c_crosscheck.sh`, `tests/run_c_frontend_oracle.sh`, `tests/run_static_diag_parity.sh` |
| C-native syntax-only check | N/A | Yes (`c/pscc --check-c`) | N/A | Yes | `tests/run_cli_tests.sh`, `tests/run_c_frontend_oracle.sh` |
| C-native static check | N/A | Yes (`c/pscc --check-c-static`) | N/A | Yes | `tests/run_c_static_oracle.sh`, `tests/run_node_c_crosscheck.sh --strict-static-c` |
| Runtime execution (reference) | Yes (`--run`) | Yes (`c/ps run`) | Yes (generated C + gcc) | Yes (runtime) | `tests/run_cli_runtime_parity.sh`, `tests/run_runtime_crosscheck.sh`, `tests/run_runtime_triangle_parity.sh` |
| AST emission | Yes (`--emit-ast-json`) | Yes (`c/pscc --ast-c`) | N/A | Yes | `tests/run_ast_structural_crosscheck.sh` |
| IR emission (Node) | Yes (`--emit-ir-json`) | N/A | N/A | Yes | `tests/run_ir_node_c_crosscheck.sh`, `tests/run_ir_format.sh` |
| IR emission (C frontend) | N/A | Yes (`c/pscc --emit-ir-c-json`) | N/A | Yes | `tests/run_ir_node_c_crosscheck.sh`, `tests/run_ir_switch_lowering.sh` |
| emit-c generation | Yes (`--emit-c`) | Partial (`c/pscc --emit-c` forwards to Node) | N/A | Partial | `tests/run_node_c_crosscheck.sh`, `tests/run_runtime_crosscheck.sh`, `tests/run_runtime_triangle_parity.sh`, `tests/run_opt_safety.sh` |
| emit-c compile + execute | Source from Node | N/A | Yes (`gcc` compile/run) | Yes (execution path) | `tests/run_runtime_crosscheck.sh`, `tests/run_runtime_triangle_parity.sh`, `tests/run_node_c_crosscheck.sh` |
| WASM runtime (C VM compiled to WASM) | Oracle only | N/A | N/A | Yes | `tests/run_wasm_runtime_parity.sh`, `tests/run_runtime_triangle_parity.sh` |
| Sanitizer robustness path | N/A | Yes (ASan C build) | Yes (ASan/UBSan emit-c policy build) | Yes (when enabled) | `tests/run_robustness.sh`, `tests/robustness/run_asan_ubsan_emitc.sh` |

## Explicit forwarding / non-independence

- `c/pscc --check`: runs C syntax parse then forwards to `bin/protoscriptc` for full check flow.
- `c/pscc --emit-ir`: forwards to `bin/protoscriptc`.
- `c/pscc --emit-c`: forwards to `bin/protoscriptc`.
- Therefore strict implementation independence is partial for `pscc` in these modes.

## emit-c partial/exclusion notes

- emit-c path is exercised in parity/crosscheck scripts and sanitizer policy checks.
- Some scripts emulate C path for module-heavy cases (`run_node_c_crosscheck.sh`), i.e., not every case is emitted/compiled identically.
- Sanitizer policy for emit-c is currently optional via robustness flow, not always-on in every default test invocation.
