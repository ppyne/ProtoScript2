#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NODE_COMPILER="${COMPILER:-$ROOT_DIR/bin/protoscriptc}"
WASM_JS="$ROOT_DIR/web/protoscript.js"
WASM_CASE_RUNNER="$ROOT_DIR/tests/wasm/run_wasm_case.js"

if ! command -v node >/dev/null 2>&1; then
  echo "ERROR: node is required." >&2
  exit 2
fi

if [[ ! -x "$NODE_COMPILER" ]]; then
  echo "ERROR: missing Node oracle compiler: $NODE_COMPILER" >&2
  exit 2
fi

if [[ ! -f "$WASM_JS" ]]; then
  echo "ERROR: missing wasm JS module: $WASM_JS" >&2
  exit 2
fi

echo "== WASM Runtime Parity (Node oracle vs C runtime in WASM) =="
echo "Node compiler: $NODE_COMPILER"
echo "WASM module:   $WASM_JS"
echo

normalize_stream() {
  local src="$1"
  local dst="$2"
  sed -E 's#([A-Za-z0-9_./-]*/)?ps_[0-9a-fA-F-]+_[0-9]+#ps_tmp#g' "$src" >"$dst"
}

pass=0
fail=0

declare -a CASES=(
  "$ROOT_DIR/tests/cli/hello.pts"
  "$ROOT_DIR/tests/cli/clone_inherited_throw_parity.pts"
  "$ROOT_DIR/tests/edge/clone_inherited_override.pts"
)

for src in "${CASES[@]}"; do
  case_id="${src#$ROOT_DIR/tests/}"
  out_node="$(mktemp)"
  err_node="$(mktemp)"
  out_wasm="$(mktemp)"
  err_wasm="$(mktemp)"
  out_node_norm="$(mktemp)"
  err_node_norm="$(mktemp)"
  out_wasm_norm="$(mktemp)"
  err_wasm_norm="$(mktemp)"

  set +e
  "$NODE_COMPILER" --run "$src" >"$out_node" 2>"$err_node"
  rc_node=$?
  node "$WASM_CASE_RUNNER" "$WASM_JS" "$src" "$out_wasm" "$err_wasm"
  rc_wasm=$?
  set -e

  normalize_stream "$out_node" "$out_node_norm"
  normalize_stream "$err_node" "$err_node_norm"
  normalize_stream "$out_wasm" "$out_wasm_norm"
  normalize_stream "$err_wasm" "$err_wasm_norm"

  ok=true
  reason=""
  if [[ "$rc_node" -ne "$rc_wasm" ]]; then
    ok=false
    reason="exit code diverges ($rc_node vs $rc_wasm)"
  elif ! cmp -s "$out_node_norm" "$out_wasm_norm"; then
    ok=false
    reason="stdout diverges"
  elif ! cmp -s "$err_node_norm" "$err_wasm_norm"; then
    ok=false
    reason="stderr diverges"
  fi

  if [[ "$ok" == true ]]; then
    echo "PASS $case_id"
    pass=$((pass + 1))
  else
    echo "FAIL $case_id"
    echo "  $reason"
    echo "  node rc=$rc_node wasm rc=$rc_wasm"
    if [[ "$reason" == "stdout diverges" ]]; then
      echo "  stdout diff (normalized):"
      diff -u "$out_node_norm" "$out_wasm_norm" | sed -n '1,80p' || true
    elif [[ "$reason" == "stderr diverges" ]]; then
      echo "  stderr diff (normalized):"
      diff -u "$err_node_norm" "$err_wasm_norm" | sed -n '1,80p' || true
    else
      echo "  node stderr:"
      sed -n '1,80p' "$err_node"
      echo "  wasm stderr:"
      sed -n '1,80p' "$err_wasm"
    fi
    fail=$((fail + 1))
  fi

  rm -f "$out_node" "$err_node" "$out_wasm" "$err_wasm" \
    "$out_node_norm" "$err_node_norm" "$out_wasm_norm" "$err_wasm_norm"
done

echo
echo "Summary: PASS=$pass FAIL=$fail TOTAL=$((pass + fail))"
if [[ "$fail" -ne 0 ]]; then
  exit 1
fi
echo "WASM runtime parity PASSED."
