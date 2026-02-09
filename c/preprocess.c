#define _GNU_SOURCE
#include "preprocess.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../third_party/mcpp/mcpp_lib.h"

static char *dup_str(const char *s) {
  if (!s) return NULL;
  size_t n = strlen(s);
  char *out = (char *)malloc(n + 1);
  if (!out) return NULL;
  memcpy(out, s, n + 1);
  return out;
}

void preprocess_config_init(PreprocessConfig *cfg) {
  if (!cfg) return;
  cfg->enabled = 0;
  cfg->tool = NULL;
  cfg->options = NULL;
  cfg->option_len = 0;
}

void preprocess_config_free(PreprocessConfig *cfg) {
  if (!cfg) return;
  free(cfg->tool);
  for (size_t i = 0; i < cfg->option_len; i++) free(cfg->options[i]);
  free(cfg->options);
  preprocess_config_init(cfg);
}

static char *dirname_dup(const char *path) {
  if (!path || !*path) return dup_str(".");
  const char *slash = strrchr(path, '/');
  if (!slash) return dup_str(".");
  size_t n = (size_t)(slash - path);
  if (n == 0) return dup_str("/");
  char *out = (char *)malloc(n + 1);
  if (!out) return NULL;
  memcpy(out, path, n);
  out[n] = '\0';
  return out;
}

static char *strip_line_markers(const char *src, size_t *out_len) {
  size_t n = strlen(src);
  char *out = (char *)malloc(n + 1);
  if (!out) return NULL;
  size_t oi = 0;
  size_t i = 0;
  while (i < n) {
    size_t line_start = i;
    size_t j = i;
    while (j < n && (src[j] == ' ' || src[j] == '\t')) j++;
    int is_marker = (j < n && src[j] == '#');
    if (is_marker) {
      while (i < n && src[i] != '\n') i++;
      if (i < n && src[i] == '\n') i++;
      continue;
    }
    while (i < n && src[i] != '\n') out[oi++] = src[i++];
    if (i < n && src[i] == '\n') out[oi++] = src[i++];
    (void)line_start;
  }
  out[oi] = '\0';
  if (out_len) *out_len = oi;
  return out;
}

typedef struct {
  const char *buf;
  size_t len;
  size_t pos;
} MemInput;

#if defined(__APPLE__)
static int mem_read(void *cookie, char *buf, int size) {
  MemInput *mi = (MemInput *)cookie;
  if (!mi || mi->pos >= mi->len) return 0;
  size_t avail = mi->len - mi->pos;
  size_t n = (size_t)size < avail ? (size_t)size : avail;
  memcpy(buf, mi->buf + mi->pos, n);
  mi->pos += n;
  return (int)n;
}

static fpos_t mem_seek(void *cookie, fpos_t offset, int whence) {
  MemInput *mi = (MemInput *)cookie;
  if (!mi) return (fpos_t)-1;
  size_t base = 0;
  if (whence == SEEK_SET) base = 0;
  else if (whence == SEEK_CUR) base = mi->pos;
  else if (whence == SEEK_END) base = mi->len;
  else return (fpos_t)-1;
  long long off = (long long)offset;
  size_t next = 0;
  if (off < 0) {
    size_t neg = (size_t)(-off);
    if (neg > base) return (fpos_t)-1;
    next = base - neg;
  } else {
    next = base + (size_t)off;
  }
  if (next > mi->len) return (fpos_t)-1;
  mi->pos = next;
  return (fpos_t)mi->pos;
}

static int mem_close(void *cookie) {
  MemInput *mi = (MemInput *)cookie;
  free(mi);
  return 0;
}
#endif

static FILE *open_mem_input(const char *input, size_t input_len, char **out_err) {
#if defined(__GLIBC__) || defined(__EMSCRIPTEN__)
  FILE *f = fmemopen((void *)input, input_len, "r");
  if (!f && out_err) {
    char buf[128];
    snprintf(buf, sizeof(buf), "fmemopen failed: %s", strerror(errno));
    *out_err = dup_str(buf);
  }
  return f;
#elif defined(__APPLE__)
  MemInput *mi = (MemInput *)calloc(1, sizeof(MemInput));
  if (!mi) {
    if (out_err) *out_err = dup_str("memory allocation failed");
    return NULL;
  }
  mi->buf = input;
  mi->len = input_len;
  mi->pos = 0;
  FILE *f = funopen(mi, mem_read, NULL, mem_seek, mem_close);
  if (!f) {
    if (out_err) {
      char buf[128];
      snprintf(buf, sizeof(buf), "funopen failed: %s", strerror(errno));
      *out_err = dup_str(buf);
    }
    free(mi);
  }
  return f;
#else
  (void)input;
  (void)input_len;
  if (out_err) *out_err = dup_str("in-memory input unavailable on this platform");
  return NULL;
#endif
}

int preprocess_source(
    const char *input,
    size_t input_len,
    char **output,
    size_t *output_len,
    const PreprocessConfig *config,
    const char *input_name,
    char **out_error
) {
  if (!output || !output_len) return 0;
  *output = NULL;
  *output_len = 0;
  if (out_error) *out_error = NULL;

  if (!config || !config->enabled) {
    char *copy = (char *)malloc(input_len + 1);
    if (!copy) return 0;
    memcpy(copy, input, input_len);
    copy[input_len] = '\0';
    *output = copy;
    *output_len = input_len;
    return 1;
  }

  if (!config->tool || strcmp(config->tool, "mcpp") != 0) {
    if (out_error) *out_error = dup_str("unsupported preprocessor tool");
    return 0;
  }

  const char *input_arg = "-";
  FILE *mem_in = NULL;
  FILE *prev_stdin = NULL;
#if defined(__EMSCRIPTEN__)
  char tmp_path[128];
  snprintf(tmp_path, sizeof(tmp_path), "/tmp/ps_mcpp_input_%lu.txt", (unsigned long)time(NULL));
  FILE *tmpf = fopen(tmp_path, "wb");
  if (!tmpf) {
    if (out_error) {
      char buf[128];
      snprintf(buf, sizeof(buf), "failed to create temp input: %s", strerror(errno));
      *out_error = dup_str(buf);
    }
    return 0;
  }
  if (input_len && fwrite(input, 1, input_len, tmpf) != input_len) {
    if (out_error) *out_error = dup_str("failed to write temp input");
    fclose(tmpf);
    remove(tmp_path);
    return 0;
  }
  fclose(tmpf);
  input_arg = tmp_path;
#else
  prev_stdin = stdin;
  mem_in = open_mem_input(input, input_len, out_error);
  if (!mem_in) return 0;
  stdin = mem_in;
#endif

  size_t extra_args = 0;
  char *dir = dirname_dup(input_name);
  if (dir && strcmp(dir, ".") != 0) extra_args = 2; /* -I <dir> */

  size_t argc = 1 + config->option_len + extra_args + 1;
  char **argv = (char **)calloc(argc + 1, sizeof(char *));
  if (!argv) {
#if defined(__EMSCRIPTEN__)
    remove(input_arg);
#else
    stdin = prev_stdin;
    fclose(mem_in);
#endif
    free(dir);
    return 0;
  }

  size_t idx = 0;
  argv[idx++] = (char *)"mcpp";
  for (size_t i = 0; i < config->option_len; i++) argv[idx++] = config->options[i];
  if (extra_args) {
    argv[idx++] = (char *)"-I";
    argv[idx++] = dir;
  }
  argv[idx++] = (char *)input_arg;
  argv[idx] = NULL;

  mcpp_use_mem_buffers(1);
  mcpp_reset_def_out_func();

  int rc = mcpp_lib_main((int)idx, argv);

#if defined(__EMSCRIPTEN__)
  remove(input_arg);
#else
  stdin = prev_stdin;
  fclose(mem_in);
#endif
  free(dir);
  free(argv);

  if (rc != 0) {
    if (out_error) {
      const char *err = mcpp_get_mem_buffer(ERR);
      *out_error = err ? dup_str(err) : dup_str("preprocessor failed");
    }
    return 0;
  }

  const char *buf = mcpp_get_mem_buffer(OUT);
  if (!buf) {
    if (out_error) *out_error = dup_str("preprocessor returned empty output");
    return 0;
  }

  size_t n = 0;
  char *filtered = strip_line_markers(buf, &n);
  if (!filtered) return 0;
  *output = filtered;
  *output_len = n;
  return 1;
}
