#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "diag.h"

void ps_diag_format(char *out, size_t out_sz, PsDiagTemplate tpl, const char *short_msg, const char *got, const char *expected) {
  if (!out || out_sz == 0) return;
  if (tpl == PS_DIAG_TEMPLATE_PARSE_UNEXPECTED) {
    const char *g = (got && got[0]) ? got : "token";
    if (expected && expected[0]) {
      snprintf(out, out_sz, "unexpected token %s, expecting '%s'", g, expected);
    } else {
      snprintf(out, out_sz, "unexpected token %s", g);
    }
    return;
  }

  const char *s = (short_msg && short_msg[0]) ? short_msg : "runtime error";
  if (got && got[0] && expected && expected[0]) {
    snprintf(out, out_sz, "%s. got %s; expected %s", s, got, expected);
  } else if (got && got[0]) {
    snprintf(out, out_sz, "%s. got %s", s, got);
  } else if (expected && expected[0]) {
    snprintf(out, out_sz, "%s. expected %s", s, expected);
  } else {
    snprintf(out, out_sz, "%s", s);
  }
}

void ps_diag_normalize_loc(int *line, int *col) {
  if (line && *line < 1) *line = 1;
  if (col && *col < 1) *col = 1;
}

static int match_manual_ex(const char *name, char *digits) {
  const char *prefix = "manual_ex";
  size_t plen = strlen(prefix);
  if (strncmp(name, prefix, plen) != 0) return 0;
  const char *p = name + plen;
  for (int i = 0; i < 3; i++) {
    if (!isdigit((unsigned char)p[i])) return 0;
    digits[i] = p[i];
  }
  digits[3] = '\0';
  return 1;
}

const char *ps_diag_display_file(const char *file, char *buf, size_t buf_sz) {
  if (!file || !*file || !buf || buf_sz == 0) return file;
  const char *base = file;
  for (const char *p = file; *p; p++) {
    if (*p == '/' || *p == '\\') base = p + 1;
  }
  char digits[4];
  if (match_manual_ex(base, digits)) {
    snprintf(buf, buf_sz, "EX-%s.pts", digits);
    return buf;
  }
  return file;
}
