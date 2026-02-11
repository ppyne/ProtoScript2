#ifndef PS_ERRORS_H
#define PS_ERRORS_H

#include "ps/ps_api.h"

typedef struct {
  PS_ErrorCode code;
  char message[256];
} PS_Error;

void ps_error_set(PS_Context *ctx, PS_ErrorCode code, const char *msg);
void ps_error_clear(PS_Context *ctx);

// Build a normative diagnostic message with got/expected details.
// Format: "<short>. got <got>; expected <expected>"
void ps_format_diag(char *out, size_t out_sz, const char *short_msg, const char *got, const char *expected);
void ps_throw_diag(PS_Context *ctx, PS_ErrorCode code, const char *short_msg, const char *got, const char *expected);

// Runtime diagnostic mapping (best-effort).
// Returns category string or NULL if no mapping exists.
// If non-NULL, out_code is set to the canonical Rxxxx code.
const char *ps_runtime_category(PS_ErrorCode code, const char *msg, const char **out_code);

#endif // PS_ERRORS_H
