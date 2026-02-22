# ProtoScript2 v1.0.0 Release Plan

## Stability Promise (v1.x)

### Stable in v1.x (breaking changes require v2.0)

- CORE language semantics specified in `SPECIFICATION.md`.
- Static diagnostics shape used by conformance (`tests/manifest.json` + expected diagnostics fixtures).
- Runtime behavior validated by parity/crosscheck suites run by `tools/test-orchestrator`.
- Standard library module contracts covered by normative suites (`Io`, `Math`, `JSON`, `Time`, `Fs`, `Sys`, `SysExecute`, `RegExp`).
- CLI contract for `c/ps` and `c/pscc` commands already validated by `tests/run_cli_tests.sh` via orchestrator flows.

### Experimental in v1.x (may evolve without major bump)

- WASM packaging/distribution details and browser integration surface.
- Performance thresholds and benchmark corpus composition (regression policy remains enforced).
- Fuzz corpus breadth and long-run fuzz campaign parameters.
- Release artifact layout inside `dist/` (manifest compatibility is maintained, payload may expand).

## Release Gates Checklist

All gates are mandatory for `v1.0.0`.

```bash
make -C c
./tools/test-orchestrator --full --summary
tests/run_runtime_crosscheck.sh
tests/run_sanitizer_smoke.sh
```

Nightly hardening profile additionally runs:

```bash
tests/run_robustness.sh
```

Single-entry release gate runner:

```bash
./tools/release-build --version 1.0.0
# nightly variant
./tools/release-build --version 1.0.0 --nightly
```

## Supported Platforms Matrix

| Platform | Architecture | Status | Validation source |
|---|---|---|---|
| Linux | x86_64 | Tier 1 | PR/Nightly CI (`.github/workflows/validation.yml`) |
| Linux | aarch64 | Tier 2 | Local/release-candidate validation required |
| macOS | arm64 | Tier 2 | Local release validation (`tools/release-build`) |
| macOS | x86_64 | Tier 2 | Local release validation (`tools/release-build`) |
| WASM (web runtime) | wasm32 | Experimental | `make web` + parity checks as available |

## Reproducible Local Release Build

Prerequisites:

- `node`, `make`, `cc`, `jq`
- Git submodules initialized (`third_party/mcpp`)

Commands:

```bash
git submodule update --init --recursive
make -C c
./tools/release-build --version 1.0.0
```

Expected outputs:

- Versioned payload under `dist/protoscript2-v1.0.0-<platform>/`
- Release manifest with hashes and tool versions at `dist/manifest.json`
- Validation logs under `reports/test-orchestrator/` and `reports/release-build/`

## Release Artifact Contents

`tools/release-build` currently publishes:

- `bin/ps`
- `bin/pscc`
- `bin/protoscriptc`
- `manifest.json` metadata
- `tests/manifest.json` and `tests/spec_refs.json` snapshot for traceability
