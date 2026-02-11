#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include "ps/ps_api.h"
#include "ps_list.h"
#include "ps_map.h"
#include "ps_object.h"
#include "ps_runtime.h"
#include "ps_string.h"

PS_TypeTag ps_typeof(PS_Value *v) {
  if (!v) return PS_T_VOID;
  switch (v->tag) {
    case PS_V_BOOL:
      return PS_T_BOOL;
    case PS_V_INT:
      return PS_T_INT;
    case PS_V_FLOAT:
      return PS_T_FLOAT;
    case PS_V_BYTE:
      return PS_T_BYTE;
    case PS_V_GLYPH:
      return PS_T_GLYPH;
    case PS_V_STRING:
      return PS_T_STRING;
    case PS_V_BYTES:
      return PS_T_BYTES;
    case PS_V_LIST:
      return PS_T_LIST;
    case PS_V_MAP:
      return PS_T_MAP;
    case PS_V_OBJECT:
      return PS_T_OBJECT;
    case PS_V_EXCEPTION:
      return PS_T_OBJECT;
    case PS_V_FILE:
      return PS_T_FILE;
    default:
      return PS_T_VOID;
  }
}

PS_Value *ps_make_bool(PS_Context *ctx, int value) {
  (void)ctx;
  PS_Value *v = ps_value_alloc(PS_V_BOOL);
  if (!v) return NULL;
  v->as.bool_v = value ? 1 : 0;
  return v;
}

PS_Value *ps_make_int(PS_Context *ctx, int64_t value) {
  (void)ctx;
  PS_Value *v = ps_value_alloc(PS_V_INT);
  if (!v) return NULL;
  v->as.int_v = value;
  return v;
}

PS_Value *ps_make_float(PS_Context *ctx, double value) {
  (void)ctx;
  PS_Value *v = ps_value_alloc(PS_V_FLOAT);
  if (!v) return NULL;
  v->as.float_v = value;
  return v;
}

PS_Value *ps_make_byte(PS_Context *ctx, uint8_t value) {
  (void)ctx;
  PS_Value *v = ps_value_alloc(PS_V_BYTE);
  if (!v) return NULL;
  v->as.byte_v = value;
  return v;
}

PS_Value *ps_make_glyph(PS_Context *ctx, uint32_t value) {
  (void)ctx;
  PS_Value *v = ps_value_alloc(PS_V_GLYPH);
  if (!v) return NULL;
  v->as.glyph_v = value;
  return v;
}

PS_Value *ps_make_string_utf8(PS_Context *ctx, const char *utf8, size_t len) {
  return ps_string_from_utf8(ctx, utf8, len);
}

PS_Value *ps_make_bytes(PS_Context *ctx, const uint8_t *bytes, size_t len) {
  PS_Value *v = ps_value_alloc(PS_V_BYTES);
  if (!v) {
    ps_throw_diag(ctx, PS_ERR_OOM, "out of memory", "byte buffer allocation failed", "available memory");
    return NULL;
  }
  v->as.bytes_v.ptr = (uint8_t *)malloc(len);
  if (!v->as.bytes_v.ptr && len > 0) {
    ps_value_release(v);
    ps_throw_diag(ctx, PS_ERR_OOM, "out of memory", "byte buffer allocation failed", "available memory");
    return NULL;
  }
  if (len > 0) memcpy(v->as.bytes_v.ptr, bytes, len);
  v->as.bytes_v.len = len;
  return v;
}

PS_Value *ps_make_list(PS_Context *ctx) { return ps_list_new(ctx); }

PS_Value *ps_make_map(PS_Context *ctx) { return ps_map_new(ctx); }

PS_Value *ps_make_object(PS_Context *ctx) { return ps_object_new(ctx); }

PS_Value *ps_make_file(PS_Context *ctx, FILE *fp, uint32_t flags, const char *path) {
  (void)ctx;
  if (!fp) return NULL;
  PS_Value *v = ps_value_alloc(PS_V_FILE);
  if (!v) return NULL;
  v->as.file_v.fp = fp;
  v->as.file_v.flags = flags;
  v->as.file_v.closed = 0;
  v->as.file_v.path = path ? strdup(path) : NULL;
  setvbuf(fp, NULL, _IONBF, 0);
  return v;
}

int ps_as_bool(PS_Value *v) { return v ? v->as.bool_v : 0; }
int64_t ps_as_int(PS_Value *v) { return v ? v->as.int_v : 0; }
double ps_as_float(PS_Value *v) { return v ? v->as.float_v : 0.0; }
uint8_t ps_as_byte(PS_Value *v) { return v ? v->as.byte_v : 0; }
uint32_t ps_as_glyph(PS_Value *v) { return v ? v->as.glyph_v : 0; }
const char *ps_string_ptr(PS_Value *v) { return v ? v->as.string_v.ptr : NULL; }
size_t ps_string_len(PS_Value *v) { return v ? v->as.string_v.len : 0; }
const uint8_t *ps_bytes_ptr(PS_Value *v) { return v ? v->as.bytes_v.ptr : NULL; }
size_t ps_bytes_len(PS_Value *v) { return v ? v->as.bytes_v.len : 0; }

size_t ps_list_len(PS_Value *list) { return ps_list_len_internal(list); }

PS_Value *ps_list_get(PS_Context *ctx, PS_Value *list, size_t index) {
  return ps_list_get_internal(ctx, list, index);
}

PS_Status ps_list_set(PS_Context *ctx, PS_Value *list, size_t index, PS_Value *value) {
  return ps_list_set_internal(ctx, list, index, value) ? PS_OK : PS_ERR;
}

PS_Status ps_list_push(PS_Context *ctx, PS_Value *list, PS_Value *value) {
  return ps_list_push_internal(ctx, list, value) ? PS_OK : PS_ERR;
}

PS_Value *ps_object_get_str(PS_Context *ctx, PS_Value *obj, const char *key_utf8, size_t key_len) {
  if (!ps_utf8_validate((const uint8_t *)key_utf8, key_len)) {
    ps_throw_diag(ctx, PS_ERR_UTF8, "invalid UTF-8", "object key", "valid UTF-8");
    return NULL;
  }
  return ps_object_get_str_internal(ctx, obj, key_utf8, key_len);
}

PS_Status ps_object_set_str(PS_Context *ctx, PS_Value *obj, const char *key_utf8, size_t key_len, PS_Value *value) {
  if (!ps_utf8_validate((const uint8_t *)key_utf8, key_len)) {
    ps_throw_diag(ctx, PS_ERR_UTF8, "invalid UTF-8", "object key", "valid UTF-8");
    return PS_ERR;
  }
  return ps_object_set_str_internal(ctx, obj, key_utf8, key_len, value) ? PS_OK : PS_ERR;
}

size_t ps_object_len(PS_Value *obj) {
  return ps_object_len_internal(obj);
}

PS_Status ps_object_entry(PS_Context *ctx, PS_Value *obj, size_t index, const char **out_key, size_t *out_len, PS_Value **out_value) {
  return ps_object_entry_internal(ctx, obj, index, out_key, out_len, out_value) ? PS_OK : PS_ERR;
}

PS_Value *ps_string_to_utf8_bytes(PS_Context *ctx, PS_Value *str) {
  if (!str || str->tag != PS_V_STRING) {
    const char *got = str ? (str->tag == PS_V_BYTES ? "bytes" : "non-string value") : "null";
    ps_throw_diag(ctx, PS_ERR_TYPE, "invalid string conversion", got, "string");
    return NULL;
  }
  return ps_make_bytes(ctx, (const uint8_t *)str->as.string_v.ptr, str->as.string_v.len);
}

PS_Value *ps_bytes_to_utf8_string(PS_Context *ctx, PS_Value *bytes) {
  if (!bytes || bytes->tag != PS_V_BYTES) {
    const char *got = bytes ? (bytes->tag == PS_V_STRING ? "string" : "non-bytes value") : "null";
    ps_throw_diag(ctx, PS_ERR_TYPE, "invalid bytes conversion", got, "bytes");
    return NULL;
  }
  if (!ps_utf8_validate(bytes->as.bytes_v.ptr, bytes->as.bytes_v.len)) {
    ps_throw_diag(ctx, PS_ERR_UTF8, "invalid UTF-8", "byte stream", "valid UTF-8");
    return NULL;
  }
  return ps_string_from_utf8(ctx, (const char *)bytes->as.bytes_v.ptr, bytes->as.bytes_v.len);
}

PS_Status ps_throw_exception(PS_Context *ctx, const char *type, const char *message) {
  if (!ctx) return PS_ERR;
  if (ctx->last_exception) {
    ps_value_release(ctx->last_exception);
    ctx->last_exception = NULL;
  }
  PS_Value *ex = ps_value_alloc(PS_V_EXCEPTION);
  if (!ex) {
    ps_throw_diag(ctx, PS_ERR_OOM, "out of memory", "exception allocation failed", "available memory");
    return PS_ERR;
  }
  ex->as.exc_v.is_runtime = 0;
  ex->as.exc_v.type_name = type ? strdup(type) : NULL;
  ex->as.exc_v.parent_name = strdup("Exception");
  ex->as.exc_v.fields = ps_object_new(ctx);
  ex->as.exc_v.file = ps_make_string_utf8(ctx, "", 0);
  ex->as.exc_v.line = 1;
  ex->as.exc_v.column = 1;
  ex->as.exc_v.message = ps_make_string_utf8(ctx, message ? message : "", message ? strlen(message) : 0);
  ex->as.exc_v.cause = NULL;
  ex->as.exc_v.code = NULL;
  ex->as.exc_v.category = NULL;
  if (!ex->as.exc_v.parent_name || !ex->as.exc_v.fields || !ex->as.exc_v.file || !ex->as.exc_v.message) {
    ps_value_release(ex);
    ps_throw_diag(ctx, PS_ERR_OOM, "out of memory", "exception allocation failed", "available memory");
    return PS_ERR;
  }
  ctx->last_exception = ex;
  ps_error_clear(ctx);
  return PS_ERR;
}
