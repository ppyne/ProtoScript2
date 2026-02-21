#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

void ps_diag_write(FILE *out, const char *fallback_file, const PsDiag *d) {
  if (!out) return;
  int count = (d && d->count > 0) ? d->count : 0;
  if (count > PS_DIAG_MAX_ITEMS) count = PS_DIAG_MAX_ITEMS;
  if (count == 0) {
    char mapped[128];
    const char *raw = (d && d->file) ? d->file : fallback_file;
    const char *file = ps_diag_display_file(raw, mapped, sizeof(mapped));
    int line = (d && d->line > 0) ? d->line : 1;
    int col = (d && d->col > 0) ? d->col : 1;
    const char *code = (d && d->code) ? d->code : NULL;
    const char *name = (d && d->name) ? d->name : (d && d->category ? d->category : NULL);
    const char *msg = (d && d->message[0]) ? d->message : "unknown error";
    if (code && code[0] && name && name[0]) {
      fprintf(out, "%s:%d:%d %s %s: %s\n", file ? file : "<unknown>", line, col, code, name, msg);
    } else if (name && name[0]) {
      fprintf(out, "%s:%d:%d %s: %s\n", file ? file : "<unknown>", line, col, name, msg);
    } else if (code && code[0]) {
      fprintf(out, "%s:%d:%d %s: %s\n", file ? file : "<unknown>", line, col, code, msg);
    } else {
      fprintf(out, "%s:%d:%d Error: %s\n", file ? file : "<unknown>", line, col, msg);
    }
    if (d && d->suggestion_count == 1 && d->suggestions[0][0]) {
      fprintf(out, "Did you mean '%s'?\n", d->suggestions[0]);
    } else if (d && d->suggestion_count >= 2 && d->suggestions[0][0] && d->suggestions[1][0]) {
      fprintf(out, "Did you mean '%s' or '%s'?\n", d->suggestions[0], d->suggestions[1]);
    }
    return;
  }

  for (int i = 0; i < count; i++) {
    const PsDiagItem *it = &d->items[i];
    char mapped[128];
    const char *raw = it->file ? it->file : fallback_file;
    const char *file = ps_diag_display_file(raw, mapped, sizeof(mapped));
    int line = it->line > 0 ? it->line : 1;
    int col = it->col > 0 ? it->col : 1;
    const char *code = it->code;
    const char *name = it->name ? it->name : it->category;
    const char *msg = it->message[0] ? it->message : "unknown error";
    if (code && code[0] && name && name[0]) {
      fprintf(out, "%s:%d:%d %s %s: %s\n", file ? file : "<unknown>", line, col, code, name, msg);
    } else if (name && name[0]) {
      fprintf(out, "%s:%d:%d %s: %s\n", file ? file : "<unknown>", line, col, name, msg);
    } else if (code && code[0]) {
      fprintf(out, "%s:%d:%d %s: %s\n", file ? file : "<unknown>", line, col, code, msg);
    } else {
      fprintf(out, "%s:%d:%d Error: %s\n", file ? file : "<unknown>", line, col, msg);
    }
    if (it->suggestion_count == 1 && it->suggestions[0][0]) {
      fprintf(out, "Did you mean '%s'?\n", it->suggestions[0]);
    } else if (it->suggestion_count >= 2 && it->suggestions[0][0] && it->suggestions[1][0]) {
      fprintf(out, "Did you mean '%s' or '%s'?\n", it->suggestions[0], it->suggestions[1]);
    }
  }
}

static int levenshtein(const char *a, const char *b) {
  size_t n = a ? strlen(a) : 0;
  size_t m = b ? strlen(b) : 0;
  if (n == 0) return (int)m;
  if (m == 0) return (int)n;
  int *dp = (int *)malloc((m + 1) * sizeof(int));
  if (!dp) return 9999;
  for (size_t j = 0; j <= m; j++) dp[j] = (int)j;
  for (size_t i = 1; i <= n; i++) {
    int prev = dp[0];
    dp[0] = (int)i;
    for (size_t j = 1; j <= m; j++) {
      int tmp = dp[j];
      int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
      int del = dp[j] + 1;
      int ins = dp[j - 1] + 1;
      int sub = prev + cost;
      int best = del < ins ? del : ins;
      if (sub < best) best = sub;
      dp[j] = best;
      prev = tmp;
    }
  }
  int out = dp[m];
  free(dp);
  return out;
}

typedef struct {
  const char *name;
  int dist;
} CandScore;

static int cand_cmp(const void *lhs, const void *rhs) {
  const CandScore *a = (const CandScore *)lhs;
  const CandScore *b = (const CandScore *)rhs;
  if (a->dist != b->dist) return a->dist - b->dist;
  return strcmp(a->name ? a->name : "", b->name ? b->name : "");
}

int ps_diag_pick_suggestions(const char *query, const char **candidates, int candidate_count, char out[][64], int out_cap) {
  if (!query || !query[0] || !candidates || candidate_count <= 0 || !out || out_cap <= 0) return 0;
  CandScore *scored = (CandScore *)calloc((size_t)candidate_count, sizeof(CandScore));
  if (!scored) return 0;
  int len = 0;
  for (int i = 0; i < candidate_count; i++) {
    const char *c = candidates[i];
    if (!c || !c[0] || strcmp(c, query) == 0) continue;
    int dist = levenshtein(query, c);
    if (dist > 2) continue;
    int dup = 0;
    for (int j = 0; j < len; j++) {
      if (strcmp(scored[j].name, c) == 0) {
        dup = 1;
        break;
      }
    }
    if (dup) continue;
    scored[len].name = c;
    scored[len].dist = dist;
    len++;
  }
  if (len == 0) {
    free(scored);
    return 0;
  }
  qsort(scored, (size_t)len, sizeof(CandScore), cand_cmp);
  int best_dist = scored[0].dist;
  int best_count = 0;
  for (int i = 0; i < len; i++) {
    if (scored[i].dist != best_dist) break;
    best_count++;
  }
  if (best_count > 2) {
    free(scored);
    return 0;
  }
  int nout = best_count < out_cap ? best_count : out_cap;
  for (int i = 0; i < nout; i++) {
    snprintf(out[i], 64, "%s", scored[i].name ? scored[i].name : "");
  }
  free(scored);
  return nout;
}
