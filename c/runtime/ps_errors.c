#include <string.h>

#include "ps_errors.h"
#include "ps_runtime.h"

void ps_error_set(PS_Context *ctx, PS_ErrorCode code, const char *msg) {
  if (!ctx) return;
  ctx->last_error.code = code;
  if (msg) {
    strncpy(ctx->last_error.message, msg, sizeof(ctx->last_error.message) - 1);
    ctx->last_error.message[sizeof(ctx->last_error.message) - 1] = '\0';
  } else {
    ctx->last_error.message[0] = '\0';
  }
}

void ps_error_clear(PS_Context *ctx) {
  if (!ctx) return;
  ctx->last_error.code = PS_ERR_NONE;
  ctx->last_error.message[0] = '\0';
}
