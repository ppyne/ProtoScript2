#ifndef PS_OBJECT_H
#define PS_OBJECT_H

#include "ps_runtime.h"

PS_Value *ps_object_new(PS_Context *ctx);
PS_Value *ps_object_get_str_internal(PS_Context *ctx, PS_Value *obj, const char *key, size_t key_len);
int ps_object_set_str_internal(PS_Context *ctx, PS_Value *obj, const char *key, size_t key_len, PS_Value *value);

#endif // PS_OBJECT_H
