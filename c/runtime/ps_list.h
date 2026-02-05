#ifndef PS_LIST_H
#define PS_LIST_H

#include "ps_runtime.h"

PS_Value *ps_list_new(PS_Context *ctx);
size_t ps_list_len_internal(PS_Value *list);
PS_Value *ps_list_get_internal(PS_Context *ctx, PS_Value *list, size_t index);
int ps_list_set_internal(PS_Context *ctx, PS_Value *list, size_t index, PS_Value *value);
int ps_list_push_internal(PS_Context *ctx, PS_Value *list, PS_Value *value);

#endif // PS_LIST_H
