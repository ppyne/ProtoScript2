#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NODE_COMPILER="${NODE_COMPILER:-$ROOT_DIR/bin/protoscriptc}"
C_COMPILER="${C_COMPILER:-$ROOT_DIR/c/pscc}"
SRC="${1:-$ROOT_DIR/tests/edge/generic_token_parity.pts}"

out_node="$(mktemp)"
out_c="$(mktemp)"
err_node="$(mktemp)"
err_c="$(mktemp)"
cleanup() {
  rm -f "$out_node" "$out_c" "$err_node" "$err_c"
}
trap cleanup EXIT

echo "== Lexer Token Parity (Node vs C) =="
echo "Node: $NODE_COMPILER"
echo "C:    $C_COMPILER"
echo "Src:  $SRC"

set +e
"$NODE_COMPILER" --dump-tokens "$SRC" >"$out_node" 2>"$err_node"
rc_node=$?
"$C_COMPILER" --dump-tokens "$SRC" >"$out_c" 2>"$err_c"
rc_c=$?
set -e

if [[ $rc_node -ne 0 || $rc_c -ne 0 ]]; then
  echo "FAIL token dump invocation"
  echo "  node rc=$rc_node c rc=$rc_c"
  echo "  node stderr:" && sed -n '1,80p' "$err_node"
  echo "  c stderr:" && sed -n '1,80p' "$err_c"
  exit 1
fi

if cmp -s "$out_node" "$out_c"; then
  echo "PASS token parity"
  exit 0
fi

echo "FAIL token parity"
echo "-- node dump --"
sed -n '1,120p' "$out_node"
echo "-- c dump --"
sed -n '1,120p' "$out_c"
exit 1
