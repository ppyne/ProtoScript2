#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

ASAN_CFLAGS="-std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address -fno-omit-frame-pointer -I../include -I."
ASAN_GCC_FLAGS="-fsanitize=address -fno-omit-frame-pointer"

export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"
ASAN_STAMP="$ROOT_DIR/tests/.asan_build"
date -u +"%Y-%m-%dT%H:%M:%SZ" > "$ASAN_STAMP"

echo "== ProtoScript V2 Robustness Pass (ASAN + determinism) =="
echo

echo "-- build C toolchain with ASAN"
make -C "$ROOT_DIR/c" clean
make -C "$ROOT_DIR/c" CFLAGS="$ASAN_CFLAGS"
echo

echo "-- determinism (Node compiler)"
"$ROOT_DIR/tests/run_determinism.sh"
echo

echo "-- determinism (C frontend, same process)"
"$ROOT_DIR/tests/run_c_determinism.sh"
echo

echo "-- memory ownership audits (C, ASAN)"
CFLAGS="$ASAN_CFLAGS" "$ROOT_DIR/tests/robustness/run_memory_audit.sh"
echo

echo "-- conformance (Node, modules enabled)"
CONFORMANCE_MODULES=1 "$ROOT_DIR/tests/run_conformance.sh"
echo

echo "-- crosscheck (Node  C, strict AST + static C, emit-c compiled with ASAN)"
CROSSCHECK_GCC_FLAGS="$ASAN_GCC_FLAGS" "$ROOT_DIR/tests/run_node_c_crosscheck.sh" --strict-ast --strict-static-c
echo

echo "-- CLI tests (C, ASAN)"
"$ROOT_DIR/tests/run_cli_tests.sh"
echo

echo "-- repeated-run loop (C, ASAN)"
for i in {1..50}; do
  "$ROOT_DIR/c/ps" run "$ROOT_DIR/stress.pts" >/dev/null
done
echo

echo "-- clean ASAN artifacts (restore normal build state)"
make -C "$ROOT_DIR/c" clean
echo
rm -f "$ASAN_STAMP"

echo "ROBUSTNESS OK"
