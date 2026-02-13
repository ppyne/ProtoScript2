#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/tests/debug"
EXPECTED="$OUT_DIR/golden/debug_expected.txt"
TMP_C_OUT="$(mktemp)"
TMP_NODE_OUT="$(mktemp)"
TMP_BIN="$(mktemp)"

cc -std=c11 -Wall -Wextra -Werror -O2 -Dps_module_init=ps_module_init_JSON \
  -I"$ROOT_DIR/include" -I"$ROOT_DIR/c" \
  "$OUT_DIR/test_debug.c" \
  "$ROOT_DIR/tests/modules_src/json.c" \
  "$ROOT_DIR/c/diag.c" \
  "$ROOT_DIR/c/runtime/ps_api.c" \
  "$ROOT_DIR/c/runtime/ps_errors.c" \
  "$ROOT_DIR/c/runtime/ps_heap.c" \
  "$ROOT_DIR/c/runtime/ps_value.c" \
  "$ROOT_DIR/c/runtime/ps_string.c" \
  "$ROOT_DIR/c/runtime/ps_list.c" \
  "$ROOT_DIR/c/runtime/ps_object.c" \
  "$ROOT_DIR/c/runtime/ps_map.c" \
  "$ROOT_DIR/c/runtime/ps_json.c" \
  "$ROOT_DIR/c/runtime/ps_modules.c" \
  "$ROOT_DIR/c/runtime/ps_dynlib_posix.c" \
  "$ROOT_DIR/c/runtime/ps_vm.c" \
  "$ROOT_DIR/c/modules/debug.c" \
  -o "$TMP_BIN"

"$TMP_BIN" 2>"$TMP_C_OUT"
node "$OUT_DIR/test_debug_node.js" 2>"$TMP_NODE_OUT"

if ! diff -u "$EXPECTED" "$TMP_C_OUT" >/dev/null; then
  echo "FAIL C debug output mismatch" >&2
  diff -u "$EXPECTED" "$TMP_C_OUT" >&2 || true
  exit 1
fi

if ! diff -u "$EXPECTED" "$TMP_NODE_OUT" >/dev/null; then
  echo "FAIL Node debug output mismatch" >&2
  diff -u "$EXPECTED" "$TMP_NODE_OUT" >&2 || true
  exit 1
fi

rm -f "$TMP_C_OUT" "$TMP_NODE_OUT" "$TMP_BIN"
echo "PASS Debug module tests"
