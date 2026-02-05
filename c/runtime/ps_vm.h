#ifndef PS_VM_H
#define PS_VM_H

#include "ps_runtime.h"

typedef struct PS_IR_Module PS_IR_Module;

PS_IR_Module *ps_ir_load_json(PS_Context *ctx, const char *json, size_t len);
void ps_ir_free(PS_IR_Module *m);

// Execute module entry function "main". Returns 0 on success, non-zero on runtime error.
int ps_vm_run_main(PS_Context *ctx, PS_IR_Module *m);

#endif // PS_VM_H
