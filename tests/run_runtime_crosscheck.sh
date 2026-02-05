#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TESTS_DIR="$ROOT_DIR/tests"
MANIFEST="$TESTS_DIR/manifest.json"
COMPILER="${COMPILER:-$ROOT_DIR/bin/protoscriptc}"

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
if ! command -v gcc >/dev/null 2>&1; then
  echo "ERROR: gcc is required for runtime crosscheck." >&2
  exit 2
fi

pass=0
fail=0

echo "== Runtime Crosscheck (Node runtime vs C runtime) =="
echo "Compiler: $COMPILER"
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
  if [[ "$status" != "reject-runtime" && "$status" != "accept-runtime" ]]; then
    continue
  fi

  expected_code="$(jq -r '.error_code // empty' "$expect")"
  expected_cat="$(jq -r '.category // empty' "$expect")"
  expected_stdout="$(jq -r '.expected_stdout // empty' "$expect")"

  out_node="$(mktemp)"
  out_c="$(mktemp)"
  c_file="$(mktemp /tmp/ps_cross_XXXXXX)"
  c_bin="$(mktemp)"

  ok=true

  set +e
  "$COMPILER" --run "$src" >"$out_node" 2>&1
  rc_node=$?
  "$COMPILER" --emit-c "$src" >"$c_file" 2>"$out_c"
  rc_emit_c=$?
  rc_gcc=0
  rc_c=0
  if [[ $rc_emit_c -eq 0 ]]; then
    gcc -std=c11 -x c -w "$c_file" -o "$c_bin" >>"$out_c" 2>&1
    rc_gcc=$?
    if [[ $rc_gcc -eq 0 ]]; then
      "$c_bin" >>"$out_c" 2>&1
      rc_c=$?
    fi
  fi
  set -e

  [[ $rc_emit_c -ne 0 ]] && ok=false
  [[ $rc_gcc -ne 0 ]] && ok=false
  if [[ "$status" == "reject-runtime" ]]; then
    [[ $rc_node -eq 0 ]] && ok=false
    [[ $rc_c -eq 0 ]] && ok=false
    grep -Fq "$expected_code" "$out_node" || ok=false
    grep -Fq "$expected_cat" "$out_node" || ok=false
    grep -Fq "$expected_code" "$out_c" || ok=false
    grep -Fq "$expected_cat" "$out_c" || ok=false
  else
    [[ $rc_node -ne 0 ]] && ok=false
    [[ $rc_c -ne 0 ]] && ok=false
    if [[ -n "$expected_stdout" ]]; then
      [[ "$(cat "$out_node")" != "$expected_stdout" ]] && ok=false
      [[ "$(cat "$out_c")" != "$expected_stdout" ]] && ok=false
    fi
  fi

  if [[ "$ok" == true ]]; then
    echo "PASS $case_id"
    pass=$((pass + 1))
  else
    echo "FAIL $case_id"
    if [[ "$status" == "reject-runtime" ]]; then
      echo "  expected runtime parity on code/category $expected_code / $expected_cat"
    else
      echo "  expected runtime parity on stdout"
    fi
    echo "  --run output:"
    sed 's/^/    /' "$out_node"
    echo "  --emit-c/compiled output:"
    sed 's/^/    /' "$out_c"
    fail=$((fail + 1))
  fi

  rm -f "$out_node" "$out_c" "$c_file" "$c_bin"
done < <(jq -r '.suites | to_entries[] | .value[]' "$MANIFEST")

echo
echo "Summary: PASS=$pass FAIL=$fail TOTAL=$((pass + fail))"
if [[ "$fail" -ne 0 ]]; then
  rm -f "$TESTS_DIR/.runtime_crosscheck_passed"
  exit 1
fi
date -u +"%Y-%m-%dT%H:%M:%SZ" > "$TESTS_DIR/.runtime_crosscheck_passed"
echo "Runtime crosscheck PASSED."
