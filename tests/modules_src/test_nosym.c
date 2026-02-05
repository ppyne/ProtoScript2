#include "ps/ps_api.h"

static PS_Status mod_other(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  (void)argv;
  *out = ps_make_int(ctx, 3);
  return *out ? PS_OK : PS_ERR;
}

PS_Status ps_module_init(PS_Context *ctx, PS_Module *out) {
  (void)ctx;
  static PS_NativeFnDesc fns[] = {
      {.name = "other", .fn = mod_other, .arity = 0, .ret_type = PS_T_INT, .param_types = NULL, .flags = 0},
  };
  out->module_name = "test.nosym";
  out->api_version = PS_API_VERSION;
  out->fn_count = 1;
  out->fns = fns;
  return PS_OK;
}
