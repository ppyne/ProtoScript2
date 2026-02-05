#include "ps/ps_api.h"

// Intentionally missing ps_module_init symbol.

static PS_Status mod_ping(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  (void)argv;
  *out = ps_make_int(ctx, 1);
  return *out ? PS_OK : PS_ERR;
}

PS_Status ps_module_not_init(PS_Context *ctx, PS_Module *out) {
  (void)ctx;
  static PS_NativeFnDesc fns[] = {
      {.name = "ping", .fn = mod_ping, .arity = 0, .ret_type = PS_T_INT, .param_types = NULL, .flags = 0},
  };
  out->module_name = "test.noinit";
  out->api_version = PS_API_VERSION;
  out->fn_count = 1;
  out->fns = fns;
  return PS_OK;
}
