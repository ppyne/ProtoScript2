#include <math.h>
#include <stdint.h>
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
  ps_throw(ctx, PS_ERR_TYPE, "invalid argument. got string; expected float");
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

static PS_Status mod_trunc(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (!get_float_arg(ctx, argv[0], &x)) return PS_ERR;
  return ret_float(ctx, trunc(x), out);
}

static PS_Status mod_sign(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (!get_float_arg(ctx, argv[0], &x)) return PS_ERR;
  if (isnan(x)) return ret_float(ctx, NAN, out);
  if (x == 0.0) return ret_float(ctx, copysign(0.0, x), out);
  return ret_float(ctx, x > 0.0 ? 1.0 : -1.0, out);
}

static PS_Status mod_fround(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (!get_float_arg(ctx, argv[0], &x)) return PS_ERR;
  float f = (float)x;
  return ret_float(ctx, (double)f, out);
}

static PS_Status mod_sqrt(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (!get_float_arg(ctx, argv[0], &x)) return PS_ERR;
  return ret_float(ctx, sqrt(x), out);
}

static PS_Status mod_cbrt(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (!get_float_arg(ctx, argv[0], &x)) return PS_ERR;
  return ret_float(ctx, cbrt(x), out);
}

static PS_Status mod_pow(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double a = 0.0, b = 0.0;
  if (!get_float_arg(ctx, argv[0], &a)) return PS_ERR;
  if (!get_float_arg(ctx, argv[1], &b)) return PS_ERR;
  return ret_float(ctx, pow(a, b), out);
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

static PS_Status mod_asin(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (!get_float_arg(ctx, argv[0], &x)) return PS_ERR;
  return ret_float(ctx, asin(x), out);
}

static PS_Status mod_acos(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (!get_float_arg(ctx, argv[0], &x)) return PS_ERR;
  return ret_float(ctx, acos(x), out);
}

static PS_Status mod_atan(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (!get_float_arg(ctx, argv[0], &x)) return PS_ERR;
  return ret_float(ctx, atan(x), out);
}

static PS_Status mod_atan2(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double y = 0.0, x = 0.0;
  if (!get_float_arg(ctx, argv[0], &y)) return PS_ERR;
  if (!get_float_arg(ctx, argv[1], &x)) return PS_ERR;
  return ret_float(ctx, atan2(y, x), out);
}

static PS_Status mod_sinh(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (!get_float_arg(ctx, argv[0], &x)) return PS_ERR;
  return ret_float(ctx, sinh(x), out);
}

static PS_Status mod_cosh(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (!get_float_arg(ctx, argv[0], &x)) return PS_ERR;
  return ret_float(ctx, cosh(x), out);
}

static PS_Status mod_tanh(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (!get_float_arg(ctx, argv[0], &x)) return PS_ERR;
  return ret_float(ctx, tanh(x), out);
}

static PS_Status mod_asinh(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (!get_float_arg(ctx, argv[0], &x)) return PS_ERR;
  return ret_float(ctx, asinh(x), out);
}

static PS_Status mod_acosh(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (!get_float_arg(ctx, argv[0], &x)) return PS_ERR;
  return ret_float(ctx, acosh(x), out);
}

static PS_Status mod_atanh(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (!get_float_arg(ctx, argv[0], &x)) return PS_ERR;
  return ret_float(ctx, atanh(x), out);
}

static PS_Status mod_log(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (!get_float_arg(ctx, argv[0], &x)) return PS_ERR;
  return ret_float(ctx, log(x), out);
}

static PS_Status mod_log1p(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (!get_float_arg(ctx, argv[0], &x)) return PS_ERR;
  return ret_float(ctx, log1p(x), out);
}

static PS_Status mod_log2(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (!get_float_arg(ctx, argv[0], &x)) return PS_ERR;
  return ret_float(ctx, log2(x), out);
}

static PS_Status mod_log10(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (!get_float_arg(ctx, argv[0], &x)) return PS_ERR;
  return ret_float(ctx, log10(x), out);
}

static PS_Status mod_exp(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (!get_float_arg(ctx, argv[0], &x)) return PS_ERR;
  return ret_float(ctx, exp(x), out);
}

static PS_Status mod_expm1(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (!get_float_arg(ctx, argv[0], &x)) return PS_ERR;
  return ret_float(ctx, expm1(x), out);
}

static PS_Status mod_hypot(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double a = 0.0, b = 0.0;
  if (!get_float_arg(ctx, argv[0], &a)) return PS_ERR;
  if (!get_float_arg(ctx, argv[1], &b)) return PS_ERR;
  return ret_float(ctx, hypot(a, b), out);
}

static uint32_t to_uint32(double x) {
  if (!isfinite(x) || x == 0.0) return 0;
  double t = floor(x);
  t = fmod(t, 4294967296.0);
  if (t < 0) t += 4294967296.0;
  return (uint32_t)t;
}

static int32_t to_int32(double x) {
  return (int32_t)to_uint32(x);
}

static PS_Status mod_clz32(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (!get_float_arg(ctx, argv[0], &x)) return PS_ERR;
  uint32_t v = to_uint32(x);
  int c = 32;
  if (v != 0) {
#if defined(__GNUC__) || defined(__clang__)
    c = __builtin_clz(v);
#else
    c = 0;
    for (uint32_t m = 0x80000000u; (v & m) == 0; m >>= 1) c++;
#endif
  }
  return ret_float(ctx, (double)c, out);
}

static PS_Status mod_imul(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double a = 0.0, b = 0.0;
  if (!get_float_arg(ctx, argv[0], &a)) return PS_ERR;
  if (!get_float_arg(ctx, argv[1], &b)) return PS_ERR;
  int32_t ai = to_int32(a);
  int32_t bi = to_int32(b);
  int32_t r = (int32_t)((uint32_t)ai * (uint32_t)bi);
  return ret_float(ctx, (double)r, out);
}

static uint32_t math_rng_state = 0;
static int math_rng_seeded = 0;

static void math_rng_seed(void) {
  uintptr_t p = (uintptr_t)&math_rng_state;
  uint32_t s = (uint32_t)(p ^ (p >> 16) ^ 0xA5A5A5A5u);
  if (s == 0) s = 0x6d2b79f5u;
  math_rng_state = s;
  math_rng_seeded = 1;
}

static uint32_t math_rng_next(void) {
  if (!math_rng_seeded) math_rng_seed();
  uint32_t x = math_rng_state;
  x ^= x << 13;
  x ^= x >> 17;
  x ^= x << 5;
  math_rng_state = x;
  return x;
}

static PS_Status mod_random(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  (void)argv;
  uint32_t r = math_rng_next();
  double v = (double)r / 4294967296.0;
  return ret_float(ctx, v, out);
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
      {.name = "trunc", .fn = mod_trunc, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "sign", .fn = mod_sign, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "fround", .fn = mod_fround, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "sqrt", .fn = mod_sqrt, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "cbrt", .fn = mod_cbrt, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "pow", .fn = mod_pow, .arity = 2, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "sin", .fn = mod_sin, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "cos", .fn = mod_cos, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "tan", .fn = mod_tan, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "asin", .fn = mod_asin, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "acos", .fn = mod_acos, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "atan", .fn = mod_atan, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "atan2", .fn = mod_atan2, .arity = 2, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "sinh", .fn = mod_sinh, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "cosh", .fn = mod_cosh, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "tanh", .fn = mod_tanh, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "asinh", .fn = mod_asinh, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "acosh", .fn = mod_acosh, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "atanh", .fn = mod_atanh, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "log", .fn = mod_log, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "log1p", .fn = mod_log1p, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "log2", .fn = mod_log2, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "log10", .fn = mod_log10, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "exp", .fn = mod_exp, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "expm1", .fn = mod_expm1, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "hypot", .fn = mod_hypot, .arity = 2, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "clz32", .fn = mod_clz32, .arity = 1, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "imul", .fn = mod_imul, .arity = 2, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
      {.name = "random", .fn = mod_random, .arity = 0, .ret_type = PS_T_FLOAT, .param_types = NULL, .flags = 0},
  };
  out->module_name = "Math";
  out->api_version = PS_API_VERSION;
  out->fn_count = sizeof(fns) / sizeof(fns[0]);
  out->fns = fns;
  return PS_OK;
}
