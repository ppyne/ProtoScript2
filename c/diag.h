#ifndef PS_DIAG_H
#define PS_DIAG_H

#include <stddef.h>

typedef enum {
  PS_DIAG_TEMPLATE_PARSE_UNEXPECTED = 1,
  PS_DIAG_TEMPLATE_RUNTIME = 2
} PsDiagTemplate;

void ps_diag_format(char *out, size_t out_sz, PsDiagTemplate tpl, const char *short_msg, const char *got, const char *expected);
void ps_diag_normalize_loc(int *line, int *col);
const char *ps_diag_display_file(const char *file, char *buf, size_t buf_sz);

#endif
