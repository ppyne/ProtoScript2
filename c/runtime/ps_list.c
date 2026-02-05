#include <stdlib.h>

#include "ps_list.h"

static int ensure_cap(PS_List *l, size_t need) {
  if (need <= l->cap) return 1;
  size_t new_cap = l->cap == 0 ? 8 : l->cap * 2;
  while (new_cap < need) new_cap *= 2;
  PS_Value **n = (PS_Value **)realloc(l->items, sizeof(PS_Value *) * new_cap);
  if (!n) return 0;
  l->items = n;
  l->cap = new_cap;
  return 1;
}

PS_Value *ps_list_new(PS_Context *ctx) {
  (void)ctx;
  PS_Value *v = ps_value_alloc(PS_V_LIST);
  if (!v) return NULL;
  v->as.list_v.items = NULL;
  v->as.list_v.len = 0;
  v->as.list_v.cap = 0;
  return v;
}

size_t ps_list_len_internal(PS_Value *list) {
  return list ? list->as.list_v.len : 0;
}

PS_Value *ps_list_get_internal(PS_Context *ctx, PS_Value *list, size_t index) {
  if (!list || list->tag != PS_V_LIST) {
    ps_throw(ctx, PS_ERR_TYPE, "not a list");
    return NULL;
  }
  if (index >= list->as.list_v.len) {
    ps_throw(ctx, PS_ERR_RANGE, "index out of bounds");
    return NULL;
  }
  return list->as.list_v.items[index];
}

int ps_list_set_internal(PS_Context *ctx, PS_Value *list, size_t index, PS_Value *value) {
  if (!list || list->tag != PS_V_LIST) {
    ps_throw(ctx, PS_ERR_TYPE, "not a list");
    return 0;
  }
  if (index >= list->as.list_v.len) {
    ps_throw(ctx, PS_ERR_RANGE, "index out of bounds");
    return 0;
  }
  if (list->as.list_v.items[index]) ps_value_release(list->as.list_v.items[index]);
  list->as.list_v.items[index] = ps_value_retain(value);
  return 1;
}

int ps_list_push_internal(PS_Context *ctx, PS_Value *list, PS_Value *value) {
  if (!list || list->tag != PS_V_LIST) {
    ps_throw(ctx, PS_ERR_TYPE, "not a list");
    return 0;
  }
  if (!ensure_cap(&list->as.list_v, list->as.list_v.len + 1)) {
    ps_throw(ctx, PS_ERR_OOM, "out of memory");
    return 0;
  }
  list->as.list_v.items[list->as.list_v.len++] = ps_value_retain(value);
  return 1;
}
