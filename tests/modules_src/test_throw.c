#include "ps/ps_api.h"

static PS_Status mod_fail(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  (void)argv;
  (void)out;
  ps_throw(ctx, PS_ERR_INTERNAL, "native failure");
  return PS_ERR;
}

PS_Status ps_module_init(PS_Context *ctx, PS_Module *out) {
  (void)ctx;
  static PS_NativeFnDesc fns[] = {
      {.name = "fail", .fn = mod_fail, .arity = 0, .ret_type = PS_T_VOID, .param_types = NULL, .flags = 0},
  };
  out->module_name = "test.throw";
  out->api_version = PS_API_VERSION;
  out->fn_count = 1;
  out->fns = fns;
  return PS_OK;
}
