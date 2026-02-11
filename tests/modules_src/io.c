#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#else
#include <unistd.h>
#include <fcntl.h>
#endif

#include "ps/ps_api.h"

static int parse_mode(const char *mode, uint32_t *flags, char out[3], int binary) {
  if (!mode || !flags || !out) return 0;
  if (strlen(mode) != 1) return 0;
  char m0 = mode[0];
  uint32_t f = 0;
  if (m0 == 'r') f |= PS_FILE_READ;
  else if (m0 == 'w') f |= PS_FILE_WRITE;
  else if (m0 == 'a') f |= PS_FILE_APPEND | PS_FILE_WRITE;
  else return 0;
  if (binary) f |= PS_FILE_BINARY;
  out[0] = m0;
  out[1] = binary ? 'b' : '\0';
  out[2] = '\0';
  *flags = f;
  return 1;
}

static PS_Status io_open_text(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!argv || !out) return PS_ERR;
  if (ps_typeof(argv[0]) != PS_T_STRING || ps_typeof(argv[1]) != PS_T_STRING) {
    ps_throw(ctx, PS_ERR_TYPE, "Io.openText expects (string, string)");
    return PS_ERR;
  }
  const char *path = ps_string_ptr(argv[0]);
  const char *mode = ps_string_ptr(argv[1]);
  uint32_t flags = 0;
  char fmode[3];
  if (!parse_mode(mode, &flags, fmode, 0)) {
    ps_throw(ctx, PS_ERR_RANGE, "invalid mode");
    return PS_ERR;
  }
  FILE *fp = fopen(path, fmode);
  if (!fp) {
    ps_throw(ctx, PS_ERR_INTERNAL, strerror(errno));
    return PS_ERR;
  }
  PS_Value *f = ps_make_file(ctx, fp, flags, path);
  if (!f) {
    fclose(fp);
    return PS_ERR;
  }
  *out = f;
  return PS_OK;
}

static PS_Status io_open_binary(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!argv || !out) return PS_ERR;
  if (ps_typeof(argv[0]) != PS_T_STRING || ps_typeof(argv[1]) != PS_T_STRING) {
    ps_throw(ctx, PS_ERR_TYPE, "Io.openBinary expects (string, string)");
    return PS_ERR;
  }
  const char *path = ps_string_ptr(argv[0]);
  const char *mode = ps_string_ptr(argv[1]);
  uint32_t flags = 0;
  char fmode[3];
  if (!parse_mode(mode, &flags, fmode, 1)) {
    ps_throw(ctx, PS_ERR_RANGE, "invalid mode");
    return PS_ERR;
  }
  FILE *fp = fopen(path, fmode);
  if (!fp) {
    ps_throw(ctx, PS_ERR_INTERNAL, strerror(errno));
    return PS_ERR;
  }
  PS_Value *f = ps_make_file(ctx, fp, flags, path);
  if (!f) {
    fclose(fp);
    return PS_ERR;
  }
  *out = f;
  return PS_OK;
}

static uint64_t io_temp_seq = 0;

static const char *io_temp_dir(PS_Context *ctx) {
#ifndef _WIN32
  (void)ctx;
#endif
#ifdef _WIN32
  const char *dir = getenv("TEMP");
  if (!dir || dir[0] == '\0') {
    ps_throw(ctx, PS_ERR_INTERNAL, "TEMP not set");
    return NULL;
  }
  return dir;
#else
  const char *dir = getenv("TMPDIR");
  if (!dir || dir[0] == '\0') dir = "/tmp";
  return dir;
#endif
}

static int io_path_exists(const char *path) {
#ifdef _WIN32
  struct _stat st;
  return _stat(path, &st) == 0;
#else
  struct stat st;
  return stat(path, &st) == 0;
#endif
}

static void io_seed_rand(void) {
  static int seeded = 0;
  if (seeded) return;
  seeded = 1;
  unsigned int seed = (unsigned int)time(NULL);
#ifdef _WIN32
  seed ^= (unsigned int)_getpid();
#else
  seed ^= (unsigned int)getpid();
#endif
  srand(seed);
}

static int io_rand_bytes(uint8_t *out, size_t n) {
#ifdef _WIN32
  for (size_t i = 0; i < n; i += 1) {
    unsigned int v = 0;
    if (rand_s(&v) != 0) {
      io_seed_rand();
      v = (unsigned int)rand();
    }
    out[i] = (uint8_t)(v & 0xFF);
  }
  return 1;
#else
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd >= 0) {
    size_t off = 0;
    while (off < n) {
      ssize_t r = read(fd, out + off, n - off);
      if (r <= 0) break;
      off += (size_t)r;
    }
    close(fd);
    if (off == n) return 1;
  }
  io_seed_rand();
  for (size_t i = 0; i < n; i += 1) out[i] = (uint8_t)(rand() & 0xFF);
  return 1;
#endif
}

static void io_hex_encode(const uint8_t *in, size_t n, char *out) {
  static const char *hex = "0123456789abcdef";
  for (size_t i = 0; i < n; i += 1) {
    out[i * 2] = hex[(in[i] >> 4) & 0xF];
    out[i * 2 + 1] = hex[in[i] & 0xF];
  }
  out[n * 2] = '\0';
}

static PS_Status io_temp_path(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  (void)argv;
  if (!out) return PS_ERR;
  const char *dir = io_temp_dir(ctx);
  if (!dir) return PS_ERR;
  size_t dir_len = strlen(dir);
  char sep[2] = {0, 0};
  if (dir_len > 0) {
    char last = dir[dir_len - 1];
    if (last != '/' && last != '\\') {
#ifdef _WIN32
      sep[0] = '\\';
#else
      sep[0] = '/';
#endif
    }
  }
  const int max_attempts = 128;
  const char *prefix = "ps_";
  for (int attempt = 0; attempt < max_attempts; attempt += 1) {
    uint8_t rnd[16];
    if (!io_rand_bytes(rnd, sizeof(rnd))) {
      ps_throw(ctx, PS_ERR_INTERNAL, "tempPath failed");
      return PS_ERR;
    }
    char hex[33];
    io_hex_encode(rnd, sizeof(rnd), hex);
    uint64_t seq = io_temp_seq++;
    char name[80];
    snprintf(name, sizeof(name), "%s%s_%llx", prefix, hex, (unsigned long long)seq);
    size_t path_len = dir_len + strlen(sep) + strlen(name);
    char *full = (char *)malloc(path_len + 1);
    if (!full) {
      ps_throw(ctx, PS_ERR_OOM, "out of memory");
      return PS_ERR;
    }
    snprintf(full, path_len + 1, "%s%s%s", dir, sep, name);
    if (!io_path_exists(full)) {
      PS_Value *s = ps_make_string_utf8(ctx, full, path_len);
      free(full);
      if (!s) return PS_ERR;
      *out = s;
      return PS_OK;
    }
    free(full);
  }
  ps_throw(ctx, PS_ERR_INTERNAL, "tempPath failed");
  return PS_ERR;
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
      {.name = "openText", .fn = io_open_text, .arity = 2, .ret_type = PS_T_FILE, .param_types = NULL, .flags = 0},
      {.name = "openBinary", .fn = io_open_binary, .arity = 2, .ret_type = PS_T_FILE, .param_types = NULL, .flags = 0},
      {.name = "tempPath", .fn = io_temp_path, .arity = 0, .ret_type = PS_T_STRING, .param_types = NULL, .flags = 0},
      {.name = "print", .fn = io_print, .arity = 1, .ret_type = PS_T_VOID, .param_types = NULL, .flags = 0},
      {.name = "printLine", .fn = io_print_line, .arity = 1, .ret_type = PS_T_VOID, .param_types = NULL, .flags = 0},
  };
  out->module_name = "Io";
  out->api_version = PS_API_VERSION;
  out->fn_count = sizeof(fns) / sizeof(fns[0]);
  out->fns = fns;
  return PS_OK;
}
