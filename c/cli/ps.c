#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>

#include "../frontend.h"
#include "../runtime/ps_vm.h"
#include "../runtime/ps_runtime.h"
#include "../runtime/ps_errors.h"

static void usage(void) {
  fprintf(stderr, "Usage:\n");
  fprintf(stderr, "  ps run <file>\n");
  fprintf(stderr, "  ps -e \"<code>\"\n");
  fprintf(stderr, "  ps repl\n");
  fprintf(stderr, "  ps check <file>\n");
  fprintf(stderr, "  ps ast <file>\n");
  fprintf(stderr, "  ps ir <file>\n");
  fprintf(stderr, "  ps test\n");
  fprintf(stderr, "Options:\n");
  fprintf(stderr, "  --help, --version, --trace, --trace-ir, --time\n");
}

static int write_temp_source(const char *code, char *out_path, size_t out_sz) {
  snprintf(out_path, out_sz, "/tmp/ps_inline_%d.pts", getpid());
  FILE *f = fopen(out_path, "w");
  if (!f) return 0;
  fprintf(f, "function main() : void {\n%s\n}\n", code);
  fclose(f);
  return 1;
}

static PS_IR_Module *load_ir_from_file(PS_Context *ctx, const char *file) {
  PsDiag d;
  char *buf = NULL;
  size_t len = 0;
  FILE *mem = open_memstream(&buf, &len);
  if (!mem) {
    ps_throw(ctx, PS_ERR_INTERNAL, "open_memstream failed");
    return NULL;
  }
  int rc = ps_emit_ir_json(file, &d, mem);
  fclose(mem);
  if (rc != 0) {
    fprintf(stderr, "%s:%d:%d %s %s: %s\n", d.file ? d.file : file, d.line, d.col, d.code ? d.code : "E0001",
            d.category ? d.category : "FRONTEND_ERROR", d.message);
    free(buf);
    return NULL;
  }
  PS_IR_Module *m = ps_ir_load_json(ctx, buf, len);
  free(buf);
  return m;
}

static int run_file(PS_Context *ctx, const char *file) {
  PS_IR_Module *m = load_ir_from_file(ctx, file);
  if (!m) return 1;
  int rc = ps_vm_run_main(ctx, m);
  ps_ir_free(m);
  return rc;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    usage();
    return 2;
  }

  int trace = 0;
  int trace_ir = 0;
  int do_time = 0;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--help") == 0) {
      usage();
      return 0;
    }
    if (strcmp(argv[i], "--version") == 0) {
      printf("ProtoScript CLI (C runtime) v2.0\n");
      return 0;
    }
    if (strcmp(argv[i], "--trace") == 0) trace = 1;
    if (strcmp(argv[i], "--trace-ir") == 0) trace_ir = 1;
    if (strcmp(argv[i], "--time") == 0) do_time = 1;
  }

  PS_Context *ctx = ps_ctx_create();
  if (!ctx) return 2;
  ctx->trace = trace;
  ctx->trace_ir = trace_ir;

  struct timespec t0, t1;
  if (do_time) clock_gettime(CLOCK_MONOTONIC, &t0);

  int rc = 0;
  if (strcmp(argv[1], "run") == 0 && argc >= 3) {
    rc = run_file(ctx, argv[2]);
  } else if (strcmp(argv[1], "-e") == 0 && argc >= 3) {
    char path[256];
    if (!write_temp_source(argv[2], path, sizeof(path))) {
      fprintf(stderr, "ps: failed to write temp source\n");
      rc = 2;
    } else {
      rc = run_file(ctx, path);
    }
  } else if (strcmp(argv[1], "repl") == 0) {
    char line[1024];
    while (1) {
      fprintf(stdout, "ps> ");
      fflush(stdout);
      if (!fgets(line, sizeof(line), stdin)) break;
      if (strncmp(line, "exit", 4) == 0) break;
      char path[256];
      if (!write_temp_source(line, path, sizeof(path))) break;
      rc = run_file(ctx, path);
      if (rc != 0) {
        const char *code = NULL;
        const char *cat = ps_runtime_category(ps_last_error_code(ctx), ps_last_error_message(ctx), &code);
        if (cat && code) {
          fprintf(stderr, "%s %s: %s\n", code, cat, ps_last_error_message(ctx));
        } else {
          fprintf(stderr, "%s\n", ps_last_error_message(ctx));
        }
        ps_clear_error(ctx);
      }
    }
    rc = 0;
  } else if (strcmp(argv[1], "check") == 0 && argc >= 3) {
    PsDiag d;
    int r = ps_check_file_static(argv[2], &d);
    if (r != 0) {
      fprintf(stderr, "%s:%d:%d %s %s: %s\n", d.file ? d.file : argv[2], d.line, d.col, d.code ? d.code : "E0001",
              d.category ? d.category : "FRONTEND_ERROR", d.message);
      rc = (r == 2) ? 2 : 1;
    }
  } else if (strcmp(argv[1], "ast") == 0 && argc >= 3) {
    PsDiag d;
    int r = ps_parse_file_ast(argv[2], &d, stdout);
    if (r != 0) {
      fprintf(stderr, "%s:%d:%d %s %s: %s\n", d.file ? d.file : argv[2], d.line, d.col, d.code ? d.code : "E0001",
              d.category ? d.category : "FRONTEND_ERROR", d.message);
      rc = (r == 2) ? 2 : 1;
    }
  } else if (strcmp(argv[1], "ir") == 0 && argc >= 3) {
    PsDiag d;
    int r = ps_emit_ir_json(argv[2], &d, stdout);
    if (r != 0) {
      fprintf(stderr, "%s:%d:%d %s %s: %s\n", d.file ? d.file : argv[2], d.line, d.col, d.code ? d.code : "E0001",
              d.category ? d.category : "FRONTEND_ERROR", d.message);
      rc = (r == 2) ? 2 : 1;
    }
  } else if (strcmp(argv[1], "test") == 0) {
    rc = system("CONFORMANCE_CHECK_CMD=\"./c/ps check\" CONFORMANCE_RUN_CMD=\"./c/ps run\" tests/run_conformance.sh");
    if (rc != 0) rc = 1;
  } else {
    usage();
    rc = 2;
  }

  if (do_time) {
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
    fprintf(stderr, "time: %.2f ms\n", ms);
  }

  if (rc != 0 && ps_last_error_code(ctx) != PS_ERR_NONE) {
    const char *code = NULL;
    const char *cat = ps_runtime_category(ps_last_error_code(ctx), ps_last_error_message(ctx), &code);
    if (cat && code) {
      fprintf(stderr, "%s %s: %s\n", code, cat, ps_last_error_message(ctx));
    } else {
      fprintf(stderr, "error: %s\n", ps_last_error_message(ctx));
    }
  }

  ps_ctx_destroy(ctx);
  return rc == 0 ? 0 : 1;
}
