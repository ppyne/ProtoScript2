#ifndef PS_MAP_H
#define PS_MAP_H

#include "ps_runtime.h"

PS_Value *ps_map_new(PS_Context *ctx);
int ps_map_has_key(PS_Context *ctx, PS_Value *map, PS_Value *key);
PS_Value *ps_map_get(PS_Context *ctx, PS_Value *map, PS_Value *key);
int ps_map_set(PS_Context *ctx, PS_Value *map, PS_Value *key, PS_Value *value);
int ps_map_remove(PS_Context *ctx, PS_Value *map, PS_Value *key);
size_t ps_map_len(PS_Value *map);
PS_Status ps_map_entry(PS_Context *ctx, PS_Value *map, size_t index, PS_Value **out_key, PS_Value **out_value);
const char *ps_map_type_name_internal(PS_Value *map);
int ps_map_set_type_name_internal(PS_Context *ctx, PS_Value *map, const char *name);

#endif // PS_MAP_H
