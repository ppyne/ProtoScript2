#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TESTS_DIR="$ROOT_DIR/tests"
MANIFEST="$TESTS_DIR/manifest.json"
NODE_COMPILER="${NODE_COMPILER:-$ROOT_DIR/bin/protoscriptc}"
C_COMPILER="${C_COMPILER:-$ROOT_DIR/c/pscc}"

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

sig_jq='[.module.functions[] | {name, params:(.params|length), variadic:([.params[]? | select(.variadic==true)]|length), ret:.returnType.name}] | sort_by(.name)'
inv_jq='(
  (.module.functions | length) > 0
) and (
  all(.module.functions[];
    ((.blocks | length) > 0)
    and (any(.blocks[]; .label == "entry"))
    and (([.blocks[].instrs[]?] | length) > 0)
    and (all(.blocks[]; ((.instrs | length) > 0)))
  )
)'

pass=0
fail=0
skip=0

echo "== IR Node/C Crosscheck (structure + invariants) =="
echo "Node compiler: $NODE_COMPILER"
echo "C compiler:    $C_COMPILER"
echo

while IFS= read -r case_id; do
  [[ -z "$case_id" ]] && continue
  src="$TESTS_DIR/$case_id.pts"
  expect="$TESTS_DIR/$case_id.expect.json"
  status="$(jq -r '.status // empty' "$expect")"

  # IR emission requires source accepted statically.
  if [[ "$status" != "reject-runtime" && "$status" != "accept-runtime" ]]; then
    echo "SKIP $case_id (status=$status)"
    skip=$((skip + 1))
    continue
  fi

  node_ir="$(mktemp)"
  c_ir="$(mktemp)"
  node_err="$(mktemp)"
  c_err="$(mktemp)"

  set +e
  "$NODE_COMPILER" --emit-ir-json "$src" >"$node_ir" 2>"$node_err"
  rc_node=$?
  "$C_COMPILER" --emit-ir-c-json "$src" >"$c_ir" 2>"$c_err"
  rc_c=$?
  set -e

  ok=true
  reason=""

  if [[ $rc_node -ne 0 || $rc_c -ne 0 ]]; then
    ok=false
    reason="cannot emit IR (node=$rc_node c=$rc_c)"
  fi

  if [[ "$ok" == true ]]; then
    set +e
    "$NODE_COMPILER" --validate-ir "$node_ir" >/dev/null 2>&1
    rc_vn=$?
    "$NODE_COMPILER" --validate-ir "$c_ir" >/dev/null 2>&1
    rc_vc=$?
    set -e
    if [[ $rc_vn -ne 0 || $rc_vc -ne 0 ]]; then
      ok=false
      reason="IR validation failed (node_ir=$rc_vn c_ir=$rc_vc)"
    fi
  fi

  if [[ "$ok" == true ]]; then
    node_sig="$(jq -c "$sig_jq" "$node_ir")"
    c_sig="$(jq -c "$sig_jq" "$c_ir")"
    if [[ "$node_sig" != "$c_sig" ]]; then
      ok=false
      reason="function signature mismatch"
    fi
  fi

  if [[ "$ok" == true ]]; then
    node_inv="$(jq -e "$inv_jq" "$node_ir" >/dev/null; echo $?)"
    c_inv="$(jq -e "$inv_jq" "$c_ir" >/dev/null; echo $?)"
    if [[ "$node_inv" != "0" || "$c_inv" != "0" ]]; then
      ok=false
      reason="invariants failed (node_ir=$node_inv c_ir=$c_inv)"
    fi
  fi

  if [[ "$ok" == true ]]; then
    echo "PASS $case_id"
    pass=$((pass + 1))
  else
    echo "FAIL $case_id"
    echo "  $reason"
    echo "  --node stderr:"; sed 's/^/    /' "$node_err"
    echo "  --c stderr:"; sed 's/^/    /' "$c_err"
    if [[ -f "$node_ir" ]]; then
      echo "  --node signatures:"; jq -c "$sig_jq" "$node_ir" | sed 's/^/    /'
    fi
    if [[ -f "$c_ir" ]]; then
      echo "  --c signatures:"; jq -c "$sig_jq" "$c_ir" | sed 's/^/    /'
    fi
    fail=$((fail + 1))
  fi

  rm -f "$node_ir" "$c_ir" "$node_err" "$c_err"
done < <(jq -r '.suites | to_entries[] | .value[]' "$MANIFEST")

echo
echo "Summary: PASS=$pass FAIL=$fail SKIP=$skip TOTAL=$((pass + fail + skip))"
if [[ $fail -ne 0 ]]; then
  exit 1
fi

echo "IR Node/C crosscheck PASSED."
