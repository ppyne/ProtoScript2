#ifndef PS_ERRORS_H
#define PS_ERRORS_H

#include "ps/ps_api.h"

typedef struct {
  PS_ErrorCode code;
  char message[256];
} PS_Error;

void ps_error_set(PS_Context *ctx, PS_ErrorCode code, const char *msg);
void ps_error_clear(PS_Context *ctx);

#endif // PS_ERRORS_H
