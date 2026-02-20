# Decision Log: Sealed Builtin Handles

## Scope
Lot "discipline syntaxique + sealed builtins + garde-fous".

## Decision
Native-handle builtins are sealed by default:
- TextFile
- BinaryFile
- Dir
- Walker
- RegExp

## Why this is durable
1. Handle inheritance has low user value and high semantic risk (lifecycle/ownership ambiguity).
2. Handles are already non-clonable (`R1013`), so keeping inheritance open created contradictory extension paths.
3. Sealed-by-default avoids backend-specific surprises and keeps Node/C/WASM/emit-C aligned.

## Normative alignment
SPEC now states (normative language):
- Native handle builtin prototypes MUST be sealed by default.
- Runtime/backend MUST NOT allow inheriting a native handle unless explicitly unsealed by a normative rule.
- `clone()` on handles MUST fail with `R1013 RUNTIME_CLONE_NOT_SUPPORTED`.

## Guard rails added
1. `tests/run_docs_proto_lint.sh`
- Fails on `name:type` syntax inside ProtoScript code blocks in key docs.
2. `tests/run_builtin_contract_guard.sh`
- Fails if handle-sealed flags drift in JS/C frontends/runtime metadata.
- Fails if direct handle clone expectations stop asserting `R1013`.

## Tests and behavior changes
1. Added invalid/type sealed inheritance tests for builtins:
- `builtin_sealed_inheritance_textfile`
- `builtin_sealed_inheritance_regexp`
- `builtin_sealed_inheritance_dir`
2. Removed runtime edge inheritance/super-clone handle cases from manifest because they are now statically rejected by `E3140`.

## Notes
No delegation or fallback path was introduced.
All changes are implementation + guarding changes consistent with existing clone-handle policy.
