#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PS="${PS:-$ROOT_DIR/c/ps}"
NODE_COMPILER="${NODE_COMPILER:-$ROOT_DIR/bin/protoscriptc}"
BUILTIN_CLONE_INSTANCE_MODULES="${BUILTIN_CLONE_INSTANCE_MODULES:-1}"
MODULES_BUILT=0
MODULES_TMP_DIR=""

if [[ ! -x "$PS" ]]; then
  echo "ERROR: missing CLI runtime: $PS" >&2
  exit 2
fi
if [[ ! -x "$NODE_COMPILER" ]]; then
  echo "ERROR: missing node compiler: $NODE_COMPILER" >&2
  exit 2
fi

if [[ "$BUILTIN_CLONE_INSTANCE_MODULES" == "1" ]]; then
  export PS_MODULE_REGISTRY="${PS_MODULE_REGISTRY:-$ROOT_DIR/modules/registry.json}"
  if [[ -z "${PS_MODULE_PATH:-}" ]]; then
    MODULES_TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/ps_modules_clone_instance_XXXXXX")"
    export PS_MODULE_PATH="$MODULES_TMP_DIR"
  fi
fi

cleanup_modules_tmp() {
  if [[ -n "$MODULES_TMP_DIR" && -d "$MODULES_TMP_DIR" ]]; then
    rm -rf "$MODULES_TMP_DIR"
  fi
}
trap cleanup_modules_tmp EXIT

if [[ "$BUILTIN_CLONE_INSTANCE_MODULES" == "1" && "$MODULES_BUILT" == "0" ]]; then
  if [[ -x "$ROOT_DIR/scripts/build_modules.sh" ]]; then
    "$ROOT_DIR/scripts/build_modules.sh" >/tmp/ps_clone_instance_modules_build.out 2>&1 || {
      echo "ERROR: failed to build test modules" >&2
      sed -n '1,80p' /tmp/ps_clone_instance_modules_build.out >&2
      exit 2
    }
    MODULES_BUILT=1
  else
    echo "ERROR: module build script missing: $ROOT_DIR/scripts/build_modules.sh" >&2
    exit 2
  fi
fi

reject_cases=(
  "tests/edge/handle_clone_textfile_instance.pts"
  "tests/edge/handle_clone_binaryfile_instance.pts"
  "tests/edge/handle_clone_dir_instance.pts"
  "tests/edge/handle_clone_walker_instance.pts"
  "tests/edge/handle_clone_regexp_instance.pts"
  "tests/edge/handle_clone_pathinfo_instance.pts"
  "tests/edge/handle_clone_pathentry_instance.pts"
  "tests/edge/handle_clone_regexpmatch_instance.pts"
  "tests/edge/handle_clone_processevent_instance.pts"
  "tests/edge/handle_clone_processresult_instance.pts"
)

pass=0
fail=0

echo "== Builtin Clone Instance Contract (C/JS) =="
echo "Node compiler: $NODE_COMPILER"
echo "CLI runtime:   $PS"
echo

norm() {
  sed -E "s#^$ROOT_DIR/##; s#^tests/#tests/#; s#^(.*/)?tests/#tests/#"
}

for case_path in "${reject_cases[@]}"; do
  out_c="$(mktemp)"; err_c="$(mktemp)"
  out_n="$(mktemp)"; err_n="$(mktemp)"
  err_c_norm="$(mktemp)"; err_n_norm="$(mktemp)"
  ok=true
  reason=""

  set +e
  "$PS" run "$ROOT_DIR/$case_path" >"$out_c" 2>"$err_c"
  rc_c=$?
  "$NODE_COMPILER" --run "$ROOT_DIR/$case_path" >"$out_n" 2>"$err_n"
  rc_n=$?
  set -e

  norm <"$err_c" >"$err_c_norm"
  norm <"$err_n" >"$err_n_norm"

  if [[ $rc_c -eq 0 || $rc_n -eq 0 ]]; then
    ok=false
    reason="expected runtime failure with R1013 (rc c=$rc_c node=$rc_n)"
  elif ! grep -q "R1013 RUNTIME_CLONE_NOT_SUPPORTED" "$err_c_norm"; then
    ok=false
    reason="c/ps missing R1013 diagnostic"
  elif ! grep -q "R1013 RUNTIME_CLONE_NOT_SUPPORTED" "$err_n_norm"; then
    ok=false
    reason="node runtime missing R1013 diagnostic"
  elif grep -q "unknown method" "$err_c_norm" || grep -q "unknown method" "$err_n_norm"; then
    ok=false
    reason="found forbidden 'unknown method' diagnostic"
  elif ! cmp -s "$err_c_norm" "$err_n_norm"; then
    ok=false
    reason="diagnostic mismatch between c/ps and node runtime"
  fi

  if [[ "$ok" == true ]]; then
    echo "PASS ${case_path#tests/}"
    pass=$((pass + 1))
  else
    echo "FAIL ${case_path#tests/}"
    echo "  $reason"
    echo "  c/ps stderr: $(tr '\n' ' ' <"$err_c_norm")"
    echo "  node stderr: $(tr '\n' ' ' <"$err_n_norm")"
    fail=$((fail + 1))
  fi

  rm -f "$out_c" "$err_c" "$out_n" "$err_n" "$err_c_norm" "$err_n_norm"
done

out_c="$(mktemp)"; err_c="$(mktemp)"
out_n="$(mktemp)"; err_n="$(mktemp)"
set +e
"$PS" run "$ROOT_DIR/tests/edge/civildatetime_clone_distinct.pts" >"$out_c" 2>"$err_c"
rc_c=$?
"$NODE_COMPILER" --run "$ROOT_DIR/tests/edge/civildatetime_clone_distinct.pts" >"$out_n" 2>"$err_n"
rc_n=$?
set -e

if [[ $rc_c -eq 0 && $rc_n -eq 0 && "$(cat "$out_c")" == "true" && "$(cat "$out_n")" == "true" ]]; then
  echo "PASS edge/civildatetime_clone_distinct"
  pass=$((pass + 1))
else
  echo "FAIL edge/civildatetime_clone_distinct"
  echo "  expected stdout=true and rc=0 for c/ps and node"
  echo "  rc c=$rc_c rc node=$rc_n"
  echo "  c stdout: $(tr '\n' ' ' <"$out_c")"
  echo "  c stderr: $(tr '\n' ' ' <"$err_c")"
  echo "  n stdout: $(tr '\n' ' ' <"$out_n")"
  echo "  n stderr: $(tr '\n' ' ' <"$err_n")"
  fail=$((fail + 1))
fi
rm -f "$out_c" "$err_c" "$out_n" "$err_n"

total=$((pass + fail))
echo
echo "Summary: PASS=$pass FAIL=$fail TOTAL=$total"
if [[ $fail -ne 0 ]]; then
  exit 1
fi
