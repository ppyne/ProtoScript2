#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PS="${PS:-$ROOT_DIR/c/ps}"
export PS_MODULE_REGISTRY="${PS_MODULE_REGISTRY:-$ROOT_DIR/modules/registry.json}"
export PS_MODULE_PATH="${PS_MODULE_PATH:-$ROOT_DIR/modules}"

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

expect_output_contains() {
  local desc="$1"
  local needle="$2"
  shift 2
  set +e
  "$@" >/tmp/ps_cli_test.out 2>&1
  local rc=$?
  set -e
  if [[ "$rc" -ne 0 ]]; then
    echo "FAIL $desc"
    echo "  expected exit 0, got $rc"
    echo "  cmd: $*"
    echo "  output:"
    sed -n '1,80p' /tmp/ps_cli_test.out
    fail=$((fail + 1))
    return
  fi
  if grep -Fq "$needle" /tmp/ps_cli_test.out; then
    echo "PASS $desc"
    pass=$((pass + 1))
  else
    echo "FAIL $desc"
    echo "  missing output: $needle"
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
expect_exit "inline -e" 0 "$PS" -e "int x = 1 + 2;"
expect_output_contains "repl prompt" "ps> " bash -c "printf 'exit\n' | \"$PS\" repl"
expect_output_contains "run outputs" "hello" "$PS" run "$ROOT_DIR/tests/cli/hello.pts"
expect_exit "run exit code" 100 "$PS" run "$ROOT_DIR/tests/cli/exit_code.pts"
expect_exit "argv passthrough" 0 "$PS" run "$ROOT_DIR/tests/cli/args.pts"
expect_exit "runtime error exit" 2 "$PS" run "$ROOT_DIR/tests/cli/runtime_error.pts"
expect_exit "static check success" 0 "$PS" check "$ROOT_DIR/tests/cli/hello.pts"
expect_exit "static check exit" 2 "$PS" check "$ROOT_DIR/tests/invalid/type/type_mismatch_assignment.pts"
expect_output_contains "ast outputs" "\"kind\"" "$PS" ast "$ROOT_DIR/tests/cli/exit_code.pts"
expect_output_contains "ir outputs" "\"functions\"" "$PS" ir "$ROOT_DIR/tests/cli/exit_code.pts"
expect_exit "trace enabled" 0 "$PS" run "$ROOT_DIR/tests/cli/hello.pts" --trace
expect_output_contains "trace-ir enabled" "[ir]" "$PS" run "$ROOT_DIR/tests/cli/hello.pts" --trace-ir
expect_output_contains "time enabled" "time:" "$PS" run "$ROOT_DIR/tests/cli/hello.pts" --time
expect_output_contains "emit-c outputs C" "int main" "$PS" emit-c "$ROOT_DIR/tests/cli/exit_code.pts"

echo
echo "Summary: PASS=$pass FAIL=$fail TOTAL=$((pass + fail))"
if [[ "$fail" -ne 0 ]]; then
  exit 1
fi
