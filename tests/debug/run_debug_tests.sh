#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/tests/debug"
EXPECTED="$OUT_DIR/golden/debug_expected.txt"
EXPECTED_VARIADIC="$OUT_DIR/golden/variadic_type_dump_expected.txt"
EXPECTED_BUILTIN_HANDLES="$OUT_DIR/golden/debug_builtin_handles_expected.txt"
EXPECTED_C_BUILTIN_HANDLES="$OUT_DIR/golden/debug_c_builtin_handles_expected.txt"
EXPECTED_CLI_BUILTIN_HANDLES="$OUT_DIR/golden/debug_builtin_handles_cli_expected.txt"
VARIADIC_SRC="$OUT_DIR/variadic_type_dump.pts"
BUILTIN_HANDLES_SRC="$OUT_DIR/test_debug_node_builtin_handles.js"
CLI_BUILTIN_HANDLES_SRC="$OUT_DIR/debug_builtin_handles_cli.pts"
TMP_C_OUT="$(mktemp)"
TMP_NODE_OUT="$(mktemp)"
TMP_BIN="$(mktemp)"
TMP_VARIADIC_NODE="$(mktemp)"
TMP_VARIADIC_C="$(mktemp)"
TMP_BUILTIN_HANDLES_NODE="$(mktemp)"
TMP_C_BUILTIN_OUT="$(mktemp)"
TMP_C_BUILTIN_BIN="$(mktemp)"
TMP_CLI_BUILTIN_NODE="$(mktemp)"
TMP_CLI_BUILTIN_C="$(mktemp)"
DEBUG_TESTS_MODULES="${DEBUG_TESTS_MODULES:-1}"
MODULES_TMP_DIR=""

if [[ "$DEBUG_TESTS_MODULES" == "1" ]]; then
  export PS_MODULE_REGISTRY="${PS_MODULE_REGISTRY:-$ROOT_DIR/modules/registry.json}"
  if [[ -z "${PS_MODULE_PATH:-}" ]]; then
    MODULES_TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/ps_modules_debug_tests_XXXXXX")"
    export PS_MODULE_PATH="$MODULES_TMP_DIR"
  fi
  if [[ -x "$ROOT_DIR/scripts/build_modules.sh" ]]; then
    "$ROOT_DIR/scripts/build_modules.sh" >/tmp/ps_debug_tests_modules_build.out 2>&1 || {
      echo "FAIL building modules for debug tests" >&2
      sed -n '1,80p' /tmp/ps_debug_tests_modules_build.out >&2 || true
      exit 1
    }
  else
    echo "FAIL missing module build script: $ROOT_DIR/scripts/build_modules.sh" >&2
    exit 1
  fi
fi

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

"$ROOT_DIR/bin/protoscriptc" --run "$VARIADIC_SRC" >"$TMP_VARIADIC_NODE" 2>&1
"$ROOT_DIR/c/ps" run "$VARIADIC_SRC" >"$TMP_VARIADIC_C" 2>&1

if ! diff -u "$EXPECTED_VARIADIC" "$TMP_VARIADIC_NODE" >/dev/null; then
  echo "FAIL Node variadic debug typing mismatch" >&2
  diff -u "$EXPECTED_VARIADIC" "$TMP_VARIADIC_NODE" >&2 || true
  exit 1
fi

if ! diff -u "$EXPECTED_VARIADIC" "$TMP_VARIADIC_C" >/dev/null; then
  echo "FAIL C variadic debug typing mismatch" >&2
  diff -u "$EXPECTED_VARIADIC" "$TMP_VARIADIC_C" >&2 || true
  exit 1
fi

node "$BUILTIN_HANDLES_SRC" 2>"$TMP_BUILTIN_HANDLES_NODE"

if ! diff -u "$EXPECTED_BUILTIN_HANDLES" "$TMP_BUILTIN_HANDLES_NODE" >/dev/null; then
  echo "FAIL Node builtin handles debug mismatch" >&2
  diff -u "$EXPECTED_BUILTIN_HANDLES" "$TMP_BUILTIN_HANDLES_NODE" >&2 || true
  exit 1
fi

cc -std=c11 -Wall -Wextra -Werror -O2 -Dps_module_init=ps_module_init_Fs \
  -I"$ROOT_DIR/include" -I"$ROOT_DIR/c" \
  "$OUT_DIR/test_debug_c_builtin_handles.c" \
  "$ROOT_DIR/tests/modules_src/fs.c" \
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
  -o "$TMP_C_BUILTIN_BIN"

"$TMP_C_BUILTIN_BIN" 2>"$TMP_C_BUILTIN_OUT"
if ! diff -u "$EXPECTED_C_BUILTIN_HANDLES" "$TMP_C_BUILTIN_OUT" >/dev/null; then
  echo "FAIL C builtin handles debug mismatch" >&2
  diff -u "$EXPECTED_C_BUILTIN_HANDLES" "$TMP_C_BUILTIN_OUT" >&2 || true
  exit 1
fi

make -C "$ROOT_DIR/c" ps >/tmp/ps_debug_tests_make_ps.out 2>&1 || {
  echo "FAIL rebuilding c/ps for CLI debug tests" >&2
  cat /tmp/ps_debug_tests_make_ps.out >&2 || true
  exit 1
}

"$ROOT_DIR/bin/protoscriptc" --run "$CLI_BUILTIN_HANDLES_SRC" >"$TMP_CLI_BUILTIN_NODE" 2>&1
"$ROOT_DIR/c/ps" run "$CLI_BUILTIN_HANDLES_SRC" >"$TMP_CLI_BUILTIN_C" 2>&1

if ! diff -u "$EXPECTED_CLI_BUILTIN_HANDLES" "$TMP_CLI_BUILTIN_NODE" >/dev/null; then
  echo "FAIL Node CLI builtin handles debug mismatch" >&2
  diff -u "$EXPECTED_CLI_BUILTIN_HANDLES" "$TMP_CLI_BUILTIN_NODE" >&2 || true
  exit 1
fi

if ! diff -u "$EXPECTED_CLI_BUILTIN_HANDLES" "$TMP_CLI_BUILTIN_C" >/dev/null; then
  echo "FAIL C CLI builtin handles debug mismatch" >&2
  diff -u "$EXPECTED_CLI_BUILTIN_HANDLES" "$TMP_CLI_BUILTIN_C" >&2 || true
  exit 1
fi

rm -f "$TMP_C_OUT" "$TMP_NODE_OUT" "$TMP_BIN" "$TMP_VARIADIC_NODE" "$TMP_VARIADIC_C" \
  "$TMP_BUILTIN_HANDLES_NODE" "$TMP_C_BUILTIN_OUT" "$TMP_C_BUILTIN_BIN" \
  "$TMP_CLI_BUILTIN_NODE" "$TMP_CLI_BUILTIN_C" /tmp/ps_debug_tests_make_ps.out \
  /tmp/ps_debug_tests_modules_build.out
if [[ -n "$MODULES_TMP_DIR" && -d "$MODULES_TMP_DIR" ]]; then
  rm -rf "$MODULES_TMP_DIR"
fi
echo "PASS Debug module tests"
