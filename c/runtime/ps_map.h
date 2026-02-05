#ifndef PS_MAP_H
#define PS_MAP_H

#include "ps_runtime.h"

PS_Value *ps_map_new(PS_Context *ctx);
int ps_map_has_key(PS_Context *ctx, PS_Value *map, PS_Value *key);
PS_Value *ps_map_get(PS_Context *ctx, PS_Value *map, PS_Value *key);
int ps_map_set(PS_Context *ctx, PS_Value *map, PS_Value *key, PS_Value *value);

#endif // PS_MAP_H
