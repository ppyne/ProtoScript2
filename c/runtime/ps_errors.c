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

static int msg_has(const char *msg, const char *needle) {
  if (!msg || !needle) return 0;
  return strstr(msg, needle) != NULL;
}

const char *ps_runtime_category(PS_ErrorCode code, const char *msg, const char **out_code) {
  if (!out_code) return NULL;
  *out_code = NULL;
  if (code == PS_ERR_RANGE) {
    if (msg_has(msg, "int overflow")) {
      *out_code = "R1001";
      return "RUNTIME_INT_OVERFLOW";
    }
    if (msg_has(msg, "index out of bounds") || msg_has(msg, "string index out of bounds")) {
      *out_code = "R1002";
      return "RUNTIME_INDEX_OOB";
    }
    if (msg_has(msg, "missing key")) {
      *out_code = "R1003";
      return "RUNTIME_MISSING_KEY";
    }
    if (msg_has(msg, "division by zero")) {
      *out_code = "R1004";
      return "RUNTIME_DIVIDE_BY_ZERO";
    }
    if (msg_has(msg, "invalid shift")) {
      *out_code = "R1005";
      return "RUNTIME_SHIFT_RANGE";
    }
    if (msg_has(msg, "pop on empty list")) {
      *out_code = "R1006";
      return "RUNTIME_EMPTY_POP";
    }
    if (msg_has(msg, "byte out of range")) {
      *out_code = "R1008";
      return "RUNTIME_BYTE_RANGE";
    }
  }
  if (code == PS_ERR_UTF8) {
    *out_code = "R1007";
    return "RUNTIME_INVALID_UTF8";
  }
  return NULL;
}
