#include <stdio.h>
#include <stdlib.h>

#include "runtime/ps_runtime.h"
#include "runtime/ps_value_impl.h"
#include "runtime/ps_list.h"

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
  for (int i = 0; i < 16; i++) {
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

  PS_Value *view = ps_value_alloc(PS_V_VIEW);
  if (!view) {
    ps_value_release(list);
    ps_ctx_destroy(ctx);
    return 1;
  }
  view->as.view_v.source = ps_value_retain(list);
  view->as.view_v.offset = 0;
  view->as.view_v.len = list->as.list_v.len;
  view->as.view_v.readonly = 1;
  view->as.view_v.version = list->as.list_v.version;
  view->as.view_v.type_name = NULL;

  ps_value_release(list);
  if (!view->as.view_v.source || view->as.view_v.source->tag != PS_V_LIST) {
    fprintf(stderr, "view source invalid after list release\n");
    ps_value_release(view);
    ps_ctx_destroy(ctx);
    return 2;
  }

  ps_value_release(view);
  ps_ctx_destroy(ctx);
  printf("OK\n");
  return 0;
}
