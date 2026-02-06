#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "ps/ps_api.h"

static int parse_mode(const char *mode, uint32_t *flags, char out[3]) {
  if (!mode || !flags || !out) return 0;
  size_t len = strlen(mode);
  if (len != 1 && len != 2) return 0;
  char m0 = mode[0];
  char m1 = (len == 2) ? mode[1] : '\0';
  if (m1 && m1 != 'b') return 0;
  uint32_t f = 0;
  if (m0 == 'r') f |= PS_FILE_READ;
  else if (m0 == 'w') f |= PS_FILE_WRITE;
  else if (m0 == 'a') f |= PS_FILE_APPEND | PS_FILE_WRITE;
  else return 0;
  if (m1 == 'b') f |= PS_FILE_BINARY;
  out[0] = m0;
  out[1] = m1 ? 'b' : '\0';
  out[2] = '\0';
  *flags = f;
  return 1;
}

static PS_Status io_open(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!argv || !out) return PS_ERR;
  if (ps_typeof(argv[0]) != PS_T_STRING || ps_typeof(argv[1]) != PS_T_STRING) {
    ps_throw(ctx, PS_ERR_TYPE, "Io.open expects (string, string)");
    return PS_ERR;
  }
  const char *path = ps_string_ptr(argv[0]);
  const char *mode = ps_string_ptr(argv[1]);
  uint32_t flags = 0;
  char fmode[3];
  if (!parse_mode(mode, &flags, fmode)) {
    ps_throw(ctx, PS_ERR_RANGE, "invalid mode");
    return PS_ERR;
  }
  FILE *fp = fopen(path, fmode);
  if (!fp) {
    ps_throw(ctx, PS_ERR_INTERNAL, strerror(errno));
    return PS_ERR;
  }
  PS_Value *f = ps_make_file(ctx, fp, flags);
  if (!f) {
    fclose(fp);
    return PS_ERR;
  }
  *out = f;
  return PS_OK;
}

static PS_Value *to_string_value(PS_Context *ctx, PS_Value *v) {
  if (!v) return ps_make_string_utf8(ctx, "", 0);
  PS_TypeTag t = ps_typeof(v);
  if (t == PS_T_STRING) return ps_value_retain(v);
  if (t == PS_T_BOOL) {
    const char *s = ps_as_bool(v) ? "true" : "false";
    return ps_make_string_utf8(ctx, s, strlen(s));
  }
  if (t == PS_T_INT) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%lld", (long long)ps_as_int(v));
    return ps_make_string_utf8(ctx, buf, strlen(buf));
  }
  if (t == PS_T_FLOAT) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.17g", ps_as_float(v));
    return ps_make_string_utf8(ctx, buf, strlen(buf));
  }
  if (t == PS_T_BYTE) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%u", (unsigned)ps_as_byte(v));
    return ps_make_string_utf8(ctx, buf, strlen(buf));
  }
  if (t == PS_T_GLYPH) {
    char buf[32];
    snprintf(buf, sizeof(buf), "U+%04X", (unsigned)ps_as_glyph(v));
    return ps_make_string_utf8(ctx, buf, strlen(buf));
  }
  return ps_make_string_utf8(ctx, "<value>", 7);
}

static PS_Status io_print(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)out;
  if (!argv || argc < 1) return PS_OK;
  PS_Value *s = to_string_value(ctx, argv[0]);
  if (!s) return PS_ERR;
  const char *p = ps_string_ptr(s);
  size_t n = ps_string_len(s);
  if (n > 0) fwrite(p, 1, n, stdout);
  ps_value_release(s);
  return PS_OK;
}

static PS_Status io_print_line(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)out;
  if (!argv || argc < 1) return PS_OK;
  PS_Value *s = to_string_value(ctx, argv[0]);
  if (!s) return PS_ERR;
  const char *p = ps_string_ptr(s);
  size_t n = ps_string_len(s);
  if (n > 0) fwrite(p, 1, n, stdout);
  fwrite("\n", 1, 1, stdout);
  ps_value_release(s);
  return PS_OK;
}

PS_Status ps_module_init(PS_Context *ctx, PS_Module *out) {
  (void)ctx;
  setvbuf(stdout, NULL, _IONBF, 0);
  setvbuf(stderr, NULL, _IONBF, 0);
  static PS_NativeFnDesc fns[] = {
      {.name = "open", .fn = io_open, .arity = 2, .ret_type = PS_T_FILE, .param_types = NULL, .flags = 0},
      {.name = "print", .fn = io_print, .arity = 1, .ret_type = PS_T_VOID, .param_types = NULL, .flags = 0},
      {.name = "printLine", .fn = io_print_line, .arity = 1, .ret_type = PS_T_VOID, .param_types = NULL, .flags = 0},
  };
  out->module_name = "Io";
  out->api_version = PS_API_VERSION;
  out->fn_count = sizeof(fns) / sizeof(fns[0]);
  out->fns = fns;
  return PS_OK;
}
