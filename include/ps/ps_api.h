// ProtoScript V2 — Public C API for native modules (ABI stable).
// This header is standalone and must not expose internal runtime structures.
#ifndef PS_API_H
#define PS_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ABI version negotiated at module load time.
#define PS_API_VERSION 1

typedef struct PS_Value PS_Value;     // opaque handle to a runtime value
typedef struct PS_Context PS_Context; // opaque runtime context

typedef enum {
  PS_T_BOOL,
  PS_T_INT,
  PS_T_FLOAT,
  PS_T_BYTE,
  PS_T_GLYPH,
  PS_T_STRING,
  PS_T_BYTES,
  PS_T_LIST,
  PS_T_OBJECT,
  PS_T_VOID
} PS_TypeTag;

typedef enum {
  PS_ERR_NONE = 0,
  PS_ERR_TYPE,
  PS_ERR_RANGE,
  PS_ERR_UTF8,
  PS_ERR_IMPORT,
  PS_ERR_OOM,
  PS_ERR_INTERNAL
} PS_ErrorCode;

typedef enum {
  PS_OK = 0,
  PS_ERR = 1
} PS_Status;

typedef PS_Status (*PS_NativeFn)(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out);

typedef struct {
  const char *name;
  PS_NativeFn fn;
  int arity;          // -1 for variadic (if/when supported); otherwise exact
  PS_TypeTag ret_type;
  const PS_TypeTag *param_types; // optional, can be NULL
  uint32_t flags;     // reserved for future (e.g., purity)
} PS_NativeFnDesc;

typedef struct {
  const char *module_name; // e.g., "std.io"
  int api_version;         // must be PS_API_VERSION
  size_t fn_count;
  const PS_NativeFnDesc *fns;
} PS_Module;

// Module entry point symbol (required).
// Return PS_OK on success, PS_ERR on failure (ctx carries error).
PS_Status ps_module_init(PS_Context *ctx, PS_Module *out);

// Context lifecycle.
PS_Context *ps_ctx_create(void);
void ps_ctx_destroy(PS_Context *ctx);

// Error handling (native modules).
PS_ErrorCode ps_last_error_code(PS_Context *ctx);
const char *ps_last_error_message(PS_Context *ctx);
void ps_clear_error(PS_Context *ctx);
void ps_throw(PS_Context *ctx, PS_ErrorCode code, const char *message);

// Handle ownership:
// - All constructors return a new handle owned by the caller (refcount +1).
// - ps_value_retain adds a reference; ps_value_release removes one.
// - ps_handle_push adds a root (refcount +1); ps_handle_pop removes it (refcount -1).
PS_Value *ps_value_retain(PS_Value *v);
void ps_value_release(PS_Value *v);
void ps_handle_push(PS_Context *ctx, PS_Value *v);
void ps_handle_pop(PS_Context *ctx);

// Type inspection.
PS_TypeTag ps_typeof(PS_Value *v);

// Constructors (owned by caller).
PS_Value *ps_make_bool(PS_Context *ctx, int value);
PS_Value *ps_make_int(PS_Context *ctx, int64_t value);
PS_Value *ps_make_float(PS_Context *ctx, double value);
PS_Value *ps_make_byte(PS_Context *ctx, uint8_t value);
PS_Value *ps_make_glyph(PS_Context *ctx, uint32_t value);
PS_Value *ps_make_string_utf8(PS_Context *ctx, const char *utf8, size_t len);
PS_Value *ps_make_bytes(PS_Context *ctx, const uint8_t *bytes, size_t len);
PS_Value *ps_make_list(PS_Context *ctx);
PS_Value *ps_make_object(PS_Context *ctx);

// Accessors (do not transfer ownership).
int ps_as_bool(PS_Value *v);
int64_t ps_as_int(PS_Value *v);
double ps_as_float(PS_Value *v);
uint8_t ps_as_byte(PS_Value *v);
uint32_t ps_as_glyph(PS_Value *v);
const char *ps_string_ptr(PS_Value *v);
size_t ps_string_len(PS_Value *v);
const uint8_t *ps_bytes_ptr(PS_Value *v);
size_t ps_bytes_len(PS_Value *v);

// Collection helpers.
size_t ps_list_len(PS_Value *list);
PS_Value *ps_list_get(PS_Context *ctx, PS_Value *list, size_t index);
PS_Status ps_list_set(PS_Context *ctx, PS_Value *list, size_t index, PS_Value *value);
PS_Status ps_list_push(PS_Context *ctx, PS_Value *list, PS_Value *value);

PS_Value *ps_object_get_str(PS_Context *ctx, PS_Value *obj, const char *key_utf8, size_t key_len);
PS_Status ps_object_set_str(PS_Context *ctx, PS_Value *obj, const char *key_utf8, size_t key_len, PS_Value *value);

// UTF-8 conversions (strict).
PS_Value *ps_string_to_utf8_bytes(PS_Context *ctx, PS_Value *str);
PS_Value *ps_bytes_to_utf8_string(PS_Context *ctx, PS_Value *bytes);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // PS_API_H
