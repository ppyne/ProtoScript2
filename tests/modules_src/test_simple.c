#include "ps/ps_api.h"

static PS_Status mod_add(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  int64_t a = ps_as_int(argv[0]);
  int64_t b = ps_as_int(argv[1]);
  *out = ps_make_int(ctx, a + b);
  return *out ? PS_OK : PS_ERR;
}

PS_Status ps_module_init(PS_Context *ctx, PS_Module *out) {
  (void)ctx;
  static PS_TypeTag params[] = {PS_T_INT, PS_T_INT};
  static PS_NativeFnDesc fns[] = {
      {.name = "add", .fn = mod_add, .arity = 2, .ret_type = PS_T_INT, .param_types = params, .flags = 0},
  };
  out->module_name = "test.simple";
  out->api_version = PS_API_VERSION;
  out->fn_count = 1;
  out->fns = fns;
  return PS_OK;
}
