#ifndef PS_RUNTIME_H
#define PS_RUNTIME_H

#include "ps/ps_api.h"
#include "ps_errors.h"
#include "ps_value_impl.h"

typedef struct {
  PS_Value **items;
  size_t len;
  size_t cap;
} PS_HandleStack;

struct PS_Context {
  PS_HandleStack handles;
  PS_Error last_error;
  int trace;
  int trace_ir;
  struct PS_ModuleRecord *modules;
  size_t module_count;
  size_t module_cap;
};

PS_Value *ps_value_alloc(PS_ValueTag tag);
void ps_value_free(PS_Value *v);

#endif // PS_RUNTIME_H
