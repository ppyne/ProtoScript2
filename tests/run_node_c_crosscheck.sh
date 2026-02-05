#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TESTS_DIR="$ROOT_DIR/tests"
MANIFEST="$TESTS_DIR/manifest.json"
COMPILER="${COMPILER:-$ROOT_DIR/bin/protoscriptc}"
C_COMPILER="${C_COMPILER:-$ROOT_DIR/c/pscc}"
STRICT_AST=0

for arg in "$@"; do
  case "$arg" in
    --strict-ast)
      STRICT_AST=1
      ;;
    *)
      echo "ERROR: unsupported option '$arg' (supported: --strict-ast)" >&2
      exit 2
      ;;
  esac
done

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
if ! command -v gcc >/dev/null 2>&1; then
  echo "ERROR: gcc is required for Node/C crosscheck." >&2
  exit 2
fi

extract_code() {
  local f="$1"
  grep -Eo '(^|[^A-Z0-9])(E[0-9]{4}|R[0-9]{4})([^A-Z0-9]|$)' "$f" | head -n1 | sed -E 's/.*(E[0-9]{4}|R[0-9]{4}).*/\1/' || true
}

extract_category() {
  local f="$1"
  sed -nE 's/.*(E[0-9]{4}|R[0-9]{4}) ([A-Z0-9_]+):.*/\2/p' "$f" | head -n1 || true
}

pass=0
fail=0

echo "== Node/C Crosscheck (diagnostics + comportement) =="
echo "Compiler: $COMPILER"
echo "C compiler: $C_COMPILER"
echo "Strict AST: $STRICT_AST"
echo

while IFS= read -r case_id; do
  [[ -z "$case_id" ]] && continue
  src="$TESTS_DIR/$case_id.pts"
  expect="$TESTS_DIR/$case_id.expect.json"

  if [[ ! -f "$src" || ! -f "$expect" ]]; then
    echo "FAIL $case_id"
    echo "  missing source or expectation file"
    fail=$((fail + 1))
    continue
  fi

  status="$(jq -r '.status // empty' "$expect")"
  expected_code="$(jq -r '.error_code // empty' "$expect")"
  expected_cat="$(jq -r '.category // empty' "$expect")"
  expected_stdout="$(jq -r '.expected_stdout // empty' "$expect")"

  out_node="$(mktemp)"
  out_c="$(mktemp)"
  c_file="$(mktemp /tmp/ps_cross_all_XXXXXX.c)"
  c_bin="$(mktemp /tmp/ps_cross_all_XXXXXX)"

  rc_node=0
  rc_emit_c=0
  rc_gcc=0
  rc_c=0

  set +e
  case "$status" in
    reject-parse|reject-static)
      "$COMPILER" --check "$src" >"$out_node" 2>&1
      rc_node=$?
      "$COMPILER" --emit-c "$src" >"$out_c" 2>&1
      rc_emit_c=$?
      ;;
    reject-runtime|accept-runtime)
      "$COMPILER" --run "$src" >"$out_node" 2>&1
      rc_node=$?
      "$COMPILER" --emit-c "$src" >"$c_file" 2>"$out_c"
      rc_emit_c=$?
      if [[ $rc_emit_c -eq 0 ]]; then
        gcc -std=c11 -x c -w "$c_file" -o "$c_bin" >>"$out_c" 2>&1
        rc_gcc=$?
        if [[ $rc_gcc -eq 0 ]]; then
          "$c_bin" >>"$out_c" 2>&1
          rc_c=$?
        fi
      fi
      ;;
    *)
      echo "FAIL $case_id"
      echo "  unsupported status: $status"
      rm -f "$out_node" "$out_c" "$c_file" "$c_bin"
      fail=$((fail + 1))
      set -e
      continue
      ;;
  esac
  set -e

  ok=true
  reason=""

  case "$status" in
    reject-parse|reject-static|reject-runtime)
      [[ $rc_node -eq 0 ]] && ok=false && reason="node path should fail"
      [[ $rc_emit_c -ne 0 && "$status" == "reject-runtime" ]] && ok=false && reason="emit-c should succeed for runtime case"
      if [[ "$status" == "reject-runtime" ]]; then
        [[ $rc_gcc -ne 0 ]] && ok=false && reason="gcc compile failed"
        [[ $rc_c -eq 0 ]] && ok=false && reason="c runtime should fail"
      fi

      if [[ "$ok" == true ]]; then
        node_code="$(extract_code "$out_node")"
        node_cat="$(extract_category "$out_node")"
        c_code="$(extract_code "$out_c")"
        c_cat="$(extract_category "$out_c")"

        if [[ -n "$expected_code" && "$node_code" != "$expected_code" ]]; then
          ok=false; reason="node diagnostic code mismatch ($node_code != $expected_code)"
        fi
        if [[ "$ok" == true && -n "$expected_cat" && "$node_cat" != "$expected_cat" ]]; then
          ok=false; reason="node diagnostic category mismatch ($node_cat != $expected_cat)"
        fi
        if [[ "$ok" == true && -n "$expected_code" && "$c_code" != "$expected_code" ]]; then
          ok=false; reason="c diagnostic code mismatch ($c_code != $expected_code)"
        fi
        if [[ "$ok" == true && -n "$expected_cat" && "$c_cat" != "$expected_cat" ]]; then
          ok=false; reason="c diagnostic category mismatch ($c_cat != $expected_cat)"
        fi
        if [[ "$ok" == true && "$node_code" != "$c_code" ]]; then
          ok=false; reason="node/c diagnostic code diverge ($node_code vs $c_code)"
        fi
        if [[ "$ok" == true && "$node_cat" != "$c_cat" ]]; then
          ok=false; reason="node/c diagnostic category diverge ($node_cat vs $c_cat)"
        fi
      fi
      ;;
    accept-runtime)
      [[ $rc_node -ne 0 ]] && ok=false && reason="node runtime should succeed"
      [[ $rc_emit_c -ne 0 ]] && ok=false && reason="emit-c should succeed"
      [[ $rc_gcc -ne 0 ]] && ok=false && reason="gcc compile failed"
      [[ $rc_c -ne 0 ]] && ok=false && reason="c runtime should succeed"
      if [[ "$ok" == true ]]; then
        node_out="$(cat "$out_node")"
        c_out="$(cat "$out_c")"
        if [[ "$node_out" != "$c_out" ]]; then
          ok=false; reason="stdout diverges between node and c"
        fi
        if [[ "$ok" == true && -n "$expected_stdout" && "$node_out" != "$expected_stdout" ]]; then
          ok=false; reason="stdout mismatch against expectation"
        fi
      fi
      ;;
  esac

  if [[ "$ok" == true ]]; then
    echo "PASS $case_id"
    pass=$((pass + 1))
  else
    echo "FAIL $case_id"
    echo "  $reason"
    echo "  --node output:"
    sed 's/^/    /' "$out_node"
    echo "  --c path output:"
    sed 's/^/    /' "$out_c"
    fail=$((fail + 1))
  fi

  rm -f "$out_node" "$out_c" "$c_file" "$c_bin"
done < <(jq -r '.suites | to_entries[] | .value[]' "$MANIFEST")

echo
echo "Summary: PASS=$pass FAIL=$fail TOTAL=$((pass + fail))"
if [[ "$fail" -ne 0 ]]; then
  exit 1
fi

if [[ "$STRICT_AST" == "1" ]]; then
  echo
  echo "== Strict AST check =="
  NODE_COMPILER="$COMPILER" C_COMPILER="$C_COMPILER" "$TESTS_DIR/run_ast_structural_crosscheck.sh"
fi

echo "Node/C crosscheck PASSED."
