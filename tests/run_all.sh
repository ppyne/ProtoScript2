#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

echo "== ProtoScript V2 Full Test Run =="
echo

echo "-- build C toolchain (pscc + ps)"
make -C "$ROOT_DIR/c"
echo

echo "-- conformance (Node, modules enabled)"
CONFORMANCE_MODULES=1 "$ROOT_DIR/tests/run_conformance.sh"
echo

echo "-- crosscheck (Node ↔ C, strict AST + static C)"
"$ROOT_DIR/tests/run_node_c_crosscheck.sh" --strict-ast --strict-static-c
echo

echo "-- CLI tests (C)"
"$ROOT_DIR/tests/run_cli_tests.sh"
echo

echo "ALL OK"
