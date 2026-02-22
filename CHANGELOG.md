# Changelog

## v1.0.0

### Notable Guarantees

- Single validation entrypoint is standardized: `./tools/test-orchestrator`.
- Cross-path validation is enforced across Node, C runtime/CLI, and emit-c compiled execution paths.
- Sanitizer visibility is integrated in the validation flow (smoke in PR profile, full robustness in nightly/release hardening).
- Spec/manual traceability is enforced with required-suite checks and unresolved reference detection.
- Performance regression policy is active with baseline comparison and superlinear/pathological slowdown detection.

### Included Release Tooling

- `tools/release-build` now produces versioned release payloads under `dist/`.
- `dist/manifest.json` includes:
  - artifact SHA-256 hashes
  - tool versions
  - executed gate commands and exit status
  - commit/platform metadata

### Known Limitations

- `pscc` remains partially dependent on Node compiler forwarding for some outputs (`--emit-c`, `--emit-ir`).
- WASM path is not Tier 1 and is still treated as experimental for packaging guarantees.
- Fuzzing breadth is improving but does not yet claim complete grammar/runtime state-space coverage.
- Benchmark suite is regression-oriented; it does not claim absolute throughput certification across machines.

### Performance Benchmarking Notes

- emit-c runtime benchmarks are now split:
  - `emit-c-compile` metric for generation+compilation time
  - `emit-c-runtime` metric for execution-only timing (compile once, run N times)

### Stability Scope for v1.x

- CORE language semantics and STDLIB contracts covered by normative suites are stable for v1.x.
- Experimental surfaces (WASM packaging, benchmark corpus shape, release payload layout details) may evolve within v1.x.
