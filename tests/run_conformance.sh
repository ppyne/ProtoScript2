#!/usr/bin/env bash
set -euo pipefail

# ProtoScript V2 Conformance Runner
#
# Default compiler integration expects:
#   --check <file>  for parse/static checks
#   --run <file>    for runtime checks
#
# Override command prefixes if needed:
#   CONFORMANCE_CHECK_CMD="my-compiler check"
#   CONFORMANCE_RUN_CMD="my-compiler run"
#
# Usage:
#   tests/run_conformance.sh
#   COMPILER=./bin/protoscript tests/run_conformance.sh
#   CONFORMANCE_CHECK_CMD="./myc --mode check" CONFORMANCE_RUN_CMD="./myc --mode run" tests/run_conformance.sh

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TESTS_DIR="$ROOT_DIR/tests"
MANIFEST="$TESTS_DIR/manifest.json"

COMPILER="${COMPILER:-$ROOT_DIR/bin/protoscriptc}"
CONFORMANCE_CHECK_CMD="${CONFORMANCE_CHECK_CMD:-$COMPILER --check}"
CONFORMANCE_RUN_CMD="${CONFORMANCE_RUN_CMD:-$COMPILER --run}"
FRONTEND_ONLY="${FRONTEND_ONLY:-0}"

if ! command -v jq >/dev/null 2>&1; then
  if [[ -x "/usr/local/bin/jq" ]]; then
    PATH="/usr/local/bin:$PATH"
  elif [[ -x "/opt/local/bin/jq" ]]; then
    PATH="/opt/local/bin:$PATH"
  fi
fi

if ! command -v jq >/dev/null 2>&1; then
  echo "ERROR: jq is required." >&2
  echo "Tried PATH and fallback locations: /usr/local/bin, /opt/local/bin" >&2
  exit 2
fi

if [[ ! -f "$MANIFEST" ]]; then
  echo "ERROR: manifest not found: $MANIFEST" >&2
  exit 2
fi

run_with_prefix() {
  local prefix="$1"
  local src="$2"
  local out_file="$3"
  set +e
  bash -lc "$prefix \"\$1\"" -- "$src" >"$out_file" 2>&1
  local code=$?
  set -e
  return "$code"
}

check_output_contains() {
  local needle="$1"
  local file="$2"
  if [[ -z "$needle" ]]; then
    return 0
  fi
  grep -Fq "$needle" "$file"
}

line_col_match() {
  local line="$1"
  local col="$2"
  local file="$3"
  grep -Eq "(^|[^0-9])${line}:${col}([^0-9]|$)" "$file"
}

pass=0
fail=0

echo "== ProtoScript V2 Conformance Runner =="
echo "Rule: compiler is correct only if 100% normative tests pass."
echo "Check command: $CONFORMANCE_CHECK_CMD"
echo "Run command:   $CONFORMANCE_RUN_CMD"
echo "Frontend only: $FRONTEND_ONLY"
echo

while IFS= read -r case_id; do
  [[ -z "$case_id" ]] && continue
  src="$TESTS_DIR/$case_id.pts"
  expect="$TESTS_DIR/$case_id.expect.json"
  out="$(mktemp)"

  if [[ ! -f "$src" || ! -f "$expect" ]]; then
    echo "FAIL $case_id"
    echo "  missing source or expectation file"
    fail=$((fail + 1))
    rm -f "$out"
    continue
  fi

  status="$(jq -r '.status // empty' "$expect")"
  error_code="$(jq -r '.error_code // empty' "$expect")"
  category="$(jq -r '.category // empty' "$expect")"
  line="$(jq -r '.position.line // empty' "$expect")"
  col="$(jq -r '.position.column // empty' "$expect")"

  if [[ -z "$status" || -z "$error_code" || -z "$category" || -z "$line" || -z "$col" ]]; then
    echo "FAIL $case_id"
    echo "  malformed expectation file: missing required field(s)"
    fail=$((fail + 1))
    rm -f "$out"
    continue
  fi

  rc=0
  case "$status" in
    reject-parse|reject-static)
      if run_with_prefix "$CONFORMANCE_CHECK_CMD" "$src" "$out"; then
        rc=0
      else
        rc=$?
      fi
      ;;
    reject-runtime)
      if [[ "$FRONTEND_ONLY" == "1" ]]; then
        echo "SKIP $case_id (reject-runtime, frontend-only)"
        rm -f "$out"
        continue
      fi
      if run_with_prefix "$CONFORMANCE_RUN_CMD" "$src" "$out"; then
        rc=0
      else
        rc=$?
      fi
      ;;
    *)
      echo "FAIL $case_id"
      echo "  unsupported status in expect: $status"
      fail=$((fail + 1))
      rm -f "$out"
      continue
      ;;
  esac

  ok=true

  # All current normative cases are rejection cases: non-zero is required.
  if [[ "$rc" -eq 0 ]]; then
    ok=false
    reason="expected non-zero exit for $status"
  fi

  if [[ "$ok" == true ]] && ! check_output_contains "$error_code" "$out"; then
    ok=false
    reason="missing error_code '$error_code' in compiler output"
  fi

  if [[ "$ok" == true ]] && ! check_output_contains "$category" "$out"; then
    ok=false
    reason="missing category '$category' in compiler output"
  fi

  if [[ "$ok" == true ]] && ! line_col_match "$line" "$col" "$out"; then
    ok=false
    reason="missing line:column '${line}:${col}' in compiler output"
  fi

  if [[ "$ok" == true ]]; then
    echo "PASS $case_id"
    pass=$((pass + 1))
  else
    echo "FAIL $case_id"
    echo "  $reason"
    echo "  output:"
    sed 's/^/    /' "$out"
    fail=$((fail + 1))
  fi

  rm -f "$out"
done < <(jq -r '.suites | to_entries[] | .value[]' "$MANIFEST")

echo
echo "Summary: PASS=$pass FAIL=$fail TOTAL=$((pass + fail))"

if [[ "$fail" -ne 0 ]]; then
  echo "Conformance FAILED (must be 100%)." >&2
  exit 1
fi

echo "Conformance PASSED (100%)."
