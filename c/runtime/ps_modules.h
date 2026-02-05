#ifndef PS_MODULES_H
#define PS_MODULES_H

#include "ps/ps_api.h"
#include "ps_dynlib.h"

typedef struct PS_ModuleRecord {
  PS_Module desc;
  PS_DynLib *lib;
} PS_ModuleRecord;

PS_Status ps_module_load(PS_Context *ctx, const char *module_name);
const PS_NativeFnDesc *ps_module_find_fn(PS_Context *ctx, const char *module_name, const char *fn_name);

#endif // PS_MODULES_H
