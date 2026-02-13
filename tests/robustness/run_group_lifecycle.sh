#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_BIN="$ROOT_DIR/tests/robustness/group_lifecycle"

MCPP_DIR="$ROOT_DIR/third_party/mcpp"
MCPP_LIB="$MCPP_DIR/lib/libmcpp.a"
if [ ! -f "$MCPP_LIB" ]; then
  make -C "$MCPP_DIR" clean
  make -C "$MCPP_DIR" CFLAGS="-O2 -Wno-deprecated-declarations"
fi

CFLAGS="-std=c11 -Wall -Wextra -Werror -O1 -g -I$ROOT_DIR/include -I$ROOT_DIR -I$ROOT_DIR/c -I$MCPP_DIR"

cc $CFLAGS \
  "$ROOT_DIR/tests/robustness/group_lifecycle.c" \
  "$ROOT_DIR/c/frontend.c" \
  "$ROOT_DIR/c/preprocess.c" \
  "$ROOT_DIR/c/diag.c" \
  "$ROOT_DIR/c/runtime/ps_api.c" \
  "$ROOT_DIR/c/runtime/ps_errors.c" \
  "$ROOT_DIR/c/runtime/ps_heap.c" \
  "$ROOT_DIR/c/runtime/ps_value.c" \
  "$ROOT_DIR/c/runtime/ps_string.c" \
  "$ROOT_DIR/c/runtime/ps_list.c" \
  "$ROOT_DIR/c/runtime/ps_object.c" \
  "$ROOT_DIR/c/runtime/ps_map.c" \
  "$ROOT_DIR/c/runtime/ps_dynlib_posix.c" \
  "$ROOT_DIR/c/runtime/ps_json.c" \
  "$ROOT_DIR/c/runtime/ps_modules.c" \
  "$ROOT_DIR/c/runtime/ps_vm.c" \
  "$ROOT_DIR/c/modules/debug.c" \
  "$MCPP_LIB" \
  -Dps_module_init=ps_module_init_Debug \
  -ldl \
  -o "$OUT_BIN"

"$OUT_BIN" "$ROOT_DIR/stress.pts"
