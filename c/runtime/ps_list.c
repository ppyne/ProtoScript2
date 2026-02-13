#include <stdlib.h>
#include <string.h>

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
  v->as.list_v.version = 0;
  v->as.list_v.type_name = NULL;
  return v;
}

size_t ps_list_len_internal(PS_Value *list) {
  return list ? list->as.list_v.len : 0;
}

PS_Value *ps_list_get_internal(PS_Context *ctx, PS_Value *list, size_t index) {
  if (!list || list->tag != PS_V_LIST) {
    ps_throw_diag(ctx, PS_ERR_TYPE, "invalid list access", "non-list value", "list");
    return NULL;
  }
  if (index >= list->as.list_v.len) {
    char got[64];
    char expected[96];
    snprintf(got, sizeof(got), "%zu", index);
    if (list->as.list_v.len == 0) snprintf(expected, sizeof(expected), "empty list (no valid index)");
    else snprintf(expected, sizeof(expected), "0..%zu", list->as.list_v.len - 1);
    ps_throw_diag(ctx, PS_ERR_RANGE, "index out of bounds", got, expected);
    return NULL;
  }
  return list->as.list_v.items[index];
}

int ps_list_set_internal(PS_Context *ctx, PS_Value *list, size_t index, PS_Value *value) {
  if (!list || list->tag != PS_V_LIST) {
    ps_throw_diag(ctx, PS_ERR_TYPE, "invalid list assignment", "non-list value", "list");
    return 0;
  }
  if (index >= list->as.list_v.len) {
    char got[64];
    char expected[96];
    snprintf(got, sizeof(got), "%zu", index);
    if (list->as.list_v.len == 0) snprintf(expected, sizeof(expected), "empty list (no valid index)");
    else snprintf(expected, sizeof(expected), "0..%zu", list->as.list_v.len - 1);
    ps_throw_diag(ctx, PS_ERR_RANGE, "index out of bounds", got, expected);
    return 0;
  }
  if (list->as.list_v.items[index]) ps_value_release(list->as.list_v.items[index]);
  list->as.list_v.items[index] = ps_value_retain(value);
  return 1;
}

int ps_list_push_internal(PS_Context *ctx, PS_Value *list, PS_Value *value) {
  if (!list || list->tag != PS_V_LIST) {
    ps_throw_diag(ctx, PS_ERR_TYPE, "invalid list push", "non-list value", "list");
    return 0;
  }
  if (!ensure_cap(&list->as.list_v, list->as.list_v.len + 1)) {
  ps_throw_diag(ctx, PS_ERR_OOM, "out of memory", "list allocation failed", "available memory");
  return 0;
}
  list->as.list_v.items[list->as.list_v.len++] = ps_value_retain(value);
  list->as.list_v.version += 1;
  return 1;
}

const char *ps_list_type_name_internal(PS_Value *list) {
  if (!list || list->tag != PS_V_LIST) return NULL;
  return list->as.list_v.type_name;
}

int ps_list_set_type_name_internal(PS_Context *ctx, PS_Value *list, const char *name) {
  (void)ctx;
  if (!list || list->tag != PS_V_LIST) return 0;
  if (list->as.list_v.type_name) {
    free(list->as.list_v.type_name);
    list->as.list_v.type_name = NULL;
  }
  if (name) {
    list->as.list_v.type_name = strdup(name);
    if (!list->as.list_v.type_name) return 0;
  }
  return 1;
}
