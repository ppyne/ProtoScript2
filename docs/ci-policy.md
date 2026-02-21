# CI Policy (Phase 3)

## Objectives

- Keep `tools/test-orchestrator` as the single entry point.
- Enforce stable PR gating with reasonable runtime.
- Run deeper robustness/security/performance checks nightly.

## PR Policy

PR CI runs:

- `./tools/test-orchestrator --ci-pr --summary`
- with `FUZZ_REQUIRE_ENGINE=1` in CI environment

Profile includes:

- orchestrator prerequisite guard
- spec/manual traceability check + coverage generation
- sanitizer smoke pass (C + emit-c sanitizer policy)
- short fuzz pass (ASan/UBSan + libFuzzer)
- quick benchmark pass with relative regression checks

If any step fails, PR CI fails.

## Nightly Policy

Nightly CI runs:

- `./tools/test-orchestrator --full --summary`
- `./tools/test-orchestrator --traceability --fuzz-nightly --bench-full --summary`
- with `FUZZ_REQUIRE_ENGINE=1` in CI environment

Profile includes:

- full orchestrator baseline (`run_all`, conformance, parity, robustness)
- spec/manual traceability check + coverage generation
- long fuzz pass
- full benchmark pass

## Artifacts

CI stores at least:

- `reports/test-orchestrator/**`
- `reports/fuzz/**`
- `reports/benchmarks/**`
- `docs/test-baseline.json`
- `docs/spec-test-coverage.md`

## Sanitizer Requirement

- At least one sanitizer-enabled C/emit-c pass is mandatory on PR (`sanitizer_smoke`).
- Full sanitizer robust flow is mandatory nightly (`run_robustness.sh` through orchestrator).
