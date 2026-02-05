#include <math.h>
#include "ps/ps_api.h"

static int get_float_arg(PS_Context *ctx, PS_Value *v, double *out) {
  if (!v || !out) return 0;
  PS_TypeTag t = ps_typeof(v);
  if (t == PS_T_FLOAT) {
    *out = ps_as_float(v);
    return 1;
  }
  if (t == PS_T_INT) {
    *out = (double)ps_as_int(v);
    return 1;
  }
  ps_throw(ctx, PS_ERR_TYPE, "expected float");
  return 0;
}

static PS_Status ret_float(PS_Context *ctx, double v, PS_Value **out) {
  PS_Value *r = ps_make_float(ctx, v);
  if (!r) return PS_ERR;
  *out = r;
  return PS_OK;
}

static PS_Status mod_abs(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (!get_float_arg(ctx, argv[0], &x)) return PS_ERR;
  return ret_float(ctx, fabs(x), out);
}

static PS_Status mod_min(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double a = 0.0, b = 0.0;
  if (!get_float_arg(ctx, argv[0], &a)) return PS_ERR;
  if (!get_float_arg(ctx, argv[1], &b)) return PS_ERR;
  return ret_float(ctx, fmin(a, b), out);
}

static PS_Status mod_max(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double a = 0.0, b = 0.0;
  if (!get_float_arg(ctx, argv[0], &a)) return PS_ERR;
  if (!get_float_arg(ctx, argv[1], &b)) return PS_ERR;
  return ret_float(ctx, fmax(a, b), out);
}

static PS_Status mod_floor(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (!get_float_arg(ctx, argv[0], &x)) return PS_ERR;
  return ret_float(ctx, floor(x), out);
}

static PS_Status mod_ceil(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (!get_float_arg(ctx, argv[0], &x)) return PS_ERR;
  return ret_float(ctx, ceil(x), out);
}

static PS_Status mod_round(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (!get_float_arg(ctx, argv[0], &x)) return PS_ERR;
  return ret_float(ctx, round(x), out);
}

static PS_Status mod_sqrt(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (!get_float_arg(ctx, argv[0], &x)) return PS_ERR;
  if (x < 0.0) {
    ps_throw(ctx, PS_ERR_RANGE, "sqrt domain error");
    return PS_ERR;
  }
  return ret_float(ctx, sqrt(x), out);
}

static PS_Status mod_pow(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double a = 0.0, b = 0.0;
  if (!get_float_arg(ctx, argv[0], &a)) return PS_ERR;
  if (!get_float_arg(ctx, argv[1], &b)) return PS_ERR;
  double r = pow(a, b);
  if (isnan(r)) {
    ps_throw(ctx, PS_ERR_RANGE, "pow domain error");
    return PS_ERR;
  }
  return ret_float(ctx, r, out);
}

static PS_Status mod_sin(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (!get_float_arg(ctx, argv[0], &x)) return PS_ERR;
  return ret_float(ctx, sin(x), out);
}

static PS_Status mod_cos(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (!get_float_arg(ctx, argv[0], &x)) return PS_ERR;
  return ret_float(ctx, cos(x), out);
}

static PS_Status mod_tan(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (!get_float_arg(ctx, argv[0], &x)) return PS_ERR;
  return ret_float(ctx, tan(x), out);
}

static PS_Status mod_log(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (!get_float_arg(ctx, argv[0], &x)) return PS_ERR;
  if (x <= 0.0) {
    ps_throw(ctx, PS_ERR_RANGE, "log domain error");
    return PS_ERR;
  }
  return ret_float(ctx, log(x), out);
}

static PS_Status mod_exp(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (!get_float_arg(ctx, argv[0], &x)) return PS_ERR;
  return ret_float(ctx, exp(x), out);
}

PS_Status ps_module_init(PS_Context *ctx, PS_Module *out) {
  (void)ctx;
  static PS_NativeFnDesc fns[] = {
      {.name = "abs", .fn = mod_abs, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "min", .fn = mod_min, .arity = 2, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "max", .fn = mod_max, .arity = 2, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "floor", .fn = mod_floor, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "ceil", .fn = mod_ceil, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "round", .fn = mod_round, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "sqrt", .fn = mod_sqrt, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "pow", .fn = mod_pow, .arity = 2, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "sin", .fn = mod_sin, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "cos", .fn = mod_cos, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "tan", .fn = mod_tan, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "log", .fn = mod_log, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "exp", .fn = mod_exp, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
  };
  out->module_name = "Math";
  out->api_version = PS_API_VERSION;
  out->fn_count = sizeof(fns) / sizeof(fns[0]);
  out->fns = fns;
  return PS_OK;
}
