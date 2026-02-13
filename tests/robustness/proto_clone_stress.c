#include <stdio.h>
#include <stdlib.h>

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
  const char *file = argc > 1 ? argv[1] : "tests/edge/proto_clone_stress.pts";
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

  const PS_IR_Proto *base = ps_ir_find_proto(m, "Base");
  const PS_IR_Proto *child = ps_ir_find_proto(m, "Child");
  printf("proto.Base=%p methods=%p fields=%p\n", (void *)base, base ? (void *)base->methods : NULL, base ? (void *)base->fields : NULL);
  printf("proto.Child=%p methods=%p fields=%p\n", (void *)child, child ? (void *)child->methods : NULL, child ? (void *)child->fields : NULL);

  for (int i = 0; i < 3; i++) {
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
    if (ps_ir_find_proto(m, "Child") != child || ps_ir_find_proto(m, "Base") != base) {
      fprintf(stderr, "proto descriptor pointer changed at iter %d\n", i);
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
