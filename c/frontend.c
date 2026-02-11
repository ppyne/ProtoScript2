#include "frontend.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "runtime/ps_json.h"
#include "preprocess.h"

typedef struct AstNode AstNode;

static char *str_printf(const char *fmt, ...);
static int parse_file_internal(const char *file, PsDiag *out_diag, AstNode **out_root);

static char g_registry_exe_dir[PATH_MAX];
static int g_registry_exe_dir_set = 0;
static PreprocessConfig g_preprocess_config;
static int g_preprocess_config_loaded = 0;

typedef struct PreprocessMapEntry {
  char *file;
  PreprocessLineMap map;
  struct PreprocessMapEntry *next;
} PreprocessMapEntry;

static PreprocessMapEntry *g_preprocess_maps = NULL;

static const PreprocessLineMap *preprocess_map_lookup(const char *file) {
  if (!file) return NULL;
  for (PreprocessMapEntry *e = g_preprocess_maps; e; e = e->next) {
    if (e->file && strcmp(e->file, file) == 0) return &e->map;
  }
  return NULL;
}

static void preprocess_map_clear(const char *file) {
  if (!file) return;
  PreprocessMapEntry *prev = NULL;
  PreprocessMapEntry *cur = g_preprocess_maps;
  while (cur) {
    if (cur->file && strcmp(cur->file, file) == 0) {
      if (prev) prev->next = cur->next;
      else g_preprocess_maps = cur->next;
      preprocess_line_map_free(&cur->map);
      free(cur->file);
      free(cur);
      return;
    }
    prev = cur;
    cur = cur->next;
  }
}

static void preprocess_map_store(const char *file, PreprocessLineMap *map) {
  if (!file || !map) return;
  preprocess_map_clear(file);
  PreprocessMapEntry *e = (PreprocessMapEntry *)calloc(1, sizeof(PreprocessMapEntry));
  if (!e) {
    preprocess_line_map_free(map);
    return;
  }
  e->file = strdup(file);
  if (!e->file) {
    free(e);
    preprocess_line_map_free(map);
    return;
  }
  e->map = *map;
  map->len = 0;
  map->cap = 0;
  map->files = NULL;
  map->lines = NULL;
  map->owned_files = NULL;
  map->owned_len = 0;
  map->owned_cap = 0;
  e->next = g_preprocess_maps;
  g_preprocess_maps = e;
}

void ps_set_registry_exe_dir(const char *dir) {
  if (!dir || !dir[0]) return;
  size_t n = strlen(dir);
  if (n >= sizeof(g_registry_exe_dir)) n = sizeof(g_registry_exe_dir) - 1;
  memcpy(g_registry_exe_dir, dir, n);
  g_registry_exe_dir[n] = '\0';
  g_registry_exe_dir_set = 1;
}

typedef enum {
  TK_EOF,
  TK_KW,
  TK_ID,
  TK_NUM,
  TK_STR,
  TK_SYM,
} TokenKind;

typedef struct {
  TokenKind kind;
  char *text;
  int line;
  int col;
} Token;

typedef struct {
  Token *items;
  size_t len;
  size_t cap;
} TokenVec;

typedef struct AstNode {
  char *kind;
  char *text;
  int line;
  int col;
  struct AstNode **children;
  size_t child_len;
  size_t child_cap;
} AstNode;

typedef struct {
  const char *file;
  const char *src;
  size_t n;
  size_t i;
  int line;
  int col;
  TokenVec toks;
  PsDiag *diag;
} Lexer;

typedef struct {
  const char *file;
  TokenVec *toks;
  size_t i;
  PsDiag *diag;
  AstNode *ast_root;
  AstNode *ast_stack[256];
  size_t ast_sp;
} Parser;

static char *dup_range(const char *s, size_t a, size_t b) {
  size_t n = (b > a) ? (b - a) : 0;
  char *out = (char *)malloc(n + 1);
  if (!out) return NULL;
  if (n > 0) memcpy(out, s + a, n);
  out[n] = '\0';
  return out;
}

static void token_vec_free(TokenVec *v) {
  for (size_t i = 0; i < v->len; i++) free(v->items[i].text);
  free(v->items);
  v->items = NULL;
  v->len = 0;
  v->cap = 0;
}

static int token_vec_push(TokenVec *v, Token t) {
  if (v->len == v->cap) {
    size_t nc = (v->cap == 0) ? 128 : (v->cap * 2);
    Token *ni = (Token *)realloc(v->items, nc * sizeof(Token));
    if (!ni) return 0;
    v->items = ni;
    v->cap = nc;
  }
  v->items[v->len++] = t;
  return 1;
}

static AstNode *ast_new(const char *kind, const char *text, int line, int col) {
  AstNode *n = (AstNode *)calloc(1, sizeof(AstNode));
  if (!n) return NULL;
  n->kind = strdup(kind ? kind : "Unknown");
  n->text = text ? strdup(text) : NULL;
  n->line = line;
  n->col = col;
  if (!n->kind || (text && !n->text)) {
    free(n->kind);
    free(n->text);
    free(n);
    return NULL;
  }
  return n;
}

static int ast_add_child(AstNode *parent, AstNode *child) {
  if (!parent || !child) return 0;
  if (parent->child_len == parent->child_cap) {
    size_t nc = (parent->child_cap == 0) ? 8 : (parent->child_cap * 2);
    AstNode **ni = (AstNode **)realloc(parent->children, nc * sizeof(AstNode *));
    if (!ni) return 0;
    parent->children = ni;
    parent->child_cap = nc;
  }
  parent->children[parent->child_len++] = child;
  return 1;
}

static void ast_free(AstNode *n) {
  if (!n) return;
  for (size_t i = 0; i < n->child_len; i++) ast_free(n->children[i]);
  free(n->children);
  free(n->kind);
  free(n->text);
  free(n);
}

static void json_print_escaped(FILE *out, const char *s) {
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    if (*p == '\\' || *p == '"') {
      fputc('\\', out);
      fputc((int)*p, out);
    } else if (*p == '\n') {
      fputs("\\n", out);
    } else if (*p == '\r') {
      fputs("\\r", out);
    } else if (*p == '\t') {
      fputs("\\t", out);
    } else if (*p == '\b') {
      fputs("\\b", out);
    } else if (*p == '\f') {
      fputs("\\f", out);
    } else if (*p < 0x20) {
      fprintf(out, "\\u%04x", (unsigned int)*p);
    } else {
      fputc((int)*p, out);
    }
  }
}

static void ast_print_json(FILE *out, AstNode *n, int indent) {
  if (!n) return;
  for (int i = 0; i < indent; i++) fputs("  ", out);
  fputs("{\"kind\":\"", out);
  json_print_escaped(out, n->kind);
  fputc('"', out);
  if (n->text) {
    fputs(",\"text\":\"", out);
    json_print_escaped(out, n->text);
    fputc('"', out);
  }
  fprintf(out, ",\"line\":%d,\"col\":%d", n->line, n->col);
  if (n->child_len == 0) {
    fputs("}", out);
    return;
  }
  fputs(",\"children\":[\n", out);
  for (size_t i = 0; i < n->child_len; i++) {
    ast_print_json(out, n->children[i], indent + 1);
    if (i + 1 < n->child_len) fputs(",\n", out);
  }
  fputc('\n', out);
  for (int i = 0; i < indent; i++) fputs("  ", out);
  fputs("]}", out);
}

static AstNode *p_ast_parent(Parser *p) {
  if (!p || p->ast_sp == 0) return NULL;
  return p->ast_stack[p->ast_sp - 1];
}

static int p_ast_push(Parser *p, AstNode *n) {
  if (!p || !n || p->ast_sp >= 256) return 0;
  p->ast_stack[p->ast_sp++] = n;
  return 1;
}

static void p_ast_pop(Parser *p) {
  if (p && p->ast_sp > 0) p->ast_sp--;
}

static int p_ast_add(Parser *p, const char *kind, const char *text, int line, int col, AstNode **out_node) {
  AstNode *n = ast_new(kind, text, line, col);
  if (!n) return 0;
  AstNode *parent = p_ast_parent(p);
  if (parent) {
    if (!ast_add_child(parent, n)) {
      ast_free(n);
      return 0;
    }
  } else {
    p->ast_root = n;
  }
  if (out_node) *out_node = n;
  return 1;
}

static char *token_span_text(Parser *p, size_t start, size_t end) {
  if (!p || !p->toks || end <= start || end > p->toks->len) return strdup("");
  size_t cap = 64;
  size_t len = 0;
  char *out = (char *)malloc(cap);
  if (!out) return NULL;
  out[0] = '\0';
  for (size_t i = start; i < end; i++) {
    const char *t = p->toks->items[i].text;
    size_t tl = strlen(t);
    size_t need = len + tl + 2;
    if (need > cap) {
      while (cap < need) cap *= 2;
      char *ni = (char *)realloc(out, cap);
      if (!ni) {
        free(out);
        return NULL;
      }
      out = ni;
    }
    if (len > 0) out[len++] = ' ';
    memcpy(out + len, t, tl);
    len += tl;
    out[len] = '\0';
  }
  return out;
}

static int is_keyword(const char *s) {
  static const char *kws[] = {
      "prototype", "function", "var",      "int",    "float",   "bool",   "byte",  "glyph",
      "string",    "list",     "map",      "slice",  "view",    "void",   "if",    "else",
      "for",       "of",       "in",       "while",  "do",      "switch", "case",  "default",
      "break",     "continue", "return",   "try",    "catch",   "finally","throw", "true",
      "false",     "self",     "import",   "as",     NULL,
  };
  for (int i = 0; kws[i]; i++) {
    if (strcmp(s, kws[i]) == 0) return 1;
  }
  return 0;
}

static void append_trunc(char *dst, size_t dst_sz, const char *src, size_t max_len) {
  if (!dst || dst_sz == 0) return;
  size_t i = 0;
  if (!src) src = "";
  while (src[i] && i + 1 < dst_sz && i < max_len) {
    dst[i] = src[i];
    i++;
  }
  if (src[i] && i + 4 < dst_sz) {
    dst[i++] = '.';
    dst[i++] = '.';
    dst[i++] = '.';
  }
  dst[i] = '\0';
}

static void set_diag(PsDiag *d, const char *file, int line, int col, const char *code, const char *category, const char *message);

static void format_token_desc(Token *t, char *out, size_t out_sz) {
  if (!out || out_sz == 0) return;
  if (!t) {
    snprintf(out, out_sz, "token");
    return;
  }
  char val[64];
  append_trunc(val, sizeof(val), t->text ? t->text : "", 48);
  switch (t->kind) {
    case TK_ID: snprintf(out, out_sz, "identifier '%s'", val); break;
    case TK_KW: snprintf(out, out_sz, "keyword '%s'", val); break;
    case TK_NUM: snprintf(out, out_sz, "number '%s'", val); break;
    case TK_STR: snprintf(out, out_sz, "string \"%s\"", val); break;
    case TK_SYM: snprintf(out, out_sz, "symbol '%s'", val); break;
    case TK_EOF: snprintf(out, out_sz, "end of file"); break;
    default: snprintf(out, out_sz, "token '%s'", val); break;
  }
}

static void format_expected(TokenKind kind, const char *text, char *out, size_t out_sz) {
  if (!out || out_sz == 0) return;
  if (text) {
    if (kind == TK_KW) snprintf(out, out_sz, "keyword '%s'", text);
    else if (kind == TK_SYM) snprintf(out, out_sz, "symbol '%s'", text);
    else if (kind == TK_ID) snprintf(out, out_sz, "identifier '%s'", text);
    else snprintf(out, out_sz, "token '%s'", text);
    return;
  }
  switch (kind) {
    case TK_ID: snprintf(out, out_sz, "identifier"); break;
    case TK_KW: snprintf(out, out_sz, "keyword"); break;
    case TK_NUM: snprintf(out, out_sz, "number"); break;
    case TK_STR: snprintf(out, out_sz, "string"); break;
    case TK_SYM: snprintf(out, out_sz, "symbol"); break;
    case TK_EOF: snprintf(out, out_sz, "end of file"); break;
    default: snprintf(out, out_sz, "token"); break;
  }
}

static void parse_unexpected(Parser *p, Token *t, const char *expected) {
  char got[96];
  format_token_desc(t, got, sizeof(got));
  char msg[192];
  if (expected && expected[0]) snprintf(msg, sizeof(msg), "unexpected %s; expected %s", got, expected);
  else snprintf(msg, sizeof(msg), "unexpected %s", got);
  set_diag(p->diag, p->file, t ? t->line : 1, t ? t->col : 1, "E1001", "PARSE_UNEXPECTED_TOKEN", msg);
}

static void lex_unexpected(Lexer *l, int line, int col, const char *got, const char *expected) {
  char msg[192];
  if (expected && expected[0]) snprintf(msg, sizeof(msg), "unexpected %s; expected %s", got, expected);
  else snprintf(msg, sizeof(msg), "unexpected %s", got);
  set_diag(l->diag, l->file, line, col, "E1001", "PARSE_UNEXPECTED_TOKEN", msg);
}

static int l_eof(Lexer *l) { return l->i >= l->n; }
static char l_ch(Lexer *l, size_t off) { return (l->i + off < l->n) ? l->src[l->i + off] : '\0'; }

static char l_advance(Lexer *l) {
  char c = l_ch(l, 0);
  l->i++;
  if (c == '\n') {
    l->line++;
    l->col = 1;
  } else {
    l->col++;
  }
  return c;
}

static void set_diag(PsDiag *d, const char *file, int line, int col, const char *code, const char *category, const char *message) {
  const PreprocessLineMap *map = preprocess_map_lookup(file);
  if (map && line > 0 && (size_t)line <= map->len) {
    const char *mf = map->files[line - 1];
    int ml = map->lines[line - 1];
    if (mf && *mf) file = mf;
    if (ml > 0) line = ml;
  }
  d->file = file;
  d->line = line;
  d->col = col;
  d->code = code;
  d->category = category;
  snprintf(d->message, sizeof(d->message), "%s", message);
}

static int lex_add(Lexer *l, TokenKind kind, const char *text, int line, int col) {
  Token t;
  t.kind = kind;
  t.text = strdup(text);
  t.line = line;
  t.col = col;
  if (!t.text) return 0;
  if (!token_vec_push(&l->toks, t)) {
    free(t.text);
    return 0;
  }
  return 1;
}

static int is_two_sym(const char *s) {
  static const char *two[] = {"==", "!=", "<=", ">=", "&&", "||", "<<", ">>", "++", "--", "+=", "-=", "*=", "/=", NULL};
  for (int i = 0; two[i]; i++) if (strcmp(two[i], s) == 0) return 1;
  return 0;
}

static int hex_val(char c, uint32_t *out) {
  if (c >= '0' && c <= '9') {
    *out = (uint32_t)(c - '0');
    return 1;
  }
  if (c >= 'a' && c <= 'f') {
    *out = 10u + (uint32_t)(c - 'a');
    return 1;
  }
  if (c >= 'A' && c <= 'F') {
    *out = 10u + (uint32_t)(c - 'A');
    return 1;
  }
  return 0;
}

static int buf_push(char **buf, size_t *len, size_t *cap, char c) {
  if (*len + 1 >= *cap) {
    size_t nc = (*cap == 0) ? 64 : (*cap * 2);
    while (nc < *len + 2) nc *= 2;
    char *nb = (char *)realloc(*buf, nc);
    if (!nb) return 0;
    *buf = nb;
    *cap = nc;
  }
  (*buf)[(*len)++] = c;
  return 1;
}

static int buf_push_utf8(char **buf, size_t *len, size_t *cap, uint32_t cp) {
  if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
  if (cp <= 0x7F) {
    return buf_push(buf, len, cap, (char)cp);
  }
  if (cp <= 0x7FF) {
    return buf_push(buf, len, cap, (char)(0xC0 | (cp >> 6))) &&
           buf_push(buf, len, cap, (char)(0x80 | (cp & 0x3F)));
  }
  if (cp <= 0xFFFF) {
    return buf_push(buf, len, cap, (char)(0xE0 | (cp >> 12))) &&
           buf_push(buf, len, cap, (char)(0x80 | ((cp >> 6) & 0x3F))) &&
           buf_push(buf, len, cap, (char)(0x80 | (cp & 0x3F)));
  }
  return buf_push(buf, len, cap, (char)(0xF0 | (cp >> 18))) &&
         buf_push(buf, len, cap, (char)(0x80 | ((cp >> 12) & 0x3F))) &&
         buf_push(buf, len, cap, (char)(0x80 | ((cp >> 6) & 0x3F))) &&
         buf_push(buf, len, cap, (char)(0x80 | (cp & 0x3F)));
}

static int is_one_sym(char c) {
  const char *s = "{}()[];,:.?+-*/%&|^~!=<>";
  return strchr(s, c) != NULL;
}

static int run_lexer(Lexer *l) {
  while (!l_eof(l)) {
    char c = l_ch(l, 0);
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
      l_advance(l);
      continue;
    }
    if (c == '/' && l_ch(l, 1) == '/') {
      while (!l_eof(l) && l_ch(l, 0) != '\n') l_advance(l);
      continue;
    }
    if (c == '/' && l_ch(l, 1) == '*') {
      int sl = l->line, sc = l->col;
      l_advance(l);
      l_advance(l);
      int closed = 0;
      while (!l_eof(l)) {
        if (l_ch(l, 0) == '*' && l_ch(l, 1) == '/') {
          l_advance(l);
          l_advance(l);
          closed = 1;
          break;
        }
        l_advance(l);
      }
      if (!closed) {
        set_diag(l->diag, l->file, sl, sc, "E1002", "PARSE_UNCLOSED_BLOCK",
                 "unexpected end of file; expected '*/' to close block comment");
        return 0;
      }
      continue;
    }

    int line = l->line, col = l->col;
    if (isalpha((unsigned char)c) || c == '_') {
      size_t a = l->i;
      while (isalnum((unsigned char)l_ch(l, 0)) || l_ch(l, 0) == '_') l_advance(l);
      char *s = dup_range(l->src, a, l->i);
      if (!s) return 0;
      int ok = lex_add(l, is_keyword(s) ? TK_KW : TK_ID, s, line, col);
      free(s);
      if (!ok) return 0;
      continue;
    }

    if (c == '.' && isdigit((unsigned char)l_ch(l, 1))) {
      size_t a = l->i;
      l_advance(l);
      while (isdigit((unsigned char)l_ch(l, 0))) l_advance(l);
      if (l_ch(l, 0) == 'e' || l_ch(l, 0) == 'E') {
        l_advance(l);
        if (l_ch(l, 0) == '+' || l_ch(l, 0) == '-') l_advance(l);
        while (isdigit((unsigned char)l_ch(l, 0))) l_advance(l);
      }
      char *s = dup_range(l->src, a, l->i);
      if (!s) return 0;
      int ok = lex_add(l, TK_NUM, s, line, col);
      free(s);
      if (!ok) return 0;
      continue;
    }

    if (isdigit((unsigned char)c)) {
      size_t a = l->i;
      if (c == '0' && (l_ch(l, 1) == 'x' || l_ch(l, 1) == 'X')) {
        l_advance(l);
        l_advance(l);
        while (isxdigit((unsigned char)l_ch(l, 0))) l_advance(l);
      } else if (c == '0' && (l_ch(l, 1) == 'b' || l_ch(l, 1) == 'B')) {
        l_advance(l);
        l_advance(l);
        while (l_ch(l, 0) == '0' || l_ch(l, 0) == '1') l_advance(l);
      } else {
        while (isdigit((unsigned char)l_ch(l, 0))) l_advance(l);
        if (l_ch(l, 0) == '.') {
          l_advance(l);
          while (isdigit((unsigned char)l_ch(l, 0))) l_advance(l);
        }
        if (l_ch(l, 0) == 'e' || l_ch(l, 0) == 'E') {
          l_advance(l);
          if (l_ch(l, 0) == '+' || l_ch(l, 0) == '-') l_advance(l);
          while (isdigit((unsigned char)l_ch(l, 0))) l_advance(l);
        }
      }
      char *s = dup_range(l->src, a, l->i);
      if (!s) return 0;
      int ok = lex_add(l, TK_NUM, s, line, col);
      free(s);
      if (!ok) return 0;
      continue;
    }

    if (c == '"') {
      l_advance(l);
      char *out = NULL;
      size_t out_len = 0;
      size_t out_cap = 0;
      int ok_close = 0;
      while (!l_eof(l)) {
        char ch = l_ch(l, 0);
        if (ch == '"') {
          ok_close = 1;
          l_advance(l);
          break;
        }
        if (ch == '\\') {
          l_advance(l);
          if (l_eof(l)) {
            lex_unexpected(l, line, col, "end of file in escape sequence", "valid escape");
            free(out);
            return 0;
          }
          char esc = l_ch(l, 0);
          l_advance(l);
          if (esc == '"') {
            if (!buf_push(&out, &out_len, &out_cap, '"')) return 0;
          } else if (esc == '\\') {
            if (!buf_push(&out, &out_len, &out_cap, '\\')) return 0;
          } else if (esc == 'n') {
            if (!buf_push(&out, &out_len, &out_cap, '\n')) return 0;
          } else if (esc == 't') {
            if (!buf_push(&out, &out_len, &out_cap, '\t')) return 0;
          } else if (esc == 'r') {
            if (!buf_push(&out, &out_len, &out_cap, '\r')) return 0;
          } else if (esc == 'b') {
            if (!buf_push(&out, &out_len, &out_cap, '\b')) return 0;
          } else if (esc == 'f') {
            if (!buf_push(&out, &out_len, &out_cap, '\f')) return 0;
          } else if (esc == 'u') {
            uint32_t v = 0;
            for (int i = 0; i < 4; i += 1) {
              if (l_eof(l)) {
                lex_unexpected(l, line, col, "end of file in escape sequence", "4 hex digits");
                free(out);
                return 0;
              }
              uint32_t h = 0;
              if (!hex_val(l_ch(l, 0), &h)) {
                char got[32];
                snprintf(got, sizeof(got), "escape '\\u%c'", l_ch(l, 0));
                lex_unexpected(l, line, col, got, "4 hex digits");
                free(out);
                return 0;
              }
              v = (v << 4) | h;
              l_advance(l);
            }
            if (!buf_push_utf8(&out, &out_len, &out_cap, v)) {
              lex_unexpected(l, line, col, "escape '\\u'", "valid unicode scalar");
              free(out);
              return 0;
            }
          } else {
            char got[24];
            snprintf(got, sizeof(got), "escape '\\%c'", esc);
            lex_unexpected(l, line, col, got, "valid escape (\\\\, \\\", \\n, \\t, \\r, \\b, \\f, \\uXXXX)");
            free(out);
            return 0;
          }
          continue;
        }
        if (!buf_push(&out, &out_len, &out_cap, ch)) return 0;
        l_advance(l);
      }
      if (!ok_close) {
        set_diag(l->diag, l->file, line, col, "E1002", "PARSE_UNCLOSED_BLOCK",
                 "unexpected end of file; expected '\"' to close string literal");
        free(out);
        return 0;
      }
      if (!buf_push(&out, &out_len, &out_cap, '\0')) return 0;
      int ok = lex_add(l, TK_STR, out ? out : "", line, col);
      free(out);
      if (!ok) return 0;
      continue;
    }

    if (l_ch(l, 0) == '.' && l_ch(l, 1) == '.' && l_ch(l, 2) == '.') {
      l_advance(l); l_advance(l); l_advance(l);
      if (!lex_add(l, TK_SYM, "...", line, col)) return 0;
      continue;
    }

    char two[3] = {l_ch(l, 0), l_ch(l, 1), '\0'};
    if (is_two_sym(two)) {
      l_advance(l); l_advance(l);
      if (!lex_add(l, TK_SYM, two, line, col)) return 0;
      continue;
    }

    if (is_one_sym(c)) {
      char one[2] = {c, '\0'};
      l_advance(l);
      if (!lex_add(l, TK_SYM, one, line, col)) return 0;
      continue;
    }

    char got[32];
    snprintf(got, sizeof(got), "character '%c'", c);
    lex_unexpected(l, line, col, got, "token start");
    return 0;
  }

  if (!lex_add(l, TK_EOF, "eof", l->line, l->col)) return 0;
  return 1;
}

static Token *p_t(Parser *p, int off) {
  long idx = (long)p->i + (long)off;
  if (idx < 0) idx = 0;
  if ((size_t)idx >= p->toks->len) idx = (long)p->toks->len - 1;
  return &p->toks->items[(size_t)idx];
}

static int p_at(Parser *p, TokenKind kind, const char *text) {
  Token *t = p_t(p, 0);
  if (t->kind != kind) return 0;
  if (!text) return 1;
  return strcmp(t->text, text) == 0;
}

static int p_eat(Parser *p, TokenKind kind, const char *text) {
  if (!p_at(p, kind, text)) {
    Token *t = p_t(p, 0);
    char expected[96];
    format_expected(kind, text, expected, sizeof(expected));
    parse_unexpected(p, t, expected);
    return 0;
  }
  p->i++;
  return 1;
}

static int is_primitive_type_kw(const char *s) {
  return strcmp(s, "int") == 0 || strcmp(s, "float") == 0 || strcmp(s, "bool") == 0 || strcmp(s, "byte") == 0 ||
         strcmp(s, "glyph") == 0 || strcmp(s, "string") == 0 || strcmp(s, "void") == 0;
}

static int looks_like_type_start(Parser *p) {
  Token *t = p_t(p, 0);
  if (t->kind == TK_ID) return 1;
  if (t->kind == TK_KW &&
      (is_primitive_type_kw(t->text) || strcmp(t->text, "list") == 0 || strcmp(t->text, "map") == 0 ||
       strcmp(t->text, "slice") == 0 || strcmp(t->text, "view") == 0)) {
    return 1;
  }
  return 0;
}

static int parse_expr(Parser *p, AstNode **out);
static int parse_stmt(Parser *p);
static int parse_try_stmt(Parser *p);
static int parse_type(Parser *p);
static int parse_postfix_expr(Parser *p, AstNode **out);
static int parse_conditional_expr(Parser *p, AstNode **out);
static int parse_param(Parser *p, AstNode *fn_node);

static int parse_type(Parser *p) {
  Token *t = p_t(p, 0);
  if (t->kind != TK_KW && t->kind != TK_ID) {
    parse_unexpected(p, t, "type");
    return 0;
  }
  if (t->kind == TK_ID || is_primitive_type_kw(t->text)) {
    p->i++;
    return 1;
  }
  if (strcmp(t->text, "list") == 0 || strcmp(t->text, "slice") == 0 || strcmp(t->text, "view") == 0) {
    p->i++;
    return p_eat(p, TK_SYM, "<") && parse_type(p) && p_eat(p, TK_SYM, ">");
  }
  if (strcmp(t->text, "map") == 0) {
    p->i++;
    return p_eat(p, TK_SYM, "<") && parse_type(p) && p_eat(p, TK_SYM, ",") && parse_type(p) && p_eat(p, TK_SYM, ">");
  }
  parse_unexpected(p, t, "type");
  return 0;
}

static int parse_block(Parser *p) {
  Token *l = p_t(p, 0);
  if (!p_eat(p, TK_SYM, "{")) return 0;
  AstNode *block = NULL;
  if (!p_ast_add(p, "Block", NULL, l->line, l->col, &block)) return 0;
  if (!p_ast_push(p, block)) return 0;
  while (!p_at(p, TK_SYM, "}")) {
    if (p_at(p, TK_EOF, NULL)) {
      set_diag(p->diag, p->file, l->line, l->col, "E1002", "PARSE_UNCLOSED_BLOCK",
               "unexpected end of file; expected '}' to close block");
      p_ast_pop(p);
      return 0;
    }
    if (!parse_stmt(p)) {
      p_ast_pop(p);
      return 0;
    }
  }
  p_ast_pop(p);
  return p_eat(p, TK_SYM, "}");
}

static int find_until_kw(Parser *p, const char *a, const char *b, const char *stop) {
  int depth = 0;
  for (size_t j = p->i; j < p->toks->len; j++) {
    Token *t = &p->toks->items[j];
    if (t->kind == TK_SYM && strcmp(t->text, "(") == 0) depth++;
    else if (t->kind == TK_SYM && strcmp(t->text, ")") == 0) {
      if (depth == 0 && strcmp(stop, ")") == 0) return 0;
      depth--;
    }
    if (depth == 0 && t->kind == TK_KW && (strcmp(t->text, a) == 0 || strcmp(t->text, b) == 0)) return 1;
    if (depth == 0 && t->kind == TK_SYM && strcmp(t->text, stop) == 0) return 0;
  }
  return 0;
}

static int looks_like_assign_stmt(Parser *p) {
  size_t j = p->i;
  Token *t0 = p_t(p, 0);
  if (!(t0->kind == TK_ID || (t0->kind == TK_KW && strcmp(t0->text, "self") == 0))) return 0;
  j++;
  while (j < p->toks->len) {
    Token *t = &p->toks->items[j];
    if (t->kind == TK_SYM &&
        (strcmp(t->text, ".") == 0 || strcmp(t->text, "[") == 0 || strcmp(t->text, "(") == 0 ||
         strcmp(t->text, "++") == 0 || strcmp(t->text, "--") == 0)) {
      if (strcmp(t->text, "[") == 0 || strcmp(t->text, "(") == 0) {
        const char *open = t->text;
        const char *close = (strcmp(open, "[") == 0) ? "]" : ")";
        int depth = 1;
        j++;
        while (j < p->toks->len && depth > 0) {
          Token *u = &p->toks->items[j];
          if (u->kind == TK_SYM && strcmp(u->text, open) == 0) depth++;
          else if (u->kind == TK_SYM && strcmp(u->text, close) == 0) depth--;
          j++;
        }
      } else {
        j++;
        if (strcmp(t->text, ".") == 0) j++;
      }
      continue;
    }
    if (t->kind == TK_SYM &&
        (strcmp(t->text, "=") == 0 || strcmp(t->text, "+=") == 0 || strcmp(t->text, "-=") == 0 ||
         strcmp(t->text, "*=") == 0 || strcmp(t->text, "/=") == 0)) {
      return 1;
    }
    return 0;
  }
  return 0;
}

static int parse_var_decl(Parser *p, AstNode **out) {
  if (p_at(p, TK_KW, "var")) {
    Token *start = p_t(p, 0);
    if (!p_eat(p, TK_KW, "var")) return 0;
    Token *name = p_t(p, 0);
    if (!p_eat(p, TK_ID, NULL)) return 0;
    if (!p_eat(p, TK_SYM, "=")) return 0;
    AstNode *init = NULL;
    if (!parse_expr(p, &init)) return 0;
    AstNode *node = ast_new("VarDecl", name->text, start->line, start->col);
    if (!node || !ast_add_child(node, init)) {
      ast_free(node);
      ast_free(init);
      return 0;
    }
    if (out) *out = node;
    else ast_free(node);
    return 1;
  }
  size_t type_start = p->i;
  Token *type_tok = p_t(p, 0);
  if (!parse_type(p)) return 0;
  size_t type_end = p->i;
  Token *name = p_t(p, 0);
  if (!p_eat(p, TK_ID, NULL)) return 0;
  AstNode *node = ast_new("VarDecl", name->text, name->line, name->col);
  if (!node) return 0;
  char *type_txt = token_span_text(p, type_start, type_end);
  AstNode *tn = ast_new("Type", type_txt ? type_txt : "", type_tok->line, type_tok->col);
  free(type_txt);
  if (!tn || !ast_add_child(node, tn)) {
    ast_free(node);
    ast_free(tn);
    return 0;
  }
  if (p_at(p, TK_SYM, "=")) {
    if (!p_eat(p, TK_SYM, "=")) return 0;
    AstNode *init = NULL;
    if (!parse_expr(p, &init) || !ast_add_child(node, init)) {
      ast_free(init);
      ast_free(node);
      return 0;
    }
  }
  if (out) *out = node;
  else ast_free(node);
  return 1;
}

static int parse_switch_stmt(Parser *p) {
  Token *st = p_t(p, 0);
  AstNode *node = NULL;
  if (!p_ast_add(p, "SwitchStmt", NULL, st->line, st->col, &node)) return 0;
  if (!p_ast_push(p, node)) return 0;
  if (!p_eat(p, TK_KW, "switch")) return 0;
  if (!p_eat(p, TK_SYM, "(")) return 0;
  AstNode *switch_expr = NULL;
  if (!parse_expr(p, &switch_expr)) return 0;
  if (switch_expr && !ast_add_child(node, switch_expr)) {
    ast_free(switch_expr);
    return 0;
  }
  if (!p_eat(p, TK_SYM, ")")) return 0;
  if (!p_eat(p, TK_SYM, "{")) return 0;
  while (!p_at(p, TK_SYM, "}")) {
    if (p_at(p, TK_KW, "case")) {
      Token *case_kw = p_t(p, 0);
      p_eat(p, TK_KW, "case");
      AstNode *case_value = NULL;
      if (!parse_expr(p, &case_value)) return 0;
      if (!p_eat(p, TK_SYM, ":")) return 0;
      AstNode *case_node = NULL;
      if (!p_ast_add(p, "CaseClause", NULL, case_kw->line, case_kw->col, &case_node)) return 0;
      if (!p_ast_push(p, case_node)) return 0;
      if (case_value && !ast_add_child(case_node, case_value)) {
        ast_free(case_value);
        return 0;
      }
      while (!p_at(p, TK_KW, "case") && !p_at(p, TK_KW, "default") && !p_at(p, TK_SYM, "}")) {
        if (!parse_stmt(p)) return 0;
      }
      p_ast_pop(p);
      continue;
    }
    if (p_at(p, TK_KW, "default")) {
      Token *def_kw = p_t(p, 0);
      p_eat(p, TK_KW, "default");
      if (!p_eat(p, TK_SYM, ":")) return 0;
      AstNode *def_node = NULL;
      if (!p_ast_add(p, "DefaultClause", NULL, def_kw->line, def_kw->col, &def_node)) return 0;
      if (!p_ast_push(p, def_node)) return 0;
      while (!p_at(p, TK_KW, "case") && !p_at(p, TK_SYM, "}")) {
        if (!parse_stmt(p)) return 0;
      }
      p_ast_pop(p);
      continue;
    }
    Token *t = p_t(p, 0);
    parse_unexpected(p, t, "case/default clause");
    p_ast_pop(p);
    return 0;
  }
  p_ast_pop(p);
  return p_eat(p, TK_SYM, "}");
}

static int parse_if_stmt(Parser *p) {
  Token *t = p_t(p, 0);
  AstNode *node = NULL;
  if (!p_ast_add(p, "IfStmt", NULL, t->line, t->col, &node)) return 0;
  if (!p_ast_push(p, node)) return 0;
  if (!p_eat(p, TK_KW, "if")) {
    p_ast_pop(p);
    return 0;
  }
  if (!p_eat(p, TK_SYM, "(")) {
    p_ast_pop(p);
    return 0;
  }
  AstNode *cond = NULL;
  if (!parse_expr(p, &cond)) {
    p_ast_pop(p);
    return 0;
  }
  if (cond && !ast_add_child(node, cond)) {
    ast_free(cond);
    p_ast_pop(p);
    return 0;
  }
  if (!p_eat(p, TK_SYM, ")")) {
    p_ast_pop(p);
    return 0;
  }
  if (!parse_stmt(p)) {
    p_ast_pop(p);
    return 0;
  }
  if (p_at(p, TK_KW, "else")) {
    p_eat(p, TK_KW, "else");
    if (!parse_stmt(p)) {
      p_ast_pop(p);
      return 0;
    }
  }
  p_ast_pop(p);
  return 1;
}

static int parse_while_stmt(Parser *p) {
  Token *t = p_t(p, 0);
  AstNode *node = NULL;
  if (!p_ast_add(p, "WhileStmt", NULL, t->line, t->col, &node)) return 0;
  if (!p_ast_push(p, node)) return 0;
  if (!p_eat(p, TK_KW, "while")) {
    p_ast_pop(p);
    return 0;
  }
  if (!p_eat(p, TK_SYM, "(")) {
    p_ast_pop(p);
    return 0;
  }
  AstNode *cond = NULL;
  if (!parse_expr(p, &cond)) {
    p_ast_pop(p);
    return 0;
  }
  if (cond && !ast_add_child(node, cond)) {
    ast_free(cond);
    p_ast_pop(p);
    return 0;
  }
  if (!p_eat(p, TK_SYM, ")")) {
    p_ast_pop(p);
    return 0;
  }
  if (!parse_stmt(p)) {
    p_ast_pop(p);
    return 0;
  }
  p_ast_pop(p);
  return 1;
}

static int parse_do_while_stmt(Parser *p) {
  Token *t = p_t(p, 0);
  AstNode *node = NULL;
  if (!p_ast_add(p, "DoWhileStmt", NULL, t->line, t->col, &node)) return 0;
  if (!p_ast_push(p, node)) return 0;
  if (!p_eat(p, TK_KW, "do")) {
    p_ast_pop(p);
    return 0;
  }
  if (!parse_stmt(p)) {
    p_ast_pop(p);
    return 0;
  }
  if (!p_eat(p, TK_KW, "while")) {
    p_ast_pop(p);
    return 0;
  }
  if (!p_eat(p, TK_SYM, "(")) {
    p_ast_pop(p);
    return 0;
  }
  AstNode *cond = NULL;
  if (!parse_expr(p, &cond)) {
    p_ast_pop(p);
    return 0;
  }
  if (cond && !ast_add_child(node, cond)) {
    ast_free(cond);
    p_ast_pop(p);
    return 0;
  }
  if (node->child_len >= 2) {
    AstNode *tmp = node->children[0];
    node->children[0] = node->children[1];
    node->children[1] = tmp;
  }
  if (!p_eat(p, TK_SYM, ")")) {
    p_ast_pop(p);
    return 0;
  }
  if (!p_eat(p, TK_SYM, ";")) {
    p_ast_pop(p);
    return 0;
  }
  p_ast_pop(p);
  return 1;
}

static int parse_try_stmt(Parser *p) {
  Token *t = p_t(p, 0);
  AstNode *node = NULL;
  if (!p_ast_add(p, "TryStmt", NULL, t->line, t->col, &node)) return 0;
  if (!p_ast_push(p, node)) return 0;
  if (!p_eat(p, TK_KW, "try")) {
    p_ast_pop(p);
    return 0;
  }
  if (!parse_block(p)) {
    p_ast_pop(p);
    return 0;
  }
  int saw_clause = 0;
  while (p_at(p, TK_KW, "catch")) {
    if (!p_eat(p, TK_KW, "catch") || !p_eat(p, TK_SYM, "(")) {
      p_ast_pop(p);
      return 0;
    }
    size_t type_start = p->i;
    Token *type_tok = p_t(p, 0);
    if (!parse_type(p)) {
      p_ast_pop(p);
      return 0;
    }
    size_t type_end = p->i;
    Token *name = p_t(p, 0);
    if (!p_eat(p, TK_ID, NULL) || !p_eat(p, TK_SYM, ")")) {
      p_ast_pop(p);
      return 0;
    }
    AstNode *clause = NULL;
    if (!p_ast_add(p, "CatchClause", name->text, name->line, name->col, &clause)) {
      p_ast_pop(p);
      return 0;
    }
    if (!p_ast_push(p, clause)) {
      p_ast_pop(p);
      return 0;
    }
    char *type_txt = token_span_text(p, type_start, type_end);
    AstNode *tn = ast_new("Type", type_txt ? type_txt : "", type_tok->line, type_tok->col);
    free(type_txt);
    if (!tn || !ast_add_child(clause, tn)) {
      ast_free(tn);
      p_ast_pop(p);
      p_ast_pop(p);
      return 0;
    }
    if (!parse_block(p)) {
      p_ast_pop(p);
      p_ast_pop(p);
      return 0;
    }
    p_ast_pop(p);
    saw_clause = 1;
  }
  if (p_at(p, TK_KW, "finally")) {
    Token *ft = p_t(p, 0);
    if (!p_eat(p, TK_KW, "finally")) {
      p_ast_pop(p);
      return 0;
    }
    AstNode *fnode = NULL;
    if (!p_ast_add(p, "FinallyClause", NULL, ft->line, ft->col, &fnode)) {
      p_ast_pop(p);
      return 0;
    }
    if (!p_ast_push(p, fnode)) {
      p_ast_pop(p);
      return 0;
    }
    if (!parse_block(p)) {
      p_ast_pop(p);
      p_ast_pop(p);
      return 0;
    }
    p_ast_pop(p);
    saw_clause = 1;
  }
  if (!saw_clause) {
    Token *cur = p_t(p, 0);
    parse_unexpected(p, cur, "catch or finally clause");
    p_ast_pop(p);
    return 0;
  }
  p_ast_pop(p);
  return 1;
}

static int parse_for_stmt(Parser *p) {
  Token *st = p_t(p, 0);
  AstNode *node = NULL;
  if (!p_ast_add(p, "ForStmt", NULL, st->line, st->col, &node)) return 0;
  if (!p_ast_push(p, node)) return 0;
  if (!p_eat(p, TK_KW, "for")) return 0;
  if (!p_eat(p, TK_SYM, "(")) return 0;

  size_t mark = p->i;
  int is_iter = 0;
  if ((p_at(p, TK_KW, "var") || looks_like_type_start(p)) && find_until_kw(p, "of", "in", ")")) {
    AstNode *iter_var = NULL;
    if (p_at(p, TK_KW, "var")) {
      Token *v = p_t(p, 0);
      if (!p_eat(p, TK_KW, "var")) return 0;
      Token *id = p_t(p, 0);
      if (!p_eat(p, TK_ID, NULL)) return 0;
      iter_var = ast_new("IterVar", id->text, v->line, v->col);
    } else {
      size_t ts = p->i;
      Token *tt = p_t(p, 0);
      if (!parse_type(p)) return 0;
      size_t te = p->i;
      Token *id = p_t(p, 0);
      if (!p_eat(p, TK_ID, NULL)) return 0;
      iter_var = ast_new("IterVar", id->text, tt->line, tt->col);
      if (iter_var) {
        char *type_txt = token_span_text(p, ts, te);
        AstNode *tn = ast_new("Type", type_txt ? type_txt : "", tt->line, tt->col);
        free(type_txt);
        if (!tn || !ast_add_child(iter_var, tn)) {
          ast_free(iter_var);
          iter_var = NULL;
        }
      }
    }
    if (iter_var && !ast_add_child(node, iter_var)) {
      ast_free(iter_var);
      return 0;
    }
    AstNode *iter_expr = NULL;
    if (p_at(p, TK_KW, "of")) {
      Token *kw = p_t(p, 0);
      p_eat(p, TK_KW, "of");
      free(node->text);
      node->text = strdup(kw->text);
      if (!parse_expr(p, &iter_expr)) return 0;
      is_iter = 1;
    } else if (p_at(p, TK_KW, "in")) {
      Token *kw = p_t(p, 0);
      p_eat(p, TK_KW, "in");
      free(node->text);
      node->text = strdup(kw->text);
      if (!parse_expr(p, &iter_expr)) return 0;
      is_iter = 1;
    } else {
      ast_free(iter_expr);
      p->i = mark;
    }
    if (is_iter && iter_expr && !ast_add_child(node, iter_expr)) {
      ast_free(iter_expr);
      return 0;
    }
  }

  if (!is_iter) {
    if (!p_at(p, TK_SYM, ";")) {
      if (p_at(p, TK_KW, "var") || looks_like_type_start(p)) {
        AstNode *tmp_decl = NULL;
        if (!parse_var_decl(p, &tmp_decl)) return 0;
        if (tmp_decl && !ast_add_child(node, tmp_decl)) {
          ast_free(tmp_decl);
          return 0;
        }
      } else if (looks_like_assign_stmt(p)) {
        Token *t = p_t(p, 0);
        AstNode *target = NULL;
        if (!parse_postfix_expr(p, &target)) return 0;
        if (!p_eat(p, TK_SYM, NULL)) return 0;
        Token *op = &p->toks->items[p->i - 1];
        if (!(strcmp(op->text, "=") == 0 || strcmp(op->text, "+=") == 0 || strcmp(op->text, "-=") == 0 ||
              strcmp(op->text, "*=") == 0 || strcmp(op->text, "/=") == 0)) {
          parse_unexpected(p, op, "assignment operator (=, +=, -=, *=, /=)");
          ast_free(target);
          return 0;
        }
        AstNode *rhs = NULL;
        if (!parse_conditional_expr(p, &rhs)) {
          ast_free(target);
          return 0;
        }
        AstNode *assign = ast_new("AssignStmt", op->text, t->line, t->col);
        if (!assign || !ast_add_child(assign, target) || !ast_add_child(assign, rhs) || !ast_add_child(node, assign)) {
          ast_free(target);
          ast_free(rhs);
          ast_free(assign);
          return 0;
        }
      } else {
        AstNode *expr = NULL;
        if (!parse_expr(p, &expr)) return 0;
        if (expr && !ast_add_child(node, expr)) {
          ast_free(expr);
          return 0;
        }
      }
    }
    if (!p_eat(p, TK_SYM, ";")) return 0;
    if (!p_at(p, TK_SYM, ";")) {
      AstNode *cond = NULL;
      if (!parse_expr(p, &cond)) return 0;
      if (cond && !ast_add_child(node, cond)) {
        ast_free(cond);
        return 0;
      }
    }
    if (!p_eat(p, TK_SYM, ";")) return 0;
    if (!p_at(p, TK_SYM, ")")) {
      if (looks_like_assign_stmt(p)) {
        Token *t = p_t(p, 0);
        AstNode *target = NULL;
        if (!parse_postfix_expr(p, &target)) return 0;
        if (!p_eat(p, TK_SYM, NULL)) return 0;
        Token *op = &p->toks->items[p->i - 1];
        if (!(strcmp(op->text, "=") == 0 || strcmp(op->text, "+=") == 0 || strcmp(op->text, "-=") == 0 ||
              strcmp(op->text, "*=") == 0 || strcmp(op->text, "/=") == 0)) {
          parse_unexpected(p, op, "assignment operator (=, +=, -=, *=, /=)");
          ast_free(target);
          return 0;
        }
        AstNode *rhs = NULL;
        if (!parse_conditional_expr(p, &rhs)) {
          ast_free(target);
          return 0;
        }
        AstNode *assign = ast_new("AssignStmt", op->text, t->line, t->col);
        if (!assign || !ast_add_child(assign, target) || !ast_add_child(assign, rhs) || !ast_add_child(node, assign)) {
          ast_free(target);
          ast_free(rhs);
          ast_free(assign);
          return 0;
        }
      } else {
        AstNode *step = NULL;
        if (!parse_expr(p, &step)) return 0;
        if (step && !ast_add_child(node, step)) {
          ast_free(step);
          return 0;
        }
      }
    }
  }

  if (!p_eat(p, TK_SYM, ")")) {
    p_ast_pop(p);
    return 0;
  }
  int ok = parse_stmt(p);
  p_ast_pop(p);
  return ok;
}

static int parse_stmt(Parser *p) {
  if (p_at(p, TK_SYM, "{")) return parse_block(p);
  if (p_at(p, TK_KW, "var") || looks_like_type_start(p)) {
    AstNode *decl = NULL;
    size_t mark = p->i;
    if (parse_var_decl(p, &decl) && p_eat(p, TK_SYM, ";")) {
      AstNode *parent = p_ast_parent(p);
      if (!parent || !ast_add_child(parent, decl)) {
        ast_free(decl);
        return 0;
      }
      return 1;
    }
    ast_free(decl);
    p->i = mark;
  }
  if (p_at(p, TK_KW, "if")) return parse_if_stmt(p);
  if (p_at(p, TK_KW, "while")) return parse_while_stmt(p);
  if (p_at(p, TK_KW, "do")) return parse_do_while_stmt(p);
  if (p_at(p, TK_KW, "for")) return parse_for_stmt(p);
  if (p_at(p, TK_KW, "switch")) return parse_switch_stmt(p);
  if (p_at(p, TK_KW, "try")) return parse_try_stmt(p);
  if (p_at(p, TK_KW, "return")) {
    Token *t = p_t(p, 0);
    p_eat(p, TK_KW, "return");
    AstNode *expr = NULL;
    if (!p_at(p, TK_SYM, ";") && !parse_expr(p, &expr)) return 0;
    if (!p_eat(p, TK_SYM, ";")) return 0;
    AstNode *node = NULL;
    if (!p_ast_add(p, "ReturnStmt", NULL, t->line, t->col, &node)) return 0;
    if (expr && !ast_add_child(node, expr)) {
      ast_free(expr);
      return 0;
    }
    return 1;
  }
  if (p_at(p, TK_KW, "break")) {
    Token *t = p_t(p, 0);
    if (!(p_eat(p, TK_KW, "break") && p_eat(p, TK_SYM, ";"))) return 0;
    return p_ast_add(p, "BreakStmt", NULL, t->line, t->col, NULL);
  }
  if (p_at(p, TK_KW, "continue")) {
    Token *t = p_t(p, 0);
    if (!(p_eat(p, TK_KW, "continue") && p_eat(p, TK_SYM, ";"))) return 0;
    return p_ast_add(p, "ContinueStmt", NULL, t->line, t->col, NULL);
  }
  if (p_at(p, TK_KW, "throw")) {
    Token *t = p_t(p, 0);
    AstNode *expr = NULL;
    if (!(p_eat(p, TK_KW, "throw") && parse_expr(p, &expr) && p_eat(p, TK_SYM, ";"))) return 0;
    AstNode *node = NULL;
    if (!p_ast_add(p, "ThrowStmt", NULL, t->line, t->col, &node)) return 0;
    if (expr && !ast_add_child(node, expr)) {
      ast_free(expr);
      return 0;
    }
    return 1;
  }
  if (looks_like_assign_stmt(p)) {
    Token *t = p_t(p, 0);
    AstNode *target = NULL;
    if (!parse_postfix_expr(p, &target)) return 0;
    if (!p_eat(p, TK_SYM, NULL)) return 0;
    Token *op = &p->toks->items[p->i - 1];
    if (!(strcmp(op->text, "=") == 0 || strcmp(op->text, "+=") == 0 || strcmp(op->text, "-=") == 0 ||
          strcmp(op->text, "*=") == 0 || strcmp(op->text, "/=") == 0)) {
      parse_unexpected(p, op, "assignment operator (=, +=, -=, *=, /=)");
      return 0;
    }
    AstNode *rhs = NULL;
    if (!(parse_conditional_expr(p, &rhs) && p_eat(p, TK_SYM, ";"))) return 0;
    AstNode *node = NULL;
    if (!p_ast_add(p, "AssignStmt", op->text, t->line, t->col, &node)) return 0;
    if (target && !ast_add_child(node, target)) {
      ast_free(target);
      return 0;
    }
    if (rhs && !ast_add_child(node, rhs)) {
      ast_free(rhs);
      return 0;
    }
    return 1;
  }
  Token *t = p_t(p, 0);
  AstNode *expr = NULL;
  if (!(parse_expr(p, &expr) && p_eat(p, TK_SYM, ";"))) return 0;
  AstNode *node = NULL;
  if (!p_ast_add(p, "ExprStmt", NULL, t->line, t->col, &node)) return 0;
  if (expr && !ast_add_child(node, expr)) {
    ast_free(expr);
    return 0;
  }
  return 1;
}

static int parse_primary_expr(Parser *p, AstNode **out) {
  Token *t = p_t(p, 0);
  if (t->kind == TK_NUM || t->kind == TK_STR || t->kind == TK_ID ||
      (t->kind == TK_KW && (strcmp(t->text, "true") == 0 || strcmp(t->text, "false") == 0 || strcmp(t->text, "self") == 0))) {
    p->i++;
    const char *k = (t->kind == TK_ID || (t->kind == TK_KW && strcmp(t->text, "self") == 0)) ? "Identifier" : "Literal";
    if (out) *out = ast_new(k, t->text, t->line, t->col);
    return 1;
  }
  if (p_at(p, TK_SYM, "(")) {
    if (!p_eat(p, TK_SYM, "(")) return 0;
    AstNode *inner = NULL;
    if (!parse_expr(p, &inner)) return 0;
    if (!p_eat(p, TK_SYM, ")")) {
      ast_free(inner);
      return 0;
    }
    if (out) *out = inner;
    else ast_free(inner);
    return 1;
  }
  if (p_at(p, TK_SYM, "[")) {
    Token *lb = p_t(p, 0);
    AstNode *list = ast_new("ListLiteral", NULL, lb->line, lb->col);
    if (!list) return 0;
    p_eat(p, TK_SYM, "[");
    if (!p_at(p, TK_SYM, "]")) {
      AstNode *it = NULL;
      if (!parse_expr(p, &it) || !ast_add_child(list, it)) {
        ast_free(it);
        ast_free(list);
        return 0;
      }
      while (p_at(p, TK_SYM, ",")) {
        p_eat(p, TK_SYM, ",");
        it = NULL;
        if (!parse_expr(p, &it) || !ast_add_child(list, it)) {
          ast_free(it);
          ast_free(list);
          return 0;
        }
      }
    }
    if (!p_eat(p, TK_SYM, "]")) {
      ast_free(list);
      return 0;
    }
    if (out) *out = list;
    else ast_free(list);
    return 1;
  }
  if (p_at(p, TK_SYM, "{")) {
    Token *lb = p_t(p, 0);
    AstNode *map = ast_new("MapLiteral", NULL, lb->line, lb->col);
    if (!map) return 0;
    p_eat(p, TK_SYM, "{");
    if (!p_at(p, TK_SYM, "}")) {
      AstNode *k = NULL;
      AstNode *v = NULL;
      if (!parse_expr(p, &k) || !p_eat(p, TK_SYM, ":") || !parse_expr(p, &v)) {
        ast_free(k);
        ast_free(v);
        ast_free(map);
        return 0;
      }
      AstNode *pair = ast_new("MapPair", NULL, k->line, k->col);
      if (!pair || !ast_add_child(pair, k) || !ast_add_child(pair, v) || !ast_add_child(map, pair)) {
        ast_free(pair);
        ast_free(map);
        return 0;
      }
      while (p_at(p, TK_SYM, ",")) {
        p_eat(p, TK_SYM, ",");
        k = NULL;
        v = NULL;
        if (!parse_expr(p, &k) || !p_eat(p, TK_SYM, ":") || !parse_expr(p, &v)) {
          ast_free(k);
          ast_free(v);
          ast_free(map);
          return 0;
        }
        pair = ast_new("MapPair", NULL, k->line, k->col);
        if (!pair || !ast_add_child(pair, k) || !ast_add_child(pair, v) || !ast_add_child(map, pair)) {
          ast_free(pair);
          ast_free(map);
          return 0;
        }
      }
    }
    if (!p_eat(p, TK_SYM, "}")) {
      ast_free(map);
      return 0;
    }
    if (out) *out = map;
    else ast_free(map);
    return 1;
  }
  parse_unexpected(p, t, "expression");
  return 0;
}

static int parse_postfix_expr(Parser *p, AstNode **out) {
  AstNode *expr = NULL;
  if (!parse_primary_expr(p, &expr)) return 0;
  while (1) {
    if (p_at(p, TK_SYM, "(")) {
      Token *lp = p_t(p, 0);
      p_eat(p, TK_SYM, "(");
      AstNode *call = ast_new("CallExpr", NULL, lp->line, lp->col);
      if (!call || !ast_add_child(call, expr)) {
        ast_free(call);
        ast_free(expr);
        return 0;
      }
      if (!p_at(p, TK_SYM, ")")) {
        AstNode *arg = NULL;
        if (!parse_expr(p, &arg) || !ast_add_child(call, arg)) {
          ast_free(arg);
          ast_free(call);
          return 0;
        }
        while (p_at(p, TK_SYM, ",")) {
          p_eat(p, TK_SYM, ",");
          arg = NULL;
          if (!parse_expr(p, &arg) || !ast_add_child(call, arg)) {
            ast_free(arg);
            ast_free(call);
            return 0;
          }
        }
      }
      if (!p_eat(p, TK_SYM, ")")) {
        ast_free(call);
        return 0;
      }
      expr = call;
      continue;
    }
    if (p_at(p, TK_SYM, "[")) {
      Token *lb = p_t(p, 0);
      p_eat(p, TK_SYM, "[");
      AstNode *idx = NULL;
      if (!parse_expr(p, &idx) || !p_eat(p, TK_SYM, "]")) {
        ast_free(idx);
        ast_free(expr);
        return 0;
      }
      AstNode *ix = ast_new("IndexExpr", NULL, lb->line, lb->col);
      if (!ix || !ast_add_child(ix, expr) || !ast_add_child(ix, idx)) {
        ast_free(ix);
        ast_free(expr);
        return 0;
      }
      expr = ix;
      continue;
    }
    if (p_at(p, TK_SYM, ".")) {
      Token *dot = p_t(p, 0);
      p_eat(p, TK_SYM, ".");
      Token *name = p_t(p, 0);
      if (!(name->kind == TK_ID || name->kind == TK_KW)) {
        parse_unexpected(p, name, "member name (identifier)");
        ast_free(expr);
        return 0;
      }
      p->i++;
      AstNode *mem = ast_new("MemberExpr", name->text, dot->line, dot->col);
      if (!mem || !ast_add_child(mem, expr)) {
        ast_free(mem);
        ast_free(expr);
        return 0;
      }
      expr = mem;
      continue;
    }
    if (p_at(p, TK_SYM, "++") || p_at(p, TK_SYM, "--")) {
      Token *op = p_t(p, 0);
      p->i++;
      AstNode *post = ast_new("PostfixExpr", op->text, op->line, op->col);
      if (!post || !ast_add_child(post, expr)) {
        ast_free(post);
        ast_free(expr);
        return 0;
      }
      expr = post;
      continue;
    }
    break;
  }
  if (out) *out = expr;
  else ast_free(expr);
  return 1;
}

static int parse_unary_expr(Parser *p, AstNode **out) {
  if (p_at(p, TK_SYM, "(") && p_t(p, 1)->kind == TK_KW &&
      (strcmp(p_t(p, 1)->text, "int") == 0 || strcmp(p_t(p, 1)->text, "float") == 0 || strcmp(p_t(p, 1)->text, "byte") == 0)) {
    Token *lp = p_t(p, 0);
    Token *tt = p_t(p, 1);
    if (!p_eat(p, TK_SYM, "(")) return 0;
    if (!p_eat(p, TK_KW, tt->text)) return 0;
    if (!p_eat(p, TK_SYM, ")")) return 0;
    AstNode *inner = NULL;
    if (!parse_unary_expr(p, &inner)) return 0;
    AstNode *c = ast_new("CastExpr", tt->text, lp->line, lp->col);
    if (!c || !ast_add_child(c, inner)) {
      ast_free(c);
      ast_free(inner);
      return 0;
    }
    if (out) *out = c;
    else ast_free(c);
    return 1;
  }
  if (p_at(p, TK_SYM, "!") || p_at(p, TK_SYM, "~") || p_at(p, TK_SYM, "-") || p_at(p, TK_SYM, "++") || p_at(p, TK_SYM, "--")) {
    Token *op = p_t(p, 0);
    p->i++;
    AstNode *inner = NULL;
    if (!parse_postfix_expr(p, &inner)) return 0;
    AstNode *u = ast_new("UnaryExpr", op->text, op->line, op->col);
    if (!u || !ast_add_child(u, inner)) {
      ast_free(u);
      ast_free(inner);
      return 0;
    }
    if (out) *out = u;
    else ast_free(u);
    return 1;
  }
  return parse_postfix_expr(p, out);
}

static int parse_bin_chain(Parser *p, int (*next)(Parser *, AstNode **), const char **ops, const char *kind, AstNode **out) {
  AstNode *left = NULL;
  if (!next(p, &left)) return 0;
  while (p_at(p, TK_SYM, NULL)) {
    Token *t = p_t(p, 0);
    int match = 0;
    for (int i = 0; ops[i]; i++) {
      if (strcmp(t->text, ops[i]) == 0) {
        match = 1;
        break;
      }
    }
    if (!match) break;
    p->i++;
    AstNode *right = NULL;
    if (!next(p, &right)) {
      ast_free(left);
      return 0;
    }
    AstNode *node = ast_new(kind, t->text, t->line, t->col);
    if (!node || !ast_add_child(node, left) || !ast_add_child(node, right)) {
      ast_free(node);
      ast_free(left);
      ast_free(right);
      return 0;
    }
    left = node;
  }
  if (out) *out = left;
  else ast_free(left);
  return 1;
}

static int parse_mul_expr(Parser *p, AstNode **out) {
  static const char *ops[] = {"*", "/", "%", "&", NULL};
  return parse_bin_chain(p, parse_unary_expr, ops, "BinaryExpr", out);
}

static int parse_add_expr(Parser *p, AstNode **out) {
  static const char *ops[] = {"+", "-", "|", "^", NULL};
  return parse_bin_chain(p, parse_mul_expr, ops, "BinaryExpr", out);
}

static int parse_shift_expr(Parser *p, AstNode **out) {
  static const char *ops[] = {"<<", ">>", NULL};
  return parse_bin_chain(p, parse_add_expr, ops, "BinaryExpr", out);
}

static int parse_rel_expr(Parser *p, AstNode **out) {
  static const char *ops[] = {"<", "<=", ">", ">=", NULL};
  return parse_bin_chain(p, parse_shift_expr, ops, "BinaryExpr", out);
}

static int parse_eq_expr(Parser *p, AstNode **out) {
  static const char *ops[] = {"==", "!=", NULL};
  return parse_bin_chain(p, parse_rel_expr, ops, "BinaryExpr", out);
}

static int parse_and_expr(Parser *p, AstNode **out) {
  static const char *ops[] = {"&&", NULL};
  return parse_bin_chain(p, parse_eq_expr, ops, "BinaryExpr", out);
}

static int parse_or_expr(Parser *p, AstNode **out) {
  static const char *ops[] = {"||", NULL};
  return parse_bin_chain(p, parse_and_expr, ops, "BinaryExpr", out);
}

static int parse_conditional_expr(Parser *p, AstNode **out) {
  AstNode *cond = NULL;
  if (!parse_or_expr(p, &cond)) return 0;
  if (p_at(p, TK_SYM, "?")) {
    Token *q = p_t(p, 0);
    p_eat(p, TK_SYM, "?");
    AstNode *then_expr = NULL;
    AstNode *else_expr = NULL;
    if (!parse_conditional_expr(p, &then_expr) || !p_eat(p, TK_SYM, ":") || !parse_conditional_expr(p, &else_expr)) {
      ast_free(cond);
      ast_free(then_expr);
      ast_free(else_expr);
      return 0;
    }
    AstNode *sel = ast_new("ConditionalExpr", "?:", q->line, q->col);
    if (!sel || !ast_add_child(sel, cond) || !ast_add_child(sel, then_expr) || !ast_add_child(sel, else_expr)) {
      ast_free(sel);
      ast_free(cond);
      ast_free(then_expr);
      ast_free(else_expr);
      return 0;
    }
    cond = sel;
  }
  if (out) *out = cond;
  else ast_free(cond);
  return 1;
}

static int parse_expr(Parser *p, AstNode **out) { return parse_conditional_expr(p, out); }

static int parse_param(Parser *p, AstNode *fn_node) {
  size_t type_start = p->i;
  Token *t = p_t(p, 0);
  if (!parse_type(p)) return 0;
  size_t type_end = p->i;
  Token *name = p_t(p, 0);
  if (!p_eat(p, TK_ID, NULL)) return 0;
  AstNode *param = ast_new("Param", name->text, t->line, t->col);
  if (!param) return 0;
  char *type_txt = token_span_text(p, type_start, type_end);
  AstNode *type = ast_new("Type", type_txt ? type_txt : "", t->line, t->col);
  free(type_txt);
  if (!type || !ast_add_child(param, type) || !ast_add_child(fn_node, param)) {
    ast_free(type);
    ast_free(param);
    return 0;
  }
  if (p_at(p, TK_SYM, "...")) {
    Token *v = p_t(p, 0);
    p_eat(p, TK_SYM, "...");
    if (!ast_add_child(param, ast_new("Variadic", "...", v->line, v->col))) return 0;
  }
  return 1;
}

static int parse_function_decl(Parser *p) {
  Token *fkw = p_t(p, 0);
  if (!p_eat(p, TK_KW, "function")) return 0;
  Token *name = p_t(p, 0);
  if (!p_eat(p, TK_ID, NULL)) return 0;
  AstNode *fn = NULL;
  if (!p_ast_add(p, "FunctionDecl", name->text, fkw->line, fkw->col, &fn)) return 0;
  if (!p_ast_push(p, fn)) return 0;
  if (!p_eat(p, TK_SYM, "(")) return 0;
  if (!p_at(p, TK_SYM, ")")) {
    if (!parse_param(p, fn)) return 0;
    while (p_at(p, TK_SYM, ",")) {
      p_eat(p, TK_SYM, ",");
      if (!parse_param(p, fn)) return 0;
    }
  }
  if (!p_eat(p, TK_SYM, ")")) return 0;
  if (!p_eat(p, TK_SYM, ":")) return 0;
  size_t ret_start = p->i;
  Token *rt = p_t(p, 0);
  if (!parse_type(p)) return 0;
  size_t ret_end = p->i;
  char *ret_txt = token_span_text(p, ret_start, ret_end);
  if (!ast_add_child(fn, ast_new("ReturnType", ret_txt ? ret_txt : "", rt->line, rt->col))) {
    free(ret_txt);
    return 0;
  }
  free(ret_txt);
  int ok = parse_block(p);
  p_ast_pop(p);
  return ok;
}

static int parse_prototype_decl(Parser *p) {
  Token *pkw = p_t(p, 0);
  if (!p_eat(p, TK_KW, "prototype")) return 0;
  Token *name = p_t(p, 0);
  if (!p_eat(p, TK_ID, NULL)) return 0;
  AstNode *proto = NULL;
  if (!p_ast_add(p, "PrototypeDecl", name->text, pkw->line, pkw->col, &proto)) return 0;
  if (!p_ast_push(p, proto)) return 0;

  if (p_at(p, TK_SYM, ":")) {
    p_eat(p, TK_SYM, ":");
    Token *parent = p_t(p, 0);
    if (!p_eat(p, TK_ID, NULL)) return 0;
    AstNode *pn = ast_new("Parent", parent->text ? parent->text : "", parent->line, parent->col);
    if (!pn || !ast_add_child(proto, pn)) return 0;
  }

  if (!p_eat(p, TK_SYM, "{")) return 0;
  while (!p_at(p, TK_SYM, "}")) {
    if (p_at(p, TK_EOF, NULL)) {
      set_diag(p->diag, p->file, pkw->line, pkw->col, "E1002", "PARSE_UNCLOSED_BLOCK",
               "unexpected end of file; expected '}' to close prototype");
      p_ast_pop(p);
      return 0;
    }
    if (p_at(p, TK_KW, "function")) {
      if (!parse_function_decl(p)) {
        p_ast_pop(p);
        return 0;
      }
      continue;
    }
    size_t type_start = p->i;
    Token *tt = p_t(p, 0);
    if (!parse_type(p)) {
      p_ast_pop(p);
      return 0;
    }
    size_t type_end = p->i;
    Token *fname = p_t(p, 0);
    if (!p_eat(p, TK_ID, NULL)) {
      p_ast_pop(p);
      return 0;
    }
    if (!p_eat(p, TK_SYM, ";")) {
      p_ast_pop(p);
      return 0;
    }
    AstNode *field = ast_new("FieldDecl", fname->text, tt->line, tt->col);
    if (!field) {
      p_ast_pop(p);
      return 0;
    }
    char *type_txt = token_span_text(p, type_start, type_end);
    AstNode *tn = ast_new("Type", type_txt ? type_txt : "", tt->line, tt->col);
    free(type_txt);
    if (!tn || !ast_add_child(field, tn) || !ast_add_child(proto, field)) {
      ast_free(tn);
      ast_free(field);
      p_ast_pop(p);
      return 0;
    }
  }
  if (!p_eat(p, TK_SYM, "}")) {
    p_ast_pop(p);
    return 0;
  }
  p_ast_pop(p);
  return 1;
}

static char *parse_module_path(Parser *p) {
  Token *t = p_t(p, 0);
  if (!t || (t->kind != TK_ID && t->kind != TK_KW)) return NULL;
  if (!p_eat(p, t->kind, NULL)) return NULL;
  char *out = strdup(t->text ? t->text : "");
  if (!out) return NULL;
  while (p_at(p, TK_SYM, ".") && p_t(p, 1) && (p_t(p, 1)->kind == TK_ID || p_t(p, 1)->kind == TK_KW)) {
    p_eat(p, TK_SYM, ".");
    Token *seg = p_t(p, 0);
    if (!p_eat(p, seg->kind, NULL)) {
      free(out);
      return NULL;
    }
    char *prev = out;
    out = str_printf("%s.%s", prev, seg->text ? seg->text : "");
    free(prev);
    if (!out) return NULL;
  }
  return out;
}

static int parse_import_decl(Parser *p) {
  Token *ikw = p_t(p, 0);
  if (!p_eat(p, TK_KW, "import")) return 0;
  char *mod = NULL;
  int is_path = 0;
  Token *t0 = p_t(p, 0);
  if (t0 && t0->kind == TK_STR) {
    if (!p_eat(p, TK_STR, NULL)) return 0;
    mod = strdup(t0->text ? t0->text : "");
    is_path = 1;
  } else {
    mod = parse_module_path(p);
  }
  if (!mod) return 0;
  AstNode *imp = NULL;
  if (!p_ast_add(p, "ImportDecl", mod, ikw->line, ikw->col, &imp)) {
    free(mod);
    return 0;
  }
  free(mod);
  if (!p_ast_push(p, imp)) return 0;
  if (is_path) {
    AstNode *ip = ast_new("ImportPath", NULL, ikw->line, ikw->col);
    if (!ip || !ast_add_child(imp, ip)) return 0;
  }

  if (p_at(p, TK_KW, "as")) {
    p_eat(p, TK_KW, "as");
    Token *alias = p_t(p, 0);
    if (!p_eat(p, TK_ID, NULL)) return 0;
    AstNode *a = ast_new("Alias", alias->text ? alias->text : "", alias->line, alias->col);
    if (!a || !ast_add_child(imp, a)) return 0;
  } else if (p_at(p, TK_SYM, ".") && p_t(p, 1) && strcmp(p_t(p, 1)->text, "{") == 0) {
    p_eat(p, TK_SYM, ".");
    if (!p_eat(p, TK_SYM, "{")) return 0;
    if (!p_at(p, TK_SYM, "}")) {
      while (1) {
        Token *name = p_t(p, 0);
        if (!p_eat(p, TK_ID, NULL)) return 0;
        AstNode *it = ast_new("ImportItem", name->text ? name->text : "", name->line, name->col);
        if (!it || !ast_add_child(imp, it)) return 0;
        if (p_at(p, TK_KW, "as")) {
          p_eat(p, TK_KW, "as");
          Token *al = p_t(p, 0);
          if (!p_eat(p, TK_ID, NULL)) return 0;
          AstNode *a = ast_new("Alias", al->text ? al->text : "", al->line, al->col);
          if (!a || !ast_add_child(it, a)) return 0;
        }
        if (p_at(p, TK_SYM, ",")) {
          p_eat(p, TK_SYM, ",");
          continue;
        }
        break;
      }
    }
    if (!p_eat(p, TK_SYM, "}")) return 0;
  }

  if (!p_eat(p, TK_SYM, ";")) return 0;
  p_ast_pop(p);
  return 1;
}

static int parse_program(Parser *p) {
  Token *t0 = p_t(p, 0);
  AstNode *root = ast_new("Program", NULL, t0->line, t0->col);
  if (!root) return 0;
  p->ast_root = root;
  p->ast_stack[0] = root;
  p->ast_sp = 1;
  while (!p_at(p, TK_EOF, NULL)) {
    if (p_at(p, TK_KW, "import")) {
      if (!parse_import_decl(p)) return 0;
      continue;
    }
    if (p_at(p, TK_KW, "prototype")) {
      if (!parse_prototype_decl(p)) return 0;
      continue;
    }
    if (p_at(p, TK_KW, "function")) {
      if (!parse_function_decl(p)) return 0;
      continue;
    }
    Token *t = p_t(p, 0);
    parse_unexpected(p, t, "top-level declaration (import, prototype, function)");
    return 0;
  }
  return 1;
}

static char *read_file_raw(const char *path, size_t *out_n) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }
  long sz = ftell(f);
  if (sz < 0) {
    fclose(f);
    return NULL;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return NULL;
  }
  char *buf = (char *)malloc((size_t)sz + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  size_t n = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  if (n != (size_t)sz) {
    free(buf);
    return NULL;
  }
  buf[n] = '\0';
  *out_n = n;
  return buf;
}

static char *read_registry_file(size_t *out_n) {
  const char *env = getenv("PS_MODULE_REGISTRY");
  char exe_path[PATH_MAX];
  const char *exe_candidate = NULL;
  if (g_registry_exe_dir_set) {
    snprintf(exe_path, sizeof(exe_path), "%s/registry.json", g_registry_exe_dir);
    exe_candidate = exe_path;
  }
  const char *candidates[] = {
    env,
    exe_candidate,
    "registry.json",
    "/etc/ps/registry.json",
    "/usr/local/etc/ps/registry.json",
    "/opt/local/etc/ps/registry.json",
    "./modules/registry.json",
    NULL
  };
  char *data = NULL;
  size_t n = 0;
  for (int i = 0; candidates[i]; i++) {
    if (!candidates[i] || !*candidates[i]) continue;
    data = read_file_raw(candidates[i], &n);
    if (data) break;
  }
  if (!data) {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd))) {
      char path[PATH_MAX];
      snprintf(path, sizeof(path), "%s/registry.json", cwd);
      data = read_file_raw(path, &n);
      if (!data) {
        snprintf(path, sizeof(path), "%s/modules/registry.json", cwd);
        data = read_file_raw(path, &n);
      }
    }
  }
  if (data) *out_n = n;
  return data;
}

static void preprocess_config_load_once(void) {
  if (g_preprocess_config_loaded) return;
  preprocess_config_init(&g_preprocess_config);
  g_preprocess_config_loaded = 1;

  size_t n = 0;
  char *data = read_registry_file(&n);
  if (!data) return;

  const char *err = NULL;
  PS_JsonValue *root = ps_json_parse(data, n, &err);
  free(data);
  if (!root) return;

  PS_JsonValue *pp = ps_json_obj_get(root, "preprocessor");
  if (pp && pp->type == PS_JSON_OBJECT) {
    PS_JsonValue *enabled = ps_json_obj_get(pp, "enabled");
    if (enabled && enabled->type == PS_JSON_BOOL) g_preprocess_config.enabled = enabled->as.bool_v ? 1 : 0;

    PS_JsonValue *tool = ps_json_obj_get(pp, "tool");
    if (tool && tool->type == PS_JSON_STRING) g_preprocess_config.tool = strdup(tool->as.str_v);

    PS_JsonValue *opts = ps_json_obj_get(pp, "options");
    if (opts && opts->type == PS_JSON_ARRAY) {
      g_preprocess_config.option_len = opts->as.array_v.len;
      g_preprocess_config.options = (char **)calloc(g_preprocess_config.option_len ? g_preprocess_config.option_len : 1, sizeof(char *));
      if (g_preprocess_config.options) {
        for (size_t i = 0; i < g_preprocess_config.option_len; i++) {
          PS_JsonValue *ov = opts->as.array_v.items[i];
          if (!ov || ov->type != PS_JSON_STRING) continue;
          g_preprocess_config.options[i] = strdup(ov->as.str_v);
        }
      } else {
        g_preprocess_config.option_len = 0;
      }
    }
  }

  if (g_preprocess_config.enabled && !g_preprocess_config.tool) {
    g_preprocess_config.tool = strdup("mcpp");
  }

  ps_json_free(root);
}

static char *read_file(const char *path, size_t *out_n, PsDiag *out_diag) {
  size_t n = 0;
  char *raw = read_file_raw(path, &n);
  if (!raw) {
    set_diag(out_diag, path, 1, 1, "E0001", "IO_READ_ERROR", "cannot read source file");
    return NULL;
  }

  preprocess_config_load_once();
  if (!g_preprocess_config.enabled) {
    preprocess_map_clear(path);
    *out_n = n;
    return raw;
  }

  char *pre = NULL;
  size_t pre_len = 0;
  char *err = NULL;
  PreprocessLineMap map = {0};
  if (!preprocess_source(raw, n, &pre, &pre_len, &g_preprocess_config, path, &map, &err)) {
    const char *msg = err ? err : "preprocessor failed";
    set_diag(out_diag, path, 1, 1, "E0003", "PREPROCESS_ERROR", msg);
    free(err);
    free(raw);
    preprocess_line_map_free(&map);
    return NULL;
  }
  preprocess_map_store(path, &map);
  free(raw);
  *out_n = pre_len;
  return pre;
}

typedef struct Sym {
  char *name;
  char *type;
  int known_list_len; // -1 = unknown
  int initialized;
  int alias_self;
  struct Sym *next;
} Sym;

typedef struct Scope {
  Sym *syms;
  struct Scope *parent;
} Scope;

typedef struct FnSig {
  char *name;
  char *ret_type;
  int param_count;
  int fixed_count;
  int variadic;
  struct FnSig *next;
} FnSig;

typedef struct RegFn {
  char *name;
  char *ret_type;
  int param_count;
  int valid;
  struct RegFn *next;
} RegFn;

typedef struct RegConst {
  char *name;
  char *type;
  char *value;
  struct RegConst *next;
} RegConst;

typedef struct RegMod {
  char *name;
  RegFn *fns;
  RegConst *consts;
  struct RegMod *next;
} RegMod;

typedef struct {
  RegMod *mods;
  char **search_paths;
  size_t search_len;
} ModuleRegistry;

typedef struct ImportSymbol {
  char *local;
  char *module;
  char *name;
  struct ImportSymbol *next;
} ImportSymbol;

typedef struct ImportNamespace {
  char *alias;
  char *module;
  int is_proto;
  struct ImportNamespace *next;
} ImportNamespace;

typedef struct UserModule {
  char *module_name;
  char *path;
  char *proto;
  AstNode *proto_node;
  struct UserModule *next;
} UserModule;

typedef struct {
  const char *file;
  PsDiag *diag;
  FnSig *fns;
  ModuleRegistry *registry;
  ImportSymbol *imports;
  ImportNamespace *namespaces;
  struct ProtoInfo *protos;
  UserModule *user_modules;
} Analyzer;

typedef struct ProtoField {
  char *name;
  char *type;
  struct ProtoField *next;
} ProtoField;

typedef struct ProtoMethod {
  char *name;
  char *ret_type;
  char **param_types;
  int param_count;
  struct ProtoMethod *next;
} ProtoMethod;

typedef struct ProtoInfo {
  char *name;
  char *parent;
  int line;
  int col;
  int builtin;
  ProtoField *fields;
  ProtoMethod *methods;
  struct ProtoInfo *next;
} ProtoInfo;

static void free_syms(Sym *s) {
  while (s) {
    Sym *n = s->next;
    free(s->name);
    free(s->type);
    free(s);
    s = n;
  }
}

static void free_fns(FnSig *f) {
  while (f) {
    FnSig *n = f->next;
    free(f->name);
    free(f->ret_type);
    free(f);
    f = n;
  }
}

static void free_registry(ModuleRegistry *r) {
  if (!r) return;
  RegMod *m = r->mods;
  while (m) {
    RegMod *mn = m->next;
    RegFn *f = m->fns;
    while (f) {
      RegFn *fn = f->next;
      free(f->name);
      free(f->ret_type);
      free(f);
      f = fn;
    }
    RegConst *c = m->consts;
    while (c) {
      RegConst *cn = c->next;
      free(c->name);
      free(c->type);
      free(c->value);
      free(c);
      c = cn;
    }
    free(m->name);
    free(m);
    m = mn;
  }
  if (r->search_paths) {
    for (size_t i = 0; i < r->search_len; i++) free(r->search_paths[i]);
    free(r->search_paths);
  }
  free(r);
}

static void free_imports(ImportSymbol *s) {
  while (s) {
    ImportSymbol *n = s->next;
    free(s->local);
    free(s->module);
    free(s->name);
    free(s);
    s = n;
  }
}

static void free_namespaces(ImportNamespace *n) {
  while (n) {
    ImportNamespace *nx = n->next;
    free(n->alias);
    free(n->module);
    free(n);
    n = nx;
  }
}

static void free_user_modules(UserModule *u) {
  while (u) {
    UserModule *nx = u->next;
    free(u->module_name);
    free(u->path);
    free(u->proto);
    free(u);
    u = nx;
  }
}

static void free_protos(ProtoInfo *p) {
  while (p) {
    ProtoInfo *pn = p->next;
    ProtoField *f = p->fields;
    while (f) {
      ProtoField *fn = f->next;
      free(f->name);
      free(f->type);
      free(f);
      f = fn;
    }
    ProtoMethod *m = p->methods;
    while (m) {
      ProtoMethod *mn = m->next;
      free(m->name);
      free(m->ret_type);
      if (m->param_types) {
        for (int i = 0; i < m->param_count; i++) free(m->param_types[i]);
      }
      free(m->param_types);
      free(m);
      m = mn;
    }
    free(p->name);
    free(p->parent);
    free(p);
    p = pn;
  }
}

static ProtoInfo *proto_find(ProtoInfo *list, const char *name) {
  for (ProtoInfo *p = list; p; p = p->next) {
    if (p->name && name && strcmp(p->name, name) == 0) return p;
  }
  return NULL;
}

static ProtoField *proto_find_field(ProtoInfo *list, const char *proto, const char *field) {
  for (ProtoInfo *p = proto_find(list, proto); p; p = p->parent ? proto_find(list, p->parent) : NULL) {
    for (ProtoField *f = p->fields; f; f = f->next) {
      if (f->name && field && strcmp(f->name, field) == 0) return f;
    }
    if (!p->parent) break;
  }
  return NULL;
}

static ProtoMethod *proto_find_method(ProtoInfo *list, const char *proto, const char *method) {
  for (ProtoInfo *p = proto_find(list, proto); p; p = p->parent ? proto_find(list, p->parent) : NULL) {
    for (ProtoMethod *m = p->methods; m; m = m->next) {
      if (m->name && method && strcmp(m->name, method) == 0) return m;
    }
    if (!p->parent) break;
  }
  return NULL;
}

static int proto_is_subtype(ProtoInfo *list, const char *child, const char *parent) {
  if (!child || !parent) return 0;
  if (strcmp(child, parent) == 0) return 1;
  ProtoInfo *p = proto_find(list, child);
  while (p && p->parent) {
    if (strcmp(p->parent, parent) == 0) return 1;
    p = proto_find(list, p->parent);
  }
  return 0;
}

typedef struct {
  ProtoField **items;
  size_t len;
  size_t cap;
} ProtoFieldVec;

static int proto_field_vec_push(ProtoFieldVec *v, ProtoField *f) {
  if (v->len == v->cap) {
    size_t nc = v->cap == 0 ? 8 : v->cap * 2;
    ProtoField **ni = (ProtoField **)realloc(v->items, nc * sizeof(ProtoField *));
    if (!ni) return 0;
    v->items = ni;
    v->cap = nc;
  }
  v->items[v->len++] = f;
  return 1;
}

static ProtoFieldVec proto_collect_fields(ProtoInfo *list, const char *proto) {
  ProtoFieldVec out;
  memset(&out, 0, sizeof(out));
  ProtoInfo *chain[64];
  size_t chain_len = 0;
  ProtoInfo *cur = proto_find(list, proto);
  while (cur && chain_len < 64) {
    chain[chain_len++] = cur;
    cur = cur->parent ? proto_find(list, cur->parent) : NULL;
  }
  for (size_t i = 0; i < chain_len; i++) {
    ProtoInfo *p = chain[chain_len - 1 - i];
    for (ProtoField *f = p->fields; f; f = f->next) {
      if (!proto_field_vec_push(&out, f)) {
        free(out.items);
        out.items = NULL;
        out.len = 0;
        out.cap = 0;
        return out;
      }
    }
  }
  return out;
}

static void skip_ws(const char **p) {
  while (*p && (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n')) (*p)++;
}

static int consume_kw(const char **p, const char *kw) {
  size_t n = strlen(kw);
  if (strncmp(*p, kw, n) != 0) return 0;
  *p += n;
  return 1;
}

static int parse_registry_type(const char **p, int allow_void) {
  skip_ws(p);
  if (allow_void && consume_kw(p, "void")) return 1;
  if (consume_kw(p, "int") || consume_kw(p, "float") || consume_kw(p, "bool") || consume_kw(p, "byte") || consume_kw(p, "glyph") ||
      consume_kw(p, "string") || consume_kw(p, "TextFile") || consume_kw(p, "BinaryFile") || consume_kw(p, "JSONValue")) {
    return 1;
  }
  if (consume_kw(p, "list") || consume_kw(p, "slice") || consume_kw(p, "view")) {
    if (**p != '<') return 0;
    (*p)++;
    if (!parse_registry_type(p, 0)) return 0;
    if (**p != '>') return 0;
    (*p)++;
    return 1;
  }
  if (consume_kw(p, "map")) {
    if (**p != '<') return 0;
    (*p)++;
    if (!parse_registry_type(p, 0)) return 0;
    if (**p != ',') return 0;
    (*p)++;
    if (!parse_registry_type(p, 0)) return 0;
    if (**p != '>') return 0;
    (*p)++;
    return 1;
  }
  return 0;
}

static int registry_type_valid(const char *s, int allow_void) {
  if (!s) return 0;
  const char *p = s;
  if (!parse_registry_type(&p, allow_void)) return 0;
  skip_ws(&p);
  return *p == '\0';
}

static ModuleRegistry *registry_load(void) {
  char *data = NULL;
  size_t n = 0;
  data = read_registry_file(&n);
  if (!data) return NULL;
  const char *err = NULL;
  PS_JsonValue *root = ps_json_parse(data, n, &err);
  free(data);
  if (!root) return NULL;
  PS_JsonValue *mods = ps_json_obj_get(root, "modules");
  if (!mods || mods->type != PS_JSON_ARRAY) {
    ps_json_free(root);
    return NULL;
  }
  ModuleRegistry *reg = (ModuleRegistry *)calloc(1, sizeof(ModuleRegistry));
  if (!reg) {
    ps_json_free(root);
    return NULL;
  }
  PS_JsonValue *paths = ps_json_obj_get(root, "search_paths");
  if (paths && paths->type == PS_JSON_ARRAY) {
    reg->search_len = paths->as.array_v.len;
    reg->search_paths = (char **)calloc(reg->search_len ? reg->search_len : 1, sizeof(char *));
    if (!reg->search_paths) {
      ps_json_free(root);
      free(reg);
      return NULL;
    }
    for (size_t i = 0; i < reg->search_len; i++) {
      PS_JsonValue *pv = paths->as.array_v.items[i];
      if (!pv || pv->type != PS_JSON_STRING) continue;
      reg->search_paths[i] = strdup(pv->as.str_v);
    }
  }
  for (size_t i = 0; i < mods->as.array_v.len; i++) {
    PS_JsonValue *m = mods->as.array_v.items[i];
    PS_JsonValue *name = ps_json_obj_get(m, "name");
    if (!name || name->type != PS_JSON_STRING) continue;
    RegMod *rm = (RegMod *)calloc(1, sizeof(RegMod));
    if (!rm) continue;
    rm->name = strdup(name->as.str_v);
    rm->next = reg->mods;
    reg->mods = rm;
    PS_JsonValue *fns = ps_json_obj_get(m, "functions");
    if (!fns || fns->type != PS_JSON_ARRAY) continue;
    for (size_t j = 0; j < fns->as.array_v.len; j++) {
      PS_JsonValue *f = fns->as.array_v.items[j];
      PS_JsonValue *fn_name = ps_json_obj_get(f, "name");
      PS_JsonValue *fn_ret = ps_json_obj_get(f, "ret");
      PS_JsonValue *fn_params = ps_json_obj_get(f, "params");
      if (!fn_name || fn_name->type != PS_JSON_STRING) continue;
      RegFn *rf = (RegFn *)calloc(1, sizeof(RegFn));
      if (!rf) continue;
      rf->name = strdup(fn_name->as.str_v);
      const char *ret_str = (fn_ret && fn_ret->type == PS_JSON_STRING) ? fn_ret->as.str_v : "void";
      rf->ret_type = strdup(ret_str);
      rf->param_count = (fn_params && fn_params->type == PS_JSON_ARRAY) ? (int)fn_params->as.array_v.len : 0;
      rf->valid = registry_type_valid(ret_str, 1);
      if (fn_params && fn_params->type == PS_JSON_ARRAY) {
        for (size_t pi = 0; pi < fn_params->as.array_v.len; pi++) {
          PS_JsonValue *pv = fn_params->as.array_v.items[pi];
          if (!pv || pv->type != PS_JSON_STRING || !registry_type_valid(pv->as.str_v, 0)) {
            rf->valid = 0;
          }
        }
      } else if (fn_params && fn_params->type != PS_JSON_ARRAY) {
        rf->valid = 0;
      }
      rf->next = rm->fns;
      rm->fns = rf;
    }
    PS_JsonValue *consts = ps_json_obj_get(m, "constants");
    if (consts && consts->type == PS_JSON_ARRAY) {
      for (size_t j = 0; j < consts->as.array_v.len; j++) {
        PS_JsonValue *c = consts->as.array_v.items[j];
        PS_JsonValue *cname = ps_json_obj_get(c, "name");
        PS_JsonValue *ctype = ps_json_obj_get(c, "type");
        PS_JsonValue *cval = ps_json_obj_get(c, "value");
        if (!cname || cname->type != PS_JSON_STRING) continue;
        if (!ctype || ctype->type != PS_JSON_STRING) continue;
        RegConst *rc = (RegConst *)calloc(1, sizeof(RegConst));
        if (!rc) continue;
        rc->name = strdup(cname->as.str_v);
        rc->type = strdup(ctype->as.str_v);
        if (strcmp(ctype->as.str_v, "float") == 0) {
          if (!cval || (cval->type != PS_JSON_STRING && cval->type != PS_JSON_NUMBER)) {
            free(rc->name);
            free(rc->type);
            free(rc);
            continue;
          }
          if (cval->type == PS_JSON_STRING) rc->value = strdup(cval->as.str_v);
          else {
            char buf[64];
            snprintf(buf, sizeof(buf), "%.17g", cval->as.num_v);
            rc->value = strdup(buf);
          }
        } else if (strcmp(ctype->as.str_v, "int") == 0) {
          if (!cval || (cval->type != PS_JSON_STRING && cval->type != PS_JSON_NUMBER)) {
            free(rc->name);
            free(rc->type);
            free(rc);
            continue;
          }
          if (cval->type == PS_JSON_STRING) rc->value = strdup(cval->as.str_v);
          else {
            char buf[64];
            snprintf(buf, sizeof(buf), "%.0f", cval->as.num_v);
            rc->value = strdup(buf);
          }
        } else if (strcmp(ctype->as.str_v, "string") == 0 || strcmp(ctype->as.str_v, "file") == 0 ||
                   strcmp(ctype->as.str_v, "TextFile") == 0 || strcmp(ctype->as.str_v, "BinaryFile") == 0) {
          if (!cval || cval->type != PS_JSON_STRING) {
            free(rc->name);
            free(rc->type);
            free(rc);
            continue;
          }
          rc->value = strdup(cval->as.str_v);
        } else {
          free(rc->name);
          free(rc->type);
          free(rc);
          continue;
        }
        rc->next = rm->consts;
        rm->consts = rc;
      }
    }
  }
  ps_json_free(root);
  return reg;
}

static RegMod *registry_find_mod(ModuleRegistry *r, const char *name) {
  for (RegMod *m = r ? r->mods : NULL; m; m = m->next) {
    if (strcmp(m->name, name) == 0) return m;
  }
  return NULL;
}

static RegFn *registry_find_fn(ModuleRegistry *r, const char *mod, const char *name) {
  RegMod *m = registry_find_mod(r, mod);
  if (!m) return NULL;
  for (RegFn *f = m->fns; f; f = f->next) {
    if (strcmp(f->name, name) == 0) return f;
  }
  return NULL;
}

static RegConst *registry_find_const(ModuleRegistry *r, const char *mod, const char *name) {
  RegMod *m = registry_find_mod(r, mod);
  if (!m) return NULL;
  for (RegConst *c = m->consts; c; c = c->next) {
    if (strcmp(c->name, name) == 0) return c;
  }
  return NULL;
}

static char *canon_type(const char *in) {
  if (!in) return strdup("unknown");
  size_t n = strlen(in);
  char *out = (char *)malloc(n + 1);
  if (!out) return NULL;
  size_t j = 0;
  for (size_t i = 0; i < n; i++) {
    if (in[i] != ' ' && in[i] != '\t' && in[i] != '\r' && in[i] != '\n') out[j++] = in[i];
  }
  out[j] = '\0';
  return out;
}

static AstNode *ast_child_kind(AstNode *n, const char *kind) {
  if (!n) return NULL;
  for (size_t i = 0; i < n->child_len; i++) {
    if (strcmp(n->children[i]->kind, kind) == 0) return n->children[i];
  }
  return NULL;
}

static AstNode *ast_last_child(AstNode *n) {
  if (!n || n->child_len == 0) return NULL;
  return n->children[n->child_len - 1];
}

static int ast_is_terminator(AstNode *n) {
  if (!n) return 0;
  return strcmp(n->kind, "BreakStmt") == 0 || strcmp(n->kind, "ReturnStmt") == 0 || strcmp(n->kind, "ThrowStmt") == 0;
}

static int scope_define_alias(Scope *s, const char *name, const char *type, int known_list_len, int initialized, int alias_self) {
  Sym *e = (Sym *)calloc(1, sizeof(Sym));
  if (!e) return 0;
  e->name = strdup(name ? name : "");
  e->type = strdup(type ? type : "unknown");
  e->known_list_len = known_list_len;
  e->initialized = initialized;
  e->alias_self = alias_self;
  if (!e->name || !e->type) {
    free(e->name);
    free(e->type);
    free(e);
    return 0;
  }
  e->next = s->syms;
  s->syms = e;
  return 1;
}

static int scope_define(Scope *s, const char *name, const char *type, int known_list_len, int initialized) {
  return scope_define_alias(s, name, type, known_list_len, initialized, 0);
}

static Sym *scope_lookup_sym(Scope *s, const char *name) {
  for (Scope *cur = s; cur; cur = cur->parent) {
    for (Sym *e = cur->syms; e; e = e->next) {
      if (strcmp(e->name, name) == 0) return e;
    }
  }
  return NULL;
}

static int expr_is_self_alias(AstNode *e, Scope *scope) {
  if (!e || strcmp(e->kind, "Identifier") != 0) return 0;
  if (e->text && strcmp(e->text, "self") == 0) return 1;
  if (!e->text) return 0;
  Sym *s = scope_lookup_sym(scope, e->text);
  return s && s->alias_self;
}

static const char *scope_lookup(Scope *s, const char *name) {
  Sym *sym = scope_lookup_sym(s, name);
  return sym ? sym->type : "unknown";
}

static FnSig *find_fn(Analyzer *a, const char *name) {
  for (FnSig *f = a->fns; f; f = f->next) {
    if (strcmp(f->name, name) == 0) return f;
  }
  return NULL;
}

static ImportNamespace *find_namespace(Analyzer *a, const char *alias) {
  for (ImportNamespace *n = a->namespaces; n; n = n->next) {
    if (strcmp(n->alias, alias) == 0) return n;
  }
  return NULL;
}

static int add_imported_fn(Analyzer *a, const char *local, const char *ret_type, int param_count) {
  FnSig *f = (FnSig *)calloc(1, sizeof(FnSig));
  if (!f) return 0;
  f->name = strdup(local ? local : "");
  f->ret_type = canon_type(ret_type ? ret_type : "void");
  if (!f->name || !f->ret_type) {
    free(f->name);
    free(f->ret_type);
    free(f);
    return 0;
  }
  f->param_count = param_count;
  f->fixed_count = param_count;
  f->variadic = 0;
  f->next = a->fns;
  a->fns = f;
  return 1;
}

static const char *import_decl_alias(AstNode *imp) {
  AstNode *a = ast_child_kind(imp, "Alias");
  return a ? a->text : NULL;
}

static const char *import_item_alias(AstNode *item) {
  AstNode *a = ast_child_kind(item, "Alias");
  return a ? a->text : NULL;
}

static const char *last_segment(const char *mod) {
  const char *dot = strrchr(mod, '.');
  return dot ? dot + 1 : mod;
}

static int import_is_path(AstNode *imp) {
  return ast_child_kind(imp, "ImportPath") != NULL;
}

static int has_pts_ext(const char *s) {
  if (!s) return 0;
  size_t n = strlen(s);
  return n >= 4 && strcmp(s + n - 4, ".pts") == 0;
}

static int is_abs_path(const char *s) { return s && s[0] == '/'; }

static int file_exists(const char *path) {
  struct stat st;
  return path && stat(path, &st) == 0;
}

static char *path_dirname_dup(const char *path) {
  if (!path) return strdup(".");
  const char *slash = strrchr(path, '/');
  if (!slash) return strdup(".");
  size_t len = (size_t)(slash - path);
  return dup_range(path, 0, len);
}

static char *path_join(const char *a, const char *b) {
  if (!a || !*a) return strdup(b ? b : "");
  if (!b || !*b) return strdup(a);
  if (a[strlen(a) - 1] == '/') return str_printf("%s%s", a, b);
  return str_printf("%s/%s", a, b);
}

static char *module_name_to_relpath(const char *mod) {
  if (!mod) return NULL;
  size_t n = strlen(mod);
  char *tmp = (char *)malloc(n + 5);
  if (!tmp) return NULL;
  for (size_t i = 0; i < n; i++) tmp[i] = (mod[i] == '.') ? '/' : mod[i];
  tmp[n] = '\0';
  char *out = str_printf("%s.pts", tmp);
  free(tmp);
  return out;
}

static UserModule *find_user_module_by_path(UserModule *list, const char *path) {
  for (UserModule *u = list; u; u = u->next) {
    if (u->path && path && strcmp(u->path, path) == 0) return u;
  }
  return NULL;
}

static UserModule *find_user_module_by_name(UserModule *list, const char *name) {
  for (UserModule *u = list; u; u = u->next) {
    if (u->module_name && name && strcmp(u->module_name, name) == 0) return u;
  }
  return NULL;
}

static AstNode *find_root_prototype(AstNode *root, AstNode *imp, PsDiag *diag, const char *file) {
  AstNode *proto = NULL;
  int proto_count = 0;
  for (size_t i = 0; i < root->child_len; i++) {
    AstNode *c = root->children[i];
    if (strcmp(c->kind, "FunctionDecl") == 0) {
      set_diag(diag, file, imp->line, imp->col, "E2004", "IMPORT_PATH_NO_ROOT_PROTO", "module must define exactly one root prototype");
      return NULL;
    }
    if (strcmp(c->kind, "PrototypeDecl") == 0) {
      proto_count += 1;
      proto = c;
    }
  }
  if (proto_count != 1 || !proto) {
    set_diag(diag, file, imp->line, imp->col, "E2004", "IMPORT_PATH_NO_ROOT_PROTO", "module must define exactly one root prototype");
    return NULL;
  }
  return proto;
}

static void detach_child(AstNode *root, AstNode *child) {
  if (!root || !child) return;
  for (size_t i = 0; i < root->child_len; i++) {
    if (root->children[i] == child) {
      for (size_t j = i + 1; j < root->child_len; j++) root->children[j - 1] = root->children[j];
      root->child_len -= 1;
      return;
    }
  }
}

static AstNode *proto_find_method_node(AstNode *proto, const char *name) {
  if (!proto || !name) return NULL;
  for (size_t i = 0; i < proto->child_len; i++) {
    AstNode *c = proto->children[i];
    if (strcmp(c->kind, "FunctionDecl") == 0 && c->text && strcmp(c->text, name) == 0) return c;
  }
  return NULL;
}

static int proto_method_param_count(AstNode *fn) {
  int count = 0;
  if (!fn) return 0;
  for (size_t i = 0; i < fn->child_len; i++) {
    if (strcmp(fn->children[i]->kind, "Param") == 0) count += 1;
  }
  return count;
}

static char *proto_method_ret_type(AstNode *fn) {
  AstNode *rt = ast_child_kind(fn, "ReturnType");
  return canon_type(rt && rt->text ? rt->text : "void");
}

static char *resolve_import_path_literal(const char *importer_file, const char *literal) {
  if (!literal) return NULL;
  if (is_abs_path(literal)) return strdup(literal);
  char *dir = path_dirname_dup(importer_file);
  char *out = path_join(dir, literal);
  free(dir);
  return out;
}

static char *resolve_module_by_name(ModuleRegistry *reg, const char *root_dir, const char *mod) {
  if (!reg || !reg->search_paths || reg->search_len == 0) return NULL;
  char *rel = module_name_to_relpath(mod);
  char *short_name = str_printf("%s.pts", last_segment(mod));
  for (size_t i = 0; i < reg->search_len; i++) {
    const char *sp = reg->search_paths[i];
    if (!sp || !*sp) continue;
    char *base = is_abs_path(sp) ? strdup(sp) : path_join(root_dir, sp);
    char *cand1 = path_join(base, rel);
    if (file_exists(cand1)) {
      free(base); free(short_name);
      char *out = cand1;
      free(rel);
      return out;
    }
    free(cand1);
    char *cand2 = path_join(base, short_name);
    if (file_exists(cand2)) {
      free(base); free(rel);
      char *out = cand2;
      free(short_name);
      return out;
    }
    free(cand2);
    free(base);
  }
  free(rel);
  free(short_name);
  return NULL;
}

static int collect_imports(Analyzer *a, AstNode *root) {
  size_t import_count = 0;
  for (size_t i = 0; i < root->child_len; i++) {
    if (strcmp(root->children[i]->kind, "ImportDecl") == 0) import_count += 1;
  }
  if (import_count == 0) return 1;
  int has_by_name = 0;
  for (size_t i = 0; i < root->child_len; i++) {
    AstNode *imp = root->children[i];
    if (strcmp(imp->kind, "ImportDecl") != 0) continue;
    if (!import_is_path(imp)) {
      has_by_name = 1;
      break;
    }
  }
  if (has_by_name) {
    a->registry = registry_load();
    if (!a->registry) {
      AstNode *imp = NULL;
      for (size_t i = 0; i < root->child_len; i++) {
        if (strcmp(root->children[i]->kind, "ImportDecl") == 0) {
          imp = root->children[i];
          break;
        }
      }
      if (!imp) return 0;
      set_diag(a->diag, a->file, imp->line, imp->col, "E2001", "UNRESOLVED_NAME", "module registry not found");
      return 0;
    }
  } else {
    a->registry = registry_load();
  }

  char *root_dir = path_dirname_dup(a->file);
  for (size_t i = 0; i < root->child_len; i++) {
    AstNode *imp = root->children[i];
    if (strcmp(imp->kind, "ImportDecl") != 0) continue;
    const char *mod = imp->text ? imp->text : "";
    int is_path = import_is_path(imp);
    UserModule *um = NULL;
    RegMod *m = (!is_path && a->registry) ? registry_find_mod(a->registry, mod) : NULL;
    if (is_path) {
      if (!has_pts_ext(mod)) {
        set_diag(a->diag, a->file, imp->line, imp->col, "E2003", "IMPORT_PATH_BAD_EXTENSION", "import path must end with .pts");
        free(root_dir);
        return 0;
      }
      char *abs = resolve_import_path_literal(a->file, mod);
      if (!abs || !file_exists(abs)) {
        free(abs);
        set_diag(a->diag, a->file, imp->line, imp->col, "E2002", "IMPORT_PATH_NOT_FOUND", "import path not found");
        free(root_dir);
        return 0;
      }
      um = find_user_module_by_path(a->user_modules, abs);
      if (!um) {
        AstNode *mod_root = NULL;
        PsDiag tmp;
        int rc = parse_file_internal(abs, &tmp, &mod_root);
        if (rc != 0) {
          *a->diag = tmp;
          ast_free(mod_root);
          free(abs);
          free(root_dir);
          return 0;
        }
        AstNode *proto = find_root_prototype(mod_root, imp, a->diag, a->file);
        if (!proto) {
          ast_free(mod_root);
          free(abs);
          free(root_dir);
          return 0;
        }
        detach_child(mod_root, proto);
        if (!ast_add_child(root, proto)) {
          ast_free(mod_root);
          free(abs);
          free(root_dir);
          return 0;
        }
        um = (UserModule *)calloc(1, sizeof(UserModule));
        if (!um) {
          ast_free(mod_root);
          free(abs);
          free(root_dir);
          return 0;
        }
        um->path = abs;
        um->proto = strdup(proto->text ? proto->text : "");
        um->proto_node = proto;
        um->next = a->user_modules;
        a->user_modules = um;
        ast_free(mod_root);
      } else {
        free(abs);
      }
    } else if (!m) {
      char *abs = resolve_module_by_name(a->registry, root_dir, mod);
      if (!abs) {
        set_diag(a->diag, a->file, imp->line, imp->col, "E2001", "UNRESOLVED_NAME", "unknown module");
        free(root_dir);
        return 0;
      }
      um = find_user_module_by_name(a->user_modules, mod);
      if (!um) {
        AstNode *mod_root = NULL;
        PsDiag tmp;
        int rc = parse_file_internal(abs, &tmp, &mod_root);
        if (rc != 0) {
          *a->diag = tmp;
          ast_free(mod_root);
          free(abs);
          free(root_dir);
          return 0;
        }
        AstNode *proto = find_root_prototype(mod_root, imp, a->diag, a->file);
        if (!proto) {
          ast_free(mod_root);
          free(abs);
          free(root_dir);
          return 0;
        }
        detach_child(mod_root, proto);
        if (!ast_add_child(root, proto)) {
          ast_free(mod_root);
          free(abs);
          free(root_dir);
          return 0;
        }
        um = (UserModule *)calloc(1, sizeof(UserModule));
        if (!um) {
          ast_free(mod_root);
          free(abs);
          free(root_dir);
          return 0;
        }
        um->module_name = strdup(mod);
        um->path = abs;
        um->proto = strdup(proto->text ? proto->text : "");
        um->proto_node = proto;
        um->next = a->user_modules;
        a->user_modules = um;
        ast_free(mod_root);
      } else {
        free(abs);
      }
    }

    int has_items = 0;
    for (size_t j = 0; j < imp->child_len; j++) {
      if (strcmp(imp->children[j]->kind, "ImportItem") == 0) has_items = 1;
    }
    if (has_items) {
      for (size_t j = 0; j < imp->child_len; j++) {
        AstNode *it = imp->children[j];
        if (strcmp(it->kind, "ImportItem") != 0) continue;
        const char *name = it->text ? it->text : "";
        if (um) {
          if (strcmp(name, "clone") == 0) {
            const char *alias = import_item_alias(it);
            const char *local = alias ? alias : name;
            ImportSymbol *s = (ImportSymbol *)calloc(1, sizeof(ImportSymbol));
            if (!s) return 0;
            s->local = strdup(local);
            s->module = strdup(um->proto ? um->proto : "");
            s->name = strdup(name);
            s->next = a->imports;
            a->imports = s;
            if (!add_imported_fn(a, local, um->proto ? um->proto : "unknown", 0)) return 0;
            continue;
          }
          AstNode *method = proto_find_method_node(um->proto_node, name);
          if (!method) {
            set_diag(a->diag, a->file, it->line, it->col, "E2001", "UNRESOLVED_NAME", "unknown symbol in module");
            free(root_dir);
            return 0;
          }
          char *ret = proto_method_ret_type(method);
          int pc = proto_method_param_count(method) + 1;
          const char *alias = import_item_alias(it);
          const char *local = alias ? alias : name;
          ImportSymbol *s = (ImportSymbol *)calloc(1, sizeof(ImportSymbol));
          if (!s) {
            free(ret);
            free(root_dir);
            return 0;
          }
          s->local = strdup(local);
          s->module = strdup(um->proto ? um->proto : "");
          s->name = strdup(name);
          s->next = a->imports;
          a->imports = s;
          if (!add_imported_fn(a, local, ret ? ret : "void", pc)) {
            free(ret);
            free(root_dir);
            return 0;
          }
          free(ret);
        } else {
          RegFn *rf = registry_find_fn(a->registry, mod, name);
          if (!rf) {
            set_diag(a->diag, a->file, it->line, it->col, "E2001", "UNRESOLVED_NAME", "unknown symbol in module");
            free(root_dir);
            return 0;
          }
          if (!rf->valid) {
            set_diag(a->diag, a->file, it->line, it->col, "E2001", "UNRESOLVED_NAME", "invalid registry signature");
            free(root_dir);
            return 0;
          }
          const char *alias = import_item_alias(it);
          const char *local = alias ? alias : name;
          ImportSymbol *s = (ImportSymbol *)calloc(1, sizeof(ImportSymbol));
          if (!s) {
            free(root_dir);
            return 0;
          }
          s->local = strdup(local);
          s->module = strdup(mod);
          s->name = strdup(name);
          s->next = a->imports;
          a->imports = s;
          if (!add_imported_fn(a, local, rf->ret_type, rf->param_count)) {
            free(root_dir);
            return 0;
          }
        }
      }
    } else {
      const char *alias = import_decl_alias(imp);
      if (um) {
        const char *ns = alias ? alias : (is_path ? (um->proto ? um->proto : "") : last_segment(mod));
        if (ns && *ns) {
          ImportNamespace *n = (ImportNamespace *)calloc(1, sizeof(ImportNamespace));
          if (!n) {
            free(root_dir);
            return 0;
          }
          n->alias = strdup(ns);
          n->module = strdup(um->proto ? um->proto : "");
          n->is_proto = 1;
          n->next = a->namespaces;
          a->namespaces = n;
        }
      } else {
        const char *ns = alias ? alias : last_segment(mod);
        ImportNamespace *n = (ImportNamespace *)calloc(1, sizeof(ImportNamespace));
        if (!n) {
          free(root_dir);
          return 0;
        }
        n->alias = strdup(ns);
        n->module = strdup(mod);
        n->is_proto = 0;
        n->next = a->namespaces;
        a->namespaces = n;
      }
    }
  }
  free(root_dir);
  return 1;
}

static int proto_same_signature(ProtoMethod *a, ProtoMethod *b) {
  if (!a || !b) return 0;
  if (strcmp(a->ret_type ? a->ret_type : "void", b->ret_type ? b->ret_type : "void") != 0) return 0;
  if (a->param_count != b->param_count) return 0;
  for (int i = 0; i < a->param_count; i++) {
    const char *at = a->param_types ? a->param_types[i] : NULL;
    const char *bt = b->param_types ? b->param_types[i] : NULL;
    if (!at || !bt || strcmp(at, bt) != 0) return 0;
  }
  return 1;
}

static ProtoInfo *proto_append(Analyzer *a, const char *name, const char *parent) {
  ProtoInfo *p = (ProtoInfo *)calloc(1, sizeof(ProtoInfo));
  if (!p) return NULL;
  p->name = strdup(name ? name : "");
  p->parent = parent ? strdup(parent) : NULL;
  p->line = 1;
  p->col = 1;
  p->builtin = 0;
  p->next = a->protos;
  a->protos = p;
  return p;
}

static int proto_add_field(ProtoInfo *p, const char *name, const char *type) {
  if (!p || !name || !type) return 0;
  ProtoField *nf = (ProtoField *)calloc(1, sizeof(ProtoField));
  if (!nf) return 0;
  nf->name = strdup(name);
  nf->type = strdup(type);
  nf->next = NULL;
  if (!p->fields) {
    p->fields = nf;
  } else {
    ProtoField *tail = p->fields;
    while (tail->next) tail = tail->next;
    tail->next = nf;
  }
  return 1;
}

static int add_builtin_exception_protos(Analyzer *a) {
  if (proto_find(a->protos, "Exception")) return 1;
  ProtoInfo *ex = proto_append(a, "Exception", NULL);
  if (!ex) return 0;
  ex->builtin = 1;
  if (!proto_add_field(ex, "file", "string")) return 0;
  if (!proto_add_field(ex, "line", "int")) return 0;
  if (!proto_add_field(ex, "column", "int")) return 0;
  if (!proto_add_field(ex, "message", "string")) return 0;
  if (!proto_add_field(ex, "cause", "Exception")) return 0;

  if (proto_find(a->protos, "RuntimeException")) return 1;
  ProtoInfo *rex = proto_append(a, "RuntimeException", "Exception");
  if (!rex) return 0;
  rex->builtin = 1;
  if (!proto_add_field(rex, "code", "string")) return 0;
  if (!proto_add_field(rex, "category", "string")) return 0;
  return 1;
}

static int collect_prototypes(Analyzer *a, AstNode *root) {
  if (!add_builtin_exception_protos(a)) return 0;
  for (size_t i = 0; i < root->child_len; i++) {
    AstNode *pd = root->children[i];
    if (strcmp(pd->kind, "PrototypeDecl") != 0) continue;
    const char *name = pd->text ? pd->text : "";
    if (strcmp(name, "Exception") == 0 || strcmp(name, "RuntimeException") == 0) {
      set_diag(a->diag, a->file, pd->line, pd->col, "E2001", "UNRESOLVED_NAME", "reserved prototype name");
      return 0;
    }
    if (proto_find(a->protos, name)) {
      set_diag(a->diag, a->file, pd->line, pd->col, "E2001", "UNRESOLVED_NAME", "duplicate prototype");
      return 0;
    }
    ProtoInfo *p = (ProtoInfo *)calloc(1, sizeof(ProtoInfo));
    if (!p) return 0;
    p->name = strdup(name);
    p->line = pd->line;
    p->col = pd->col;
    AstNode *parent = ast_child_kind(pd, "Parent");
    if (parent && parent->text) p->parent = strdup(parent->text);
    p->next = a->protos;
    a->protos = p;

    for (size_t j = 0; j < pd->child_len; j++) {
      AstNode *c = pd->children[j];
      if (strcmp(c->kind, "FieldDecl") == 0) {
        const char *fname = c->text ? c->text : "";
        for (ProtoField *f = p->fields; f; f = f->next) {
          if (f->name && strcmp(f->name, fname) == 0) {
            set_diag(a->diag, a->file, c->line, c->col, "E2001", "UNRESOLVED_NAME", "duplicate field in prototype");
            return 0;
          }
        }
        AstNode *tn = ast_child_kind(c, "Type");
        char *ft = canon_type(tn ? tn->text : "unknown");
        ProtoField *nf = (ProtoField *)calloc(1, sizeof(ProtoField));
        if (!nf) return 0;
        nf->name = strdup(fname);
        nf->type = ft ? ft : strdup("unknown");
        nf->next = NULL;
        if (!p->fields) {
          p->fields = nf;
        } else {
          ProtoField *tail = p->fields;
          while (tail->next) tail = tail->next;
          tail->next = nf;
        }
      } else if (strcmp(c->kind, "FunctionDecl") == 0) {
        const char *mname = c->text ? c->text : "";
        for (ProtoMethod *m = p->methods; m; m = m->next) {
          if (m->name && strcmp(m->name, mname) == 0) {
            set_diag(a->diag, a->file, c->line, c->col, "E2001", "UNRESOLVED_NAME", "duplicate method in prototype");
            return 0;
          }
        }
        ProtoMethod *nm = (ProtoMethod *)calloc(1, sizeof(ProtoMethod));
        if (!nm) return 0;
        nm->name = strdup(mname);
        AstNode *rt = ast_child_kind(c, "ReturnType");
        nm->ret_type = canon_type(rt ? rt->text : "void");
        int pc = 0;
        for (size_t pi = 0; pi < c->child_len; pi++) {
          if (strcmp(c->children[pi]->kind, "Param") == 0) pc++;
        }
        nm->param_count = pc;
        if (pc > 0) {
          nm->param_types = (char **)calloc((size_t)pc, sizeof(char *));
          if (!nm->param_types) return 0;
          int idx = 0;
          for (size_t pi = 0; pi < c->child_len; pi++) {
            AstNode *pn = c->children[pi];
            if (strcmp(pn->kind, "Param") != 0) continue;
            AstNode *pt = ast_child_kind(pn, "Type");
            nm->param_types[idx++] = canon_type(pt ? pt->text : "unknown");
          }
        }
        nm->next = NULL;
        if (!p->methods) {
          p->methods = nm;
        } else {
          ProtoMethod *tail = p->methods;
          while (tail->next) tail = tail->next;
          tail->next = nm;
        }
      }
    }
  }

  int missing_parent = 0;
  int min_line = 0;
  int min_col = 0;
  for (ProtoInfo *p = a->protos; p; p = p->next) {
    if (p->parent && !proto_find(a->protos, p->parent)) {
      if (!missing_parent || p->line < min_line || (p->line == min_line && p->col < min_col)) {
        min_line = p->line;
        min_col = p->col;
      }
      missing_parent = 1;
      continue;
    }
    if (p->parent) {
      for (ProtoField *f = p->fields; f; f = f->next) {
        if (proto_find_field(a->protos, p->parent, f->name)) {
          set_diag(a->diag, a->file, p->line, p->col, "E3001", "TYPE_MISMATCH_ASSIGNMENT", "field already defined in parent");
          return 0;
        }
      }
      for (ProtoMethod *m = p->methods; m; m = m->next) {
        ProtoMethod *pm = proto_find_method(a->protos, p->parent, m->name);
        if (pm && !proto_same_signature(pm, m)) {
          set_diag(a->diag, a->file, p->line, p->col, "E3001", "TYPE_MISMATCH_ASSIGNMENT", "override signature mismatch");
          return 0;
        }
      }
    }
  }
  if (missing_parent) {
    set_diag(a->diag, a->file, min_line, min_col, "E2001", "UNRESOLVED_NAME", "unknown parent prototype");
    return 0;
  }
  return 1;
}

static int add_fn(Analyzer *a, AstNode *fn) {
  FnSig *f = (FnSig *)calloc(1, sizeof(FnSig));
  if (!f) return 0;
  f->name = strdup(fn->text ? fn->text : "");
  AstNode *rt = ast_child_kind(fn, "ReturnType");
  char *rt_c = canon_type(rt ? rt->text : "void");
  f->ret_type = rt_c ? rt_c : strdup("void");
  if (!f->name || !f->ret_type) {
    free(f->name);
    free(f->ret_type);
    free(f);
    return 0;
  }
  for (size_t i = 0; i < fn->child_len; i++) {
    AstNode *c = fn->children[i];
    if (strcmp(c->kind, "Param") != 0) continue;
    f->param_count += 1;
    if (ast_child_kind(c, "Variadic")) f->variadic = 1;
    else f->fixed_count += 1;
  }
  f->next = a->fns;
  a->fns = f;
  return 1;
}

static int is_all_digits(const char *s) {
  if (!s || !*s) return 0;
  for (const char *p = s; *p; p++) if (!isdigit((unsigned char)*p)) return 0;
  return 1;
}

static int is_hex_token(const char *s) {
  if (!s || !*s) return 0;
  if (strlen(s) < 3) return 0;
  if (!(s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))) return 0;
  for (const char *p = s + 2; *p; p++) {
    if (!isxdigit((unsigned char)*p)) return 0;
  }
  return 1;
}

static int is_bin_token(const char *s) {
  if (!s || !*s) return 0;
  if (strlen(s) < 3) return 0;
  if (!(s[0] == '0' && (s[1] == 'b' || s[1] == 'B'))) return 0;
  for (const char *p = s + 2; *p; p++) {
    if (!(*p == '0' || *p == '1')) return 0;
  }
  return 1;
}

static int is_float_token(const char *s) {
  if (!s || !*s) return 0;
  int i = 0;
  int seen_digit = 0;
  int seen_dot = 0;
  while (s[i] && isdigit((unsigned char)s[i])) {
    seen_digit = 1;
    i++;
  }
  if (s[i] == '.') {
    seen_dot = 1;
    i++;
    while (s[i] && isdigit((unsigned char)s[i])) {
      seen_digit = 1;
      i++;
    }
  }
  if (s[i] == 'e' || s[i] == 'E') {
    if (!seen_digit) return 0;
    i++;
    if (s[i] == '+' || s[i] == '-') i++;
    int exp_digit = 0;
    while (s[i] && isdigit((unsigned char)s[i])) {
      exp_digit = 1;
      i++;
    }
    if (!exp_digit) return 0;
    return s[i] == '\0';
  }
  if (!seen_dot) return 0;
  return s[i] == '\0' && seen_digit;
}

static int int_literal_to_ll(const char *s, long long *out) {
  if (!s || !*s) return 0;
  if (is_hex_token(s)) {
    *out = strtoll(s, NULL, 16);
    return 1;
  }
  if (is_bin_token(s)) {
    long long v = 0;
    for (const char *p = s + 2; *p; p++) v = (v << 1) + (*p == '1' ? 1 : 0);
    *out = v;
    return 1;
  }
  if (s[0] == '0' && s[1] && is_all_digits(s)) {
    *out = strtoll(s + 1, NULL, 8);
    return 1;
  }
  if (is_all_digits(s)) {
    *out = strtoll(s, NULL, 10);
    return 1;
  }
  return 0;
}

static int int_literal_to_u64(const char *s, unsigned long long *out) {
  if (!s || !*s) return 0;
  if (is_hex_token(s)) {
    *out = strtoull(s, NULL, 16);
    return 1;
  }
  if (is_bin_token(s)) {
    unsigned long long v = 0;
    for (const char *p = s + 2; *p; p++) v = (v << 1) + (*p == '1' ? 1ULL : 0ULL);
    *out = v;
    return 1;
  }
  if (s[0] == '0' && s[1] && is_all_digits(s)) {
    *out = strtoull(s + 1, NULL, 8);
    return 1;
  }
  if (is_all_digits(s)) {
    *out = strtoull(s, NULL, 10);
    return 1;
  }
  return 0;
}

static int is_byte_literal_expr(AstNode *e) {
  if (!e || strcmp(e->kind, "Literal") != 0 || !e->text) return 0;
  if (is_float_token(e->text)) return 0;
  long long v = 0;
  if (!int_literal_to_ll(e->text, &v)) return 0;
  return v >= 0 && v <= 255;
}

static int is_byte_list_literal(AstNode *e) {
  if (!e || strcmp(e->kind, "ListLiteral") != 0) return 0;
  for (size_t i = 0; i < e->child_len; i++) {
    if (!is_byte_literal_expr(e->children[i])) return 0;
  }
  return 1;
}

static int is_numeric_type(const char *t) {
  return t && (strcmp(t, "byte") == 0 || strcmp(t, "int") == 0 || strcmp(t, "float") == 0);
}

static int const_numeric_value(AstNode *e, const char **type_out, long long *iv, double *fv);

static int const_numeric_value(AstNode *e, const char **type_out, long long *iv, double *fv) {
  if (!e || !type_out) return 0;
  if (strcmp(e->kind, "Literal") == 0 && e->text) {
    if (strcmp(e->text, "true") == 0 || strcmp(e->text, "false") == 0) return 0;
    long long v = 0;
    if (int_literal_to_ll(e->text, &v) && !is_float_token(e->text)) {
      *type_out = "int";
      *iv = v;
      return 1;
    }
    double f = 0.0;
    if (is_float_token(e->text)) {
      char *end = NULL;
      f = strtod(e->text, &end);
      if (end && *end == '\0' && isfinite(f)) {
        *type_out = "float";
        *fv = f;
        return 1;
      }
    }
  }
  if (strcmp(e->kind, "UnaryExpr") == 0 && e->text && strcmp(e->text, "-") == 0 && e->child_len > 0) {
    const char *t = NULL;
    long long iv2 = 0;
    double fv2 = 0.0;
    if (!const_numeric_value(e->children[0], &t, &iv2, &fv2)) return 0;
    if (strcmp(t, "float") == 0) {
      *type_out = "float";
      *fv = -fv2;
      return 1;
    }
    *type_out = t;
    *iv = -iv2;
    return 1;
  }
  if (strcmp(e->kind, "CastExpr") == 0 && e->child_len > 0 && e->text) {
    const char *t = NULL;
    long long iv2 = 0;
    double fv2 = 0.0;
    if (!const_numeric_value(e->children[0], &t, &iv2, &fv2)) return 0;
    const char *dst = e->text;
    if (strcmp(dst, "byte") == 0) {
      if (strcmp(t, "int") == 0) {
        if (iv2 < 0 || iv2 > 255) return 0;
        *type_out = "byte";
        *iv = iv2;
        return 1;
      }
      if (strcmp(t, "float") == 0) {
        long long iv3 = 0;
        if (!isfinite(fv2) || floor(fv2) != fv2) return 0;
        if (fv2 < 0 || fv2 > 255) return 0;
        iv3 = (long long)fv2;
        if ((double)iv3 != fv2) return 0;
        *type_out = "byte";
        *iv = iv3;
        return 1;
      }
    } else if (strcmp(dst, "int") == 0) {
      if (strcmp(t, "int") == 0) {
        *type_out = "int";
        *iv = iv2;
        return 1;
      }
      if (strcmp(t, "float") == 0) {
        if (!isfinite(fv2) || floor(fv2) != fv2) return 0;
        if (fv2 < (double)LLONG_MIN || fv2 > (double)LLONG_MAX) return 0;
        long long iv3 = (long long)fv2;
        if ((double)iv3 != fv2) return 0;
        *type_out = "int";
        *iv = iv3;
        return 1;
      }
    } else if (strcmp(dst, "float") == 0) {
      if (strcmp(t, "int") == 0 || strcmp(t, "byte") == 0) {
        *type_out = "float";
        *fv = (double)iv2;
        return 1;
      }
      if (strcmp(t, "float") == 0) {
        *type_out = "float";
        *fv = fv2;
        return 1;
      }
    }
  }
  return 0;
}

static char *infer_expr_type(Analyzer *a, AstNode *e, Scope *scope, int *ok);

static int check_method_arity(Analyzer *a, AstNode *e, const char *recv_t, const char *method, int argc) {
  if (!recv_t || !method) return 1;
  int min = -1;
  int max = -1;
  if (strcmp(recv_t, "string") == 0) {
    if (strcmp(method, "length") == 0 || strcmp(method, "isEmpty") == 0 || strcmp(method, "toString") == 0 ||
        strcmp(method, "toInt") == 0 || strcmp(method, "toFloat") == 0 || strcmp(method, "toUpper") == 0 ||
        strcmp(method, "toLower") == 0 || strcmp(method, "toUtf8Bytes") == 0 || strcmp(method, "trim") == 0 ||
        strcmp(method, "trimStart") == 0 || strcmp(method, "trimEnd") == 0) {
      min = max = 0;
    } else if (strcmp(method, "concat") == 0 || strcmp(method, "indexOf") == 0 || strcmp(method, "startsWith") == 0 ||
               strcmp(method, "endsWith") == 0 || strcmp(method, "split") == 0) {
      min = max = 1;
    } else if (strcmp(method, "substring") == 0 || strcmp(method, "replace") == 0) {
      min = max = 2;
    }
  } else if (strcmp(recv_t, "TextFile") == 0 || strcmp(recv_t, "BinaryFile") == 0) {
    if (strcmp(method, "close") == 0 || strcmp(method, "tell") == 0 || strcmp(method, "size") == 0 || strcmp(method, "name") == 0) {
      min = max = 0;
    } else if (strcmp(method, "read") == 0 || strcmp(method, "write") == 0 || strcmp(method, "seek") == 0) {
      min = max = 1;
    }
  } else if (strcmp(recv_t, "int") == 0) {
    if (strcmp(method, "toByte") == 0 || strcmp(method, "toFloat") == 0 || strcmp(method, "toString") == 0 ||
        strcmp(method, "toBytes") == 0 || strcmp(method, "abs") == 0 || strcmp(method, "sign") == 0) {
      min = max = 0;
    }
  } else if (strcmp(recv_t, "byte") == 0) {
    if (strcmp(method, "toInt") == 0 || strcmp(method, "toFloat") == 0 || strcmp(method, "toString") == 0) {
      min = max = 0;
    }
  } else if (strcmp(recv_t, "float") == 0) {
    if (strcmp(method, "toInt") == 0 || strcmp(method, "toString") == 0 || strcmp(method, "toBytes") == 0 ||
        strcmp(method, "abs") == 0 || strcmp(method, "isNaN") == 0 || strcmp(method, "isInfinite") == 0 ||
        strcmp(method, "isFinite") == 0) {
      min = max = 0;
    }
  } else if (strcmp(recv_t, "glyph") == 0) {
    if (strcmp(method, "toString") == 0 || strcmp(method, "toInt") == 0 || strcmp(method, "toUtf8Bytes") == 0 ||
        strcmp(method, "isLetter") == 0 || strcmp(method, "isDigit") == 0 || strcmp(method, "isWhitespace") == 0 ||
        strcmp(method, "isUpper") == 0 || strcmp(method, "isLower") == 0 || strcmp(method, "toUpper") == 0 ||
        strcmp(method, "toLower") == 0) {
      min = max = 0;
    }
  } else if (strncmp(recv_t, "list<", 5) == 0) {
    if (strcmp(method, "length") == 0 || strcmp(method, "isEmpty") == 0 || strcmp(method, "pop") == 0 ||
        strcmp(method, "sort") == 0 || strcmp(method, "concat") == 0 || strcmp(method, "toUtf8String") == 0) {
      min = max = 0;
    } else if (strcmp(method, "push") == 0 || strcmp(method, "contains") == 0 || strcmp(method, "join") == 0) {
      min = max = 1;
    }
  } else if (strncmp(recv_t, "map<", 4) == 0) {
    if (strcmp(method, "length") == 0 || strcmp(method, "isEmpty") == 0 || strcmp(method, "keys") == 0 ||
        strcmp(method, "values") == 0) {
      min = max = 0;
    } else if (strcmp(method, "containsKey") == 0 || strcmp(method, "remove") == 0) {
      min = max = 1;
    }
  }

  if (min == -1) return 1;
  if (argc < min || argc > max) {
    set_diag(a->diag, a->file, e->line, e->col, "E1003", "ARITY_MISMATCH", "arity mismatch");
    return 0;
  }
  return 1;
}

static int check_call_args(Analyzer *a, AstNode *e, Scope *scope, int *ok) {
  if (!e || strcmp(e->kind, "CallExpr") != 0) return 1;
  for (int i = 1; i < (int)e->child_len; i += 1) {
    char *t = infer_expr_type(a, e->children[i], scope, ok);
    if (!(*ok)) {
      free(t);
      return 0;
    }
    free(t);
  }
  return 1;
}

static char *ir_type_elem_for_index(const char *t);
static char *ir_type_map_key(const char *t);
static char *ir_type_map_value(const char *t);

static char *method_ret_type(const char *recv_t, const char *m) {
  if (!recv_t || !m) return NULL;
  if (strcmp(recv_t, "int") == 0) {
    if (strcmp(m, "toByte") == 0) return strdup("byte");
    if (strcmp(m, "toFloat") == 0) return strdup("float");
    if (strcmp(m, "toString") == 0) return strdup("string");
    if (strcmp(m, "toBytes") == 0) return strdup("list<byte>");
    if (strcmp(m, "abs") == 0 || strcmp(m, "sign") == 0) return strdup("int");
  } else if (strcmp(recv_t, "byte") == 0) {
    if (strcmp(m, "toInt") == 0) return strdup("int");
    if (strcmp(m, "toFloat") == 0) return strdup("float");
    if (strcmp(m, "toString") == 0) return strdup("string");
  } else if (strcmp(recv_t, "float") == 0) {
    if (strcmp(m, "toInt") == 0) return strdup("int");
    if (strcmp(m, "toString") == 0) return strdup("string");
    if (strcmp(m, "toBytes") == 0) return strdup("list<byte>");
    if (strcmp(m, "abs") == 0) return strdup("float");
    if (strcmp(m, "isNaN") == 0 || strcmp(m, "isInfinite") == 0 || strcmp(m, "isFinite") == 0) return strdup("bool");
  } else if (strcmp(recv_t, "glyph") == 0) {
    if (strcmp(m, "toString") == 0) return strdup("string");
    if (strcmp(m, "toInt") == 0) return strdup("int");
    if (strcmp(m, "toUtf8Bytes") == 0) return strdup("list<byte>");
    if (strcmp(m, "isLetter") == 0 || strcmp(m, "isDigit") == 0 || strcmp(m, "isWhitespace") == 0 ||
        strcmp(m, "isUpper") == 0 || strcmp(m, "isLower") == 0)
      return strdup("bool");
    if (strcmp(m, "toUpper") == 0 || strcmp(m, "toLower") == 0) return strdup("glyph");
  } else if (strcmp(recv_t, "string") == 0) {
    if (strcmp(m, "length") == 0) return strdup("int");
    if (strcmp(m, "isEmpty") == 0) return strdup("bool");
    if (strcmp(m, "toString") == 0) return strdup("string");
    if (strcmp(m, "toInt") == 0) return strdup("int");
    if (strcmp(m, "toFloat") == 0) return strdup("float");
    if (strcmp(m, "substring") == 0) return strdup("string");
    if (strcmp(m, "indexOf") == 0) return strdup("int");
    if (strcmp(m, "startsWith") == 0 || strcmp(m, "endsWith") == 0) return strdup("bool");
    if (strcmp(m, "split") == 0) return strdup("list<string>");
    if (strcmp(m, "trim") == 0 || strcmp(m, "trimStart") == 0 || strcmp(m, "trimEnd") == 0) return strdup("string");
    if (strcmp(m, "replace") == 0) return strdup("string");
    if (strcmp(m, "toUpper") == 0 || strcmp(m, "toLower") == 0) return strdup("string");
    if (strcmp(m, "toUtf8Bytes") == 0) return strdup("list<byte>");
  } else if (strncmp(recv_t, "list<", 5) == 0) {
    char *et = ir_type_elem_for_index(recv_t);
    if (et && strcmp(et, "byte") == 0 && strcmp(m, "toUtf8String") == 0) {
      free(et);
      return strdup("string");
    }
    if (et && strcmp(et, "string") == 0 && (strcmp(m, "join") == 0 || strcmp(m, "concat") == 0)) {
      free(et);
      return strdup("string");
    }
    free(et);
    if (strcmp(m, "length") == 0) return strdup("int");
    if (strcmp(m, "isEmpty") == 0) return strdup("bool");
    if (strcmp(m, "push") == 0) return strdup("int");
    if (strcmp(m, "contains") == 0) return strdup("bool");
    if (strcmp(m, "sort") == 0) return strdup("int");
    if (strcmp(m, "view") == 0 || strcmp(m, "slice") == 0) {
      char *et2 = ir_type_elem_for_index(recv_t);
      char *out = NULL;
      if (et2) out = str_printf("%s<%s>", m, et2);
      free(et2);
      return out ? out : strdup("unknown");
    }
  } else if (strncmp(recv_t, "slice<", 6) == 0) {
    if (strcmp(m, "length") == 0) return strdup("int");
    if (strcmp(m, "isEmpty") == 0) return strdup("bool");
    if (strcmp(m, "slice") == 0) {
      char *et2 = ir_type_elem_for_index(recv_t);
      char *out = NULL;
      if (et2) out = str_printf("slice<%s>", et2);
      free(et2);
      return out ? out : strdup("unknown");
    }
  } else if (strncmp(recv_t, "view<", 5) == 0) {
    if (strcmp(m, "length") == 0) return strdup("int");
    if (strcmp(m, "isEmpty") == 0) return strdup("bool");
    if (strcmp(m, "view") == 0) {
      char *et2 = ir_type_elem_for_index(recv_t);
      char *out = NULL;
      if (et2) out = str_printf("view<%s>", et2);
      free(et2);
      return out ? out : strdup("unknown");
    }
  } else if (strncmp(recv_t, "map<", 4) == 0) {
    if (strcmp(m, "length") == 0) return strdup("int");
    if (strcmp(m, "isEmpty") == 0) return strdup("bool");
    if (strcmp(m, "containsKey") == 0) return strdup("bool");
    if (strcmp(m, "remove") == 0) return strdup("bool");
    if (strcmp(m, "keys") == 0) {
      char *kt = ir_type_map_key(recv_t);
      char *out = str_printf("list<%s>", kt ? kt : "unknown");
      if (kt) free(kt);
      return out ? out : strdup("unknown");
    }
    if (strcmp(m, "values") == 0) {
      char *vt = ir_type_map_value(recv_t);
      char *out = str_printf("list<%s>", vt ? vt : "unknown");
      if (vt) free(vt);
      return out ? out : strdup("unknown");
    }
  } else if (strcmp(recv_t, "JSONValue") == 0) {
    if (strcmp(m, "isNull") == 0 || strcmp(m, "isBool") == 0 || strcmp(m, "isNumber") == 0 ||
        strcmp(m, "isString") == 0 || strcmp(m, "isArray") == 0 || strcmp(m, "isObject") == 0)
      return strdup("bool");
    if (strcmp(m, "asBool") == 0) return strdup("bool");
    if (strcmp(m, "asNumber") == 0) return strdup("float");
    if (strcmp(m, "asString") == 0) return strdup("string");
    if (strcmp(m, "asArray") == 0) return strdup("list<JSONValue>");
    if (strcmp(m, "asObject") == 0) return strdup("map<string,JSONValue>");
  }
  return NULL;
}

static char *infer_call_type(Analyzer *a, AstNode *e, Scope *scope, int *ok) {
  (void)scope;
  if (e->child_len == 0) return strdup("unknown");
  AstNode *callee = e->children[0];
  if (strcmp(callee->kind, "Identifier") == 0) {
    FnSig *f = find_fn(a, callee->text ? callee->text : "");
    if (callee->text && (strcmp(callee->text, "Exception") == 0 || strcmp(callee->text, "RuntimeException") == 0)) {
      const char *n = callee->text;
      char msg[160];
      snprintf(msg, sizeof(msg), "%s is not callable; use %s.clone()", n, n);
      set_diag(a->diag, a->file, e->line, e->col, "E2001", "UNRESOLVED_NAME", msg);
      *ok = 0;
      return NULL;
    }
    if (!f) {
      set_diag(a->diag, a->file, e->line, e->col, "E2001", "UNRESOLVED_NAME", "unknown function");
      *ok = 0;
      return NULL;
    }
    int argc = (int)e->child_len - 1;
    if (!f->variadic) {
      if (argc != f->param_count) {
        set_diag(a->diag, a->file, e->line, e->col, "E1003", "ARITY_MISMATCH", "arity mismatch");
        *ok = 0;
        return NULL;
      }
    } else {
      if (argc < f->fixed_count) {
        set_diag(a->diag, a->file, e->line, e->col, "E1003", "ARITY_MISMATCH", "arity mismatch");
        *ok = 0;
        return NULL;
      }
    }
    if (!check_call_args(a, e, scope, ok)) return NULL;
    return strdup(f->ret_type);
  }
  if (strcmp(callee->kind, "MemberExpr") == 0 && callee->child_len > 0) {
    AstNode *target = callee->children[0];
    if (strcmp(target->kind, "Identifier") == 0) {
      ProtoInfo *proto = proto_find(a->protos, target->text ? target->text : "");
      if (proto) {
        const char *mname = callee->text ? callee->text : "";
        int argc = (int)e->child_len - 1;
        if (strcmp(mname, "clone") == 0) {
          if (argc != 0) {
            set_diag(a->diag, a->file, e->line, e->col, "E1003", "ARITY_MISMATCH", "arity mismatch for 'clone'");
            *ok = 0;
            return NULL;
          }
          return strdup(proto->name ? proto->name : "unknown");
        }
        ProtoMethod *pm = proto_find_method(a->protos, proto->name, mname);
        if (!pm) {
          set_diag(a->diag, a->file, e->line, e->col, "E2001", "UNRESOLVED_NAME", "unknown prototype method");
          *ok = 0;
          return NULL;
        }
        int expected = pm->param_count + 1;
        if (argc != expected) {
          set_diag(a->diag, a->file, e->line, e->col, "E1003", "ARITY_MISMATCH", "arity mismatch");
          *ok = 0;
          return NULL;
        }
        if (!check_call_args(a, e, scope, ok)) return NULL;
        return strdup(pm->ret_type ? pm->ret_type : "unknown");
      }
      ImportNamespace *ns = find_namespace(a, target->text ? target->text : "");
      if (ns) {
        if (ns->is_proto) {
          ProtoInfo *proto = proto_find(a->protos, ns->module);
          if (!proto) {
            set_diag(a->diag, a->file, e->line, e->col, "E2001", "UNRESOLVED_NAME", "unknown prototype");
            *ok = 0;
            return NULL;
          }
          const char *mname = callee->text ? callee->text : "";
          int argc = (int)e->child_len - 1;
          if (strcmp(mname, "clone") == 0) {
            if (argc != 0) {
              set_diag(a->diag, a->file, e->line, e->col, "E1003", "ARITY_MISMATCH", "arity mismatch for 'clone'");
              *ok = 0;
              return NULL;
            }
            return strdup(proto->name ? proto->name : "unknown");
          }
          ProtoMethod *pm = proto_find_method(a->protos, proto->name, mname);
          if (!pm) {
            set_diag(a->diag, a->file, e->line, e->col, "E2001", "UNRESOLVED_NAME", "unknown prototype method");
            *ok = 0;
            return NULL;
          }
          int expected = pm->param_count + 1;
          if (argc != expected) {
            set_diag(a->diag, a->file, e->line, e->col, "E1003", "ARITY_MISMATCH", "arity mismatch");
            *ok = 0;
            return NULL;
          }
          if (!check_call_args(a, e, scope, ok)) return NULL;
          return strdup(pm->ret_type ? pm->ret_type : "unknown");
        }
        RegFn *rf = registry_find_fn(a->registry, ns->module, callee->text ? callee->text : "");
        if (!rf) {
          set_diag(a->diag, a->file, e->line, e->col, "E2001", "UNRESOLVED_NAME", "unknown module symbol");
          *ok = 0;
          return NULL;
        }
        int argc = (int)e->child_len - 1;
        if (argc != rf->param_count) {
          set_diag(a->diag, a->file, e->line, e->col, "E1003", "ARITY_MISMATCH", "arity mismatch");
          *ok = 0;
          return NULL;
        }
        if (!check_call_args(a, e, scope, ok)) return NULL;
        return strdup(rf->ret_type);
      }
    }
    if (callee->text) {
      int argc = (int)e->child_len - 1;
      char *tt = infer_expr_type(a, target, scope, ok);
      if (tt) {
        if (proto_find(a->protos, tt)) {
          ProtoMethod *pm = proto_find_method(a->protos, tt, callee->text);
          if (!pm) {
            set_diag(a->diag, a->file, e->line, e->col, "E2001", "UNRESOLVED_NAME", "unknown prototype method");
            *ok = 0;
            free(tt);
            return NULL;
          }
          if (argc != pm->param_count) {
            set_diag(a->diag, a->file, e->line, e->col, "E1003", "ARITY_MISMATCH", "arity mismatch");
            *ok = 0;
            free(tt);
            return NULL;
          }
          char *ret = strdup(pm->ret_type ? pm->ret_type : "unknown");
          free(tt);
          return ret;
        }
        if (strcmp(tt, "string") == 0 && strcmp(callee->text, "view") == 0) {
          if (!(argc == 0 || argc == 2)) {
            set_diag(a->diag, a->file, e->line, e->col, "E1003", "ARITY_MISMATCH", "arity mismatch");
            *ok = 0;
            free(tt);
            return NULL;
          }
        } else if (strncmp(tt, "list<", 5) == 0 && (strcmp(callee->text, "view") == 0 || strcmp(callee->text, "slice") == 0)) {
          if (argc != 2) {
            set_diag(a->diag, a->file, e->line, e->col, "E1003", "ARITY_MISMATCH", "arity mismatch");
            *ok = 0;
            free(tt);
            return NULL;
          }
        } else if (strncmp(tt, "slice<", 6) == 0 && strcmp(callee->text, "slice") == 0) {
          if (argc != 2) {
            set_diag(a->diag, a->file, e->line, e->col, "E1003", "ARITY_MISMATCH", "arity mismatch");
            *ok = 0;
            free(tt);
            return NULL;
          }
        } else if (strncmp(tt, "view<", 5) == 0 && strcmp(callee->text, "view") == 0) {
          if (argc != 2) {
            set_diag(a->diag, a->file, e->line, e->col, "E1003", "ARITY_MISMATCH", "arity mismatch");
            *ok = 0;
            free(tt);
            return NULL;
          }
        }
        if (!check_method_arity(a, e, tt, callee->text, argc)) {
          *ok = 0;
          free(tt);
          return NULL;
        }
        if (!check_call_args(a, e, scope, ok)) {
          free(tt);
          return NULL;
        }
        char *ret = method_ret_type(tt, callee->text);
        free(tt);
        if (ret) return ret;
      }
    }
  }
  return strdup("unknown");
}

static char *infer_expr_type(Analyzer *a, AstNode *e, Scope *scope, int *ok) {
  if (!e) return strdup("unknown");
  if (strcmp(e->kind, "Literal") == 0) {
    if (e->text && (strcmp(e->text, "true") == 0 || strcmp(e->text, "false") == 0)) return strdup("bool");
    if (e->text && (is_all_digits(e->text) || is_hex_token(e->text) || is_bin_token(e->text) || is_float_token(e->text)))
      return strdup(is_float_token(e->text) ? "float" : "int");
    return strdup("string");
  }
  if (strcmp(e->kind, "CastExpr") == 0) {
    const char *dst = e->text ? e->text : "";
    if (!is_numeric_type(dst)) {
      set_diag(a->diag, a->file, e->line, e->col, "E3001", "TYPE_MISMATCH_ASSIGNMENT", "cast target must be numeric type");
      *ok = 0;
      return NULL;
    }
    if (e->child_len == 0) return strdup(dst);
    int ok1 = 1;
    char *src = infer_expr_type(a, e->children[0], scope, &ok1);
    if (!ok1 || !src) {
      free(src);
      *ok = 0;
      return NULL;
    }
    if (!is_numeric_type(src)) {
      set_diag(a->diag, a->file, e->line, e->col, "E3001", "TYPE_MISMATCH_ASSIGNMENT", "numeric cast requires numeric source");
      free(src);
      *ok = 0;
      return NULL;
    }
    int representable = 0;
    if (strcmp(src, dst) == 0) {
      representable = 1;
    } else if (strcmp(src, "byte") == 0 && (strcmp(dst, "int") == 0 || strcmp(dst, "float") == 0 || strcmp(dst, "byte") == 0)) {
      representable = 1;
    } else if (strcmp(src, "int") == 0 && strcmp(dst, "float") == 0) {
      representable = 1;
    } else {
      const char *ct = NULL;
      long long iv = 0;
      double fv = 0.0;
      if (const_numeric_value(e->children[0], &ct, &iv, &fv)) {
        if (strcmp(dst, "byte") == 0) {
          if ((strcmp(ct, "int") == 0 || strcmp(ct, "byte") == 0) && iv >= 0 && iv <= 255) representable = 1;
          if (strcmp(ct, "float") == 0) {
            if (isfinite(fv) && floor(fv) == fv && fv >= 0 && fv <= 255 && (double)((long long)fv) == fv) representable = 1;
          }
        } else if (strcmp(dst, "int") == 0) {
          if ((strcmp(ct, "int") == 0 || strcmp(ct, "byte") == 0)) representable = 1;
          if (strcmp(ct, "float") == 0) {
            if (isfinite(fv) && floor(fv) == fv && fv >= (double)LLONG_MIN && fv <= (double)LLONG_MAX &&
                (double)((long long)fv) == fv)
              representable = 1;
          }
        } else if (strcmp(dst, "float") == 0) {
          representable = 1;
        }
      }
    }
    if (!representable) {
      set_diag(a->diag, a->file, e->line, e->col, "E3001", "TYPE_MISMATCH_ASSIGNMENT", "numeric cast not representable");
      free(src);
      *ok = 0;
      return NULL;
    }
    free(src);
    return strdup(dst);
  }
  if (strcmp(e->kind, "Identifier") == 0) {
    Sym *sym = scope_lookup_sym(scope, e->text ? e->text : "");
    if (sym) {
      return strdup(sym->type ? sym->type : "unknown");
    }
    if (e->text && strcmp(e->text, "Sys") == 0) return strdup("Sys");
    if (e->text && find_namespace(a, e->text)) return strdup("module");
    set_diag(a->diag, a->file, e->line, e->col, "E2001", "UNRESOLVED_NAME", "unknown identifier");
    *ok = 0;
    return NULL;
  }
  if (strcmp(e->kind, "UnaryExpr") == 0 || strcmp(e->kind, "PostfixExpr") == 0 || strcmp(e->kind, "MemberExpr") == 0) {
    if (strcmp(e->kind, "MemberExpr") == 0 && e->child_len > 0) {
      AstNode *target = e->children[0];
      if (strcmp(target->kind, "Identifier") == 0) {
        ImportNamespace *ns = find_namespace(a, target->text ? target->text : "");
        if (ns && !ns->is_proto) {
          RegConst *rc = registry_find_const(a->registry, ns->module, e->text ? e->text : "");
          if (rc && rc->type) {
            if (strcmp(rc->type, "float") == 0) return strdup("float");
            if (strcmp(rc->type, "int") == 0) return strdup("int");
            if (strcmp(rc->type, "string") == 0) return strdup("string");
            if (strcmp(rc->type, "TextFile") == 0 || strcmp(rc->type, "BinaryFile") == 0) return strdup(rc->type);
          }
        }
      }
      int ok2 = 1;
      char *tt = infer_expr_type(a, target, scope, &ok2);
      if (!ok2) {
        free(tt);
        *ok = 0;
        return NULL;
      }
      if (tt && proto_find(a->protos, tt)) {
        ProtoField *pf = proto_find_field(a->protos, tt, e->text ? e->text : "");
        if (pf && pf->type) {
          char *ret = strdup(pf->type);
          free(tt);
          return ret;
        }
      }
      free(tt);
    }
    if (e->child_len > 0) return infer_expr_type(a, e->children[0], scope, ok);
    return strdup("unknown");
  }
  if (strcmp(e->kind, "BinaryExpr") == 0) {
    if (e->child_len >= 2) {
      int ok1 = 1, ok2 = 1;
      char *lt = infer_expr_type(a, e->children[0], scope, &ok1);
      char *rt = infer_expr_type(a, e->children[1], scope, &ok2);
      if (!ok1 || !ok2) {
        free(lt);
        free(rt);
        *ok = 0;
        return NULL;
      }
      if (lt && rt && strcmp(lt, rt) != 0) {
        set_diag(a->diag, a->file, e->line, e->col, "E3001", "TYPE_MISMATCH_ASSIGNMENT", "incompatible operands");
        free(lt);
        free(rt);
        *ok = 0;
        return NULL;
      }
      if (e->text && (strcmp(e->text, "&&") == 0 || strcmp(e->text, "||") == 0)) {
        if (lt && strcmp(lt, "bool") != 0) {
          set_diag(a->diag, a->file, e->line, e->col, "E3001", "TYPE_MISMATCH_ASSIGNMENT", "logical operators require bool operands");
          free(lt);
          free(rt);
          *ok = 0;
          return NULL;
        }
      }
      if (e->text && (strcmp(e->text, "+") == 0 || strcmp(e->text, "-") == 0 || strcmp(e->text, "*") == 0 ||
                      strcmp(e->text, "/") == 0 || strcmp(e->text, "%") == 0)) {
        if (lt && !(strcmp(lt, "int") == 0 || strcmp(lt, "float") == 0 || strcmp(lt, "byte") == 0 || strcmp(lt, "glyph") == 0)) {
          set_diag(a->diag, a->file, e->line, e->col, "E3001", "TYPE_MISMATCH_ASSIGNMENT", "arithmetic operators require numeric operands");
          free(lt);
          free(rt);
          *ok = 0;
          return NULL;
        }
      }
      if (e->text && (strcmp(e->text, "&") == 0 || strcmp(e->text, "|") == 0 || strcmp(e->text, "^") == 0 ||
                      strcmp(e->text, "<<") == 0 || strcmp(e->text, ">>") == 0)) {
        if (lt && !(strcmp(lt, "int") == 0 || strcmp(lt, "byte") == 0)) {
          set_diag(a->diag, a->file, e->line, e->col, "E3001", "TYPE_MISMATCH_ASSIGNMENT", "bitwise operators require int or byte operands");
          free(lt);
          free(rt);
          *ok = 0;
          return NULL;
        }
      }
      free(lt);
      free(rt);
    }
    if (e->text && (strcmp(e->text, "==") == 0 || strcmp(e->text, "!=") == 0 || strcmp(e->text, "<") == 0 ||
                    strcmp(e->text, "<=") == 0 || strcmp(e->text, ">") == 0 || strcmp(e->text, ">=") == 0 ||
                    strcmp(e->text, "&&") == 0 || strcmp(e->text, "||") == 0)) {
      return strdup("bool");
    }
    if (e->child_len > 0) return infer_expr_type(a, e->children[0], scope, ok);
    return strdup("unknown");
  }
  if (strcmp(e->kind, "ConditionalExpr") == 0) {
    if (e->child_len >= 2) return infer_expr_type(a, e->children[1], scope, ok);
    return strdup("unknown");
  }
  if (strcmp(e->kind, "CallExpr") == 0) return infer_call_type(a, e, scope, ok);
  if (strcmp(e->kind, "IndexExpr") == 0) {
    char *tt = infer_expr_type(a, e->child_len > 0 ? e->children[0] : NULL, scope, ok);
    if (!tt) return NULL;
    char *out = NULL;
    if (strncmp(tt, "list<", 5) == 0 || strncmp(tt, "slice<", 6) == 0 || strncmp(tt, "view<", 5) == 0) {
      char *lt = strchr(tt, '<');
      char *gt = strrchr(tt, '>');
      if (lt && gt && gt > lt + 1) out = dup_range(tt, (size_t)(lt - tt + 1), (size_t)(gt - tt));
    } else if (strncmp(tt, "map<", 4) == 0) {
      char *comma = strchr(tt, ',');
      char *gt = strrchr(tt, '>');
      if (comma && gt && gt > comma + 1) out = dup_range(tt, (size_t)(comma - tt + 1), (size_t)(gt - tt));
    } else if (strcmp(tt, "string") == 0) {
      out = strdup("glyph");
    }
    free(tt);
    return out ? out : strdup("unknown");
  }
  if (strcmp(e->kind, "ListLiteral") == 0) {
    if (e->child_len == 0) return strdup("list<void>");
    char *it = infer_expr_type(a, e->children[0], scope, ok);
    if (!it) return NULL;
    size_t n = strlen(it) + 8;
    char *out = (char *)malloc(n);
    if (out) snprintf(out, n, "list<%s>", it);
    free(it);
    return out ? out : strdup("unknown");
  }
  if (strcmp(e->kind, "MapLiteral") == 0) {
    if (e->child_len == 0) return strdup("map<void,void>");
    AstNode *pair = e->children[0];
    if (!pair || pair->child_len < 2) return strdup("map<void,void>");
    char *k = infer_expr_type(a, pair->children[0], scope, ok);
    char *v = infer_expr_type(a, pair->children[1], scope, ok);
    if (!k || !v) {
      free(k);
      free(v);
      return NULL;
    }
    size_t n = strlen(k) + strlen(v) + 8;
    char *out = (char *)malloc(n);
    if (out) snprintf(out, n, "map<%s,%s>", k, v);
    free(k);
    free(v);
    return out ? out : strdup("unknown");
  }
  return strdup("unknown");
}

static int check_list_pop(Analyzer *a, AstNode *e, Scope *scope) {
  if (!e || strcmp(e->kind, "CallExpr") != 0 || e->child_len == 0) return 1;
  AstNode *callee = e->children[0];
  if (!callee || strcmp(callee->kind, "MemberExpr") != 0 || !callee->text) return 1;
  if (strcmp(callee->text, "pop") != 0) return 1;
  if (callee->child_len == 0) return 1;
  AstNode *target = callee->children[0];
  if (!target || strcmp(target->kind, "Identifier") != 0) return 1;
  Sym *sym = scope_lookup_sym(scope, target->text ? target->text : "");
  if (!sym || sym->known_list_len != 0 || !sym->type) return 1;
  if (strncmp(sym->type, "list<", 5) != 0) return 1;
  set_diag(a->diag, a->file, target->line, target->col, "E3005", "STATIC_EMPTY_POP", "pop on statically empty list");
  return 0;
}

static char *infer_assignable_type(Analyzer *a, AstNode *lhs, Scope *scope, int *ok) {
  if (!lhs) return strdup("unknown");
  if (strcmp(lhs->kind, "Identifier") == 0) return strdup(scope_lookup(scope, lhs->text ? lhs->text : ""));
  if (strcmp(lhs->kind, "IndexExpr") == 0) return infer_expr_type(a, lhs, scope, ok);
  if (strcmp(lhs->kind, "MemberExpr") == 0 && lhs->child_len > 0) {
    int ok2 = 1;
    char *tt = infer_expr_type(a, lhs->children[0], scope, &ok2);
    if (!ok2) {
      free(tt);
      *ok = 0;
      return NULL;
    }
    if (tt && proto_find(a->protos, tt)) {
      ProtoField *pf = proto_find_field(a->protos, tt, lhs->text ? lhs->text : "");
      if (pf && pf->type) {
        char *ret = strdup(pf->type);
        free(tt);
        return ret;
      }
    }
    free(tt);
  }
  return strdup("unknown");
}

static int analyze_stmt(Analyzer *a, AstNode *st, Scope *scope);

static int analyze_block(Analyzer *a, AstNode *blk, Scope *parent) {
  Scope local;
  memset(&local, 0, sizeof(local));
  local.parent = parent;
  for (size_t i = 0; i < blk->child_len; i++) {
    if (!analyze_stmt(a, blk->children[i], &local)) {
      free_syms(local.syms);
      return 0;
    }
  }
  free_syms(local.syms);
  return 1;
}

static int analyze_switch_termination(Analyzer *a, AstNode *sw) {
  for (size_t i = 0; i < sw->child_len; i++) {
    AstNode *c = sw->children[i];
    if (strcmp(c->kind, "CaseClause") != 0 && strcmp(c->kind, "DefaultClause") != 0) continue;
    size_t start = (strcmp(c->kind, "CaseClause") == 0 && c->child_len > 0) ? 1 : 0;
    if (c->child_len <= start) {
      set_diag(a->diag, a->file, c->line, c->col, "E3003", "SWITCH_CASE_NO_TERMINATION", "case without explicit termination");
      return 0;
    }
    AstNode *last = c->children[c->child_len - 1];
    if (!ast_is_terminator(last)) {
      set_diag(a->diag, a->file, c->line, c->col, "E3003", "SWITCH_CASE_NO_TERMINATION", "case without explicit termination");
      return 0;
    }
  }
  return 1;
}

static int analyze_stmt(Analyzer *a, AstNode *st, Scope *scope) {
  if (strcmp(st->kind, "Block") == 0) return analyze_block(a, st, scope);
  if (strcmp(st->kind, "VarDecl") == 0) {
    AstNode *tn = ast_child_kind(st, "Type");
    AstNode *init = ast_last_child(st);
    if (tn && init && strcmp(init->kind, "Type") != 0) {
      int ok = 1;
      char *lhs = canon_type(tn->text);
      char *rhs = infer_expr_type(a, init, scope, &ok);
      if (!ok) {
        free(lhs);
        free(rhs);
        return 0;
      }
      if (lhs && rhs && strcmp(lhs, rhs) != 0 && strcmp(rhs, "unknown") != 0) {
        int allow_sub = proto_is_subtype(a->protos, rhs, lhs);
        int empty_map_init =
            (init && strcmp(init->kind, "MapLiteral") == 0 && init->child_len == 0 && lhs && strncmp(lhs, "map<", 4) == 0);
        int empty_list_init =
            (init && strcmp(init->kind, "ListLiteral") == 0 && init->child_len == 0 && lhs && strncmp(lhs, "list<", 5) == 0);
        int allow_byte_lit = (lhs && strcmp(lhs, "byte") == 0 && is_byte_literal_expr(init));
        int allow_byte_list = (lhs && strcmp(lhs, "list<byte>") == 0 && is_byte_list_literal(init));
        if (!allow_sub && !empty_map_init && !empty_list_init && !allow_byte_lit && !allow_byte_list) {
        char msg[160];
        snprintf(msg, sizeof(msg), "cannot assign %s to %s", rhs, lhs);
        set_diag(a->diag, a->file, st->line, st->col, "E3001", "TYPE_MISMATCH_ASSIGNMENT", msg);
        free(lhs);
        free(rhs);
        return 0;
        }
      }
      if (lhs) {
        int alias_self = expr_is_self_alias(init, scope);
        int known_len = -1;
        if (init && strcmp(init->kind, "ListLiteral") == 0) known_len = (int)init->child_len;
        if (!scope_define_alias(scope, st->text ? st->text : "", lhs, known_len, 1, alias_self)) {
          free(lhs);
          free(rhs);
          return 0;
        }
      }
      free(lhs);
      free(rhs);
      return 1;
    }
    if (tn) {
      char *lhs = canon_type(tn->text);
      if (!lhs || !scope_define(scope, st->text ? st->text : "", lhs, -1, 1)) {
        free(lhs);
        return 0;
      }
      free(lhs);
    } else if (init && strcmp(init->kind, "Type") != 0) {
      int ok = 1;
      char *rhs = infer_expr_type(a, init, scope, &ok);
      if (!ok) {
        free(rhs);
        return 0;
      }
      if ((init->kind && (strcmp(init->kind, "ListLiteral") == 0 || strcmp(init->kind, "MapLiteral") == 0)) &&
          rhs && (strcmp(rhs, "list<void>") == 0 || strcmp(rhs, "map<void,void>") == 0)) {
        set_diag(a->diag, a->file, init->line, init->col, "E3006", "MISSING_TYPE_CONTEXT",
                 "empty literal requires explicit type context");
        free(rhs);
        return 0;
      }
      int alias_self = expr_is_self_alias(init, scope);
      if (!scope_define_alias(scope, st->text ? st->text : "", rhs ? rhs : "unknown", -1, 1, alias_self)) {
        free(rhs);
        return 0;
      }
      free(rhs);
    }
    return 1;
  }
  if (strcmp(st->kind, "AssignStmt") == 0 && st->child_len >= 2) {
    if (st->children[0] && strcmp(st->children[0]->kind, "IndexExpr") == 0 && st->children[0]->child_len > 0) {
      int tok = 1;
      char *target_t = infer_expr_type(a, st->children[0]->children[0], scope, &tok);
      if (!tok) {
        free(target_t);
        return 0;
      }
      if (target_t && (strcmp(target_t, "string") == 0 || strncmp(target_t, "view<", 5) == 0)) {
        set_diag(a->diag, a->file, st->line, st->col, "E3004", "IMMUTABLE_INDEX_WRITE",
                 "cannot assign through immutable index access");
        free(target_t);
        return 0;
      }
      free(target_t);
    }
    int ok = 1;
    char *lhs = infer_assignable_type(a, st->children[0], scope, &ok);
    char *rhs = infer_expr_type(a, st->children[1], scope, &ok);
    if (!ok) {
      free(lhs);
      free(rhs);
      return 0;
    }
    if ((!lhs || strcmp(lhs, "unknown") == 0) && st->children[1] &&
        (strcmp(st->children[1]->kind, "ListLiteral") == 0 || strcmp(st->children[1]->kind, "MapLiteral") == 0) &&
        rhs && (strcmp(rhs, "list<void>") == 0 || strcmp(rhs, "map<void,void>") == 0)) {
      set_diag(a->diag, a->file, st->children[1]->line, st->children[1]->col, "E3006", "MISSING_TYPE_CONTEXT",
               "empty literal requires explicit type context");
      free(lhs);
      free(rhs);
      return 0;
    }
    if (lhs && rhs && strcmp(lhs, rhs) != 0) {
      int allow_sub = proto_is_subtype(a->protos, rhs, lhs);
      int empty_map_assign =
          (st->child_len >= 2 && st->children[1] && strcmp(st->children[1]->kind, "MapLiteral") == 0 &&
           st->children[1]->child_len == 0 && lhs && strncmp(lhs, "map<", 4) == 0);
      int empty_list_assign =
          (st->child_len >= 2 && st->children[1] && strcmp(st->children[1]->kind, "ListLiteral") == 0 &&
           st->children[1]->child_len == 0 && lhs && strncmp(lhs, "list<", 5) == 0);
      int allow_byte_lit = (lhs && strcmp(lhs, "byte") == 0 && is_byte_literal_expr(st->children[1]));
      int allow_byte_list = (lhs && strcmp(lhs, "list<byte>") == 0 && is_byte_list_literal(st->children[1]));
      if (!allow_sub && !empty_map_assign && !empty_list_assign && !allow_byte_lit && !allow_byte_list) {
      char msg[160];
      snprintf(msg, sizeof(msg), "cannot assign %s to %s", rhs, lhs);
      set_diag(a->diag, a->file, st->line, st->col, "E3001", "TYPE_MISMATCH_ASSIGNMENT", msg);
      free(lhs);
      free(rhs);
      return 0;
      }
    }
    if (st->children[0] && strcmp(st->children[0]->kind, "Identifier") == 0) {
      Sym *sym = scope_lookup_sym(scope, st->children[0]->text ? st->children[0]->text : "");
      if (sym) {
        if (st->children[1] && strcmp(st->children[1]->kind, "ListLiteral") == 0) sym->known_list_len = (int)st->children[1]->child_len;
        else sym->known_list_len = -1;
        sym->initialized = 1;
        if (!st->text || strcmp(st->text, "=") == 0) {
          sym->alias_self = expr_is_self_alias(st->children[1], scope);
        } else {
          sym->alias_self = 0;
        }
      }
    }
    free(lhs);
    free(rhs);
    return 1;
  }
  if (strcmp(st->kind, "ExprStmt") == 0 && st->child_len > 0) {
    int ok = 1;
    char *t = infer_expr_type(a, st->children[0], scope, &ok);
    free(t);
    if (!ok) return 0;
    return check_list_pop(a, st->children[0], scope);
  }
  if (strcmp(st->kind, "IfStmt") == 0) {
    AstNode *cond = (st->child_len > 0) ? st->children[0] : NULL;
    AstNode *then_st = (st->child_len > 1) ? st->children[1] : NULL;
    AstNode *else_st = (st->child_len > 2) ? st->children[2] : NULL;
    int ok = 1;
    char *ct = cond ? infer_expr_type(a, cond, scope, &ok) : NULL;
    if (!ok) {
      free(ct);
      return 0;
    }
    if (ct && strcmp(ct, "bool") != 0) {
      set_diag(a->diag, a->file, cond ? cond->line : st->line, cond ? cond->col : st->col,
               "E3001", "TYPE_MISMATCH_ASSIGNMENT", "condition must be bool");
      free(ct);
      return 0;
    }
    free(ct);
    if (then_st) {
      Scope s_then;
      memset(&s_then, 0, sizeof(s_then));
      s_then.parent = scope;
      if (!analyze_stmt(a, then_st, &s_then)) {
        free_syms(s_then.syms);
        return 0;
      }
      free_syms(s_then.syms);
    }
    if (else_st) {
      Scope s_else;
      memset(&s_else, 0, sizeof(s_else));
      s_else.parent = scope;
      if (!analyze_stmt(a, else_st, &s_else)) {
        free_syms(s_else.syms);
        return 0;
      }
      free_syms(s_else.syms);
    }
    return 1;
  }
  if (strcmp(st->kind, "WhileStmt") == 0) {
    AstNode *cond = (st->child_len > 0) ? st->children[0] : NULL;
    AstNode *body = (st->child_len > 1) ? st->children[1] : NULL;
    int ok = 1;
    char *ct = cond ? infer_expr_type(a, cond, scope, &ok) : NULL;
    if (!ok) {
      free(ct);
      return 0;
    }
    if (ct && strcmp(ct, "bool") != 0) {
      set_diag(a->diag, a->file, cond ? cond->line : st->line, cond ? cond->col : st->col,
               "E3001", "TYPE_MISMATCH_ASSIGNMENT", "condition must be bool");
      free(ct);
      return 0;
    }
    free(ct);
    if (body) {
      Scope s_body;
      memset(&s_body, 0, sizeof(s_body));
      s_body.parent = scope;
      int okb = analyze_stmt(a, body, &s_body);
      free_syms(s_body.syms);
      return okb;
    }
    return 1;
  }
  if (strcmp(st->kind, "DoWhileStmt") == 0) {
    AstNode *cond = (st->child_len > 0) ? st->children[0] : NULL;
    AstNode *body = (st->child_len > 1) ? st->children[1] : NULL;
    int ok = 1;
    char *ct = cond ? infer_expr_type(a, cond, scope, &ok) : NULL;
    if (!ok) {
      free(ct);
      return 0;
    }
    if (ct && strcmp(ct, "bool") != 0) {
      set_diag(a->diag, a->file, cond ? cond->line : st->line, cond ? cond->col : st->col,
               "E3001", "TYPE_MISMATCH_ASSIGNMENT", "condition must be bool");
      free(ct);
      return 0;
    }
    free(ct);
    if (body) {
      Scope s_body;
      memset(&s_body, 0, sizeof(s_body));
      s_body.parent = scope;
      int okb = analyze_stmt(a, body, &s_body);
      free_syms(s_body.syms);
      return okb;
    }
    return 1;
  }
  if (strcmp(st->kind, "ReturnStmt") == 0 && st->child_len > 0) {
    AstNode *expr = st->children[0];
    if (expr_is_self_alias(expr, scope)) {
      set_diag(a->diag, a->file, expr ? expr->line : st->line, expr ? expr->col : st->col,
               "E3007", "INVALID_RETURN", "cannot return self");
      return 0;
    }
    int ok = 1;
    char *t = infer_expr_type(a, expr, scope, &ok);
    free(t);
    return ok;
  }
  if (strcmp(st->kind, "ThrowStmt") == 0 && st->child_len > 0) {
    int ok = 1;
    char *t = infer_expr_type(a, st->children[0], scope, &ok);
    if (t && strcmp(t, "unknown") != 0 && !proto_is_subtype(a->protos, t, "Exception")) {
      set_diag(a->diag, a->file, st->line, st->col, "E3001", "TYPE_MISMATCH_ASSIGNMENT", "throw expects Exception");
      free(t);
      return 0;
    }
    free(t);
    return ok;
  }
  if (strcmp(st->kind, "ForStmt") == 0) {
    if (st->text && (strcmp(st->text, "in") == 0 || strcmp(st->text, "of") == 0)) {
      Scope s2;
      memset(&s2, 0, sizeof(s2));
      s2.parent = scope;
      for (size_t i = 0; i < st->child_len; i++) {
        AstNode *c = st->children[i];
        if (strcmp(c->kind, "IterVar") == 0) {
          AstNode *tn = ast_child_kind(c, "Type");
          char *tt = canon_type(tn ? tn->text : "unknown");
          int okd = scope_define(&s2, c->text ? c->text : "", tt ? tt : "unknown", -1, 1);
          free(tt);
          if (!okd) {
            free_syms(s2.syms);
            return 0;
          }
        } else if (strcmp(c->kind, "Block") == 0) {
          int ok = analyze_block(a, c, &s2);
          free_syms(s2.syms);
          return ok;
        } else {
          int ok = 1;
          char *t = infer_expr_type(a, c, &s2, &ok);
          free(t);
          if (!ok) {
            free_syms(s2.syms);
            return 0;
          }
        }
      }
      free_syms(s2.syms);
      return 1;
    }

    Scope s2;
    memset(&s2, 0, sizeof(s2));
    s2.parent = scope;
    if (st->child_len == 0) return 1;
    AstNode *body = st->children[st->child_len - 1];
    size_t parts = st->child_len - 1;
    for (size_t i = 0; i < parts; i++) {
      AstNode *c = st->children[i];
      if (strcmp(c->kind, "VarDecl") == 0 || strcmp(c->kind, "AssignStmt") == 0) {
        if (!analyze_stmt(a, c, &s2)) {
          free_syms(s2.syms);
          return 0;
        }
        continue;
      }
      int ok = 1;
      char *t = infer_expr_type(a, c, &s2, &ok);
      free(t);
      if (!ok) {
        free_syms(s2.syms);
        return 0;
      }
      if (i == 1 || (parts == 1 && i == 0)) {
        char *ct = infer_expr_type(a, c, &s2, &ok);
        if (ct && strcmp(ct, "bool") != 0) {
          set_diag(a->diag, a->file, c->line, c->col, "E3001", "TYPE_MISMATCH_ASSIGNMENT", "condition must be bool");
          free(ct);
          free_syms(s2.syms);
          return 0;
        }
        free(ct);
      }
    }
    if (body && !analyze_stmt(a, body, &s2)) {
      free_syms(s2.syms);
      return 0;
    }
    free_syms(s2.syms);
    return 1;
  }
  if (strcmp(st->kind, "SwitchStmt") == 0) {
    AstNode *sw_expr = (st->child_len > 0) ? st->children[0] : NULL;
    int ok = 1;
    char *t = sw_expr ? infer_expr_type(a, sw_expr, scope, &ok) : NULL;
    free(t);
    if (!ok) return 0;
    if (!analyze_switch_termination(a, st)) return 0;
    for (size_t i = 0; i < st->child_len; i++) {
      AstNode *c = st->children[i];
      if (strcmp(c->kind, "CaseClause") != 0 && strcmp(c->kind, "DefaultClause") != 0) continue;
      size_t start = (strcmp(c->kind, "CaseClause") == 0 && c->child_len > 0) ? 1 : 0;
      for (size_t j = start; j < c->child_len; j++) {
        if (!analyze_stmt(a, c->children[j], scope)) return 0;
      }
    }
    return 1;
  }
  if (strcmp(st->kind, "TryStmt") == 0) {
    for (size_t i = 0; i < st->child_len; i++) {
      AstNode *c = st->children[i];
      if (strcmp(c->kind, "Block") == 0) {
        if (!analyze_block(a, c, scope)) return 0;
      } else if (strcmp(c->kind, "CatchClause") == 0) {
        Scope s2;
        memset(&s2, 0, sizeof(s2));
        s2.parent = scope;
        AstNode *tn = ast_child_kind(c, "Type");
        char *tt = canon_type(tn ? tn->text : "unknown");
        if (tt && strcmp(tt, "unknown") != 0 && !proto_is_subtype(a->protos, tt, "Exception")) {
          set_diag(a->diag, a->file, c->line, c->col, "E3001", "TYPE_MISMATCH_ASSIGNMENT", "catch type must derive from Exception");
          free(tt);
          free_syms(s2.syms);
          return 0;
        }
        if (!scope_define(&s2, c->text ? c->text : "", tt ? tt : "unknown", -1, 1)) {
          free(tt);
          free_syms(s2.syms);
          return 0;
        }
        free(tt);
        AstNode *blk = ast_child_kind(c, "Block");
        int ok = blk ? analyze_block(a, blk, &s2) : 1;
        free_syms(s2.syms);
        if (!ok) return 0;
      } else if (strcmp(c->kind, "FinallyClause") == 0) {
        AstNode *blk = ast_child_kind(c, "Block");
        if (blk && !analyze_block(a, blk, scope)) return 0;
      }
    }
    return 1;
  }
  return 1;
}

static int analyze_function(Analyzer *a, AstNode *fn) {
  Scope root;
  memset(&root, 0, sizeof(root));
  root.parent = NULL;
  for (size_t i = 0; i < fn->child_len; i++) {
    AstNode *c = fn->children[i];
    if (strcmp(c->kind, "Param") != 0) continue;
    AstNode *tn = ast_child_kind(c, "Type");
    char *tt = canon_type(tn ? tn->text : "unknown");
    if (!tt || !scope_define(&root, c->text ? c->text : "", tt, -1, 1)) {
      free(tt);
      free_syms(root.syms);
      return 0;
    }
    free(tt);
  }
  AstNode *blk = ast_child_kind(fn, "Block");
  int ok = blk ? analyze_block(a, blk, &root) : 1;
  free_syms(root.syms);
  return ok;
}

static int analyze_method(Analyzer *a, AstNode *fn, const char *self_type) {
  Scope root;
  memset(&root, 0, sizeof(root));
  root.parent = NULL;
  if (self_type) {
    char *st = canon_type(self_type);
    if (!st || !scope_define_alias(&root, "self", st, -1, 1, 1)) {
      free(st);
      free_syms(root.syms);
      return 0;
    }
    free(st);
  }
  for (size_t i = 0; i < fn->child_len; i++) {
    AstNode *c = fn->children[i];
    if (strcmp(c->kind, "Param") != 0) continue;
    AstNode *tn = ast_child_kind(c, "Type");
    char *tt = canon_type(tn ? tn->text : "unknown");
    if (!tt || !scope_define(&root, c->text ? c->text : "", tt, -1, 1)) {
      free(tt);
      free_syms(root.syms);
      return 0;
    }
    free(tt);
  }
  AstNode *blk = ast_child_kind(fn, "Block");
  int ok = blk ? analyze_block(a, blk, &root) : 1;
  free_syms(root.syms);
  return ok;
}

static int parse_file_internal(const char *file, PsDiag *out_diag, AstNode **out_root) {
  memset(out_diag, 0, sizeof(*out_diag));
  size_t n = 0;
  char *src = read_file(file, &n, out_diag);
  if (!src) {
    return 2;
  }

  Lexer lx;
  memset(&lx, 0, sizeof(lx));
  lx.file = file;
  lx.src = src;
  lx.n = n;
  lx.i = 0;
  lx.line = 1;
  lx.col = 1;
  lx.diag = out_diag;

  if (!run_lexer(&lx)) {
    token_vec_free(&lx.toks);
    free(src);
    return 1;
  }

  Parser p;
  memset(&p, 0, sizeof(p));
  p.file = file;
  p.toks = &lx.toks;
  p.i = 0;
  p.diag = out_diag;
  p.ast_root = NULL;
  p.ast_sp = 0;

  int ok = parse_program(&p);
  if (out_root) *out_root = p.ast_root;
  else ast_free(p.ast_root);
  token_vec_free(&lx.toks);
  free(src);
  return ok ? 0 : 1;
}

int ps_parse_file_syntax(const char *file, PsDiag *out_diag) { return parse_file_internal(file, out_diag, NULL); }

int ps_parse_file_ast(const char *file, PsDiag *out_diag, FILE *out) {
  AstNode *root = NULL;
  int rc = parse_file_internal(file, out_diag, &root);
  if (rc != 0) {
    ast_free(root);
    return rc;
  }
  ast_print_json(out, root, 0);
  fputc('\n', out);
  ast_free(root);
  return 0;
}

typedef struct {
  char **items;
  size_t len;
  size_t cap;
} StrVec;

typedef struct {
  char *label;
  StrVec instrs;
} IrBlock;

typedef struct {
  IrBlock *items;
  size_t len;
  size_t cap;
} IrBlockVec;

typedef struct {
  char *name;
  char *type;
} IrVarType;

typedef struct {
  IrVarType *items;
  size_t len;
  size_t cap;
} IrVarTypeVec;

typedef struct IrFnSig {
  char *name;
  char *ret_type;
  int variadic;
  struct IrFnSig *next;
} IrFnSig;

typedef struct IrVarName {
  char *name;
  char *ir;
  struct IrVarName *next;
} IrVarName;

typedef struct IrScope {
  IrVarName *vars;
  struct IrScope *parent;
} IrScope;

typedef struct LoopTarget {
  char *break_label;
  char *continue_label;
  struct LoopTarget *next;
} LoopTarget;

typedef struct {
  IrBlockVec blocks;
  size_t cur_block;
  StrVec instrs;
  int temp_id;
  int label_id;
  int var_id;
  IrVarTypeVec vars;
  IrFnSig *fn_sigs;
  ImportSymbol *imports;
  ImportNamespace *namespaces;
  ModuleRegistry *registry;
  ProtoInfo *protos;
  IrScope *scope;
  LoopTarget *loop_targets;
  LoopTarget *break_targets;
  const char *file;
  const char *loc_file;
  int loc_line;
  int loc_col;
} IrFnCtx;

static void str_vec_free(StrVec *v) {
  for (size_t i = 0; i < v->len; i++) free(v->items[i]);
  free(v->items);
  v->items = NULL;
  v->len = 0;
  v->cap = 0;
}

static void ir_block_vec_free(IrBlockVec *v) {
  for (size_t i = 0; i < v->len; i++) {
    free(v->items[i].label);
    str_vec_free(&v->items[i].instrs);
  }
  free(v->items);
  v->items = NULL;
  v->len = 0;
  v->cap = 0;
}

static void ir_var_type_vec_free(IrVarTypeVec *v) {
  for (size_t i = 0; i < v->len; i++) {
    free(v->items[i].name);
    free(v->items[i].type);
  }
  free(v->items);
  v->items = NULL;
  v->len = 0;
  v->cap = 0;
}

static int str_vec_push(StrVec *v, char *s) {
  if (v->len == v->cap) {
    size_t nc = (v->cap == 0) ? 32 : v->cap * 2;
    char **ni = (char **)realloc(v->items, nc * sizeof(char *));
    if (!ni) return 0;
    v->items = ni;
    v->cap = nc;
  }
  v->items[v->len++] = s;
  return 1;
}

static int ir_block_vec_push(IrBlockVec *v, IrBlock b) {
  if (v->len == v->cap) {
    size_t nc = (v->cap == 0) ? 8 : v->cap * 2;
    IrBlock *ni = (IrBlock *)realloc(v->items, nc * sizeof(IrBlock));
    if (!ni) return 0;
    v->items = ni;
    v->cap = nc;
  }
  v->items[v->len++] = b;
  return 1;
}

static int ir_set_var_type(IrFnCtx *ctx, const char *name, const char *type) {
  if (!ctx || !name || !type) return 0;
  for (size_t i = 0; i < ctx->vars.len; i++) {
    if (strcmp(ctx->vars.items[i].name, name) == 0) {
      char *nt = strdup(type);
      if (!nt) return 0;
      free(ctx->vars.items[i].type);
      ctx->vars.items[i].type = nt;
      return 1;
    }
  }
  if (ctx->vars.len == ctx->vars.cap) {
    size_t nc = (ctx->vars.cap == 0) ? 16 : ctx->vars.cap * 2;
    IrVarType *ni = (IrVarType *)realloc(ctx->vars.items, nc * sizeof(IrVarType));
    if (!ni) return 0;
    ctx->vars.items = ni;
    ctx->vars.cap = nc;
  }
  ctx->vars.items[ctx->vars.len].name = strdup(name);
  ctx->vars.items[ctx->vars.len].type = strdup(type);
  if (!ctx->vars.items[ctx->vars.len].name || !ctx->vars.items[ctx->vars.len].type) {
    free(ctx->vars.items[ctx->vars.len].name);
    free(ctx->vars.items[ctx->vars.len].type);
    return 0;
  }
  ctx->vars.len++;
  return 1;
}

static const char *ir_get_var_type(IrFnCtx *ctx, const char *name) {
  if (!ctx || !name) return NULL;
  for (size_t i = 0; i < ctx->vars.len; i++) {
    if (strcmp(ctx->vars.items[i].name, name) == 0) return ctx->vars.items[i].type;
  }
  return NULL;
}

static void ir_scope_init(IrScope *s, IrScope *parent) {
  if (!s) return;
  s->vars = NULL;
  s->parent = parent;
}

static void ir_scope_free(IrScope *s) {
  if (!s) return;
  IrVarName *cur = s->vars;
  while (cur) {
    IrVarName *next = cur->next;
    free(cur->name);
    free(cur->ir);
    free(cur);
    cur = next;
  }
  s->vars = NULL;
}

static int ir_scope_define(IrScope *s, const char *name, const char *ir) {
  if (!s || !name || !ir) return 0;
  IrVarName *e = (IrVarName *)calloc(1, sizeof(IrVarName));
  if (!e) return 0;
  e->name = strdup(name);
  e->ir = strdup(ir);
  if (!e->name || !e->ir) {
    free(e->name);
    free(e->ir);
    free(e);
    return 0;
  }
  e->next = s->vars;
  s->vars = e;
  return 1;
}

static const char *ir_scope_lookup(IrScope *s, const char *name) {
  for (IrScope *cur = s; cur; cur = cur->parent) {
    for (IrVarName *e = cur->vars; e; e = e->next) {
      if (strcmp(e->name, name) == 0) return e->ir;
    }
  }
  return NULL;
}

static char *ir_next_var(IrFnCtx *ctx, const char *name) {
  const char *base = name ? name : "v";
  return str_printf("%s$%d", base, ++ctx->var_id);
}

static const IrFnSig *ir_find_fn_sig(IrFnCtx *ctx, const char *name) {
  for (const IrFnSig *f = ctx ? ctx->fn_sigs : NULL; f; f = f->next) {
    if (strcmp(f->name, name) == 0) return f;
  }
  return NULL;
}

static ImportSymbol *ir_find_import(IrFnCtx *ctx, const char *local) {
  for (ImportSymbol *s = ctx ? ctx->imports : NULL; s; s = s->next) {
    if (strcmp(s->local, local) == 0) return s;
  }
  return NULL;
}

static ImportNamespace *ir_find_namespace(IrFnCtx *ctx, const char *alias) {
  for (ImportNamespace *n = ctx ? ctx->namespaces : NULL; n; n = n->next) {
    if (strcmp(n->alias, alias) == 0) return n;
  }
  return NULL;
}

static char *str_printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(NULL, 0, fmt, ap);
  va_end(ap);
  if (n < 0) return NULL;
  char *out = (char *)malloc((size_t)n + 1);
  if (!out) return NULL;
  va_start(ap, fmt);
  vsnprintf(out, (size_t)n + 1, fmt, ap);
  va_end(ap);
  return out;
}

static char *json_escape(const char *s) {
  if (!s) return strdup("");
  size_t cap = strlen(s) * 2 + 8;
  char *out = (char *)malloc(cap);
  if (!out) return NULL;
  size_t j = 0;
  for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
    if (j + 8 >= cap) {
      cap *= 2;
      char *ni = (char *)realloc(out, cap);
      if (!ni) {
        free(out);
        return NULL;
      }
      out = ni;
    }
    if (*p == '"' || *p == '\\') {
      out[j++] = '\\';
      out[j++] = (char)*p;
    } else if (*p == '\n') {
      out[j++] = '\\';
      out[j++] = 'n';
    } else if (*p == '\r') {
      out[j++] = '\\';
      out[j++] = 'r';
    } else if (*p == '\t') {
      out[j++] = '\\';
      out[j++] = 't';
    } else {
      out[j++] = (char)*p;
    }
  }
  out[j] = '\0';
  return out;
}

static void ir_set_loc(IrFnCtx *ctx, AstNode *node) {
  if (!ctx) return;
  const char *file = ctx->file;
  int line = node ? node->line : 1;
  int col = node ? node->col : 1;
  const PreprocessLineMap *map = preprocess_map_lookup(ctx->file);
  if (map && line > 0 && (size_t)line <= map->len) {
    const char *mf = map->files[line - 1];
    int ml = map->lines[line - 1];
    if (mf && *mf) file = mf;
    if (ml > 0) line = ml;
  }
  ctx->loc_file = file;
  ctx->loc_line = line;
  ctx->loc_col = col;
}

static char *ir_attach_loc(IrFnCtx *c, char *json_obj) {
  if (!c || !json_obj) return json_obj;
  const char *file = c->loc_file ? c->loc_file : c->file;
  int line = c->loc_line > 0 ? c->loc_line : 1;
  int col = c->loc_col > 0 ? c->loc_col : 1;
  if (!file || !file[0]) return json_obj;
  size_t n = strlen(json_obj);
  if (n == 0 || json_obj[n - 1] != '}') return json_obj;
  char *fesc = json_escape(file);
  if (!fesc) return json_obj;
  char *out = str_printf("%.*s,\"file\":\"%s\",\"line\":%d,\"col\":%d}", (int)(n - 1), json_obj, fesc, line, col);
  free(fesc);
  if (!out) return json_obj;
  free(json_obj);
  return out;
}

static int ir_emit(IrFnCtx *c, char *json_obj) {
  if (!json_obj) return 0;
  if (c->blocks.len == 0) return 0;
  json_obj = ir_attach_loc(c, json_obj);
  if (!str_vec_push(&c->blocks.items[c->cur_block].instrs, json_obj)) {
    free(json_obj);
    return 0;
  }
  return 1;
}

static int ir_push_loop(IrFnCtx *ctx, const char *break_label, const char *continue_label) {
  LoopTarget *t = (LoopTarget *)calloc(1, sizeof(LoopTarget));
  if (!t) return 0;
  t->break_label = break_label ? strdup(break_label) : NULL;
  t->continue_label = continue_label ? strdup(continue_label) : NULL;
  if ((break_label && !t->break_label) || (continue_label && !t->continue_label)) {
    free(t->break_label);
    free(t->continue_label);
    free(t);
    return 0;
  }
  t->next = ctx->loop_targets;
  ctx->loop_targets = t;
  return 1;
}

static void ir_pop_loop(IrFnCtx *ctx) {
  if (!ctx->loop_targets) return;
  LoopTarget *t = ctx->loop_targets;
  ctx->loop_targets = t->next;
  free(t->break_label);
  free(t->continue_label);
  free(t);
}

static int ir_push_break(IrFnCtx *ctx, const char *break_label) {
  LoopTarget *t = (LoopTarget *)calloc(1, sizeof(LoopTarget));
  if (!t) return 0;
  t->break_label = break_label ? strdup(break_label) : NULL;
  if (break_label && !t->break_label) {
    free(t);
    return 0;
  }
  t->next = ctx->break_targets;
  ctx->break_targets = t;
  return 1;
}

static void ir_pop_break(IrFnCtx *ctx) {
  if (!ctx->break_targets) return;
  LoopTarget *t = ctx->break_targets;
  ctx->break_targets = t->next;
  free(t->break_label);
  free(t);
}

static char *ir_next_tmp(IrFnCtx *c) { return str_printf("%%t%d", ++c->temp_id); }

static char *ir_next_label(IrFnCtx *c, const char *prefix) { return str_printf("%s%d", prefix ? prefix : "b", ++c->label_id); }

static int ir_add_block(IrFnCtx *c, const char *label, size_t *out_idx) {
  IrBlock b;
  memset(&b, 0, sizeof(b));
  b.label = strdup(label ? label : "b");
  if (!b.label) return 0;
  if (!ir_block_vec_push(&c->blocks, b)) {
    free(b.label);
    return 0;
  }
  if (out_idx) *out_idx = c->blocks.len - 1;
  return 1;
}

static int ir_is_terminated_block(const StrVec *v) {
  if (!v || v->len == 0) return 0;
  const char *s = v->items[v->len - 1];
  return strstr(s, "\"op\":\"ret\"") || strstr(s, "\"op\":\"ret_void\"") || strstr(s, "\"op\":\"throw\"") ||
         strstr(s, "\"op\":\"jump\"") || strstr(s, "\"op\":\"branch_if\"") || strstr(s, "\"op\":\"branch_iter_has_next\"");
}

static void ir_free_fn_sigs(IrFnSig *s) {
  while (s) {
    IrFnSig *n = s->next;
    free(s->name);
    free(s->ret_type);
    free(s);
    s = n;
  }
}

static char *ast_type_to_ir_name(AstNode *type_node) {
  if (!type_node || !type_node->text) return strdup("unknown");
  return canon_type(type_node->text);
}

static int ir_is_int_like(const char *t) { return t && (strcmp(t, "int") == 0 || strcmp(t, "byte") == 0); }

static int ir_type_is_map(const char *t) { return t && strncmp(t, "map<", 4) == 0; }

static char *ir_type_map_key(const char *t) {
  if (!t || strncmp(t, "map<", 4) != 0) return strdup("unknown");
  const char *lt = strchr(t, '<');
  const char *comma = strchr(t, ',');
  if (!lt || !comma || comma <= lt + 1) return strdup("unknown");
  return dup_range(t, (size_t)(lt - t + 1), (size_t)(comma - t));
}

static char *ir_type_map_value(const char *t) {
  if (!t || strncmp(t, "map<", 4) != 0) return strdup("unknown");
  const char *comma = strchr(t, ',');
  const char *gt = strrchr(t, '>');
  if (!comma || !gt || gt <= comma + 1) return strdup("unknown");
  return dup_range(t, (size_t)(comma - t + 1), (size_t)(gt - t));
}

static char *ir_type_elem_for_index(const char *t) {
  if (!t) return strdup("unknown");
  if (strncmp(t, "list<", 5) == 0 || strncmp(t, "slice<", 6) == 0 || strncmp(t, "view<", 5) == 0) {
    const char *lt = strchr(t, '<');
    const char *gt = strrchr(t, '>');
    if (lt && gt && gt > lt + 1) return dup_range(t, (size_t)(lt - t + 1), (size_t)(gt - t));
  }
  if (strncmp(t, "map<", 4) == 0) {
    const char *comma = strchr(t, ',');
    const char *gt = strrchr(t, '>');
    if (comma && gt && gt > comma + 1) return dup_range(t, (size_t)(comma - t + 1), (size_t)(gt - t));
  }
  if (strcmp(t, "string") == 0) return strdup("glyph");
  return strdup("unknown");
}

static char *ir_type_elem_for_iter(const char *t, const char *mode) {
  if (!t) return strdup("unknown");
  if (strncmp(t, "map<", 4) == 0) {
    const char *lt = strchr(t, '<');
    const char *comma = strchr(t, ',');
    const char *gt = strrchr(t, '>');
    if (!lt || !comma || !gt || comma <= lt + 1 || gt <= comma + 1) return strdup("unknown");
    if (mode && strcmp(mode, "in") == 0) return dup_range(t, (size_t)(lt - t + 1), (size_t)(comma - t));
    return dup_range(t, (size_t)(comma - t + 1), (size_t)(gt - t));
  }
  return ir_type_elem_for_index(t);
}

static char *ir_guess_expr_type(AstNode *e, IrFnCtx *ctx) {
  if (!e) return strdup("unknown");
  if (strcmp(e->kind, "Literal") == 0) {
    if (e->text && (strcmp(e->text, "true") == 0 || strcmp(e->text, "false") == 0)) return strdup("bool");
    if (e->text && (is_all_digits(e->text) || is_hex_token(e->text) || is_bin_token(e->text) || is_float_token(e->text)))
      return strdup(is_float_token(e->text) ? "float" : "int");
    return strdup("string");
  }
  if (strcmp(e->kind, "CastExpr") == 0) {
    if (e->text) return strdup(e->text);
    return strdup("unknown");
  }
  if (strcmp(e->kind, "Identifier") == 0) {
    const char *mapped = ir_scope_lookup(ctx ? ctx->scope : NULL, e->text ? e->text : "");
    const char *t = ir_get_var_type(ctx, mapped ? mapped : (e->text ? e->text : ""));
    return strdup(t ? t : "unknown");
  }
  if (strcmp(e->kind, "UnaryExpr") == 0 || strcmp(e->kind, "PostfixExpr") == 0 || strcmp(e->kind, "MemberExpr") == 0) {
    if (strcmp(e->kind, "MemberExpr") == 0) {
      if (e->text && strcmp(e->text, "toString") == 0) return strdup("string");
      if (e->child_len > 0) {
        AstNode *target = e->children[0];
      if (strcmp(target->kind, "Identifier") == 0) {
        ImportNamespace *ns = ir_find_namespace(ctx, target->text ? target->text : "");
        if (ns && !ns->is_proto) {
          RegConst *rc = registry_find_const(ctx->registry, ns->module, e->text ? e->text : "");
          if (rc && rc->type) {
            if (strcmp(rc->type, "float") == 0) return strdup("float");
              if (strcmp(rc->type, "int") == 0) return strdup("int");
              if (strcmp(rc->type, "string") == 0) return strdup("string");
              if (strcmp(rc->type, "TextFile") == 0 || strcmp(rc->type, "BinaryFile") == 0) return strdup(rc->type);
            }
          }
        }
      }
      if (e->child_len > 0) {
        char *tt = ir_guess_expr_type(e->children[0], ctx);
        if (tt && proto_find(ctx->protos, tt)) {
          ProtoField *pf = proto_find_field(ctx->protos, tt, e->text ? e->text : "");
          if (pf && pf->type) {
            char *ret = strdup(pf->type);
            free(tt);
            return ret;
          }
        }
        free(tt);
      }
    }
    return (e->child_len > 0) ? ir_guess_expr_type(e->children[0], ctx) : strdup("unknown");
  }
  if (strcmp(e->kind, "BinaryExpr") == 0) {
    if (e->text && (strcmp(e->text, "==") == 0 || strcmp(e->text, "!=") == 0 || strcmp(e->text, "<") == 0 || strcmp(e->text, "<=") == 0 ||
                    strcmp(e->text, ">") == 0 || strcmp(e->text, ">=") == 0 || strcmp(e->text, "&&") == 0 || strcmp(e->text, "||") == 0))
      return strdup("bool");
    return (e->child_len > 0) ? ir_guess_expr_type(e->children[0], ctx) : strdup("unknown");
  }
  if (strcmp(e->kind, "ConditionalExpr") == 0) {
    return (e->child_len >= 2) ? ir_guess_expr_type(e->children[1], ctx) : strdup("unknown");
  }
  if (strcmp(e->kind, "CallExpr") == 0 && e->child_len > 0) {
    AstNode *c = e->children[0];
    if (strcmp(c->kind, "Identifier") == 0) {
      const IrFnSig *f = ir_find_fn_sig(ctx, c->text ? c->text : "");
      return strdup(f ? f->ret_type : "unknown");
    }
    if (strcmp(c->kind, "MemberExpr") == 0 && c->text) {
      if (c->child_len > 0 && strcmp(c->children[0]->kind, "Identifier") == 0) {
        const char *target = c->children[0]->text ? c->children[0]->text : "";
        ProtoInfo *proto = proto_find(ctx->protos, target);
        if (proto) {
          if (strcmp(c->text, "clone") == 0) return strdup(proto->name ? proto->name : "unknown");
          ProtoMethod *pm = proto_find_method(ctx->protos, proto->name, c->text);
          return strdup(pm && pm->ret_type ? pm->ret_type : "unknown");
        }
        ImportNamespace *ns = ir_find_namespace(ctx, target);
        if (ns && ns->is_proto) {
          ProtoInfo *p2 = proto_find(ctx->protos, ns->module);
          if (p2) {
            if (strcmp(c->text, "clone") == 0) return strdup(p2->name ? p2->name : "unknown");
            ProtoMethod *pm = proto_find_method(ctx->protos, p2->name, c->text);
            return strdup(pm && pm->ret_type ? pm->ret_type : "unknown");
          }
        }
      }
      char *recv_t = (c->child_len > 0) ? ir_guess_expr_type(c->children[0], ctx) : NULL;
      const char *m = c->text;
      if (recv_t) {
        if (proto_find(ctx->protos, recv_t)) {
          ProtoMethod *pm = proto_find_method(ctx->protos, recv_t, m);
          char *ret = strdup(pm && pm->ret_type ? pm->ret_type : "unknown");
          free(recv_t);
          return ret;
        }
        if (strcmp(recv_t, "int") == 0) {
          if (strcmp(m, "toByte") == 0) return strdup("byte");
          if (strcmp(m, "toFloat") == 0) return strdup("float");
          if (strcmp(m, "toString") == 0) return strdup("string");
          if (strcmp(m, "toBytes") == 0) return strdup("list<byte>");
          if (strcmp(m, "abs") == 0 || strcmp(m, "sign") == 0) return strdup("int");
        } else if (strcmp(recv_t, "byte") == 0) {
          if (strcmp(m, "toInt") == 0) return strdup("int");
          if (strcmp(m, "toFloat") == 0) return strdup("float");
          if (strcmp(m, "toString") == 0) return strdup("string");
        } else if (strcmp(recv_t, "float") == 0) {
          if (strcmp(m, "toInt") == 0) return strdup("int");
          if (strcmp(m, "toString") == 0) return strdup("string");
          if (strcmp(m, "toBytes") == 0) return strdup("list<byte>");
          if (strcmp(m, "abs") == 0) return strdup("float");
          if (strcmp(m, "isNaN") == 0 || strcmp(m, "isInfinite") == 0 || strcmp(m, "isFinite") == 0) return strdup("bool");
        } else if (strcmp(recv_t, "glyph") == 0) {
          if (strcmp(m, "toString") == 0) return strdup("string");
          if (strcmp(m, "toInt") == 0) return strdup("int");
          if (strcmp(m, "toUtf8Bytes") == 0) return strdup("list<byte>");
          if (strcmp(m, "isLetter") == 0 || strcmp(m, "isDigit") == 0 || strcmp(m, "isWhitespace") == 0 ||
              strcmp(m, "isUpper") == 0 || strcmp(m, "isLower") == 0)
            return strdup("bool");
          if (strcmp(m, "toUpper") == 0 || strcmp(m, "toLower") == 0) return strdup("glyph");
        } else if (strcmp(recv_t, "string") == 0) {
          if (strcmp(m, "length") == 0) return strdup("int");
          if (strcmp(m, "isEmpty") == 0) return strdup("bool");
          if (strcmp(m, "toString") == 0) return strdup("string");
          if (strcmp(m, "toInt") == 0) return strdup("int");
          if (strcmp(m, "toFloat") == 0) return strdup("float");
          if (strcmp(m, "substring") == 0) return strdup("string");
          if (strcmp(m, "indexOf") == 0) return strdup("int");
          if (strcmp(m, "startsWith") == 0 || strcmp(m, "endsWith") == 0) return strdup("bool");
          if (strcmp(m, "split") == 0) return strdup("list<string>");
          if (strcmp(m, "trim") == 0 || strcmp(m, "trimStart") == 0 || strcmp(m, "trimEnd") == 0) return strdup("string");
          if (strcmp(m, "replace") == 0) return strdup("string");
          if (strcmp(m, "toUpper") == 0 || strcmp(m, "toLower") == 0) return strdup("string");
          if (strcmp(m, "toUtf8Bytes") == 0) return strdup("list<byte>");
        } else if (strncmp(recv_t, "list<", 5) == 0) {
          char *et = ir_type_elem_for_index(recv_t);
          if (et && strcmp(et, "byte") == 0 && strcmp(m, "toUtf8String") == 0) {
            free(et);
            return strdup("string");
          }
          if (et && strcmp(et, "string") == 0 && (strcmp(m, "join") == 0 || strcmp(m, "concat") == 0)) {
            free(et);
            return strdup("string");
          }
          free(et);
          if (strcmp(m, "length") == 0) return strdup("int");
          if (strcmp(m, "isEmpty") == 0) return strdup("bool");
          if (strcmp(m, "push") == 0) return strdup("int");
          if (strcmp(m, "contains") == 0) return strdup("bool");
          if (strcmp(m, "sort") == 0) return strdup("int");
          if (strcmp(m, "view") == 0 || strcmp(m, "slice") == 0) {
            char *et2 = ir_type_elem_for_index(recv_t);
            char *out = NULL;
            if (et2) out = str_printf("%s<%s>", m, et2);
            free(et2);
            return out ? out : strdup("unknown");
          }
        } else if (strncmp(recv_t, "slice<", 6) == 0) {
          if (strcmp(m, "length") == 0) return strdup("int");
          if (strcmp(m, "isEmpty") == 0) return strdup("bool");
          if (strcmp(m, "slice") == 0) {
            char *et2 = ir_type_elem_for_index(recv_t);
            char *out = NULL;
            if (et2) out = str_printf("slice<%s>", et2);
            free(et2);
            return out ? out : strdup("unknown");
          }
        } else if (strncmp(recv_t, "view<", 5) == 0) {
          if (strcmp(m, "length") == 0) return strdup("int");
          if (strcmp(m, "isEmpty") == 0) return strdup("bool");
          if (strcmp(m, "view") == 0) {
            char *et2 = ir_type_elem_for_index(recv_t);
            char *out = NULL;
            if (et2) out = str_printf("view<%s>", et2);
            free(et2);
            return out ? out : strdup("unknown");
          }
        } else if (strncmp(recv_t, "map<", 4) == 0) {
          if (strcmp(m, "length") == 0) return strdup("int");
          if (strcmp(m, "isEmpty") == 0) return strdup("bool");
          if (strcmp(m, "containsKey") == 0) return strdup("bool");
          if (strcmp(m, "keys") == 0) {
            char *kt = ir_type_map_key(recv_t);
            char *out = str_printf("list<%s>", kt ? kt : "unknown");
            if (kt) free(kt);
            return out ? out : strdup("unknown");
          }
          if (strcmp(m, "values") == 0) {
            char *vt = ir_type_map_value(recv_t);
            char *out = str_printf("list<%s>", vt ? vt : "unknown");
            if (vt) free(vt);
            return out ? out : strdup("unknown");
          }
        } else if (strcmp(recv_t, "JSONValue") == 0) {
          if (strcmp(m, "isNull") == 0 || strcmp(m, "isBool") == 0 || strcmp(m, "isNumber") == 0 ||
              strcmp(m, "isString") == 0 || strcmp(m, "isArray") == 0 || strcmp(m, "isObject") == 0)
            return strdup("bool");
          if (strcmp(m, "asBool") == 0) return strdup("bool");
          if (strcmp(m, "asNumber") == 0) return strdup("float");
          if (strcmp(m, "asString") == 0) return strdup("string");
          if (strcmp(m, "asArray") == 0) return strdup("list<JSONValue>");
          if (strcmp(m, "asObject") == 0) return strdup("map<string,JSONValue>");
        }
      }
      if (recv_t) free(recv_t);
    }
    return strdup("unknown");
  }
  if (strcmp(e->kind, "IndexExpr") == 0 && e->child_len > 0) {
    char *tt = ir_guess_expr_type(e->children[0], ctx);
    char *et = ir_type_elem_for_index(tt);
    free(tt);
    return et;
  }
  if (strcmp(e->kind, "ListLiteral") == 0) {
    if (e->child_len == 0) return strdup("list<void>");
    char *et = ir_guess_expr_type(e->children[0], ctx);
    char *out = str_printf("list<%s>", et ? et : "unknown");
    free(et);
    return out ? out : strdup("unknown");
  }
  if (strcmp(e->kind, "MapLiteral") == 0) {
    if (e->child_len == 0) return strdup("map<void,void>");
    AstNode *p = e->children[0];
    if (!p || strcmp(p->kind, "MapPair") != 0 || p->child_len < 2) return strdup("map<void,void>");
    char *kt = ir_guess_expr_type(p->children[0], ctx);
    char *vt = ir_guess_expr_type(p->children[1], ctx);
    char *out = str_printf("map<%s,%s>", kt ? kt : "unknown", vt ? vt : "unknown");
    free(kt);
    free(vt);
    return out ? out : strdup("unknown");
  }
  return strdup("unknown");
}

static char *ir_emit_default_value(IrFnCtx *ctx, const char *type, const char *current_proto) {
  char *dst = ir_next_tmp(ctx);
  if (!dst) return NULL;
  const char *t = type ? type : "unknown";
  char *d_esc = json_escape(dst);
  if (strcmp(t, "int") == 0) {
    ir_emit(ctx, str_printf("{\"op\":\"const\",\"dst\":\"%s\",\"literalType\":\"int\",\"value\":\"0\"}", d_esc ? d_esc : ""));
  } else if (strcmp(t, "byte") == 0) {
    ir_emit(ctx, str_printf("{\"op\":\"const\",\"dst\":\"%s\",\"literalType\":\"byte\",\"value\":\"0\"}", d_esc ? d_esc : ""));
  } else if (strcmp(t, "float") == 0) {
    ir_emit(ctx, str_printf("{\"op\":\"const\",\"dst\":\"%s\",\"literalType\":\"float\",\"value\":\"0\"}", d_esc ? d_esc : ""));
  } else if (strcmp(t, "bool") == 0) {
    ir_emit(ctx, str_printf("{\"op\":\"const\",\"dst\":\"%s\",\"literalType\":\"bool\",\"value\":false}", d_esc ? d_esc : ""));
  } else if (strcmp(t, "glyph") == 0) {
    ir_emit(ctx, str_printf("{\"op\":\"const\",\"dst\":\"%s\",\"literalType\":\"glyph\",\"value\":\"0\"}", d_esc ? d_esc : ""));
  } else if (strcmp(t, "string") == 0) {
    ir_emit(ctx, str_printf("{\"op\":\"const\",\"dst\":\"%s\",\"literalType\":\"string\",\"value\":\"\"}", d_esc ? d_esc : ""));
  } else if (proto_find(ctx->protos, t)) {
    if (current_proto && strcmp(current_proto, t) == 0) {
      ir_emit(ctx, str_printf("{\"op\":\"make_object\",\"dst\":\"%s\",\"proto\":\"%s\"}", d_esc ? d_esc : "", t));
    } else {
      char *callee = str_printf("%s.clone", t);
      char *callee_esc = json_escape(callee ? callee : "");
      ir_emit(ctx, str_printf("{\"op\":\"call_static\",\"dst\":\"%s\",\"callee\":\"%s\",\"args\":[],\"variadic\":false}", d_esc ? d_esc : "",
                              callee_esc ? callee_esc : ""));
      free(callee);
      free(callee_esc);
    }
  } else if (strncmp(t, "list<", 5) == 0) {
    ir_emit(ctx, str_printf("{\"op\":\"make_list\",\"dst\":\"%s\",\"items\":[]}", d_esc ? d_esc : ""));
  } else if (strncmp(t, "map<", 4) == 0) {
    ir_emit(ctx, str_printf("{\"op\":\"make_map\",\"dst\":\"%s\",\"pairs\":[]}", d_esc ? d_esc : ""));
  } else {
    ir_emit(ctx, str_printf("{\"op\":\"const\",\"dst\":\"%s\",\"literalType\":\"int\",\"value\":\"0\"}", d_esc ? d_esc : ""));
  }
  free(d_esc);
  return dst;
}

static char *ir_lower_expr(AstNode *e, IrFnCtx *ctx);

static char *ir_lower_call(AstNode *e, IrFnCtx *ctx) {
  if (e->child_len == 0) return NULL;
  AstNode *callee = e->children[0];
  size_t argc = (e->child_len > 0) ? (e->child_len - 1) : 0;
  char **args = (char **)calloc(argc ? argc : 1, sizeof(char *));
  if (!args) return NULL;
  for (size_t i = 0; i < argc; i++) {
    args[i] = ir_lower_expr(e->children[i + 1], ctx);
    if (!args[i]) {
      for (size_t j = 0; j < i; j++) free(args[j]);
      free(args);
      return NULL;
    }
  }
  char *dst = ir_next_tmp(ctx);
  if (!dst) {
    for (size_t i = 0; i < argc; i++) free(args[i]);
    free(args);
    return NULL;
  }

  if (strcmp(callee->kind, "Identifier") == 0) {
    const IrFnSig *sig = ir_find_fn_sig(ctx, callee->text ? callee->text : "");
    ImportSymbol *imp = ir_find_import(ctx, callee->text ? callee->text : "");
    char *full = NULL;
    if (imp) full = str_printf("%s.%s", imp->module ? imp->module : "", imp->name ? imp->name : "");
    char *callee_esc = json_escape(full ? full : (callee->text ? callee->text : ""));
    char *args_json = strdup("");
    for (size_t i = 0; i < argc; i++) {
      char *arg_esc = json_escape(args[i]);
      char *prev = args_json;
      args_json = str_printf("%s%s\"%s\"", prev, (i == 0 ? "" : ","), arg_esc ? arg_esc : "");
      free(prev);
      free(arg_esc);
    }
    char *dst_esc = json_escape(dst);
    char *ins = str_printf(
        "{\"op\":\"call_static\",\"dst\":\"%s\",\"callee\":\"%s\",\"args\":[%s],\"variadic\":%s}", dst_esc ? dst_esc : "",
        callee_esc ? callee_esc : "", args_json ? args_json : "", (sig && sig->variadic) ? "true" : "false");
    free(dst_esc);
    free(callee_esc);
    free(full);
    free(args_json);
    if (!ir_emit(ctx, ins)) {
      free(dst);
      dst = NULL;
    }
  } else if (strcmp(callee->kind, "MemberExpr") == 0 && callee->child_len > 0) {
    AstNode *recv_ast = callee->children[0];
    if (strcmp(recv_ast->kind, "Identifier") == 0) {
      ProtoInfo *proto = proto_find(ctx->protos, recv_ast->text ? recv_ast->text : "");
      if (proto) {
        char *dst_esc = json_escape(dst);
        char *args_json = strdup("");
        for (size_t i = 0; i < argc; i++) {
          char *arg_esc = json_escape(args[i]);
          char *prev = args_json;
          args_json = str_printf("%s%s\"%s\"", prev, (i == 0 ? "" : ","), arg_esc ? arg_esc : "");
          free(prev);
          free(arg_esc);
        }
        char *callee_full = NULL;
        if (strcmp(callee->text ? callee->text : "", "clone") == 0) {
          callee_full = str_printf("%s.clone", proto->name ? proto->name : "");
          char *callee_esc = json_escape(callee_full ? callee_full : "");
          char *ins = str_printf("{\"op\":\"call_static\",\"dst\":\"%s\",\"callee\":\"%s\",\"args\":[],\"variadic\":false}",
                                 dst_esc ? dst_esc : "", callee_esc ? callee_esc : "");
          free(callee_esc);
          free(callee_full);
          free(args_json);
          if (!ir_emit(ctx, ins)) {
            free(dst);
            dst = NULL;
          }
          for (size_t i = 0; i < argc; i++) free(args[i]);
          free(args);
          return dst;
        }
        callee_full = str_printf("%s.%s", proto->name ? proto->name : "", callee->text ? callee->text : "");
        char *callee_esc = json_escape(callee_full ? callee_full : "");
        char *ins = str_printf(
            "{\"op\":\"call_static\",\"dst\":\"%s\",\"callee\":\"%s\",\"args\":[%s],\"variadic\":false}",
            dst_esc ? dst_esc : "", callee_esc ? callee_esc : "", args_json ? args_json : "");
        free(dst_esc);
        free(callee_esc);
        free(callee_full);
        free(args_json);
        if (!ir_emit(ctx, ins)) {
          free(dst);
          dst = NULL;
        }
        for (size_t i = 0; i < argc; i++) free(args[i]);
        free(args);
        return dst;
      }
      ImportNamespace *ns = ir_find_namespace(ctx, recv_ast->text ? recv_ast->text : "");
      if (ns) {
        char *dst_esc = json_escape(dst);
        char *callee_full = str_printf("%s.%s", ns->module ? ns->module : "", callee->text ? callee->text : "");
        char *callee_esc = json_escape(callee_full ? callee_full : "");
        char *args_json = strdup("");
        for (size_t i = 0; i < argc; i++) {
          char *arg_esc = json_escape(args[i]);
          char *prev = args_json;
          args_json = str_printf("%s%s\"%s\"", prev, (i == 0 ? "" : ","), arg_esc ? arg_esc : "");
          free(prev);
          free(arg_esc);
        }
        char *ins = str_printf(
            "{\"op\":\"call_static\",\"dst\":\"%s\",\"callee\":\"%s\",\"args\":[%s],\"variadic\":false}",
            dst_esc ? dst_esc : "", callee_esc ? callee_esc : "", args_json ? args_json : "");
        free(dst_esc);
        free(callee_full);
        free(callee_esc);
        free(args_json);
        if (!ir_emit(ctx, ins)) {
          free(dst);
          dst = NULL;
        }
        for (size_t i = 0; i < argc; i++) free(args[i]);
        free(args);
        return dst;
      }
    }
    char *recv = ir_lower_expr(recv_ast, ctx);
    if (!recv) {
      free(dst);
      dst = NULL;
    } else {
      char *recv_type = ir_guess_expr_type(recv_ast, ctx);
      char *recv_esc = json_escape(recv);
      char *dst_esc = json_escape(dst);
      char *method_esc = json_escape(callee->text ? callee->text : "");
      if (recv_type && proto_find(ctx->protos, recv_type)) {
        char *args_json = strdup("");
        char *recv_arg = json_escape(recv);
        char *prev = args_json;
        args_json = str_printf("%s\"%s\"", prev ? prev : "", recv_arg ? recv_arg : "");
        free(prev);
        free(recv_arg);
        for (size_t i = 0; i < argc; i++) {
          char *arg_esc = json_escape(args[i]);
          char *prev2 = args_json;
          args_json = str_printf("%s,\"%s\"", prev2 ? prev2 : "", arg_esc ? arg_esc : "");
          free(prev2);
          free(arg_esc);
        }
        char *callee_full = str_printf("%s.%s", recv_type, callee->text ? callee->text : "");
        char *callee_esc = json_escape(callee_full ? callee_full : "");
        char *ins = str_printf(
            "{\"op\":\"call_static\",\"dst\":\"%s\",\"callee\":\"%s\",\"args\":[%s],\"variadic\":false}",
            dst_esc ? dst_esc : "", callee_esc ? callee_esc : "", args_json ? args_json : "");
        free(callee_full);
        free(callee_esc);
        free(args_json);
        if (!ir_emit(ctx, ins)) {
          free(dst);
          dst = NULL;
        }
        free(recv_esc);
        free(dst_esc);
        free(method_esc);
        free(recv);
        free(recv_type);
        for (size_t i = 0; i < argc; i++) free(args[i]);
        free(args);
        return dst;
      }
      if (strcmp(callee->text ? callee->text : "", "toString") == 0) {
        char *ins = str_printf("{\"op\":\"call_builtin_tostring\",\"dst\":\"%s\",\"value\":\"%s\"}", dst_esc ? dst_esc : "",
                               recv_esc ? recv_esc : "");
        if (!ir_emit(ctx, ins)) {
          free(dst);
          dst = NULL;
        }
      } else if (strcmp(callee->text ? callee->text : "", "view") == 0 || strcmp(callee->text ? callee->text : "", "slice") == 0) {
        const char *kind = callee->text ? callee->text : "";
        char *offset = NULL;
        char *len = NULL;
        if (argc == 0) {
          offset = ir_next_tmp(ctx);
          if (offset) {
            char *o_esc = json_escape(offset);
            char *ins0 = str_printf("{\"op\":\"const\",\"dst\":\"%s\",\"literalType\":\"int\",\"value\":\"0\"}", o_esc ? o_esc : "");
            free(o_esc);
            if (!ir_emit(ctx, ins0)) {
              free(offset);
              offset = NULL;
            }
          }
          len = ir_next_tmp(ctx);
          if (len) {
            char *d_esc = json_escape(len);
            char *r_esc = json_escape(recv);
            char *insl = str_printf(
                "{\"op\":\"call_method_static\",\"dst\":\"%s\",\"receiver\":\"%s\",\"method\":\"length\",\"args\":[]}", d_esc ? d_esc : "",
                r_esc ? r_esc : "");
            free(d_esc);
            free(r_esc);
            if (!ir_emit(ctx, insl)) {
              free(len);
              len = NULL;
            }
          }
        } else if (argc >= 2) {
          offset = args[0] ? strdup(args[0]) : NULL;
          len = args[1] ? strdup(args[1]) : NULL;
        }
        if (offset && len) {
          char *t_esc = json_escape(recv);
          char *o_esc = json_escape(offset);
          char *l_esc = json_escape(len);
          char *chk = str_printf("{\"op\":\"check_view_bounds\",\"target\":\"%s\",\"offset\":\"%s\",\"len\":\"%s\"}", t_esc ? t_esc : "",
                                 o_esc ? o_esc : "", l_esc ? l_esc : "");
          free(t_esc);
          free(o_esc);
          free(l_esc);
          if (!ir_emit(ctx, chk)) {
            free(dst);
            dst = NULL;
          }
          char *d_esc2 = json_escape(dst);
          char *s_esc = json_escape(recv);
          char *o_esc2 = json_escape(offset);
          char *l_esc2 = json_escape(len);
          char *k_esc = json_escape(kind);
          char *ins = str_printf(
              "{\"op\":\"make_view\",\"dst\":\"%s\",\"kind\":\"%s\",\"source\":\"%s\",\"offset\":\"%s\",\"len\":\"%s\",\"readonly\":%s}",
              d_esc2 ? d_esc2 : "", k_esc ? k_esc : "", s_esc ? s_esc : "", o_esc2 ? o_esc2 : "", l_esc2 ? l_esc2 : "",
              strcmp(kind, "view") == 0 ? "true" : "false");
          free(d_esc2);
          free(s_esc);
          free(o_esc2);
          free(l_esc2);
          free(k_esc);
          if (!ir_emit(ctx, ins)) {
            free(dst);
            dst = NULL;
          }
        }
        free(offset);
        free(len);
      } else if (strcmp(callee->text ? callee->text : "", "print") == 0 && strcmp(recv_ast->kind, "Identifier") == 0 &&
                 recv_ast->text && strcmp(recv_ast->text, "Sys") == 0) {
        char *args_json = strdup("");
        for (size_t i = 0; i < argc; i++) {
          char *arg_esc = json_escape(args[i]);
          char *prev = args_json;
          args_json = str_printf("%s%s\"%s\"", prev, (i == 0 ? "" : ","), arg_esc ? arg_esc : "");
          free(prev);
          free(arg_esc);
        }
        char *ins = str_printf("{\"op\":\"call_builtin_print\",\"args\":[%s]}", args_json ? args_json : "");
        free(args_json);
        if (!ir_emit(ctx, ins)) {
          free(dst);
          dst = NULL;
        }
      } else {
        char *args_json = strdup("");
        for (size_t i = 0; i < argc; i++) {
          char *arg_esc = json_escape(args[i]);
          char *prev = args_json;
          args_json = str_printf("%s%s\"%s\"", prev, (i == 0 ? "" : ","), arg_esc ? arg_esc : "");
          free(prev);
          free(arg_esc);
        }
        char *ins = str_printf(
            "{\"op\":\"call_method_static\",\"dst\":\"%s\",\"receiver\":\"%s\",\"method\":\"%s\",\"args\":[%s]}",
            dst_esc ? dst_esc : "", recv_esc ? recv_esc : "", method_esc ? method_esc : "", args_json ? args_json : "");
        free(args_json);
        if (!ir_emit(ctx, ins)) {
          free(dst);
          dst = NULL;
        }
      }
      free(recv_esc);
      free(dst_esc);
      free(method_esc);
      free(recv);
      free(recv_type);
    }
  } else {
    char *dst_esc = json_escape(dst);
    char *ins = str_printf("{\"op\":\"call_unknown\",\"dst\":\"%s\"}", dst_esc ? dst_esc : "");
    free(dst_esc);
    if (!ir_emit(ctx, ins)) {
      free(dst);
      dst = NULL;
    }
  }

  for (size_t i = 0; i < argc; i++) free(args[i]);
  free(args);
  return dst;
}

static char *ir_lower_expr(AstNode *e, IrFnCtx *ctx) {
  if (!e) return NULL;
  ir_set_loc(ctx, e);
  if (strcmp(e->kind, "Literal") == 0) {
    char *dst = ir_next_tmp(ctx);
    if (!dst) return NULL;
    char *dst_esc = json_escape(dst);
    char *val_esc = json_escape(e->text ? e->text : "");
    const char *lt = "string";
    if (e->text && (strcmp(e->text, "true") == 0 || strcmp(e->text, "false") == 0)) lt = "bool";
    else if (e->text && (is_all_digits(e->text) || is_hex_token(e->text) || is_bin_token(e->text) || is_float_token(e->text)))
      lt = is_float_token(e->text) ? "float" : "int";
    char *ins = NULL;
    if (strcmp(lt, "bool") == 0) {
      ins = str_printf("{\"op\":\"const\",\"dst\":\"%s\",\"literalType\":\"bool\",\"value\":%s}", dst_esc ? dst_esc : "",
                       (e->text && strcmp(e->text, "true") == 0) ? "true" : "false");
    } else {
      ins = str_printf("{\"op\":\"const\",\"dst\":\"%s\",\"literalType\":\"%s\",\"value\":\"%s\"}", dst_esc ? dst_esc : "", lt,
                       val_esc ? val_esc : "");
    }
    free(dst_esc);
    free(val_esc);
    if (!ir_emit(ctx, ins)) {
      free(dst);
      return NULL;
    }
    return dst;
  }
  if (strcmp(e->kind, "CastExpr") == 0) {
    if (e->child_len == 0) return NULL;
    char *recv = ir_lower_expr(e->children[0], ctx);
    if (!recv) return NULL;
    const char *dst_type = e->text ? e->text : "";
    char *src_type = ir_guess_expr_type(e->children[0], ctx);
    if (src_type && dst_type && strcmp(src_type, dst_type) == 0) {
      free(src_type);
      return recv;
    }
    char *dst = ir_next_tmp(ctx);
    if (!dst) {
      free(src_type);
      free(recv);
      return NULL;
    }
    char *recv_esc = json_escape(recv);
    char *dst_esc = json_escape(dst);
    if (strcmp(dst_type, "byte") == 0) {
      if (src_type && strcmp(src_type, "float") == 0) {
        char *tmp = ir_next_tmp(ctx);
        if (tmp) {
          char *tmp_esc = json_escape(tmp);
          char *ins1 = str_printf("{\"op\":\"call_method_static\",\"dst\":\"%s\",\"receiver\":\"%s\",\"method\":\"toInt\",\"args\":[]}", tmp_esc ? tmp_esc : "",
                                  recv_esc ? recv_esc : "");
          free(tmp_esc);
          if (!ir_emit(ctx, ins1)) {
            free(tmp);
            tmp = NULL;
          }
        }
        if (tmp) {
          char *tmp_esc = json_escape(tmp);
          char *ins2 = str_printf("{\"op\":\"call_method_static\",\"dst\":\"%s\",\"receiver\":\"%s\",\"method\":\"toByte\",\"args\":[]}", dst_esc ? dst_esc : "",
                                  tmp_esc ? tmp_esc : "");
          free(tmp_esc);
          if (!ir_emit(ctx, ins2)) {
            free(dst);
            dst = NULL;
          }
          free(tmp);
        }
      } else {
        char *ins = str_printf("{\"op\":\"call_method_static\",\"dst\":\"%s\",\"receiver\":\"%s\",\"method\":\"toByte\",\"args\":[]}", dst_esc ? dst_esc : "",
                               recv_esc ? recv_esc : "");
        if (!ir_emit(ctx, ins)) {
          free(dst);
          dst = NULL;
        }
      }
    } else if (strcmp(dst_type, "int") == 0) {
      char *ins = str_printf("{\"op\":\"call_method_static\",\"dst\":\"%s\",\"receiver\":\"%s\",\"method\":\"toInt\",\"args\":[]}", dst_esc ? dst_esc : "",
                             recv_esc ? recv_esc : "");
      if (!ir_emit(ctx, ins)) {
        free(dst);
        dst = NULL;
      }
    } else if (strcmp(dst_type, "float") == 0) {
      char *ins = str_printf("{\"op\":\"call_method_static\",\"dst\":\"%s\",\"receiver\":\"%s\",\"method\":\"toFloat\",\"args\":[]}", dst_esc ? dst_esc : "",
                             recv_esc ? recv_esc : "");
      if (!ir_emit(ctx, ins)) {
        free(dst);
        dst = NULL;
      }
    } else {
      char *ins = str_printf("{\"op\":\"copy\",\"dst\":\"%s\",\"src\":\"%s\"}", dst_esc ? dst_esc : "", recv_esc ? recv_esc : "");
      if (!ir_emit(ctx, ins)) {
        free(dst);
        dst = NULL;
      }
    }
    free(recv_esc);
    free(dst_esc);
    free(src_type);
    free(recv);
    return dst;
  }
  if (strcmp(e->kind, "Identifier") == 0) {
    char *dst = ir_next_tmp(ctx);
    if (!dst) return NULL;
    char *dst_esc = json_escape(dst);
    const char *mapped = ir_scope_lookup(ctx ? ctx->scope : NULL, e->text ? e->text : "");
    const char *use_name = mapped ? mapped : (e->text ? e->text : "");
    char *name_esc = json_escape(use_name);
    const char *vt = ir_get_var_type(ctx, use_name);
    char *type_esc = json_escape(vt ? vt : "unknown");
    char *ins = str_printf("{\"op\":\"load_var\",\"dst\":\"%s\",\"name\":\"%s\",\"type\":{\"kind\":\"IRType\",\"name\":\"%s\"}}",
                           dst_esc ? dst_esc : "", name_esc ? name_esc : "", type_esc ? type_esc : "unknown");
    free(dst_esc);
    free(name_esc);
    free(type_esc);
    if (!ir_emit(ctx, ins)) {
      free(dst);
      return NULL;
    }
    return dst;
  }
  if (strcmp(e->kind, "BinaryExpr") == 0 && e->child_len >= 2) {
    char *l = ir_lower_expr(e->children[0], ctx);
    if (e->text && (strcmp(e->text, "&&") == 0 || strcmp(e->text, "||") == 0)) {
      if (!l) return NULL;
      char *right_label = ir_next_label(ctx, "logic_right_");
      char *short_label = ir_next_label(ctx, "logic_short_");
      char *done_label = ir_next_label(ctx, "logic_done_");
      char *cont_label = ir_next_label(ctx, "logic_cont_");
      size_t right_idx = 0, short_idx = 0, done_idx = 0;
      size_t cont_idx = 0;
      if (!right_label || !short_label || !done_label || !cont_label ||
          !ir_add_block(ctx, right_label, &right_idx) ||
          !ir_add_block(ctx, short_label, &short_idx) ||
          !ir_add_block(ctx, done_label, &done_idx)) {
        free(l);
        free(right_label);
        free(short_label);
        free(done_label);
        free(cont_label);
        return NULL;
      }
      char *dst = ir_next_tmp(ctx);
      if (!dst) {
        free(l);
        free(right_label);
        free(short_label);
        free(done_label);
        return NULL;
      }
      const char *then_lbl = (strcmp(e->text, "&&") == 0) ? right_label : short_label;
      const char *else_lbl = (strcmp(e->text, "&&") == 0) ? short_label : right_label;
      char *cond_esc = json_escape(l);
      char *then_esc = json_escape(then_lbl);
      char *else_esc = json_escape(else_lbl);
      char *br = str_printf("{\"op\":\"branch_if\",\"cond\":\"%s\",\"then\":\"%s\",\"else\":\"%s\"}", cond_esc ? cond_esc : "",
                            then_esc ? then_esc : "", else_esc ? else_esc : "");
      free(cond_esc);
      free(then_esc);
      free(else_esc);
      if (!ir_emit(ctx, br)) {
        free(l); free(dst); free(right_label); free(short_label); free(done_label); free(cont_label);
        return NULL;
      }

      ctx->cur_block = short_idx;
      char *d_esc = json_escape(dst);
      const char *short_val = (strcmp(e->text, "&&") == 0) ? "false" : "true";
      char *cst = str_printf("{\"op\":\"const\",\"dst\":\"%s\",\"literalType\":\"bool\",\"value\":%s}", d_esc ? d_esc : "", short_val);
      free(d_esc);
      if (!ir_emit(ctx, cst)) {
        free(l); free(dst); free(right_label); free(short_label); free(done_label); free(cont_label);
        return NULL;
      }
      char *done_esc = json_escape(done_label);
      char *jmp_short = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", done_esc ? done_esc : "");
      free(done_esc);
      if (!ir_emit(ctx, jmp_short)) {
        free(l); free(dst); free(right_label); free(short_label); free(done_label); free(cont_label);
        return NULL;
      }

      ctx->cur_block = right_idx;
      char *r = ir_lower_expr(e->children[1], ctx);
      if (!r) {
        free(l); free(dst); free(right_label); free(short_label); free(done_label); free(cont_label);
        return NULL;
      }
      char *r_esc = json_escape(r);
      char *d2_esc = json_escape(dst);
      char *cpy = str_printf("{\"op\":\"copy\",\"dst\":\"%s\",\"src\":\"%s\"}", d2_esc ? d2_esc : "", r_esc ? r_esc : "");
      free(r_esc);
      free(d2_esc);
      free(r);
      if (!ir_emit(ctx, cpy)) {
        free(l); free(dst); free(right_label); free(short_label); free(done_label); free(cont_label);
        return NULL;
      }
      char *done2_esc = json_escape(done_label);
      char *jmp_right = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", done2_esc ? done2_esc : "");
      free(done2_esc);
      if (!ir_emit(ctx, jmp_right)) {
        free(l); free(dst); free(right_label); free(short_label); free(done_label); free(cont_label);
        return NULL;
      }

      if (!ir_add_block(ctx, cont_label, &cont_idx)) {
        free(l); free(dst); free(right_label); free(short_label); free(done_label); free(cont_label);
        return NULL;
      }
      ctx->cur_block = done_idx;
      ir_emit(ctx, strdup("{\"op\":\"nop\"}"));
      char *cont_esc = json_escape(cont_label);
      char *jmp_done = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", cont_esc ? cont_esc : "");
      free(cont_esc);
      if (!ir_emit(ctx, jmp_done)) {
        free(l); free(dst); free(right_label); free(short_label); free(done_label); free(cont_label);
        return NULL;
      }

      ctx->cur_block = cont_idx;
      free(l);
      free(right_label);
      free(short_label);
      free(done_label);
      free(cont_label);
      return dst;
    }
    char *r = ir_lower_expr(e->children[1], ctx);
    char *lt = ir_guess_expr_type(e->children[0], ctx);
    char *rt = ir_guess_expr_type(e->children[1], ctx);
    if (e->text && (strcmp(e->text, "+") == 0 || strcmp(e->text, "-") == 0 || strcmp(e->text, "*") == 0) &&
        ir_is_int_like(lt) && ir_is_int_like(rt)) {
      char *l_esc = json_escape(l), *r_esc = json_escape(r), *op_esc = json_escape(e->text);
      char *chk = str_printf("{\"op\":\"check_int_overflow\",\"operator\":\"%s\",\"left\":\"%s\",\"right\":\"%s\"}", op_esc ? op_esc : "",
                             l_esc ? l_esc : "", r_esc ? r_esc : "");
      free(l_esc); free(r_esc); free(op_esc);
      if (!ir_emit(ctx, chk)) {
        free(l); free(r); free(lt); free(rt);
        return NULL;
      }
    }
    if (e->text && (strcmp(e->text, "/") == 0 || strcmp(e->text, "%") == 0) && ir_is_int_like(lt) && ir_is_int_like(rt)) {
      char *r_esc = json_escape(r);
      char *chk = str_printf("{\"op\":\"check_div_zero\",\"divisor\":\"%s\"}", r_esc ? r_esc : "");
      free(r_esc);
      if (!ir_emit(ctx, chk)) {
        free(l); free(r); free(lt); free(rt);
        return NULL;
      }
    }
    if (e->text && (strcmp(e->text, "<<") == 0 || strcmp(e->text, ">>") == 0) && ir_is_int_like(lt) && ir_is_int_like(rt)) {
      char *r_esc = json_escape(r);
      int width = (lt && strcmp(lt, "byte") == 0) ? 8 : 64;
      char *chk = str_printf("{\"op\":\"check_shift_range\",\"shift\":\"%s\",\"width\":%d}", r_esc ? r_esc : "", width);
      free(r_esc);
      if (!ir_emit(ctx, chk)) {
        free(l); free(r); free(lt); free(rt);
        return NULL;
      }
    }
    char *dst = ir_next_tmp(ctx);
    if (!l || !r || !dst) {
      free(l);
      free(r);
      free(dst);
      free(lt);
      free(rt);
      return NULL;
    }
    char *l_esc = json_escape(l), *r_esc = json_escape(r), *d_esc = json_escape(dst), *op_esc = json_escape(e->text ? e->text : "");
    char *ins =
        str_printf("{\"op\":\"bin_op\",\"dst\":\"%s\",\"operator\":\"%s\",\"left\":\"%s\",\"right\":\"%s\"}", d_esc ? d_esc : "",
                   op_esc ? op_esc : "", l_esc ? l_esc : "", r_esc ? r_esc : "");
    free(l_esc);
    free(r_esc);
    free(d_esc);
    free(op_esc);
    free(l);
    free(r);
    free(lt);
    free(rt);
    if (!ir_emit(ctx, ins)) {
      free(dst);
      return NULL;
    }
    return dst;
  }
  if ((strcmp(e->kind, "UnaryExpr") == 0 || strcmp(e->kind, "PostfixExpr") == 0) && e->child_len >= 1) {
    const int is_prefix = (strcmp(e->kind, "UnaryExpr") == 0);
    const char *op = e->text ? e->text : "";
    if (strcmp(op, "++") == 0 || strcmp(op, "--") == 0) {
      AstNode *target = e->children[0];
      char *tt = ir_guess_expr_type(target, ctx);
      const char *lit_type = (tt && strcmp(tt, "float") == 0) ? "float" : (tt && strcmp(tt, "byte") == 0) ? "byte" : "int";
      const char *lit_val = (strcmp(lit_type, "float") == 0) ? "1.0" : "1";
      const char *bin_op = (strcmp(op, "++") == 0) ? "+" : "-";

      if (target && strcmp(target->kind, "Identifier") == 0) {
        const char *mapped = ir_scope_lookup(ctx ? ctx->scope : NULL, target->text ? target->text : "");
        const char *use_name = mapped ? mapped : (target->text ? target->text : "");
        char *cur = ir_next_tmp(ctx);
        char *one = ir_next_tmp(ctx);
        char *next = ir_next_tmp(ctx);
        if (!cur || !one || !next) {
          free(tt);
          free(cur);
          free(one);
          free(next);
          return NULL;
        }
        char *cur_esc = json_escape(cur);
        char *name_esc = json_escape(use_name);
        char *type_esc = json_escape(tt ? tt : "unknown");
        char *load = str_printf("{\"op\":\"load_var\",\"dst\":\"%s\",\"name\":\"%s\",\"type\":{\"kind\":\"IRType\",\"name\":\"%s\"}}",
                                cur_esc ? cur_esc : "", name_esc ? name_esc : "", type_esc ? type_esc : "unknown");
        free(cur_esc);
        free(name_esc);
        free(type_esc);
        if (!ir_emit(ctx, load)) {
          free(tt);
          return NULL;
        }
        char *one_esc = json_escape(one);
        char *lit_esc = json_escape(lit_val);
        char *cst = str_printf("{\"op\":\"const\",\"dst\":\"%s\",\"literalType\":\"%s\",\"value\":\"%s\"}", one_esc ? one_esc : "",
                               lit_type, lit_esc ? lit_esc : "");
        free(one_esc);
        free(lit_esc);
        if (!ir_emit(ctx, cst)) {
          free(tt);
          return NULL;
        }
        char *next_esc = json_escape(next);
        char *bin_esc = json_escape(bin_op);
        char *cur2_esc = json_escape(cur);
        char *one2_esc = json_escape(one);
        char *bin = str_printf("{\"op\":\"bin_op\",\"dst\":\"%s\",\"operator\":\"%s\",\"left\":\"%s\",\"right\":\"%s\"}",
                               next_esc ? next_esc : "", bin_esc ? bin_esc : "", cur2_esc ? cur2_esc : "", one2_esc ? one2_esc : "");
        free(next_esc);
        free(bin_esc);
        free(cur2_esc);
        free(one2_esc);
        if (!ir_emit(ctx, bin)) {
          free(tt);
          return NULL;
        }
        char *name2_esc = json_escape(use_name);
        char *next2_esc = json_escape(next);
        char *type2_esc = json_escape(tt ? tt : "unknown");
        char *store = str_printf("{\"op\":\"store_var\",\"name\":\"%s\",\"src\":\"%s\",\"type\":{\"kind\":\"IRType\",\"name\":\"%s\"}}",
                                 name2_esc ? name2_esc : "", next2_esc ? next2_esc : "", type2_esc ? type2_esc : "unknown");
        free(name2_esc);
        free(next2_esc);
        free(type2_esc);
        if (!ir_emit(ctx, store)) {
          free(tt);
          return NULL;
        }
        free(tt);
        return is_prefix ? next : cur;
      }

      if (target && strcmp(target->kind, "MemberExpr") == 0 && target->child_len >= 1) {
        char *base = ir_lower_expr(target->children[0], ctx);
        if (!base) {
          free(tt);
          return NULL;
        }
        char *cur = ir_next_tmp(ctx);
        char *one = ir_next_tmp(ctx);
        char *next = ir_next_tmp(ctx);
        if (!cur || !one || !next) {
          free(tt);
          free(base);
          free(cur);
          free(one);
          free(next);
          return NULL;
        }
        char *base_esc = json_escape(base);
        char *cur_esc = json_escape(cur);
        char *name_esc = json_escape(target->text ? target->text : "");
        char *get = str_printf("{\"op\":\"member_get\",\"dst\":\"%s\",\"target\":\"%s\",\"name\":\"%s\"}",
                               cur_esc ? cur_esc : "", base_esc ? base_esc : "", name_esc ? name_esc : "");
        free(base_esc);
        free(cur_esc);
        free(name_esc);
        if (!ir_emit(ctx, get)) {
          free(tt);
          free(base);
          return NULL;
        }
        char *one_esc = json_escape(one);
        char *lit_esc = json_escape(lit_val);
        char *cst = str_printf("{\"op\":\"const\",\"dst\":\"%s\",\"literalType\":\"%s\",\"value\":\"%s\"}", one_esc ? one_esc : "",
                               lit_type, lit_esc ? lit_esc : "");
        free(one_esc);
        free(lit_esc);
        if (!ir_emit(ctx, cst)) {
          free(tt);
          free(base);
          return NULL;
        }
        char *next_esc = json_escape(next);
        char *bin_esc = json_escape(bin_op);
        char *cur2_esc = json_escape(cur);
        char *one2_esc = json_escape(one);
        char *bin = str_printf("{\"op\":\"bin_op\",\"dst\":\"%s\",\"operator\":\"%s\",\"left\":\"%s\",\"right\":\"%s\"}",
                               next_esc ? next_esc : "", bin_esc ? bin_esc : "", cur2_esc ? cur2_esc : "", one2_esc ? one2_esc : "");
        free(next_esc);
        free(bin_esc);
        free(cur2_esc);
        free(one2_esc);
        if (!ir_emit(ctx, bin)) {
          free(tt);
          free(base);
          return NULL;
        }
        char *base2_esc = json_escape(base);
        char *name2_esc = json_escape(target->text ? target->text : "");
        char *next2_esc = json_escape(next);
        char *set = str_printf("{\"op\":\"member_set\",\"target\":\"%s\",\"name\":\"%s\",\"src\":\"%s\"}",
                               base2_esc ? base2_esc : "", name2_esc ? name2_esc : "", next2_esc ? next2_esc : "");
        free(base2_esc);
        free(name2_esc);
        free(next2_esc);
        if (!ir_emit(ctx, set)) {
          free(tt);
          free(base);
          return NULL;
        }
        free(tt);
        free(base);
        return is_prefix ? next : cur;
      }

      if (target && strcmp(target->kind, "IndexExpr") == 0 && target->child_len >= 2) {
        char *t = ir_lower_expr(target->children[0], ctx);
        char *i = ir_lower_expr(target->children[1], ctx);
        char *ttt = ir_guess_expr_type(target->children[0], ctx);
        if (!t || !i) {
          free(tt);
          free(ttt);
          free(t);
          free(i);
          return NULL;
        }
        if (ir_type_is_map(ttt)) {
          char *t_esc = json_escape(t), *i_esc = json_escape(i);
          char *chk = str_printf("{\"op\":\"check_map_has_key\",\"map\":\"%s\",\"key\":\"%s\"}", t_esc ? t_esc : "", i_esc ? i_esc : "");
          free(t_esc); free(i_esc);
          if (!ir_emit(ctx, chk)) {
            free(tt);
            free(ttt);
            free(t);
            free(i);
            return NULL;
          }
        } else {
          char *t_esc = json_escape(t), *i_esc = json_escape(i);
          char *chk = str_printf("{\"op\":\"check_index_bounds\",\"target\":\"%s\",\"index\":\"%s\"}", t_esc ? t_esc : "", i_esc ? i_esc : "");
          free(t_esc); free(i_esc);
          if (!ir_emit(ctx, chk)) {
            free(tt);
            free(ttt);
            free(t);
            free(i);
            return NULL;
          }
        }
        char *cur = ir_next_tmp(ctx);
        char *one = ir_next_tmp(ctx);
        char *next = ir_next_tmp(ctx);
        if (!cur || !one || !next) {
          free(tt);
          free(ttt);
          free(cur);
          free(one);
          free(next);
          free(t);
          free(i);
          return NULL;
        }
        char *t_esc = json_escape(t), *i_esc = json_escape(i), *cur_esc = json_escape(cur);
        char *get = str_printf("{\"op\":\"index_get\",\"dst\":\"%s\",\"target\":\"%s\",\"index\":\"%s\"}",
                               cur_esc ? cur_esc : "", t_esc ? t_esc : "", i_esc ? i_esc : "");
        free(t_esc); free(i_esc); free(cur_esc);
        if (!ir_emit(ctx, get)) {
          free(tt);
          free(ttt);
          free(t);
          free(i);
          return NULL;
        }
        char *one_esc = json_escape(one);
        char *lit_esc = json_escape(lit_val);
        char *cst = str_printf("{\"op\":\"const\",\"dst\":\"%s\",\"literalType\":\"%s\",\"value\":\"%s\"}", one_esc ? one_esc : "",
                               lit_type, lit_esc ? lit_esc : "");
        free(one_esc);
        free(lit_esc);
        if (!ir_emit(ctx, cst)) {
          free(tt);
          free(ttt);
          free(t);
          free(i);
          return NULL;
        }
        char *next_esc = json_escape(next);
        char *bin_esc = json_escape(bin_op);
        char *cur2_esc = json_escape(cur);
        char *one2_esc = json_escape(one);
        char *bin = str_printf("{\"op\":\"bin_op\",\"dst\":\"%s\",\"operator\":\"%s\",\"left\":\"%s\",\"right\":\"%s\"}",
                               next_esc ? next_esc : "", bin_esc ? bin_esc : "", cur2_esc ? cur2_esc : "", one2_esc ? one2_esc : "");
        free(next_esc);
        free(bin_esc);
        free(cur2_esc);
        free(one2_esc);
        if (!ir_emit(ctx, bin)) {
          free(tt);
          free(ttt);
          free(t);
          free(i);
          return NULL;
        }
        char *t2_esc = json_escape(t), *i2_esc = json_escape(i), *next2_esc = json_escape(next);
        char *set = str_printf("{\"op\":\"index_set\",\"target\":\"%s\",\"index\":\"%s\",\"src\":\"%s\"}",
                               t2_esc ? t2_esc : "", i2_esc ? i2_esc : "", next2_esc ? next2_esc : "");
        free(t2_esc); free(i2_esc); free(next2_esc);
        if (!ir_emit(ctx, set)) {
          free(tt);
          free(ttt);
          free(t);
          free(i);
          return NULL;
        }
        free(tt);
        free(ttt);
        free(t);
        free(i);
        return is_prefix ? next : cur;
      }
      free(tt);
    }

    if (strcmp(op, "-") == 0 && e->child_len >= 1) {
      AstNode *child = e->children[0];
      if (child && strcmp(child->kind, "Literal") == 0 && child->text && !is_float_token(child->text)) {
        unsigned long long v = 0;
        if (int_literal_to_u64(child->text, &v) && v == 9223372036854775808ULL) {
          char *dst = ir_next_tmp(ctx);
          if (!dst) return NULL;
          char *d_esc = json_escape(dst);
          char *ins = str_printf("{\"op\":\"const\",\"dst\":\"%s\",\"literalType\":\"int\",\"value\":\"-9223372036854775808\"}",
                                 d_esc ? d_esc : "");
          free(d_esc);
          if (!ir_emit(ctx, ins)) {
            free(dst);
            return NULL;
          }
          return dst;
        }
      }
    }

    char *s = ir_lower_expr(e->children[0], ctx);
    char *dst = ir_next_tmp(ctx);
    if (!s || !dst) {
      free(s);
      free(dst);
      return NULL;
    }
    char *s_esc = json_escape(s), *d_esc = json_escape(dst), *op_esc = json_escape(e->text ? e->text : "");
    char *ins = NULL;
    if (strcmp(e->kind, "UnaryExpr") == 0)
      ins = str_printf("{\"op\":\"unary_op\",\"dst\":\"%s\",\"operator\":\"%s\",\"src\":\"%s\"}", d_esc ? d_esc : "", op_esc ? op_esc : "",
                       s_esc ? s_esc : "");
    else
      ins = str_printf("{\"op\":\"postfix_op\",\"dst\":\"%s\",\"operator\":\"%s\",\"src\":\"%s\"}", d_esc ? d_esc : "", op_esc ? op_esc : "",
                       s_esc ? s_esc : "");
    free(s_esc);
    free(d_esc);
    free(op_esc);
    free(s);
    if (!ir_emit(ctx, ins)) {
      free(dst);
      return NULL;
    }
    return dst;
  }
  if (strcmp(e->kind, "MemberExpr") == 0 && e->child_len >= 1) {
    AstNode *target = e->children[0];
    if (target && strcmp(target->kind, "Identifier") == 0) {
      ImportNamespace *ns = ir_find_namespace(ctx, target->text ? target->text : "");
      if (ns) {
        RegConst *rc = registry_find_const(ctx->registry, ns->module, e->text ? e->text : "");
        if (rc && rc->type) {
          char *dst = ir_next_tmp(ctx);
          if (!dst) return NULL;
          char *d_esc = json_escape(dst);
          const char *lt = rc->type ? rc->type : "unknown";
          char *lt_esc = json_escape(lt);
          char *v_esc = json_escape(rc->value ? rc->value : "");
          char *ins = str_printf("{\"op\":\"const\",\"dst\":\"%s\",\"literalType\":\"%s\",\"value\":\"%s\"}", d_esc ? d_esc : "",
                                 lt_esc ? lt_esc : "", v_esc ? v_esc : "");
          free(d_esc);
          free(v_esc);
          free(lt_esc);
          if (!ir_emit(ctx, ins)) {
            free(dst);
            return NULL;
          }
          return dst;
        }
      }
    }
    char *base = ir_lower_expr(e->children[0], ctx);
    char *dst = ir_next_tmp(ctx);
    if (!base || !dst) {
      free(base);
      free(dst);
      return NULL;
    }
    char *b_esc = json_escape(base), *d_esc = json_escape(dst), *n_esc = json_escape(e->text ? e->text : "");
    char *ins =
        str_printf("{\"op\":\"member_get\",\"dst\":\"%s\",\"target\":\"%s\",\"name\":\"%s\"}", d_esc ? d_esc : "", b_esc ? b_esc : "",
                   n_esc ? n_esc : "");
    free(b_esc);
    free(d_esc);
    free(n_esc);
    free(base);
    if (!ir_emit(ctx, ins)) {
      free(dst);
      return NULL;
    }
    return dst;
  }
  if (strcmp(e->kind, "IndexExpr") == 0 && e->child_len >= 2) {
    char *t = ir_lower_expr(e->children[0], ctx);
    char *i = ir_lower_expr(e->children[1], ctx);
    char *tt = ir_guess_expr_type(e->children[0], ctx);
    char *dst = ir_next_tmp(ctx);
    if (!t || !i || !dst || !tt) {
      free(t);
      free(i);
      free(dst);
      free(tt);
      return NULL;
    }
    if (ir_type_is_map(tt)) {
      char *t_esc = json_escape(t), *i_esc = json_escape(i);
      char *chk = str_printf("{\"op\":\"check_map_has_key\",\"map\":\"%s\",\"key\":\"%s\"}", t_esc ? t_esc : "", i_esc ? i_esc : "");
      free(t_esc); free(i_esc);
      if (!ir_emit(ctx, chk)) {
        free(t); free(i); free(dst); free(tt);
        return NULL;
      }
    } else {
      char *t_esc = json_escape(t), *i_esc = json_escape(i);
      char *chk = str_printf("{\"op\":\"check_index_bounds\",\"target\":\"%s\",\"index\":\"%s\"}", t_esc ? t_esc : "", i_esc ? i_esc : "");
      free(t_esc); free(i_esc);
      if (!ir_emit(ctx, chk)) {
        free(t); free(i); free(dst); free(tt);
        return NULL;
      }
    }
    char *t_esc = json_escape(t), *i_esc = json_escape(i), *d_esc = json_escape(dst);
    char *ins = str_printf("{\"op\":\"index_get\",\"dst\":\"%s\",\"target\":\"%s\",\"index\":\"%s\"}", d_esc ? d_esc : "", t_esc ? t_esc : "",
                           i_esc ? i_esc : "");
    free(t_esc);
    free(i_esc);
    free(d_esc);
    free(t);
    free(i);
    free(tt);
    if (!ir_emit(ctx, ins)) {
      free(dst);
      return NULL;
    }
    return dst;
  }
  if (strcmp(e->kind, "CallExpr") == 0) return ir_lower_call(e, ctx);
  if (strcmp(e->kind, "ConditionalExpr") == 0 && e->child_len >= 3) {
    char *c = ir_lower_expr(e->children[0], ctx);
    char *t = ir_lower_expr(e->children[1], ctx);
    char *f = ir_lower_expr(e->children[2], ctx);
    char *dst = ir_next_tmp(ctx);
    if (!c || !t || !f || !dst) {
      free(c);
      free(t);
      free(f);
      free(dst);
      return NULL;
    }
    char *c_esc = json_escape(c), *t_esc = json_escape(t), *f_esc = json_escape(f), *d_esc = json_escape(dst);
    char *ins = str_printf("{\"op\":\"select\",\"dst\":\"%s\",\"cond\":\"%s\",\"thenValue\":\"%s\",\"elseValue\":\"%s\"}", d_esc ? d_esc : "",
                           c_esc ? c_esc : "", t_esc ? t_esc : "", f_esc ? f_esc : "");
    free(c_esc);
    free(t_esc);
    free(f_esc);
    free(d_esc);
    free(c);
    free(t);
    free(f);
    if (!ir_emit(ctx, ins)) {
      free(dst);
      return NULL;
    }
    return dst;
  }
  if (strcmp(e->kind, "ListLiteral") == 0) {
    char *dst = ir_next_tmp(ctx);
    if (!dst) return NULL;
    char *items = strdup("");
    if (!items) {
      free(dst);
      return NULL;
    }
    for (size_t i = 0; i < e->child_len; i++) {
      char *v = ir_lower_expr(e->children[i], ctx);
      char *v_esc = json_escape(v ? v : "");
      char *prev = items;
      items = str_printf("%s%s\"%s\"", prev, (i == 0 ? "" : ","), v_esc ? v_esc : "");
      free(prev);
      free(v_esc);
      free(v);
      if (!items) {
        free(dst);
        return NULL;
      }
    }
    char *d_esc = json_escape(dst);
    char *ins = str_printf("{\"op\":\"make_list\",\"dst\":\"%s\",\"items\":[%s]}", d_esc ? d_esc : "", items);
    free(d_esc);
    free(items);
    if (!ir_emit(ctx, ins)) {
      free(dst);
      return NULL;
    }
    return dst;
  }
  if (strcmp(e->kind, "MapLiteral") == 0) {
    char *dst = ir_next_tmp(ctx);
    if (!dst) return NULL;
    char *pairs = strdup("");
    if (!pairs) {
      free(dst);
      return NULL;
    }
    for (size_t i = 0; i < e->child_len; i++) {
      AstNode *p = e->children[i];
      if (!p || strcmp(p->kind, "MapPair") != 0 || p->child_len < 2) continue;
      char *k = ir_lower_expr(p->children[0], ctx);
      char *v = ir_lower_expr(p->children[1], ctx);
      char *k_esc = json_escape(k ? k : "");
      char *v_esc = json_escape(v ? v : "");
      char *prev = pairs;
      pairs = str_printf("%s%s{\"key\":\"%s\",\"value\":\"%s\"}", prev, (strlen(prev) == 0 ? "" : ","), k_esc ? k_esc : "",
                         v_esc ? v_esc : "");
      free(prev);
      free(k_esc);
      free(v_esc);
      free(k);
      free(v);
      if (!pairs) {
        free(dst);
        return NULL;
      }
    }
    char *d_esc = json_escape(dst);
    char *ins = str_printf("{\"op\":\"make_map\",\"dst\":\"%s\",\"pairs\":[%s]}", d_esc ? d_esc : "", pairs);
    free(d_esc);
    free(pairs);
    if (!ir_emit(ctx, ins)) {
      free(dst);
      return NULL;
    }
    return dst;
  }
  char *dst = ir_next_tmp(ctx);
  if (!dst) return NULL;
  char *d_esc = json_escape(dst), *k_esc = json_escape(e->kind);
  char *ins = str_printf("{\"op\":\"unknown_expr\",\"dst\":\"%s\",\"kind\":\"%s\"}", d_esc ? d_esc : "", k_esc ? k_esc : "");
  free(d_esc);
  free(k_esc);
  if (!ir_emit(ctx, ins)) {
    free(dst);
    return NULL;
  }
  return dst;
}

static int ir_lower_stmt(AstNode *st, IrFnCtx *ctx) {
  if (!st) return 1;
  ir_set_loc(ctx, st);
  if (strcmp(st->kind, "Block") == 0) {
    IrScope local;
    ir_scope_init(&local, ctx->scope);
    IrScope *prev = ctx->scope;
    ctx->scope = &local;
    for (size_t i = 0; i < st->child_len; i++) {
      if (!ir_lower_stmt(st->children[i], ctx)) {
        ctx->scope = prev;
        ir_scope_free(&local);
        return 0;
      }
    }
    ctx->scope = prev;
    ir_scope_free(&local);
    return 1;
  }
  if (strcmp(st->kind, "VarDecl") == 0) {
    AstNode *tn = ast_child_kind(st, "Type");
    char *type = ast_type_to_ir_name(tn);
    if (!type) type = strdup("unknown");
    char *ir_name = ir_next_var(ctx, st->text ? st->text : "v");
    if (!ir_name || !ir_scope_define(ctx->scope, st->text ? st->text : "", ir_name)) {
      free(type);
      free(ir_name);
      return 0;
    }
    if (!ir_set_var_type(ctx, ir_name, type ? type : "unknown")) {
      free(type);
      free(ir_name);
      return 0;
    }
    char *name_esc = json_escape(ir_name);
    char *type_esc = json_escape(type ? type : "unknown");
    char *ins = str_printf("{\"op\":\"var_decl\",\"name\":\"%s\",\"type\":{\"kind\":\"IRType\",\"name\":\"%s\"}}", name_esc ? name_esc : "",
                           type_esc ? type_esc : "");
    free(name_esc);
    free(type_esc);
    if (!ir_emit(ctx, ins)) { free(type); free(ir_name); return 0; }
    AstNode *last = ast_last_child(st);
    if (last && (!tn || last != tn)) {
      char *v = ir_lower_expr(last, ctx);
      if (!v) { free(type); free(ir_name); return 0; }
      char *v_esc = json_escape(v);
      char *n_esc = json_escape(ir_scope_lookup(ctx->scope, st->text ? st->text : "") ? ir_scope_lookup(ctx->scope, st->text ? st->text : "") : (st->text ? st->text : ""));
      char *ins2 = str_printf("{\"op\":\"store_var\",\"name\":\"%s\",\"src\":\"%s\",\"type\":{\"kind\":\"IRType\",\"name\":\"unknown\"}}",
                              n_esc ? n_esc : "", v_esc ? v_esc : "");
      free(v_esc);
      free(n_esc);
      free(v);
      if (!ir_emit(ctx, ins2)) { free(type); free(ir_name); return 0; }
    } else if (tn) {
      char *dv = ir_emit_default_value(ctx, type, NULL);
      if (!dv) { free(type); free(ir_name); return 0; }
      char *dv_esc = json_escape(dv);
      char *n_esc = json_escape(ir_scope_lookup(ctx->scope, st->text ? st->text : "") ? ir_scope_lookup(ctx->scope, st->text ? st->text : "") : (st->text ? st->text : ""));
      char *ins2 = str_printf("{\"op\":\"store_var\",\"name\":\"%s\",\"src\":\"%s\",\"type\":{\"kind\":\"IRType\",\"name\":\"unknown\"}}",
                              n_esc ? n_esc : "", dv_esc ? dv_esc : "");
      free(dv_esc);
      free(n_esc);
      free(dv);
      if (!ir_emit(ctx, ins2)) { free(type); free(ir_name); return 0; }
    }
    free(type);
    free(ir_name);
    return 1;
  }
  if (strcmp(st->kind, "AssignStmt") == 0 && st->child_len >= 2) {
    AstNode *lhs = st->children[0];
    AstNode *rhs = st->children[1];
    const char *assign_op = st->text ? st->text : "=";
    const char *bin_op = NULL;
    if (strcmp(assign_op, "+=") == 0) bin_op = "+";
    else if (strcmp(assign_op, "-=") == 0) bin_op = "-";
    else if (strcmp(assign_op, "*=") == 0) bin_op = "*";
    else if (strcmp(assign_op, "/=") == 0) bin_op = "/";

    if (bin_op) {
      char *rhs_v = ir_lower_expr(rhs, ctx);
      if (!rhs_v) return 0;
      char *lhs_t = ir_guess_expr_type(lhs, ctx);
      char *rhs_t = ir_guess_expr_type(rhs, ctx);

      if (strcmp(lhs->kind, "Identifier") == 0) {
        char *cur = ir_lower_expr(lhs, ctx);
        if (!cur) {
          free(rhs_v);
          free(lhs_t);
          free(rhs_t);
          return 0;
        }
        if ((strcmp(bin_op, "+") == 0 || strcmp(bin_op, "-") == 0 || strcmp(bin_op, "*") == 0) &&
            ir_is_int_like(lhs_t) && ir_is_int_like(rhs_t)) {
          char *l_esc = json_escape(cur), *r_esc = json_escape(rhs_v), *op_esc = json_escape(bin_op);
          char *chk = str_printf("{\"op\":\"check_int_overflow\",\"operator\":\"%s\",\"left\":\"%s\",\"right\":\"%s\"}", op_esc ? op_esc : "",
                                 l_esc ? l_esc : "", r_esc ? r_esc : "");
          free(l_esc); free(r_esc); free(op_esc);
          if (!ir_emit(ctx, chk)) {
            free(cur); free(rhs_v); free(lhs_t); free(rhs_t);
            return 0;
          }
        }
        if ((strcmp(bin_op, "/") == 0 || strcmp(bin_op, "%") == 0) && ir_is_int_like(lhs_t) && ir_is_int_like(rhs_t)) {
          char *r_esc = json_escape(rhs_v);
          char *chk = str_printf("{\"op\":\"check_div_zero\",\"divisor\":\"%s\"}", r_esc ? r_esc : "");
          free(r_esc);
          if (!ir_emit(ctx, chk)) {
            free(cur); free(rhs_v); free(lhs_t); free(rhs_t);
            return 0;
          }
        }
        char *next = ir_next_tmp(ctx);
        if (!next) {
          free(cur); free(rhs_v); free(lhs_t); free(rhs_t);
          return 0;
        }
        char *l_esc = json_escape(cur), *r_esc = json_escape(rhs_v), *d_esc = json_escape(next), *op_esc = json_escape(bin_op);
        char *ins = str_printf("{\"op\":\"bin_op\",\"dst\":\"%s\",\"operator\":\"%s\",\"left\":\"%s\",\"right\":\"%s\"}", d_esc ? d_esc : "",
                               op_esc ? op_esc : "", l_esc ? l_esc : "", r_esc ? r_esc : "");
        free(l_esc); free(r_esc); free(d_esc); free(op_esc);
        if (!ir_emit(ctx, ins)) {
          free(cur); free(rhs_v); free(next); free(lhs_t); free(rhs_t);
          return 0;
        }
        const char *mapped2 = ir_scope_lookup(ctx->scope, lhs->text ? lhs->text : "");
        const char *use_name2 = mapped2 ? mapped2 : (lhs->text ? lhs->text : "");
        char *n_esc = json_escape(use_name2);
        char *v_esc = json_escape(next);
        char *store = str_printf("{\"op\":\"store_var\",\"name\":\"%s\",\"src\":\"%s\",\"type\":{\"kind\":\"IRType\",\"name\":\"unknown\"}}",
                                 n_esc ? n_esc : "", v_esc ? v_esc : "");
        free(n_esc);
        free(v_esc);
        free(cur);
        free(rhs_v);
        free(next);
        free(lhs_t);
        free(rhs_t);
        return ir_emit(ctx, store);
      }

      if (strcmp(lhs->kind, "MemberExpr") == 0 && lhs->child_len >= 1) {
        char *obj = ir_lower_expr(lhs->children[0], ctx);
        if (!obj) {
          free(rhs_v); free(lhs_t); free(rhs_t);
          return 0;
        }
        char *cur = ir_next_tmp(ctx);
        if (!cur) {
          free(obj); free(rhs_v); free(lhs_t); free(rhs_t);
          return 0;
        }
        char *o_esc = json_escape(obj), *c_esc = json_escape(cur), *n_esc = json_escape(lhs->text ? lhs->text : "");
        char *get = str_printf("{\"op\":\"member_get\",\"dst\":\"%s\",\"target\":\"%s\",\"name\":\"%s\"}", c_esc ? c_esc : "",
                               o_esc ? o_esc : "", n_esc ? n_esc : "");
        free(o_esc); free(c_esc); free(n_esc);
        if (!ir_emit(ctx, get)) {
          free(obj); free(cur); free(rhs_v); free(lhs_t); free(rhs_t);
          return 0;
        }
        if ((strcmp(bin_op, "+") == 0 || strcmp(bin_op, "-") == 0 || strcmp(bin_op, "*") == 0) &&
            ir_is_int_like(lhs_t) && ir_is_int_like(rhs_t)) {
          char *l_esc = json_escape(cur), *r_esc = json_escape(rhs_v), *op_esc = json_escape(bin_op);
          char *chk = str_printf("{\"op\":\"check_int_overflow\",\"operator\":\"%s\",\"left\":\"%s\",\"right\":\"%s\"}", op_esc ? op_esc : "",
                                 l_esc ? l_esc : "", r_esc ? r_esc : "");
          free(l_esc); free(r_esc); free(op_esc);
          if (!ir_emit(ctx, chk)) {
            free(obj); free(cur); free(rhs_v); free(lhs_t); free(rhs_t);
            return 0;
          }
        }
        if ((strcmp(bin_op, "/") == 0 || strcmp(bin_op, "%") == 0) && ir_is_int_like(lhs_t) && ir_is_int_like(rhs_t)) {
          char *r_esc = json_escape(rhs_v);
          char *chk = str_printf("{\"op\":\"check_div_zero\",\"divisor\":\"%s\"}", r_esc ? r_esc : "");
          free(r_esc);
          if (!ir_emit(ctx, chk)) {
            free(obj); free(cur); free(rhs_v); free(lhs_t); free(rhs_t);
            return 0;
          }
        }
        char *next = ir_next_tmp(ctx);
        if (!next) {
          free(obj); free(cur); free(rhs_v); free(lhs_t); free(rhs_t);
          return 0;
        }
        char *l_esc = json_escape(cur), *r_esc = json_escape(rhs_v), *d_esc = json_escape(next), *op_esc = json_escape(bin_op);
        char *bin = str_printf("{\"op\":\"bin_op\",\"dst\":\"%s\",\"operator\":\"%s\",\"left\":\"%s\",\"right\":\"%s\"}", d_esc ? d_esc : "",
                               op_esc ? op_esc : "", l_esc ? l_esc : "", r_esc ? r_esc : "");
        free(l_esc); free(r_esc); free(d_esc); free(op_esc);
        if (!ir_emit(ctx, bin)) {
          free(obj); free(cur); free(rhs_v); free(next); free(lhs_t); free(rhs_t);
          return 0;
        }
        char *o2_esc = json_escape(obj), *n2_esc = json_escape(lhs->text ? lhs->text : ""), *v_esc = json_escape(next);
        char *set = str_printf("{\"op\":\"member_set\",\"target\":\"%s\",\"name\":\"%s\",\"src\":\"%s\"}", o2_esc ? o2_esc : "",
                               n2_esc ? n2_esc : "", v_esc ? v_esc : "");
        free(o2_esc); free(n2_esc); free(v_esc);
        free(obj);
        free(cur);
        free(rhs_v);
        free(next);
        free(lhs_t);
        free(rhs_t);
        return ir_emit(ctx, set);
      }

      if (strcmp(lhs->kind, "IndexExpr") == 0 && lhs->child_len >= 2) {
        char *t = ir_lower_expr(lhs->children[0], ctx);
        char *i = ir_lower_expr(lhs->children[1], ctx);
        char *base_t = ir_guess_expr_type(lhs->children[0], ctx);
        if (!t || !i || !base_t) {
          free(t); free(i); free(base_t); free(rhs_v); free(lhs_t); free(rhs_t);
          return 0;
        }
        if (ir_type_is_map(base_t)) {
          char *t_esc = json_escape(t), *i_esc = json_escape(i);
          char *chk = str_printf("{\"op\":\"check_map_has_key\",\"map\":\"%s\",\"key\":\"%s\"}", t_esc ? t_esc : "", i_esc ? i_esc : "");
          free(t_esc); free(i_esc);
          if (!ir_emit(ctx, chk)) {
            free(t); free(i); free(base_t); free(rhs_v); free(lhs_t); free(rhs_t);
            return 0;
          }
        } else {
          char *t_esc = json_escape(t), *i_esc = json_escape(i);
          char *chk = str_printf("{\"op\":\"check_index_bounds\",\"target\":\"%s\",\"index\":\"%s\"}", t_esc ? t_esc : "", i_esc ? i_esc : "");
          free(t_esc); free(i_esc);
          if (!ir_emit(ctx, chk)) {
            free(t); free(i); free(base_t); free(rhs_v); free(lhs_t); free(rhs_t);
            return 0;
          }
        }
        char *cur = ir_next_tmp(ctx);
        if (!cur) {
          free(t); free(i); free(base_t); free(rhs_v); free(lhs_t); free(rhs_t);
          return 0;
        }
        char *t_esc = json_escape(t), *i_esc = json_escape(i), *c_esc = json_escape(cur);
        char *get = str_printf("{\"op\":\"index_get\",\"dst\":\"%s\",\"target\":\"%s\",\"index\":\"%s\"}", c_esc ? c_esc : "",
                               t_esc ? t_esc : "", i_esc ? i_esc : "");
        free(t_esc); free(i_esc); free(c_esc);
        if (!ir_emit(ctx, get)) {
          free(t); free(i); free(base_t); free(cur); free(rhs_v); free(lhs_t); free(rhs_t);
          return 0;
        }
        if ((strcmp(bin_op, "+") == 0 || strcmp(bin_op, "-") == 0 || strcmp(bin_op, "*") == 0) &&
            ir_is_int_like(lhs_t) && ir_is_int_like(rhs_t)) {
          char *l_esc = json_escape(cur), *r_esc = json_escape(rhs_v), *op_esc = json_escape(bin_op);
          char *chk = str_printf("{\"op\":\"check_int_overflow\",\"operator\":\"%s\",\"left\":\"%s\",\"right\":\"%s\"}", op_esc ? op_esc : "",
                                 l_esc ? l_esc : "", r_esc ? r_esc : "");
          free(l_esc); free(r_esc); free(op_esc);
          if (!ir_emit(ctx, chk)) {
            free(t); free(i); free(base_t); free(cur); free(rhs_v); free(lhs_t); free(rhs_t);
            return 0;
          }
        }
        if ((strcmp(bin_op, "/") == 0 || strcmp(bin_op, "%") == 0) && ir_is_int_like(lhs_t) && ir_is_int_like(rhs_t)) {
          char *r_esc = json_escape(rhs_v);
          char *chk = str_printf("{\"op\":\"check_div_zero\",\"divisor\":\"%s\"}", r_esc ? r_esc : "");
          free(r_esc);
          if (!ir_emit(ctx, chk)) {
            free(t); free(i); free(base_t); free(cur); free(rhs_v); free(lhs_t); free(rhs_t);
            return 0;
          }
        }
        char *next = ir_next_tmp(ctx);
        if (!next) {
          free(t); free(i); free(base_t); free(cur); free(rhs_v); free(lhs_t); free(rhs_t);
          return 0;
        }
        char *l_esc = json_escape(cur), *r_esc = json_escape(rhs_v), *d_esc = json_escape(next), *op_esc = json_escape(bin_op);
        char *bin = str_printf("{\"op\":\"bin_op\",\"dst\":\"%s\",\"operator\":\"%s\",\"left\":\"%s\",\"right\":\"%s\"}", d_esc ? d_esc : "",
                               op_esc ? op_esc : "", l_esc ? l_esc : "", r_esc ? r_esc : "");
        free(l_esc); free(r_esc); free(d_esc); free(op_esc);
        if (!ir_emit(ctx, bin)) {
          free(t); free(i); free(base_t); free(cur); free(rhs_v); free(next); free(lhs_t); free(rhs_t);
          return 0;
        }
        char *t2_esc = json_escape(t), *i2_esc = json_escape(i), *v_esc = json_escape(next);
        char *set = str_printf("{\"op\":\"index_set\",\"target\":\"%s\",\"index\":\"%s\",\"src\":\"%s\"}", t2_esc ? t2_esc : "",
                               i2_esc ? i2_esc : "", v_esc ? v_esc : "");
        free(t2_esc); free(i2_esc); free(v_esc);
        free(t); free(i); free(base_t); free(cur); free(rhs_v); free(next); free(lhs_t); free(rhs_t);
        return ir_emit(ctx, set);
      }

      free(rhs_v);
      free(lhs_t);
      free(rhs_t);
      return ir_emit(ctx, strdup("{\"op\":\"unhandled_stmt\",\"kind\":\"AssignStmt\"}"));
    }

    char *v = ir_lower_expr(rhs, ctx);
    if (!v) return 0;
    if (strcmp(lhs->kind, "Identifier") == 0) {
      char *rhs_type = ir_guess_expr_type(rhs, ctx);
      if (rhs_type) {
        const char *mapped = ir_scope_lookup(ctx->scope, lhs->text ? lhs->text : "");
        const char *use_name = mapped ? mapped : (lhs->text ? lhs->text : "");
        if (!ir_set_var_type(ctx, use_name, rhs_type)) {
          free(rhs_type);
          free(v);
          return 0;
        }
        free(rhs_type);
      }
      const char *mapped2 = ir_scope_lookup(ctx->scope, lhs->text ? lhs->text : "");
      const char *use_name2 = mapped2 ? mapped2 : (lhs->text ? lhs->text : "");
      char *n_esc = json_escape(use_name2);
      char *v_esc = json_escape(v);
      char *ins = str_printf("{\"op\":\"store_var\",\"name\":\"%s\",\"src\":\"%s\",\"type\":{\"kind\":\"IRType\",\"name\":\"unknown\"}}",
                             n_esc ? n_esc : "", v_esc ? v_esc : "");
      free(n_esc);
      free(v_esc);
      free(v);
      return ir_emit(ctx, ins);
    }
    if (strcmp(lhs->kind, "IndexExpr") == 0 && lhs->child_len >= 2) {
      char *lhs_t = ir_guess_expr_type(lhs->children[0], ctx);
      char *t = ir_lower_expr(lhs->children[0], ctx);
      char *i = ir_lower_expr(lhs->children[1], ctx);
      if (!t || !i || !lhs_t) {
        free(v);
        free(t);
        free(i);
        free(lhs_t);
        return 0;
      }
      if (!ir_type_is_map(lhs_t)) {
        char *t_esc = json_escape(t), *i_esc = json_escape(i);
        char *chk = str_printf("{\"op\":\"check_index_bounds\",\"target\":\"%s\",\"index\":\"%s\"}", t_esc ? t_esc : "", i_esc ? i_esc : "");
        free(t_esc);
        free(i_esc);
        if (!ir_emit(ctx, chk)) {
          free(v); free(t); free(i); free(lhs_t);
          return 0;
        }
      }
      char *t_esc = json_escape(t), *i_esc = json_escape(i), *v_esc = json_escape(v);
      char *ins = str_printf("{\"op\":\"index_set\",\"target\":\"%s\",\"index\":\"%s\",\"src\":\"%s\"}", t_esc ? t_esc : "",
                             i_esc ? i_esc : "", v_esc ? v_esc : "");
      free(t_esc);
      free(i_esc);
      free(v_esc);
      free(v);
      free(t);
      free(i);
      free(lhs_t);
      return ir_emit(ctx, ins);
    }
    if (strcmp(lhs->kind, "MemberExpr") == 0 && lhs->child_len >= 1) {
      char *t = ir_lower_expr(lhs->children[0], ctx);
      if (!t) {
        free(v);
        return 0;
      }
      char *t_esc = json_escape(t);
      char *n_esc = json_escape(lhs->text ? lhs->text : "");
      char *v_esc = json_escape(v);
      char *ins = str_printf("{\"op\":\"member_set\",\"target\":\"%s\",\"name\":\"%s\",\"src\":\"%s\"}", t_esc ? t_esc : "",
                             n_esc ? n_esc : "", v_esc ? v_esc : "");
      free(t_esc);
      free(n_esc);
      free(v_esc);
      free(v);
      free(t);
      return ir_emit(ctx, ins);
    }
    free(v);
    return ir_emit(ctx, strdup("{\"op\":\"unhandled_stmt\",\"kind\":\"AssignStmt\"}"));
  }
  if (strcmp(st->kind, "ExprStmt") == 0) {
    if (st->child_len > 0) {
      char *tmp = ir_lower_expr(st->children[0], ctx);
      free(tmp);
    }
    return 1;
  }
  if (strcmp(st->kind, "ReturnStmt") == 0) {
    if (st->child_len == 0) return ir_emit(ctx, strdup("{\"op\":\"ret_void\"}"));
    char *v = ir_lower_expr(st->children[0], ctx);
    if (!v) return 0;
    char *v_esc = json_escape(v);
    char *ins = str_printf("{\"op\":\"ret\",\"value\":\"%s\",\"type\":{\"kind\":\"IRType\",\"name\":\"unknown\"}}", v_esc ? v_esc : "");
    free(v_esc);
    free(v);
    return ir_emit(ctx, ins);
  }
  if (strcmp(st->kind, "ThrowStmt") == 0) {
    char *v = st->child_len > 0 ? ir_lower_expr(st->children[0], ctx) : NULL;
    if (v) {
      char *v_esc = json_escape(v);
      char *f_esc = json_escape(ctx->file ? ctx->file : "");
      char *ins = str_printf("{\"op\":\"throw\",\"value\":\"%s\",\"file\":\"%s\",\"line\":%d,\"col\":%d}",
                             v_esc ? v_esc : "", f_esc ? f_esc : "", st->line, st->col);
      free(v_esc);
      free(f_esc);
      free(v);
      return ir_emit(ctx, ins);
    }
    return ir_emit(ctx, strdup("{\"op\":\"throw\",\"value\":\"\"}"));
  }
  if (strcmp(st->kind, "TryStmt") == 0) {
    AstNode *try_block = NULL;
    AstNode *finally_clause = NULL;
    size_t catch_count = 0;
    for (size_t i = 0; i < st->child_len; i++) {
      AstNode *c = st->children[i];
      if (strcmp(c->kind, "Block") == 0 && !try_block) try_block = c;
      else if (strcmp(c->kind, "CatchClause") == 0) catch_count += 1;
      else if (strcmp(c->kind, "FinallyClause") == 0 && !finally_clause) finally_clause = c;
    }
    AstNode **catches = NULL;
    if (catch_count > 0) {
      catches = (AstNode **)calloc(catch_count, sizeof(AstNode *));
      if (!catches) return 0;
      size_t ci = 0;
      for (size_t i = 0; i < st->child_len; i++) {
        AstNode *c = st->children[i];
        if (strcmp(c->kind, "CatchClause") == 0 && ci < catch_count) catches[ci++] = c;
      }
    }
    for (size_t i = 0; i < st->child_len; i++) {
      AstNode *c = st->children[i];
      if (strcmp(c->kind, "Block") == 0 && !try_block) try_block = c;
    }
    if (!try_block) {
      free(catches);
      return 0;
    }
    char *try_label = ir_next_label(ctx, "try_body_");
    char *dispatch_label = catch_count > 0 ? ir_next_label(ctx, "try_dispatch_") : NULL;
    char *rethrow_label = catch_count > 0 ? ir_next_label(ctx, "try_rethrow_") : NULL;
    char *finally_label = finally_clause ? ir_next_label(ctx, "try_finally_") : NULL;
    char *finally_rethrow_label = (finally_clause && catch_count == 0) ? ir_next_label(ctx, "try_finally_rethrow_") : NULL;
    char *done_label = ir_next_label(ctx, "try_done_");
    if (!try_label || !done_label || (catch_count > 0 && (!dispatch_label || !rethrow_label)) ||
        (finally_clause && !finally_label) || (finally_clause && catch_count == 0 && !finally_rethrow_label)) {
      free(catches);
      free(try_label); free(dispatch_label); free(rethrow_label); free(finally_label); free(finally_rethrow_label); free(done_label);
      return 0;
    }
    size_t try_idx = 0, finally_idx = 0, done_idx = 0;
    size_t finally_rethrow_idx = 0;
    char **dispatch_labels = NULL;
    char **catch_labels = NULL;
    size_t *dispatch_idxs = NULL;
    size_t *catch_idxs = NULL;
    size_t rethrow_idx = 0;
    if (catch_count > 0) {
      dispatch_labels = (char **)calloc(catch_count, sizeof(char *));
      catch_labels = (char **)calloc(catch_count, sizeof(char *));
      dispatch_idxs = (size_t *)calloc(catch_count, sizeof(size_t));
      catch_idxs = (size_t *)calloc(catch_count, sizeof(size_t));
      if (!dispatch_labels || !catch_labels || !dispatch_idxs || !catch_idxs) {
        free(catches); free(try_label); free(dispatch_label); free(rethrow_label); free(finally_label); free(finally_rethrow_label); free(done_label);
        free(dispatch_labels); free(catch_labels); free(dispatch_idxs); free(catch_idxs);
        return 0;
      }
      dispatch_labels[0] = dispatch_label;
      for (size_t i = 1; i < catch_count; i++) {
        dispatch_labels[i] = ir_next_label(ctx, "try_dispatch_");
        if (!dispatch_labels[i]) {
          free(catches); free(try_label); free(rethrow_label); free(finally_label); free(finally_rethrow_label); free(done_label);
          for (size_t k = 0; k < i; k++) free(dispatch_labels[k]);
          free(dispatch_labels); free(catch_labels); free(dispatch_idxs); free(catch_idxs);
          return 0;
        }
      }
      for (size_t i = 0; i < catch_count; i++) {
        catch_labels[i] = ir_next_label(ctx, "try_catch_");
        if (!catch_labels[i]) {
          free(catches); free(try_label); free(rethrow_label); free(finally_label); free(finally_rethrow_label); free(done_label);
          for (size_t k = 0; k < catch_count; k++) { if (dispatch_labels[k]) free(dispatch_labels[k]); if (catch_labels[k]) free(catch_labels[k]); }
          free(dispatch_labels); free(catch_labels); free(dispatch_idxs); free(catch_idxs);
          return 0;
        }
      }
    }
    if (!ir_add_block(ctx, try_label, &try_idx)) {
      free(catches); free(try_label); free(dispatch_label); free(rethrow_label); free(finally_label); free(finally_rethrow_label); free(done_label);
      if (catch_count > 0) { for (size_t k = 0; k < catch_count; k++) { free(dispatch_labels[k]); free(catch_labels[k]); } free(dispatch_labels); free(catch_labels); free(dispatch_idxs); free(catch_idxs); }
      return 0;
    }
    if (catch_count > 0) {
      for (size_t i = 0; i < catch_count; i++) {
        if (!ir_add_block(ctx, dispatch_labels[i], &dispatch_idxs[i]) || !ir_add_block(ctx, catch_labels[i], &catch_idxs[i])) {
          free(catches); free(try_label); free(dispatch_label); free(rethrow_label); free(finally_label); free(finally_rethrow_label); free(done_label);
          for (size_t k = 0; k < catch_count; k++) { free(dispatch_labels[k]); free(catch_labels[k]); }
          free(dispatch_labels); free(catch_labels); free(dispatch_idxs); free(catch_idxs);
          return 0;
        }
      }
      if (!ir_add_block(ctx, rethrow_label, &rethrow_idx)) {
        free(catches); free(try_label); free(dispatch_label); free(rethrow_label); free(finally_label); free(finally_rethrow_label); free(done_label);
        for (size_t k = 0; k < catch_count; k++) { free(dispatch_labels[k]); free(catch_labels[k]); }
        free(dispatch_labels); free(catch_labels); free(dispatch_idxs); free(catch_idxs);
        return 0;
      }
    }
    if ((finally_label && !ir_add_block(ctx, finally_label, &finally_idx)) ||
        (finally_rethrow_label && !ir_add_block(ctx, finally_rethrow_label, &finally_rethrow_idx)) ||
        !ir_add_block(ctx, done_label, &done_idx)) {
      free(catches); free(try_label); free(dispatch_label); free(rethrow_label); free(finally_label); free(finally_rethrow_label); free(done_label);
      if (catch_count > 0) { for (size_t k = 0; k < catch_count; k++) { free(dispatch_labels[k]); free(catch_labels[k]); } free(dispatch_labels); free(catch_labels); free(dispatch_idxs); free(catch_idxs); }
      return 0;
    }
    const char *handler = (catch_count > 0) ? dispatch_label
                            : (finally_rethrow_label ? finally_rethrow_label : (finally_label ? finally_label : done_label));
    char *h_esc = json_escape(handler);
    char *push = str_printf("{\"op\":\"push_handler\",\"target\":\"%s\"}", h_esc ? h_esc : "");
    free(h_esc);
    if (!ir_emit(ctx, push)) {
      free(catches); free(try_label); free(dispatch_label); free(rethrow_label); free(finally_label); free(finally_rethrow_label); free(done_label);
      if (catch_count > 0) { for (size_t k = 0; k < catch_count; k++) { free(dispatch_labels[k]); free(catch_labels[k]); } free(dispatch_labels); free(catch_labels); free(dispatch_idxs); free(catch_idxs); }
      return 0;
    }
    char *tgt = json_escape(try_label);
    char *jmp = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", tgt ? tgt : "");
    free(tgt);
    if (!ir_emit(ctx, jmp)) {
      free(catches); free(try_label); free(dispatch_label); free(rethrow_label); free(finally_label); free(finally_rethrow_label); free(done_label);
      if (catch_count > 0) { for (size_t k = 0; k < catch_count; k++) { free(dispatch_labels[k]); free(catch_labels[k]); } free(dispatch_labels); free(catch_labels); free(dispatch_idxs); free(catch_idxs); }
      return 0;
    }
    ctx->cur_block = try_idx;
    if (!ir_lower_stmt(try_block, ctx)) {
      free(catches); free(try_label); free(dispatch_label); free(rethrow_label); free(finally_label); free(finally_rethrow_label); free(done_label);
      if (catch_count > 0) { for (size_t k = 0; k < catch_count; k++) { free(dispatch_labels[k]); free(catch_labels[k]); } free(dispatch_labels); free(catch_labels); free(dispatch_idxs); free(catch_idxs); }
      return 0;
    }
    if (!ir_emit(ctx, strdup("{\"op\":\"pop_handler\"}"))) {
      free(catches); free(try_label); free(dispatch_label); free(rethrow_label); free(finally_label); free(finally_rethrow_label); free(done_label);
      if (catch_count > 0) { for (size_t k = 0; k < catch_count; k++) { free(dispatch_labels[k]); free(catch_labels[k]); } free(dispatch_labels); free(catch_labels); free(dispatch_idxs); free(catch_idxs); }
      return 0;
    }
    char *after_try = json_escape(finally_label ? finally_label : done_label);
    char *jmp2 = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", after_try ? after_try : "");
    free(after_try);
    if (!ir_emit(ctx, jmp2)) {
      free(catches); free(try_label); free(dispatch_label); free(rethrow_label); free(finally_label); free(finally_rethrow_label); free(done_label);
      if (catch_count > 0) { for (size_t k = 0; k < catch_count; k++) { free(dispatch_labels[k]); free(catch_labels[k]); } free(dispatch_labels); free(catch_labels); free(dispatch_idxs); free(catch_idxs); }
      return 0;
    }
    if (catch_count > 0) {
      char *ex = ir_next_tmp(ctx);
      if (!ex) {
        free(catches); free(try_label); free(dispatch_label); free(rethrow_label); free(finally_label); free(finally_rethrow_label); free(done_label);
        for (size_t k = 0; k < catch_count; k++) { free(dispatch_labels[k]); free(catch_labels[k]); }
        free(dispatch_labels); free(catch_labels); free(dispatch_idxs); free(catch_idxs);
        return 0;
      }
      // Dispatch blocks
      for (size_t i = 0; i < catch_count; i++) {
        ctx->cur_block = dispatch_idxs[i];
        if (i == 0) {
          char *ex_esc = json_escape(ex);
          char *get = str_printf("{\"op\":\"get_exception\",\"dst\":\"%s\"}", ex_esc ? ex_esc : "");
          free(ex_esc);
          if (!ir_emit(ctx, get)) { free(ex); goto try_cleanup; }
        }
        AstNode *tn = ast_child_kind(catches[i], "Type");
        char *type = ast_type_to_ir_name(tn);
        if (!type) type = strdup("unknown");
        char *cond = ir_next_tmp(ctx);
        char *cond_esc = json_escape(cond);
        char *ex_esc2 = json_escape(ex);
        char *type_esc = json_escape(type);
        char *match = str_printf("{\"op\":\"exception_is\",\"dst\":\"%s\",\"value\":\"%s\",\"type\":\"%s\"}", cond_esc ? cond_esc : "",
                                 ex_esc2 ? ex_esc2 : "", type_esc ? type_esc : "");
        free(cond_esc); free(ex_esc2); free(type_esc); free(type);
        if (!ir_emit(ctx, match)) { free(cond); free(ex); goto try_cleanup; }
        const char *else_lbl = (i + 1 < catch_count) ? dispatch_labels[i + 1] : rethrow_label;
        char *cond2 = json_escape(cond);
        char *then_esc = json_escape(catch_labels[i]);
        char *else_esc = json_escape(else_lbl);
        char *br = str_printf("{\"op\":\"branch_if\",\"cond\":\"%s\",\"then\":\"%s\",\"else\":\"%s\"}", cond2 ? cond2 : "",
                              then_esc ? then_esc : "", else_esc ? else_esc : "");
        free(cond2); free(then_esc); free(else_esc); free(cond);
        if (!ir_emit(ctx, br)) { free(ex); goto try_cleanup; }
      }
      ctx->cur_block = rethrow_idx;
      if (!ir_emit(ctx, strdup("{\"op\":\"rethrow\"}"))) { free(ex); goto try_cleanup; }

      for (size_t i = 0; i < catch_count; i++) {
        ctx->cur_block = catch_idxs[i];
        AstNode *tn = ast_child_kind(catches[i], "Type");
        char *type = ast_type_to_ir_name(tn);
        if (!type) type = strdup("unknown");
        if (!ir_set_var_type(ctx, catches[i]->text ? catches[i]->text : "", type ? type : "unknown")) {
          free(type); free(ex); goto try_cleanup;
        }
        char *name_esc = json_escape(catches[i]->text ? catches[i]->text : "");
        char *type_esc = json_escape(type ? type : "unknown");
        char *decl = str_printf("{\"op\":\"var_decl\",\"name\":\"%s\",\"type\":{\"kind\":\"IRType\",\"name\":\"%s\"}}", name_esc ? name_esc : "",
                                type_esc ? type_esc : "");
        free(name_esc); free(type_esc); free(type);
        if (!ir_emit(ctx, decl)) { free(ex); goto try_cleanup; }
        char *n_esc = json_escape(catches[i]->text ? catches[i]->text : "");
        char *ex2 = json_escape(ex);
        char *store = str_printf("{\"op\":\"store_var\",\"name\":\"%s\",\"src\":\"%s\",\"type\":{\"kind\":\"IRType\",\"name\":\"unknown\"}}",
                                 n_esc ? n_esc : "", ex2 ? ex2 : "");
        free(n_esc); free(ex2);
        if (!ir_emit(ctx, store)) { free(ex); goto try_cleanup; }
        AstNode *cblk = ast_child_kind(catches[i], "Block");
        if (cblk && !ir_lower_stmt(cblk, ctx)) { free(ex); goto try_cleanup; }
        char *after_c = json_escape(finally_label ? finally_label : done_label);
        char *jmpc = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", after_c ? after_c : "");
        free(after_c);
        if (!ir_emit(ctx, jmpc)) { free(ex); goto try_cleanup; }
      }
      free(ex);
    }
    if (finally_clause) {
      ctx->cur_block = finally_idx;
      AstNode *fblk = ast_child_kind(finally_clause, "Block");
      if (fblk && !ir_lower_stmt(fblk, ctx)) {
        goto try_cleanup;
        return 0;
      }
      char *aft = json_escape(done_label);
      char *jmpf = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", aft ? aft : "");
      free(aft);
      if (!ir_emit(ctx, jmpf)) {
        goto try_cleanup;
        return 0;
      }
    }
    if (finally_rethrow_label) {
      ctx->cur_block = finally_rethrow_idx;
      AstNode *fblk2 = ast_child_kind(finally_clause, "Block");
      if (fblk2 && !ir_lower_stmt(fblk2, ctx)) {
        goto try_cleanup;
        return 0;
      }
      if (!ir_emit(ctx, strdup("{\"op\":\"rethrow\"}"))) {
        goto try_cleanup;
        return 0;
      }
    }
    ctx->cur_block = done_idx;
    if (!ir_emit(ctx, strdup("{\"op\":\"nop\"}"))) {
      goto try_cleanup;
    }
    free(try_label); free(rethrow_label); free(finally_label); free(finally_rethrow_label); free(done_label);
    if (catch_count > 0) {
      for (size_t k = 0; k < catch_count; k++) { free(dispatch_labels[k]); free(catch_labels[k]); }
      free(dispatch_labels); free(catch_labels); free(dispatch_idxs); free(catch_idxs);
    }
    free(catches);
    return 1;
  try_cleanup:
    free(try_label); free(rethrow_label); free(finally_label); free(finally_rethrow_label); free(done_label);
    if (catch_count > 0) {
      for (size_t k = 0; k < catch_count; k++) { if (dispatch_labels) free(dispatch_labels[k]); if (catch_labels) free(catch_labels[k]); }
      free(dispatch_labels); free(catch_labels); free(dispatch_idxs); free(catch_idxs);
    }
    free(catches);
    return 0;
  }
  if (strcmp(st->kind, "BreakStmt") == 0) {
    if (!ctx->break_targets || !ctx->break_targets->break_label) {
      return ir_emit(ctx, str_printf("{\"op\":\"unhandled_stmt\",\"kind\":\"%s\"}", st->kind));
    }
    char *t_esc = json_escape(ctx->break_targets->break_label);
    char *j = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", t_esc ? t_esc : "");
    free(t_esc);
    return ir_emit(ctx, j);
  }
  if (strcmp(st->kind, "ContinueStmt") == 0) {
    if (!ctx->loop_targets || !ctx->loop_targets->continue_label) {
      return ir_emit(ctx, str_printf("{\"op\":\"unhandled_stmt\",\"kind\":\"%s\"}", st->kind));
    }
    char *t_esc = json_escape(ctx->loop_targets->continue_label);
    char *j = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", t_esc ? t_esc : "");
    free(t_esc);
    return ir_emit(ctx, j);
  }
  if (strcmp(st->kind, "IfStmt") == 0) {
    AstNode *cond = (st->child_len > 0) ? st->children[0] : NULL;
    AstNode *then_st = (st->child_len > 1) ? st->children[1] : NULL;
    AstNode *else_st = (st->child_len > 2) ? st->children[2] : NULL;
    char *cv = cond ? ir_lower_expr(cond, ctx) : NULL;
    if (!cv) return 0;
    char *then_label = ir_next_label(ctx, "if_then_");
    char *done_label = ir_next_label(ctx, "if_done_");
    char *else_label = else_st ? ir_next_label(ctx, "if_else_") : done_label;
    if (!then_label || !done_label || !else_label) {
      free(cv);
      free(then_label);
      free(done_label);
      if (else_st) free(else_label);
      return 0;
    }
    size_t then_idx = 0, else_idx = 0, done_idx = 0;
    if (!ir_add_block(ctx, then_label, &then_idx) || !ir_add_block(ctx, done_label, &done_idx)) {
      free(cv); free(then_label); free(done_label);
      if (else_st) free(else_label);
      return 0;
    }
    if (else_st && !ir_add_block(ctx, else_label, &else_idx)) {
      free(cv); free(then_label); free(done_label); free(else_label);
      return 0;
    }

    char *c_esc = json_escape(cv);
    char *t_esc = json_escape(then_label);
    char *e_esc = json_escape(else_label);
    char *br = str_printf("{\"op\":\"branch_if\",\"cond\":\"%s\",\"then\":\"%s\",\"else\":\"%s\"}", c_esc ? c_esc : "",
                          t_esc ? t_esc : "", e_esc ? e_esc : "");
    free(c_esc); free(t_esc); free(e_esc);
    if (!ir_emit(ctx, br)) {
      free(cv); free(then_label); free(done_label);
      if (else_st) free(else_label);
      return 0;
    }

    ctx->cur_block = then_idx;
    if (then_st && !ir_lower_stmt(then_st, ctx)) {
      free(cv); free(then_label); free(done_label);
      if (else_st) free(else_label);
      return 0;
    }
    if (!ir_is_terminated_block(&ctx->blocks.items[ctx->cur_block].instrs)) {
      char *d_esc = json_escape(done_label);
      char *j = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", d_esc ? d_esc : "");
      free(d_esc);
      if (!ir_emit(ctx, j)) {
        free(cv); free(then_label); free(done_label);
        if (else_st) free(else_label);
        return 0;
      }
    }

    if (else_st) {
      ctx->cur_block = else_idx;
      if (!ir_lower_stmt(else_st, ctx)) {
        free(cv); free(then_label); free(done_label); free(else_label);
        return 0;
      }
      if (!ir_is_terminated_block(&ctx->blocks.items[ctx->cur_block].instrs)) {
        char *d_esc = json_escape(done_label);
        char *j = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", d_esc ? d_esc : "");
        free(d_esc);
        if (!ir_emit(ctx, j)) {
          free(cv); free(then_label); free(done_label); free(else_label);
          return 0;
        }
      }
    }

    ctx->cur_block = done_idx;
    if (!ir_emit(ctx, strdup("{\"op\":\"nop\"}"))) {
      free(cv); free(then_label); free(done_label);
      if (else_st) free(else_label);
      return 0;
    }
    free(cv);
    free(then_label);
    free(done_label);
    if (else_st) free(else_label);
    return 1;
  }
  if (strcmp(st->kind, "WhileStmt") == 0) {
    AstNode *cond = (st->child_len > 0) ? st->children[0] : NULL;
    AstNode *body = (st->child_len > 1) ? st->children[1] : NULL;
    char *cond_label = ir_next_label(ctx, "while_cond_");
    char *body_label = ir_next_label(ctx, "while_body_");
    char *done_label = ir_next_label(ctx, "while_done_");
    if (!cond_label || !body_label || !done_label) {
      free(cond_label); free(body_label); free(done_label);
      return 0;
    }
    size_t cond_idx = 0, body_idx = 0, done_idx = 0;
    if (!ir_add_block(ctx, cond_label, &cond_idx) || !ir_add_block(ctx, body_label, &body_idx) ||
        !ir_add_block(ctx, done_label, &done_idx)) {
      free(cond_label); free(body_label); free(done_label);
      return 0;
    }
    char *c_esc = json_escape(cond_label);
    char *j = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", c_esc ? c_esc : "");
    free(c_esc);
    if (!ir_emit(ctx, j)) {
      free(cond_label); free(body_label); free(done_label);
      return 0;
    }

    ctx->cur_block = cond_idx;
    char *cv = cond ? ir_lower_expr(cond, ctx) : NULL;
    if (!cv) {
      free(cond_label); free(body_label); free(done_label);
      return 0;
    }
    char *cv_esc = json_escape(cv);
    char *body_esc = json_escape(body_label);
    char *done_esc = json_escape(done_label);
    char *br = str_printf("{\"op\":\"branch_if\",\"cond\":\"%s\",\"then\":\"%s\",\"else\":\"%s\"}", cv_esc ? cv_esc : "",
                          body_esc ? body_esc : "", done_esc ? done_esc : "");
    free(cv_esc); free(body_esc); free(done_esc); free(cv);
    if (!ir_emit(ctx, br)) {
      free(cond_label); free(body_label); free(done_label);
      return 0;
    }

    if (!ir_push_break(ctx, done_label) || !ir_push_loop(ctx, done_label, cond_label)) {
      ir_pop_break(ctx);
      ir_pop_loop(ctx);
      free(cond_label); free(body_label); free(done_label);
      return 0;
    }

    ctx->cur_block = body_idx;
    if (body && !ir_lower_stmt(body, ctx)) {
      ir_pop_loop(ctx);
      ir_pop_break(ctx);
      free(cond_label); free(body_label); free(done_label);
      return 0;
    }
    if (!ir_is_terminated_block(&ctx->blocks.items[ctx->cur_block].instrs)) {
      char *c2 = json_escape(cond_label);
      char *j2 = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", c2 ? c2 : "");
      free(c2);
      if (!ir_emit(ctx, j2)) {
        ir_pop_loop(ctx);
        ir_pop_break(ctx);
        free(cond_label); free(body_label); free(done_label);
        return 0;
      }
    }

    ir_pop_loop(ctx);
    ir_pop_break(ctx);
    ctx->cur_block = done_idx;
    if (!ir_emit(ctx, strdup("{\"op\":\"nop\"}"))) {
      free(cond_label); free(body_label); free(done_label);
      return 0;
    }
    free(cond_label); free(body_label); free(done_label);
    return 1;
  }
  if (strcmp(st->kind, "DoWhileStmt") == 0) {
    AstNode *cond = (st->child_len > 0) ? st->children[0] : NULL;
    AstNode *body = (st->child_len > 1) ? st->children[1] : NULL;
    char *body_label = ir_next_label(ctx, "do_body_");
    char *cond_label = ir_next_label(ctx, "do_cond_");
    char *done_label = ir_next_label(ctx, "do_done_");
    if (!body_label || !cond_label || !done_label) {
      free(body_label); free(cond_label); free(done_label);
      return 0;
    }
    size_t body_idx = 0, cond_idx = 0, done_idx = 0;
    if (!ir_add_block(ctx, body_label, &body_idx) || !ir_add_block(ctx, cond_label, &cond_idx) ||
        !ir_add_block(ctx, done_label, &done_idx)) {
      free(body_label); free(cond_label); free(done_label);
      return 0;
    }
    char *b_esc = json_escape(body_label);
    char *j = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", b_esc ? b_esc : "");
    free(b_esc);
    if (!ir_emit(ctx, j)) {
      free(body_label); free(cond_label); free(done_label);
      return 0;
    }

    if (!ir_push_break(ctx, done_label) || !ir_push_loop(ctx, done_label, cond_label)) {
      ir_pop_break(ctx);
      ir_pop_loop(ctx);
      free(body_label); free(cond_label); free(done_label);
      return 0;
    }

    ctx->cur_block = body_idx;
    if (body && !ir_lower_stmt(body, ctx)) {
      ir_pop_loop(ctx);
      ir_pop_break(ctx);
      free(body_label); free(cond_label); free(done_label);
      return 0;
    }
    if (!ir_is_terminated_block(&ctx->blocks.items[ctx->cur_block].instrs)) {
      char *c2 = json_escape(cond_label);
      char *j2 = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", c2 ? c2 : "");
      free(c2);
      if (!ir_emit(ctx, j2)) {
        ir_pop_loop(ctx);
        ir_pop_break(ctx);
        free(body_label); free(cond_label); free(done_label);
        return 0;
      }
    }

    ir_pop_loop(ctx);
    ir_pop_break(ctx);
    ctx->cur_block = cond_idx;
    char *cv = cond ? ir_lower_expr(cond, ctx) : NULL;
    if (!cv) {
      free(body_label); free(cond_label); free(done_label);
      return 0;
    }
    char *cv_esc = json_escape(cv);
    char *body2_esc = json_escape(body_label);
    char *done_esc = json_escape(done_label);
    char *br = str_printf("{\"op\":\"branch_if\",\"cond\":\"%s\",\"then\":\"%s\",\"else\":\"%s\"}", cv_esc ? cv_esc : "",
                          body2_esc ? body2_esc : "", done_esc ? done_esc : "");
    free(cv_esc); free(body2_esc); free(done_esc); free(cv);
    if (!ir_emit(ctx, br)) {
      free(body_label); free(cond_label); free(done_label);
      return 0;
    }

    ctx->cur_block = done_idx;
    if (!ir_emit(ctx, strdup("{\"op\":\"nop\"}"))) {
      free(body_label); free(cond_label); free(done_label);
      return 0;
    }
    free(body_label); free(cond_label); free(done_label);
    return 1;
  }
  if (strcmp(st->kind, "SwitchStmt") == 0) {
    AstNode *sw_expr = (st->child_len > 0) ? st->children[0] : NULL;
    char *swv = sw_expr ? ir_lower_expr(sw_expr, ctx) : NULL;
    if (!swv) return 0;

    char *done_label = ir_next_label(ctx, "sw_done_");
    if (!done_label) {
      free(swv);
      return 0;
    }
    if (!ir_push_break(ctx, done_label)) {
      free(done_label);
      free(swv);
      return 0;
    }
    size_t done_idx = 0;

    size_t case_count = 0;
    AstNode *default_case = NULL;
    for (size_t i = 1; i < st->child_len; i++) {
      if (strcmp(st->children[i]->kind, "CaseClause") == 0) case_count++;
      if (strcmp(st->children[i]->kind, "DefaultClause") == 0) default_case = st->children[i];
    }

    char **cmp_labels = (char **)calloc(case_count ? case_count : 1, sizeof(char *));
    char **body_labels = (char **)calloc(case_count ? case_count : 1, sizeof(char *));
    size_t *cmp_idxs = (size_t *)calloc(case_count ? case_count : 1, sizeof(size_t));
    size_t *body_idxs = (size_t *)calloc(case_count ? case_count : 1, sizeof(size_t));
    if (!cmp_labels || !body_labels || !cmp_idxs || !body_idxs) {
      free(cmp_labels); free(body_labels); free(cmp_idxs); free(body_idxs); free(done_label); free(swv);
      return 0;
    }

    size_t ci = 0;
    for (size_t i = 1; i < st->child_len; i++) {
      AstNode *c = st->children[i];
      if (strcmp(c->kind, "CaseClause") != 0) continue;
      cmp_labels[ci] = ir_next_label(ctx, "sw_cmp_");
      body_labels[ci] = ir_next_label(ctx, "sw_body_");
      if (!cmp_labels[ci] || !body_labels[ci] || !ir_add_block(ctx, cmp_labels[ci], &cmp_idxs[ci]) ||
          !ir_add_block(ctx, body_labels[ci], &body_idxs[ci])) {
        free(done_label); free(swv);
        for (size_t k = 0; k <= ci; k++) { free(cmp_labels[k]); free(body_labels[k]); }
        free(cmp_labels); free(body_labels); free(cmp_idxs); free(body_idxs);
        return 0;
      }
      ci++;
    }

    char *default_label = NULL;
    size_t default_idx = 0;
    if (default_case) {
      default_label = ir_next_label(ctx, "sw_default_");
      if (!default_label || !ir_add_block(ctx, default_label, &default_idx)) {
        free(default_label); free(done_label); free(swv);
        for (size_t k = 0; k < case_count; k++) { free(cmp_labels[k]); free(body_labels[k]); }
        free(cmp_labels); free(body_labels); free(cmp_idxs); free(body_idxs);
        return 0;
      }
    }

    if (case_count > 0) {
      char *tgt = json_escape(cmp_labels[0]);
      char *ins = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", tgt ? tgt : "");
      free(tgt);
      if (!ir_emit(ctx, ins)) {
        free(done_label); free(default_label); free(swv);
        for (size_t k = 0; k < case_count; k++) { free(cmp_labels[k]); free(body_labels[k]); }
        free(cmp_labels); free(body_labels); free(cmp_idxs); free(body_idxs);
        return 0;
      }
    } else if (default_case) {
      char *tgt = json_escape(default_label);
      char *ins = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", tgt ? tgt : "");
      free(tgt);
      if (!ir_emit(ctx, ins)) {
        free(done_label); free(default_label); free(swv);
        for (size_t k = 0; k < case_count; k++) { free(cmp_labels[k]); free(body_labels[k]); }
        free(cmp_labels); free(body_labels); free(cmp_idxs); free(body_idxs);
        return 0;
      }
    }

    ci = 0;
    for (size_t i = 1; i < st->child_len; i++) {
      AstNode *c = st->children[i];
      if (strcmp(c->kind, "CaseClause") != 0) continue;
      ctx->cur_block = cmp_idxs[ci];
      AstNode *cv = (c->child_len > 0) ? c->children[0] : NULL;
      char *vv = cv ? ir_lower_expr(cv, ctx) : NULL;
      char *eq = ir_next_tmp(ctx);
      if (!vv || !eq) {
        free(vv); free(eq); free(done_label); free(default_label); free(swv);
        for (size_t k = 0; k < case_count; k++) { free(cmp_labels[k]); free(body_labels[k]); }
        free(cmp_labels); free(body_labels); free(cmp_idxs); free(body_idxs);
        return 0;
      }
      char *eq_esc = json_escape(eq), *sw_esc = json_escape(swv), *vv_esc = json_escape(vv);
      char *bin = str_printf("{\"op\":\"bin_op\",\"dst\":\"%s\",\"operator\":\"==\",\"left\":\"%s\",\"right\":\"%s\"}",
                             eq_esc ? eq_esc : "", sw_esc ? sw_esc : "", vv_esc ? vv_esc : "");
      free(eq_esc); free(sw_esc); free(vv_esc); free(vv);
      if (!ir_emit(ctx, bin)) {
        free(eq); free(done_label); free(default_label); free(swv);
        for (size_t k = 0; k < case_count; k++) { free(cmp_labels[k]); free(body_labels[k]); }
        free(cmp_labels); free(body_labels); free(cmp_idxs); free(body_idxs);
        return 0;
      }
      const char *else_lbl = (ci + 1 < case_count) ? cmp_labels[ci + 1] : (default_case ? default_label : done_label);
      char *eq2 = json_escape(eq), *then_esc = json_escape(body_labels[ci]), *else_esc = json_escape(else_lbl);
      char *br = str_printf("{\"op\":\"branch_if\",\"cond\":\"%s\",\"then\":\"%s\",\"else\":\"%s\"}", eq2 ? eq2 : "",
                            then_esc ? then_esc : "", else_esc ? else_esc : "");
      free(eq2); free(then_esc); free(else_esc); free(eq);
      if (!ir_emit(ctx, br)) {
        free(done_label); free(default_label); free(swv);
        for (size_t k = 0; k < case_count; k++) { free(cmp_labels[k]); free(body_labels[k]); }
        free(cmp_labels); free(body_labels); free(cmp_idxs); free(body_idxs);
        return 0;
      }

      ctx->cur_block = body_idxs[ci];
      for (size_t j = 1; j < c->child_len; j++) {
        if (!ir_lower_stmt(c->children[j], ctx)) {
          free(done_label); free(default_label); free(swv);
          for (size_t k = 0; k < case_count; k++) { free(cmp_labels[k]); free(body_labels[k]); }
          free(cmp_labels); free(body_labels); free(cmp_idxs); free(body_idxs);
          return 0;
        }
      }
      if (!ir_is_terminated_block(&ctx->blocks.items[ctx->cur_block].instrs)) {
        char *d_esc = json_escape(done_label);
        char *j = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", d_esc ? d_esc : "");
        free(d_esc);
        if (!ir_emit(ctx, j)) {
          free(done_label); free(default_label); free(swv);
          for (size_t k = 0; k < case_count; k++) { free(cmp_labels[k]); free(body_labels[k]); }
          free(cmp_labels); free(body_labels); free(cmp_idxs); free(body_idxs);
          return 0;
        }
      }
      ci++;
    }

    if (default_case) {
      ctx->cur_block = default_idx;
      for (size_t j = 0; j < default_case->child_len; j++) {
        if (!ir_lower_stmt(default_case->children[j], ctx)) {
          free(done_label); free(default_label); free(swv);
          for (size_t k = 0; k < case_count; k++) { free(cmp_labels[k]); free(body_labels[k]); }
          free(cmp_labels); free(body_labels); free(cmp_idxs); free(body_idxs);
          return 0;
        }
      }
      if (!ir_is_terminated_block(&ctx->blocks.items[ctx->cur_block].instrs)) {
        char *d_esc = json_escape(done_label);
        char *j = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", d_esc ? d_esc : "");
        free(d_esc);
        if (!ir_emit(ctx, j)) {
          free(done_label); free(default_label); free(swv);
          for (size_t k = 0; k < case_count; k++) { free(cmp_labels[k]); free(body_labels[k]); }
          free(cmp_labels); free(body_labels); free(cmp_idxs); free(body_idxs);
          return 0;
        }
      }
    }

    if (!ir_add_block(ctx, done_label, &done_idx)) {
      free(done_label); free(default_label); free(swv);
      for (size_t k = 0; k < case_count; k++) { free(cmp_labels[k]); free(body_labels[k]); }
      free(cmp_labels); free(body_labels); free(cmp_idxs); free(body_idxs);
      return 0;
    }
    ctx->cur_block = done_idx;
    if (!ir_emit(ctx, strdup("{\"op\":\"nop\"}"))) {
      free(done_label); free(default_label); free(swv);
      for (size_t k = 0; k < case_count; k++) { free(cmp_labels[k]); free(body_labels[k]); }
      free(cmp_labels); free(body_labels); free(cmp_idxs); free(body_idxs);
      return 0;
    }

    free(done_label); free(default_label); free(swv);
    for (size_t k = 0; k < case_count; k++) { free(cmp_labels[k]); free(body_labels[k]); }
    free(cmp_labels); free(body_labels); free(cmp_idxs); free(body_idxs);
    ir_pop_break(ctx);
    return 1;
  }
  if (strcmp(st->kind, "ForStmt") == 0) {
    if (!st->text || (strcmp(st->text, "in") != 0 && strcmp(st->text, "of") != 0)) {
      AstNode *init = NULL;
      AstNode *cond = NULL;
      AstNode *step = NULL;
      AstNode *body = NULL;
      if (st->child_len > 0) {
        body = st->children[st->child_len - 1];
        if (st->child_len > 1) init = st->children[0];
        if (st->child_len > 2) cond = st->children[1];
        if (st->child_len > 3) step = st->children[2];
      }

      char *init_label = ir_next_label(ctx, "for_init_");
      char *cond_label = ir_next_label(ctx, "for_cond_");
      char *body_label = ir_next_label(ctx, "for_body_");
      char *step_label = ir_next_label(ctx, "for_step_");
      char *done_label = ir_next_label(ctx, "for_done_");
      if (!init_label || !cond_label || !body_label || !step_label || !done_label) {
        free(init_label); free(cond_label); free(body_label); free(step_label); free(done_label);
        return 0;
      }

      size_t init_idx = 0, cond_idx = 0, body_idx = 0, step_idx = 0, done_idx = 0;
      if (!ir_add_block(ctx, init_label, &init_idx) || !ir_add_block(ctx, cond_label, &cond_idx) ||
          !ir_add_block(ctx, body_label, &body_idx) || !ir_add_block(ctx, step_label, &step_idx) ||
          !ir_add_block(ctx, done_label, &done_idx)) {
        free(init_label); free(cond_label); free(body_label); free(step_label); free(done_label);
        return 0;
      }

      char *init_esc = json_escape(init_label);
      char *j_entry = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", init_esc ? init_esc : "");
      free(init_esc);
      if (!ir_emit(ctx, j_entry)) {
        free(init_label); free(cond_label); free(body_label); free(step_label); free(done_label);
        return 0;
      }

      ctx->cur_block = init_idx;
      if (init) {
        if (strcmp(init->kind, "VarDecl") == 0 || strcmp(init->kind, "AssignStmt") == 0) {
          if (!ir_lower_stmt(init, ctx)) return 0;
        } else {
          char *tmp = ir_lower_expr(init, ctx);
          free(tmp);
        }
      }
      char *cond_esc = json_escape(cond_label);
      char *j_cond = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", cond_esc ? cond_esc : "");
      free(cond_esc);
      if (!ir_emit(ctx, j_cond)) return 0;

      ctx->cur_block = cond_idx;
      if (cond) {
        char *cv = ir_lower_expr(cond, ctx);
        if (!cv) return 0;
        char *cv_esc = json_escape(cv);
        char *body_esc = json_escape(body_label);
        char *done_esc = json_escape(done_label);
        char *br = str_printf("{\"op\":\"branch_if\",\"cond\":\"%s\",\"then\":\"%s\",\"else\":\"%s\"}", cv_esc ? cv_esc : "",
                              body_esc ? body_esc : "", done_esc ? done_esc : "");
        free(cv_esc); free(body_esc); free(done_esc); free(cv);
        if (!ir_emit(ctx, br)) return 0;
      } else {
        char *body_esc = json_escape(body_label);
        char *j = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", body_esc ? body_esc : "");
        free(body_esc);
        if (!ir_emit(ctx, j)) return 0;
      }

      if (!ir_push_break(ctx, done_label) || !ir_push_loop(ctx, done_label, step_label)) {
        ir_pop_break(ctx);
        ir_pop_loop(ctx);
        return 0;
      }

      ctx->cur_block = body_idx;
      if (body && !ir_lower_stmt(body, ctx)) {
        ir_pop_loop(ctx);
        ir_pop_break(ctx);
        return 0;
      }
      if (!ir_is_terminated_block(&ctx->blocks.items[ctx->cur_block].instrs)) {
        char *step_esc = json_escape(step_label);
        char *j = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", step_esc ? step_esc : "");
        free(step_esc);
        if (!ir_emit(ctx, j)) {
          ir_pop_loop(ctx);
          ir_pop_break(ctx);
          return 0;
        }
      }

      ir_pop_loop(ctx);
      ir_pop_break(ctx);

      ctx->cur_block = step_idx;
      if (step) {
        if (strcmp(step->kind, "VarDecl") == 0 || strcmp(step->kind, "AssignStmt") == 0) {
          if (!ir_lower_stmt(step, ctx)) return 0;
        } else {
          char *tmp = ir_lower_expr(step, ctx);
          free(tmp);
        }
      }
      char *cond2_esc = json_escape(cond_label);
      char *j_back = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", cond2_esc ? cond2_esc : "");
      free(cond2_esc);
      if (!ir_emit(ctx, j_back)) return 0;

      ctx->cur_block = done_idx;
      if (!ir_emit(ctx, strdup("{\"op\":\"nop\"}"))) return 0;

      free(init_label); free(cond_label); free(body_label); free(step_label); free(done_label);
      return 1;
    }
    AstNode *iter_var = NULL;
    AstNode *iter_expr = NULL;
    AstNode *body = NULL;
    for (size_t i = 0; i < st->child_len; i++) {
      AstNode *c = st->children[i];
      if (strcmp(c->kind, "IterVar") == 0) iter_var = c;
      else if (strcmp(c->kind, "Block") == 0) body = c;
      else if (!iter_expr) iter_expr = c;
    }
    if (!iter_expr || !body) return ir_emit(ctx, str_printf("{\"op\":\"unhandled_stmt\",\"kind\":\"%s\"}", st->kind));

    const char *mode = (st->text && (strcmp(st->text, "in") == 0 || strcmp(st->text, "of") == 0)) ? st->text : "of";
    char *seq = ir_lower_expr(iter_expr, ctx);
    char *cursor = ir_next_tmp(ctx);
    char *elem = ir_next_tmp(ctx);
    char *init_label = ir_next_label(ctx, "for_init_");
    char *cond_label = ir_next_label(ctx, "for_cond_");
    char *body_label = ir_next_label(ctx, "for_body_");
    char *done_label = ir_next_label(ctx, "for_done_");
    if (!seq || !cursor || !elem || !init_label || !cond_label || !body_label || !done_label) {
      free(seq); free(cursor); free(elem);
      free(init_label); free(cond_label); free(body_label); free(done_label);
      return 0;
    }

    size_t init_idx = 0, cond_idx = 0, body_idx = 0, done_idx = 0;
    if (!ir_add_block(ctx, init_label, &init_idx) || !ir_add_block(ctx, cond_label, &cond_idx) ||
        !ir_add_block(ctx, body_label, &body_idx) || !ir_add_block(ctx, done_label, &done_idx)) {
      free(seq); free(cursor); free(elem);
      free(init_label); free(cond_label); free(body_label); free(done_label);
      return 0;
    }

    char *init_esc = json_escape(init_label);
    char *j_entry = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", init_esc ? init_esc : "");
    free(init_esc);
    if (!ir_emit(ctx, j_entry)) {
      free(seq); free(cursor); free(elem);
      free(init_label); free(cond_label); free(body_label); free(done_label);
      return 0;
    }

    ctx->cur_block = init_idx;
    char *c_esc = json_escape(cursor), *s_esc = json_escape(seq), *m_esc = json_escape(mode);
    char *iter_begin = str_printf("{\"op\":\"iter_begin\",\"dst\":\"%s\",\"source\":\"%s\",\"mode\":\"%s\"}", c_esc ? c_esc : "",
                                  s_esc ? s_esc : "", m_esc ? m_esc : "of");
    free(c_esc); free(s_esc); free(m_esc);
    if (!ir_emit(ctx, iter_begin)) {
      free(seq); free(cursor); free(elem);
      free(init_label); free(cond_label); free(body_label); free(done_label);
      return 0;
    }
    char *cond_esc = json_escape(cond_label);
    char *j_cond = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", cond_esc ? cond_esc : "");
    free(cond_esc);
    if (!ir_emit(ctx, j_cond)) {
      free(seq); free(cursor); free(elem);
      free(init_label); free(cond_label); free(body_label); free(done_label);
      return 0;
    }

    ctx->cur_block = cond_idx;
    char *cur_esc = json_escape(cursor), *body_esc = json_escape(body_label), *done_esc = json_escape(done_label);
    char *iter_cond = str_printf("{\"op\":\"branch_iter_has_next\",\"iter\":\"%s\",\"then\":\"%s\",\"else\":\"%s\"}",
                                 cur_esc ? cur_esc : "", body_esc ? body_esc : "", done_esc ? done_esc : "");
    free(cur_esc); free(body_esc); free(done_esc);
    if (!ir_emit(ctx, iter_cond)) {
      free(seq); free(cursor); free(elem);
      free(init_label); free(cond_label); free(body_label); free(done_label);
      return 0;
    }

    ctx->cur_block = body_idx;
    char *e_esc = json_escape(elem), *cur2_esc = json_escape(cursor), *src2_esc = json_escape(seq), *m2_esc = json_escape(mode);
    char *iter_next = str_printf("{\"op\":\"iter_next\",\"dst\":\"%s\",\"iter\":\"%s\",\"source\":\"%s\",\"mode\":\"%s\"}",
                                 e_esc ? e_esc : "", cur2_esc ? cur2_esc : "", src2_esc ? src2_esc : "", m2_esc ? m2_esc : "of");
    free(e_esc); free(cur2_esc); free(src2_esc); free(m2_esc);
    if (!ir_emit(ctx, iter_next)) {
      free(seq); free(cursor); free(elem);
      free(init_label); free(cond_label); free(body_label); free(done_label);
      return 0;
    }

    if (iter_var && iter_var->text) {
      AstNode *it_tn = ast_child_kind(iter_var, "Type");
      char *decl_t = it_tn ? ast_type_to_ir_name(it_tn) : NULL;
      if (!decl_t) {
        char *seq_t = ir_guess_expr_type(iter_expr, ctx);
        decl_t = ir_type_elem_for_iter(seq_t, mode);
        free(seq_t);
      }
      if (!decl_t) decl_t = strdup("unknown");
      if (!ir_set_var_type(ctx, iter_var->text, decl_t ? decl_t : "unknown")) {
        free(decl_t);
        free(seq); free(cursor); free(elem);
        free(init_label); free(cond_label); free(body_label); free(done_label);
        return 0;
      }
      char *n_esc = json_escape(iter_var->text), *t_esc = json_escape(decl_t), *ev_esc = json_escape(elem);
      char *vd = str_printf("{\"op\":\"var_decl\",\"name\":\"%s\",\"type\":{\"kind\":\"IRType\",\"name\":\"%s\"}}",
                            n_esc ? n_esc : "", t_esc ? t_esc : "unknown");
      if (!ir_emit(ctx, vd)) {
        free(n_esc); free(t_esc); free(ev_esc); free(decl_t);
        free(seq); free(cursor); free(elem);
        free(init_label); free(cond_label); free(body_label); free(done_label);
        return 0;
      }
      char *sv = str_printf("{\"op\":\"store_var\",\"name\":\"%s\",\"src\":\"%s\",\"type\":{\"kind\":\"IRType\",\"name\":\"%s\"}}",
                            n_esc ? n_esc : "", ev_esc ? ev_esc : "", t_esc ? t_esc : "unknown");
      free(n_esc); free(t_esc); free(ev_esc); free(decl_t);
      if (!ir_emit(ctx, sv)) {
        free(seq); free(cursor); free(elem);
        free(init_label); free(cond_label); free(body_label); free(done_label);
        return 0;
      }
    }

    if (!ir_push_break(ctx, done_label) || !ir_push_loop(ctx, done_label, cond_label)) {
      ir_pop_break(ctx);
      ir_pop_loop(ctx);
      free(seq); free(cursor); free(elem);
      free(init_label); free(cond_label); free(body_label); free(done_label);
      return 0;
    }

    if (!ir_lower_stmt(body, ctx)) {
      ir_pop_loop(ctx);
      ir_pop_break(ctx);
      free(seq); free(cursor); free(elem);
      free(init_label); free(cond_label); free(body_label); free(done_label);
      return 0;
    }
    if (!ir_is_terminated_block(&ctx->blocks.items[ctx->cur_block].instrs)) {
      char *cond2_esc = json_escape(cond_label);
      char *j_back = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", cond2_esc ? cond2_esc : "");
      free(cond2_esc);
      if (!ir_emit(ctx, j_back)) {
        ir_pop_loop(ctx);
        ir_pop_break(ctx);
        free(seq); free(cursor); free(elem);
        free(init_label); free(cond_label); free(body_label); free(done_label);
        return 0;
      }
    }

    ir_pop_loop(ctx);
    ir_pop_break(ctx);

    ctx->cur_block = done_idx;
    if (!ir_emit(ctx, strdup("{\"op\":\"nop\"}"))) {
      free(seq); free(cursor); free(elem);
      free(init_label); free(cond_label); free(body_label); free(done_label);
      return 0;
    }

    free(seq); free(cursor); free(elem);
    free(init_label); free(cond_label); free(body_label); free(done_label);
    return 1;
  }
  return ir_emit(ctx, str_printf("{\"op\":\"unhandled_stmt\",\"kind\":\"%s\"}", st->kind));
}

static int ir_fn_terminated(IrFnCtx *ctx) {
  if (ctx->blocks.len == 0) return 0;
  return ir_is_terminated_block(&ctx->blocks.items[ctx->cur_block].instrs);
}

int ps_emit_ir_json(const char *file, PsDiag *out_diag, FILE *out) {
  AstNode *root = NULL;
  int rc = parse_file_internal(file, out_diag, &root);
  if (rc != 0) {
    ast_free(root);
    return rc;
  }

  Analyzer a;
  memset(&a, 0, sizeof(a));
  a.file = file;
  a.diag = out_diag;
  if (!collect_imports(&a, root)) {
    ast_free(root);
    free_fns(a.fns);
    free_imports(a.imports);
    free_namespaces(a.namespaces);
    free_user_modules(a.user_modules);
    free_registry(a.registry);
    return 1;
  }
  if (!collect_prototypes(&a, root)) {
    ast_free(root);
    free_fns(a.fns);
    free_imports(a.imports);
    free_namespaces(a.namespaces);
    free_user_modules(a.user_modules);
    free_registry(a.registry);
    free_protos(a.protos);
    return 1;
  }

  fputs("{\n", out);
  fputs("  \"ir_version\": \"1.0.0\",\n", out);
  fputs("  \"format\": \"ProtoScriptIR\",\n", out);
  fputs("  \"module\": {\n", out);
  fputs("    \"kind\": \"Module\",\n", out);
  fputs("    \"prototypes\": [\n", out);
  int first_proto = 1;
  for (ProtoInfo *p = a.protos; p; p = p->next) {
    if (!p->name) continue;
    char *n = json_escape(p->name);
    char *parent = p->parent ? json_escape(p->parent) : NULL;
    if (!first_proto) fputs(",\n", out);
    first_proto = 0;
    fprintf(out, "      {\"name\":\"%s\"", n ? n : "");
    if (parent) fprintf(out, ",\"parent\":\"%s\"", parent);
    fputs("}", out);
    free(n);
    free(parent);
  }
  fputs("\n    ],\n", out);
  fputs("    \"functions\": [\n", out);

  IrFnSig *fn_sigs = NULL;
  for (FnSig *f = a.fns; f; f = f->next) {
    IrFnSig *s = (IrFnSig *)calloc(1, sizeof(IrFnSig));
    if (!s) {
      ast_free(root);
      free_fns(a.fns);
      free_imports(a.imports);
      free_namespaces(a.namespaces);
      free_user_modules(a.user_modules);
      free_registry(a.registry);
      set_diag(out_diag, file, 1, 1, "E0002", "INTERNAL_ERROR", "IR signature allocation failure");
      return 2;
    }
    s->name = strdup(f->name ? f->name : "");
    s->ret_type = strdup(f->ret_type ? f->ret_type : "void");
    s->variadic = f->variadic;
    s->next = fn_sigs;
    fn_sigs = s;
  }
  for (size_t fi = 0; fi < root->child_len; fi++) {
    AstNode *fn = root->children[fi];
    if (strcmp(fn->kind, "FunctionDecl") != 0) continue;
    IrFnSig *s = (IrFnSig *)calloc(1, sizeof(IrFnSig));
    if (!s) {
      ast_free(root);
      set_diag(out_diag, file, 1, 1, "E0002", "INTERNAL_ERROR", "IR signature allocation failure");
      return 2;
    }
    s->name = strdup(fn->text ? fn->text : "");
    AstNode *rt = ast_child_kind(fn, "ReturnType");
    s->ret_type = ast_type_to_ir_name(rt);
    if (!s->name || !s->ret_type) {
      free(s->name);
      free(s->ret_type);
      free(s);
      ast_free(root);
      ir_free_fn_sigs(fn_sigs);
      set_diag(out_diag, file, 1, 1, "E0002", "INTERNAL_ERROR", "IR signature allocation failure");
      return 2;
    }
    for (size_t pi = 0; pi < fn->child_len; pi++) {
      AstNode *p = fn->children[pi];
      if (strcmp(p->kind, "Param") == 0 && ast_child_kind(p, "Variadic")) s->variadic = 1;
    }
    s->next = fn_sigs;
    fn_sigs = s;
  }
  for (ProtoInfo *p = a.protos; p; p = p->next) {
    if (!p->name) continue;
    IrFnSig *clone = (IrFnSig *)calloc(1, sizeof(IrFnSig));
    if (!clone) {
      ast_free(root);
      ir_free_fn_sigs(fn_sigs);
      set_diag(out_diag, file, 1, 1, "E0002", "INTERNAL_ERROR", "IR signature allocation failure");
      return 2;
    }
    clone->name = str_printf("%s.clone", p->name);
    clone->ret_type = strdup(p->name);
    clone->variadic = 0;
    clone->next = fn_sigs;
    fn_sigs = clone;
    for (ProtoMethod *m = p->methods; m; m = m->next) {
      IrFnSig *ms = (IrFnSig *)calloc(1, sizeof(IrFnSig));
      if (!ms) {
        ast_free(root);
        ir_free_fn_sigs(fn_sigs);
        set_diag(out_diag, file, 1, 1, "E0002", "INTERNAL_ERROR", "IR signature allocation failure");
        return 2;
      }
      ms->name = str_printf("%s.%s", p->name, m->name ? m->name : "");
      ms->ret_type = strdup(m->ret_type ? m->ret_type : "void");
      ms->variadic = 0;
      ms->next = fn_sigs;
      fn_sigs = ms;
    }
  }

  int first_fn = 1;
  for (size_t fi = 0; fi < root->child_len; fi++) {
    AstNode *fn = root->children[fi];
    if (strcmp(fn->kind, "FunctionDecl") != 0) continue;
    if (!first_fn) fputs(",\n", out);
    first_fn = 0;

    IrFnCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.fn_sigs = fn_sigs;
    ctx.imports = a.imports;
    ctx.namespaces = a.namespaces;
    ctx.registry = a.registry;
    ctx.protos = a.protos;
    ctx.file = file;
    ctx.loc_file = file;
    ctx.loc_line = 1;
    ctx.loc_col = 1;
    ctx.loc_file = file;
    ctx.loc_line = 1;
    ctx.loc_col = 1;
    ctx.loc_file = file;
    ctx.loc_line = 1;
    ctx.loc_col = 1;
    IrScope root_scope;
    ir_scope_init(&root_scope, NULL);
    ctx.scope = &root_scope;
    if (!ir_add_block(&ctx, "entry", &ctx.cur_block)) {
      ast_free(root);
      ir_free_fn_sigs(fn_sigs);
      free_fns(a.fns);
      free_imports(a.imports);
      free_namespaces(a.namespaces);
      free_registry(a.registry);
      set_diag(out_diag, file, 1, 1, "E0002", "INTERNAL_ERROR", "IR block allocation failure");
      return 2;
    }
    for (size_t pi = 0; pi < fn->child_len; pi++) {
      AstNode *p = fn->children[pi];
      if (strcmp(p->kind, "Param") != 0) continue;
      AstNode *pt = ast_child_kind(p, "Type");
      char *ptn = ast_type_to_ir_name(pt);
      char *irn = ir_next_var(&ctx, p->text ? p->text : "p");
      if (!ptn || !irn || !ir_scope_define(&root_scope, p->text ? p->text : "", irn) || !ir_set_var_type(&ctx, irn, ptn)) {
        free(ptn);
        free(irn);
        ir_block_vec_free(&ctx.blocks);
        ir_var_type_vec_free(&ctx.vars);
        ir_scope_free(&root_scope);
        ast_free(root);
        ir_free_fn_sigs(fn_sigs);
        set_diag(out_diag, file, 1, 1, "E0002", "INTERNAL_ERROR", "IR param allocation failure");
        return 2;
      }
      free(ptn);
      free(irn);
    }
    AstNode *blk = ast_child_kind(fn, "Block");
    if (blk && !ir_lower_stmt(blk, &ctx)) {
      ir_block_vec_free(&ctx.blocks);
      ir_var_type_vec_free(&ctx.vars);
      ir_scope_free(&root_scope);
      ast_free(root);
      ir_free_fn_sigs(fn_sigs);
      set_diag(out_diag, file, 1, 1, "E0002", "INTERNAL_ERROR", "IR lowering allocation failure");
      return 2;
    }
    AstNode *rt = ast_child_kind(fn, "ReturnType");
    char *ret = ast_type_to_ir_name(rt);
    if (!ir_fn_terminated(&ctx)) {
      if (ret && strcmp(ret, "void") == 0)
        ir_emit(&ctx, strdup("{\"op\":\"ret_void\"}"));
      else
        ir_emit(&ctx, strdup("{\"op\":\"ret\",\"value\":\"0\",\"type\":{\"kind\":\"IRType\",\"name\":\"unknown\"}}"));
    }

    char *fn_name = json_escape(fn->text ? fn->text : "");
    char *ret_esc = json_escape(ret ? ret : "void");
    fprintf(out, "      {\n");
    fprintf(out, "        \"kind\": \"Function\",\n");
    fprintf(out, "        \"name\": \"%s\",\n", fn_name ? fn_name : "");
    fputs("        \"params\": [", out);
    int first_p = 1;
    for (size_t pi = 0; pi < fn->child_len; pi++) {
      AstNode *p = fn->children[pi];
      if (strcmp(p->kind, "Param") != 0) continue;
      AstNode *pt = ast_child_kind(p, "Type");
      AstNode *pv = ast_child_kind(p, "Variadic");
      const char *mapped = ir_scope_lookup(&root_scope, p->text ? p->text : "");
      char *pn = json_escape(mapped ? mapped : (p->text ? p->text : ""));
      char *ptn_raw = ast_type_to_ir_name(pt);
      char *ptn = json_escape(ptn_raw ? ptn_raw : "unknown");
      if (!first_p) fputs(",", out);
      first_p = 0;
      fprintf(out,
              "{\"name\":\"%s\",\"type\":{\"kind\":\"IRType\",\"name\":\"%s\"},\"variadic\":%s}",
              pn ? pn : "", ptn ? ptn : "", pv ? "true" : "false");
      free(pn);
      free(ptn_raw);
      free(ptn);
    }
    fputs("],\n", out);
    fprintf(out, "        \"returnType\": {\"kind\": \"IRType\", \"name\": \"%s\"},\n", ret_esc ? ret_esc : "void");
    fputs("        \"blocks\": [\n", out);
    for (size_t bi = 0; bi < ctx.blocks.len; bi++) {
      IrBlock *b = &ctx.blocks.items[bi];
      fprintf(out, "          {\n");
      fprintf(out, "            \"kind\": \"Block\",\n");
      fprintf(out, "            \"label\": \"%s\",\n", b->label ? b->label : "entry");
      fputs("            \"instrs\": [\n", out);
      for (size_t ii = 0; ii < b->instrs.len; ii++) {
        fprintf(out, "              %s%s\n", b->instrs.items[ii], (ii + 1 < b->instrs.len) ? "," : "");
      }
      fputs("            ]\n", out);
      fprintf(out, "          }%s\n", (bi + 1 < ctx.blocks.len) ? "," : "");
    }
    fputs("        ]\n", out);
    fputs("      }", out);

    free(fn_name);
    free(ret_esc);
    free(ret);
    ir_block_vec_free(&ctx.blocks);
    ir_var_type_vec_free(&ctx.vars);
    ir_scope_free(&root_scope);
  }

  for (size_t pi = 0; pi < root->child_len; pi++) {
    AstNode *pd = root->children[pi];
    if (strcmp(pd->kind, "PrototypeDecl") != 0) continue;
    const char *proto_name = pd->text ? pd->text : "";

    for (size_t mi = 0; mi < pd->child_len; mi++) {
      AstNode *m = pd->children[mi];
      if (strcmp(m->kind, "FunctionDecl") != 0) continue;
      if (!first_fn) fputs(",\n", out);
      first_fn = 0;

      IrFnCtx ctx;
      memset(&ctx, 0, sizeof(ctx));
      ctx.fn_sigs = fn_sigs;
      ctx.imports = a.imports;
      ctx.namespaces = a.namespaces;
      ctx.registry = a.registry;
      ctx.protos = a.protos;
      ctx.file = file;
      ctx.loc_file = file;
      ctx.loc_line = 1;
      ctx.loc_col = 1;
      IrScope root_scope;
      ir_scope_init(&root_scope, NULL);
      ctx.scope = &root_scope;
      if (!ir_add_block(&ctx, "entry", &ctx.cur_block)) {
        ast_free(root);
        ir_free_fn_sigs(fn_sigs);
        free_fns(a.fns);
        free_imports(a.imports);
        free_namespaces(a.namespaces);
        free_registry(a.registry);
        free_protos(a.protos);
        set_diag(out_diag, file, 1, 1, "E0002", "INTERNAL_ERROR", "IR block allocation failure");
        return 2;
      }
      char *self_ir = ir_next_var(&ctx, "self");
      if (!self_ir || !ir_scope_define(&root_scope, "self", self_ir) || !ir_set_var_type(&ctx, self_ir, proto_name)) {
        free(self_ir);
        ir_block_vec_free(&ctx.blocks);
        ir_var_type_vec_free(&ctx.vars);
        ir_scope_free(&root_scope);
        ast_free(root);
        ir_free_fn_sigs(fn_sigs);
        set_diag(out_diag, file, 1, 1, "E0002", "INTERNAL_ERROR", "IR param allocation failure");
        return 2;
      }
      free(self_ir);
      for (size_t pj = 0; pj < m->child_len; pj++) {
        AstNode *p = m->children[pj];
        if (strcmp(p->kind, "Param") != 0) continue;
        AstNode *pt = ast_child_kind(p, "Type");
        char *ptn = ast_type_to_ir_name(pt);
        char *irn = ir_next_var(&ctx, p->text ? p->text : "p");
        if (!ptn || !irn || !ir_scope_define(&root_scope, p->text ? p->text : "", irn) || !ir_set_var_type(&ctx, irn, ptn)) {
          free(ptn);
          free(irn);
          ir_block_vec_free(&ctx.blocks);
          ir_var_type_vec_free(&ctx.vars);
          ir_scope_free(&root_scope);
          ast_free(root);
          ir_free_fn_sigs(fn_sigs);
          set_diag(out_diag, file, 1, 1, "E0002", "INTERNAL_ERROR", "IR param allocation failure");
          return 2;
        }
        free(ptn);
        free(irn);
      }
      AstNode *blk = ast_child_kind(m, "Block");
      if (blk && !ir_lower_stmt(blk, &ctx)) {
        ir_block_vec_free(&ctx.blocks);
        ir_var_type_vec_free(&ctx.vars);
        ir_scope_free(&root_scope);
        ast_free(root);
        ir_free_fn_sigs(fn_sigs);
        set_diag(out_diag, file, 1, 1, "E0002", "INTERNAL_ERROR", "IR lowering allocation failure");
        return 2;
      }
      AstNode *rt = ast_child_kind(m, "ReturnType");
      char *ret = ast_type_to_ir_name(rt);
      if (!ir_fn_terminated(&ctx)) {
        if (ret && strcmp(ret, "void") == 0)
          ir_emit(&ctx, strdup("{\"op\":\"ret_void\"}"));
        else
          ir_emit(&ctx, strdup("{\"op\":\"ret\",\"value\":\"0\",\"type\":{\"kind\":\"IRType\",\"name\":\"unknown\"}}"));
      }

      char *full_name = str_printf("%s.%s", proto_name, m->text ? m->text : "");
      char *fn_name = json_escape(full_name ? full_name : "");
      free(full_name);
      char *ret_esc = json_escape(ret ? ret : "void");
      fprintf(out, "      {\n");
      fprintf(out, "        \"kind\": \"Function\",\n");
      fprintf(out, "        \"name\": \"%s\",\n", fn_name ? fn_name : "");
      fputs("        \"params\": [", out);
      const char *self_mapped = ir_scope_lookup(&root_scope, "self");
      char *self_esc = json_escape(self_mapped ? self_mapped : "self");
      char *self_type_esc = json_escape(proto_name);
      fprintf(out, "{\"name\":\"%s\",\"type\":{\"kind\":\"IRType\",\"name\":\"%s\"},\"variadic\":false}",
              self_esc ? self_esc : "self", self_type_esc ? self_type_esc : proto_name);
      free(self_esc);
      free(self_type_esc);
      for (size_t pj = 0; pj < m->child_len; pj++) {
        AstNode *p = m->children[pj];
        if (strcmp(p->kind, "Param") != 0) continue;
        AstNode *pt = ast_child_kind(p, "Type");
        AstNode *pv = ast_child_kind(p, "Variadic");
        const char *mapped = ir_scope_lookup(&root_scope, p->text ? p->text : "");
        char *pn = json_escape(mapped ? mapped : (p->text ? p->text : ""));
        char *ptn_raw = ast_type_to_ir_name(pt);
        char *ptn = json_escape(ptn_raw ? ptn_raw : "unknown");
        fprintf(out, ",{\"name\":\"%s\",\"type\":{\"kind\":\"IRType\",\"name\":\"%s\"},\"variadic\":%s}",
                pn ? pn : "", ptn ? ptn : "", pv ? "true" : "false");
        free(pn);
        free(ptn_raw);
        free(ptn);
      }
      fputs("],\n", out);
      fprintf(out, "        \"returnType\": {\"kind\": \"IRType\", \"name\": \"%s\"},\n", ret_esc ? ret_esc : "void");
      fputs("        \"blocks\": [\n", out);
      for (size_t bi = 0; bi < ctx.blocks.len; bi++) {
        IrBlock *b = &ctx.blocks.items[bi];
        fprintf(out, "          {\n");
        fprintf(out, "            \"kind\": \"Block\",\n");
        fprintf(out, "            \"label\": \"%s\",\n", b->label ? b->label : "entry");
        fputs("            \"instrs\": [\n", out);
        for (size_t ii = 0; ii < b->instrs.len; ii++) {
          fprintf(out, "              %s%s\n", b->instrs.items[ii], (ii + 1 < b->instrs.len) ? "," : "");
        }
        fputs("            ]\n", out);
        fprintf(out, "          }%s\n", (bi + 1 < ctx.blocks.len) ? "," : "");
      }
      fputs("        ]\n", out);
      fputs("      }", out);

      free(fn_name);
      free(ret_esc);
      free(ret);
      ir_block_vec_free(&ctx.blocks);
      ir_var_type_vec_free(&ctx.vars);
      ir_scope_free(&root_scope);
    }

    if (!first_fn) fputs(",\n", out);
    first_fn = 0;

    IrFnCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.fn_sigs = fn_sigs;
    ctx.imports = a.imports;
    ctx.namespaces = a.namespaces;
    ctx.registry = a.registry;
    ctx.protos = a.protos;
    ctx.file = file;
    if (!ir_add_block(&ctx, "entry", &ctx.cur_block)) {
      ast_free(root);
      ir_free_fn_sigs(fn_sigs);
      free_fns(a.fns);
      free_imports(a.imports);
      free_namespaces(a.namespaces);
      free_registry(a.registry);
      free_protos(a.protos);
      set_diag(out_diag, file, 1, 1, "E0002", "INTERNAL_ERROR", "IR block allocation failure");
      return 2;
    }
    char *dst = ir_next_tmp(&ctx);
    char *d_esc = json_escape(dst ? dst : "");
    char *p_esc = json_escape(proto_name);
    if (!dst || !ir_emit(&ctx, str_printf("{\"op\":\"make_object\",\"dst\":\"%s\",\"proto\":\"%s\"}", d_esc ? d_esc : "", p_esc ? p_esc : ""))) {
      free(dst);
      free(d_esc);
      free(p_esc);
      ir_block_vec_free(&ctx.blocks);
      ir_var_type_vec_free(&ctx.vars);
      ast_free(root);
      ir_free_fn_sigs(fn_sigs);
      set_diag(out_diag, file, 1, 1, "E0002", "INTERNAL_ERROR", "IR lowering allocation failure");
      return 2;
    }
    free(d_esc);
    free(p_esc);
    ProtoFieldVec fv = proto_collect_fields(a.protos, proto_name);
    for (size_t fi = 0; fi < fv.len; fi++) {
      ProtoField *f = fv.items[fi];
      int is_exc = proto_is_subtype(a.protos, proto_name, "Exception");
      if (is_exc && f && f->name) {
        if (strcmp(f->name, "file") == 0 || strcmp(f->name, "line") == 0 || strcmp(f->name, "column") == 0 ||
            strcmp(f->name, "message") == 0 || strcmp(f->name, "cause") == 0 ||
            strcmp(f->name, "code") == 0 || strcmp(f->name, "category") == 0) {
          continue;
        }
      }
      char *val = ir_emit_default_value(&ctx, f && f->type ? f->type : "unknown", proto_name);
      if (!val) continue;
      char *t_esc = json_escape(dst);
      char *n_esc = json_escape(f ? f->name : "");
      char *v_esc = json_escape(val);
      ir_emit(&ctx, str_printf("{\"op\":\"member_set\",\"target\":\"%s\",\"name\":\"%s\",\"src\":\"%s\"}", t_esc ? t_esc : "",
                               n_esc ? n_esc : "", v_esc ? v_esc : ""));
      free(t_esc);
      free(n_esc);
      free(v_esc);
      free(val);
    }
    free(fv.items);
    char *dst_esc = json_escape(dst);
    ir_emit(&ctx, str_printf("{\"op\":\"ret\",\"value\":\"%s\",\"type\":{\"kind\":\"IRType\",\"name\":\"%s\"}}", dst_esc ? dst_esc : "",
                             proto_name));
    free(dst_esc);
    free(dst);

    char *clone_name = str_printf("%s.clone", proto_name);
    char *fn_name = json_escape(clone_name ? clone_name : "");
    free(clone_name);
    char *ret_esc = json_escape(proto_name);
    fprintf(out, "      {\n");
    fprintf(out, "        \"kind\": \"Function\",\n");
    fprintf(out, "        \"name\": \"%s\",\n", fn_name ? fn_name : "");
    fputs("        \"params\": [],\n", out);
    fprintf(out, "        \"returnType\": {\"kind\": \"IRType\", \"name\": \"%s\"},\n", ret_esc ? ret_esc : "void");
    fputs("        \"blocks\": [\n", out);
    for (size_t bi = 0; bi < ctx.blocks.len; bi++) {
      IrBlock *b = &ctx.blocks.items[bi];
      fprintf(out, "          {\n");
      fprintf(out, "            \"kind\": \"Block\",\n");
      fprintf(out, "            \"label\": \"%s\",\n", b->label ? b->label : "entry");
      fputs("            \"instrs\": [\n", out);
      for (size_t ii = 0; ii < b->instrs.len; ii++) {
        fprintf(out, "              %s%s\n", b->instrs.items[ii], (ii + 1 < b->instrs.len) ? "," : "");
      }
      fputs("            ]\n", out);
      fprintf(out, "          }%s\n", (bi + 1 < ctx.blocks.len) ? "," : "");
    }
    fputs("        ]\n", out);
    fputs("      }", out);

    free(fn_name);
    free(ret_esc);
    ir_block_vec_free(&ctx.blocks);
    ir_var_type_vec_free(&ctx.vars);
  }

  // Emit clone functions for builtin prototypes that are not declared in source.
  for (ProtoInfo *pb = a.protos; pb; pb = pb->next) {
    if (!pb->builtin || !pb->name) continue;
    const char *proto_name = pb->name;

    if (!first_fn) fputs(",\n", out);
    first_fn = 0;

    IrFnCtx ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.fn_sigs = fn_sigs;
    ctx.imports = a.imports;
    ctx.namespaces = a.namespaces;
    ctx.registry = a.registry;
    ctx.protos = a.protos;
    ctx.file = file;
    if (!ir_add_block(&ctx, "entry", &ctx.cur_block)) {
      ast_free(root);
      ir_free_fn_sigs(fn_sigs);
      free_fns(a.fns);
      free_imports(a.imports);
      free_namespaces(a.namespaces);
      free_registry(a.registry);
      free_protos(a.protos);
      set_diag(out_diag, file, 1, 1, "E0002", "INTERNAL_ERROR", "IR block allocation failure");
      return 2;
    }
    char *dst = ir_next_tmp(&ctx);
    char *d_esc = json_escape(dst ? dst : "");
    char *p_esc = json_escape(proto_name);
    if (!dst || !ir_emit(&ctx, str_printf("{\"op\":\"make_object\",\"dst\":\"%s\",\"proto\":\"%s\"}", d_esc ? d_esc : "", p_esc ? p_esc : ""))) {
      free(dst);
      free(d_esc);
      free(p_esc);
      ir_block_vec_free(&ctx.blocks);
      ir_var_type_vec_free(&ctx.vars);
      ast_free(root);
      ir_free_fn_sigs(fn_sigs);
      set_diag(out_diag, file, 1, 1, "E0002", "INTERNAL_ERROR", "IR lowering allocation failure");
      return 2;
    }
    free(d_esc);
    free(p_esc);
    ProtoFieldVec fv = proto_collect_fields(a.protos, proto_name);
    for (size_t fi = 0; fi < fv.len; fi++) {
      ProtoField *f = fv.items[fi];
      int is_exc = proto_is_subtype(a.protos, proto_name, "Exception");
      if (is_exc && f && f->name) {
        if (strcmp(f->name, "file") == 0 || strcmp(f->name, "line") == 0 || strcmp(f->name, "column") == 0 ||
            strcmp(f->name, "message") == 0 || strcmp(f->name, "cause") == 0 ||
            strcmp(f->name, "code") == 0 || strcmp(f->name, "category") == 0) {
          continue;
        }
      }
      char *val = ir_emit_default_value(&ctx, f && f->type ? f->type : "unknown", proto_name);
      if (!val) continue;
      char *t_esc = json_escape(dst);
      char *n_esc = json_escape(f ? f->name : "");
      char *v_esc = json_escape(val);
      ir_emit(&ctx, str_printf("{\"op\":\"member_set\",\"target\":\"%s\",\"name\":\"%s\",\"src\":\"%s\"}", t_esc ? t_esc : "",
                               n_esc ? n_esc : "", v_esc ? v_esc : ""));
      free(t_esc);
      free(n_esc);
      free(v_esc);
      free(val);
    }
    free(fv.items);
    char *dst_esc = json_escape(dst);
    ir_emit(&ctx, str_printf("{\"op\":\"ret\",\"value\":\"%s\",\"type\":{\"kind\":\"IRType\",\"name\":\"%s\"}}", dst_esc ? dst_esc : "",
                             proto_name));
    free(dst_esc);
    free(dst);

    char *clone_name = str_printf("%s.clone", proto_name);
    char *fn_name = json_escape(clone_name ? clone_name : "");
    free(clone_name);
    char *ret_esc = json_escape(proto_name);
    fprintf(out, "      {\n");
    fprintf(out, "        \"kind\": \"Function\",\n");
    fprintf(out, "        \"name\": \"%s\",\n", fn_name ? fn_name : "");
    fputs("        \"params\": [],\n", out);
    fprintf(out, "        \"returnType\": {\"kind\": \"IRType\", \"name\": \"%s\"},\n", ret_esc ? ret_esc : "void");
    fputs("        \"blocks\": [\n", out);
    for (size_t bi = 0; bi < ctx.blocks.len; bi++) {
      IrBlock *b = &ctx.blocks.items[bi];
      fprintf(out, "          {\n");
      fprintf(out, "            \"kind\": \"Block\",\n");
      fprintf(out, "            \"label\": \"%s\",\n", b->label ? b->label : "entry");
      fputs("            \"instrs\": [\n", out);
      for (size_t ii = 0; ii < b->instrs.len; ii++) {
        fprintf(out, "              %s%s\n", b->instrs.items[ii], (ii + 1 < b->instrs.len) ? "," : "");
      }
      fputs("            ]\n", out);
      fprintf(out, "          }%s\n", (bi + 1 < ctx.blocks.len) ? "," : "");
    }
    fputs("        ]\n", out);
    fputs("      }", out);

    free(fn_name);
    free(ret_esc);
    ir_block_vec_free(&ctx.blocks);
    ir_var_type_vec_free(&ctx.vars);
  }

  fputs("\n    ]\n", out);
  fputs("  }\n", out);
  fputs("}\n", out);
  ast_free(root);
  ir_free_fn_sigs(fn_sigs);
  free_fns(a.fns);
  free_imports(a.imports);
  free_namespaces(a.namespaces);
  free_user_modules(a.user_modules);
  free_registry(a.registry);
  free_protos(a.protos);
  return 0;
}

int ps_check_file_static(const char *file, PsDiag *out_diag) {
  AstNode *root = NULL;
  int rc = parse_file_internal(file, out_diag, &root);
  if (rc != 0) {
    ast_free(root);
    return rc;
  }

  Analyzer a;
  memset(&a, 0, sizeof(a));
  a.file = file;
  a.diag = out_diag;

  if (!collect_imports(&a, root)) {
    ast_free(root);
    free_fns(a.fns);
    free_imports(a.imports);
    free_namespaces(a.namespaces);
    free_user_modules(a.user_modules);
    free_registry(a.registry);
    return 1;
  }
  if (!collect_prototypes(&a, root)) {
    ast_free(root);
    free_fns(a.fns);
    free_imports(a.imports);
    free_namespaces(a.namespaces);
    free_user_modules(a.user_modules);
    free_registry(a.registry);
    free_protos(a.protos);
    return 1;
  }

  for (size_t i = 0; i < root->child_len; i++) {
    AstNode *fn = root->children[i];
    if (strcmp(fn->kind, "FunctionDecl") == 0) {
      if (!add_fn(&a, fn)) {
        ast_free(root);
        free_fns(a.fns);
        free_protos(a.protos);
        set_diag(out_diag, file, 1, 1, "E0002", "INTERNAL_ERROR", "analyzer allocation failure");
        return 2;
      }
    }
  }

  for (size_t i = 0; i < root->child_len; i++) {
    AstNode *fn = root->children[i];
    if (strcmp(fn->kind, "FunctionDecl") != 0) continue;
    if (!analyze_function(&a, fn)) {
      ast_free(root);
      free_fns(a.fns);
      free_protos(a.protos);
      return 1;
    }
  }

  for (size_t i = 0; i < root->child_len; i++) {
    AstNode *pd = root->children[i];
    if (strcmp(pd->kind, "PrototypeDecl") != 0) continue;
    const char *proto_name = pd->text ? pd->text : "";
    for (size_t j = 0; j < pd->child_len; j++) {
      AstNode *m = pd->children[j];
      if (strcmp(m->kind, "FunctionDecl") != 0) continue;
      if (!analyze_method(&a, m, proto_name)) {
        ast_free(root);
        free_fns(a.fns);
        free_protos(a.protos);
        free_user_modules(a.user_modules);
        return 1;
      }
    }
  }

  ast_free(root);
  free_fns(a.fns);
  free_imports(a.imports);
  free_namespaces(a.namespaces);
  free_user_modules(a.user_modules);
  free_registry(a.registry);
  free_protos(a.protos);
  return 0;
}
