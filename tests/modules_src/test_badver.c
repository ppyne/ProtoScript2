#include "ps/ps_api.h"

static PS_Status mod_ping(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  (void)argv;
  *out = ps_make_int(ctx, 2);
  return *out ? PS_OK : PS_ERR;
}

PS_Status ps_module_init(PS_Context *ctx, PS_Module *out) {
  (void)ctx;
  static PS_NativeFnDesc fns[] = {
      {.name = "ping", .fn = mod_ping, .arity = 0, .ret_type = PS_T_INT, .param_types = NULL, .flags = 0},
  };
  out->module_name = "test.badver";
  out->api_version = PS_API_VERSION + 1;
  out->fn_count = 1;
  out->fns = fns;
  return PS_OK;
}
