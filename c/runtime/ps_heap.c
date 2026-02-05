#include <stdlib.h>

#include "ps_runtime.h"

PS_Context *ps_ctx_create(void) {
  PS_Context *ctx = (PS_Context *)calloc(1, sizeof(PS_Context));
  if (!ctx) return NULL;
  ctx->handles.items = NULL;
  ctx->handles.len = 0;
  ctx->handles.cap = 0;
  ctx->last_error.code = PS_ERR_NONE;
  ctx->last_error.message[0] = '\0';
  ctx->trace = 0;
  ctx->trace_ir = 0;
  return ctx;
}

void ps_ctx_destroy(PS_Context *ctx) {
  if (!ctx) return;
  while (ctx->handles.len > 0) {
    ps_handle_pop(ctx);
  }
  free(ctx->handles.items);
  free(ctx);
}

void ps_handle_push(PS_Context *ctx, PS_Value *v) {
  if (!ctx || !v) return;
  if (ctx->handles.len == ctx->handles.cap) {
    size_t new_cap = (ctx->handles.cap == 0) ? 16 : (ctx->handles.cap * 2);
    PS_Value **n = (PS_Value **)realloc(ctx->handles.items, sizeof(PS_Value *) * new_cap);
    if (!n) return;
    ctx->handles.items = n;
    ctx->handles.cap = new_cap;
  }
  ctx->handles.items[ctx->handles.len++] = ps_value_retain(v);
}

void ps_handle_pop(PS_Context *ctx) {
  if (!ctx || ctx->handles.len == 0) return;
  PS_Value *v = ctx->handles.items[ctx->handles.len - 1];
  ctx->handles.len -= 1;
  ps_value_release(v);
}

PS_ErrorCode ps_last_error_code(PS_Context *ctx) {
  if (!ctx) return PS_ERR_INTERNAL;
  return ctx->last_error.code;
}

const char *ps_last_error_message(PS_Context *ctx) {
  if (!ctx) return "internal error";
  return ctx->last_error.message;
}

void ps_clear_error(PS_Context *ctx) { ps_error_clear(ctx); }

void ps_throw(PS_Context *ctx, PS_ErrorCode code, const char *message) {
  ps_error_set(ctx, code, message);
}
