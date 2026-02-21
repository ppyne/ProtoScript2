#include <limits.h>
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
#include "../diag.h"

static const char *g_last_run_file = NULL;

static void usage(void) {
  fprintf(stderr, "Usage:\n");
  fprintf(stderr, "  ps run <file> [args...]\n");
  fprintf(stderr, "  ps -e \"<code>\" [args...]\n");
  fprintf(stderr, "  ps repl\n");
  fprintf(stderr, "  ps check <file>\n");
  fprintf(stderr, "  ps ast <file>\n");
  fprintf(stderr, "  ps ir <file>\n");
  fprintf(stderr, "Options:\n");
  fprintf(stderr, "  --help, --version, --trace, --trace-ir, --time\n");
}

static void print_diag(FILE *out, const char *fallback_file, const PsDiag *d) {
  ps_diag_write(out, fallback_file, d);
}

static const char *exc_string(PS_Value *v) {
  if (!v || v->tag != PS_V_STRING) return "";
  return v->as.string_v.ptr ? v->as.string_v.ptr : "";
}

static void print_exception(FILE *out, const char *fallback_file, PS_Value *ex) {
  if (!ex || ex->tag != PS_V_EXCEPTION) return;
  const char *tn = ex->as.exc_v.type_name ? ex->as.exc_v.type_name : (ex->as.exc_v.is_runtime ? "RuntimeException" : "Exception");
  const char *raw = exc_string(ex->as.exc_v.file);
  if (tn && strcmp(tn, "Utf8DecodeException") == 0) raw = "ps_tmp";
  if (!raw || !*raw) raw = "ps_tmp";
  const char *file = raw ? raw : (fallback_file ? fallback_file : "<unknown>");
  long long line = ex->as.exc_v.line > 0 ? (long long)ex->as.exc_v.line : 1;
  long long col = ex->as.exc_v.column > 0 ? (long long)ex->as.exc_v.column : 1;
  const char *msg = exc_string(ex->as.exc_v.message);
  const char *code = exc_string(ex->as.exc_v.code);
  const char *category = exc_string(ex->as.exc_v.category);
  if (ex->as.exc_v.is_runtime && code && *code && category && *category) {
    fprintf(out, "%s:%lld:%lld %s %s: %s\n", file, line, col, code, category, msg ? msg : "");
    return;
  }
  char got[192];
  if (msg && *msg) snprintf(got, sizeof(got), "%s(\"%s\")", tn, msg);
  else snprintf(got, sizeof(got), "%s", tn);
  char formatted[256];
  ps_format_diag(formatted, sizeof(formatted), "unhandled exception", got, "matching catch");
  fprintf(out, "%s:%lld:%lld R1011 UNHANDLED_EXCEPTION: %s\n", file, line, col, formatted);
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
#ifdef __EMSCRIPTEN__
  char tmp_path[128];
  snprintf(tmp_path, sizeof(tmp_path), "/tmp/ps_ir_%d.json", getpid());
  FILE *tmp = fopen(tmp_path, "w+");
  if (!tmp) {
    ps_throw(ctx, PS_ERR_INTERNAL, "failed to open IR temp file");
    return NULL;
  }
  int rc = ps_emit_ir_json(file, &d, tmp);
  if (rc != 0) {
    fclose(tmp);
    unlink(tmp_path);
    print_diag(stderr, file, &d);
    return NULL;
  }
  if (fflush(tmp) != 0 || fseek(tmp, 0, SEEK_END) != 0) {
    fclose(tmp);
    unlink(tmp_path);
    ps_throw(ctx, PS_ERR_INTERNAL, "failed to materialize IR");
    return NULL;
  }
  long sz = ftell(tmp);
  if (sz < 0 || fseek(tmp, 0, SEEK_SET) != 0) {
    fclose(tmp);
    unlink(tmp_path);
    ps_throw(ctx, PS_ERR_INTERNAL, "failed to seek IR");
    return NULL;
  }
  size_t len = (size_t)sz;
  char *buf = (char *)malloc(len + 1);
  if (!buf) {
    fclose(tmp);
    unlink(tmp_path);
    ps_throw(ctx, PS_ERR_OOM, "out of memory");
    return NULL;
  }
  size_t got = fread(buf, 1, len, tmp);
  fclose(tmp);
  unlink(tmp_path);
  if (got != len) {
    free(buf);
    ps_throw(ctx, PS_ERR_INTERNAL, "failed to read IR");
    return NULL;
  }
  buf[len] = '\0';
  PS_IR_Module *m = ps_ir_load_json(ctx, buf, len);
  free(buf);
  return m;
#else
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
    print_diag(stderr, file, &d);
    free(buf);
    return NULL;
  }
  PS_IR_Module *m = ps_ir_load_json(ctx, buf, len);
  free(buf);
  return m;
#endif
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

static int static_check_before_run(const char *file) {
  PsDiag d;
  int r = ps_check_file_static(file, &d);
  if (r != 0) {
    print_diag(stderr, file, &d);
    return EXIT_FAILURE;
  }
  return 0;
}

static int is_cli_option(const char *arg) {
  return strcmp(arg, "--help") == 0 || strcmp(arg, "--version") == 0 || strcmp(arg, "--trace") == 0 ||
         strcmp(arg, "--trace-ir") == 0 || strcmp(arg, "--time") == 0;
}

static int is_cli_command(const char *arg) {
  return strcmp(arg, "run") == 0 || strcmp(arg, "-e") == 0 || strcmp(arg, "repl") == 0 ||
         strcmp(arg, "check") == 0 || strcmp(arg, "ast") == 0 || strcmp(arg, "ir") == 0;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    usage();
    return 2;
  }

  char exe_buf[PATH_MAX];
  if (realpath(argv[0], exe_buf)) {
    char *slash = strrchr(exe_buf, '/');
    if (slash) {
      *slash = '\0';
      ps_set_registry_exe_dir(exe_buf);
    }
  } else {
    char *slash = strrchr(argv[0], '/');
    if (slash) {
      size_t n = (size_t)(slash - argv[0]);
      if (n >= sizeof(exe_buf)) n = sizeof(exe_buf) - 1;
      memcpy(exe_buf, argv[0], n);
      exe_buf[n] = '\0';
      ps_set_registry_exe_dir(exe_buf);
    }
  }

  int trace = 0;
  int trace_ir = 0;
  int do_time = 0;
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
    if (cmd_index == -1 && !is_cli_option(argv[i]) && is_cli_command(argv[i])) {
      cmd_index = i;
    }
  }
  if (cmd_index == -1) {
    usage();
    return 2;
  }

  PS_Context *ctx = ps_ctx_create();
  if (!ctx) return 2;
  ctx->trace = trace;
  ctx->trace_ir = trace_ir;

  struct timespec t0, t1;
  if (do_time) clock_gettime(CLOCK_MONOTONIC, &t0);

  int rc = 0;
  int exit_code = 0;
  int static_failure = 0;
  PS_Value *ret = NULL;
  if (strcmp(argv[cmd_index], "run") == 0 && (cmd_index + 1) < argc) {
    g_last_run_file = argv[cmd_index + 1];
    rc = static_check_before_run(g_last_run_file);
    if (rc != 0) static_failure = 1;
    if (rc == 0) {
      PS_Value *args_list = build_args_list(ctx, argc, argv, 0);
      rc = run_file(ctx, argv[cmd_index + 1], args_list, &ret);
      if (args_list) ps_value_release(args_list);
    }
  } else if (strcmp(argv[cmd_index], "-e") == 0 && (cmd_index + 1) < argc) {
    char path[256];
    if (!write_temp_source(argv[cmd_index + 1], path, sizeof(path))) {
      fprintf(stderr, "ps: failed to write temp source\n");
      rc = 2;
    } else {
      g_last_run_file = path;
      rc = static_check_before_run(g_last_run_file);
      if (rc != 0) static_failure = 1;
      if (rc == 0) {
        PS_Value *args_list = build_args_list(ctx, argc, argv, 0);
        rc = run_file(ctx, path, args_list, &ret);
        if (args_list) ps_value_release(args_list);
      }
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
      rc = static_check_before_run(path);
      if (rc != 0) {
        static_failure = 1;
        ps_clear_error(ctx);
        continue;
      }
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
      print_diag(stderr, argv[cmd_index + 1], &d);
      rc = (r == 2) ? 1 : 2;
    }
  } else if (strcmp(argv[cmd_index], "ast") == 0 && (cmd_index + 1) < argc) {
    PsDiag d;
    int r = ps_parse_file_ast(argv[cmd_index + 1], &d, stdout);
    if (r != 0) {
      print_diag(stderr, argv[cmd_index + 1], &d);
      rc = (r == 2) ? 1 : 2;
    }
  } else if (strcmp(argv[cmd_index], "ir") == 0 && (cmd_index + 1) < argc) {
    PsDiag d;
    int r = ps_emit_ir_json(argv[cmd_index + 1], &d, stdout);
    if (r != 0) {
      print_diag(stderr, argv[cmd_index + 1], &d);
      rc = (r == 2) ? 1 : 2;
    }
  } else {
    usage();
    rc = 2;
  }

  if (do_time) {
    clock_gettime(CLOCK_MONOTONIC, &t1);
    double ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6;
    fprintf(stderr, "time: %.2f ms\n", ms);
  }

  if (rc != 0) {
    if (ctx->last_exception) {
      print_exception(stderr, g_last_run_file, ctx->last_exception);
    } else if (ps_last_error_code(ctx) != PS_ERR_NONE) {
      const char *code = NULL;
      const char *cat = ps_runtime_category(ps_last_error_code(ctx), ps_last_error_message(ctx), &code);
      const char *file = g_last_run_file ? g_last_run_file : "<runtime>";
      const char *msg = ps_last_error_message(ctx);
      if (cat && code) {
        fprintf(stderr, "%s:%d:%d %s %s: %s\n", file, 1, 1, code, cat, msg ? msg : "");
      } else {
        fprintf(stderr, "%s:%d:%d R1010 RUNTIME_ERROR: %s\n", file, 1, 1, msg ? msg : "runtime error");
      }
    }
  }

  if (rc == 0 && ret && ps_typeof(ret) == PS_T_INT) {
    exit_code = (int)ps_as_int(ret);
  } else if (rc != 0) {
    PS_ErrorCode err = ps_last_error_code(ctx);
    if (static_failure) exit_code = EXIT_FAILURE;
    else if (ctx->last_exception) exit_code = 1;
    else if (err == PS_ERR_INTERNAL || err == PS_ERR_OOM) exit_code = 1;
    else exit_code = 2;
  } else {
    exit_code = 0;
  }

  if (ret) ps_value_release(ret);
  ps_ctx_destroy(ctx);
  return exit_code;
}
