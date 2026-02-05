#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMPILER="${COMPILER:-$ROOT_DIR/bin/protoscriptc}"
IR_DIR="$ROOT_DIR/tests/ir-format"

pass=0
fail=0

run_expect_ok() {
  local file="$1"
  if "$COMPILER" --validate-ir "$file" >/dev/null 2>&1; then
    echo "PASS $(basename "$file")"
    pass=$((pass + 1))
  else
    echo "FAIL $(basename "$file") (expected valid)"
    fail=$((fail + 1))
  fi
}

run_expect_fail() {
  local file="$1"
  local needle="$2"
  local out
  out="$(mktemp)"
  if "$COMPILER" --validate-ir "$file" >"$out" 2>&1; then
    echo "FAIL $(basename "$file") (expected invalid)"
    fail=$((fail + 1))
  elif grep -Fq "$needle" "$out"; then
    echo "PASS $(basename "$file")"
    pass=$((pass + 1))
  else
    echo "FAIL $(basename "$file") (missing expected message: $needle)"
    sed 's/^/  /' "$out"
    fail=$((fail + 1))
  fi
  rm -f "$out"
}

echo "== IR Format Validation =="

run_expect_ok "$IR_DIR/valid_minimal.ir.json"
run_expect_fail "$IR_DIR/invalid_version.ir.json" "ir_version must be '1.0.0'"
run_expect_fail "$IR_DIR/invalid_missing_jump_target.ir.json" "jump target 'missing_block' does not exist"

echo
echo "Summary: PASS=$pass FAIL=$fail TOTAL=$((pass + fail))"
if [[ "$fail" -ne 0 ]]; then
  exit 1
fi
echo "IR format checks PASSED."

