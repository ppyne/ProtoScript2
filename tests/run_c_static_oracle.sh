#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TESTS_DIR="$ROOT_DIR/tests"
MANIFEST="$TESTS_DIR/manifest.json"
NODE_COMPILER="${NODE_COMPILER:-$ROOT_DIR/bin/protoscriptc}"
C_COMPILER="${C_COMPILER:-$ROOT_DIR/c/pscc}"
export PS_MODULE_REGISTRY="${PS_MODULE_REGISTRY:-$ROOT_DIR/modules/registry.json}"

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

extract_code() {
  local f="$1"
  grep -Eo '(^|[^A-Z0-9])(E[0-9]{4}|R[0-9]{4})([^A-Z0-9]|$)' "$f" | head -n1 | sed -E 's/.*(E[0-9]{4}|R[0-9]{4}).*/\1/' || true
}
extract_category() {
  local f="$1"
  sed -nE 's/.*(E[0-9]{4}|R[0-9]{4}) ([A-Z0-9_]+):.*/\2/p' "$f" | head -n1 || true
}
extract_line_col() {
  local f="$1"
  sed -nE 's/^.*:([0-9]+):([0-9]+) (E[0-9]{4}|R[0-9]{4}) .*/\1:\2/p' "$f" | head -n1 || true
}

pass=0
fail=0

echo "== C Static Oracle Crosscheck =="
echo "Node compiler: $NODE_COMPILER"
echo "C compiler:    $C_COMPILER"
echo

while IFS= read -r case_id; do
  [[ -z "$case_id" ]] && continue
  if [[ -n "${CROSSCHECK_ONLY:-}" ]]; then
    case ",$CROSSCHECK_ONLY," in
      *,"$case_id",*) ;;
      *) continue ;;
    esac
  fi
  if [[ -n "${CROSSCHECK_START_AFTER:-}" ]]; then
    if [[ "$case_id" == "$CROSSCHECK_START_AFTER" ]]; then
      CROSSCHECK_START_AFTER=""
      continue
    else
      continue
    fi
  fi
  if [[ -n "${CROSSCHECK_MAX_CASES:-}" && "${CROSSCHECK_MAX_CASES}" -le 0 ]]; then
    break
  fi
  if [[ "${CROSSCHECK_TRACE:-0}" == "1" ]]; then
    echo "RUN $case_id"
  fi
  src="$TESTS_DIR/$case_id.pts"
  expect="$TESTS_DIR/$case_id.expect.json"
  status="$(jq -r '.status // empty' "$expect")"
  if [[ "$status" != "reject-static" ]]; then
    continue
  fi

  out_node="$(mktemp)"
  out_c="$(mktemp)"

  set +e
  "$NODE_COMPILER" --check "$src" >"$out_node" 2>&1
  rc_node=$?
  "$C_COMPILER" --check-c-static "$src" >"$out_c" 2>&1
  rc_c=$?
  set -e

  ok=true
  reason=""
  [[ $rc_node -eq 0 ]] && ok=false && reason="node --check should fail"
  [[ $rc_c -eq 0 ]] && ok=false && reason="c --check-c-static should fail"

  node_code="$(extract_code "$out_node")"
  node_cat="$(extract_category "$out_node")"
  node_pos="$(extract_line_col "$out_node")"
  c_code="$(extract_code "$out_c")"
  c_cat="$(extract_category "$out_c")"
  c_pos="$(extract_line_col "$out_c")"

  if [[ "$ok" == true && "$node_code" != "$c_code" ]]; then ok=false; reason="diagnostic code mismatch ($node_code vs $c_code)"; fi
  if [[ "$ok" == true && "$node_cat" != "$c_cat" ]]; then ok=false; reason="diagnostic category mismatch ($node_cat vs $c_cat)"; fi
  if [[ "$ok" == true && "$node_pos" != "$c_pos" ]]; then ok=false; reason="diagnostic position mismatch ($node_pos vs $c_pos)"; fi

  if [[ "$ok" == true ]]; then
    echo "PASS $case_id"
    pass=$((pass + 1))
  else
    echo "FAIL $case_id"
    echo "  $reason"
    echo "  --node output:"; sed 's/^/    /' "$out_node"
    echo "  --c output:"; sed 's/^/    /' "$out_c"
    fail=$((fail + 1))
  fi

  rm -f "$out_node" "$out_c"
  if [[ -n "${CROSSCHECK_MAX_CASES:-}" ]]; then
    CROSSCHECK_MAX_CASES=$((CROSSCHECK_MAX_CASES - 1))
  fi
done < <(jq -r '.suites | to_entries[] | .value[]' "$MANIFEST")

echo
 echo "Summary: PASS=$pass FAIL=$fail TOTAL=$((pass + fail))"
if [[ $fail -ne 0 ]]; then exit 1; fi

echo "C static oracle crosscheck PASSED."
