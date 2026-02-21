# ProtoScript2 Normalization Policy (Phase 1)

Date: 2026-02-21

## Purpose

Normalization is used only for parity comparison where output contains unavoidable volatile tokens (temp file names and host-dependent absolute paths). This policy makes those rules explicit and auditable.

## Source of truth

The canonical utility is:

- `tools/test-normalization`

The utility extracts rules already present in existing scripts (no semantic change):

- `tests/run_cli_runtime_parity.sh`
- `tests/run_wasm_runtime_parity.sh`
- `tests/run_runtime_triangle_parity.sh`

## Profiles

### `cli-runtime`

Used for CLI runtime parity (`protoscriptc --run` vs `c/ps run`).

Normalized:

- volatile temp tokens matching:
  - `([A-Za-z0-9_./-]*/)?ps_<uuid>_<n>`
  - replaced with `ps_tmp`

Not normalized:

- diagnostics code/category/message text
- non-temp paths
- stdout/stderr structure

### `wasm`

Used for Node vs WASM runtime parity.

Normalized:

- temp tokens `ps_<uuid>_<n>` -> `ps_tmp`
- preprocessor temp file path:
  - `/tmp/ps_mcpp_input_<n>.txt` -> `/tmp/ps_mcpp_input_tmp.txt`
- source path prefix at line start:
  - `(/tmp/ps_mcpp_input_tmp.txt|.*/tests/<file>.pts):` -> `__SRC__:`

Not normalized:

- line/column, error codes, categories
- runtime messages (except path prefix normalization above)
- non-matching path content

### `triangle`

Used for runtime triangle parity (C native VM vs WASM vs emit-c compiled).

Normalized:

- temp tokens `ps_<uuid>_<n>` -> `ps_tmp`
- preprocessor temp path `/tmp/ps_mcpp_input_<n>.txt` -> `/tmp/ps_mcpp_input_tmp.txt`
- leading source path tokens:
  - `/tmp/ps_mcpp_input_tmp.txt`
  - `ps_tmp`
  - `$ROOT_DIR/tests/<file>.pts`
  - `.*/tests/<file>.pts`
  - `tests/<file>.pts`
  - all rewritten to `__SRC__:`

Not normalized:

- diagnostic code/category/message content
- ordering/content of non-path text
- semantic runtime output

## Why these rules exist

- Temp file names include process-specific/random components.
- Absolute paths differ across host machines/workspaces.
- Existing parity scripts already account for this volatility; this extraction centralizes and documents it.

## What is intentionally NOT normalized

- Error codes (`E****`, `R****`)
- Diagnostic categories
- Exit codes
- Runtime semantic messages
- Relative ordering of output lines
- Values unrelated to volatile paths/temp tokens

## Safety boundary

Normalization is a comparison pre-processing step only. It must not be used to transform compiler/runtime behavior, diagnostics generation, or expected outputs in source-of-truth tests.
