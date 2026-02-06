#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PS="${PS:-$ROOT_DIR/c/ps}"

pass=0
fail=0

expect_exit() {
  local desc="$1"
  local expected="$2"
  shift 2
  set +e
  "$@" >/tmp/ps_cli_test.out 2>&1
  local rc=$?
  set -e
  if [[ "$rc" -eq "$expected" ]]; then
    echo "PASS $desc"
    pass=$((pass + 1))
  else
    echo "FAIL $desc"
    echo "  expected exit $expected, got $rc"
    echo "  cmd: $*"
    echo "  output:"
    sed -n '1,80p' /tmp/ps_cli_test.out
    fail=$((fail + 1))
  fi
}

echo "== ProtoScript CLI Tests =="
echo "PS: $PS"
echo

expect_exit "help" 0 "$PS" --help
expect_exit "version" 0 "$PS" --version
expect_exit "run exit code" 100 "$PS" run "$ROOT_DIR/tests/cli/exit_code.pts"
expect_exit "argv passthrough" 0 "$PS" run "$ROOT_DIR/tests/cli/args.pts"
expect_exit "runtime error exit" 2 "$PS" run "$ROOT_DIR/tests/cli/runtime_error.pts"
expect_exit "static check exit" 2 "$PS" check "$ROOT_DIR/tests/invalid/type/type_mismatch_assignment.pts"

echo
echo "Summary: PASS=$pass FAIL=$fail TOTAL=$((pass + fail))"
if [[ "$fail" -ne 0 ]]; then
  exit 1
fi
