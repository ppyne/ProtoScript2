#include <stdio.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

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
  return NULL;
}

static PS_Value *get_value(PS_Bindings *temps, PS_Bindings *vars, const char *name) {
  if (!name) return NULL;
  PS_Value *v = bindings_get(temps, name);
  if (v) return v;
  return bindings_get(vars, name);
}

static int is_truthy(PS_Value *v) {
  if (!v) return 0;
  if (v->tag == PS_V_BOOL) return v->as.bool_v != 0;
  if (v->tag == PS_V_INT) return v->as.int_v != 0;
  return 0;
}

static int exec_function(PS_Context *ctx, PS_IR_Module *m, IRFunction *f, PS_Value **args, size_t argc, PS_Value **out);

static int exec_call_static(PS_Context *ctx, PS_IR_Module *m, const char *callee, PS_Value **args, size_t argc, PS_Value **out) {
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
  char last_exception_msg[256];
  PS_ErrorCode last_exception_code = PS_ERR_INTERNAL;
  last_exception_msg[0] = '\0';
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
        const char *msg = last_exception_msg[0] ? last_exception_msg : "exception";
        PS_Value *v = ps_make_string_utf8(ctx, msg, strlen(msg));
        if (!v) goto raise;
        bindings_set(&temps, ins->dst, v);
        ps_value_release(v);
        continue;
      }
      if (strcmp(ins->op, "rethrow") == 0) {
        const char *msg = last_exception_msg[0] ? last_exception_msg : "exception";
        ps_throw(ctx, last_exception_code, msg);
        goto raise;
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
        else if (strcmp(ins->operator, "==") == 0) res = ps_make_bool(ctx, l->as.int_v == r->as.int_v);
        else if (strcmp(ins->operator, "!=") == 0) res = ps_make_bool(ctx, l->as.int_v != r->as.int_v);
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
        if (recv->tag == PS_V_STRING) {
          if (strcmp(ins->method, "length") == 0) {
            size_t gl = ps_utf8_glyph_len((const uint8_t *)recv->as.string_v.ptr, recv->as.string_v.len);
            PS_Value *v = ps_make_int(ctx, (int64_t)gl);
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
        } else if (recv->tag == PS_V_LIST && strcmp(ins->method, "pop") == 0) {
          if (recv->as.list_v.len == 0) {
            ps_throw(ctx, PS_ERR_RANGE, "pop on empty list");
            goto raise;
          }
          PS_Value *v = recv->as.list_v.items[recv->as.list_v.len - 1];
          recv->as.list_v.len -= 1;
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
        return 0;
      }
      if (strcmp(ins->op, "ret_void") == 0) {
        bindings_free(&temps);
        bindings_free(&vars);
        return 0;
      }
      if (strcmp(ins->op, "throw") == 0) {
        ps_throw(ctx, PS_ERR_INTERNAL, "exception thrown");
        goto raise;
      }
    }
    block_idx += 1;
  next_block:
    continue;
  }
  bindings_free(&temps);
  bindings_free(&vars);
  free(tries);
  return 0;

raise:
  if (ps_last_error_code(ctx) == PS_ERR_NONE) {
    ps_throw(ctx, PS_ERR_INTERNAL, "runtime error");
  }
  if (try_len > 0) {
    const char *handler = tries[try_len - 1].handler;
    try_len -= 1;
    strncpy(last_exception_msg, ps_last_error_message(ctx), sizeof(last_exception_msg) - 1);
    last_exception_msg[sizeof(last_exception_msg) - 1] = '\0';
    last_exception_code = ps_last_error_code(ctx);
    ps_clear_error(ctx);
    block_idx = find_block(f, handler);
    if (block_idx < f->block_count) goto next_block;
  }
  goto error;

error:
  bindings_free(&temps);
  bindings_free(&vars);
  free(tries);
  return 1;
}

int ps_vm_run_main(PS_Context *ctx, PS_IR_Module *m) {
  IRFunction *main_fn = find_fn(m, "main");
  if (!main_fn) {
    ps_throw(ctx, PS_ERR_INTERNAL, "no main");
    return 1;
  }
  return exec_function(ctx, m, main_fn, NULL, 0, NULL);
}
