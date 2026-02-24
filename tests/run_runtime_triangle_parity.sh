#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TESTS_DIR="$ROOT_DIR/tests"
MANIFEST="$TESTS_DIR/manifest.json"
COMPILER="${COMPILER:-$ROOT_DIR/bin/protoscriptc}"
PS="${PS:-$ROOT_DIR/c/ps}"
WASM_JS="${WASM_JS:-$ROOT_DIR/web/protoscript.js}"
WASM_CASE_RUNNER="${WASM_CASE_RUNNER:-$ROOT_DIR/tests/wasm/run_wasm_case.js}"
TRIANGLE_PARITY_MODULES="${TRIANGLE_PARITY_MODULES:-1}"
TRIANGLE_GCC_FLAGS="${TRIANGLE_GCC_FLAGS:-}"

MODULES_BUILT=0
MODULES_TMP_DIR=""

if ! command -v jq >/dev/null 2>&1; then
  if [[ -x "/usr/local/bin/jq" ]]; then
    PATH="/usr/local/bin:$PATH"
  elif [[ -x "/opt/local/bin/jq" ]]; then
    PATH="/opt/local/bin:$PATH"
  fi
fi

if ! command -v jq >/dev/null 2>&1; then
  echo "ERROR: jq is required." >&2
  exit 2
fi
if ! command -v node >/dev/null 2>&1; then
  echo "ERROR: node is required." >&2
  exit 2
fi
if ! command -v gcc >/dev/null 2>&1; then
  echo "ERROR: gcc is required." >&2
  exit 2
fi
if [[ ! -x "$COMPILER" ]]; then
  echo "ERROR: missing compiler: $COMPILER" >&2
  exit 2
fi
if [[ ! -x "$PS" ]]; then
  echo "ERROR: missing CLI runtime: $PS" >&2
  exit 2
fi
if [[ ! -f "$WASM_JS" ]]; then
  echo "ERROR: missing wasm module: $WASM_JS" >&2
  exit 2
fi
if [[ ! -f "$WASM_CASE_RUNNER" ]]; then
  echo "ERROR: missing wasm case runner: $WASM_CASE_RUNNER" >&2
  exit 2
fi

if [[ "$TRIANGLE_PARITY_MODULES" == "1" ]]; then
  export PS_MODULE_REGISTRY="${PS_MODULE_REGISTRY:-$ROOT_DIR/modules/registry.json}"
  if [[ -z "${PS_MODULE_PATH:-}" ]]; then
    MODULES_TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/ps_modules_triangle_XXXXXX")"
    export PS_MODULE_PATH="$MODULES_TMP_DIR"
  fi
fi

cleanup_modules_tmp() {
  if [[ -n "$MODULES_TMP_DIR" && -d "$MODULES_TMP_DIR" ]]; then
    rm -rf "$MODULES_TMP_DIR"
  fi
}
trap cleanup_modules_tmp EXIT

# Allowed normalization list (strict):
# - volatile temporary filename token: ps_<uuid>_<n>
# - preprocessor temp path: /tmp/ps_mcpp_input_<n>.txt
# - source file path prefix only (line/column preserved)
normalize_stream() {
  local src="$1"
  local dst="$2"
  sed -E \
    -e 's#([A-Za-z0-9_./-]*/)?ps_[0-9a-fA-F-]+_[0-9]+#ps_tmp#g' \
    -e 's#/tmp/ps_mcpp_input_[0-9]+\.txt#/tmp/ps_mcpp_input_tmp.txt#g' \
    -e "s#^(/tmp/ps_mcpp_input_tmp\\.txt|ps_tmp|$ROOT_DIR/tests/[^:]+\\.pts|.*/tests/[^:]+\\.pts|tests/[^:]+\\.pts):#__SRC__:#" \
    "$src" >"$dst"
}

print_diff_minimal() {
  local a="$1"
  local b="$2"
  diff -u "$a" "$b" | sed -n '1,80p' || true
}

pass=0
fail=0
skip=0

echo "== Runtime Triangle Parity (VM C native ↔ WASM ↔ emit-C) =="
echo "Node compiler: $COMPILER"
echo "CLI runtime:   $PS"
echo "WASM module:   $WASM_JS"
echo

while IFS= read -r case_id; do
  [[ -z "$case_id" ]] && continue

  src="$TESTS_DIR/$case_id.pts"
  expect="$TESTS_DIR/$case_id.expect.json"

  if [[ ! -f "$src" || ! -f "$expect" ]]; then
    echo "FAIL $case_id"
    echo "  missing source or expectation file"
    fail=$((fail + 1))
    continue
  fi

  status="$(jq -r '.status // empty' "$expect")"
  if [[ "$status" != "accept-runtime" && "$status" != "reject-runtime" ]]; then
    continue
  fi

  requires_modules="$(jq -r '.requires | index("modules") // empty' "$expect")"
  if [[ -n "$requires_modules" && "$TRIANGLE_PARITY_MODULES" != "1" ]]; then
    echo "SKIP $case_id (modules not enabled)"
    skip=$((skip + 1))
    continue
  fi

  if [[ -n "$requires_modules" && "$TRIANGLE_PARITY_MODULES" == "1" && "$MODULES_BUILT" == "0" ]]; then
    if [[ -x "$ROOT_DIR/scripts/build_modules.sh" ]]; then
      "$ROOT_DIR/scripts/build_modules.sh" >/tmp/ps_triangle_modules_build.out 2>&1 || {
        echo "FAIL $case_id"
        echo "  failed to build test modules"
        sed -n '1,80p' /tmp/ps_triangle_modules_build.out
        fail=$((fail + 1))
        continue
      }
      MODULES_BUILT=1
    else
      echo "FAIL $case_id"
      echo "  module build script missing"
      fail=$((fail + 1))
      continue
    fi
  fi

  out_cli="$(mktemp)"
  err_cli="$(mktemp)"
  out_wasm="$(mktemp)"
  err_wasm="$(mktemp)"
  out_emit="$(mktemp)"
  err_emit="$(mktemp)"
  err_emit_build="$(mktemp)"

  out_cli_norm="$(mktemp)"
  err_cli_norm="$(mktemp)"
  out_wasm_norm="$(mktemp)"
  err_wasm_norm="$(mktemp)"
  out_emit_norm="$(mktemp)"
  err_emit_norm="$(mktemp)"

  # BSD mktemp (macOS) does not support suffixes in templates portably.
  tmp_emit_base="$(mktemp "${TMPDIR:-/tmp}/ps_triangle_emitc_XXXXXX")"
  tmp_c="${tmp_emit_base}.c"
  tmp_bin="${tmp_emit_base}.bin"
  rm -f "$tmp_c" "$tmp_bin"

  set +e
  "$PS" run "$src" >"$out_cli" 2>"$err_cli"
  rc_cli=$?

  node "$WASM_CASE_RUNNER" "$WASM_JS" "$src" "$out_wasm" "$err_wasm"
  rc_wasm=$?

  "$COMPILER" --emit-c "$src" >"$tmp_c" 2>"$err_emit_build"
  rc_emit_gen=$?
  rc_emit_cc=0
  rc_emit=0
  if [[ $rc_emit_gen -eq 0 ]]; then
    gcc -std=c11 -x c -w $TRIANGLE_GCC_FLAGS "$tmp_c" -o "$tmp_bin" >>"$err_emit_build" 2>&1
    rc_emit_cc=$?
    if [[ $rc_emit_cc -eq 0 ]]; then
      "$tmp_bin" >"$out_emit" 2>"$err_emit"
      rc_emit=$?
    fi
  fi
  set -e

  ok=true
  reason=""

  if [[ $rc_emit_gen -ne 0 || $rc_emit_cc -ne 0 ]]; then
    ok=false
    reason="emit-c compile pipeline failed (emit=$rc_emit_gen gcc=$rc_emit_cc)"
  else
    normalize_stream "$out_cli" "$out_cli_norm"
    normalize_stream "$err_cli" "$err_cli_norm"
    normalize_stream "$out_wasm" "$out_wasm_norm"
    normalize_stream "$err_wasm" "$err_wasm_norm"
    normalize_stream "$out_emit" "$out_emit_norm"
    normalize_stream "$err_emit" "$err_emit_norm"

    if [[ "$rc_emit" -ne 0 ]] && grep -qE '^<runtime>:1:1 [RE][0-9]+' "$err_emit_norm"; then
      ok=false
      reason="emit-c lost source mapping (<runtime>:1:1)"
    elif [[ "$rc_cli" -ne "$rc_wasm" || "$rc_cli" -ne "$rc_emit" ]]; then
      ok=false
      reason="exit code diverges (cli=$rc_cli wasm=$rc_wasm emit-c=$rc_emit)"
    elif ! cmp -s "$out_cli_norm" "$out_wasm_norm"; then
      ok=false
      reason="stdout diverges (cli vs wasm)"
    elif ! cmp -s "$out_cli_norm" "$out_emit_norm"; then
      ok=false
      reason="stdout diverges (cli vs emit-c)"
    elif ! cmp -s "$err_cli_norm" "$err_wasm_norm"; then
      ok=false
      reason="stderr diverges (cli vs wasm)"
    elif ! cmp -s "$err_cli_norm" "$err_emit_norm"; then
      ok=false
      reason="stderr diverges (cli vs emit-c)"
    fi
  fi

  if [[ "$ok" == true ]]; then
    echo "PASS $case_id"
    pass=$((pass + 1))
  else
    echo "FAIL $case_id"
    echo "  $reason"
    echo "  rc cli=$rc_cli wasm=$rc_wasm emit-c=$rc_emit"
    if [[ "$reason" == emit-c* ]]; then
      sed -n '1,80p' "$err_emit_build"
    elif [[ "$reason" == *"stdout diverges (cli vs wasm)"* ]]; then
      print_diff_minimal "$out_cli_norm" "$out_wasm_norm"
    elif [[ "$reason" == *"stdout diverges (cli vs emit-c)"* ]]; then
      print_diff_minimal "$out_cli_norm" "$out_emit_norm"
    elif [[ "$reason" == *"stderr diverges (cli vs wasm)"* ]]; then
      print_diff_minimal "$err_cli_norm" "$err_wasm_norm"
    elif [[ "$reason" == *"stderr diverges (cli vs emit-c)"* ]]; then
      print_diff_minimal "$err_cli_norm" "$err_emit_norm"
    else
      echo "  cli stderr:"; sed -n '1,60p' "$err_cli"
      echo "  wasm stderr:"; sed -n '1,60p' "$err_wasm"
      echo "  emit-c stderr:"; sed -n '1,60p' "$err_emit"
    fi
    fail=$((fail + 1))
  fi

  rm -f "$out_cli" "$err_cli" "$out_wasm" "$err_wasm" "$out_emit" "$err_emit" "$err_emit_build" \
    "$out_cli_norm" "$err_cli_norm" "$out_wasm_norm" "$err_wasm_norm" "$out_emit_norm" "$err_emit_norm" \
    "$tmp_c" "$tmp_bin"
done < <(jq -r '.suites | to_entries[] | .value[]' "$MANIFEST")

echo
echo "Summary: PASS=$pass FAIL=$fail SKIP=$skip TOTAL=$((pass + fail + skip))"
if [[ "$fail" -ne 0 ]]; then
  exit 1
fi

echo "Runtime triangle parity PASSED."
