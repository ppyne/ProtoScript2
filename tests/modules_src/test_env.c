#include <stdlib.h>
#include <string.h>

#include "ps/ps_api.h"

static PS_Status env_throw(PS_Context *ctx, const char *msg) {
  ps_throw(ctx, PS_ERR_INTERNAL, msg ? msg : "env error");
  return PS_ERR;
}

static char *env_dup_string(PS_Context *ctx, PS_Value *v) {
  if (!v || ps_typeof(v) != PS_T_STRING) return NULL;
  const char *s = ps_string_ptr(v);
  size_t len = ps_string_len(v);
  char *out = (char *)malloc(len + 1);
  if (!out) {
    ps_throw(ctx, PS_ERR_OOM, "out of memory");
    return NULL;
  }
  if (len > 0) memcpy(out, s, len);
  out[len] = '\0';
  return out;
}

static PS_Status env_set(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  (void)out;
  if (!argv) return PS_ERR;
  char *name = env_dup_string(ctx, argv[0]);
  if (!name) return env_throw(ctx, "invalid name");
  char *value = env_dup_string(ctx, argv[1]);
  if (!value) {
    free(name);
    return env_throw(ctx, "invalid value");
  }
  if (setenv(name, value, 1) != 0) {
    free(name);
    free(value);
    return env_throw(ctx, "setenv failed");
  }
  free(name);
  free(value);
  return PS_OK;
}

static PS_Status env_set_invalid_utf8(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  (void)out;
  if (!argv) return PS_ERR;
  char *name = env_dup_string(ctx, argv[0]);
  if (!name) return env_throw(ctx, "invalid name");
  char bad[] = {(char)0xC3, (char)0x28, '\0'};
  if (setenv(name, bad, 1) != 0) {
    free(name);
    return env_throw(ctx, "setenv failed");
  }
  free(name);
  return PS_OK;
}

PS_Status ps_module_init(PS_Context *ctx, PS_Module *out) {
  (void)ctx;
  static PS_TypeTag params_set[] = {PS_T_STRING, PS_T_STRING};
  static PS_TypeTag params_name[] = {PS_T_STRING};
  static PS_NativeFnDesc fns[] = {
      {.name = "set", .fn = env_set, .arity = 2, .ret_type = PS_T_VOID, .param_types = params_set, .flags = 0},
      {.name = "setInvalidUtf8", .fn = env_set_invalid_utf8, .arity = 1, .ret_type = PS_T_VOID, .param_types = params_name, .flags = 0},
  };
  out->module_name = "test.env";
  out->api_version = PS_API_VERSION;
  out->fn_count = sizeof(fns) / sizeof(fns[0]);
  out->fns = fns;
  return PS_OK;
}
