#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PS="${PS:-$ROOT_DIR/c/ps}"
export PS_MODULE_REGISTRY="${PS_MODULE_REGISTRY:-$ROOT_DIR/modules/registry.json}"
CLI_MODULES_TMP_DIR=""
if [[ -z "${PS_MODULE_PATH:-}" ]]; then
  CLI_MODULES_TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/ps_modules_cli_XXXXXX")"
  export PS_MODULE_PATH="$CLI_MODULES_TMP_DIR"
fi

cleanup_cli_modules_tmp() {
  if [[ -n "$CLI_MODULES_TMP_DIR" && -d "$CLI_MODULES_TMP_DIR" ]]; then
    rm -rf "$CLI_MODULES_TMP_DIR"
  fi
}
trap cleanup_cli_modules_tmp EXIT

if [[ -x "$ROOT_DIR/tests/build_modules.sh" ]]; then
  "$ROOT_DIR/tests/build_modules.sh" >/tmp/ps_cli_modules_build.out 2>&1 || {
    echo "ERROR: failed to build test modules" >&2
    sed -n '1,80p' /tmp/ps_cli_modules_build.out >&2
    exit 2
  }
fi

pass=0
fail=0

json_status() {
  local file="$1"
  if command -v jq >/dev/null 2>&1; then
    jq -r '.status // ""' "$file"
  else
    python3 - "$file" <<'PY'
import json, sys
with open(sys.argv[1], "r", encoding="utf-8") as f:
    data = json.load(f)
print(data.get("status", ""))
PY
  fi
}

json_stdout() {
  local file="$1"
  if command -v jq >/dev/null 2>&1; then
    jq -r -j '.expected_stdout // ""' "$file"
  else
    python3 - "$file" <<'PY'
import json, sys
with open(sys.argv[1], "r", encoding="utf-8") as f:
    data = json.load(f)
sys.stdout.write(data.get("expected_stdout", "") or "")
PY
  fi
}

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

expect_error_contains() {
  local desc="$1"
  local needle="$2"
  shift 2
  set +e
  "$@" >/tmp/ps_cli_test.out 2>&1
  local rc=$?
  set -e
  if [[ "$rc" -eq 0 ]]; then
    echo "FAIL $desc"
    echo "  expected non-zero exit, got 0"
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

expect_error_contains_not_contains() {
  local desc="$1"
  local must_have="$2"
  local must_not_have="$3"
  shift 3
  set +e
  "$@" >/tmp/ps_cli_test.out 2>&1
  local rc=$?
  set -e
  if [[ "$rc" -eq 0 ]]; then
    echo "FAIL $desc"
    echo "  expected non-zero exit, got 0"
    echo "  cmd: $*"
    echo "  output:"
    sed -n '1,80p' /tmp/ps_cli_test.out
    fail=$((fail + 1))
    return
  fi
  if ! grep -Fq "$must_have" /tmp/ps_cli_test.out; then
    echo "FAIL $desc"
    echo "  missing output: $must_have"
    echo "  cmd: $*"
    echo "  output:"
    sed -n '1,80p' /tmp/ps_cli_test.out
    fail=$((fail + 1))
    return
  fi
  if grep -Fq "$must_not_have" /tmp/ps_cli_test.out; then
    echo "FAIL $desc"
    echo "  unexpected output present: $must_not_have"
    echo "  cmd: $*"
    echo "  output:"
    sed -n '1,80p' /tmp/ps_cli_test.out
    fail=$((fail + 1))
    return
  fi
  echo "PASS $desc"
  pass=$((pass + 1))
}

expect_no_output() {
  local desc="$1"
  shift
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
  if [[ -s /tmp/ps_cli_test.out ]]; then
    echo "FAIL $desc"
    echo "  expected no output"
    echo "  cmd: $*"
    echo "  output:"
    sed -n '1,80p' /tmp/ps_cli_test.out
    fail=$((fail + 1))
  else
    echo "PASS $desc"
    pass=$((pass + 1))
  fi
}

expect_same_static_diag_check_run() {
  local desc="$1"
  local src="$2"
  set +e
  "$PS" check "$src" >/tmp/ps_cli_check.out 2>&1
  local rc_check=$?
  "$PS" run "$src" >/tmp/ps_cli_run.out 2>&1
  local rc_run=$?
  set -e
  if [[ "$rc_check" -eq 0 || "$rc_run" -eq 0 ]]; then
    echo "FAIL $desc"
    echo "  expected non-zero for both check and run"
    echo "  check rc=$rc_check run rc=$rc_run"
    fail=$((fail + 1))
    return
  fi
  if cmp -s /tmp/ps_cli_check.out /tmp/ps_cli_run.out; then
    echo "PASS $desc"
    pass=$((pass + 1))
  else
    echo "FAIL $desc"
    echo "  check and run diagnostics differ"
    echo "  check output:"
    sed -n '1,80p' /tmp/ps_cli_check.out
    echo "  run output:"
    sed -n '1,80p' /tmp/ps_cli_run.out
    fail=$((fail + 1))
  fi
}

expect_node_c_run_parity() {
  local desc="$1"
  local src="$2"
  local node_bin="$ROOT_DIR/bin/protoscriptc"
  set +e
  "$node_bin" --run "$src" >/tmp/ps_cli_node.out 2>&1
  local rc_node=$?
  "$PS" run "$src" >/tmp/ps_cli_c.out 2>&1
  local rc_c=$?
  set -e
  if [[ "$rc_node" -eq 0 && "$rc_c" -ne 0 ]]; then
    echo "FAIL $desc"
    echo "  node succeeded but c/ps failed"
    sed -n '1,80p' /tmp/ps_cli_node.out
    sed -n '1,80p' /tmp/ps_cli_c.out
    fail=$((fail + 1))
    return
  fi
  if [[ "$rc_node" -ne 0 && "$rc_c" -eq 0 ]]; then
    echo "FAIL $desc"
    echo "  node failed but c/ps succeeded"
    sed -n '1,80p' /tmp/ps_cli_node.out
    sed -n '1,80p' /tmp/ps_cli_c.out
    fail=$((fail + 1))
    return
  fi
  if cmp -s /tmp/ps_cli_node.out /tmp/ps_cli_c.out; then
    echo "PASS $desc"
    pass=$((pass + 1))
  else
    echo "FAIL $desc"
    echo "  output differs between protoscriptc --run and c/ps run"
    echo "  node output:"
    sed -n '1,80p' /tmp/ps_cli_node.out
    echo "  c/ps output:"
    sed -n '1,80p' /tmp/ps_cli_c.out
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
expect_output_contains "run if basic" "yes" "$PS" run "$ROOT_DIR/tests/cli/if_basic.pts"
expect_output_contains "run list concat" "hello world" "$PS" run "$ROOT_DIR/tests/cli/list_concat.pts"
expect_output_contains "run io temp path" "ok" "$PS" run "$ROOT_DIR/tests/cli/io_temp_path.pts"
expect_output_contains "run proto bool field" "true" "$PS" run "$ROOT_DIR/tests/cli/proto_bool_field.pts"
expect_output_contains "run control flow continuation" "after_try" "$PS" run "$ROOT_DIR/tests/cli/control_flow_continuation.pts"
expect_node_c_run_parity "run parity clone inherited throw" "$ROOT_DIR/tests/cli/clone_inherited_throw_parity.pts"
expect_output_contains "run manual ex008" "1" "$PS" run "$ROOT_DIR/tests/cli/manual_ex008.pts"
expect_output_contains "run manual ex010" "12" "$PS" run "$ROOT_DIR/tests/cli/manual_ex010.pts"
expect_output_contains "run manual ex011" "42" "$PS" run "$ROOT_DIR/tests/cli/manual_ex011.pts"
expect_output_contains "run manual ex012" "0.001" "$PS" run "$ROOT_DIR/tests/cli/manual_ex012.pts"
expect_output_contains "run manual ex013" "Bonjour" "$PS" run "$ROOT_DIR/tests/cli/manual_ex013.pts"
expect_output_contains "run manual ex014" "☺" "$PS" run "$ROOT_DIR/tests/cli/manual_ex014.pts"
expect_output_contains "run manual ex015" "a=1" "$PS" run "$ROOT_DIR/tests/cli/manual_ex015.pts"
expect_output_contains "run manual ex016" "0" "$PS" run "$ROOT_DIR/tests/cli/manual_ex016.pts"
expect_output_contains "run manual ex018" "10" "$PS" run "$ROOT_DIR/tests/cli/manual_ex018.pts"
expect_output_contains "run manual ex019" "ok" "$PS" run "$ROOT_DIR/tests/cli/manual_ex019.pts"
expect_output_contains "run manual ex021" "2" "$PS" run "$ROOT_DIR/tests/cli/manual_ex021.pts"
expect_output_contains "run manual ex024" "L0" "$PS" run "$ROOT_DIR/tests/cli/manual_ex024.pts"
expect_output_contains "run manual ex007" "json null" "$PS" run "$ROOT_DIR/tests/cli/manual_ex007.pts"
expect_exit "run exit code" 100 "$PS" run "$ROOT_DIR/tests/cli/exit_code.pts"
expect_exit "run static error exit" 1 "$PS" run "$ROOT_DIR/tests/invalid/type/type_mismatch_assignment.pts"
expect_error_contains_not_contains \
  "run static redeclaration guard (no runtime)" \
  "E3131 REDECLARATION" \
  "R1011 UNHANDLED_EXCEPTION" \
  "$PS" run "$ROOT_DIR/tests/invalid/type/redeclaration_same_scope.pts"
expect_same_static_diag_check_run \
  "run/check static diag parity redeclaration" \
  "$ROOT_DIR/tests/invalid/type/redeclaration_same_scope.pts"
expect_error_contains_not_contains \
  "run multiple static errors guard (no runtime)" \
  "E3131 REDECLARATION" \
  "R1011 UNHANDLED_EXCEPTION" \
  "$PS" run "$ROOT_DIR/tests/invalid/multiple_static_errors.pts"
expect_same_static_diag_check_run \
  "run/check static diag parity multi-error-first" \
  "$ROOT_DIR/tests/invalid/multiple_static_errors.pts"
expect_error_contains_not_contains \
  "run --trace static error no runtime trace" \
  "E3131 REDECLARATION" \
  "[trace]" \
  "$PS" --trace run "$ROOT_DIR/tests/invalid/multiple_static_errors.pts"
expect_error_contains_not_contains \
  "run --trace --trace-ir static error no ir trace" \
  "E3131 REDECLARATION" \
  "[ir]" \
  "$PS" --trace --trace-ir run "$ROOT_DIR/tests/invalid/multiple_static_errors.pts"
expect_exit "argv passthrough" 0 "$PS" run "$ROOT_DIR/tests/cli/args.pts"
expect_exit "runtime error exit" 1 "$PS" run "$ROOT_DIR/tests/cli/runtime_error.pts"
expect_error_contains "preprocess mapping" "mapped_file.pts:202:17 R1004 RUNTIME_DIVIDE_BY_ZERO:" "$PS" run "$ROOT_DIR/tests/cli/preprocess_runtime_error.pts"

abs_module_path="$ROOT_DIR/tests/fixtures/datastruct/Stack.pts"
tmp_abs_import="$(mktemp)"
cat >"$tmp_abs_import" <<EOF
import Io;
import "$abs_module_path";

function main() : void {
    Stack s = Stack.clone();
    int v = s.value();
    Io.printLine(v.toString());
}
EOF
expect_output_contains "run import abs path" "444" "$PS" run "$tmp_abs_import"
rm -f "$tmp_abs_import"
expect_exit "static check success" 0 "$PS" check "$ROOT_DIR/tests/cli/hello.pts"
expect_exit "pscc check if basic" 0 "$ROOT_DIR/c/pscc" --check "$ROOT_DIR/tests/cli/if_basic.pts"
expect_exit "pscc check list concat" 0 "$ROOT_DIR/c/pscc" --check "$ROOT_DIR/tests/cli/list_concat.pts"
expect_exit "pscc check proto bool field" 0 "$ROOT_DIR/c/pscc" --check "$ROOT_DIR/tests/cli/proto_bool_field.pts"
expect_exit "pscc check control flow continuation" 0 "$ROOT_DIR/c/pscc" --check "$ROOT_DIR/tests/cli/control_flow_continuation.pts"
expect_exit "pscc check manual ex008" 0 "$ROOT_DIR/c/pscc" --check "$ROOT_DIR/tests/cli/manual_ex008.pts"
expect_exit "pscc check manual ex010" 0 "$ROOT_DIR/c/pscc" --check "$ROOT_DIR/tests/cli/manual_ex010.pts"
expect_exit "pscc check manual ex011" 0 "$ROOT_DIR/c/pscc" --check "$ROOT_DIR/tests/cli/manual_ex011.pts"
expect_exit "pscc check manual ex012" 0 "$ROOT_DIR/c/pscc" --check "$ROOT_DIR/tests/cli/manual_ex012.pts"
expect_exit "pscc check manual ex013" 0 "$ROOT_DIR/c/pscc" --check "$ROOT_DIR/tests/cli/manual_ex013.pts"
expect_exit "pscc check manual ex014" 0 "$ROOT_DIR/c/pscc" --check "$ROOT_DIR/tests/cli/manual_ex014.pts"
expect_exit "pscc check manual ex015" 0 "$ROOT_DIR/c/pscc" --check "$ROOT_DIR/tests/cli/manual_ex015.pts"
expect_exit "pscc check manual ex016" 0 "$ROOT_DIR/c/pscc" --check "$ROOT_DIR/tests/cli/manual_ex016.pts"
expect_exit "pscc check manual ex018" 0 "$ROOT_DIR/c/pscc" --check "$ROOT_DIR/tests/cli/manual_ex018.pts"
expect_exit "pscc check manual ex019" 0 "$ROOT_DIR/c/pscc" --check "$ROOT_DIR/tests/cli/manual_ex019.pts"
expect_exit "pscc check manual ex021" 0 "$ROOT_DIR/c/pscc" --check "$ROOT_DIR/tests/cli/manual_ex021.pts"
expect_exit "pscc check manual ex024" 0 "$ROOT_DIR/c/pscc" --check "$ROOT_DIR/tests/cli/manual_ex024.pts"
expect_exit "pscc check empty list no context" 1 "$ROOT_DIR/c/pscc" --check "$ROOT_DIR/tests/invalid/type/empty_list_no_context.pts"
expect_exit "pscc check empty map no context" 1 "$ROOT_DIR/c/pscc" --check "$ROOT_DIR/tests/invalid/type/empty_map_no_context.pts"
expect_exit "pscc check manual ex007" 0 "$ROOT_DIR/c/pscc" --check "$ROOT_DIR/tests/cli/manual_ex007.pts"
expect_exit "static check exit" 2 "$PS" check "$ROOT_DIR/tests/invalid/type/type_mismatch_assignment.pts"
expect_exit "static check uninitialized call arg" 2 "$PS" check "$ROOT_DIR/tests/invalid/type/uninitialized_read_call_arg.pts"
expect_exit "static check empty list no context" 2 "$PS" check "$ROOT_DIR/tests/invalid/type/empty_list_no_context.pts"
expect_exit "static check empty map no context" 2 "$PS" check "$ROOT_DIR/tests/invalid/type/empty_map_no_context.pts"
expect_exit "static check null literal" 2 "$PS" check "$ROOT_DIR/tests/invalid/type/null_literal.pts"
expect_output_contains "ast outputs" "\"kind\"" "$PS" ast "$ROOT_DIR/tests/cli/exit_code.pts"
expect_output_contains "ir outputs" "\"functions\"" "$PS" ir "$ROOT_DIR/tests/cli/exit_code.pts"
expect_exit "trace enabled" 0 "$PS" run "$ROOT_DIR/tests/cli/hello.pts" --trace
expect_output_contains "trace-ir enabled" "[ir]" "$PS" run "$ROOT_DIR/tests/cli/hello.pts" --trace-ir
expect_output_contains "time enabled" "time:" "$PS" run "$ROOT_DIR/tests/cli/hello.pts" --time
expect_output_contains "pscc emit-c outputs C" "int main" "$ROOT_DIR/c/pscc" --emit-c "$ROOT_DIR/tests/cli/exit_code.pts"

expect_output_exact() {
  local desc="$1"
  local expected_file="$2"
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
  if cmp -s /tmp/ps_cli_test.out "$expected_file"; then
    echo "PASS $desc"
    pass=$((pass + 1))
  else
    echo "FAIL $desc"
    echo "  output differs from expected"
    echo "  cmd: $*"
    echo "  expected:"
    sed -n '1,80p' "$expected_file"
    echo "  actual:"
    sed -n '1,80p' /tmp/ps_cli_test.out
    fail=$((fail + 1))
  fi
}

# Manual examples (runtime, exact stdout)
for exp in "$ROOT_DIR/tests/edge/manual_ex"*.expect.json; do
  [[ -f "$exp" ]] || continue
  status="$(json_status "$exp")"
  if [[ "$status" != "accept-runtime" ]]; then
    continue
  fi
  case_id="$(basename "$exp" .expect.json)"
  expected_file="/tmp/ps_cli_expected.out"
  json_stdout "$exp" >"$expected_file"
  expect_output_exact "run $case_id" "$expected_file" "$PS" run "$ROOT_DIR/tests/edge/$case_id.pts"
done

# Manual examples (static errors)
for exp in "$ROOT_DIR/tests/invalid/parse/manual_ex"*.expect.json "$ROOT_DIR/tests/invalid/type/manual_ex"*.expect.json; do
  [[ -f "$exp" ]] || continue
  case_id="$(basename "$exp" .expect.json)"
  suite_dir="$(dirname "$exp")"
  expect_exit "check $case_id" 2 "$PS" check "$suite_dir/$case_id.pts"
done

# Manual examples (runtime errors)
for exp in "$ROOT_DIR/tests/invalid/runtime/manual_ex"*.expect.json; do
  [[ -f "$exp" ]] || continue
  case_id="$(basename "$exp" .expect.json)"
  suite_dir="$(dirname "$exp")"
  expect_exit "run $case_id runtime" 1 "$PS" run "$suite_dir/$case_id.pts"
done

# pscc checks for manual examples
for src in "$ROOT_DIR/tests/edge/manual_ex"*.pts; do
  [[ -f "$src" ]] || continue
  case_id="$(basename "$src" .pts)"
  expect_exit "pscc check $case_id" 0 "$ROOT_DIR/c/pscc" --check "$src"
done

echo
echo "Summary: PASS=$pass FAIL=$fail TOTAL=$((pass + fail))"
if [[ "$fail" -ne 0 ]]; then
  exit 1
fi
