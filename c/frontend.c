#include "frontend.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime/ps_json.h"

static char *str_printf(const char *fmt, ...);

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
        set_diag(l->diag, l->file, sl, sc, "E1002", "PARSE_UNCLOSED_BLOCK", "Unclosed block comment");
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
      size_t a = l->i;
      int ok_close = 0;
      while (!l_eof(l)) {
        if (l_ch(l, 0) == '\\') {
          l_advance(l);
          if (!l_eof(l)) l_advance(l);
          continue;
        }
        if (l_ch(l, 0) == '"') {
          ok_close = 1;
          break;
        }
        l_advance(l);
      }
      if (!ok_close) {
        set_diag(l->diag, l->file, line, col, "E1002", "PARSE_UNCLOSED_BLOCK", "Unclosed string literal");
        return 0;
      }
      char *s = dup_range(l->src, a, l->i);
      if (!s) return 0;
      l_advance(l);
      int ok = lex_add(l, TK_STR, s, line, col);
      free(s);
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

    char msg[96];
    snprintf(msg, sizeof(msg), "Unexpected character '%c'", c);
    set_diag(l->diag, l->file, line, col, "E1001", "PARSE_UNEXPECTED_TOKEN", msg);
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
    char msg[128];
    if (text) snprintf(msg, sizeof(msg), "unexpected token '%s', expecting '%s'", t->text, text);
    else snprintf(msg, sizeof(msg), "unexpected token '%s'", t->text);
    set_diag(p->diag, p->file, t->line, t->col, "E1001", "PARSE_UNEXPECTED_TOKEN", msg);
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
    set_diag(p->diag, p->file, t->line, t->col, "E1001", "PARSE_UNEXPECTED_TOKEN", "type expected");
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
  set_diag(p->diag, p->file, t->line, t->col, "E1001", "PARSE_UNEXPECTED_TOKEN", "unknown type");
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
      set_diag(p->diag, p->file, l->line, l->col, "E1002", "PARSE_UNCLOSED_BLOCK", "Unclosed block");
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
    set_diag(p->diag, p->file, t->line, t->col, "E1001", "PARSE_UNEXPECTED_TOKEN", "Expected 'case' or 'default'");
    p_ast_pop(p);
    return 0;
  }
  p_ast_pop(p);
  return p_eat(p, TK_SYM, "}");
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
    Token *ct = p_t(p, 0);
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
    if (!p_ast_add(p, "CatchClause", name->text, ct->line, ct->col, &clause)) {
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
    set_diag(p->diag, p->file, t->line, t->col, "E1001", "PARSE_UNEXPECTED_TOKEN", "catch or finally expected");
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
        ast_free(tmp_decl);
      } else if (looks_like_assign_stmt(p)) {
        if (!parse_postfix_expr(p, NULL)) return 0;
        if (!p_eat(p, TK_SYM, NULL)) return 0;
        Token *op = &p->toks->items[p->i - 1];
        if (!(strcmp(op->text, "=") == 0 || strcmp(op->text, "+=") == 0 || strcmp(op->text, "-=") == 0 ||
              strcmp(op->text, "*=") == 0 || strcmp(op->text, "/=") == 0)) {
          set_diag(p->diag, p->file, op->line, op->col, "E1001", "PARSE_UNEXPECTED_TOKEN", "Assignment operator expected");
          return 0;
        }
        if (!parse_conditional_expr(p, NULL)) return 0;
      } else {
        if (!parse_expr(p, NULL)) return 0;
      }
    }
    if (!p_eat(p, TK_SYM, ";")) return 0;
    if (!p_at(p, TK_SYM, ";")) {
      if (!parse_expr(p, NULL)) return 0;
    }
    if (!p_eat(p, TK_SYM, ";")) return 0;
    if (!p_at(p, TK_SYM, ")")) {
      if (looks_like_assign_stmt(p)) {
        if (!parse_postfix_expr(p, NULL)) return 0;
        if (!p_eat(p, TK_SYM, NULL)) return 0;
        Token *op = &p->toks->items[p->i - 1];
        if (!(strcmp(op->text, "=") == 0 || strcmp(op->text, "+=") == 0 || strcmp(op->text, "-=") == 0 ||
              strcmp(op->text, "*=") == 0 || strcmp(op->text, "/=") == 0)) {
          set_diag(p->diag, p->file, op->line, op->col, "E1001", "PARSE_UNEXPECTED_TOKEN", "Assignment operator expected");
          return 0;
        }
        if (!parse_conditional_expr(p, NULL)) return 0;
      } else if (!parse_expr(p, NULL)) {
        return 0;
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
      set_diag(p->diag, p->file, op->line, op->col, "E1001", "PARSE_UNEXPECTED_TOKEN", "Assignment operator expected");
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
  char msg[128];
  snprintf(msg, sizeof(msg), "unexpected token '%s'", t->text);
  set_diag(p->diag, p->file, t->line, t->col, "E1001", "PARSE_UNEXPECTED_TOKEN", msg);
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
        set_diag(p->diag, p->file, name->line, name->col, "E1001", "PARSE_UNEXPECTED_TOKEN", "member name expected");
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
  char *mod = parse_module_path(p);
  if (!mod) return 0;
  AstNode *imp = NULL;
  if (!p_ast_add(p, "ImportDecl", mod, ikw->line, ikw->col, &imp)) {
    free(mod);
    return 0;
  }
  free(mod);
  if (!p_ast_push(p, imp)) return 0;

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
    if (p_at(p, TK_KW, "function")) {
      if (!parse_function_decl(p)) return 0;
      continue;
    }
    Token *t = p_t(p, 0);
    set_diag(p->diag, p->file, t->line, t->col, "E1001", "PARSE_UNEXPECTED_TOKEN", "Top-level declaration expected");
    return 0;
  }
  return 1;
}

static char *read_file(const char *path, size_t *out_n) {
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

typedef struct Sym {
  char *name;
  char *type;
  int known_list_len; // -1 = unknown
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
  struct ImportNamespace *next;
} ImportNamespace;

typedef struct {
  const char *file;
  PsDiag *diag;
  FnSig *fns;
  ModuleRegistry *registry;
  ImportSymbol *imports;
  ImportNamespace *namespaces;
} Analyzer;

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
      consume_kw(p, "string") || consume_kw(p, "File") || consume_kw(p, "JSONValue")) {
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
  const char *env = getenv("PS_MODULE_REGISTRY");
  const char *candidates[] = {env, "modules/registry.json", "../modules/registry.json", NULL};
  char *data = NULL;
  size_t n = 0;
  for (int i = 0; candidates[i]; i++) {
    if (!candidates[i] || !*candidates[i]) continue;
    data = read_file(candidates[i], &n);
    if (data) break;
  }
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
        } else if (strcmp(ctype->as.str_v, "string") == 0 || strcmp(ctype->as.str_v, "file") == 0) {
          if (!cval || cval->type != PS_JSON_STRING) {
            free(rc->name);
            free(rc->type);
            free(rc);
            continue;
          }
          rc->value = strdup(cval->as.str_v);
        } else if (strcmp(ctype->as.str_v, "eof") == 0) {
          rc->value = strdup("");
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

static int scope_define(Scope *s, const char *name, const char *type, int known_list_len) {
  Sym *e = (Sym *)calloc(1, sizeof(Sym));
  if (!e) return 0;
  e->name = strdup(name ? name : "");
  e->type = strdup(type ? type : "unknown");
  e->known_list_len = known_list_len;
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

static Sym *scope_lookup_sym(Scope *s, const char *name) {
  for (Scope *cur = s; cur; cur = cur->parent) {
    for (Sym *e = cur->syms; e; e = e->next) {
      if (strcmp(e->name, name) == 0) return e;
    }
  }
  return NULL;
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

static int collect_imports(Analyzer *a, AstNode *root) {
  size_t import_count = 0;
  for (size_t i = 0; i < root->child_len; i++) {
    if (strcmp(root->children[i]->kind, "ImportDecl") == 0) import_count += 1;
  }
  if (import_count == 0) return 1;
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
  for (size_t i = 0; i < root->child_len; i++) {
    AstNode *imp = root->children[i];
    if (strcmp(imp->kind, "ImportDecl") != 0) continue;
    const char *mod = imp->text ? imp->text : "";
    RegMod *m = registry_find_mod(a->registry, mod);
    if (!m) {
      set_diag(a->diag, a->file, imp->line, imp->col, "E2001", "UNRESOLVED_NAME", "unknown module");
      return 0;
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
        RegFn *rf = registry_find_fn(a->registry, mod, name);
        if (!rf) {
          set_diag(a->diag, a->file, it->line, it->col, "E2001", "UNRESOLVED_NAME", "unknown symbol in module");
          return 0;
        }
        if (!rf->valid) {
          set_diag(a->diag, a->file, it->line, it->col, "E2001", "UNRESOLVED_NAME", "invalid registry signature");
          return 0;
        }
        const char *alias = import_item_alias(it);
        const char *local = alias ? alias : name;
        ImportSymbol *s = (ImportSymbol *)calloc(1, sizeof(ImportSymbol));
        if (!s) return 0;
        s->local = strdup(local);
        s->module = strdup(mod);
        s->name = strdup(name);
        s->next = a->imports;
        a->imports = s;
        if (!add_imported_fn(a, local, rf->ret_type, rf->param_count)) return 0;
      }
    } else {
      const char *alias = import_decl_alias(imp);
      const char *ns = alias ? alias : last_segment(mod);
      ImportNamespace *n = (ImportNamespace *)calloc(1, sizeof(ImportNamespace));
      if (!n) return 0;
      n->alias = strdup(ns);
      n->module = strdup(mod);
      n->next = a->namespaces;
      a->namespaces = n;
    }
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

static char *infer_expr_type(Analyzer *a, AstNode *e, Scope *scope, int *ok);

static char *infer_call_type(Analyzer *a, AstNode *e, Scope *scope, int *ok) {
  (void)scope;
  if (e->child_len == 0) return strdup("unknown");
  AstNode *callee = e->children[0];
  if (strcmp(callee->kind, "Identifier") == 0) {
    FnSig *f = find_fn(a, callee->text ? callee->text : "");
    if (!f) return strdup("unknown");
    int argc = (int)e->child_len - 1;
    if (!f->variadic) {
      if (argc != f->param_count) {
        set_diag(a->diag, a->file, e->line, e->col, "E3001", "TYPE_MISMATCH_ASSIGNMENT", "arity mismatch");
        *ok = 0;
        return NULL;
      }
    } else {
      if (argc <= f->fixed_count) {
        set_diag(a->diag, a->file, e->line, e->col, "E3002", "VARIADIC_EMPTY_CALL", "variadic argument list must be non-empty");
        *ok = 0;
        return NULL;
      }
    }
    return strdup(f->ret_type);
  }
  if (strcmp(callee->kind, "MemberExpr") == 0 && callee->child_len > 0) {
    AstNode *target = callee->children[0];
    if (strcmp(target->kind, "Identifier") == 0) {
      ImportNamespace *ns = find_namespace(a, target->text ? target->text : "");
      if (ns) {
        RegFn *rf = registry_find_fn(a->registry, ns->module, callee->text ? callee->text : "");
        if (!rf) return strdup("unknown");
        int argc = (int)e->child_len - 1;
        if (argc != rf->param_count) {
          set_diag(a->diag, a->file, e->line, e->col, "E3001", "TYPE_MISMATCH_ASSIGNMENT", "arity mismatch");
          *ok = 0;
          return NULL;
        }
        return strdup(rf->ret_type);
      }
    }
  }
  return strdup("unknown");
}

static char *infer_expr_type(Analyzer *a, AstNode *e, Scope *scope, int *ok) {
  if (!e) return strdup("unknown");
  if (strcmp(e->kind, "Literal") == 0) {
    if (e->text && (strcmp(e->text, "true") == 0 || strcmp(e->text, "false") == 0)) return strdup("bool");
    if (e->text && (is_all_digits(e->text) || is_hex_token(e->text) || is_float_token(e->text)))
      return strdup(is_float_token(e->text) ? "float" : "int");
    return strdup("string");
  }
  if (strcmp(e->kind, "Identifier") == 0) return strdup(scope_lookup(scope, e->text ? e->text : ""));
  if (strcmp(e->kind, "UnaryExpr") == 0 || strcmp(e->kind, "PostfixExpr") == 0 || strcmp(e->kind, "MemberExpr") == 0) {
    if (strcmp(e->kind, "MemberExpr") == 0 && e->child_len > 0) {
      AstNode *target = e->children[0];
      if (strcmp(target->kind, "Identifier") == 0) {
        ImportNamespace *ns = find_namespace(a, target->text ? target->text : "");
        if (ns) {
          RegConst *rc = registry_find_const(a->registry, ns->module, e->text ? e->text : "");
          if (rc && rc->type) {
            if (strcmp(rc->type, "float") == 0) return strdup("float");
            if (strcmp(rc->type, "string") == 0) return strdup("string");
            if (strcmp(rc->type, "file") == 0) return strdup("File");
            if (strcmp(rc->type, "eof") == 0) return strdup("EOF");
          }
        }
      }
    }
    if (e->child_len > 0) return infer_expr_type(a, e->children[0], scope, ok);
    return strdup("unknown");
  }
  if (strcmp(e->kind, "BinaryExpr") == 0) {
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
        int empty_map_init =
            (init && strcmp(init->kind, "MapLiteral") == 0 && init->child_len == 0 && lhs && strncmp(lhs, "map<", 4) == 0);
        int empty_list_init =
            (init && strcmp(init->kind, "ListLiteral") == 0 && init->child_len == 0 && lhs && strncmp(lhs, "list<", 5) == 0);
        if (!empty_map_init && !empty_list_init) {
        char msg[160];
        snprintf(msg, sizeof(msg), "cannot assign %s to %s", rhs, lhs);
        set_diag(a->diag, a->file, st->line, st->col, "E3001", "TYPE_MISMATCH_ASSIGNMENT", msg);
        free(lhs);
        free(rhs);
        return 0;
        }
      }
      if (lhs) {
        int known_len = -1;
        if (init && strcmp(init->kind, "ListLiteral") == 0) known_len = (int)init->child_len;
        if (!scope_define(scope, st->text ? st->text : "", lhs, known_len)) {
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
      if (!lhs || !scope_define(scope, st->text ? st->text : "", lhs, -1)) {
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
      if (!scope_define(scope, st->text ? st->text : "", rhs ? rhs : "unknown", -1)) {
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
    if (lhs && rhs && strcmp(lhs, rhs) != 0) {
      int empty_map_assign =
          (st->child_len >= 2 && st->children[1] && strcmp(st->children[1]->kind, "MapLiteral") == 0 &&
           st->children[1]->child_len == 0 && lhs && strncmp(lhs, "map<", 4) == 0);
      int empty_list_assign =
          (st->child_len >= 2 && st->children[1] && strcmp(st->children[1]->kind, "ListLiteral") == 0 &&
           st->children[1]->child_len == 0 && lhs && strncmp(lhs, "list<", 5) == 0);
      if (!empty_map_assign && !empty_list_assign) {
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
  if (strcmp(st->kind, "ReturnStmt") == 0 && st->child_len > 0) {
    int ok = 1;
    char *t = infer_expr_type(a, st->children[0], scope, &ok);
    free(t);
    return ok;
  }
  if (strcmp(st->kind, "ThrowStmt") == 0 && st->child_len > 0) {
    int ok = 1;
    char *t = infer_expr_type(a, st->children[0], scope, &ok);
    free(t);
    return ok;
  }
  if (strcmp(st->kind, "ForStmt") == 0) {
    Scope s2;
    memset(&s2, 0, sizeof(s2));
    s2.parent = scope;
    for (size_t i = 0; i < st->child_len; i++) {
      AstNode *c = st->children[i];
      if (strcmp(c->kind, "IterVar") == 0) {
        AstNode *tn = ast_child_kind(c, "Type");
        char *tt = canon_type(tn ? tn->text : "unknown");
        int okd = scope_define(&s2, c->text ? c->text : "", tt ? tt : "unknown", -1);
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
  if (strcmp(st->kind, "SwitchStmt") == 0) {
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
        if (!scope_define(&s2, c->text ? c->text : "", tt ? tt : "unknown", -1)) {
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
    if (!tt || !scope_define(&root, c->text ? c->text : "", tt, -1)) {
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
  char *src = read_file(file, &n);
  if (!src) {
    set_diag(out_diag, file, 1, 1, "E0001", "IO_READ_ERROR", "cannot read source file");
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

typedef struct {
  IrBlockVec blocks;
  size_t cur_block;
  StrVec instrs;
  int temp_id;
  int label_id;
  IrVarTypeVec vars;
  IrFnSig *fn_sigs;
  ImportSymbol *imports;
  ImportNamespace *namespaces;
  ModuleRegistry *registry;
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

static int ir_emit(IrFnCtx *c, char *json_obj) {
  if (!json_obj) return 0;
  if (c->blocks.len == 0) return 0;
  if (!str_vec_push(&c->blocks.items[c->cur_block].instrs, json_obj)) {
    free(json_obj);
    return 0;
  }
  return 1;
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
    if (e->text && (is_all_digits(e->text) || is_hex_token(e->text) || is_float_token(e->text)))
      return strdup(is_float_token(e->text) ? "float" : "int");
    return strdup("string");
  }
  if (strcmp(e->kind, "Identifier") == 0) {
    const char *t = ir_get_var_type(ctx, e->text ? e->text : "");
    return strdup(t ? t : "unknown");
  }
  if (strcmp(e->kind, "UnaryExpr") == 0 || strcmp(e->kind, "PostfixExpr") == 0 || strcmp(e->kind, "MemberExpr") == 0) {
    if (strcmp(e->kind, "MemberExpr") == 0) {
      if (e->text && strcmp(e->text, "toString") == 0) return strdup("string");
      if (e->child_len > 0) {
        AstNode *target = e->children[0];
        if (strcmp(target->kind, "Identifier") == 0) {
          ImportNamespace *ns = ir_find_namespace(ctx, target->text ? target->text : "");
          if (ns) {
            RegConst *rc = registry_find_const(ctx->registry, ns->module, e->text ? e->text : "");
            if (rc && rc->type) {
              if (strcmp(rc->type, "float") == 0) return strdup("float");
              if (strcmp(rc->type, "string") == 0) return strdup("string");
              if (strcmp(rc->type, "file") == 0) return strdup("File");
              if (strcmp(rc->type, "eof") == 0) return strdup("EOF");
            }
          }
        }
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
      char *recv_t = (c->child_len > 0) ? ir_guess_expr_type(c->children[0], ctx) : NULL;
      const char *m = c->text;
      if (recv_t) {
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
        } else if (strncmp(recv_t, "map<", 4) == 0) {
          if (strcmp(m, "length") == 0) return strdup("int");
          if (strcmp(m, "isEmpty") == 0) return strdup("bool");
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
      char *recv_esc = json_escape(recv);
      char *dst_esc = json_escape(dst);
      char *method_esc = json_escape(callee->text ? callee->text : "");
      if (strcmp(callee->text ? callee->text : "", "toString") == 0) {
        char *ins = str_printf("{\"op\":\"call_builtin_tostring\",\"dst\":\"%s\",\"value\":\"%s\"}", dst_esc ? dst_esc : "",
                               recv_esc ? recv_esc : "");
        if (!ir_emit(ctx, ins)) {
          free(dst);
          dst = NULL;
        }
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
  if (strcmp(e->kind, "Literal") == 0) {
    char *dst = ir_next_tmp(ctx);
    if (!dst) return NULL;
    char *dst_esc = json_escape(dst);
    char *val_esc = json_escape(e->text ? e->text : "");
    const char *lt = "string";
    if (e->text && (strcmp(e->text, "true") == 0 || strcmp(e->text, "false") == 0)) lt = "bool";
    else if (e->text && (is_all_digits(e->text) || is_hex_token(e->text) || is_float_token(e->text)))
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
  if (strcmp(e->kind, "Identifier") == 0) {
    char *dst = ir_next_tmp(ctx);
    if (!dst) return NULL;
    char *dst_esc = json_escape(dst);
    char *name_esc = json_escape(e->text ? e->text : "");
    const char *vt = ir_get_var_type(ctx, e->text ? e->text : "");
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
  if (strcmp(st->kind, "Block") == 0) {
    for (size_t i = 0; i < st->child_len; i++) {
      if (!ir_lower_stmt(st->children[i], ctx)) return 0;
    }
    return 1;
  }
  if (strcmp(st->kind, "VarDecl") == 0) {
    AstNode *tn = ast_child_kind(st, "Type");
    char *type = ast_type_to_ir_name(tn);
    if (!type) type = strdup("unknown");
    if (!ir_set_var_type(ctx, st->text ? st->text : "", type ? type : "unknown")) {
      free(type);
      return 0;
    }
    char *name_esc = json_escape(st->text ? st->text : "");
    char *type_esc = json_escape(type ? type : "unknown");
    char *ins = str_printf("{\"op\":\"var_decl\",\"name\":\"%s\",\"type\":{\"kind\":\"IRType\",\"name\":\"%s\"}}", name_esc ? name_esc : "",
                           type_esc ? type_esc : "");
    free(name_esc);
    free(type_esc);
    free(type);
    if (!ir_emit(ctx, ins)) return 0;
    AstNode *last = ast_last_child(st);
    if (last && (!tn || last != tn)) {
      char *v = ir_lower_expr(last, ctx);
      if (!v) return 0;
      char *v_esc = json_escape(v);
      char *n_esc = json_escape(st->text ? st->text : "");
      char *ins2 = str_printf("{\"op\":\"store_var\",\"name\":\"%s\",\"src\":\"%s\",\"type\":{\"kind\":\"IRType\",\"name\":\"unknown\"}}",
                              n_esc ? n_esc : "", v_esc ? v_esc : "");
      free(v_esc);
      free(n_esc);
      free(v);
      if (!ir_emit(ctx, ins2)) return 0;
    }
    return 1;
  }
  if (strcmp(st->kind, "AssignStmt") == 0 && st->child_len >= 2) {
    AstNode *lhs = st->children[0];
    AstNode *rhs = st->children[1];
    char *v = ir_lower_expr(rhs, ctx);
    if (!v) return 0;
    if (strcmp(lhs->kind, "Identifier") == 0) {
      char *rhs_type = ir_guess_expr_type(rhs, ctx);
      if (rhs_type) {
        if (!ir_set_var_type(ctx, lhs->text ? lhs->text : "", rhs_type)) {
          free(rhs_type);
          free(v);
          return 0;
        }
        free(rhs_type);
      }
      char *n_esc = json_escape(lhs->text ? lhs->text : "");
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
      char *ins = str_printf("{\"op\":\"throw\",\"value\":\"%s\"}", v_esc ? v_esc : "");
      free(v_esc);
      free(v);
      return ir_emit(ctx, ins);
    }
    return ir_emit(ctx, strdup("{\"op\":\"throw\",\"value\":\"\"}"));
  }
  if (strcmp(st->kind, "TryStmt") == 0) {
    AstNode *try_block = NULL;
    AstNode *catch_clause = NULL;
    AstNode *finally_clause = NULL;
    for (size_t i = 0; i < st->child_len; i++) {
      AstNode *c = st->children[i];
      if (strcmp(c->kind, "Block") == 0 && !try_block) try_block = c;
      else if (strcmp(c->kind, "CatchClause") == 0 && !catch_clause) catch_clause = c;
      else if (strcmp(c->kind, "FinallyClause") == 0 && !finally_clause) finally_clause = c;
    }
    if (!try_block) return 0;
    char *try_label = ir_next_label(ctx, "try_body_");
    char *catch_label = catch_clause ? ir_next_label(ctx, "try_catch_") : NULL;
    char *finally_label = finally_clause ? ir_next_label(ctx, "try_finally_") : NULL;
    char *finally_rethrow_label = (finally_clause && !catch_clause) ? ir_next_label(ctx, "try_finally_rethrow_") : NULL;
    char *done_label = ir_next_label(ctx, "try_done_");
    if (!try_label || !done_label || (catch_clause && !catch_label) || (finally_clause && !finally_label) ||
        (finally_clause && !catch_clause && !finally_rethrow_label)) {
      free(try_label); free(catch_label); free(finally_label); free(finally_rethrow_label); free(done_label);
      return 0;
    }
    size_t try_idx = 0, catch_idx = 0, finally_idx = 0, done_idx = 0;
    size_t finally_rethrow_idx = 0;
    if (!ir_add_block(ctx, try_label, &try_idx) ||
        (catch_label && !ir_add_block(ctx, catch_label, &catch_idx)) ||
        (finally_label && !ir_add_block(ctx, finally_label, &finally_idx)) ||
        (finally_rethrow_label && !ir_add_block(ctx, finally_rethrow_label, &finally_rethrow_idx)) ||
        !ir_add_block(ctx, done_label, &done_idx)) {
      free(try_label); free(catch_label); free(finally_label); free(finally_rethrow_label); free(done_label);
      return 0;
    }
    const char *handler = catch_label ? catch_label : (finally_rethrow_label ? finally_rethrow_label : (finally_label ? finally_label : done_label));
    char *h_esc = json_escape(handler);
    char *push = str_printf("{\"op\":\"push_handler\",\"target\":\"%s\"}", h_esc ? h_esc : "");
    free(h_esc);
    if (!ir_emit(ctx, push)) {
      free(try_label); free(catch_label); free(finally_label); free(finally_rethrow_label); free(done_label);
      return 0;
    }
    char *tgt = json_escape(try_label);
    char *jmp = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", tgt ? tgt : "");
    free(tgt);
    if (!ir_emit(ctx, jmp)) {
      free(try_label); free(catch_label); free(finally_label); free(finally_rethrow_label); free(done_label);
      return 0;
    }
    ctx->cur_block = try_idx;
    if (!ir_lower_stmt(try_block, ctx)) {
      free(try_label); free(catch_label); free(finally_label); free(finally_rethrow_label); free(done_label);
      return 0;
    }
    if (!ir_emit(ctx, strdup("{\"op\":\"pop_handler\"}"))) {
      free(try_label); free(catch_label); free(finally_label); free(finally_rethrow_label); free(done_label);
      return 0;
    }
    char *after_try = json_escape(finally_label ? finally_label : done_label);
    char *jmp2 = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", after_try ? after_try : "");
    free(after_try);
    if (!ir_emit(ctx, jmp2)) {
      free(try_label); free(catch_label); free(finally_label); free(finally_rethrow_label); free(done_label);
      return 0;
    }
    if (catch_clause) {
      ctx->cur_block = catch_idx;
      AstNode *tn = ast_child_kind(catch_clause, "Type");
      char *type = ast_type_to_ir_name(tn);
      if (!type) type = strdup("unknown");
      if (!ir_set_var_type(ctx, catch_clause->text ? catch_clause->text : "", type ? type : "unknown")) {
        free(type); free(try_label); free(catch_label); free(finally_label); free(finally_rethrow_label); free(done_label);
        return 0;
      }
      char *name_esc = json_escape(catch_clause->text ? catch_clause->text : "");
      char *type_esc = json_escape(type ? type : "unknown");
      char *decl = str_printf("{\"op\":\"var_decl\",\"name\":\"%s\",\"type\":{\"kind\":\"IRType\",\"name\":\"%s\"}}", name_esc ? name_esc : "",
                              type_esc ? type_esc : "");
      free(name_esc); free(type_esc); free(type);
      if (!ir_emit(ctx, decl)) {
        free(try_label); free(catch_label); free(finally_label); free(finally_rethrow_label); free(done_label);
        return 0;
      }
      char *ex = ir_next_tmp(ctx);
      if (!ex) {
        free(try_label); free(catch_label); free(finally_label); free(finally_rethrow_label); free(done_label);
        return 0;
      }
      char *ex_esc = json_escape(ex);
      char *get = str_printf("{\"op\":\"get_exception\",\"dst\":\"%s\"}", ex_esc ? ex_esc : "");
      free(ex_esc);
      if (!ir_emit(ctx, get)) {
        free(ex); free(try_label); free(catch_label); free(finally_label); free(finally_rethrow_label); free(done_label);
        return 0;
      }
      char *n_esc = json_escape(catch_clause->text ? catch_clause->text : "");
      char *ex2 = json_escape(ex);
      char *store = str_printf("{\"op\":\"store_var\",\"name\":\"%s\",\"src\":\"%s\",\"type\":{\"kind\":\"IRType\",\"name\":\"unknown\"}}",
                               n_esc ? n_esc : "", ex2 ? ex2 : "");
      free(n_esc); free(ex2); free(ex);
      if (!ir_emit(ctx, store)) {
        free(try_label); free(catch_label); free(finally_label); free(finally_rethrow_label); free(done_label);
        return 0;
      }
      AstNode *cblk = ast_child_kind(catch_clause, "Block");
      if (cblk && !ir_lower_stmt(cblk, ctx)) {
        free(try_label); free(catch_label); free(finally_label); free(finally_rethrow_label); free(done_label);
        return 0;
      }
      char *after_c = json_escape(finally_label ? finally_label : done_label);
      char *jmpc = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", after_c ? after_c : "");
      free(after_c);
      if (!ir_emit(ctx, jmpc)) {
        free(try_label); free(catch_label); free(finally_label); free(finally_rethrow_label); free(done_label);
        return 0;
      }
    }
    if (finally_clause) {
      ctx->cur_block = finally_idx;
      AstNode *fblk = ast_child_kind(finally_clause, "Block");
      if (fblk && !ir_lower_stmt(fblk, ctx)) {
        free(try_label); free(catch_label); free(finally_label); free(finally_rethrow_label); free(done_label);
        return 0;
      }
      char *aft = json_escape(done_label);
      char *jmpf = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", aft ? aft : "");
      free(aft);
      if (!ir_emit(ctx, jmpf)) {
        free(try_label); free(catch_label); free(finally_label); free(finally_rethrow_label); free(done_label);
        return 0;
      }
    }
    if (finally_rethrow_label) {
      ctx->cur_block = finally_rethrow_idx;
      AstNode *fblk2 = ast_child_kind(finally_clause, "Block");
      if (fblk2 && !ir_lower_stmt(fblk2, ctx)) {
        free(try_label); free(catch_label); free(finally_label); free(finally_rethrow_label); free(done_label);
        return 0;
      }
      if (!ir_emit(ctx, strdup("{\"op\":\"rethrow\"}"))) {
        free(try_label); free(catch_label); free(finally_label); free(finally_rethrow_label); free(done_label);
        return 0;
      }
    }
    ctx->cur_block = done_idx;
    if (!ir_emit(ctx, strdup("{\"op\":\"nop\"}"))) {
      free(try_label); free(catch_label); free(finally_label); free(finally_rethrow_label); free(done_label);
      return 0;
    }
    free(try_label); free(catch_label); free(finally_label); free(finally_rethrow_label); free(done_label);
    return 1;
  }
  if (strcmp(st->kind, "BreakStmt") == 0) return ir_emit(ctx, strdup("{\"op\":\"break\"}"));
  if (strcmp(st->kind, "ContinueStmt") == 0) return ir_emit(ctx, strdup("{\"op\":\"continue\"}"));
  if (strcmp(st->kind, "SwitchStmt") == 0) {
    AstNode *sw_expr = (st->child_len > 0) ? st->children[0] : NULL;
    char *swv = sw_expr ? ir_lower_expr(sw_expr, ctx) : NULL;
    if (!swv) return 0;

    char *done_label = ir_next_label(ctx, "sw_done_");
    if (!done_label) {
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
    return 1;
  }
  if (strcmp(st->kind, "ForStmt") == 0) {
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

    if (!ir_lower_stmt(body, ctx)) {
      free(seq); free(cursor); free(elem);
      free(init_label); free(cond_label); free(body_label); free(done_label);
      return 0;
    }
    if (!ir_is_terminated_block(&ctx->blocks.items[ctx->cur_block].instrs)) {
      char *cond2_esc = json_escape(cond_label);
      char *j_back = str_printf("{\"op\":\"jump\",\"target\":\"%s\"}", cond2_esc ? cond2_esc : "");
      free(cond2_esc);
      if (!ir_emit(ctx, j_back)) {
        free(seq); free(cursor); free(elem);
        free(init_label); free(cond_label); free(body_label); free(done_label);
        return 0;
      }
    }

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
    free_registry(a.registry);
    return 1;
  }

  fputs("{\n", out);
  fputs("  \"ir_version\": \"1.0.0\",\n", out);
  fputs("  \"format\": \"ProtoScriptIR\",\n", out);
  fputs("  \"module\": {\n", out);
  fputs("    \"kind\": \"Module\",\n", out);
  fputs("    \"functions\": [\n", out);

  IrFnSig *fn_sigs = NULL;
  for (FnSig *f = a.fns; f; f = f->next) {
    IrFnSig *s = (IrFnSig *)calloc(1, sizeof(IrFnSig));
    if (!s) {
      ast_free(root);
      free_fns(a.fns);
      free_imports(a.imports);
      free_namespaces(a.namespaces);
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
      if (!ptn || !ir_set_var_type(&ctx, p->text ? p->text : "", ptn)) {
        free(ptn);
        ir_block_vec_free(&ctx.blocks);
        ir_var_type_vec_free(&ctx.vars);
        ast_free(root);
        ir_free_fn_sigs(fn_sigs);
        set_diag(out_diag, file, 1, 1, "E0002", "INTERNAL_ERROR", "IR param allocation failure");
        return 2;
      }
      free(ptn);
    }
    AstNode *blk = ast_child_kind(fn, "Block");
    if (blk && !ir_lower_stmt(blk, &ctx)) {
      ir_block_vec_free(&ctx.blocks);
      ir_var_type_vec_free(&ctx.vars);
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
      char *pn = json_escape(p->text ? p->text : "");
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
  }

  fputs("\n    ]\n", out);
  fputs("  }\n", out);
  fputs("}\n", out);
  ast_free(root);
  ir_free_fn_sigs(fn_sigs);
  free_fns(a.fns);
  free_imports(a.imports);
  free_namespaces(a.namespaces);
  free_registry(a.registry);
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
    free_registry(a.registry);
    return 1;
  }

  for (size_t i = 0; i < root->child_len; i++) {
    AstNode *fn = root->children[i];
    if (strcmp(fn->kind, "FunctionDecl") == 0) {
      if (!add_fn(&a, fn)) {
        ast_free(root);
        free_fns(a.fns);
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
      return 1;
    }
  }

  ast_free(root);
  free_fns(a.fns);
  free_imports(a.imports);
  free_namespaces(a.namespaces);
  free_registry(a.registry);
  return 0;
}
