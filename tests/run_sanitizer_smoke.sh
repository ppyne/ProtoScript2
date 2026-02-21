#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ASAN_CFLAGS="-std=c11 -Wall -Wextra -Werror -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer -I../include -I."
ASAN_GCC_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"

export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=0}"

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
