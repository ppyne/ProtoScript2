# WASM Build Pipeline (Reproducible)

This repository does not rely on a frozen/hand-restored WASM artifact.
`web/protoscript.{js,wasm,data}` must be reproducible from source with the canonical build:

```bash
make web-clean
make web
```

## Important — Rebuild WASM

Any modification of the C frontend (`c/frontend.c`) requires a full rebuild of the WASM target:

```bash
make web
```

Running WASM tests without rebuilding can cause diagnostic divergences between native CLI and WASM.

## Toolchain

Validated toolchain used for parity:

- Node.js: `v22.16.0`
- Emscripten: `4.0.23`
- emcc clang: `22.0.0git`

You can print local versions with:

```bash
node -v
emcc -v
python3 --version
```

## Why `STACK_SIZE` Is Explicit

With newer Emscripten versions, relying on the default WASM stack size can trigger runtime failures
(`memory access out of bounds`) on normal ProtoScript programs.

To keep runtime behavior stable across rebuilds, the build now sets:

- `WEB_STACK_SIZE := 1048576` (1 MiB)

through the canonical `WEB_LDFLAGS` in the root `Makefile`.

This removes dependence on toolchain-default stack sizing and keeps WASM parity with native C runtime.

## Functional Repro Check

After `make web`, validate with:

```bash
tests/run_runtime_triangle_parity.sh
tests/run_all.sh
```

Acceptance criterion: both commands pass without restoring any previously committed WASM binary.
