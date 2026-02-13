#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "frontend.h"
#include "runtime/ps_runtime.h"
#include "runtime/ps_vm.h"
#include "runtime/ps_vm_internal.h"

static PS_IR_Module *load_ir_from_file(PS_Context *ctx, const char *file) {
  PsDiag d;
  char *buf = NULL;
  size_t len = 0;
  FILE *mem = open_memstream(&buf, &len);
  if (!mem) {
    fprintf(stderr, "open_memstream failed\n");
    return NULL;
  }
  int rc = ps_emit_ir_json(file, &d, mem);
  fclose(mem);
  if (rc != 0) {
    fprintf(stderr, "%s:%d:%d %s %s: %s\n",
            d.file ? d.file : file,
            d.line ? d.line : 1,
            d.col ? d.col : 1,
            d.code ? d.code : "E?",
            d.category ? d.category : "ERROR",
            d.message[0] ? d.message : "failed to emit IR");
    free(buf);
    return NULL;
  }
  PS_IR_Module *m = ps_ir_load_json(ctx, buf, len);
  free(buf);
  return m;
}

int main(int argc, char **argv) {
  const char *file = argc > 1 ? argv[1] : "stress.pts";
  PS_Context *ctx = ps_ctx_create();
  if (!ctx) {
    fprintf(stderr, "ctx create failed\n");
    return 1;
  }

  PS_IR_Module *m = load_ir_from_file(ctx, file);
  if (!m) {
    ps_ctx_destroy(ctx);
    return 1;
  }

  size_t group_count = ps_ir_group_count(m);
  size_t member_total = ps_ir_group_member_total(m);
  const void *group_ptr = ps_ir_group_table_ptr(m);
  const void *member_ptr0 = ps_ir_group_member_table_ptr(m, 0);

  printf("groups=%zu members=%zu\n", group_count, member_total);
  printf("group_table=%p\n", group_ptr);
  if (group_count > 0) {
    printf("member_table[0]=%p\n", member_ptr0);
  }

  for (int i = 0; i < 100; i++) {
    PS_Value *ret = NULL;
    int rc = ps_vm_run_main(ctx, m, NULL, 0, &ret);
    if (ret) ps_value_release(ret);
    if (rc != 0) {
      const char *msg = ps_last_error_message(ctx);
      fprintf(stderr, "run failed at iter %d: %s\n", i, msg ? msg : "error");
      ps_ir_free(m);
      ps_ctx_destroy(ctx);
      return 2;
    }
    if (ps_ir_group_table_ptr(m) != group_ptr) {
      fprintf(stderr, "group table pointer changed at iter %d\n", i);
      ps_ir_free(m);
      ps_ctx_destroy(ctx);
      return 3;
    }
    if (group_count > 0 && ps_ir_group_member_table_ptr(m, 0) != member_ptr0) {
      fprintf(stderr, "member table pointer changed at iter %d\n", i);
      ps_ir_free(m);
      ps_ctx_destroy(ctx);
      return 3;
    }
  }

  ps_ir_free(m);
  ps_ctx_destroy(ctx);
  printf("OK\n");
  return 0;
}
