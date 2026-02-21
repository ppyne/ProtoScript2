# Allocation Confinement & WASM Parity Report (factual)

## Scope
- Requested tokens searched globally: `ps_file`, `ps_fs_dir`, `ps_fs_walker`, `make_object`, `rt === "TextFile"`, `rt === "Dir"`, `rt === "Walker"`.
- Commands used: `rg` over repository, plus targeted file inspection.

## Objective 1 — Confinement of backend-C allocation logic

### 1) Search results (requested tokens)
- `make_object` appears in IR/frontend/runtime code paths:
  - `src/ir.js` (IR op emission/printing/validation)
  - `c/frontend.c` (IR JSON emission)
  - `c/runtime/ps_vm.c` (IR interpreter op execution)
  - `src/c_backend.js` (emit-C codegen for op `make_object`)
- `ps_file`, `ps_fs_dir`, `ps_fs_walker` appear in:
  - `src/c_backend.js` (C emit helper/runtime strings + allocation branch)
  - generated/derived artifacts and reports (`web/protoscript.data`, `reports/...warnings.txt`)
- `rt === "TextFile"|"Dir"|"Walker"` appears in:
  - `src/c_backend.js` (type inference + call lowering + `make_object` emit branch)
  - `src/ir.js` (type/lowering decisions)

### 2) Explicit confirmation of non-semantic influence
- The new allocation-specialization branch is only in `src/c_backend.js` inside `emitInstr` for op `make_object`.
- It allocates concrete native storage type only:
  - `TextFile|BinaryFile -> ps_file`
  - `Dir -> ps_fs_dir`
  - `Walker -> ps_fs_walker`
- No use of `ps_file/ps_fs_dir/ps_fs_walker` found in:
  - `src/ir.js` clone/super/Self resolution
  - `c/runtime/ps_vm.c` clone/super dispatch logic
- Clone/super/Self behavior is handled by:
  - IR lowering (`src/ir.js`) and VM dispatch (`c/runtime/ps_vm.c`), not by `sizeof` allocation branch.
- No evidence that these C storage names modify:
  - clone dispatch
  - super resolution
  - Self specialization
  - prototype hierarchy
  - lookup path

### 3) Files touched and impacted function(s)
- Touched file (this change):
  - `src/c_backend.js`
- Impacted function:
  - `emitInstr(...)`, `case "make_object"`.
- Added explicit technical-only comment in this branch.

### 4) Execution flow (factual)
1. Frontend/IR emits `make_object` op with proto metadata (`src/ir.js`, `c/frontend.c`).
2. On emit-C path only, `src/c_backend.js` translates `make_object` into C allocation statement.
3. Allocation branch chooses C storage type for handle builtins.
4. Clone/super/Self semantics remain in IR/VM dispatch stages (separate code paths).

### 5) Runtime branch dependency on layout C
- No runtime dispatch branch in IR/VM was found to depend on `ps_file/ps_fs_dir/ps_fs_walker` names from `src/c_backend.js`.
- The branch is allocation-size/type selection for emitted C only.

## Objective 2 — WASM model equivalence

### 1) WASM implementation model
- WASM build uses C sources compiled by Emscripten (`Makefile`, target `web`).
- `WEB_SRCS` includes:
  - `c/cli/ps.c`, `c/frontend.c`, `c/runtime/ps_vm.c`, other C runtime files.
- `web/protoscript.js` is Emscripten glue loading `web/protoscript.wasm`.
- Therefore WASM runtime path is C runtime compiled to WASM, not an alternative JS runtime implementation.

### 2) Is modified `make_object` branch used in C->WASM generation?
- `src/c_backend.js` affects emit-C compiler backend output.
- WASM CLI runtime (`web/protoscript.wasm`) is built from `c/*` sources via `make web`, not from `src/c_backend.js`.
- Factual conclusion: this branch is not part of the CLI WASM execution pipeline; WASM parity is ensured via shared C VM/runtime sources.

### 3) WASM parity tests updated
- `tests/run_wasm_runtime_parity.sh` updated cases:
  - `tests/edge/handle_clone_regexp_direct.pts`
  - `tests/edge/handle_clone_textfile_direct.pts`
  - `tests/edge/handle_clone_dir_direct.pts`
- Result:
  - `PASS=6 FAIL=0 TOTAL=6`.

### 4) Comparative proof (CLI native vs WASM)
- Program set:
  - `tests/edge/handle_clone_regexp_direct.pts`
  - `tests/edge/handle_clone_textfile_direct.pts`
  - `tests/edge/handle_clone_dir_direct.pts`
- Native CLI and WASM both return `rc=1`.
- Same diagnostic code/category/message on all three:
  - `R1013 RUNTIME_CLONE_NOT_SUPPORTED: clone not supported for builtin handle <Type>`.
- Difference observed is source path formatting only (`tests/...` vs `/tmp/ps_mcpp_input_...`), normalized in parity script.

### 5) Parallel WASM-specific implementation check
- No separate WASM-only semantic implementation for clone/super/dispatch found.
- WASM path reuses compiled C runtime (`c/runtime/*`, `c/frontend.c`, `c/cli/ps.c`) through Emscripten artifacts.

## Validation commands executed
- `tests/run_wasm_runtime_parity.sh` -> `PASS=6 FAIL=0`
- Native vs WASM targeted comparison for 3 handle-clone cases -> same `rc`, same `R1013` diagnostics (path-only formatting difference).
