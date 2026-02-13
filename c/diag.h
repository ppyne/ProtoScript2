#ifndef PS_DIAG_H
#define PS_DIAG_H

#include <stddef.h>
#include <stdio.h>

#include "frontend.h"

typedef enum {
  PS_DIAG_TEMPLATE_PARSE_UNEXPECTED = 1,
  PS_DIAG_TEMPLATE_RUNTIME = 2
} PsDiagTemplate;

void ps_diag_format(char *out, size_t out_sz, PsDiagTemplate tpl, const char *short_msg, const char *got, const char *expected);
void ps_diag_normalize_loc(int *line, int *col);
const char *ps_diag_display_file(const char *file, char *buf, size_t buf_sz);
void ps_diag_write(FILE *out, const char *fallback_file, const PsDiag *d);
int ps_diag_pick_suggestions(const char *query, const char **candidates, int candidate_count, char out[][64], int out_cap);

#endif
