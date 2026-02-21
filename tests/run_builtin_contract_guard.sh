#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

SEALED_NON_CLONABLE=(TextFile BinaryFile Dir Walker RegExp PathInfo PathEntry RegExpMatch ProcessEvent ProcessResult)
CLONABLE_NON_SEALED=(CivilDateTime)

echo "== Builtin Contract Guard =="

# 1) IR non-clonable builtin list
for h in "${SEALED_NON_CLONABLE[@]}"; do
  if ! rg -n "\"${h}\"" "$ROOT_DIR/src/ir.js" >/dev/null; then
    echo "FAIL: src/ir.js missing non-clonable builtin ${h}" >&2
    exit 1
  fi
done

# 2) JS frontend builtins sealed
for h in "${SEALED_NON_CLONABLE[@]}"; do
  if ! rg -n "this\.prototypes\.set\(\"${h}\".*sealed: true" "$ROOT_DIR/src/frontend.js" >/dev/null; then
    echo "FAIL: src/frontend.js ${h} must be sealed: true" >&2
    exit 1
  fi
done
for h in "${CLONABLE_NON_SEALED[@]}"; do
  if rg -n "this\.prototypes\.set\(\"${h}\".*sealed: true" "$ROOT_DIR/src/frontend.js" >/dev/null; then
    echo "FAIL: src/frontend.js ${h} must stay non-sealed" >&2
    exit 1
  fi
done

# 3) C frontend builtins sealed
for h in "${SEALED_NON_CLONABLE[@]}"; do
  if ! rg -n "proto_find\(a->protos, \"${h}\"\)|sealed = 1" "$ROOT_DIR/c/frontend.c" >/dev/null; then
    echo "FAIL: c/frontend.c ${h} must be sealed" >&2
    exit 1
  fi
done

# 4) Runtime metadata sealed consistency
for h in "${SEALED_NON_CLONABLE[@]}"; do
  if ! awk -v h="$h" '
    BEGIN { seen = 0; ok = 0 }
    index($0, "name: \"" h "\"") { seen = 1; next }
    seen && index($0, "name: \"") { exit ok ? 0 : 1 }
    seen && index($0, "sealed: true") { ok = 1; exit 0 }
    END { if (!seen || !ok) exit 1 }
  ' "$ROOT_DIR/src/runtime.js"; then
    echo "FAIL: src/runtime.js ${h} must be sealed" >&2
    exit 1
  fi
done
for h in "${CLONABLE_NON_SEALED[@]}"; do
  if ! awk -v h="$h" '
    BEGIN { seen = 0; bad = 0 }
    index($0, "name: \"" h "\"") { seen = 1; next }
    seen && index($0, "name: \"") { exit bad ? 1 : 0 }
    seen && index($0, "sealed: true") { bad = 1; exit 1 }
    END { if (!seen) exit 1; if (bad) exit 1 }
  ' "$ROOT_DIR/src/runtime.js"; then
    echo "FAIL: src/runtime.js ${h} must stay non-sealed" >&2
    exit 1
  fi
done

# 5) Clone not supported expectations remain present for sealed builtins
for h in textfile binaryfile dir walker regexp pathinfo pathentry regexpmatch processevent processresult; do
  for mode in direct; do
    expect="$ROOT_DIR/tests/edge/handle_clone_${h}_${mode}.expect.json"
    if [[ ! -f "$expect" ]]; then
      echo "FAIL: missing test expect for handle clone ${h} ${mode}" >&2
      exit 1
    fi
    if ! rg -n '"error_code": "R1013"' "$expect" >/dev/null; then
      echo "FAIL: handle clone ${h} ${mode} must assert R1013" >&2
      exit 1
    fi
  done
done

# 6) Structural behavioral checks (not grep-only)
CC_NODE="$ROOT_DIR/bin/protoscriptc"
CC_C="$ROOT_DIR/c/pscc"
CLI_C="$ROOT_DIR/c/ps"

for case in \
  "$ROOT_DIR/tests/invalid/type/builtin_sealed_inheritance_textfile.pts" \
  "$ROOT_DIR/tests/invalid/type/builtin_sealed_inheritance_binaryfile.pts" \
  "$ROOT_DIR/tests/invalid/type/builtin_sealed_inheritance_walker.pts" \
  "$ROOT_DIR/tests/invalid/type/builtin_sealed_inheritance_regexp.pts" \
  "$ROOT_DIR/tests/invalid/type/builtin_sealed_inheritance_dir.pts" \
  "$ROOT_DIR/tests/invalid/type/builtin_sealed_inheritance_pathinfo.pts" \
  "$ROOT_DIR/tests/invalid/type/builtin_sealed_inheritance_pathentry.pts" \
  "$ROOT_DIR/tests/invalid/type/builtin_sealed_inheritance_regexpmatch.pts" \
  "$ROOT_DIR/tests/invalid/type/builtin_sealed_inheritance_processevent.pts" \
  "$ROOT_DIR/tests/invalid/type/builtin_sealed_inheritance_processresult.pts"; do
  out_node="$($CC_NODE --check "$case" 2>&1 || true)"
  if [[ "$out_node" != *"E3140"* || "$out_node" != *"cannot inherit from sealed prototype"* ]]; then
    echo "FAIL: Node compiler must reject sealed inheritance for $case with E3140" >&2
    exit 1
  fi
  out_c="$($CC_C --check "$case" 2>&1 || true)"
  if [[ "$out_c" != *"E3140"* || "$out_c" != *"cannot inherit from sealed prototype"* ]]; then
    echo "FAIL: C compiler must reject sealed inheritance for $case with E3140" >&2
    exit 1
  fi
done

for case in \
  "$ROOT_DIR/tests/edge/handle_clone_textfile_direct.pts" \
  "$ROOT_DIR/tests/edge/handle_clone_binaryfile_direct.pts" \
  "$ROOT_DIR/tests/edge/handle_clone_dir_direct.pts" \
  "$ROOT_DIR/tests/edge/handle_clone_walker_direct.pts" \
  "$ROOT_DIR/tests/edge/handle_clone_regexp_direct.pts" \
  "$ROOT_DIR/tests/edge/handle_clone_pathinfo_direct.pts" \
  "$ROOT_DIR/tests/edge/handle_clone_pathentry_direct.pts" \
  "$ROOT_DIR/tests/edge/handle_clone_regexpmatch_direct.pts" \
  "$ROOT_DIR/tests/edge/handle_clone_processevent_direct.pts" \
  "$ROOT_DIR/tests/edge/handle_clone_processresult_direct.pts"; do
  run_node="$($CC_NODE --run "$case" 2>&1 || true)"
  if [[ "$run_node" != *"R1013"* ]]; then
    echo "FAIL: Node runtime must raise R1013 for $case" >&2
    exit 1
  fi
  run_c="$($CLI_C run "$case" 2>&1 || true)"
  if [[ "$run_c" != *"R1013"* ]]; then
    echo "FAIL: C CLI runtime must raise R1013 for $case" >&2
    exit 1
  fi
done

run_node_dt="$($CC_NODE --run "$ROOT_DIR/tests/edge/civildatetime_clone_allowed.pts" 2>&1 || true)"
if [[ "$run_node_dt" == *"R1013"* ]]; then
  echo "FAIL: Node runtime must keep CivilDateTime clonable" >&2
  exit 1
fi
run_c_dt="$($CLI_C run "$ROOT_DIR/tests/edge/civildatetime_clone_allowed.pts" 2>&1 || true)"
if [[ "$run_c_dt" == *"R1013"* ]]; then
  echo "FAIL: C CLI runtime must keep CivilDateTime clonable" >&2
  exit 1
fi

echo "Builtin contract guard PASSED"
