#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "ps_object.h"
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
    case PS_V_GROUP: return "group";
    default: return "value";
  }
}

static uint64_t hash_bytes(const char *s, size_t len) {
  uint64_t h = 1469598103934665603ULL;
  for (size_t i = 0; i < len; i++) {
    h ^= (uint8_t)s[i];
    h *= 1099511628211ULL;
  }
  return h;
}

static int ensure_cap(PS_Object *o, size_t need) {
  if (o->cap >= need) return 1;
  size_t new_cap = o->cap == 0 ? 8 : o->cap * 2;
  while (new_cap < need * 2) new_cap *= 2;
  PS_String *nkeys = (PS_String *)calloc(new_cap, sizeof(PS_String));
  PS_Value **nvals = (PS_Value **)calloc(new_cap, sizeof(PS_Value *));
  uint8_t *nused = (uint8_t *)calloc(new_cap, sizeof(uint8_t));
  if (!nkeys || !nvals || !nused) {
    free(nkeys);
    free(nvals);
    free(nused);
    return 0;
  }
  for (size_t i = 0; i < o->cap; i++) {
    if (!o->used[i]) continue;
    PS_String k = o->keys[i];
    uint64_t h = hash_bytes(k.ptr, k.len);
    size_t idx = (size_t)h & (new_cap - 1);
    while (nused[idx]) idx = (idx + 1) & (new_cap - 1);
    nused[idx] = 1;
    nkeys[idx] = k;
    nvals[idx] = o->values[i];
  }
  free(o->keys);
  free(o->values);
  free(o->used);
  o->keys = nkeys;
  o->values = nvals;
  o->used = nused;
  o->cap = new_cap;
  return 1;
}

PS_Value *ps_object_new(PS_Context *ctx) {
  (void)ctx;
  PS_Value *v = ps_value_alloc(PS_V_OBJECT);
  if (!v) return NULL;
  v->as.object_v.keys = NULL;
  v->as.object_v.values = NULL;
  v->as.object_v.used = NULL;
  v->as.object_v.cap = 0;
  v->as.object_v.len = 0;
  v->as.object_v.proto_name = NULL;
  return v;
}

PS_Value *ps_object_get_str_internal(PS_Context *ctx, PS_Value *obj, const char *key, size_t key_len) {
  if (!obj || obj->tag != PS_V_OBJECT) {
    char got[64];
    snprintf(got, sizeof(got), "%s", value_type_name(obj));
    ps_throw_diag(ctx, PS_ERR_TYPE, "invalid object access", got, "object");
    return NULL;
  }
  PS_Object *o = &obj->as.object_v;
  if (o->cap == 0) return NULL;
  uint64_t h = hash_bytes(key, key_len);
  size_t idx = (size_t)h & (o->cap - 1);
  for (size_t probes = 0; probes < o->cap; probes++) {
    if (!o->used[idx]) return NULL;
    PS_String k = o->keys[idx];
    if (k.len == key_len && memcmp(k.ptr, key, key_len) == 0) return o->values[idx];
    idx = (idx + 1) & (o->cap - 1);
  }
  return NULL;
}

int ps_object_set_str_internal(PS_Context *ctx, PS_Value *obj, const char *key, size_t key_len, PS_Value *value) {
  if (!obj || obj->tag != PS_V_OBJECT) {
    char got[64];
    snprintf(got, sizeof(got), "%s", value_type_name(obj));
    ps_throw_diag(ctx, PS_ERR_TYPE, "invalid object assignment", got, "object");
    return 0;
  }
  PS_Object *o = &obj->as.object_v;
  if (!ensure_cap(o, o->len + 1)) {
    ps_throw_diag(ctx, PS_ERR_OOM, "out of memory", "object allocation failed", "available memory");
    return 0;
  }
  uint64_t h = hash_bytes(key, key_len);
  size_t idx = (size_t)h & (o->cap - 1);
  while (o->used[idx]) {
    PS_String k = o->keys[idx];
    if (k.len == key_len && memcmp(k.ptr, key, key_len) == 0) {
      if (o->values[idx]) ps_value_release(o->values[idx]);
      o->values[idx] = ps_value_retain(value);
      return 1;
    }
    idx = (idx + 1) & (o->cap - 1);
  }
  o->used[idx] = 1;
  o->keys[idx].ptr = (char *)malloc(key_len + 1);
  if (!o->keys[idx].ptr && key_len > 0) {
    ps_throw_diag(ctx, PS_ERR_OOM, "out of memory", "object key allocation failed", "available memory");
    return 0;
  }
  memcpy(o->keys[idx].ptr, key, key_len);
  o->keys[idx].ptr[key_len] = '\0';
  o->keys[idx].len = key_len;
  o->values[idx] = ps_value_retain(value);
  o->len += 1;
  return 1;
}

size_t ps_object_len_internal(PS_Value *obj) {
  if (!obj || obj->tag != PS_V_OBJECT) return 0;
  return obj->as.object_v.len;
}

int ps_object_entry_internal(PS_Context *ctx, PS_Value *obj, size_t index, const char **out_key, size_t *out_len, PS_Value **out_value) {
  if (!obj || obj->tag != PS_V_OBJECT) {
    char got[64];
    snprintf(got, sizeof(got), "%s", value_type_name(obj));
    ps_throw_diag(ctx, PS_ERR_TYPE, "invalid object access", got, "object");
    return 0;
  }
  PS_Object *o = &obj->as.object_v;
  if (index >= o->len) {
    char got[64];
    char expected[64];
    snprintf(got, sizeof(got), "%zu", index);
    if (o->len == 0) snprintf(expected, sizeof(expected), "empty object");
    else snprintf(expected, sizeof(expected), "index < %zu", o->len);
    ps_throw_diag(ctx, PS_ERR_RANGE, "index out of bounds", got, expected);
    return 0;
  }
  size_t seen = 0;
  for (size_t i = 0; i < o->cap; i++) {
    if (!o->used[i]) continue;
    if (seen == index) {
      if (out_key) *out_key = o->keys[i].ptr;
      if (out_len) *out_len = o->keys[i].len;
      if (out_value) *out_value = o->values[i];
      return 1;
    }
    seen += 1;
  }
  {
    char got[64];
    char expected[64];
    snprintf(got, sizeof(got), "%zu", index);
    if (o->len == 0) snprintf(expected, sizeof(expected), "empty object");
    else snprintf(expected, sizeof(expected), "index < %zu", o->len);
    ps_throw_diag(ctx, PS_ERR_RANGE, "index out of bounds", got, expected);
  }
  return 0;
}

const char *ps_object_proto_name_internal(PS_Value *obj) {
  if (!obj || obj->tag != PS_V_OBJECT) return NULL;
  return obj->as.object_v.proto_name;
}

int ps_object_set_proto_name_internal(PS_Context *ctx, PS_Value *obj, const char *name) {
  (void)ctx;
  if (!obj || obj->tag != PS_V_OBJECT) return 0;
  if (obj->as.object_v.proto_name) {
    free(obj->as.object_v.proto_name);
    obj->as.object_v.proto_name = NULL;
  }
  if (name) {
    obj->as.object_v.proto_name = strdup(name);
    if (!obj->as.object_v.proto_name) return 0;
  }
  return 1;
}
