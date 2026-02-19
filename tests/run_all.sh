#!/usr/bin/env bash
set -euo pipefail

RUN_ROBUST=0
for arg in "$@"; do
  case "$arg" in
    --robust)
      RUN_ROBUST=1
      ;;
    *)
      echo "ERROR: unsupported option '$arg' (supported: --robust)" >&2
      exit 2
      ;;
  esac
done

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ASAN_STAMP="$ROOT_DIR/tests/.asan_build"

echo "== ProtoScript V2 Full Test Run =="
echo

echo "-- build C toolchain (pscc + ps)"
if [[ -f "$ASAN_STAMP" ]]; then
  echo "note: ASAN build detected; cleaning C artifacts"
  make -C "$ROOT_DIR/c" clean
  rm -f "$ASAN_STAMP"
fi
make -C "$ROOT_DIR/c"
echo

echo "-- conformance (Node, modules enabled)"
CONFORMANCE_MODULES=1 "$ROOT_DIR/tests/run_conformance.sh"
echo

echo "-- crosscheck (Node ↔ C, strict AST + static C)"
"$ROOT_DIR/tests/run_node_c_crosscheck.sh" --strict-ast --strict-static-c
echo

echo "-- strict diagnostics (format + suggestions + JS/C parity)"
"$ROOT_DIR/tests/run_diagnostics_strict.sh"
echo

echo "-- CLI autonomy guard (no delegation in c/ps)"
"$ROOT_DIR/tests/run_cli_autonomy_guard.sh"
echo

echo "-- WASM runtime parity (Node oracle vs C runtime in WASM)"
"$ROOT_DIR/tests/run_wasm_runtime_parity.sh"
echo

echo "-- CLI runtime parity (Node --run vs c/ps run)"
"$ROOT_DIR/tests/run_cli_runtime_parity.sh"
echo

echo "-- CLI tests (C)"
"$ROOT_DIR/tests/run_cli_tests.sh"
echo

echo "-- Debug module tests"
"$ROOT_DIR/tests/debug/run_debug_tests.sh"
echo

if [[ "$RUN_ROBUST" == "1" ]]; then
  echo "-- robustness (ASAN + determinism)"
  "$ROOT_DIR/tests/run_robustness.sh"
  echo
fi

echo "ALL OK"
