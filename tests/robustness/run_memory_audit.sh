#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
OUT_DIR="$ROOT_DIR/tests/robustness/bin"
mkdir -p "$OUT_DIR"

MCPP_DIR="$ROOT_DIR/third_party/mcpp"
MCPP_LIB="$MCPP_DIR/lib/libmcpp.a"
if [ ! -f "$MCPP_LIB" ]; then
  make -C "$MCPP_DIR" clean
  make -C "$MCPP_DIR" CFLAGS="-O2 -Wno-deprecated-declarations -fno-common"
fi

BASE_CFLAGS="-std=c11 -Wall -Wextra -Werror -O1 -g -I$ROOT_DIR/include -I$ROOT_DIR -I$ROOT_DIR/c -I$MCPP_DIR"
if [ -n "${CFLAGS:-}" ]; then
  BUILD_CFLAGS="$BASE_CFLAGS $CFLAGS"
else
  BUILD_CFLAGS="$BASE_CFLAGS"
fi

build_test() {
  local name="$1"
  local src="$2"
  cc $BUILD_CFLAGS \
    "$ROOT_DIR/$src" \
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
    -o "$OUT_DIR/$name"
}

build_test "group_lifecycle" "tests/robustness/group_lifecycle.c"
build_test "ir_load_loop" "tests/robustness/ir_load_loop.c"
build_test "proto_clone_stress" "tests/robustness/proto_clone_stress.c"
build_test "collection_growth" "tests/robustness/collection_growth.c"
build_test "view_lifetime" "tests/robustness/view_lifetime.c"

"$OUT_DIR/group_lifecycle" "$ROOT_DIR/stress.pts"
"$OUT_DIR/ir_load_loop" "$ROOT_DIR/stress.pts"
"$OUT_DIR/proto_clone_stress" "$ROOT_DIR/tests/edge/proto_clone_stress.pts"
"$OUT_DIR/collection_growth"
"$OUT_DIR/view_lifetime"

echo "MEMORY AUDIT OK"
