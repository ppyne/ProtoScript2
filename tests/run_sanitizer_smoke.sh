#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ASAN_CFLAGS="-std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -I../include -I."
ASAN_GCC_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"

export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"

SANITIZER_MODULES_TMP_DIR=""
if [[ -z "${PS_MODULE_PATH:-}" ]]; then
  SANITIZER_MODULES_TMP_DIR="$(mktemp -d "${TMPDIR:-/tmp}/ps_modules_sanitizer_XXXXXX")"
  export PS_MODULE_PATH="$SANITIZER_MODULES_TMP_DIR"
fi

cleanup_sanitizer_modules_tmp() {
  if [[ -n "$SANITIZER_MODULES_TMP_DIR" && -d "$SANITIZER_MODULES_TMP_DIR" ]]; then
    rm -rf "$SANITIZER_MODULES_TMP_DIR"
  fi
}
trap cleanup_sanitizer_modules_tmp EXIT

if [[ -x "$ROOT_DIR/scripts/build_modules.sh" ]]; then
  "$ROOT_DIR/scripts/build_modules.sh" >/tmp/ps_sanitizer_modules_build.out 2>&1 || {
    echo "ERROR: failed to build test modules" >&2
    sed -n '1,80p' /tmp/ps_sanitizer_modules_build.out >&2
    exit 2
  }
fi

echo "== Sanitizer Smoke (C + emit-c) =="
echo

echo "-- build C toolchain with ASan/UBSan"
make -C "$ROOT_DIR/c" clean
make -C "$ROOT_DIR/c" CFLAGS="$ASAN_CFLAGS"
echo

echo "-- C runtime smoke"
"$ROOT_DIR/c/ps" run "$ROOT_DIR/tests/cli/hello.pts" >/dev/null
echo

echo "-- emit-c sanitizer policy smoke"
CC="${CC:-cc}" "$ROOT_DIR/tests/robustness/run_asan_ubsan_emitc.sh" "$ROOT_DIR/tests/cli/hello.pts"
echo

echo "-- restore default C toolchain"
make -C "$ROOT_DIR/c" clean
make -C "$ROOT_DIR/c"
echo

echo "SANITIZER SMOKE OK"
