#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="$ROOT_DIR/modules"
SRC_DIR="$ROOT_DIR/tests/modules_src"

mkdir -p "$OUT_DIR"

CC="${CC:-cc}"
CFLAGS="-std=c11 -Wall -Wextra -Werror -O2 -fPIC -I$ROOT_DIR/include"

OS="$(uname -s)"
EXT="so"
LINKFLAGS="-shared"
if [[ "$OS" == "Darwin" ]]; then
  EXT="dylib"
  LINKFLAGS="-dynamiclib -Wl,-undefined,dynamic_lookup"
fi

build_mod() {
  local name="$1"
  local src="$2"
  local out="$OUT_DIR/psmod_${name}.${EXT}"
  shift 2
  $CC $CFLAGS "$src" -o "$out" $LINKFLAGS "$@"
}

build_mod "test_simple" "$SRC_DIR/test_simple.c"
build_mod "test_utf8" "$SRC_DIR/test_utf8.c"
build_mod "test_throw" "$SRC_DIR/test_throw.c"
build_mod "test_noinit" "$SRC_DIR/test_noinit.c"
build_mod "test_badver" "$SRC_DIR/test_badver.c"
build_mod "test_nosym" "$SRC_DIR/test_nosym.c"
build_mod "Math" "$SRC_DIR/math.c" -lm
build_mod "Io" "$SRC_DIR/io.c"
build_mod "JSON" "$SRC_DIR/json.c"
build_mod "Time" "$ROOT_DIR/c/modules/time.c"
build_mod "TimeCivil" "$ROOT_DIR/c/modules/time_civil.c" -lpthread

echo "modules built in $OUT_DIR"
