#include <stdlib.h>
#include <string.h>

#include "ps_map.h"
#include "ps_string.h"

static size_t hash_value(PS_Value *v) {
  if (!v) return 0;
  switch (v->tag) {
    case PS_V_BOOL:
      return (size_t)v->as.bool_v * 1315423911u;
    case PS_V_INT:
      return (size_t)v->as.int_v * 2654435761u;
    case PS_V_BYTE:
      return (size_t)v->as.byte_v * 2654435761u;
    case PS_V_GLYPH:
      return (size_t)v->as.glyph_v * 2654435761u;
    case PS_V_STRING:
      return ps_utf8_glyph_len((const uint8_t *)v->as.string_v.ptr, v->as.string_v.len) ^ (size_t)v->as.string_v.len;
    default:
      return (size_t)(uintptr_t)v;
  }
}

static int value_equals(PS_Value *a, PS_Value *b) {
  if (!a || !b) return 0;
  if (a->tag != b->tag) return 0;
  switch (a->tag) {
    case PS_V_BOOL:
      return a->as.bool_v == b->as.bool_v;
    case PS_V_INT:
      return a->as.int_v == b->as.int_v;
    case PS_V_BYTE:
      return a->as.byte_v == b->as.byte_v;
    case PS_V_GLYPH:
      return a->as.glyph_v == b->as.glyph_v;
    case PS_V_STRING:
      return a->as.string_v.len == b->as.string_v.len &&
             memcmp(a->as.string_v.ptr, b->as.string_v.ptr, a->as.string_v.len) == 0;
    default:
      return a == b;
  }
}

static int ensure_cap(PS_Map *m, size_t need) {
  if (m->cap >= need * 2) return 1;
  size_t new_cap = m->cap == 0 ? 8 : m->cap * 2;
  while (new_cap < need * 2) new_cap *= 2;
  PS_Value **nkeys = (PS_Value **)calloc(new_cap, sizeof(PS_Value *));
  PS_Value **nvals = (PS_Value **)calloc(new_cap, sizeof(PS_Value *));
  uint8_t *nused = (uint8_t *)calloc(new_cap, sizeof(uint8_t));
  if (!nkeys || !nvals || !nused) {
    free(nkeys);
    free(nvals);
    free(nused);
    return 0;
  }
  for (size_t i = 0; i < m->cap; i++) {
    if (!m->used[i]) continue;
    PS_Value *k = m->keys[i];
    size_t h = hash_value(k);
    size_t idx = h & (new_cap - 1);
    while (nused[idx]) idx = (idx + 1) & (new_cap - 1);
    nused[idx] = 1;
    nkeys[idx] = k;
    nvals[idx] = m->values[i];
  }
  free(m->keys);
  free(m->values);
  free(m->used);
  m->keys = nkeys;
  m->values = nvals;
  m->used = nused;
  m->cap = new_cap;
  return 1;
}

PS_Value *ps_map_new(PS_Context *ctx) {
  (void)ctx;
  PS_Value *v = ps_value_alloc(PS_V_MAP);
  if (!v) return NULL;
  v->as.map_v.keys = NULL;
  v->as.map_v.values = NULL;
  v->as.map_v.used = NULL;
  v->as.map_v.cap = 0;
  v->as.map_v.len = 0;
  return v;
}

int ps_map_has_key(PS_Context *ctx, PS_Value *map, PS_Value *key) {
  if (!map || map->tag != PS_V_MAP) {
    ps_throw(ctx, PS_ERR_TYPE, "not a map");
    return 0;
  }
  PS_Map *m = &map->as.map_v;
  if (m->cap == 0) return 0;
  size_t h = hash_value(key);
  size_t idx = h & (m->cap - 1);
  for (size_t probes = 0; probes < m->cap; probes++) {
    if (!m->used[idx]) return 0;
    if (value_equals(m->keys[idx], key)) return 1;
    idx = (idx + 1) & (m->cap - 1);
  }
  return 0;
}

PS_Value *ps_map_get(PS_Context *ctx, PS_Value *map, PS_Value *key) {
  if (!map || map->tag != PS_V_MAP) {
    ps_throw(ctx, PS_ERR_TYPE, "not a map");
    return NULL;
  }
  PS_Map *m = &map->as.map_v;
  if (m->cap == 0) {
    ps_throw(ctx, PS_ERR_RANGE, "missing key");
    return NULL;
  }
  size_t h = hash_value(key);
  size_t idx = h & (m->cap - 1);
  for (size_t probes = 0; probes < m->cap; probes++) {
    if (!m->used[idx]) break;
    if (value_equals(m->keys[idx], key)) return m->values[idx];
    idx = (idx + 1) & (m->cap - 1);
  }
  ps_throw(ctx, PS_ERR_RANGE, "missing key");
  return NULL;
}

int ps_map_set(PS_Context *ctx, PS_Value *map, PS_Value *key, PS_Value *value) {
  if (!map || map->tag != PS_V_MAP) {
    ps_throw(ctx, PS_ERR_TYPE, "not a map");
    return 0;
  }
  PS_Map *m = &map->as.map_v;
  if (!ensure_cap(m, m->len + 1)) {
    ps_throw(ctx, PS_ERR_OOM, "out of memory");
    return 0;
  }
  size_t h = hash_value(key);
  size_t idx = h & (m->cap - 1);
  while (m->used[idx]) {
    if (value_equals(m->keys[idx], key)) {
      if (m->values[idx]) ps_value_release(m->values[idx]);
      m->values[idx] = ps_value_retain(value);
      return 1;
    }
    idx = (idx + 1) & (m->cap - 1);
  }
  m->used[idx] = 1;
  m->keys[idx] = ps_value_retain(key);
  m->values[idx] = ps_value_retain(value);
  m->len += 1;
  return 1;
}

size_t ps_map_len(PS_Value *map) {
  if (!map || map->tag != PS_V_MAP) return 0;
  return map->as.map_v.len;
}

PS_Status ps_map_entry(PS_Context *ctx, PS_Value *map, size_t index, PS_Value **out_key, PS_Value **out_value) {
  if (!map || map->tag != PS_V_MAP) {
    ps_throw(ctx, PS_ERR_TYPE, "not a map");
    return PS_ERR;
  }
  PS_Map *m = &map->as.map_v;
  if (index >= m->len) {
    ps_throw(ctx, PS_ERR_RANGE, "index out of bounds");
    return PS_ERR;
  }
  size_t seen = 0;
  for (size_t i = 0; i < m->cap; i++) {
    if (!m->used[i]) continue;
    if (seen == index) {
      if (out_key) *out_key = m->keys[i];
      if (out_value) *out_value = m->values[i];
      return PS_OK;
    }
    seen += 1;
  }
  ps_throw(ctx, PS_ERR_RANGE, "index out of bounds");
  return PS_ERR;
}
