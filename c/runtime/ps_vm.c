#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <math.h>

#include "ps_json.h"
#include "ps_list.h"
#include "ps_map.h"
#include "ps_modules.h"
#include "ps_object.h"
#include "ps_string.h"
#include "ps_vm.h"

typedef struct {
  char *name;
  PS_Value *value;
} PS_Binding;

typedef struct {
  PS_Binding *items;
  size_t len;
  size_t cap;
} PS_Bindings;

static int parse_int_strict(PS_Context *ctx, const char *s, size_t len, int64_t *out) {
  if (!s || !out) return 0;
  if (len == 0) {
    ps_throw(ctx, PS_ERR_TYPE, "invalid int format");
    return 0;
  }
  char *buf = (char *)malloc(len + 1);
  if (!buf) {
    ps_throw(ctx, PS_ERR_OOM, "out of memory");
    return 0;
  }
  memcpy(buf, s, len);
  buf[len] = '\0';
  char *end = NULL;
  errno = 0;
  long long v = strtoll(buf, &end, 10);
  int ok = (end && *end == '\0' && errno != ERANGE);
  free(buf);
  if (!ok) {
    ps_throw(ctx, PS_ERR_TYPE, "invalid int format");
    return 0;
  }
  *out = (int64_t)v;
  return 1;
}

static int parse_float_strict(PS_Context *ctx, const char *s, size_t len, double *out) {
  if (!s || !out) return 0;
  if (len == 0) {
    ps_throw(ctx, PS_ERR_TYPE, "invalid float format");
    return 0;
  }
  char *buf = (char *)malloc(len + 1);
  if (!buf) {
    ps_throw(ctx, PS_ERR_OOM, "out of memory");
    return 0;
  }
  memcpy(buf, s, len);
  buf[len] = '\0';
  char *end = NULL;
  errno = 0;
  double v = strtod(buf, &end);
  int ok = (end && *end == '\0' && errno != ERANGE);
  free(buf);
  if (!ok) {
    ps_throw(ctx, PS_ERR_TYPE, "invalid float format");
    return 0;
  }
  *out = v;
  return 1;
}

static int glyph_is_letter(uint32_t g) { return (g >= 'A' && g <= 'Z') || (g >= 'a' && g <= 'z'); }
static int glyph_is_digit(uint32_t g) { return (g >= '0' && g <= '9'); }
static int glyph_is_whitespace(uint32_t g) { return g == ' ' || g == '\t' || g == '\n' || g == '\r'; }
static int glyph_is_upper(uint32_t g) { return (g >= 'A' && g <= 'Z'); }
static int glyph_is_lower(uint32_t g) { return (g >= 'a' && g <= 'z'); }
static uint32_t glyph_to_upper(uint32_t g) { return (g >= 'a' && g <= 'z') ? (g - 32) : g; }
static uint32_t glyph_to_lower(uint32_t g) { return (g >= 'A' && g <= 'Z') ? (g + 32) : g; }

static int glyph_to_utf8(uint32_t g, uint8_t out[4], size_t *out_len) {
  if (g <= 0x7F) {
    out[0] = (uint8_t)g;
    *out_len = 1;
    return 1;
  }
  if (g <= 0x7FF) {
    out[0] = (uint8_t)(0xC0 | (g >> 6));
    out[1] = (uint8_t)(0x80 | (g & 0x3F));
    *out_len = 2;
    return 1;
  }
  if (g <= 0xFFFF) {
    if (g >= 0xD800 && g <= 0xDFFF) return 0;
    out[0] = (uint8_t)(0xE0 | (g >> 12));
    out[1] = (uint8_t)(0x80 | ((g >> 6) & 0x3F));
    out[2] = (uint8_t)(0x80 | (g & 0x3F));
    *out_len = 3;
    return 1;
  }
  if (g <= 0x10FFFF) {
    out[0] = (uint8_t)(0xF0 | (g >> 18));
    out[1] = (uint8_t)(0x80 | ((g >> 12) & 0x3F));
    out[2] = (uint8_t)(0x80 | ((g >> 6) & 0x3F));
    out[3] = (uint8_t)(0x80 | (g & 0x3F));
    *out_len = 4;
    return 1;
  }
  return 0;
}

typedef struct {
  char *op;
  char *dst;
  char *name;
  char *type;
  char *value;
  char *literalType;
  char *left;
  char *right;
  char *operator;
  char *cond;
  char *then_label;
  char *else_label;
  char *target;
  char *index;
  char *src;
  char *kind;
  char *iter;
  char *source;
  char *offset;
  char *len;
  char *mode;
  char *callee;
  char *receiver;
  char *divisor;
  char *map;
  char *key;
  char *thenValue;
  char *elseValue;
  char *shift;
  int width;
  char *method;
  char *proto;
  int readonly;
  char **args;
  size_t arg_count;
  struct {
    char *key;
    char *value;
  } *pairs;
  size_t pair_count;
} IRInstr;

typedef struct {
  char *label;
  IRInstr *instrs;
  size_t instr_count;
} IRBlock;

typedef struct {
  char *name;
  char **params;
  size_t param_count;
  char *ret_type;
  IRBlock *blocks;
  size_t block_count;
} IRFunction;

struct PS_IR_Module {
  IRFunction *fns;
  size_t fn_count;
};

static int view_is_valid(PS_Value *v) {
  if (!v || v->tag != PS_V_VIEW) return 0;
  if (!v->as.view_v.source) return 1;
  if (v->as.view_v.source->tag == PS_V_LIST) {
    return v->as.view_v.version == v->as.view_v.source->as.list_v.version;
  }
  return 1;
}

static void bindings_free(PS_Bindings *b) {
  if (!b) return;
  for (size_t i = 0; i < b->len; i++) {
    free(b->items[i].name);
    if (b->items[i].value) ps_value_release(b->items[i].value);
  }
  free(b->items);
  b->items = NULL;
  b->len = 0;
  b->cap = 0;
}

static PS_Value *bindings_get(PS_Bindings *b, const char *name) {
  for (size_t i = 0; i < b->len; i++) {
    if (strcmp(b->items[i].name, name) == 0) return b->items[i].value;
  }
  return NULL;
}

static void bindings_set(PS_Bindings *b, const char *name, PS_Value *v) {
  for (size_t i = 0; i < b->len; i++) {
    if (strcmp(b->items[i].name, name) == 0) {
      if (b->items[i].value) ps_value_release(b->items[i].value);
      b->items[i].value = v ? ps_value_retain(v) : NULL;
      return;
    }
  }
  if (b->len == b->cap) {
    size_t new_cap = b->cap == 0 ? 16 : b->cap * 2;
    PS_Binding *n = (PS_Binding *)realloc(b->items, sizeof(PS_Binding) * new_cap);
    if (!n) return;
    b->items = n;
    b->cap = new_cap;
  }
  b->items[b->len].name = strdup(name);
  b->items[b->len].value = v ? ps_value_retain(v) : NULL;
  b->len += 1;
}

static char *dup_json_string(PS_JsonValue *v) {
  if (!v || v->type != PS_JSON_STRING) return NULL;
  return strdup(v->as.str_v);
}

static IRInstr parse_instr(PS_JsonValue *obj) {
  IRInstr ins;
  memset(&ins, 0, sizeof(ins));
  ins.op = dup_json_string(ps_json_obj_get(obj, "op"));
  ins.dst = dup_json_string(ps_json_obj_get(obj, "dst"));
  ins.name = dup_json_string(ps_json_obj_get(obj, "name"));
  ins.type = dup_json_string(ps_json_obj_get(obj, "type"));
  ins.value = dup_json_string(ps_json_obj_get(obj, "value"));
  ins.literalType = dup_json_string(ps_json_obj_get(obj, "literalType"));
  ins.left = dup_json_string(ps_json_obj_get(obj, "left"));
  ins.right = dup_json_string(ps_json_obj_get(obj, "right"));
  ins.operator = dup_json_string(ps_json_obj_get(obj, "operator"));
  ins.cond = dup_json_string(ps_json_obj_get(obj, "cond"));
  ins.then_label = dup_json_string(ps_json_obj_get(obj, "then"));
  ins.else_label = dup_json_string(ps_json_obj_get(obj, "else"));
  ins.target = dup_json_string(ps_json_obj_get(obj, "target"));
  ins.index = dup_json_string(ps_json_obj_get(obj, "index"));
  ins.src = dup_json_string(ps_json_obj_get(obj, "src"));
  ins.kind = dup_json_string(ps_json_obj_get(obj, "kind"));
  ins.iter = dup_json_string(ps_json_obj_get(obj, "iter"));
  ins.source = dup_json_string(ps_json_obj_get(obj, "source"));
  ins.offset = dup_json_string(ps_json_obj_get(obj, "offset"));
  ins.len = dup_json_string(ps_json_obj_get(obj, "len"));
  ins.mode = dup_json_string(ps_json_obj_get(obj, "mode"));
  ins.callee = dup_json_string(ps_json_obj_get(obj, "callee"));
  ins.receiver = dup_json_string(ps_json_obj_get(obj, "receiver"));
  ins.divisor = dup_json_string(ps_json_obj_get(obj, "divisor"));
  ins.map = dup_json_string(ps_json_obj_get(obj, "map"));
  ins.key = dup_json_string(ps_json_obj_get(obj, "key"));
  ins.thenValue = dup_json_string(ps_json_obj_get(obj, "thenValue"));
  ins.elseValue = dup_json_string(ps_json_obj_get(obj, "elseValue"));
  ins.shift = dup_json_string(ps_json_obj_get(obj, "shift"));
  PS_JsonValue *w = ps_json_obj_get(obj, "width");
  if (w && w->type == PS_JSON_NUMBER) ins.width = (int)w->as.num_v;
  ins.method = dup_json_string(ps_json_obj_get(obj, "method"));
  ins.proto = dup_json_string(ps_json_obj_get(obj, "proto"));
  PS_JsonValue *ro = ps_json_obj_get(obj, "readonly");
  if (ro && ro->type == PS_JSON_BOOL) ins.readonly = ro->as.bool_v ? 1 : 0;
  PS_JsonValue *args = ps_json_obj_get(obj, "args");
  if (args && args->type == PS_JSON_ARRAY) {
    ins.arg_count = args->as.array_v.len;
    ins.args = (char **)calloc(ins.arg_count, sizeof(char *));
    for (size_t i = 0; i < ins.arg_count; i++) ins.args[i] = dup_json_string(args->as.array_v.items[i]);
  }
  PS_JsonValue *pairs = ps_json_obj_get(obj, "pairs");
  if (pairs && pairs->type == PS_JSON_ARRAY) {
    ins.pair_count = pairs->as.array_v.len;
    ins.pairs = (void *)calloc(ins.pair_count, sizeof(*ins.pairs));
    for (size_t i = 0; i < ins.pair_count; i++) {
      PS_JsonValue *p = pairs->as.array_v.items[i];
      ins.pairs[i].key = dup_json_string(ps_json_obj_get(p, "key"));
      ins.pairs[i].value = dup_json_string(ps_json_obj_get(p, "value"));
    }
  }
  return ins;
}

static void free_instr(IRInstr *i) {
  free(i->op);
  free(i->dst);
  free(i->name);
  free(i->type);
  free(i->value);
  free(i->literalType);
  free(i->left);
  free(i->right);
  free(i->operator);
  free(i->cond);
  free(i->then_label);
  free(i->else_label);
  free(i->target);
  free(i->index);
  free(i->src);
  free(i->kind);
  free(i->iter);
  free(i->source);
  free(i->offset);
  free(i->len);
  free(i->mode);
  free(i->callee);
  free(i->receiver);
  free(i->divisor);
  free(i->map);
  free(i->key);
  free(i->thenValue);
  free(i->elseValue);
  free(i->shift);
  free(i->method);
  free(i->proto);
  if (i->args) {
    for (size_t j = 0; j < i->arg_count; j++) free(i->args[j]);
  }
  free(i->args);
  if (i->pairs) {
    for (size_t j = 0; j < i->pair_count; j++) {
      free(i->pairs[j].key);
      free(i->pairs[j].value);
    }
  }
  free(i->pairs);
}

PS_IR_Module *ps_ir_load_json(PS_Context *ctx, const char *json, size_t len) {
  (void)ctx;
  const char *err = NULL;
  PS_JsonValue *root = ps_json_parse(json, len, &err);
  if (!root) {
    ps_throw(ctx, PS_ERR_INTERNAL, err ? err : "invalid IR json");
    return NULL;
  }
  PS_JsonValue *module = ps_json_obj_get(root, "module");
  PS_JsonValue *functions = module ? ps_json_obj_get(module, "functions") : NULL;
  if (!functions || functions->type != PS_JSON_ARRAY) {
    ps_json_free(root);
    ps_throw(ctx, PS_ERR_INTERNAL, "invalid IR json");
    return NULL;
  }
  PS_IR_Module *m = (PS_IR_Module *)calloc(1, sizeof(PS_IR_Module));
  if (!m) {
    ps_json_free(root);
    ps_throw(ctx, PS_ERR_OOM, "out of memory");
    return NULL;
  }
  m->fn_count = functions->as.array_v.len;
  m->fns = (IRFunction *)calloc(m->fn_count, sizeof(IRFunction));
  if (!m->fns) {
    free(m);
    ps_json_free(root);
    ps_throw(ctx, PS_ERR_OOM, "out of memory");
    return NULL;
  }
  for (size_t fi = 0; fi < m->fn_count; fi++) {
    PS_JsonValue *f = functions->as.array_v.items[fi];
    m->fns[fi].name = dup_json_string(ps_json_obj_get(f, "name"));
    PS_JsonValue *params = ps_json_obj_get(f, "params");
    if (params && params->type == PS_JSON_ARRAY) {
      m->fns[fi].param_count = params->as.array_v.len;
      m->fns[fi].params = (char **)calloc(m->fns[fi].param_count, sizeof(char *));
      for (size_t i = 0; i < m->fns[fi].param_count; i++) {
        PS_JsonValue *p = params->as.array_v.items[i];
        PS_JsonValue *pn = ps_json_obj_get(p, "name");
        m->fns[fi].params[i] = dup_json_string(pn);
      }
    }
    PS_JsonValue *ret = ps_json_obj_get(f, "returnType");
    m->fns[fi].ret_type = dup_json_string(ret ? ps_json_obj_get(ret, "name") : NULL);
    PS_JsonValue *blocks = ps_json_obj_get(f, "blocks");
    if (blocks && blocks->type == PS_JSON_ARRAY) {
      m->fns[fi].block_count = blocks->as.array_v.len;
      m->fns[fi].blocks = (IRBlock *)calloc(m->fns[fi].block_count, sizeof(IRBlock));
      for (size_t bi = 0; bi < m->fns[fi].block_count; bi++) {
        PS_JsonValue *b = blocks->as.array_v.items[bi];
        m->fns[fi].blocks[bi].label = dup_json_string(ps_json_obj_get(b, "label"));
        PS_JsonValue *instrs = ps_json_obj_get(b, "instrs");
        if (instrs && instrs->type == PS_JSON_ARRAY) {
          m->fns[fi].blocks[bi].instr_count = instrs->as.array_v.len;
          m->fns[fi].blocks[bi].instrs = (IRInstr *)calloc(m->fns[fi].blocks[bi].instr_count, sizeof(IRInstr));
          for (size_t ii = 0; ii < m->fns[fi].blocks[bi].instr_count; ii++) {
            m->fns[fi].blocks[bi].instrs[ii] = parse_instr(instrs->as.array_v.items[ii]);
          }
        }
      }
    }
  }
  ps_json_free(root);
  return m;
}

void ps_ir_free(PS_IR_Module *m) {
  if (!m) return;
  for (size_t fi = 0; fi < m->fn_count; fi++) {
    IRFunction *f = &m->fns[fi];
    free(f->name);
    if (f->params) {
      for (size_t i = 0; i < f->param_count; i++) free(f->params[i]);
      free(f->params);
    }
    free(f->ret_type);
    if (f->blocks) {
      for (size_t bi = 0; bi < f->block_count; bi++) {
        IRBlock *b = &f->blocks[bi];
        free(b->label);
        for (size_t ii = 0; ii < b->instr_count; ii++) free_instr(&b->instrs[ii]);
        free(b->instrs);
      }
      free(f->blocks);
    }
  }
  free(m->fns);
  free(m);
}

static IRFunction *find_fn(PS_IR_Module *m, const char *name) {
  for (size_t i = 0; i < m->fn_count; i++) {
    if (m->fns[i].name && strcmp(m->fns[i].name, name) == 0) return &m->fns[i];
  }
  return NULL;
}

static size_t find_block(IRFunction *f, const char *label) {
  for (size_t i = 0; i < f->block_count; i++) {
    if (f->blocks[i].label && strcmp(f->blocks[i].label, label) == 0) return i;
  }
  return 0;
}

static PS_Value *value_from_literal(PS_Context *ctx, const char *literalType, PS_JsonValue *json_value, const char *raw) {
  if (!literalType) return NULL;
  if (strcmp(literalType, "bool") == 0) {
    int v = 0;
    if (json_value && json_value->type == PS_JSON_BOOL) v = json_value->as.bool_v;
    return ps_make_bool(ctx, v);
  }
  if (strcmp(literalType, "int") == 0 || strcmp(literalType, "byte") == 0) {
    int64_t v = 0;
    if (raw) v = strtoll(raw, NULL, 0);
    if (strcmp(literalType, "byte") == 0) return ps_make_byte(ctx, (uint8_t)v);
    return ps_make_int(ctx, v);
  }
  if (strcmp(literalType, "float") == 0) {
    double v = 0.0;
    if (raw) v = strtod(raw, NULL);
    return ps_make_float(ctx, v);
  }
  if (strcmp(literalType, "string") == 0) {
    return ps_make_string_utf8(ctx, raw ? raw : "", raw ? strlen(raw) : 0);
  }
  if (strcmp(literalType, "eof") == 0) {
    if (!ctx->eof_value) {
      PS_Value *v = ps_value_alloc(PS_V_OBJECT);
      if (!v) {
        ps_throw(ctx, PS_ERR_OOM, "out of memory");
        return NULL;
      }
      ctx->eof_value = v;
    }
    return ps_value_retain(ctx->eof_value);
  }
  if (strcmp(literalType, "file") == 0) {
    const char *name = raw ? raw : "";
    if (strcmp(name, "stdin") == 0) {
      if (!ctx->stdin_value) ctx->stdin_value = ps_make_file(ctx, stdin, PS_FILE_READ | PS_FILE_STD);
      return ctx->stdin_value ? ps_value_retain(ctx->stdin_value) : NULL;
    }
    if (strcmp(name, "stdout") == 0) {
      if (!ctx->stdout_value) ctx->stdout_value = ps_make_file(ctx, stdout, PS_FILE_WRITE | PS_FILE_STD);
      return ctx->stdout_value ? ps_value_retain(ctx->stdout_value) : NULL;
    }
    if (strcmp(name, "stderr") == 0) {
      if (!ctx->stderr_value) ctx->stderr_value = ps_make_file(ctx, stderr, PS_FILE_WRITE | PS_FILE_STD);
      return ctx->stderr_value ? ps_value_retain(ctx->stderr_value) : NULL;
    }
    ps_throw(ctx, PS_ERR_RANGE, "invalid file constant");
    return NULL;
  }
  return NULL;
}

static PS_Value *get_value(PS_Bindings *temps, PS_Bindings *vars, const char *name) {
  if (!name) return NULL;
  PS_Value *v = bindings_get(temps, name);
  if (v) return v;
  return bindings_get(vars, name);
}

static int values_equal(PS_Value *a, PS_Value *b) {
  if (!a || !b) return a == b;
  if (a->tag != b->tag) return 0;
  switch (a->tag) {
    case PS_V_BOOL:
      return a->as.bool_v == b->as.bool_v;
    case PS_V_INT:
      return a->as.int_v == b->as.int_v;
    case PS_V_FLOAT:
      return a->as.float_v == b->as.float_v;
    case PS_V_BYTE:
      return a->as.byte_v == b->as.byte_v;
    case PS_V_GLYPH:
      return a->as.glyph_v == b->as.glyph_v;
    case PS_V_STRING:
      if (a->as.string_v.len != b->as.string_v.len) return 0;
      return memcmp(a->as.string_v.ptr, b->as.string_v.ptr, a->as.string_v.len) == 0;
    default:
      return a == b;
  }
}

static int compare_values(PS_Value *a, PS_Value *b, PS_ValueTag tag, int *out_cmp) {
  if (!a || !b || a->tag != tag || b->tag != tag) return 0;
  int cmp = 0;
  switch (tag) {
    case PS_V_INT:
      cmp = (a->as.int_v < b->as.int_v) ? -1 : (a->as.int_v > b->as.int_v) ? 1 : 0;
      break;
    case PS_V_FLOAT:
      if (isnan(a->as.float_v) || isnan(b->as.float_v)) cmp = 0;
      else cmp = (a->as.float_v < b->as.float_v) ? -1 : (a->as.float_v > b->as.float_v) ? 1 : 0;
      break;
    case PS_V_BYTE:
      cmp = (a->as.byte_v < b->as.byte_v) ? -1 : (a->as.byte_v > b->as.byte_v) ? 1 : 0;
      break;
    case PS_V_GLYPH:
      cmp = (a->as.glyph_v < b->as.glyph_v) ? -1 : (a->as.glyph_v > b->as.glyph_v) ? 1 : 0;
      break;
    case PS_V_BOOL:
      cmp = (a->as.bool_v == b->as.bool_v) ? 0 : (a->as.bool_v ? 1 : -1);
      break;
    case PS_V_STRING: {
      size_t al = a->as.string_v.len;
      size_t bl = b->as.string_v.len;
      size_t ml = (al < bl) ? al : bl;
      int r = memcmp(a->as.string_v.ptr, b->as.string_v.ptr, ml);
      if (r < 0) cmp = -1;
      else if (r > 0) cmp = 1;
      else cmp = (al < bl) ? -1 : (al > bl) ? 1 : 0;
      break;
    }
    default:
      return 0;
  }
  if (out_cmp) *out_cmp = cmp;
  return 1;
}

static PS_Value *make_exception(PS_Context *ctx, int kind, const char *file, int64_t line, int64_t column,
                                const char *message, PS_Value *cause, const char *code, const char *category) {
  PS_Value *ex = ps_value_alloc(PS_V_EXCEPTION);
  if (!ex) {
    ps_throw(ctx, PS_ERR_OOM, "out of memory");
    return NULL;
  }
  ex->as.exc_v.kind = kind;
  ex->as.exc_v.file = ps_make_string_utf8(ctx, file ? file : "", file ? strlen(file) : 0);
  ex->as.exc_v.line = line;
  ex->as.exc_v.column = column;
  ex->as.exc_v.message = ps_make_string_utf8(ctx, message ? message : "", message ? strlen(message) : 0);
  ex->as.exc_v.cause = cause ? ps_value_retain(cause) : NULL;
  ex->as.exc_v.code = code ? ps_make_string_utf8(ctx, code, strlen(code)) : NULL;
  ex->as.exc_v.category = category ? ps_make_string_utf8(ctx, category, strlen(category)) : NULL;
  if (!ex->as.exc_v.file || !ex->as.exc_v.message || (code && !ex->as.exc_v.code) || (category && !ex->as.exc_v.category)) {
    ps_value_release(ex);
    return NULL;
  }
  return ex;
}

static PS_Value *make_runtime_exception_from_error(PS_Context *ctx) {
  const char *code = NULL;
  const char *category = ps_runtime_category(ps_last_error_code(ctx), ps_last_error_message(ctx), &code);
  return make_exception(ctx, 1, "", 1, 1, ps_last_error_message(ctx), NULL, code, category);
}

static int exception_matches(PS_Value *v, const char *type_name) {
  if (!v || v->tag != PS_V_EXCEPTION || !type_name) return 0;
  if (strcmp(type_name, "Exception") == 0) return 1;
  if (strcmp(type_name, "RuntimeException") == 0) return v->as.exc_v.kind == 1;
  return 0;
}

static PS_Value *exception_get_field(PS_Context *ctx, PS_Value *v, const char *name) {
  if (!v || v->tag != PS_V_EXCEPTION || !name) {
    ps_throw(ctx, PS_ERR_TYPE, "not an exception");
    return NULL;
  }
  if (strcmp(name, "file") == 0) return v->as.exc_v.file ? ps_value_retain(v->as.exc_v.file) : ps_make_string_utf8(ctx, "", 0);
  if (strcmp(name, "line") == 0) return ps_make_int(ctx, v->as.exc_v.line);
  if (strcmp(name, "column") == 0) return ps_make_int(ctx, v->as.exc_v.column);
  if (strcmp(name, "message") == 0) return v->as.exc_v.message ? ps_value_retain(v->as.exc_v.message) : ps_make_string_utf8(ctx, "", 0);
  if (strcmp(name, "cause") == 0) return v->as.exc_v.cause ? ps_value_retain(v->as.exc_v.cause) : NULL;
  if (strcmp(name, "code") == 0) return v->as.exc_v.code ? ps_value_retain(v->as.exc_v.code) : ps_make_string_utf8(ctx, "", 0);
  if (strcmp(name, "category") == 0) return v->as.exc_v.category ? ps_value_retain(v->as.exc_v.category) : ps_make_string_utf8(ctx, "", 0);
  return NULL;
}

static int is_truthy(PS_Value *v) {
  if (!v) return 0;
  if (v->tag == PS_V_BOOL) return v->as.bool_v != 0;
  if (v->tag == PS_V_INT) return v->as.int_v != 0;
  return 0;
}

static PS_Value *map_first_key(PS_Value *map) {
  if (!map || map->tag != PS_V_MAP) return NULL;
  PS_Map *m = &map->as.map_v;
  if (m->len == 0 || m->cap == 0) return NULL;
  for (size_t i = 0; i < m->cap; i++) {
    if (m->used[i]) return m->keys[i];
  }
  return NULL;
}

static int exec_function(PS_Context *ctx, PS_IR_Module *m, IRFunction *f, PS_Value **args, size_t argc, PS_Value **out);

static int exec_call_static(PS_Context *ctx, PS_IR_Module *m, const char *callee, PS_Value **args, size_t argc, PS_Value **out) {
  if (strcmp(callee, "Exception") == 0) {
    if (argc > 2) {
      ps_throw(ctx, PS_ERR_TYPE, "Exception expects (string message, Exception cause?)");
      return 1;
    }
    const char *msg = "";
    PS_Value *cause = NULL;
    if (argc >= 1) {
      if (!args[0] || args[0]->tag != PS_V_STRING) {
        ps_throw(ctx, PS_ERR_TYPE, "Exception message must be string");
        return 1;
      }
      msg = args[0]->as.string_v.ptr ? args[0]->as.string_v.ptr : "";
    }
    if (argc == 2) {
      if (args[1] && args[1]->tag != PS_V_EXCEPTION) {
        ps_throw(ctx, PS_ERR_TYPE, "Exception cause must be Exception");
        return 1;
      }
      cause = args[1];
    }
    PS_Value *ex = make_exception(ctx, 0, "", 1, 1, msg, cause, NULL, NULL);
    if (!ex) return 1;
    *out = ex;
    return 0;
  }
  if (strcmp(callee, "RuntimeException") == 0) {
    ps_throw(ctx, PS_ERR_TYPE, "RuntimeException is not constructible");
    return 1;
  }
  IRFunction *fn = find_fn(m, callee);
  if (fn) return exec_function(ctx, m, fn, args, argc, out);
  // Module call: "module.symbol"
  const char *dot = strrchr(callee, '.');
  if (dot) {
    char module[128];
    char symbol[128];
    size_t mlen = (size_t)(dot - callee);
    if (mlen < sizeof(module) && strlen(dot + 1) < sizeof(symbol)) {
      memcpy(module, callee, mlen);
      module[mlen] = '\0';
      strncpy(symbol, dot + 1, sizeof(symbol) - 1);
      symbol[sizeof(symbol) - 1] = '\0';
      const PS_NativeFnDesc *desc = ps_module_find_fn(ctx, module, symbol);
      if (!desc) return 1;
      PS_Value *ret = NULL;
      PS_Status st = desc->fn(ctx, (int)argc, args, &ret);
      if (st != PS_OK) return 1;
      *out = ret;
      return 0;
    }
  }
  ps_throw(ctx, PS_ERR_IMPORT, "unknown function");
  return 1;
}

static int exec_function(PS_Context *ctx, PS_IR_Module *m, IRFunction *f, PS_Value **args, size_t argc, PS_Value **out) {
  PS_Bindings vars = {0};
  PS_Bindings temps = {0};
  typedef struct {
    const char *handler;
  } TryFrame;
  TryFrame *tries = NULL;
  size_t try_len = 0;
  size_t try_cap = 0;
  PS_Value *last_exception = NULL;
  for (size_t i = 0; i < f->param_count && i < argc; i++) {
    bindings_set(&vars, f->params[i], args[i]);
  }
  size_t block_idx = 0;
  size_t ip = 0;
  while (block_idx < f->block_count) {
    IRBlock *b = &f->blocks[block_idx];
    for (ip = 0; ip < b->instr_count; ip++) {
      IRInstr *ins = &b->instrs[ip];
      if (!ins->op) continue;
      if (ctx->trace) fprintf(stderr, "[trace] %s\n", ins->op);
      if (ctx->trace_ir) fprintf(stderr, "[ir] %s\n", ins->op);
      if (strcmp(ins->op, "nop") == 0) continue;
      if (strcmp(ins->op, "var_decl") == 0) {
        bindings_set(&vars, ins->name, NULL);
        continue;
      }
      if (strcmp(ins->op, "const") == 0) {
        PS_Value *v = value_from_literal(ctx, ins->literalType, NULL, ins->value);
        if (!v) goto raise;
        bindings_set(&temps, ins->dst, v);
        ps_value_release(v);
        continue;
      }
      if (strcmp(ins->op, "push_handler") == 0) {
        if (try_len == try_cap) {
          size_t nc = try_cap == 0 ? 4 : try_cap * 2;
          TryFrame *nt = (TryFrame *)realloc(tries, sizeof(TryFrame) * nc);
          if (!nt) {
            ps_throw(ctx, PS_ERR_OOM, "out of memory");
            goto raise;
          }
          tries = nt;
          try_cap = nc;
        }
        tries[try_len++].handler = ins->target;
        continue;
      }
      if (strcmp(ins->op, "pop_handler") == 0) {
        if (try_len > 0) try_len -= 1;
        continue;
      }
      if (strcmp(ins->op, "get_exception") == 0) {
        if (!last_exception) {
          last_exception = make_runtime_exception_from_error(ctx);
          if (!last_exception) goto raise;
        }
        bindings_set(&temps, ins->dst, last_exception);
        continue;
      }
      if (strcmp(ins->op, "rethrow") == 0) {
        if (!last_exception) {
          ps_throw(ctx, PS_ERR_INTERNAL, "rethrow with no exception");
        }
        goto raise;
      }
      if (strcmp(ins->op, "exception_is") == 0) {
        PS_Value *v = get_value(&temps, &vars, ins->value);
        int ok = exception_matches(v, ins->type);
        PS_Value *b = ps_make_bool(ctx, ok);
        if (!b) goto raise;
        bindings_set(&temps, ins->dst, b);
        ps_value_release(b);
        continue;
      }
      if (strcmp(ins->op, "load_var") == 0) {
        PS_Value *v = bindings_get(&vars, ins->name);
        bindings_set(&temps, ins->dst, v);
        continue;
      }
      if (strcmp(ins->op, "store_var") == 0) {
        PS_Value *v = get_value(&temps, &vars, ins->src);
        bindings_set(&vars, ins->name, v);
        continue;
      }
      if (strcmp(ins->op, "copy") == 0) {
        PS_Value *v = get_value(&temps, &vars, ins->src);
        bindings_set(&temps, ins->dst, v);
        continue;
      }
      if (strcmp(ins->op, "member_get") == 0) {
        PS_Value *recv = get_value(&temps, &vars, ins->target);
        if (recv && recv->tag == PS_V_EXCEPTION) {
          PS_Value *field = exception_get_field(ctx, recv, ins->name);
          if (!field && ps_last_error_code(ctx) != PS_ERR_NONE) goto raise;
          bindings_set(&temps, ins->dst, field);
          if (field) ps_value_release(field);
          continue;
        }
        if (recv && recv->tag == PS_V_OBJECT) {
          PS_Value *field = ps_object_get_str_internal(ctx, recv, ins->name ? ins->name : "", ins->name ? strlen(ins->name) : 0);
          bindings_set(&temps, ins->dst, field);
          continue;
        }
        ps_throw(ctx, PS_ERR_TYPE, "member access on non-object");
        goto raise;
      }
      if (strcmp(ins->op, "member_set") == 0) {
        PS_Value *recv = get_value(&temps, &vars, ins->target);
        PS_Value *val = get_value(&temps, &vars, ins->src);
        if (recv && recv->tag == PS_V_OBJECT) {
          if (!ps_object_set_str_internal(ctx, recv, ins->name ? ins->name : "", ins->name ? strlen(ins->name) : 0, val)) {
            goto raise;
          }
          continue;
        }
        ps_throw(ctx, PS_ERR_TYPE, "member assignment on non-object");
        goto raise;
      }
      if (strcmp(ins->op, "make_object") == 0) {
        PS_Value *obj = ps_object_new(ctx);
        if (!obj) goto raise;
        bindings_set(&temps, ins->dst, obj);
        ps_value_release(obj);
        continue;
      }
      if (strcmp(ins->op, "check_div_zero") == 0) {
        PS_Value *v = get_value(&temps, &vars, ins->divisor);
        if (v && v->tag == PS_V_INT && v->as.int_v == 0) {
          ps_throw(ctx, PS_ERR_RANGE, "division by zero");
          goto raise;
        }
        continue;
      }
      if (strcmp(ins->op, "check_int_overflow_unary_minus") == 0) {
        PS_Value *v = get_value(&temps, &vars, ins->value);
        if (v && v->tag == PS_V_INT && v->as.int_v == INT64_MIN) {
          ps_throw(ctx, PS_ERR_RANGE, "int overflow");
          goto raise;
        }
        continue;
      }
      if (strcmp(ins->op, "check_int_overflow") == 0) {
        PS_Value *l = get_value(&temps, &vars, ins->left);
        PS_Value *r = get_value(&temps, &vars, ins->right);
        if (!l || !r) goto raise;
        int64_t a = l->as.int_v;
        int64_t b = r->as.int_v;
        if (strcmp(ins->operator, "+") == 0) {
          if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) {
            ps_throw(ctx, PS_ERR_RANGE, "int overflow");
            goto raise;
          }
        } else if (strcmp(ins->operator, "-") == 0) {
          if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b)) {
            ps_throw(ctx, PS_ERR_RANGE, "int overflow");
            goto raise;
          }
        } else if (strcmp(ins->operator, "*") == 0) {
          if (a != 0 && b != 0) {
            if (a == -1 && b == INT64_MIN) {
              ps_throw(ctx, PS_ERR_RANGE, "int overflow");
              goto raise;
            }
            if (b == -1 && a == INT64_MIN) {
              ps_throw(ctx, PS_ERR_RANGE, "int overflow");
              goto raise;
            }
            if (llabs(a) > INT64_MAX / llabs(b)) {
              ps_throw(ctx, PS_ERR_RANGE, "int overflow");
              goto raise;
            }
          }
        }
        continue;
      }
      if (strcmp(ins->op, "check_shift_range") == 0) {
        PS_Value *s = get_value(&temps, &vars, ins->shift);
        int64_t sh = s ? s->as.int_v : 0;
        if (sh < 0 || sh >= (int64_t)ins->width) {
          ps_throw(ctx, PS_ERR_RANGE, "invalid shift");
          goto raise;
        }
        continue;
      }
      if (strcmp(ins->op, "check_index_bounds") == 0) {
        PS_Value *t = get_value(&temps, &vars, ins->target);
        PS_Value *i = get_value(&temps, &vars, ins->index);
        if (!t || !i) goto raise;
        size_t idx = (size_t)i->as.int_v;
        size_t len = 0;
        if (t->tag == PS_V_LIST) len = t->as.list_v.len;
        else if (t->tag == PS_V_STRING) len = ps_utf8_glyph_len((const uint8_t *)t->as.string_v.ptr, t->as.string_v.len);
        else if (t->tag == PS_V_VIEW) len = t->as.view_v.len;
        if (idx >= len) {
          ps_throw(ctx, PS_ERR_RANGE, "index out of bounds");
          goto raise;
        }
        continue;
      }
      if (strcmp(ins->op, "check_view_bounds") == 0) {
        PS_Value *t = get_value(&temps, &vars, ins->target);
        PS_Value *o = get_value(&temps, &vars, ins->offset);
        PS_Value *l = get_value(&temps, &vars, ins->len);
        if (!t || !o || !l) goto raise;
        int64_t off = o->as.int_v;
        int64_t ln = l->as.int_v;
        if (off < 0 || ln < 0) {
          ps_throw(ctx, PS_ERR_RANGE, "index out of bounds");
          goto raise;
        }
        size_t total = 0;
        if (t->tag == PS_V_LIST) total = t->as.list_v.len;
        else if (t->tag == PS_V_STRING) total = ps_utf8_glyph_len((const uint8_t *)t->as.string_v.ptr, t->as.string_v.len);
        else if (t->tag == PS_V_VIEW) total = t->as.view_v.len;
        if ((uint64_t)off + (uint64_t)ln > total) {
          ps_throw(ctx, PS_ERR_RANGE, "index out of bounds");
          goto raise;
        }
        continue;
      }
      if (strcmp(ins->op, "check_map_has_key") == 0) {
        PS_Value *mval = get_value(&temps, &vars, ins->map);
        PS_Value *k = get_value(&temps, &vars, ins->key);
        if (!ps_map_has_key(ctx, mval, k)) {
          ps_throw(ctx, PS_ERR_RANGE, "missing key");
          goto raise;
        }
        continue;
      }
      if (strcmp(ins->op, "bin_op") == 0) {
        PS_Value *l = get_value(&temps, &vars, ins->left);
        PS_Value *r = get_value(&temps, &vars, ins->right);
        if (!l || !r) goto raise;
        PS_Value *res = NULL;
        if (strcmp(ins->operator, "+") == 0) res = ps_make_int(ctx, l->as.int_v + r->as.int_v);
        else if (strcmp(ins->operator, "-") == 0) res = ps_make_int(ctx, l->as.int_v - r->as.int_v);
        else if (strcmp(ins->operator, "*") == 0) res = ps_make_int(ctx, l->as.int_v * r->as.int_v);
        else if (strcmp(ins->operator, "/") == 0) res = ps_make_int(ctx, l->as.int_v / r->as.int_v);
        else if (strcmp(ins->operator, "==") == 0) res = ps_make_bool(ctx, values_equal(l, r));
        else if (strcmp(ins->operator, "!=") == 0) res = ps_make_bool(ctx, !values_equal(l, r));
        else if (strcmp(ins->operator, "<") == 0) res = ps_make_bool(ctx, l->as.int_v < r->as.int_v);
        else if (strcmp(ins->operator, "<=") == 0) res = ps_make_bool(ctx, l->as.int_v <= r->as.int_v);
        else if (strcmp(ins->operator, ">") == 0) res = ps_make_bool(ctx, l->as.int_v > r->as.int_v);
        else if (strcmp(ins->operator, ">=") == 0) res = ps_make_bool(ctx, l->as.int_v >= r->as.int_v);
        else if (strcmp(ins->operator, "&&") == 0) res = ps_make_bool(ctx, is_truthy(l) && is_truthy(r));
        else if (strcmp(ins->operator, "||") == 0) res = ps_make_bool(ctx, is_truthy(l) || is_truthy(r));
        if (!res) goto raise;
        bindings_set(&temps, ins->dst, res);
        ps_value_release(res);
        continue;
      }
      if (strcmp(ins->op, "unary_op") == 0) {
        PS_Value *v = get_value(&temps, &vars, ins->src);
        PS_Value *res = NULL;
        if (strcmp(ins->operator, "!") == 0) res = ps_make_bool(ctx, !is_truthy(v));
        else if (strcmp(ins->operator, "-") == 0) res = ps_make_int(ctx, -v->as.int_v);
        if (!res) goto raise;
        bindings_set(&temps, ins->dst, res);
        ps_value_release(res);
        continue;
      }
      if (strcmp(ins->op, "select") == 0) {
        PS_Value *c = get_value(&temps, &vars, ins->cond);
        PS_Value *tv = get_value(&temps, &vars, ins->thenValue);
        PS_Value *ev = get_value(&temps, &vars, ins->elseValue);
        bindings_set(&temps, ins->dst, is_truthy(c) ? tv : ev);
        continue;
      }
      if (strcmp(ins->op, "make_list") == 0) {
        PS_Value *list = ps_list_new(ctx);
        for (size_t i = 0; i < ins->arg_count; i++) {
          PS_Value *it = get_value(&temps, &vars, ins->args[i]);
          ps_list_push_internal(ctx, list, it);
        }
        bindings_set(&temps, ins->dst, list);
        ps_value_release(list);
        continue;
      }
      if (strcmp(ins->op, "make_map") == 0) {
        PS_Value *map = ps_map_new(ctx);
        for (size_t i = 0; i < ins->pair_count; i++) {
          PS_Value *k = get_value(&temps, &vars, ins->pairs[i].key);
          PS_Value *v = get_value(&temps, &vars, ins->pairs[i].value);
          ps_map_set(ctx, map, k, v);
        }
        bindings_set(&temps, ins->dst, map);
        ps_value_release(map);
        continue;
      }
      if (strcmp(ins->op, "make_view") == 0) {
        PS_Value *src = get_value(&temps, &vars, ins->source);
        PS_Value *o = get_value(&temps, &vars, ins->offset);
        PS_Value *l = get_value(&temps, &vars, ins->len);
        if (!src || !o || !l) goto raise;
        int64_t off = o->as.int_v;
        int64_t ln = l->as.int_v;
        if (off < 0 || ln < 0) {
          ps_throw(ctx, PS_ERR_RANGE, "index out of bounds");
          goto raise;
        }
        PS_Value *base = src;
        size_t base_off = 0;
        int readonly = ins->readonly;
        if (src->tag == PS_V_VIEW) {
          base = src->as.view_v.source;
          base_off = src->as.view_v.offset;
          if (src->as.view_v.readonly) readonly = 1;
        }
        PS_Value *v = ps_value_alloc(PS_V_VIEW);
        if (!v) goto raise;
        v->as.view_v.source = ps_value_retain(base);
        v->as.view_v.offset = base_off + (size_t)off;
        v->as.view_v.len = (size_t)ln;
        v->as.view_v.readonly = readonly;
        if (base && base->tag == PS_V_LIST) v->as.view_v.version = base->as.list_v.version;
        else v->as.view_v.version = 0;
        bindings_set(&temps, ins->dst, v);
        ps_value_release(v);
        continue;
      }
      if (strcmp(ins->op, "index_get") == 0) {
        PS_Value *t = get_value(&temps, &vars, ins->target);
        PS_Value *i = get_value(&temps, &vars, ins->index);
        if (!t || !i) goto raise;
        PS_Value *res = NULL;
        if (t->tag == PS_V_LIST) res = ps_list_get_internal(ctx, t, (size_t)i->as.int_v);
        else if (t->tag == PS_V_STRING) {
          uint32_t g = ps_utf8_glyph_at((const uint8_t *)t->as.string_v.ptr, t->as.string_v.len, (size_t)i->as.int_v);
          res = ps_make_glyph(ctx, g);
        } else if (t->tag == PS_V_MAP) res = ps_map_get(ctx, t, i);
        else if (t->tag == PS_V_VIEW) {
          if (!view_is_valid(t)) {
            ps_throw(ctx, PS_ERR_RANGE, "view invalidated");
            goto raise;
          }
          size_t idx = t->as.view_v.offset + (size_t)i->as.int_v;
          PS_Value *src = t->as.view_v.source;
          if (src->tag == PS_V_LIST) res = ps_list_get_internal(ctx, src, idx);
          else if (src->tag == PS_V_STRING) {
            uint32_t g = ps_utf8_glyph_at((const uint8_t *)src->as.string_v.ptr, src->as.string_v.len, idx);
            res = ps_make_glyph(ctx, g);
          }
        }
        if (!res) goto raise;
        bindings_set(&temps, ins->dst, res);
        continue;
      }
      if (strcmp(ins->op, "index_set") == 0) {
        PS_Value *t = get_value(&temps, &vars, ins->target);
        PS_Value *i = get_value(&temps, &vars, ins->index);
        PS_Value *v = get_value(&temps, &vars, ins->src);
        if (t->tag == PS_V_LIST) {
          if (!ps_list_set_internal(ctx, t, (size_t)i->as.int_v, v)) goto raise;
        } else if (t->tag == PS_V_MAP) {
          if (!ps_map_set(ctx, t, i, v)) goto raise;
        } else if (t->tag == PS_V_VIEW) {
          if (!view_is_valid(t)) {
            ps_throw(ctx, PS_ERR_RANGE, "view invalidated");
            goto raise;
          }
          if (t->as.view_v.readonly) {
            ps_throw(ctx, PS_ERR_TYPE, "cannot assign through view");
            goto raise;
          }
          size_t idx = t->as.view_v.offset + (size_t)i->as.int_v;
          PS_Value *src = t->as.view_v.source;
          if (src->tag == PS_V_LIST) {
            if (!ps_list_set_internal(ctx, src, idx, v)) goto raise;
          } else {
            ps_throw(ctx, PS_ERR_TYPE, "invalid view target");
            goto raise;
          }
        }
        continue;
      }
      if (strcmp(ins->op, "iter_begin") == 0) {
        PS_Value *src = get_value(&temps, &vars, ins->source);
        PS_Value *it = ps_value_alloc(PS_V_ITER);
        if (!it) goto raise;
        it->as.iter_v.source = ps_value_retain(src);
        it->as.iter_v.index = 0;
        it->as.iter_v.mode = (ins->mode && strcmp(ins->mode, "in") == 0) ? 1 : 0;
        bindings_set(&temps, ins->dst, it);
        ps_value_release(it);
        continue;
      }
      if (strcmp(ins->op, "branch_iter_has_next") == 0) {
        PS_Value *it = get_value(&temps, &vars, ins->iter);
        size_t has = 0;
        if (it && it->tag == PS_V_ITER) {
          PS_Value *src = it->as.iter_v.source;
          if (src->tag == PS_V_LIST) has = it->as.iter_v.index < src->as.list_v.len;
          else if (src->tag == PS_V_MAP) has = it->as.iter_v.index < src->as.map_v.len;
          else if (src->tag == PS_V_STRING) {
            size_t gl = ps_utf8_glyph_len((const uint8_t *)src->as.string_v.ptr, src->as.string_v.len);
            has = it->as.iter_v.index < gl;
          } else if (src->tag == PS_V_VIEW) {
            if (!view_is_valid(src)) {
              ps_throw(ctx, PS_ERR_RANGE, "view invalidated");
              goto raise;
            }
            has = it->as.iter_v.index < src->as.view_v.len;
          }
        }
        block_idx = find_block(f, has ? ins->then_label : ins->else_label);
        goto next_block;
      }
      if (strcmp(ins->op, "iter_next") == 0) {
        PS_Value *it = get_value(&temps, &vars, ins->iter);
        PS_Value *res = NULL;
        if (it && it->tag == PS_V_ITER) {
          PS_Value *src = it->as.iter_v.source;
          size_t idx = it->as.iter_v.index++;
          if (src->tag == PS_V_LIST) res = src->as.list_v.items[idx];
          else if (src->tag == PS_V_STRING) {
            uint32_t g = ps_utf8_glyph_at((const uint8_t *)src->as.string_v.ptr, src->as.string_v.len, idx);
            res = ps_make_glyph(ctx, g);
          } else if (src->tag == PS_V_MAP) {
            if (idx < src->as.map_v.cap) {
              size_t count = 0;
              for (size_t i = 0; i < src->as.map_v.cap; i++) {
                if (!src->as.map_v.used[i]) continue;
                if (count == idx) {
                  res = it->as.iter_v.mode ? src->as.map_v.keys[i] : src->as.map_v.values[i];
                  break;
                }
                count++;
              }
            }
          } else if (src->tag == PS_V_VIEW) {
            if (!view_is_valid(src)) {
              ps_throw(ctx, PS_ERR_RANGE, "view invalidated");
              goto raise;
            }
            size_t vidx = src->as.view_v.offset + idx;
            PS_Value *base = src->as.view_v.source;
            if (base->tag == PS_V_LIST) res = base->as.list_v.items[vidx];
            else if (base->tag == PS_V_STRING) {
              uint32_t g = ps_utf8_glyph_at((const uint8_t *)base->as.string_v.ptr, base->as.string_v.len, vidx);
              res = ps_make_glyph(ctx, g);
            }
          }
        }
        if (!res) goto raise;
        bindings_set(&temps, ins->dst, res);
        continue;
      }
      if (strcmp(ins->op, "call_static") == 0) {
        PS_Value *ret = NULL;
        PS_Value **argv = NULL;
        if (ins->arg_count > 0) {
          argv = (PS_Value **)calloc(ins->arg_count, sizeof(PS_Value *));
          for (size_t i = 0; i < ins->arg_count; i++) argv[i] = get_value(&temps, &vars, ins->args[i]);
        }
        if (exec_call_static(ctx, m, ins->callee, argv, ins->arg_count, &ret) != 0) {
          free(argv);
          goto raise;
        }
        free(argv);
        if (ins->dst) {
          bindings_set(&temps, ins->dst, ret);
          if (ret) ps_value_release(ret);
        }
        continue;
      }
      if (strcmp(ins->op, "call_method_static") == 0) {
        PS_Value *recv = get_value(&temps, &vars, ins->receiver);
        if (!recv) goto raise;
        if (recv->tag == PS_V_FILE) {
          PS_File *f = &recv->as.file_v;
          const int can_read = (f->flags & PS_FILE_READ) != 0;
          const int can_write = (f->flags & (PS_FILE_WRITE | PS_FILE_APPEND)) != 0;
          const int is_binary = (f->flags & PS_FILE_BINARY) != 0;
          if (strcmp(ins->method, "close") == 0) {
            if (f->flags & PS_FILE_STD) {
              ps_throw(ctx, PS_ERR_RANGE, "cannot close standard stream");
              goto raise;
            }
            if (!f->closed && f->fp) {
              fclose(f->fp);
              f->closed = 1;
            }
            continue;
          }
          if (f->closed || !f->fp) {
            ps_throw(ctx, PS_ERR_RANGE, "file is closed");
            goto raise;
          }
          if (strcmp(ins->method, "read") == 0) {
            if (!can_read) {
              ps_throw(ctx, PS_ERR_RANGE, "file not readable");
              goto raise;
            }
            size_t want = 0;
            int has_size = ins->arg_count > 0;
            if (has_size) {
              PS_Value *sv = get_value(&temps, &vars, ins->args[0]);
              if (!sv || sv->tag != PS_V_INT) {
                ps_throw(ctx, PS_ERR_RANGE, "invalid read size");
                goto raise;
              }
              if (sv->as.int_v <= 0) {
                ps_throw(ctx, PS_ERR_RANGE, "read size must be >= 1");
                goto raise;
              }
              want = (size_t)sv->as.int_v;
            }
            if (has_size) {
              uint8_t *buf = (uint8_t *)malloc(want);
              if (!buf) {
                ps_throw(ctx, PS_ERR_OOM, "out of memory");
                goto raise;
              }
              size_t n = fread(buf, 1, want, f->fp);
              if (ferror(f->fp)) {
                free(buf);
                ps_throw(ctx, PS_ERR_INTERNAL, "read failed");
                goto raise;
              }
              if (n == 0) {
                free(buf);
                PS_Value *eofv = value_from_literal(ctx, "eof", NULL, NULL);
                if (!eofv) goto raise;
                bindings_set(&temps, ins->dst, eofv);
                ps_value_release(eofv);
                continue;
              }
              if (!is_binary) {
                size_t start = 0;
                for (size_t i = 0; i < n; i++) {
                  if (buf[i] == 0x00) {
                    free(buf);
                    ps_throw(ctx, PS_ERR_UTF8, "NUL byte not allowed");
                    goto raise;
                  }
                }
                if (f->at_start && n >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF) {
                  start = 3;
                }
                for (size_t i = start; i + 2 < n; i++) {
                  if (buf[i] == 0xEF && buf[i + 1] == 0xBB && buf[i + 2] == 0xBF) {
                    free(buf);
                    ps_throw(ctx, PS_ERR_UTF8, "BOM not allowed here");
                    goto raise;
                  }
                }
                if (!ps_utf8_validate(buf + start, n - start)) {
                  free(buf);
                  ps_throw(ctx, PS_ERR_UTF8, "invalid UTF-8");
                  goto raise;
                }
                PS_Value *s = ps_make_string_utf8(ctx, (const char *)buf + start, n - start);
                free(buf);
                if (!s) goto raise;
                bindings_set(&temps, ins->dst, s);
                ps_value_release(s);
              } else {
                PS_Value *list = ps_list_new(ctx);
                for (size_t i = 0; i < n; i++) {
                  PS_Value *bv = ps_make_byte(ctx, buf[i]);
                  ps_list_push_internal(ctx, list, bv);
                  ps_value_release(bv);
                }
                free(buf);
                bindings_set(&temps, ins->dst, list);
                ps_value_release(list);
              }
              f->at_start = 0;
              continue;
            }
            size_t cap = 4096;
            size_t len = 0;
            uint8_t *buf = (uint8_t *)malloc(cap);
            if (!buf) {
              ps_throw(ctx, PS_ERR_OOM, "out of memory");
              goto raise;
            }
            while (1) {
              size_t n = fread(buf + len, 1, cap - len, f->fp);
              len += n;
              if (n == 0) break;
              if (len == cap) {
                size_t ncap = cap * 2;
                uint8_t *nbuf = (uint8_t *)realloc(buf, ncap);
                if (!nbuf) {
                  free(buf);
                  ps_throw(ctx, PS_ERR_OOM, "out of memory");
                  goto raise;
                }
                buf = nbuf;
                cap = ncap;
              }
            }
            if (ferror(f->fp)) {
              free(buf);
              ps_throw(ctx, PS_ERR_INTERNAL, "read failed");
              goto raise;
            }
            if (!is_binary) {
              size_t start = 0;
              for (size_t i = 0; i < len; i++) {
                if (buf[i] == 0x00) {
                  free(buf);
                  ps_throw(ctx, PS_ERR_UTF8, "NUL byte not allowed");
                  goto raise;
                }
              }
              if (f->at_start && len >= 3 && buf[0] == 0xEF && buf[1] == 0xBB && buf[2] == 0xBF) {
                start = 3;
              }
              for (size_t i = start; i + 2 < len; i++) {
                if (buf[i] == 0xEF && buf[i + 1] == 0xBB && buf[i + 2] == 0xBF) {
                  free(buf);
                  ps_throw(ctx, PS_ERR_UTF8, "BOM not allowed here");
                  goto raise;
                }
              }
              if (!ps_utf8_validate(buf + start, len - start)) {
                free(buf);
                ps_throw(ctx, PS_ERR_UTF8, "invalid UTF-8");
                goto raise;
              }
              PS_Value *s = ps_make_string_utf8(ctx, (const char *)buf + start, len - start);
              free(buf);
              if (!s) goto raise;
              bindings_set(&temps, ins->dst, s);
              ps_value_release(s);
            } else {
              PS_Value *list = ps_list_new(ctx);
              for (size_t i = 0; i < len; i++) {
                PS_Value *bv = ps_make_byte(ctx, buf[i]);
                ps_list_push_internal(ctx, list, bv);
                ps_value_release(bv);
              }
              free(buf);
              bindings_set(&temps, ins->dst, list);
              ps_value_release(list);
            }
            f->at_start = 0;
            continue;
          }
          if (strcmp(ins->method, "write") == 0) {
            if (!can_write) {
              ps_throw(ctx, PS_ERR_RANGE, "file not writable");
              goto raise;
            }
            PS_Value *arg = get_value(&temps, &vars, ins->args[0]);
            if (!arg) goto raise;
            if (!is_binary) {
              if (arg->tag != PS_V_STRING) {
                ps_throw(ctx, PS_ERR_TYPE, "write expects string");
                goto raise;
              }
              if (arg->as.string_v.len > 0) {
                fwrite(arg->as.string_v.ptr, 1, arg->as.string_v.len, f->fp);
              }
              if (ferror(f->fp)) {
                ps_throw(ctx, PS_ERR_INTERNAL, "write failed");
                goto raise;
              }
            } else {
              if (arg->tag != PS_V_LIST) {
                ps_throw(ctx, PS_ERR_TYPE, "write expects list<byte>");
                goto raise;
              }
              size_t n = arg->as.list_v.len;
              uint8_t *buf = (uint8_t *)malloc(n);
              if (!buf && n > 0) {
                ps_throw(ctx, PS_ERR_OOM, "out of memory");
                goto raise;
              }
              for (size_t i = 0; i < n; i++) {
                PS_Value *it = arg->as.list_v.items[i];
                if (!it || (it->tag != PS_V_INT && it->tag != PS_V_BYTE)) {
                  free(buf);
                  ps_throw(ctx, PS_ERR_TYPE, "list<byte> expected");
                  goto raise;
                }
                int64_t v = (it->tag == PS_V_BYTE) ? it->as.byte_v : it->as.int_v;
                if (v < 0 || v > 255) {
                  free(buf);
                  ps_throw(ctx, PS_ERR_RANGE, "byte out of range");
                  goto raise;
                }
                buf[i] = (uint8_t)v;
              }
              if (n > 0) fwrite(buf, 1, n, f->fp);
              if (ferror(f->fp)) {
                free(buf);
                ps_throw(ctx, PS_ERR_INTERNAL, "write failed");
                goto raise;
              }
              free(buf);
            }
            continue;
          }
        }
        if (recv->tag == PS_V_INT) {
          if (strcmp(ins->method, "toByte") == 0) {
            int64_t v = recv->as.int_v;
            if (v < 0 || v > 255) {
              ps_throw(ctx, PS_ERR_RANGE, "byte out of range");
              goto raise;
            }
            PS_Value *b = ps_make_byte(ctx, (uint8_t)v);
            bindings_set(&temps, ins->dst, b);
            ps_value_release(b);
          } else if (strcmp(ins->method, "toFloat") == 0) {
            PS_Value *f = ps_make_float(ctx, (double)recv->as.int_v);
            bindings_set(&temps, ins->dst, f);
            ps_value_release(f);
          } else if (strcmp(ins->method, "toBytes") == 0) {
            uint8_t buf[8];
            memcpy(buf, &recv->as.int_v, 8);
            PS_Value *b = ps_make_bytes(ctx, buf, 8);
            if (!b) goto raise;
            bindings_set(&temps, ins->dst, b);
            ps_value_release(b);
          } else if (strcmp(ins->method, "abs") == 0) {
            if (recv->as.int_v == INT64_MIN) {
              ps_throw(ctx, PS_ERR_RANGE, "int overflow");
              goto raise;
            }
            int64_t v = recv->as.int_v < 0 ? -recv->as.int_v : recv->as.int_v;
            PS_Value *i = ps_make_int(ctx, v);
            bindings_set(&temps, ins->dst, i);
            ps_value_release(i);
          } else if (strcmp(ins->method, "sign") == 0) {
            int64_t v = recv->as.int_v == 0 ? 0 : (recv->as.int_v > 0 ? 1 : -1);
            PS_Value *i = ps_make_int(ctx, v);
            bindings_set(&temps, ins->dst, i);
            ps_value_release(i);
          }
        } else if (recv->tag == PS_V_BYTE) {
          if (strcmp(ins->method, "toInt") == 0) {
            PS_Value *i = ps_make_int(ctx, (int64_t)recv->as.byte_v);
            bindings_set(&temps, ins->dst, i);
            ps_value_release(i);
          } else if (strcmp(ins->method, "toFloat") == 0) {
            PS_Value *f = ps_make_float(ctx, (double)recv->as.byte_v);
            bindings_set(&temps, ins->dst, f);
            ps_value_release(f);
          }
        } else if (recv->tag == PS_V_FLOAT) {
          if (strcmp(ins->method, "toInt") == 0) {
            double v = recv->as.float_v;
            if (!isfinite(v)) {
              ps_throw(ctx, PS_ERR_TYPE, "invalid float to int");
              goto raise;
            }
            if (v > (double)INT64_MAX || v < (double)INT64_MIN) {
              ps_throw(ctx, PS_ERR_RANGE, "int overflow");
              goto raise;
            }
            int64_t i64 = (int64_t)trunc(v);
            PS_Value *i = ps_make_int(ctx, i64);
            bindings_set(&temps, ins->dst, i);
            ps_value_release(i);
          } else if (strcmp(ins->method, "toBytes") == 0) {
            uint8_t buf[8];
            memcpy(buf, &recv->as.float_v, 8);
            PS_Value *b = ps_make_bytes(ctx, buf, 8);
            if (!b) goto raise;
            bindings_set(&temps, ins->dst, b);
            ps_value_release(b);
          } else if (strcmp(ins->method, "abs") == 0) {
            PS_Value *f = ps_make_float(ctx, fabs(recv->as.float_v));
            bindings_set(&temps, ins->dst, f);
            ps_value_release(f);
          } else if (strcmp(ins->method, "isNaN") == 0) {
            PS_Value *b = ps_make_bool(ctx, isnan(recv->as.float_v) ? 1 : 0);
            bindings_set(&temps, ins->dst, b);
            ps_value_release(b);
          } else if (strcmp(ins->method, "isInfinite") == 0) {
            PS_Value *b = ps_make_bool(ctx, isinf(recv->as.float_v) ? 1 : 0);
            bindings_set(&temps, ins->dst, b);
            ps_value_release(b);
          } else if (strcmp(ins->method, "isFinite") == 0) {
            PS_Value *b = ps_make_bool(ctx, isfinite(recv->as.float_v) ? 1 : 0);
            bindings_set(&temps, ins->dst, b);
            ps_value_release(b);
          }
        } else if (recv->tag == PS_V_GLYPH) {
          if (strcmp(ins->method, "isLetter") == 0) {
            PS_Value *b = ps_make_bool(ctx, glyph_is_letter(recv->as.glyph_v));
            bindings_set(&temps, ins->dst, b);
            ps_value_release(b);
          } else if (strcmp(ins->method, "isDigit") == 0) {
            PS_Value *b = ps_make_bool(ctx, glyph_is_digit(recv->as.glyph_v));
            bindings_set(&temps, ins->dst, b);
            ps_value_release(b);
          } else if (strcmp(ins->method, "isWhitespace") == 0) {
            PS_Value *b = ps_make_bool(ctx, glyph_is_whitespace(recv->as.glyph_v));
            bindings_set(&temps, ins->dst, b);
            ps_value_release(b);
          } else if (strcmp(ins->method, "isUpper") == 0) {
            PS_Value *b = ps_make_bool(ctx, glyph_is_upper(recv->as.glyph_v));
            bindings_set(&temps, ins->dst, b);
            ps_value_release(b);
          } else if (strcmp(ins->method, "isLower") == 0) {
            PS_Value *b = ps_make_bool(ctx, glyph_is_lower(recv->as.glyph_v));
            bindings_set(&temps, ins->dst, b);
            ps_value_release(b);
          } else if (strcmp(ins->method, "toUpper") == 0) {
            PS_Value *g = ps_make_glyph(ctx, glyph_to_upper(recv->as.glyph_v));
            bindings_set(&temps, ins->dst, g);
            ps_value_release(g);
          } else if (strcmp(ins->method, "toLower") == 0) {
            PS_Value *g = ps_make_glyph(ctx, glyph_to_lower(recv->as.glyph_v));
            bindings_set(&temps, ins->dst, g);
            ps_value_release(g);
          } else if (strcmp(ins->method, "toInt") == 0) {
            PS_Value *i = ps_make_int(ctx, (int64_t)recv->as.glyph_v);
            bindings_set(&temps, ins->dst, i);
            ps_value_release(i);
          } else if (strcmp(ins->method, "toUtf8Bytes") == 0) {
            uint8_t buf[4];
            size_t n = 0;
            if (!glyph_to_utf8(recv->as.glyph_v, buf, &n)) {
              ps_throw(ctx, PS_ERR_UTF8, "invalid UTF-8");
              goto raise;
            }
            PS_Value *b = ps_make_bytes(ctx, buf, n);
            if (!b) goto raise;
            bindings_set(&temps, ins->dst, b);
            ps_value_release(b);
          }
        } else if (recv->tag == PS_V_STRING) {
          if (strcmp(ins->method, "length") == 0) {
            size_t gl = ps_utf8_glyph_len((const uint8_t *)recv->as.string_v.ptr, recv->as.string_v.len);
            PS_Value *v = ps_make_int(ctx, (int64_t)gl);
            bindings_set(&temps, ins->dst, v);
            ps_value_release(v);
          } else if (strcmp(ins->method, "isEmpty") == 0) {
            size_t gl = ps_utf8_glyph_len((const uint8_t *)recv->as.string_v.ptr, recv->as.string_v.len);
            PS_Value *v = ps_make_bool(ctx, gl == 0);
            bindings_set(&temps, ins->dst, v);
            ps_value_release(v);
          } else if (strcmp(ins->method, "toInt") == 0) {
            int64_t iv = 0;
            if (!parse_int_strict(ctx, recv->as.string_v.ptr, recv->as.string_v.len, &iv)) goto raise;
            PS_Value *v = ps_make_int(ctx, iv);
            bindings_set(&temps, ins->dst, v);
            ps_value_release(v);
          } else if (strcmp(ins->method, "toFloat") == 0) {
            double fv = 0.0;
            if (!parse_float_strict(ctx, recv->as.string_v.ptr, recv->as.string_v.len, &fv)) goto raise;
            PS_Value *v = ps_make_float(ctx, fv);
            bindings_set(&temps, ins->dst, v);
            ps_value_release(v);
          } else if (strcmp(ins->method, "substring") == 0) {
            PS_Value *a = get_value(&temps, &vars, ins->args[0]);
            PS_Value *b = get_value(&temps, &vars, ins->args[1]);
            PS_Value *v = ps_string_substring(ctx, recv, (size_t)a->as.int_v, (size_t)b->as.int_v);
            if (!v) goto raise;
            bindings_set(&temps, ins->dst, v);
            ps_value_release(v);
          } else if (strcmp(ins->method, "indexOf") == 0) {
            PS_Value *needle = get_value(&temps, &vars, ins->args[0]);
            int64_t idx = ps_string_index_of(recv, needle);
            PS_Value *v = ps_make_int(ctx, idx);
            bindings_set(&temps, ins->dst, v);
            ps_value_release(v);
          } else if (strcmp(ins->method, "startsWith") == 0) {
            PS_Value *p = get_value(&temps, &vars, ins->args[0]);
            PS_Value *v = ps_make_bool(ctx, ps_string_starts_with(recv, p));
            bindings_set(&temps, ins->dst, v);
            ps_value_release(v);
          } else if (strcmp(ins->method, "endsWith") == 0) {
            PS_Value *p = get_value(&temps, &vars, ins->args[0]);
            PS_Value *v = ps_make_bool(ctx, ps_string_ends_with(recv, p));
            bindings_set(&temps, ins->dst, v);
            ps_value_release(v);
          } else if (strcmp(ins->method, "split") == 0) {
            PS_Value *sep = get_value(&temps, &vars, ins->args[0]);
            PS_Value *v = ps_string_split(ctx, recv, sep);
            if (!v) goto raise;
            bindings_set(&temps, ins->dst, v);
            ps_value_release(v);
          } else if (strcmp(ins->method, "trim") == 0) {
            PS_Value *v = ps_string_trim(ctx, recv, 0);
            if (!v) goto raise;
            bindings_set(&temps, ins->dst, v);
            ps_value_release(v);
          } else if (strcmp(ins->method, "trimStart") == 0) {
            PS_Value *v = ps_string_trim(ctx, recv, 1);
            if (!v) goto raise;
            bindings_set(&temps, ins->dst, v);
            ps_value_release(v);
          } else if (strcmp(ins->method, "trimEnd") == 0) {
            PS_Value *v = ps_string_trim(ctx, recv, 2);
            if (!v) goto raise;
            bindings_set(&temps, ins->dst, v);
            ps_value_release(v);
          } else if (strcmp(ins->method, "replace") == 0) {
            PS_Value *a = get_value(&temps, &vars, ins->args[0]);
            PS_Value *b = get_value(&temps, &vars, ins->args[1]);
            PS_Value *v = ps_string_replace(ctx, recv, a, b);
            if (!v) goto raise;
            bindings_set(&temps, ins->dst, v);
            ps_value_release(v);
          } else if (strcmp(ins->method, "toUpper") == 0) {
            PS_Value *v = ps_string_to_upper(ctx, recv);
            bindings_set(&temps, ins->dst, v);
            ps_value_release(v);
          } else if (strcmp(ins->method, "toLower") == 0) {
            PS_Value *v = ps_string_to_lower(ctx, recv);
            bindings_set(&temps, ins->dst, v);
            ps_value_release(v);
          } else if (strcmp(ins->method, "toUtf8Bytes") == 0) {
            PS_Value *v = ps_string_to_utf8_bytes(ctx, recv);
            bindings_set(&temps, ins->dst, v);
            ps_value_release(v);
          }
        } else if (recv->tag == PS_V_LIST) {
          if (strcmp(ins->method, "length") == 0) {
            PS_Value *v = ps_make_int(ctx, (int64_t)recv->as.list_v.len);
            bindings_set(&temps, ins->dst, v);
            ps_value_release(v);
          } else if (strcmp(ins->method, "isEmpty") == 0) {
            PS_Value *v = ps_make_bool(ctx, recv->as.list_v.len == 0);
            bindings_set(&temps, ins->dst, v);
            ps_value_release(v);
          } else if (strcmp(ins->method, "contains") == 0) {
            PS_Value *needle = get_value(&temps, &vars, ins->args[0]);
            int found = 0;
            for (size_t i = 0; i < recv->as.list_v.len; i++) {
              if (values_equal(recv->as.list_v.items[i], needle)) {
                found = 1;
                break;
              }
            }
            PS_Value *v = ps_make_bool(ctx, found);
            bindings_set(&temps, ins->dst, v);
            ps_value_release(v);
          } else if (strcmp(ins->method, "sort") == 0) {
            size_t n = recv->as.list_v.len;
            PS_ValueTag tag = PS_V_VOID;
            if (n > 0) {
              PS_Value *first = recv->as.list_v.items[0];
              tag = first ? first->tag : PS_V_VOID;
              if (!(tag == PS_V_INT || tag == PS_V_FLOAT || tag == PS_V_BYTE || tag == PS_V_GLYPH || tag == PS_V_STRING || tag == PS_V_BOOL)) {
                ps_throw(ctx, PS_ERR_TYPE, "list element not comparable");
                goto raise;
              }
              for (size_t i = 0; i < n; i++) {
                PS_Value *it = recv->as.list_v.items[i];
                if (!it || it->tag != tag) {
                  ps_throw(ctx, PS_ERR_TYPE, "list element not comparable");
                  goto raise;
                }
              }
            }
            if (n > 1) {
              for (size_t i = 0; i + 1 < n; i++) {
                for (size_t j = i + 1; j < n; j++) {
                  int cmp = 0;
                  if (!compare_values(recv->as.list_v.items[i], recv->as.list_v.items[j], tag, &cmp)) {
                    ps_throw(ctx, PS_ERR_TYPE, "list element not comparable");
                    goto raise;
                  }
                  if (cmp > 0) {
                    PS_Value *tmp = recv->as.list_v.items[i];
                    recv->as.list_v.items[i] = recv->as.list_v.items[j];
                    recv->as.list_v.items[j] = tmp;
                  }
                }
              }
            }
            PS_Value *v = ps_make_int(ctx, (int64_t)recv->as.list_v.len);
            bindings_set(&temps, ins->dst, v);
            ps_value_release(v);
          } else if (strcmp(ins->method, "join") == 0 || strcmp(ins->method, "concat") == 0) {
            size_t n = recv->as.list_v.len;
            PS_Value *sepv = NULL;
            if (strcmp(ins->method, "join") == 0) {
              if (ins->arg_count > 0) sepv = get_value(&temps, &vars, ins->args[0]);
              if (sepv && sepv->tag != PS_V_STRING) {
                ps_throw(ctx, PS_ERR_TYPE, "join expects string separator");
                goto raise;
              }
            }
            size_t sep_len = sepv ? sepv->as.string_v.len : 0;
            size_t total = 0;
            for (size_t i = 0; i < n; i++) {
              PS_Value *it = recv->as.list_v.items[i];
              if (!it || it->tag != PS_V_STRING) {
                ps_throw(ctx, PS_ERR_TYPE, "join expects list<string>");
                goto raise;
              }
              total += it->as.string_v.len;
            }
            if (n > 1) total += sep_len * (n - 1);
            char *buf = (char *)malloc(total + 1);
            if (!buf && total > 0) {
              ps_throw(ctx, PS_ERR_OOM, "out of memory");
              goto raise;
            }
            size_t off = 0;
            for (size_t i = 0; i < n; i++) {
              if (i > 0 && sep_len > 0) {
                memcpy(buf + off, sepv->as.string_v.ptr, sep_len);
                off += sep_len;
              }
              if (recv->as.list_v.items[i]->as.string_v.len > 0) {
                memcpy(buf + off, recv->as.list_v.items[i]->as.string_v.ptr, recv->as.list_v.items[i]->as.string_v.len);
                off += recv->as.list_v.items[i]->as.string_v.len;
              }
            }
            if (buf) buf[off] = '\0';
            PS_Value *out = ps_make_string_utf8(ctx, buf ? buf : "", off);
            if (buf) free(buf);
            if (!out) goto raise;
            bindings_set(&temps, ins->dst, out);
            ps_value_release(out);
          }
        } else if (recv->tag == PS_V_MAP) {
          if (strcmp(ins->method, "length") == 0) {
            PS_Value *v = ps_make_int(ctx, (int64_t)recv->as.map_v.len);
            bindings_set(&temps, ins->dst, v);
            ps_value_release(v);
          } else if (strcmp(ins->method, "isEmpty") == 0) {
            PS_Value *v = ps_make_bool(ctx, recv->as.map_v.len == 0);
            bindings_set(&temps, ins->dst, v);
            ps_value_release(v);
          } else if (strcmp(ins->method, "containsKey") == 0) {
            PS_Value *key = get_value(&temps, &vars, ins->args[0]);
            if (recv->as.map_v.len > 0) {
              PS_Value *first = map_first_key(recv);
              if (first && key && first->tag != key->tag) {
                ps_throw(ctx, PS_ERR_TYPE, "map key type mismatch");
                goto raise;
              }
            }
            int ok = ps_map_has_key(ctx, recv, key);
            if (ps_last_error_code(ctx) != PS_ERR_NONE) goto raise;
            PS_Value *v = ps_make_bool(ctx, ok);
            bindings_set(&temps, ins->dst, v);
            ps_value_release(v);
          } else if (strcmp(ins->method, "keys") == 0) {
            PS_Value *out = ps_list_new(ctx);
            if (!out) goto raise;
            PS_Map *m = &recv->as.map_v;
            for (size_t i = 0; i < m->cap; i++) {
              if (!m->used[i]) continue;
              if (!ps_list_push_internal(ctx, out, m->keys[i])) {
                ps_value_release(out);
                goto raise;
              }
            }
            bindings_set(&temps, ins->dst, out);
            ps_value_release(out);
          } else if (strcmp(ins->method, "values") == 0) {
            PS_Value *out = ps_list_new(ctx);
            if (!out) goto raise;
            PS_Map *m = &recv->as.map_v;
            for (size_t i = 0; i < m->cap; i++) {
              if (!m->used[i]) continue;
              if (!ps_list_push_internal(ctx, out, m->values[i])) {
                ps_value_release(out);
                goto raise;
              }
            }
            bindings_set(&temps, ins->dst, out);
            ps_value_release(out);
          }
        } else if (recv->tag == PS_V_VIEW) {
          if (strcmp(ins->method, "length") == 0) {
            if (!view_is_valid(recv)) {
              ps_throw(ctx, PS_ERR_RANGE, "view invalidated");
              goto raise;
            }
            PS_Value *v = ps_make_int(ctx, (int64_t)recv->as.view_v.len);
            bindings_set(&temps, ins->dst, v);
            ps_value_release(v);
          } else if (strcmp(ins->method, "isEmpty") == 0) {
            if (!view_is_valid(recv)) {
              ps_throw(ctx, PS_ERR_RANGE, "view invalidated");
              goto raise;
            }
            PS_Value *v = ps_make_bool(ctx, recv->as.view_v.len == 0);
            bindings_set(&temps, ins->dst, v);
            ps_value_release(v);
          }
        } else if (recv->tag == PS_V_LIST && strcmp(ins->method, "pop") == 0) {
          if (recv->as.list_v.len == 0) {
            ps_throw(ctx, PS_ERR_RANGE, "pop on empty list");
            goto raise;
          }
          PS_Value *v = recv->as.list_v.items[recv->as.list_v.len - 1];
          recv->as.list_v.len -= 1;
          recv->as.list_v.version += 1;
          bindings_set(&temps, ins->dst, v);
        } else if (recv->tag == PS_V_LIST && strcmp(ins->method, "push") == 0) {
          PS_Value *v = get_value(&temps, &vars, ins->args[0]);
          if (!ps_list_push_internal(ctx, recv, v)) goto raise;
          PS_Value *rv = ps_make_int(ctx, (int64_t)recv->as.list_v.len);
          bindings_set(&temps, ins->dst, rv);
          ps_value_release(rv);
        } else if (recv->tag == PS_V_BYTES && strcmp(ins->method, "toUtf8String") == 0) {
          PS_Value *v = ps_bytes_to_utf8_string(ctx, recv);
          if (!v) goto raise;
          bindings_set(&temps, ins->dst, v);
          ps_value_release(v);
        }
        continue;
      }
      if (strcmp(ins->op, "call_builtin_print") == 0) {
        if (ins->arg_count > 0) {
          PS_Value *v = get_value(&temps, &vars, ins->args[0]);
          if (v && v->tag == PS_V_STRING) {
            fwrite(v->as.string_v.ptr, 1, v->as.string_v.len, stdout);
            fputc('\n', stdout);
          }
        } else {
          fputc('\n', stdout);
        }
        continue;
      }
      if (strcmp(ins->op, "call_builtin_tostring") == 0) {
        PS_Value *v = get_value(&temps, &vars, ins->value);
        PS_Value *s = NULL;
        if (v->tag == PS_V_STRING) s = ps_value_retain(v);
        else if (v->tag == PS_V_INT) {
          char buf[64];
          snprintf(buf, sizeof(buf), "%lld", (long long)v->as.int_v);
          s = ps_make_string_utf8(ctx, buf, strlen(buf));
        } else if (v->tag == PS_V_BYTE) {
          char buf[64];
          snprintf(buf, sizeof(buf), "%u", (unsigned)v->as.byte_v);
          s = ps_make_string_utf8(ctx, buf, strlen(buf));
        } else if (v->tag == PS_V_GLYPH) {
          uint8_t buf[4];
          size_t n = 0;
          if (!glyph_to_utf8(v->as.glyph_v, buf, &n)) {
            ps_throw(ctx, PS_ERR_UTF8, "invalid UTF-8");
            goto raise;
          }
          s = ps_make_string_utf8(ctx, (const char *)buf, n);
        } else if (v->tag == PS_V_FLOAT) {
          char buf[64];
          snprintf(buf, sizeof(buf), "%.17g", v->as.float_v);
          s = ps_make_string_utf8(ctx, buf, strlen(buf));
        } else if (v->tag == PS_V_BOOL) {
          const char *t = v->as.bool_v ? "true" : "false";
          s = ps_make_string_utf8(ctx, t, strlen(t));
        } else {
          s = ps_make_string_utf8(ctx, "<value>", 7);
        }
        bindings_set(&temps, ins->dst, s);
        ps_value_release(s);
        continue;
      }
      if (strcmp(ins->op, "jump") == 0) {
        block_idx = find_block(f, ins->target);
        goto next_block;
      }
      if (strcmp(ins->op, "branch_if") == 0) {
        PS_Value *c = get_value(&temps, &vars, ins->cond);
        block_idx = find_block(f, is_truthy(c) ? ins->then_label : ins->else_label);
        goto next_block;
      }
      if (strcmp(ins->op, "ret") == 0) {
        PS_Value *v = get_value(&temps, &vars, ins->value);
        if (out) *out = v ? ps_value_retain(v) : NULL;
        bindings_free(&temps);
        bindings_free(&vars);
        if (last_exception) ps_value_release(last_exception);
        free(tries);
        return 0;
      }
      if (strcmp(ins->op, "ret_void") == 0) {
        bindings_free(&temps);
        bindings_free(&vars);
        if (last_exception) ps_value_release(last_exception);
        free(tries);
        return 0;
      }
      if (strcmp(ins->op, "throw") == 0) {
        PS_Value *v = get_value(&temps, &vars, ins->value);
        if (!v || v->tag != PS_V_EXCEPTION) {
          ps_throw(ctx, PS_ERR_TYPE, "throw expects Exception");
          goto raise;
        }
        if (last_exception) ps_value_release(last_exception);
        last_exception = ps_value_retain(v);
        goto raise;
      }
    }
    block_idx += 1;
  next_block:
    continue;
  }
  bindings_free(&temps);
  bindings_free(&vars);
  if (last_exception) ps_value_release(last_exception);
  free(tries);
  return 0;

raise:
  if (ps_last_error_code(ctx) != PS_ERR_NONE) {
    if (last_exception) {
      ps_value_release(last_exception);
      last_exception = NULL;
    }
    last_exception = make_runtime_exception_from_error(ctx);
  }
  if (!last_exception) {
    ps_throw(ctx, PS_ERR_INTERNAL, "runtime error");
    last_exception = make_runtime_exception_from_error(ctx);
  }
  if (try_len > 0) {
    const char *handler = tries[try_len - 1].handler;
    try_len -= 1;
    ps_clear_error(ctx);
    block_idx = find_block(f, handler);
    if (block_idx < f->block_count) goto next_block;
  }
  goto error;

error:
  bindings_free(&temps);
  bindings_free(&vars);
  if (last_exception) ps_value_release(last_exception);
  free(tries);
  return 1;
}

int ps_vm_run_main(PS_Context *ctx, PS_IR_Module *m, PS_Value **args, size_t argc, PS_Value **out) {
  IRFunction *main_fn = find_fn(m, "main");
  if (!main_fn) {
    ps_throw(ctx, PS_ERR_INTERNAL, "no main");
    return 1;
  }
  if (main_fn->param_count > 1) {
    ps_throw(ctx, PS_ERR_TYPE, "main must take zero or one argument");
    return 1;
  }
  if (main_fn->param_count == 1) {
    return exec_function(ctx, m, main_fn, args, argc, out);
  }
  return exec_function(ctx, m, main_fn, NULL, 0, out);
}
