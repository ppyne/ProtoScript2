#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TESTS_DIR="$ROOT_DIR/tests"
MANIFEST="$TESTS_DIR/opt-safety/manifest.json"
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
  echo "ERROR: gcc is required for opt safety checks." >&2
  exit 2
fi
if [[ ! -f "$TESTS_DIR/.conformance_passed" ]]; then
  echo "ERROR: optimization gate closed (missing tests/.conformance_passed)." >&2
  echo "Run full conformance first (no skip)." >&2
  exit 2
fi
if [[ "${BACKEND_C_STABLE:-0}" != "1" ]]; then
  echo "ERROR: set BACKEND_C_STABLE=1 to run opt safety." >&2
  exit 2
fi

pass=0
fail=0

contains_all() {
  local file="$1"
  shift
  for needle in "$@"; do
    if [[ -n "$needle" ]] && ! grep -Fq "$needle" "$file"; then
      return 1
    fi
  done
  return 0
}

contains_none() {
  local file="$1"
  shift
  for needle in "$@"; do
    if [[ -n "$needle" ]] && grep -Fq "$needle" "$file"; then
      return 1
    fi
  done
  return 0
}

echo "== Optimization Safety Runner =="
echo "Compiler: $COMPILER"
echo

while IFS= read -r raw; do
  id="$(jq -r '.id' <<<"$raw")"
  kind="$(jq -r '.kind' <<<"$raw")"
  src_rel="$(jq -r '.source // ("opt-safety/" + .id + ".pts")' <<<"$raw")"
  src="$TESTS_DIR/$src_rel"

  if [[ ! -f "$src" ]]; then
    echo "FAIL $id"
    echo "  missing source: $src_rel"
    fail=$((fail + 1))
    continue
  fi

  if [[ "$kind" == "reject-static" ]]; then
    expected_code="$(jq -r '.expected_error_code' <<<"$raw")"
    expected_cat="$(jq -r '.expected_category' <<<"$raw")"
    out_check="$(mktemp)"
    out_opt="$(mktemp)"
    rc1=0
    rc2=0
    set +e
    "$COMPILER" --check "$src" >"$out_check" 2>&1
    rc1=$?
    "$COMPILER" --emit-ir "$src" --opt >"$out_opt" 2>&1
    rc2=$?
    set -e
    ok=true
    [[ $rc1 -eq 0 ]] && ok=false
    [[ $rc2 -eq 0 ]] && ok=false
    grep -Fq "$expected_code" "$out_check" || ok=false
    grep -Fq "$expected_code" "$out_opt" || ok=false
    grep -Fq "$expected_cat" "$out_check" || ok=false
    grep -Fq "$expected_cat" "$out_opt" || ok=false
    if [[ "$ok" == true ]]; then
      echo "PASS $id"
      pass=$((pass + 1))
    else
      echo "FAIL $id"
      echo "  diagnostics mismatch between --check and --emit-ir --opt"
      echo "  --check:"
      sed 's/^/    /' "$out_check"
      echo "  --emit-ir --opt:"
      sed 's/^/    /' "$out_opt"
      fail=$((fail + 1))
    fi
    rm -f "$out_check" "$out_opt"
    continue
  fi

  if [[ "$kind" == "accept" ]]; then
    expected_exit="$(jq -r '.expected_exit' <<<"$raw")"
    before_contains=()
    while IFS= read -r line; do
      [[ -z "$line" ]] && continue
      before_contains+=("$line")
    done < <(jq -r '.ir_before_must_contain[]? // empty' <<<"$raw")

    after_contains=()
    while IFS= read -r line; do
      [[ -z "$line" ]] && continue
      after_contains+=("$line")
    done < <(jq -r '.ir_after_must_contain[]? // empty' <<<"$raw")

    after_not_contains=()
    while IFS= read -r line; do
      [[ -z "$line" ]] && continue
      after_not_contains+=("$line")
    done < <(jq -r '.ir_after_must_not_contain[]? // empty' <<<"$raw")

    ir_before="$(mktemp)"
    ir_after="$(mktemp)"
    c_before="$(mktemp /tmp/ps_opt_before_XXXXXX)"
    c_after="$(mktemp /tmp/ps_opt_after_XXXXXX)"
    bin_before="$(mktemp)"
    bin_after="$(mktemp)"

    ok=true
    set +e
    "$COMPILER" --check "$src" >/dev/null 2>&1
    rc_check=$?
    "$COMPILER" --emit-ir "$src" >"$ir_before" 2>&1
    rc_ir_before=$?
    "$COMPILER" --emit-ir "$src" --opt >"$ir_after" 2>&1
    rc_ir_after=$?
    "$COMPILER" --emit-c "$src" >"$c_before" 2>&1
    rc_c_before=$?
    "$COMPILER" --emit-c "$src" --opt >"$c_after" 2>&1
    rc_c_after=$?
    gcc -std=c11 -x c -w "$c_before" -o "$bin_before"
    rc_gcc_before=$?
    gcc -std=c11 -x c -w "$c_after" -o "$bin_after"
    rc_gcc_after=$?
    "$bin_before" >/dev/null 2>&1
    rc_run_before=$?
    "$bin_after" >/dev/null 2>&1
    rc_run_after=$?
    set -e

    [[ $rc_check -ne 0 ]] && ok=false
    [[ $rc_ir_before -ne 0 ]] && ok=false
    [[ $rc_ir_after -ne 0 ]] && ok=false
    [[ $rc_c_before -ne 0 ]] && ok=false
    [[ $rc_c_after -ne 0 ]] && ok=false
    [[ $rc_gcc_before -ne 0 ]] && ok=false
    [[ $rc_gcc_after -ne 0 ]] && ok=false
    [[ $rc_run_before -ne $expected_exit ]] && ok=false
    [[ $rc_run_after -ne $expected_exit ]] && ok=false
    if [[ ${#before_contains[@]} -gt 0 ]]; then
      contains_all "$ir_before" "${before_contains[@]}" || ok=false
    fi
    if [[ ${#after_contains[@]} -gt 0 ]]; then
      contains_all "$ir_after" "${after_contains[@]}" || ok=false
    fi
    if [[ ${#after_not_contains[@]} -gt 0 ]]; then
      contains_none "$ir_after" "${after_not_contains[@]}" || ok=false
    fi

    if [[ "$ok" == true ]]; then
      echo "PASS $id"
      pass=$((pass + 1))
    else
      echo "FAIL $id"
      echo "  check/ir/c/exec mismatch (before/after optimization)"
      fail=$((fail + 1))
    fi

    rm -f "$ir_before" "$ir_after" "$c_before" "$c_after" "$bin_before" "$bin_after"
    continue
  fi

  echo "FAIL $id"
  echo "  unsupported kind: $kind"
  fail=$((fail + 1))
done < <(jq -c '.cases[]' "$MANIFEST")

echo
echo "Summary: PASS=$pass FAIL=$fail TOTAL=$((pass + fail))"
if [[ "$fail" -ne 0 ]]; then
  exit 1
fi
echo "Optimization safety PASSED."
