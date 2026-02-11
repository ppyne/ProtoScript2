#include <stdint.h>
#include <time.h>
#include <errno.h>

#include "ps/ps_api.h"

static PS_Status mod_now_epoch_millis(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc; (void)argv;
  struct timespec ts;
  if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
    ps_throw(ctx, PS_ERR_INTERNAL, "clock_gettime failed");
    return PS_ERR;
  }
  int64_t ms = (int64_t)ts.tv_sec * 1000 + (int64_t)(ts.tv_nsec / 1000000);
  PS_Value *v = ps_make_int(ctx, ms);
  if (!v) return PS_ERR;
  *out = v;
  return PS_OK;
}

static PS_Status mod_now_monotonic_nanos(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc; (void)argv;
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
    ps_throw(ctx, PS_ERR_INTERNAL, "clock_gettime failed");
    return PS_ERR;
  }
  int64_t ns = (int64_t)ts.tv_sec * 1000000000 + (int64_t)ts.tv_nsec;
  PS_Value *v = ps_make_int(ctx, ns);
  if (!v) return PS_ERR;
  *out = v;
  return PS_OK;
}

static PS_Status mod_sleep_millis(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)out;
  if (argc < 1) {
    ps_throw(ctx, PS_ERR_TYPE, "sleepMillis expects int");
    return PS_ERR;
  }
  if (!argv[0] || ps_typeof(argv[0]) != PS_T_INT) {
    ps_throw(ctx, PS_ERR_TYPE, "sleepMillis expects int");
    return PS_ERR;
  }
  int64_t ms = ps_as_int(argv[0]);
  if (ms <= 0) return PS_OK;
  struct timespec ts;
  ts.tv_sec = (time_t)(ms / 1000);
  ts.tv_nsec = (long)((ms % 1000) * 1000000);
  while (nanosleep(&ts, &ts) != 0 && errno == EINTR) {
  }
  return PS_OK;
}

PS_Status ps_module_init(PS_Context *ctx, PS_Module *out) {
  (void)ctx;
  static PS_NativeFnDesc fns[] = {
    {.name = "nowEpochMillis", .fn = mod_now_epoch_millis, .arity = 0, .ret_type = PS_T_INT, .param_types = NULL, .flags = 0},
    {.name = "nowMonotonicNanos", .fn = mod_now_monotonic_nanos, .arity = 0, .ret_type = PS_T_INT, .param_types = NULL, .flags = 0},
    {.name = "sleepMillis", .fn = mod_sleep_millis, .arity = 1, .ret_type = PS_T_VOID, .param_types = NULL, .flags = 0},
  };
  out->module_name = "Time";
  out->api_version = PS_API_VERSION;
  out->fn_count = sizeof(fns) / sizeof(fns[0]);
  out->fns = fns;
  return PS_OK;
}
