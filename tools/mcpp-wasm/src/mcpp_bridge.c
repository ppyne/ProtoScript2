#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "../../../c/preprocess.h"

static char *g_output = NULL;
static char *g_error = NULL;
static PreprocessLineMap g_map = {0};

static char *dup_cstr(const char *s) {
  if (!s) return NULL;
  size_t n = strlen(s);
  char *out = (char *)malloc(n + 1);
  if (!out) return NULL;
  memcpy(out, s, n + 1);
  return out;
}

void ps_mcpp_clear(void) {
  if (g_output) {
    free(g_output);
    g_output = NULL;
  }
  if (g_error) {
    free(g_error);
    g_error = NULL;
  }
  preprocess_line_map_free(&g_map);
}

int ps_mcpp_preprocess(const char *input, const char *input_name) {
  ps_mcpp_clear();
  if (!input) {
    g_error = dup_cstr("null input");
    return 0;
  }

  PreprocessConfig cfg;
  preprocess_config_init(&cfg);
  cfg.enabled = 1;
  cfg.tool = dup_cstr("mcpp");

  char *out = NULL;
  size_t out_len = 0;
  char *err = NULL;
  const char *name = (input_name && *input_name) ? input_name : "<input>";

  int ok = preprocess_source(
      input,
      strlen(input),
      &out,
      &out_len,
      &cfg,
      name,
      &g_map,
      &err);

  preprocess_config_free(&cfg);

  if (!ok) {
    g_error = err ? err : dup_cstr("preprocess failed");
    return 0;
  }

  (void)out_len;
  g_output = out;
  return 1;
}

const char *ps_mcpp_output(void) {
  return g_output ? g_output : "";
}

const char *ps_mcpp_error(void) {
  return g_error ? g_error : "";
}

int ps_mcpp_map_len(void) {
  return (int)g_map.len;
}

int ps_mcpp_map_line(int index) {
  if (index < 0 || (size_t)index >= g_map.len) return 0;
  return g_map.lines[index];
}

const char *ps_mcpp_map_file(int index) {
  if (index < 0 || (size_t)index >= g_map.len) return "";
  return g_map.files[index] ? g_map.files[index] : "";
}
