#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "debug.h"
#include "runtime/ps_list.h"
#include "runtime/ps_map.h"
#include "runtime/ps_object.h"
#include "runtime/ps_modules.h"
#include "runtime/ps_runtime.h"
#include "runtime/ps_string.h"
#include "runtime/ps_vm_internal.h"

typedef struct {
  PS_Value *value;
  size_t id;
} DebugSeen;

typedef struct {
  FILE *out;
  size_t max_depth;
  size_t max_items;
  size_t max_string;
  DebugSeen *seen;
  size_t seen_len;
  size_t seen_cap;
  int io_error;
} DebugState;

static int debug_dump_scalar(DebugState *st, PS_Value *v);

static int debug_write(DebugState *st, const char *s) {
  if (!st || st->io_error) return 0;
  if (fputs(s, st->out) < 0) {
    st->io_error = 1;
    return 0;
  }
  return 1;
}

static int debug_printf(DebugState *st, const char *fmt, ...) {
  if (!st || st->io_error) return 0;
  va_list args;
  va_start(args, fmt);
  int ok = vfprintf(st->out, fmt, args) >= 0;
  va_end(args);
  if (!ok) st->io_error = 1;
  return ok;
}

static void debug_indent(DebugState *st, int indent) {
  for (int i = 0; i < indent; i++) {
    if (!debug_write(st, " ")) return;
  }
}

static int debug_seen_find(DebugState *st, PS_Value *v, size_t *out_id) {
  if (!st || !v) return 0;
  for (size_t i = 0; i < st->seen_len; i++) {
    if (st->seen[i].value == v) {
      if (out_id) *out_id = st->seen[i].id;
      return 1;
    }
  }
  return 0;
}

static int debug_seen_add(DebugState *st, PS_Value *v, size_t *out_id) {
  if (!st || !v) return 0;
  if (st->seen_len == st->seen_cap) {
    size_t new_cap = st->seen_cap == 0 ? 16 : st->seen_cap * 2;
    DebugSeen *n = (DebugSeen *)realloc(st->seen, sizeof(DebugSeen) * new_cap);
    if (!n) return 0;
    st->seen = n;
    st->seen_cap = new_cap;
  }
  size_t id = st->seen_len + 1;
  st->seen[st->seen_len].value = v;
  st->seen[st->seen_len].id = id;
  st->seen_len += 1;
  if (out_id) *out_id = id;
  return 1;
}

static int utf8_next(const uint8_t *s, size_t len, size_t *i, uint32_t *out) {
  if (*i >= len) return 0;
  uint8_t c = s[*i];
  if (c < 0x80) {
    *out = c;
    *i += 1;
    return 1;
  }
  if ((c & 0xE0) == 0xC0) {
    if (*i + 1 >= len) return -1;
    uint8_t c1 = s[*i + 1];
    if ((c1 & 0xC0) != 0x80) return -1;
    uint32_t cp = ((uint32_t)(c & 0x1F) << 6) | (uint32_t)(c1 & 0x3F);
    if (cp < 0x80) return -1;
    *out = cp;
    *i += 2;
    return 1;
  }
  if ((c & 0xF0) == 0xE0) {
    if (*i + 2 >= len) return -1;
    uint8_t c1 = s[*i + 1];
    uint8_t c2 = s[*i + 2];
    if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80) return -1;
    uint32_t cp = ((uint32_t)(c & 0x0F) << 12) | ((uint32_t)(c1 & 0x3F) << 6) | (uint32_t)(c2 & 0x3F);
    if (cp < 0x800) return -1;
    *out = cp;
    *i += 3;
    return 1;
  }
  if ((c & 0xF8) == 0xF0) {
    if (*i + 3 >= len) return -1;
    uint8_t c1 = s[*i + 1];
    uint8_t c2 = s[*i + 2];
    uint8_t c3 = s[*i + 3];
    if ((c1 & 0xC0) != 0x80 || (c2 & 0xC0) != 0x80 || (c3 & 0xC0) != 0x80) return -1;
    uint32_t cp = ((uint32_t)(c & 0x07) << 18) | ((uint32_t)(c1 & 0x3F) << 12) | ((uint32_t)(c2 & 0x3F) << 6) |
                  (uint32_t)(c3 & 0x3F);
    if (cp < 0x10000 || cp > 0x10FFFF) return -1;
    *out = cp;
    *i += 4;
    return 1;
  }
  return -1;
}

static int debug_write_escaped_glyph(DebugState *st, uint32_t cp, const uint8_t *bytes, size_t blen) {
  if (!st) return 0;
  switch (cp) {
    case '"': return debug_write(st, "\\\"");
    case '\\': return debug_write(st, "\\\\");
    case '\n': return debug_write(st, "\\n");
    case '\r': return debug_write(st, "\\r");
    case '\t': return debug_write(st, "\\t");
    case '\b': return debug_write(st, "\\b");
    case '\f': return debug_write(st, "\\f");
    default:
      if (cp < 0x20) {
        return debug_printf(st, "\\u%04X", (unsigned)cp);
      }
      if (bytes && blen > 0) {
        if (fwrite(bytes, 1, blen, st->out) != blen) {
          st->io_error = 1;
          return 0;
        }
        return 1;
      }
      return 0;
  }
}

static int debug_write_string(DebugState *st, const char *s, size_t len, size_t max_glyphs, int *truncated) {
  if (!st || !s) return 0;
  const uint8_t *buf = (const uint8_t *)s;
  size_t i = 0;
  size_t glyphs = 0;
  if (truncated) *truncated = 0;
  while (i < len) {
    if (glyphs >= max_glyphs) {
      if (truncated) *truncated = 1;
      break;
    }
    size_t start = i;
    uint32_t cp = 0;
    int r = utf8_next(buf, len, &i, &cp);
    if (r <= 0) break;
    size_t blen = i - start;
    if (!debug_write_escaped_glyph(st, cp, buf + start, blen)) return 0;
    glyphs += 1;
  }
  if (i < len && truncated) *truncated = 1;
  return 1;
}

static const char *list_type_name(PS_Value *v) {
  const char *t = ps_list_type_name_internal(v);
  return t ? t : "list<unknown>";
}

static const char *map_type_name(PS_Value *v) {
  const char *t = ps_map_type_name_internal(v);
  return t ? t : "map<unknown,unknown>";
}

static const char *view_type_name(PS_Value *v) {
  if (v && v->tag == PS_V_VIEW && v->as.view_v.type_name) return v->as.view_v.type_name;
  if (v && v->tag == PS_V_VIEW) return v->as.view_v.readonly ? "view<unknown>" : "slice<unknown>";
  return "view<unknown>";
}


static const char *value_tag_name(PS_Value *v) {
  if (!v) return "null";
  switch (v->tag) {
    case PS_V_BOOL: return "bool";
    case PS_V_INT: return "int";
    case PS_V_FLOAT: return "float";
    case PS_V_BYTE: return "byte";
    case PS_V_GLYPH: return "glyph";
    case PS_V_STRING: return "string";
    case PS_V_BYTES: return "bytes";
    case PS_V_LIST: return "list";
    case PS_V_MAP: return "map";
    case PS_V_VIEW: return "view";
    case PS_V_OBJECT: return "object";
    case PS_V_EXCEPTION: return "Exception";
    case PS_V_FILE: return "file";
    case PS_V_ITER: return "iter";
    case PS_V_GROUP: return "group";
    default: return "value";
  }
}

typedef enum {
  DEBUG_PROTO_NONE = 0,
  DEBUG_PROTO_IR = 1,
  DEBUG_PROTO_NATIVE = 2
} DebugProtoKind;

typedef struct {
  DebugProtoKind kind;
  const PS_IR_Proto *ir;
  const PS_ProtoDesc *native;
} DebugProto;

static const PS_ProtoParamDesc DEBUG_TEXT_READ_PARAMS[] = {
  { .name = "size", .type = "int", .variadic = 0 },
};
static const PS_ProtoParamDesc DEBUG_TEXT_WRITE_PARAMS[] = {
  { .name = "text", .type = "string", .variadic = 0 },
};
static const PS_ProtoParamDesc DEBUG_TEXT_SEEK_PARAMS[] = {
  { .name = "pos", .type = "int", .variadic = 0 },
};
static const PS_ProtoMethodDesc DEBUG_TEXT_METHODS[] = {
  { .name = "read", .params = DEBUG_TEXT_READ_PARAMS, .param_count = 1, .ret_type = "string" },
  { .name = "write", .params = DEBUG_TEXT_WRITE_PARAMS, .param_count = 1, .ret_type = "void" },
  { .name = "tell", .params = NULL, .param_count = 0, .ret_type = "int" },
  { .name = "seek", .params = DEBUG_TEXT_SEEK_PARAMS, .param_count = 1, .ret_type = "void" },
  { .name = "size", .params = NULL, .param_count = 0, .ret_type = "int" },
  { .name = "name", .params = NULL, .param_count = 0, .ret_type = "string" },
  { .name = "close", .params = NULL, .param_count = 0, .ret_type = "void" },
};
static const PS_ProtoMethodDesc DEBUG_OBJECT_METHODS[] = {
  { .name = "clone", .params = NULL, .param_count = 0, .ret_type = "Object" },
};
static const PS_ProtoDesc DEBUG_OBJECT_PROTO = {
  .name = "Object",
  .parent = NULL,
  .fields = NULL,
  .field_count = 0,
  .methods = DEBUG_OBJECT_METHODS,
  .method_count = sizeof(DEBUG_OBJECT_METHODS) / sizeof(DEBUG_OBJECT_METHODS[0]),
  .is_sealed = 0,
};
static const PS_ProtoDesc DEBUG_TEXTFILE_PROTO = {
  .name = "TextFile",
  .parent = "Object",
  .fields = NULL,
  .field_count = 0,
  .methods = DEBUG_TEXT_METHODS,
  .method_count = sizeof(DEBUG_TEXT_METHODS) / sizeof(DEBUG_TEXT_METHODS[0]),
  .is_sealed = 1,
};

static const PS_ProtoParamDesc DEBUG_BINARY_READ_PARAMS[] = {
  { .name = "size", .type = "int", .variadic = 0 },
};
static const PS_ProtoParamDesc DEBUG_BINARY_WRITE_PARAMS[] = {
  { .name = "bytes", .type = "list<byte>", .variadic = 0 },
};
static const PS_ProtoParamDesc DEBUG_BINARY_SEEK_PARAMS[] = {
  { .name = "pos", .type = "int", .variadic = 0 },
};
static const PS_ProtoMethodDesc DEBUG_BINARY_METHODS[] = {
  { .name = "read", .params = DEBUG_BINARY_READ_PARAMS, .param_count = 1, .ret_type = "list<byte>" },
  { .name = "write", .params = DEBUG_BINARY_WRITE_PARAMS, .param_count = 1, .ret_type = "void" },
  { .name = "tell", .params = NULL, .param_count = 0, .ret_type = "int" },
  { .name = "seek", .params = DEBUG_BINARY_SEEK_PARAMS, .param_count = 1, .ret_type = "void" },
  { .name = "size", .params = NULL, .param_count = 0, .ret_type = "int" },
  { .name = "name", .params = NULL, .param_count = 0, .ret_type = "string" },
  { .name = "close", .params = NULL, .param_count = 0, .ret_type = "void" },
};
static const PS_ProtoDesc DEBUG_BINARYFILE_PROTO = {
  .name = "BinaryFile",
  .parent = "Object",
  .fields = NULL,
  .field_count = 0,
  .methods = DEBUG_BINARY_METHODS,
  .method_count = sizeof(DEBUG_BINARY_METHODS) / sizeof(DEBUG_BINARY_METHODS[0]),
  .is_sealed = 1,
};

static DebugProto debug_proto_none(void) {
  DebugProto p;
  p.kind = DEBUG_PROTO_NONE;
  p.ir = NULL;
  p.native = NULL;
  return p;
}

static DebugProto debug_proto_from_ir(const PS_IR_Proto *p) {
  DebugProto out = debug_proto_none();
  if (p) {
    out.kind = DEBUG_PROTO_IR;
    out.ir = p;
  }
  return out;
}

static DebugProto debug_proto_from_native(const PS_ProtoDesc *p) {
  DebugProto out = debug_proto_none();
  if (p) {
    out.kind = DEBUG_PROTO_NATIVE;
    out.native = p;
  }
  return out;
}

static const PS_ProtoDesc *debug_builtin_proto_by_name(const char *name) {
  if (!name) return NULL;
  if (strcmp(name, "Object") == 0) return &DEBUG_OBJECT_PROTO;
  if (strcmp(name, "TextFile") == 0) return &DEBUG_TEXTFILE_PROTO;
  if (strcmp(name, "BinaryFile") == 0) return &DEBUG_BINARYFILE_PROTO;
  return NULL;
}

static const PS_ProtoDesc *debug_find_native_proto(PS_Context *ctx, const char *name) {
  const PS_ProtoDesc *bp = debug_builtin_proto_by_name(name);
  if (bp) return bp;
  if (!ctx || !name) return NULL;
  for (size_t i = 0; i < ctx->module_count; i++) {
    PS_Module *m = &ctx->modules[i].desc;
    if (!m->protos || m->proto_count == 0) continue;
    for (size_t j = 0; j < m->proto_count; j++) {
      if (m->protos[j].name && strcmp(m->protos[j].name, name) == 0) return &m->protos[j];
    }
  }
  return NULL;
}

static DebugProto debug_find_proto(PS_Context *ctx, const char *name) {
  if (!ctx || !name) return debug_proto_none();
  if (ctx->current_module) {
    const PS_IR_Proto *p = ps_ir_find_proto(ctx->current_module, name);
    if (p) return debug_proto_from_ir(p);
  }
  const PS_ProtoDesc *np = debug_find_native_proto(ctx, name);
  if (np) return debug_proto_from_native(np);
  return debug_proto_none();
}

static const PS_IR_Group *debug_find_group(PS_Context *ctx, const char *name) {
  if (!ctx || !ctx->current_module || !name) return NULL;
  return ps_ir_find_group(ctx->current_module, name);
}

static const PS_IR_Group *debug_group_from_value(PS_Value *v) {
  if (!v || v->tag != PS_V_GROUP) return NULL;
  return v->as.group_v.group;
}

static const char *debug_group_type_name(PS_Value *v) {
  if (!v || v->tag != PS_V_OBJECT) return NULL;
  const char *p = ps_object_proto_name_internal(v);
  if (!p) return NULL;
  const char *prefix = "@group:";
  size_t plen = strlen(prefix);
  if (strncmp(p, prefix, plen) == 0) return p + plen;
  return NULL;
}

static int debug_parse_bool(const char *raw, int *out) {
  if (!raw || !out) return 0;
  if (strcmp(raw, "true") == 0 || strcmp(raw, "1") == 0) {
    *out = 1;
    return 1;
  }
  if (strcmp(raw, "false") == 0 || strcmp(raw, "0") == 0) {
    *out = 0;
    return 1;
  }
  return 0;
}

static int debug_parse_int64(const char *raw, int64_t *out) {
  if (!raw || !out) return 0;
  char *end = NULL;
  long long v = strtoll(raw, &end, 0);
  if (!end || end == raw) return 0;
  *out = (int64_t)v;
  return 1;
}

static int debug_group_member_value(PS_Context *ctx, const char *base_type, const char *raw, PS_Value **out) {
  if (!ctx || !base_type || !raw || !out) return 0;
  if (strcmp(base_type, "bool") == 0) {
    int bv = 0;
    if (!debug_parse_bool(raw, &bv)) return 0;
    *out = ps_make_bool(ctx, bv);
    return *out != NULL;
  }
  if (strcmp(base_type, "byte") == 0) {
    int64_t iv = 0;
    if (!debug_parse_int64(raw, &iv)) return 0;
    *out = ps_make_byte(ctx, (uint8_t)iv);
    return *out != NULL;
  }
  if (strcmp(base_type, "int") == 0) {
    int64_t iv = 0;
    if (!debug_parse_int64(raw, &iv)) return 0;
    *out = ps_make_int(ctx, iv);
    return *out != NULL;
  }
  if (strcmp(base_type, "float") == 0) {
    char *end = NULL;
    double fv = strtod(raw, &end);
    if (!end || end == raw) return 0;
    *out = ps_make_float(ctx, fv);
    return *out != NULL;
  }
  if (strcmp(base_type, "glyph") == 0) {
    int64_t iv = 0;
    if (!debug_parse_int64(raw, &iv)) return 0;
    *out = ps_make_glyph(ctx, (uint32_t)iv);
    return *out != NULL;
  }
  if (strcmp(base_type, "string") == 0) {
    *out = ps_make_string_utf8(ctx, raw, strlen(raw));
    return *out != NULL;
  }
  return 0;
}

static int debug_group_value_matches(PS_Value *v, const char *base_type, const char *raw) {
  if (!v || !base_type || !raw) return 0;
  if (strcmp(base_type, "bool") == 0) {
    int bv = 0;
    if (!debug_parse_bool(raw, &bv)) return 0;
    return v->tag == PS_V_BOOL && v->as.bool_v == bv;
  }
  if (strcmp(base_type, "byte") == 0) {
    int64_t iv = 0;
    if (!debug_parse_int64(raw, &iv)) return 0;
    return v->tag == PS_V_BYTE && v->as.byte_v == (uint8_t)iv;
  }
  if (strcmp(base_type, "int") == 0) {
    int64_t iv = 0;
    if (!debug_parse_int64(raw, &iv)) return 0;
    return v->tag == PS_V_INT && v->as.int_v == iv;
  }
  if (strcmp(base_type, "float") == 0) {
    char *end = NULL;
    double fv = strtod(raw, &end);
    if (!end || end == raw) return 0;
    return v->tag == PS_V_FLOAT && v->as.float_v == fv;
  }
  if (strcmp(base_type, "glyph") == 0) {
    int64_t iv = 0;
    if (!debug_parse_int64(raw, &iv)) return 0;
    return v->tag == PS_V_GLYPH && v->as.glyph_v == (uint32_t)iv;
  }
  if (strcmp(base_type, "string") == 0) {
    if (v->tag != PS_V_STRING || !v->as.string_v.ptr) return 0;
    return strcmp(v->as.string_v.ptr, raw) == 0;
  }
  return 0;
}

static int debug_find_group_member_for_value(PS_Context *ctx, PS_Value *v, const PS_IR_Group **out_group, const PS_IR_GroupMember **out_member) {
  if (!ctx || !ctx->current_module || !v) return 0;
  size_t count = ps_ir_group_count(ctx->current_module);
  for (size_t gi = 0; gi < count; gi++) {
    const PS_IR_Group *g = ps_ir_group_at(ctx->current_module, gi);
    if (!g->base_type || !g->members) continue;
    for (size_t mi = 0; mi < g->member_count; mi++) {
      const PS_IR_GroupMember *m = &g->members[mi];
      if (!m->value) continue;
      if (debug_group_value_matches(v, g->base_type, m->value)) {
        if (out_group) *out_group = g;
        if (out_member) *out_member = m;
        return 1;
      }
    }
  }
  return 0;
}

static int debug_dump_group_value(PS_Context *ctx, DebugState *st, const PS_IR_Group *g, const PS_IR_GroupMember *m) {
  if (!g || !m) return 0;
  if (!debug_printf(st, "%s.%s = ", g->name ? g->name : "<group>", m->name ? m->name : "<member>")) return 0;
  PS_Value *mv = NULL;
  if (debug_group_member_value(ctx, g->base_type ? g->base_type : "unknown", m->value ? m->value : "", &mv)) {
    int ok = debug_dump_scalar(st, mv);
    ps_value_release(mv);
    return ok;
  }
  return debug_write(st, "unknown(value)");
}

static int debug_dump_group_type(PS_Context *ctx, DebugState *st, const PS_IR_Group *g, int indent) {
  if (!g) return 0;
  const char *name = g->name ? g->name : "<group>";
  const char *base = g->base_type ? g->base_type : "unknown";
  if (!debug_printf(st, "group %s %s {", base, name)) return 0;
  if (g->member_count == 0) return debug_write(st, "}");
  if (!debug_write(st, "\n")) return 0;
  for (size_t i = 0; i < g->member_count; i++) {
    const PS_IR_GroupMember *m = &g->members[i];
    debug_indent(st, indent + 2);
    if (!debug_printf(st, "%s = ", m->name ? m->name : "")) return 0;
    PS_Value *mv = NULL;
    if (debug_group_member_value(ctx, base, m->value ? m->value : "", &mv)) {
      if (!debug_dump_scalar(st, mv)) {
        ps_value_release(mv);
        return 0;
      }
      ps_value_release(mv);
    } else {
      if (!debug_write(st, "unknown(value)")) return 0;
    }
    if (!debug_write(st, "\n")) return 0;
  }
  debug_indent(st, indent);
  return debug_write(st, "}");
}

static const char *debug_proto_name(DebugProto p) {
  if (p.kind == DEBUG_PROTO_IR && p.ir) return p.ir->name;
  if (p.kind == DEBUG_PROTO_NATIVE && p.native) return p.native->name;
  return NULL;
}

static const char *debug_proto_parent(DebugProto p) {
  if (p.kind == DEBUG_PROTO_IR && p.ir) return p.ir->parent;
  if (p.kind == DEBUG_PROTO_NATIVE && p.native) return p.native->parent;
  return NULL;
}

static int debug_proto_is_sealed(DebugProto p) {
  if (p.kind == DEBUG_PROTO_IR && p.ir) return p.ir->is_sealed;
  if (p.kind == DEBUG_PROTO_NATIVE && p.native) return p.native->is_sealed;
  return 0;
}

static size_t debug_proto_field_count(DebugProto p) {
  if (p.kind == DEBUG_PROTO_IR && p.ir) return p.ir->field_count;
  if (p.kind == DEBUG_PROTO_NATIVE && p.native) return p.native->field_count;
  return 0;
}

static void debug_proto_field(DebugProto p, size_t idx, const char **name, const char **type) {
  if (name) *name = NULL;
  if (type) *type = NULL;
  if (p.kind == DEBUG_PROTO_IR && p.ir && idx < p.ir->field_count) {
    if (name) *name = p.ir->fields[idx].name;
    if (type) *type = p.ir->fields[idx].type;
  } else if (p.kind == DEBUG_PROTO_NATIVE && p.native && idx < p.native->field_count) {
    if (name) *name = p.native->fields[idx].name;
    if (type) *type = p.native->fields[idx].type;
  }
}

static size_t debug_proto_method_count(DebugProto p) {
  if (p.kind == DEBUG_PROTO_IR && p.ir) return p.ir->method_count;
  if (p.kind == DEBUG_PROTO_NATIVE && p.native) return p.native->method_count;
  return 0;
}

static const char *debug_proto_method_name(DebugProto p, size_t idx) {
  if (p.kind == DEBUG_PROTO_IR && p.ir && idx < p.ir->method_count) return p.ir->methods[idx].name;
  if (p.kind == DEBUG_PROTO_NATIVE && p.native && idx < p.native->method_count) return p.native->methods[idx].name;
  return NULL;
}

static const char *debug_proto_method_ret_type(DebugProto p, size_t idx) {
  if (p.kind == DEBUG_PROTO_IR && p.ir && idx < p.ir->method_count) return p.ir->methods[idx].ret_type;
  if (p.kind == DEBUG_PROTO_NATIVE && p.native && idx < p.native->method_count) return p.native->methods[idx].ret_type;
  return NULL;
}

static size_t debug_proto_method_param_count(DebugProto p, size_t idx) {
  if (p.kind == DEBUG_PROTO_IR && p.ir && idx < p.ir->method_count) return p.ir->methods[idx].param_count;
  if (p.kind == DEBUG_PROTO_NATIVE && p.native && idx < p.native->method_count) return p.native->methods[idx].param_count;
  return 0;
}

static void debug_proto_method_param(DebugProto p, size_t midx, size_t pidx, const char **name, const char **type, int *variadic) {
  if (name) *name = NULL;
  if (type) *type = NULL;
  if (variadic) *variadic = 0;
  if (p.kind == DEBUG_PROTO_IR && p.ir && midx < p.ir->method_count) {
    if (pidx < p.ir->methods[midx].param_count) {
      if (name) *name = p.ir->methods[midx].params[pidx].name;
      if (type) *type = p.ir->methods[midx].params[pidx].type;
      if (variadic) *variadic = p.ir->methods[midx].params[pidx].variadic;
    }
  } else if (p.kind == DEBUG_PROTO_NATIVE && p.native && midx < p.native->method_count) {
    if (pidx < p.native->methods[midx].param_count) {
      if (name) *name = p.native->methods[midx].params[pidx].name;
      if (type) *type = p.native->methods[midx].params[pidx].type;
      if (variadic) *variadic = p.native->methods[midx].params[pidx].variadic;
    }
  }
}

static void debug_print_chain(DebugState *st, PS_Context *ctx, const char *proto_name) {
  if (!proto_name) {
    debug_write(st, "delegation: <unknown>");
    return;
  }
  debug_write(st, "delegation: ");
  const char *cur = proto_name;
  for (size_t depth = 0; depth < 64; depth++) {
    if (depth > 0) debug_write(st, " -> ");
    DebugProto p = debug_find_proto(ctx, cur);
    if (debug_proto_is_sealed(p)) debug_write(st, "sealed ");
    debug_write(st, cur);
    const char *parent = debug_proto_parent(p);
    if (!parent || parent[0] == '\0') break;
    cur = parent;
  }
}

static int debug_is_ref_type(PS_Value *v) {
  if (!v) return 0;
  return v->tag == PS_V_LIST || v->tag == PS_V_MAP || v->tag == PS_V_OBJECT || v->tag == PS_V_VIEW || v->tag == PS_V_EXCEPTION ||
         v->tag == PS_V_FILE;
}

static const char *debug_file_proto_name(PS_Value *v) {
  if (!v || v->tag != PS_V_FILE) return "TextFile";
  return (v->as.file_v.flags & PS_FILE_BINARY) ? "BinaryFile" : "TextFile";
}

static PS_Value *debug_exception_field(PS_Context *ctx, PS_Value *ex, const char *name) {
  if (!ex || ex->tag != PS_V_EXCEPTION || !name) return NULL;
  if (strcmp(name, "file") == 0) return ex->as.exc_v.file;
  if (strcmp(name, "line") == 0) return ps_make_int(ctx, ex->as.exc_v.line);
  if (strcmp(name, "column") == 0) return ps_make_int(ctx, ex->as.exc_v.column);
  if (strcmp(name, "message") == 0) return ex->as.exc_v.message;
  if (strcmp(name, "cause") == 0) return ex->as.exc_v.cause;
  if (strcmp(name, "code") == 0) return ex->as.exc_v.code;
  if (strcmp(name, "category") == 0) return ex->as.exc_v.category;
  if (ex->as.exc_v.fields) {
    return ps_object_get_str_internal(ctx, ex->as.exc_v.fields, name, strlen(name));
  }
  return NULL;
}

static int debug_dump_value(PS_Context *ctx, DebugState *st, PS_Value *v, int depth, int indent);
static int debug_dump_native(PS_Context *ctx, DebugState *st, PS_Value *v, int depth, int indent);

typedef struct {
  PS_Context *ctx;
  DebugState *st;
} DebugWriterState;

static int debug_writer_write(void *ud, const char *s) {
  DebugWriterState *ws = (DebugWriterState *)ud;
  return debug_write(ws->st, s);
}

static int debug_writer_printf(void *ud, const char *fmt, ...) {
  DebugWriterState *ws = (DebugWriterState *)ud;
  if (!ws || !ws->st || ws->st->io_error) return 0;
  va_list args;
  va_start(args, fmt);
  int ok = vfprintf(ws->st->out, fmt, args) >= 0;
  va_end(args);
  if (!ok) ws->st->io_error = 1;
  return ok;
}

static void debug_writer_indent(void *ud, int spaces) {
  DebugWriterState *ws = (DebugWriterState *)ud;
  debug_indent(ws->st, spaces);
}

static int debug_writer_dump_value(void *ud, PS_Value *value, int depth, int indent) {
  DebugWriterState *ws = (DebugWriterState *)ud;
  return debug_dump_value(ws->ctx, ws->st, value, depth, indent);
}

static int debug_dump_ref_desc(PS_Context *ctx, DebugState *st, PS_Value *v) {
  if (!v) return debug_write(st, "unknown(null)");
  if (v->tag == PS_V_BOOL || v->tag == PS_V_BYTE || v->tag == PS_V_INT || v->tag == PS_V_FLOAT ||
      v->tag == PS_V_GLYPH || v->tag == PS_V_STRING) {
    return debug_dump_scalar(st, v);
  }
  if (v->tag == PS_V_LIST) {
    size_t len = v->as.list_v.len;
    return debug_printf(st, "%s(len=%zu) [...]", list_type_name(v), len);
  }
  if (v->tag == PS_V_MAP) {
    size_t len = ps_map_len(v);
    const char *tname = map_type_name(v);
    return debug_printf(st, "%s(len=%zu) {...}", tname, len);
  }
  if (v->tag == PS_V_VIEW) {
    size_t len = v->as.view_v.len;
    return debug_printf(st, "%s(len=%zu) [...]", view_type_name(v), len);
  }
  if (v->tag == PS_V_OBJECT || v->tag == PS_V_EXCEPTION) {
    if (debug_dump_native(ctx, st, v, (int)st->max_depth, 0)) return 1;
    const char *proto_name = NULL;
    if (v->tag == PS_V_EXCEPTION) proto_name = v->as.exc_v.type_name;
    if (!proto_name && v->tag == PS_V_OBJECT) proto_name = ps_object_proto_name_internal(v);
    return debug_printf(st, "object<%s>", proto_name ? proto_name : "unknown");
  }
  if (v->tag == PS_V_FILE) {
    return debug_printf(st, "object<%s>", debug_file_proto_name(v));
  }
  return debug_printf(st, "unknown(%s)", value_tag_name(v));
}

static int debug_dump_native(PS_Context *ctx, DebugState *st, PS_Value *v, int depth, int indent) {
  if (!ctx || !st || !v) return 0;
  if (!ctx->modules || ctx->module_count == 0) return 0;
  DebugWriterState ws;
  ws.ctx = ctx;
  ws.st = st;
  PS_DebugWriter writer;
  memset(&writer, 0, sizeof(writer));
  writer.ud = &ws;
  writer.write = debug_writer_write;
  writer.printf = debug_writer_printf;
  writer.indent = debug_writer_indent;
  writer.dump_value = debug_writer_dump_value;
  writer.max_depth = st->max_depth;
  writer.max_items = st->max_items;
  writer.max_string = st->max_string;
  for (size_t i = 0; i < ctx->module_count; i++) {
    PS_Module *m = &ctx->modules[i].desc;
    if (!m->debug_dump) continue;
    if (m->debug_dump(ctx, v, &writer, depth, indent)) return 1;
  }
  return 0;
}

static int debug_dump_scalar(DebugState *st, PS_Value *v) {
  if (!v) return debug_write(st, "unknown(null)");
  switch (v->tag) {
    case PS_V_BOOL:
      return debug_printf(st, "bool(%s)", v->as.bool_v ? "true" : "false");
    case PS_V_BYTE:
      return debug_printf(st, "byte(%u)", (unsigned)v->as.byte_v);
    case PS_V_INT:
      return debug_printf(st, "int(%lld)", (long long)v->as.int_v);
    case PS_V_FLOAT: {
      char buf[64];
      snprintf(buf, sizeof(buf), "%.17g", v->as.float_v);
      return debug_printf(st, "float(%s)", buf);
    }
    case PS_V_GLYPH:
      return debug_printf(st, "glyph(U+%04X)", (unsigned)v->as.glyph_v);
    case PS_V_STRING: {
      size_t glyphs = ps_utf8_glyph_len((const uint8_t *)v->as.string_v.ptr, v->as.string_v.len);
      int truncated = 0;
      if (!debug_printf(st, "string(len=%zu) \"", glyphs)) return 0;
      if (!debug_write_string(st, v->as.string_v.ptr ? v->as.string_v.ptr : "", v->as.string_v.len, st->max_string, &truncated)) return 0;
      if (!debug_write(st, "\"")) return 0;
      if (truncated) {
        if (!debug_write(st, " \xE2\x80\xA6 (truncated)")) return 0;
      }
      return 1;
    }
    default:
      return debug_printf(st, "unknown(%s)", value_tag_name(v));
  }
}

static int debug_dump_list(PS_Context *ctx, DebugState *st, PS_Value *v, int depth, int indent) {
  size_t len = v->as.list_v.len;
  if (!debug_printf(st, "%s(len=%zu) [", list_type_name(v), len)) return 0;
  if (depth >= (int)st->max_depth) {
    if (!debug_write(st, "\n")) return 0;
    debug_indent(st, indent + 2);
    if (!debug_write(st, "\xE2\x80\xA6 (truncated)\n")) return 0;
    debug_indent(st, indent);
    return debug_write(st, "]");
  }
  if (!debug_write(st, "\n")) return 0;
  size_t max = st->max_items;
  size_t shown = len < max ? len : max;
  for (size_t i = 0; i < shown; i++) {
    debug_indent(st, indent + 2);
    if (!debug_printf(st, "[%zu] ", i)) return 0;
    if (!debug_dump_value(ctx, st, v->as.list_v.items[i], depth + 1, indent + 2)) return 0;
    if (!debug_write(st, "\n")) return 0;
  }
  if (len > shown) {
    debug_indent(st, indent + 2);
    if (!debug_write(st, "\xE2\x80\xA6 (truncated)\n")) return 0;
  }
  debug_indent(st, indent);
  return debug_write(st, "]");
}

static int debug_dump_map_key(PS_Context *ctx, DebugState *st, PS_Value *key) {
  (void)ctx;
  if (!key) return debug_write(st, "unknown(null)");
  if (key->tag == PS_V_STRING) {
    int truncated = 0;
    if (!debug_write(st, "\"")) return 0;
    if (!debug_write_string(st, key->as.string_v.ptr ? key->as.string_v.ptr : "", key->as.string_v.len, st->max_string, &truncated)) return 0;
    if (!debug_write(st, "\"")) return 0;
    if (truncated) return debug_write(st, " \xE2\x80\xA6 (truncated)");
    return 1;
  }
  return debug_dump_scalar(st, key);
}

static int debug_dump_map(PS_Context *ctx, DebugState *st, PS_Value *v, int depth, int indent) {
  size_t len = ps_map_len(v);
  const char *tname = map_type_name(v);
  if (!debug_printf(st, "%s(len=%zu) {", tname, len)) return 0;
  if (depth >= (int)st->max_depth) {
    if (!debug_write(st, "\n")) return 0;
    debug_indent(st, indent + 2);
    if (!debug_write(st, "\xE2\x80\xA6 (truncated)\n")) return 0;
    debug_indent(st, indent);
    return debug_write(st, "}");
  }
  if (!debug_write(st, "\n")) return 0;
  size_t max = st->max_items;
  size_t shown = len < max ? len : max;
  for (size_t i = 0; i < shown; i++) {
    PS_Value *k = NULL;
    PS_Value *val = NULL;
    if (ps_map_entry(ctx, v, i, &k, &val) != PS_OK) return 0;
    debug_indent(st, indent + 2);
    if (!debug_write(st, "[")) return 0;
    if (!debug_dump_map_key(ctx, st, k)) return 0;
    if (!debug_write(st, "] ")) return 0;
    if (!debug_dump_value(ctx, st, val, depth + 1, indent + 2)) return 0;
    if (!debug_write(st, "\n")) return 0;
  }
  if (len > shown) {
    debug_indent(st, indent + 2);
    if (!debug_write(st, "\xE2\x80\xA6 (truncated)\n")) return 0;
  }
  debug_indent(st, indent);
  return debug_write(st, "}");
}

static int debug_dump_view(PS_Context *ctx, DebugState *st, PS_Value *v, int depth, int indent) {
  size_t len = v->as.view_v.len;
  if (!debug_printf(st, "%s(len=%zu) [", view_type_name(v), len)) return 0;
  if (depth >= (int)st->max_depth) {
    if (!debug_write(st, "\n")) return 0;
    debug_indent(st, indent + 2);
    if (!debug_write(st, "\xE2\x80\xA6 (truncated)\n")) return 0;
    debug_indent(st, indent);
    return debug_write(st, "]");
  }
  if (!debug_write(st, "\n")) return 0;
  size_t max = st->max_items;
  size_t shown = len < max ? len : max;
  PS_Value *src = v->as.view_v.source;
  PS_Value **borrowed = v->as.view_v.borrowed_items;
  for (size_t i = 0; i < shown; i++) {
    debug_indent(st, indent + 2);
    if (!debug_printf(st, "[%zu] ", i)) return 0;
    if (src && src->tag == PS_V_LIST) {
      size_t idx = v->as.view_v.offset + i;
      PS_Value *item = idx < src->as.list_v.len ? src->as.list_v.items[idx] : NULL;
      if (!debug_dump_value(ctx, st, item, depth + 1, indent + 2)) return 0;
    } else if (src && src->tag == PS_V_STRING) {
      size_t glyph_index = v->as.view_v.offset + i;
      uint32_t cp = ps_utf8_glyph_at((const uint8_t *)src->as.string_v.ptr, src->as.string_v.len, glyph_index);
      PS_Value tmp;
      memset(&tmp, 0, sizeof(tmp));
      tmp.tag = PS_V_GLYPH;
      tmp.as.glyph_v = cp;
      if (!debug_dump_scalar(st, &tmp)) return 0;
    } else if (!src && borrowed) {
      size_t idx = v->as.view_v.offset + i;
      PS_Value *item = borrowed[idx];
      if (!debug_dump_value(ctx, st, item, depth + 1, indent + 2)) return 0;
    } else {
      if (!debug_write(st, "unknown(view)")) return 0;
    }
    if (!debug_write(st, "\n")) return 0;
  }
  if (len > shown) {
    debug_indent(st, indent + 2);
    if (!debug_write(st, "\xE2\x80\xA6 (truncated)\n")) return 0;
  }
  debug_indent(st, indent);
  return debug_write(st, "]");
}

static int debug_dump_object_fields(PS_Context *ctx, DebugState *st, PS_Value *v, DebugProto proto, int depth, int indent) {
  DebugProto cur = proto;
  for (size_t guard = 0; cur.kind != DEBUG_PROTO_NONE && guard < 64; guard++) {
    size_t field_count = debug_proto_field_count(cur);
    for (size_t i = 0; i < field_count; i++) {
      const char *fname = NULL;
      const char *ftype = NULL;
      debug_proto_field(cur, i, &fname, &ftype);
      fname = fname ? fname : "";
      ftype = ftype ? ftype : "unknown";
      PS_Value *fval = NULL;
      PS_Value *tmp_int = NULL;
      if (v->tag == PS_V_EXCEPTION) {
        fval = debug_exception_field(ctx, v, fname);
        if ((strcmp(fname, "line") == 0 || strcmp(fname, "column") == 0) && fval && fval->tag == PS_V_INT) {
          tmp_int = fval;
        }
      } else if (v->tag == PS_V_OBJECT) {
        fval = ps_object_get_str_internal(ctx, v, fname, strlen(fname));
      }
      debug_indent(st, indent);
      if (!debug_printf(st, "[%s] %s : %s = ", debug_proto_name(cur) ? debug_proto_name(cur) : "<unknown>", fname, ftype)) return 0;
      if (fval) {
        if (!debug_dump_value(ctx, st, fval, depth + 1, indent)) return 0;
      } else {
        if (!debug_write(st, "unknown(missing)")) return 0;
      }
      if (tmp_int) ps_value_release(tmp_int);
      if (!debug_write(st, "\n")) return 0;
    }
    const char *parent = debug_proto_parent(cur);
    if (!parent) break;
    cur = debug_find_proto(ctx, parent);
  }
  return 1;
}

static DebugProto debug_find_parent_proto(PS_Context *ctx, DebugProto proto) {
  const char *parent = debug_proto_parent(proto);
  if (!parent) return debug_proto_none();
  return debug_find_proto(ctx, parent);
}

static const char *debug_find_override(PS_Context *ctx, DebugProto proto, const char *method_name) {
  DebugProto cur = debug_find_parent_proto(ctx, proto);
  while (cur.kind != DEBUG_PROTO_NONE) {
    size_t method_count = debug_proto_method_count(cur);
    for (size_t i = 0; i < method_count; i++) {
      const char *mname = debug_proto_method_name(cur, i);
      if (mname && strcmp(mname, method_name) == 0) {
        return debug_proto_name(cur);
      }
    }
    cur = debug_find_parent_proto(ctx, cur);
  }
  return NULL;
}

static int debug_dump_object_methods(PS_Context *ctx, DebugState *st, DebugProto proto, int indent) {
  DebugProto cur = proto;
  for (size_t guard = 0; cur.kind != DEBUG_PROTO_NONE && guard < 64; guard++) {
    size_t method_count = debug_proto_method_count(cur);
    for (size_t i = 0; i < method_count; i++) {
      const char *mname = debug_proto_method_name(cur, i);
      mname = mname ? mname : "";
      debug_indent(st, indent);
      if (!debug_printf(st, "[%s] %s(", debug_proto_name(cur) ? debug_proto_name(cur) : "<unknown>", mname)) return 0;
      size_t param_count = debug_proto_method_param_count(cur, i);
      for (size_t p = 0; p < param_count; p++) {
        const char *pname = NULL;
        const char *ptype = NULL;
        int variadic = 0;
        debug_proto_method_param(cur, i, p, &pname, &ptype, &variadic);
        pname = pname ? pname : "";
        ptype = ptype ? ptype : "unknown";
        if (p > 0) {
          if (!debug_write(st, ", ")) return 0;
        }
        if (variadic) {
          if (!debug_printf(st, "...%s:%s", pname, ptype)) return 0;
        } else {
          if (!debug_printf(st, "%s:%s", pname, ptype)) return 0;
        }
      }
      const char *ret = debug_proto_method_ret_type(cur, i);
      if (!debug_printf(st, ") : %s", ret ? ret : "unknown")) return 0;
      const char *over = debug_find_override(ctx, cur, mname);
      if (over) {
        if (!debug_printf(st, "  (overrides %s.%s)", over, mname)) return 0;
      }
      if (!debug_write(st, "\n")) return 0;
    }
    const char *parent = debug_proto_parent(cur);
    if (!parent) break;
    cur = debug_find_proto(ctx, parent);
  }
  return 1;
}

static int debug_dump_object(PS_Context *ctx, DebugState *st, PS_Value *v, int depth, int indent) {
  const char *proto_name = NULL;
  if (v->tag == PS_V_EXCEPTION) proto_name = v->as.exc_v.type_name;
  if (!proto_name && v->tag == PS_V_OBJECT) proto_name = ps_object_proto_name_internal(v);
  if (!proto_name && v->tag == PS_V_FILE) proto_name = debug_file_proto_name(v);
  const char *name = proto_name ? proto_name : "unknown";
  if (!debug_printf(st, "object<%s> {", name)) return 0;
  if (depth >= (int)st->max_depth) {
    if (!debug_write(st, "\n")) return 0;
    debug_indent(st, indent + 2);
    if (!debug_write(st, "\xE2\x80\xA6 (truncated)\n")) return 0;
    debug_indent(st, indent);
    return debug_write(st, "}");
  }
  if (!debug_write(st, "\n")) return 0;
  debug_indent(st, indent + 2);
  debug_print_chain(st, ctx, proto_name);
  if (!debug_write(st, "\n")) return 0;
  debug_indent(st, indent + 2);
  if (!debug_write(st, "native: ")) return 0;
  if (v->tag == PS_V_FILE) {
    PS_File *f = &v->as.file_v;
    const int is_std = (f->flags & PS_FILE_STD) != 0;
    const int closed = f->closed || !f->fp;
    const char *path = f->path ? f->path : "";
    int truncated = 0;
    if (!debug_printf(st, "%s(closed=%s, std=%s, path=\"", name, closed ? "true" : "false", is_std ? "true" : "false")) return 0;
    if (!debug_write_string(st, path, strlen(path), st->max_string, &truncated)) return 0;
    if (!debug_write(st, "\")")) return 0;
    if (truncated && !debug_write(st, " ... (truncated)")) return 0;
    if (!debug_write(st, "\n")) return 0;
  } else if (debug_dump_native(ctx, st, v, depth + 1, indent + 2)) {
    if (!debug_write(st, "\n")) return 0;
  } else {
    if (!debug_write(st, "<none>\n")) return 0;
  }
  debug_indent(st, indent + 2);
  if (!debug_write(st, "fields:\n")) return 0;
  DebugProto proto = debug_find_proto(ctx, proto_name);
  if (proto.kind != DEBUG_PROTO_NONE) {
    if (!debug_dump_object_fields(ctx, st, v, proto, depth, indent + 4)) return 0;
  } else {
    debug_indent(st, indent + 4);
    if (!debug_write(st, "\xE2\x80\xA6 (truncated)\n")) return 0;
  }
  debug_indent(st, indent + 2);
  if (!debug_write(st, "methods:\n")) return 0;
  if (proto.kind != DEBUG_PROTO_NONE) {
    if (!debug_dump_object_methods(ctx, st, proto, indent + 4)) return 0;
  } else {
    debug_indent(st, indent + 4);
    if (!debug_write(st, "\xE2\x80\xA6 (truncated)\n")) return 0;
  }
  debug_indent(st, indent);
  return debug_write(st, "}");
}

static int debug_dump_value(PS_Context *ctx, DebugState *st, PS_Value *v, int depth, int indent) {
  if (v && v->tag == PS_V_GROUP) {
    const PS_IR_Group *g = debug_group_from_value(v);
    if (g) return debug_dump_group_type(ctx, st, g, indent);
    return debug_write(st, "unknown(group)");
  }
  const char *group_type = debug_group_type_name(v);
  if (group_type) {
    const PS_IR_Group *g = debug_find_group(ctx, group_type);
    if (g) return debug_dump_group_type(ctx, st, g, indent);
    return debug_write(st, "unknown(group)");
  }
  if (debug_is_ref_type(v)) {
    size_t id = 0;
    if (debug_seen_find(st, v, &id)) {
      if (!debug_printf(st, "@ref#%zu ", id)) return 0;
      return debug_dump_ref_desc(ctx, st, v);
    }
    if (!debug_seen_add(st, v, &id)) return debug_printf(st, "unknown(cycle)");
  }
  if (!v) return debug_write(st, "unknown(null)");
  if (v->tag == PS_V_BOOL || v->tag == PS_V_BYTE || v->tag == PS_V_INT || v->tag == PS_V_FLOAT ||
      v->tag == PS_V_GLYPH || v->tag == PS_V_STRING) {
    const PS_IR_Group *g = NULL;
    const PS_IR_GroupMember *m = NULL;
    if (debug_find_group_member_for_value(ctx, v, &g, &m)) {
      return debug_dump_group_value(ctx, st, g, m);
    }
  }
  switch (v->tag) {
    case PS_V_LIST:
      return debug_dump_list(ctx, st, v, depth, indent);
    case PS_V_MAP:
      return debug_dump_map(ctx, st, v, depth, indent);
    case PS_V_VIEW:
      return debug_dump_view(ctx, st, v, depth, indent);
    case PS_V_OBJECT:
    case PS_V_EXCEPTION:
    case PS_V_FILE:
      return debug_dump_object(ctx, st, v, depth, indent);
    default:
      return debug_dump_scalar(st, v);
  }
}

static size_t debug_parse_limit(const char *env, size_t def) {
  if (!env || env[0] == '\0') return def;
  char *end = NULL;
  long v = strtol(env, &end, 10);
  if (!end || *end != '\0' || v <= 0) return def;
  return (size_t)v;
}

static PS_Status debug_dump(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)out;
  if (!ctx || !argv || argc != 1) {
    ps_throw(ctx, PS_ERR_TYPE, "Debug.dump expects 1 argument");
    return PS_ERR;
  }
  DebugState st;
  memset(&st, 0, sizeof(st));
  st.out = stderr;
  st.max_depth = debug_parse_limit(getenv("PS_DEBUG_MAX_DEPTH"), 6);
  st.max_items = debug_parse_limit(getenv("PS_DEBUG_MAX_ITEMS"), 100);
  st.max_string = debug_parse_limit(getenv("PS_DEBUG_MAX_STRING"), 200);
  if (!debug_dump_value(ctx, &st, argv[0], 0, 0)) {
    free(st.seen);
    if (st.io_error) ps_throw(ctx, PS_ERR_INTERNAL, "debug output failed");
    return PS_ERR;
  }
  if (!debug_write(&st, "\n")) {
    free(st.seen);
    ps_throw(ctx, PS_ERR_INTERNAL, "debug output failed");
    return PS_ERR;
  }
  free(st.seen);
  return PS_OK;
}

PS_Status ps_module_init_Debug(PS_Context *ctx, PS_Module *out) {
  (void)ctx;
  if (!out) return PS_ERR;
  static const PS_NativeFnDesc fns[] = {
    { "dump", debug_dump, 1, PS_T_VOID, NULL, 0 },
  };
  out->module_name = "Debug";
  out->api_version = PS_API_VERSION;
  out->fn_count = sizeof(fns) / sizeof(fns[0]);
  out->fns = fns;
  return PS_OK;
}
