#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
C_COMPILER="$ROOT_DIR/c/pscc"
NODE_COMPILER="$ROOT_DIR/bin/protoscriptc"

if [[ -x "/usr/local/bin/jq" ]]; then
  JQ="/usr/local/bin/jq"
elif [[ -x "/opt/local/bin/jq" ]]; then
  JQ="/opt/local/bin/jq"
elif command -v jq >/dev/null 2>&1; then
  JQ="$(command -v jq)"
else
  echo "ERROR: jq not found (tried /usr/local/bin and /opt/local/bin)." >&2
  exit 2
fi

cases=(
  "tests/edge/switch_cfg_case.pts"
  "tests/edge/switch_cfg_default.pts"
)

pass=0
fail=0

echo "== IR Switch Lowering Checks =="
echo "Node compiler: $NODE_COMPILER"
echo "C compiler:    $C_COMPILER"
echo

for src in "${cases[@]}"; do
  c_ir="$(mktemp)"
  node_ir="$(mktemp)"
  c_err="$(mktemp)"
  node_err="$(mktemp)"

  rc_c=0
  rc_n=0
  "$C_COMPILER" --emit-ir-c-json "$ROOT_DIR/$src" >"$c_ir" 2>"$c_err" || rc_c=$?
  "$NODE_COMPILER" --emit-ir-json "$ROOT_DIR/$src" >"$node_ir" 2>"$node_err" || rc_n=$?

  ok=true
  reason=""

  if [[ $rc_c -ne 0 || $rc_n -ne 0 ]]; then
    ok=false
    reason="IR emission failed (node=$rc_n c=$rc_c)"
  fi

  check_ir() {
    local ir="$1"
    local tag="$2"
    local c_branch c_jump c_sw_blocks c_unhandled
    c_branch="$("$JQ" '[.module.functions[].blocks[].instrs[] | select(.op=="branch_if")] | length' "$ir")"
    c_jump="$("$JQ" '[.module.functions[].blocks[].instrs[] | select(.op=="jump")] | length' "$ir")"
    c_sw_blocks="$("$JQ" '[.module.functions[].blocks[] | select((.label | tostring | startswith("sw_cmp_")) or (.label | tostring | startswith("sw_body_")) or (.label | tostring | startswith("sw_default_")) or (.label | tostring | startswith("sw_done_")) or (.label | tostring | startswith("switch_casecmp_")) or (.label | tostring | startswith("switch_casebody_")) or (.label | tostring | startswith("switch_default_")) or (.label | tostring | startswith("switch_done_")))] | length' "$ir")"
    c_unhandled="$("$JQ" '[.module.functions[].blocks[].instrs[] | select(.op=="unhandled_stmt" and .kind=="SwitchStmt")] | length' "$ir")"

    if [[ "$c_branch" -lt 1 ]]; then ok=false; reason="$tag: missing branch_if"; fi
    if [[ "$c_jump" -lt 2 ]]; then ok=false; reason="$tag: insufficient jump"; fi
    if [[ "$c_sw_blocks" -lt 3 ]]; then ok=false; reason="$tag: missing switch blocks"; fi
    if [[ "$c_unhandled" -ne 0 ]]; then ok=false; reason="$tag: contains unhandled switch"; fi
  }

  if [[ "$ok" == true ]]; then
    check_ir "$node_ir" "node"
    check_ir "$c_ir" "c"
  fi

  if [[ "$ok" == true ]]; then
    pass=$((pass + 1))
    echo "PASS ${src#tests/}"
  else
    fail=$((fail + 1))
    echo "FAIL ${src#tests/} :: $reason"
    echo "--- node stderr ---"
    sed -n '1,60p' "$node_err"
    echo "--- c stderr ---"
    sed -n '1,60p' "$c_err"
  fi

  rm -f "$c_ir" "$node_ir" "$c_err" "$node_err"
done

echo
echo "Summary: PASS=$pass FAIL=$fail TOTAL=$((pass + fail))"
if [[ "$fail" -ne 0 ]]; then
  exit 1
fi
echo "IR switch lowering checks PASSED."
