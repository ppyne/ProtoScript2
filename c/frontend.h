#ifndef PS_FRONTEND_H
#define PS_FRONTEND_H

#include <stdio.h>

typedef struct {
  const char *file;
  int line;
  int col;
  const char *code;
  const char *category;
  char message[256];
} PsDiag;

int ps_parse_file_syntax(const char *file, PsDiag *out_diag);
int ps_parse_file_ast(const char *file, PsDiag *out_diag, FILE *out);
int ps_check_file_static(const char *file, PsDiag *out_diag);
int ps_emit_ir_json(const char *file, PsDiag *out_diag, FILE *out);

#endif
