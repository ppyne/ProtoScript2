# Performance Policy (Phase 3)

## Scope

Performance checks are orchestrated via:

- `tools/bench-runner`

Benchmarks are organized under:

- `tests/benchmarks/micro`
- `tests/benchmarks/stdlib`
- `tests/benchmarks/compiler`
- `tests/benchmarks/runtime`

## Methodology

- No absolute wall-clock thresholds are enforced.
- Metrics are relative and median-based.
- Paths measured:
  - Node runtime/compiler path
  - C runtime/compiler path
  - emit-c compiled path

## Regression Rules

Configured by `tools/bench-runner`:

- baseline regression factor (default `3.0x`)
- pathological slowdown factor (default `10.0x`)
- superlinear growth factor for scale-paired tests (default `4.0x` for 2x input scale)

These are intended to catch major regressions and complexity pathologies, not small hardware noise.

## Baseline Management

- Baseline file: `tests/benchmarks/baseline.json`
- Update intentionally with:
  - `tools/bench-runner --full --update-baseline`
- Baseline updates must be reviewed with accompanying rationale.

## CI Usage

- PR: `tools/bench-runner --quick` via orchestrator `--ci-pr`
- Nightly: `tools/bench-runner --full` via orchestrator `--ci-nightly`
