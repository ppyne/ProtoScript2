#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TESTS_DIR="$ROOT_DIR/tests"
MANIFEST="$TESTS_DIR/manifest.json"
NODEC="${NODE_COMPILER:-$ROOT_DIR/bin/protoscriptc}"
CCLI="${C_COMPILER:-$ROOT_DIR/c/pscc}"

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

extract_code() {
  local f="$1"
  grep -Eo '(^|[^A-Z0-9])(E[0-9]{4}|R[0-9]{4})([^A-Z0-9]|$)' "$f" | head -n1 | sed -E 's/.*(E[0-9]{4}|R[0-9]{4}).*/\1/' || true
}

extract_category() {
  local f="$1"
  sed -nE 's/.*(E[0-9]{4}|R[0-9]{4}) ([A-Z0-9_]+):.*/\2/p' "$f" | head -n1 || true
}

pass=0
fail=0

echo "== C Frontend Oracle Crosscheck =="
echo "Node compiler: $NODEC"
echo "C compiler:    $CCLI"
echo

while IFS= read -r case_id; do
  [[ -z "$case_id" ]] && continue
  src="$TESTS_DIR/$case_id.pts"

  out_node="$(mktemp)"
  out_c="$(mktemp)"

  set +e
  "$NODEC" --check "$src" >"$out_node" 2>&1
  rc_node=$?
  "$CCLI" --check "$src" >"$out_c" 2>&1
  rc_c=$?
  set -e

  ok=true
  reason=""

  if [[ $rc_node -ne $rc_c ]]; then
    ok=false
    reason="exit code mismatch (node=$rc_node c=$rc_c)"
  fi

  node_code="$(extract_code "$out_node")"
  c_code="$(extract_code "$out_c")"
  node_cat="$(extract_category "$out_node")"
  c_cat="$(extract_category "$out_c")"

  if [[ "$ok" == true && "$node_code" != "$c_code" ]]; then
    ok=false
    reason="diagnostic code mismatch ($node_code vs $c_code)"
  fi
  if [[ "$ok" == true && "$node_cat" != "$c_cat" ]]; then
    ok=false
    reason="diagnostic category mismatch ($node_cat vs $c_cat)"
  fi

  if [[ "$ok" == true ]]; then
    echo "PASS $case_id"
    pass=$((pass + 1))
  else
    echo "FAIL $case_id"
    echo "  $reason"
    echo "  --node --check:"
    sed 's/^/    /' "$out_node"
    echo "  --c --check:"
    sed 's/^/    /' "$out_c"
    fail=$((fail + 1))
  fi

  rm -f "$out_node" "$out_c"
done < <(jq -r '.suites | to_entries[] | .value[]' "$MANIFEST")

echo
echo "Summary: PASS=$pass FAIL=$fail TOTAL=$((pass + fail))"
if [[ $fail -ne 0 ]]; then
  exit 1
fi

echo "C frontend oracle crosscheck PASSED."
