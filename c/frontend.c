#include "frontend.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
      "false",     "self",     NULL,
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
      p_eat(p, TK_KW, "of");
      if (!parse_expr(p, &iter_expr)) return 0;
      is_iter = 1;
    } else if (p_at(p, TK_KW, "in")) {
      p_eat(p, TK_KW, "in");
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

static int parse_program(Parser *p) {
  Token *t0 = p_t(p, 0);
  AstNode *root = ast_new("Program", NULL, t0->line, t0->col);
  if (!root) return 0;
  p->ast_root = root;
  p->ast_stack[0] = root;
  p->ast_sp = 1;
  while (!p_at(p, TK_EOF, NULL)) {
    if (!p_at(p, TK_KW, "function")) {
      Token *t = p_t(p, 0);
      set_diag(p->diag, p->file, t->line, t->col, "E1001", "PARSE_UNEXPECTED_TOKEN", "Top-level declaration expected");
      return 0;
    }
    if (!parse_function_decl(p)) return 0;
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

typedef struct {
  const char *file;
  PsDiag *diag;
  FnSig *fns;
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

static int scope_define(Scope *s, const char *name, const char *type) {
  Sym *e = (Sym *)calloc(1, sizeof(Sym));
  if (!e) return 0;
  e->name = strdup(name ? name : "");
  e->type = strdup(type ? type : "unknown");
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

static const char *scope_lookup(Scope *s, const char *name) {
  for (Scope *cur = s; cur; cur = cur->parent) {
    for (Sym *e = cur->syms; e; e = e->next) {
      if (strcmp(e->name, name) == 0) return e->type;
    }
  }
  return "unknown";
}

static FnSig *find_fn(Analyzer *a, const char *name) {
  for (FnSig *f = a->fns; f; f = f->next) {
    if (strcmp(f->name, name) == 0) return f;
  }
  return NULL;
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
  if (strcmp(callee->kind, "Identifier") != 0) return strdup("unknown");
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

static char *infer_expr_type(Analyzer *a, AstNode *e, Scope *scope, int *ok) {
  if (!e) return strdup("unknown");
  if (strcmp(e->kind, "Literal") == 0) {
    if (e->text && (strcmp(e->text, "true") == 0 || strcmp(e->text, "false") == 0)) return strdup("bool");
    if (e->text && (is_all_digits(e->text) || is_float_token(e->text))) return strdup(is_float_token(e->text) ? "float" : "int");
    return strdup("string");
  }
  if (strcmp(e->kind, "Identifier") == 0) return strdup(scope_lookup(scope, e->text ? e->text : ""));
  if (strcmp(e->kind, "UnaryExpr") == 0 || strcmp(e->kind, "PostfixExpr") == 0 || strcmp(e->kind, "MemberExpr") == 0) {
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
      if (lhs && rhs && strcmp(lhs, rhs) != 0) {
        char msg[160];
        snprintf(msg, sizeof(msg), "cannot assign %s to %s", rhs, lhs);
        set_diag(a->diag, a->file, st->line, st->col, "E3001", "TYPE_MISMATCH_ASSIGNMENT", msg);
        free(lhs);
        free(rhs);
        return 0;
      }
      if (lhs) {
        if (!scope_define(scope, st->text ? st->text : "", lhs)) {
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
      if (!lhs || !scope_define(scope, st->text ? st->text : "", lhs)) {
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
      if (!scope_define(scope, st->text ? st->text : "", rhs ? rhs : "unknown")) {
        free(rhs);
        return 0;
      }
      free(rhs);
    }
    return 1;
  }
  if (strcmp(st->kind, "AssignStmt") == 0 && st->child_len >= 2) {
    int ok = 1;
    char *lhs = infer_assignable_type(a, st->children[0], scope, &ok);
    char *rhs = infer_expr_type(a, st->children[1], scope, &ok);
    if (!ok) {
      free(lhs);
      free(rhs);
      return 0;
    }
    if (lhs && rhs && strcmp(lhs, rhs) != 0) {
      char msg[160];
      snprintf(msg, sizeof(msg), "cannot assign %s to %s", rhs, lhs);
      set_diag(a->diag, a->file, st->line, st->col, "E3001", "TYPE_MISMATCH_ASSIGNMENT", msg);
      free(lhs);
      free(rhs);
      return 0;
    }
    free(lhs);
    free(rhs);
    return 1;
  }
  if (strcmp(st->kind, "ExprStmt") == 0 && st->child_len > 0) {
    int ok = 1;
    char *t = infer_expr_type(a, st->children[0], scope, &ok);
    free(t);
    return ok;
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
        int okd = scope_define(&s2, c->text ? c->text : "", tt ? tt : "unknown");
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
    if (!tt || !scope_define(&root, c->text ? c->text : "", tt)) {
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
  return 0;
}
