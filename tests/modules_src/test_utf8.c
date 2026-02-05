#include "ps/ps_api.h"

static PS_Status mod_roundtrip(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  PS_Value *bytes = ps_string_to_utf8_bytes(ctx, argv[0]);
  if (!bytes) return PS_ERR;
  PS_Value *s = ps_bytes_to_utf8_string(ctx, bytes);
  ps_value_release(bytes);
  if (!s) return PS_ERR;
  *out = s;
  return PS_OK;
}

PS_Status ps_module_init(PS_Context *ctx, PS_Module *out) {
  (void)ctx;
  static PS_TypeTag params[] = {PS_T_STRING};
  static PS_NativeFnDesc fns[] = {
      {.name = "roundtrip", .fn = mod_roundtrip, .arity = 1, .ret_type = PS_T_STRING, .param_types = params, .flags = 0},
  };
  out->module_name = "test.utf8";
  out->api_version = PS_API_VERSION;
  out->fn_count = 1;
  out->fns = fns;
  return PS_OK;
}
