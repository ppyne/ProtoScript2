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
#include "../runtime/ps_list.h"

static void usage(void) {
  fprintf(stderr, "Usage:\n");
  fprintf(stderr, "  ps run <file> [args...]\n");
  fprintf(stderr, "  ps -e \"<code>\" [args...]\n");
  fprintf(stderr, "  ps repl\n");
  fprintf(stderr, "  ps check <file>\n");
  fprintf(stderr, "  ps ast <file>\n");
  fprintf(stderr, "  ps ir <file>\n");
  fprintf(stderr, "  ps emit-c <file> [--opt]\n");
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

static int file_is_executable(const char *path) {
  return access(path, X_OK) == 0;
}

static int forward_emit_c(const char *file, int opt) {
  const char *candidates[] = { "./bin/protoscriptc", "bin/protoscriptc", NULL };
  const char *target = NULL;
  for (int i = 0; candidates[i] != NULL; i++) {
    if (file_is_executable(candidates[i])) {
      target = candidates[i];
      break;
    }
  }
  if (!target) {
    fprintf(stderr, "ps: cannot find reference compiler at ./bin/protoscriptc\n");
    return 2;
  }

  if (opt) {
    char *args[] = { (char *)target, "--emit-c", (char *)file, "--opt", NULL };
    execv(target, args);
  } else {
    char *args[] = { (char *)target, "--emit-c", (char *)file, NULL };
    execv(target, args);
  }

  fprintf(stderr, "ps: failed to exec %s: %s\n", target, strerror(errno));
  return 2;
}

static PS_Value *build_args_list(PS_Context *ctx, int argc, char **argv, int start) {
  PS_Value *list = ps_make_list(ctx);
  if (!list) return NULL;
  for (int i = start; i < argc; i++) {
    const char *s = argv[i];
    PS_Value *v = ps_make_string_utf8(ctx, s, strlen(s));
    if (!v) {
      ps_value_release(list);
      return NULL;
    }
    if (ps_list_push(ctx, list, v) != PS_OK) {
      ps_value_release(v);
      ps_value_release(list);
      return NULL;
    }
    ps_value_release(v);
  }
  return list;
}

static int run_file(PS_Context *ctx, const char *file, PS_Value *args_list, PS_Value **out_ret) {
  PS_IR_Module *m = load_ir_from_file(ctx, file);
  if (!m) return 1;
  PS_Value *argvs[1];
  size_t argc = 0;
  if (args_list) {
    argvs[0] = args_list;
    argc = 1;
  }
  int rc = ps_vm_run_main(ctx, m, argvs, argc, out_ret);
  ps_ir_free(m);
  return rc;
}

static int is_cli_option(const char *arg) {
  return strcmp(arg, "--help") == 0 || strcmp(arg, "--version") == 0 || strcmp(arg, "--trace") == 0 ||
         strcmp(arg, "--trace-ir") == 0 || strcmp(arg, "--time") == 0 || strcmp(arg, "--opt") == 0;
}

static int is_cli_command(const char *arg) {
  return strcmp(arg, "run") == 0 || strcmp(arg, "-e") == 0 || strcmp(arg, "repl") == 0 ||
         strcmp(arg, "check") == 0 || strcmp(arg, "ast") == 0 || strcmp(arg, "ir") == 0 ||
         strcmp(arg, "emit-c") == 0 || strcmp(arg, "test") == 0;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    usage();
    return 2;
  }

  int trace = 0;
  int trace_ir = 0;
  int do_time = 0;
  int opt = 0;
  int cmd_index = -1;
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
    if (strcmp(argv[i], "--opt") == 0) opt = 1;
    if (cmd_index == -1 && !is_cli_option(argv[i]) && is_cli_command(argv[i])) {
      cmd_index = i;
    }
  }
  if (cmd_index == -1) {
    usage();
    return 2;
  }

  if (strcmp(argv[cmd_index], "emit-c") == 0 && (cmd_index + 1) < argc) {
    return forward_emit_c(argv[cmd_index + 1], opt);
  }

  PS_Context *ctx = ps_ctx_create();
  if (!ctx) return 2;
  ctx->trace = trace;
  ctx->trace_ir = trace_ir;

  struct timespec t0, t1;
  if (do_time) clock_gettime(CLOCK_MONOTONIC, &t0);

  int rc = 0;
  int exit_code = 0;
  PS_Value *ret = NULL;
  if (strcmp(argv[cmd_index], "run") == 0 && (cmd_index + 1) < argc) {
    PS_Value *args_list = build_args_list(ctx, argc, argv, 0);
    rc = run_file(ctx, argv[cmd_index + 1], args_list, &ret);
    if (args_list) ps_value_release(args_list);
  } else if (strcmp(argv[cmd_index], "-e") == 0 && (cmd_index + 1) < argc) {
    char path[256];
    if (!write_temp_source(argv[cmd_index + 1], path, sizeof(path))) {
      fprintf(stderr, "ps: failed to write temp source\n");
      rc = 2;
    } else {
      PS_Value *args_list = build_args_list(ctx, argc, argv, 0);
      rc = run_file(ctx, path, args_list, &ret);
      if (args_list) ps_value_release(args_list);
    }
  } else if (strcmp(argv[cmd_index], "repl") == 0) {
    char line[1024];
    while (1) {
      fprintf(stdout, "ps> ");
      fflush(stdout);
      if (!fgets(line, sizeof(line), stdin)) break;
      if (strncmp(line, "exit", 4) == 0) break;
      char path[256];
      if (!write_temp_source(line, path, sizeof(path))) break;
      rc = run_file(ctx, path, NULL, &ret);
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
  } else if (strcmp(argv[cmd_index], "check") == 0 && (cmd_index + 1) < argc) {
    PsDiag d;
    int r = ps_check_file_static(argv[cmd_index + 1], &d);
    if (r != 0) {
      fprintf(stderr, "%s:%d:%d %s %s: %s\n", d.file ? d.file : argv[cmd_index + 1], d.line, d.col,
              d.code ? d.code : "E0001",
              d.category ? d.category : "FRONTEND_ERROR", d.message);
      rc = (r == 2) ? 1 : 2;
    }
  } else if (strcmp(argv[cmd_index], "ast") == 0 && (cmd_index + 1) < argc) {
    PsDiag d;
    int r = ps_parse_file_ast(argv[cmd_index + 1], &d, stdout);
    if (r != 0) {
      fprintf(stderr, "%s:%d:%d %s %s: %s\n", d.file ? d.file : argv[cmd_index + 1], d.line, d.col,
              d.code ? d.code : "E0001",
              d.category ? d.category : "FRONTEND_ERROR", d.message);
      rc = (r == 2) ? 1 : 2;
    }
  } else if (strcmp(argv[cmd_index], "ir") == 0 && (cmd_index + 1) < argc) {
    PsDiag d;
    int r = ps_emit_ir_json(argv[cmd_index + 1], &d, stdout);
    if (r != 0) {
      fprintf(stderr, "%s:%d:%d %s %s: %s\n", d.file ? d.file : argv[cmd_index + 1], d.line, d.col,
              d.code ? d.code : "E0001",
              d.category ? d.category : "FRONTEND_ERROR", d.message);
      rc = (r == 2) ? 1 : 2;
    }
  } else if (strcmp(argv[cmd_index], "test") == 0) {
    rc = system("CONFORMANCE_CHECK_CMD=\"./c/ps check\" CONFORMANCE_RUN_CMD=\"./c/ps run\" tests/run_conformance.sh");
    if (rc != 0) rc = 2;
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

  if (rc == 0 && ret && ps_typeof(ret) == PS_T_INT) {
    exit_code = (int)ps_as_int(ret);
  } else if (rc != 0) {
    PS_ErrorCode err = ps_last_error_code(ctx);
    if (err == PS_ERR_INTERNAL || err == PS_ERR_OOM) exit_code = 1;
    else exit_code = 2;
  } else {
    exit_code = 0;
  }

  if (ret) ps_value_release(ret);
  ps_ctx_destroy(ctx);
  return exit_code;
}
