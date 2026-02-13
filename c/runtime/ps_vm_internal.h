#ifndef PS_VM_INTERNAL_H
#define PS_VM_INTERNAL_H

#include <stddef.h>

#include "ps_vm.h"

typedef struct {
  char *name;
  char *type;
} PS_IR_Field;

typedef struct {
  char *name;
  char *type;
  int variadic;
} PS_IR_Param;

typedef struct {
  char *name;
  PS_IR_Param *params;
  size_t param_count;
  char *ret_type;
} PS_IR_Method;

typedef struct PS_IR_GroupMember {
  char *name;
  char *literal_type;
  char *value;
} PS_IR_GroupMember;

typedef struct PS_IR_Group {
  char *name;
  char *base_type;
  PS_IR_GroupMember *members;
  size_t member_count;
} PS_IR_Group;

typedef struct {
  char *name;
  char *parent;
  PS_IR_Field *fields;
  size_t field_count;
  PS_IR_Method *methods;
  size_t method_count;
  int is_sealed;
} PS_IR_Proto;

const PS_IR_Proto *ps_ir_find_proto(const PS_IR_Module *m, const char *name);
const PS_IR_Group *ps_ir_find_group(const PS_IR_Module *m, const char *name);
size_t ps_ir_group_count(const PS_IR_Module *m);
const PS_IR_Group *ps_ir_group_at(const PS_IR_Module *m, size_t idx);
size_t ps_ir_group_member_total(const PS_IR_Module *m);
const void *ps_ir_group_table_ptr(const PS_IR_Module *m);
const void *ps_ir_group_member_table_ptr(const PS_IR_Module *m, size_t idx);

#endif // PS_VM_INTERNAL_H
