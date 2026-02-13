#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CASES_DIR="$ROOT_DIR/tests/errors/diagnostics"
NODE_COMPILER="${COMPILER:-$ROOT_DIR/bin/protoscriptc}"
C_COMPILER="${C_COMPILER:-$ROOT_DIR/c/pscc}"

pass=0
fail=0

check_no_trailing_ws() {
  local file="$1"
  if grep -nE '[[:blank:]]+$' "$file" >/dev/null; then
    return 1
  fi
  return 0
}

echo "== Strict Diagnostics (JS/C parity) =="
echo "Node: $NODE_COMPILER"
echo "C:    $C_COMPILER"
echo

for src in "$CASES_DIR"/*.pts; do
  case_id="$(basename "$src" .pts)"
  exp="$CASES_DIR/$case_id.expect.txt"
  out_node="$(mktemp)"
  out_node_2="$(mktemp)"
  out_c="$(mktemp)"
  norm_node="$(mktemp)"
  norm_node_2="$(mktemp)"
  norm_c="$(mktemp)"

  if [[ ! -f "$exp" ]]; then
    echo "FAIL $case_id"
    echo "  missing expectation: $exp"
    fail=$((fail + 1))
    rm -f "$out_node" "$out_node_2" "$out_c" "$norm_node" "$norm_node_2" "$norm_c"
    continue
  fi

  set +e
  "$NODE_COMPILER" --check "$src" >"$out_node" 2>&1
  rc_node=$?
  "$NODE_COMPILER" --check "$src" >"$out_node_2" 2>&1
  rc_node_2=$?
  "$C_COMPILER" --check "$src" >"$out_c" 2>&1
  rc_c=$?
  set -e

  sed "s#${ROOT_DIR}/##g" "$out_node" >"$norm_node"
  sed "s#${ROOT_DIR}/##g" "$out_node_2" >"$norm_node_2"
  sed "s#${ROOT_DIR}/##g" "$out_c" >"$norm_c"

  ok=true
  reason=""
  if [[ "$rc_node" -eq 0 || "$rc_node_2" -eq 0 || "$rc_c" -eq 0 ]]; then
    ok=false
    reason="expected non-zero exit for all compilers"
  fi
  if [[ "$ok" == true ]] && ! cmp -s "$norm_node" "$norm_node_2"; then
    ok=false
    reason="node output is not deterministic across runs"
  fi
  if [[ "$ok" == true ]] && ! cmp -s "$norm_node" "$exp"; then
    ok=false
    reason="node output mismatch expected"
  fi
  if [[ "$ok" == true ]] && ! cmp -s "$norm_c" "$exp"; then
    ok=false
    reason="c output mismatch expected"
  fi
  if [[ "$ok" == true ]] && ! cmp -s "$norm_node" "$norm_c"; then
    ok=false
    reason="node/c output mismatch"
  fi
  if [[ "$ok" == true ]] && ! check_no_trailing_ws "$norm_node"; then
    ok=false
    reason="trailing whitespace in node output"
  fi
  if [[ "$ok" == true ]] && ! check_no_trailing_ws "$norm_c"; then
    ok=false
    reason="trailing whitespace in c output"
  fi

  if [[ "$ok" == true ]]; then
    echo "PASS $case_id"
    pass=$((pass + 1))
  else
    echo "FAIL $case_id"
    echo "  $reason"
    echo "  expected:"
    sed 's/^/    /' "$exp"
    echo "  node:"
    sed 's/^/    /' "$norm_node"
    echo "  c:"
    sed 's/^/    /' "$norm_c"
    fail=$((fail + 1))
  fi

  rm -f "$out_node" "$out_node_2" "$out_c" "$norm_node" "$norm_node_2" "$norm_c"
done

echo
echo "Summary: PASS=$pass FAIL=$fail TOTAL=$((pass + fail))"
if [[ "$fail" -ne 0 ]]; then
  exit 1
fi
