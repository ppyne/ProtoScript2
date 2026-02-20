#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

H=(TextFile BinaryFile Dir Walker RegExp)

echo "== Builtin Contract Guard =="

# 1) IR non-clonable handle list
if ! rg -n 'const handles = \["TextFile", "BinaryFile", "Dir", "Walker", "RegExp"\]' "$ROOT_DIR/src/ir.js" >/dev/null; then
  echo "FAIL: src/ir.js handle non-clonable list mismatch" >&2
  exit 1
fi

# 2) JS frontend builtins sealed
for h in "${H[@]}"; do
  if ! rg -n "this\.prototypes\.set\(\"${h}\".*sealed: true" "$ROOT_DIR/src/frontend.js" >/dev/null; then
    echo "FAIL: src/frontend.js ${h} must be sealed: true" >&2
    exit 1
  fi
done

# 3) C frontend builtins sealed
if ! rg -n 'tf->sealed = 1;' "$ROOT_DIR/c/frontend.c" >/dev/null; then
  echo "FAIL: c/frontend.c TextFile must be sealed" >&2
  exit 1
fi
if ! rg -n 'bf->sealed = 1;' "$ROOT_DIR/c/frontend.c" >/dev/null; then
  echo "FAIL: c/frontend.c BinaryFile must be sealed" >&2
  exit 1
fi
for h in Dir Walker RegExp; do
  if ! rg -n "ProtoInfo \*.*= proto_find\(a->protos, \"${h}\"\);|->sealed = 1;" "$ROOT_DIR/c/frontend.c" >/dev/null; then
    echo "FAIL: c/frontend.c ${h} must be sealed" >&2
    exit 1
  fi
done

# 4) Runtime metadata sealed consistency
for h in "${H[@]}"; do
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

# 5) Clone not supported expectations remain present for handles (direct + inherited + super)
for h in textfile binaryfile dir walker regexp; do
  for mode in direct inherited super; do
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

echo "Builtin contract guard PASSED"
