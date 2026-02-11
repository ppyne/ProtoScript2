#include <stdlib.h>
#include <string.h>

#include "ps_map.h"
#include "ps_errors.h"
#include "ps_string.h"

static const char *value_type_name(PS_Value *v) {
  if (!v) return "null";
  switch (v->tag) {
    case PS_V_BOOL: return "bool";
    case PS_V_INT: return "int";
    case PS_V_BYTE: return "byte";
    case PS_V_FLOAT: return "float";
    case PS_V_GLYPH: return "glyph";
    case PS_V_STRING: return "string";
    case PS_V_LIST: return "list";
    case PS_V_MAP: return "map";
    case PS_V_OBJECT: return "object";
    case PS_V_VIEW: return "view";
    case PS_V_EXCEPTION: return "Exception";
    default: return "value";
  }
}

static void format_value_short(PS_Value *v, char *out, size_t out_len) {
  if (!out || out_len == 0) return;
  if (!v) {
    snprintf(out, out_len, "null");
    return;
  }
  switch (v->tag) {
    case PS_V_BOOL:
      snprintf(out, out_len, v->as.bool_v ? "true" : "false");
      return;
    case PS_V_INT:
      snprintf(out, out_len, "%lld", (long long)v->as.int_v);
      return;
    case PS_V_BYTE:
      snprintf(out, out_len, "%u", (unsigned)v->as.byte_v);
      return;
    case PS_V_FLOAT:
      snprintf(out, out_len, "%.17g", v->as.float_v);
      return;
    case PS_V_GLYPH:
      snprintf(out, out_len, "U+%04X", (unsigned)v->as.glyph_v);
      return;
    case PS_V_STRING: {
      const char *s = v->as.string_v.ptr ? v->as.string_v.ptr : "";
      size_t n = v->as.string_v.len;
      size_t cap = (n > 24) ? 24 : n;
      char buf[64];
      size_t bi = 0;
      buf[bi++] = '"';
      for (size_t i = 0; i < cap && bi + 2 < sizeof(buf); i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\n' || c == '\r' || c == '\t') {
          buf[bi++] = ' ';
          continue;
        }
        if (c < 0x20) {
          buf[bi++] = '?';
          continue;
        }
        buf[bi++] = (char)c;
      }
      if (n > cap && bi + 3 < sizeof(buf)) {
        buf[bi++] = '.';
        buf[bi++] = '.';
        buf[bi++] = '.';
      }
      buf[bi++] = '"';
      buf[bi] = '\0';
      snprintf(out, out_len, "%s", buf);
      return;
    }
    default:
      snprintf(out, out_len, "<%s>", value_type_name(v));
      return;
  }
}

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
    if (!m->used[i] || m->used[i] == 2) continue;
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

static int ensure_order_cap(PS_Map *m, size_t need) {
  if (m->order_cap >= need) return 1;
  size_t new_cap = m->order_cap == 0 ? 8 : m->order_cap * 2;
  while (new_cap < need) new_cap *= 2;
  PS_Value **norder = (PS_Value **)realloc(m->order, sizeof(PS_Value *) * new_cap);
  if (!norder) return 0;
  m->order = norder;
  m->order_cap = new_cap;
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
  v->as.map_v.order = NULL;
  v->as.map_v.order_len = 0;
  v->as.map_v.order_cap = 0;
  return v;
}

int ps_map_has_key(PS_Context *ctx, PS_Value *map, PS_Value *key) {
  if (!map || map->tag != PS_V_MAP) {
    char got[64];
    snprintf(got, sizeof(got), "%s", value_type_name(map));
    ps_throw_diag(ctx, PS_ERR_TYPE, "invalid map access", got, "map");
    return 0;
  }
  PS_Map *m = &map->as.map_v;
  if (m->cap == 0) return 0;
  size_t h = hash_value(key);
  size_t idx = h & (m->cap - 1);
  for (size_t probes = 0; probes < m->cap; probes++) {
    if (!m->used[idx]) return 0;
    if (m->used[idx] == 1 && value_equals(m->keys[idx], key)) return 1;
    idx = (idx + 1) & (m->cap - 1);
  }
  return 0;
}

PS_Value *ps_map_get(PS_Context *ctx, PS_Value *map, PS_Value *key) {
  if (!map || map->tag != PS_V_MAP) {
    char got[64];
    snprintf(got, sizeof(got), "%s", value_type_name(map));
    ps_throw_diag(ctx, PS_ERR_TYPE, "invalid map access", got, "map");
    return NULL;
  }
  PS_Map *m = &map->as.map_v;
  if (m->cap == 0) {
    char got[64];
    format_value_short(key, got, sizeof(got));
    ps_throw_diag(ctx, PS_ERR_RANGE, "missing key", got, "present key");
    return NULL;
  }
  size_t h = hash_value(key);
  size_t idx = h & (m->cap - 1);
  for (size_t probes = 0; probes < m->cap; probes++) {
    if (!m->used[idx]) break;
    if (m->used[idx] == 1 && value_equals(m->keys[idx], key)) return m->values[idx];
    idx = (idx + 1) & (m->cap - 1);
  }
  {
    char got[64];
    format_value_short(key, got, sizeof(got));
    ps_throw_diag(ctx, PS_ERR_RANGE, "missing key", got, "present key");
  }
  return NULL;
}

int ps_map_set(PS_Context *ctx, PS_Value *map, PS_Value *key, PS_Value *value) {
  if (!map || map->tag != PS_V_MAP) {
    char got[64];
    snprintf(got, sizeof(got), "%s", value_type_name(map));
    ps_throw_diag(ctx, PS_ERR_TYPE, "invalid map assignment", got, "map");
    return 0;
  }
  PS_Map *m = &map->as.map_v;
  if (!ensure_cap(m, m->len + 1)) {
    ps_throw_diag(ctx, PS_ERR_OOM, "out of memory", "map allocation failed", "available memory");
    return 0;
  }
  size_t h = hash_value(key);
  size_t idx = h & (m->cap - 1);
  size_t tomb = (size_t)-1;
  while (m->used[idx]) {
    if (m->used[idx] == 1 && value_equals(m->keys[idx], key)) {
      if (m->values[idx]) ps_value_release(m->values[idx]);
      m->values[idx] = ps_value_retain(value);
      return 1;
    }
    if (m->used[idx] == 2 && tomb == (size_t)-1) tomb = idx;
    idx = (idx + 1) & (m->cap - 1);
  }
  if (tomb != (size_t)-1) idx = tomb;
  m->used[idx] = 1;
  m->keys[idx] = ps_value_retain(key);
  m->values[idx] = ps_value_retain(value);
  if (!ensure_order_cap(m, m->order_len + 1)) {
    ps_throw_diag(ctx, PS_ERR_OOM, "out of memory", "map allocation failed", "available memory");
    return 0;
  }
  m->order[m->order_len++] = m->keys[idx];
  m->len += 1;
  return 1;
}

int ps_map_remove(PS_Context *ctx, PS_Value *map, PS_Value *key) {
  if (!map || map->tag != PS_V_MAP) {
    char got[64];
    snprintf(got, sizeof(got), "%s", value_type_name(map));
    ps_throw_diag(ctx, PS_ERR_TYPE, "invalid map access", got, "map");
    return 0;
  }
  PS_Map *m = &map->as.map_v;
  if (m->cap == 0 || m->len == 0) return 0;
  size_t h = hash_value(key);
  size_t idx = h & (m->cap - 1);
  for (size_t probes = 0; probes < m->cap; probes++) {
    if (!m->used[idx]) return 0;
    if (m->used[idx] == 1 && value_equals(m->keys[idx], key)) {
      PS_Value *stored_key = m->keys[idx];
      for (size_t i = 0; i < m->order_len; i++) {
        if (m->order[i] == stored_key || value_equals(m->order[i], stored_key)) {
          for (size_t j = i + 1; j < m->order_len; j++) {
            m->order[j - 1] = m->order[j];
          }
          m->order_len -= 1;
          break;
        }
      }
      if (m->keys[idx]) ps_value_release(m->keys[idx]);
      if (m->values[idx]) ps_value_release(m->values[idx]);
      m->keys[idx] = NULL;
      m->values[idx] = NULL;
      m->used[idx] = 2;
      if (m->len > 0) m->len -= 1;
      return 1;
    }
    idx = (idx + 1) & (m->cap - 1);
  }
  return 0;
}

size_t ps_map_len(PS_Value *map) {
  if (!map || map->tag != PS_V_MAP) return 0;
  return map->as.map_v.len;
}

PS_Status ps_map_entry(PS_Context *ctx, PS_Value *map, size_t index, PS_Value **out_key, PS_Value **out_value) {
  if (!map || map->tag != PS_V_MAP) {
    char got[64];
    snprintf(got, sizeof(got), "%s", value_type_name(map));
    ps_throw_diag(ctx, PS_ERR_TYPE, "invalid map access", got, "map");
    return PS_ERR;
  }
  PS_Map *m = &map->as.map_v;
  if (index >= m->order_len) {
    char got[64];
    char expected[64];
    snprintf(got, sizeof(got), "%zu", index);
    if (m->order_len == 0) snprintf(expected, sizeof(expected), "empty map");
    else snprintf(expected, sizeof(expected), "index < %zu", m->order_len);
    ps_throw_diag(ctx, PS_ERR_RANGE, "index out of bounds", got, expected);
    return PS_ERR;
  }
  PS_Value *k = m->order[index];
  if (out_key) *out_key = k;
  if (out_value) *out_value = ps_map_get(ctx, map, k);
  if (ps_last_error_code(ctx) != PS_ERR_NONE) return PS_ERR;
  return PS_OK;
}
