#include <stdio.h>
#include <stdlib.h>

#include "runtime/ps_runtime.h"
#include "runtime/ps_list.h"
#include "runtime/ps_map.h"

int main(void) {
  PS_Context *ctx = ps_ctx_create();
  if (!ctx) {
    fprintf(stderr, "ctx create failed\n");
    return 1;
  }

  PS_Value *list = ps_make_list(ctx);
  if (!list) {
    ps_ctx_destroy(ctx);
    return 1;
  }
  for (int i = 0; i < 50000; i++) {
    PS_Value *v = ps_make_int(ctx, i);
    if (!v) {
      ps_value_release(list);
      ps_ctx_destroy(ctx);
      return 1;
    }
    if (ps_list_push(ctx, list, v) != PS_OK) {
      ps_value_release(v);
      ps_value_release(list);
      ps_ctx_destroy(ctx);
      return 2;
    }
    ps_value_release(v);
  }

  PS_Value *map = ps_make_map(ctx);
  if (!map) {
    ps_value_release(list);
    ps_ctx_destroy(ctx);
    return 1;
  }
  for (int i = 0; i < 20000; i++) {
    PS_Value *k = ps_make_int(ctx, i);
    PS_Value *v = ps_make_int(ctx, i + 1);
    if (!k || !v) {
      if (k) ps_value_release(k);
      if (v) ps_value_release(v);
      ps_value_release(map);
      ps_value_release(list);
      ps_ctx_destroy(ctx);
      return 1;
    }
    if (!ps_map_set(ctx, map, k, v)) {
      ps_value_release(k);
      ps_value_release(v);
      ps_value_release(map);
      ps_value_release(list);
      ps_ctx_destroy(ctx);
      return 2;
    }
    ps_value_release(k);
    ps_value_release(v);
  }
  for (int i = 0; i < 10000; i++) {
    PS_Value *k = ps_make_int(ctx, i * 2);
    if (!k) {
      ps_value_release(map);
      ps_value_release(list);
      ps_ctx_destroy(ctx);
      return 1;
    }
    (void)ps_map_remove(ctx, map, k);
    ps_value_release(k);
  }

  ps_value_release(map);
  ps_value_release(list);
  ps_ctx_destroy(ctx);
  printf("OK\n");
  return 0;
}
