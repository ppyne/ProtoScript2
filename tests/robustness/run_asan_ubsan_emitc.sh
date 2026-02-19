#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
SRC="${1:-$ROOT_DIR/tests/edge/clone_super_initial_divergent.pts}"
COMPILER="${COMPILER:-$ROOT_DIR/bin/protoscriptc}"
CC_BIN="${CC:-cc}"

if [ ! -f "$SRC" ]; then
  echo "ERROR: source not found: $SRC" >&2
  exit 2
fi

TMP_C="$(mktemp /tmp/ps_asan_emitc_XXXXXX.c)"
TMP_IR="$(mktemp /tmp/ps_asan_emitc_XXXXXX.ir.json)"
TMP_BIN="$(mktemp /tmp/ps_asan_emitc_XXXXXX.bin)"
TMP_WARN_RAW="$(mktemp /tmp/ps_asan_emitc_XXXXXX.warn)"
TMP_WARN_STRICT="$(mktemp /tmp/ps_asan_emitc_XXXXXX.strict.warn)"
TMP_IR_PAIRS="$(mktemp /tmp/ps_asan_emitc_XXXXXX.ir.pairs)"
TMP_ASSERT_PAIRS="$(mktemp /tmp/ps_asan_emitc_XXXXXX.assert.pairs)"
TMP_MISSING_ALL="$(mktemp /tmp/ps_asan_emitc_XXXXXX.missing.all)"
TMP_MISSING_BLOCKING="$(mktemp /tmp/ps_asan_emitc_XXXXXX.missing.blocking)"
cleanup() {
  rm -f "$TMP_C" "$TMP_IR" "$TMP_BIN" "$TMP_WARN_RAW" "$TMP_WARN_STRICT" \
    "$TMP_IR_PAIRS" "$TMP_ASSERT_PAIRS" "$TMP_MISSING_ALL" "$TMP_MISSING_BLOCKING"
}
trap cleanup EXIT

BASE_CFLAGS="-std=c11 -Wall -Wextra -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer -g"

"$COMPILER" --emit-c "$SRC" >"$TMP_C"
"$COMPILER" --emit-ir-json "$SRC" >"$TMP_IR"

# Layout coverage check: every IR parent/child pair must have a corresponding _Static_assert pair,
# except the two canonical handle-only exception bases.
jq -r '.module.prototypes[]? | select(.parent != null) | "\(.name):\(.parent)"' "$TMP_IR" | sort -u >"$TMP_IR_PAIRS"
{
  sed -n 's/.*"layout size \([A-Za-z0-9_]*\) >= \([A-Za-z0-9_]*\)".*/\1:\2/p' "$TMP_C"
  sed -n 's/.*"exception base \([A-Za-z0-9_]*\)\.base at offset 0 (parent \([A-Za-z0-9_]*\))".*/\1:\2/p' "$TMP_C"
} | sort -u >"$TMP_ASSERT_PAIRS"

comm -23 "$TMP_IR_PAIRS" "$TMP_ASSERT_PAIRS" >"$TMP_MISSING_ALL" || true
grep -v -E '^(Exception:Object|RuntimeException:Exception)$' "$TMP_MISSING_ALL" >"$TMP_MISSING_BLOCKING" || true
if [ -s "$TMP_MISSING_BLOCKING" ]; then
  echo "ERROR: missing _Static_assert coverage for parent/child pairs:" >&2
  cat "$TMP_MISSING_BLOCKING" >&2
  echo "Allowed exclusions only:" >&2
  echo "Exception:Object" >&2
  echo "RuntimeException:Exception" >&2
  exit 1
fi

# Reference audit build: collect warnings without failing.
set +e
"$CC_BIN" $BASE_CFLAGS "$TMP_C" -o "$TMP_BIN" 2>"$TMP_WARN_RAW"
rc_raw=$?
set -e
if [ $rc_raw -ne 0 ]; then
  echo "ERROR: sanitizer compile failed before warning policy check" >&2
  cat "$TMP_WARN_RAW" >&2
  exit 1
fi

non_whitelist="$(sed -n 's/.*\[\(-W[^]]*\)\].*/\1/p' "$TMP_WARN_RAW" | sort -u | grep -v '^-Wunused-function$' || true)"
if [ -n "$non_whitelist" ]; then
  echo "ERROR: warnings outside whitelist detected:" >&2
  echo "$non_whitelist" >&2
  echo "--- full warnings ---" >&2
  cat "$TMP_WARN_RAW" >&2
  exit 1
fi

# Strict policy build: all warnings are errors except unused runtime helpers.
"$CC_BIN" $BASE_CFLAGS -Werror -Wno-unused-function "$TMP_C" -o "$TMP_BIN" 2>"$TMP_WARN_STRICT"

if [ -s "$TMP_WARN_STRICT" ]; then
  echo "ERROR: strict sanitizer build emitted warnings" >&2
  cat "$TMP_WARN_STRICT" >&2
  exit 1
fi

"$TMP_BIN" >/dev/null
echo "ASAN/UBSAN emit-c warning policy OK ($SRC)"
