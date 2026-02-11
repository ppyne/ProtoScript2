#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ps/ps_api.h"

static const char *JSON_KIND_KEY = "__json_kind";
static const char *JSON_VALUE_KEY = "__json_value";

typedef struct {
  char *buf;
  size_t len;
  size_t cap;
} StrBuf;

static void sb_init(StrBuf *sb) {
  sb->buf = NULL;
  sb->len = 0;
  sb->cap = 0;
}

static int sb_reserve(StrBuf *sb, size_t need) {
  if (need <= sb->cap) return 1;
  size_t cap = sb->cap == 0 ? 64 : sb->cap * 2;
  while (cap < need) cap *= 2;
  char *n = (char *)realloc(sb->buf, cap);
  if (!n) return 0;
  sb->buf = n;
  sb->cap = cap;
  return 1;
}

static int sb_append(StrBuf *sb, const char *s, size_t n) {
  if (!sb_reserve(sb, sb->len + n + 1)) return 0;
  memcpy(sb->buf + sb->len, s, n);
  sb->len += n;
  sb->buf[sb->len] = '\0';
  return 1;
}

static int sb_append_c(StrBuf *sb, char c) {
  return sb_append(sb, &c, 1);
}

static void sb_free(StrBuf *sb) {
  free(sb->buf);
  sb->buf = NULL;
  sb->len = 0;
  sb->cap = 0;
}

static PS_Value *json_make_value(PS_Context *ctx, const char *kind, PS_Value *value) {
  PS_Value *obj = ps_make_object(ctx);
  if (!obj) return NULL;
  PS_Value *k = ps_make_string_utf8(ctx, kind, strlen(kind));
  if (!k) {
    ps_value_release(obj);
    return NULL;
  }
  if (ps_object_set_str(ctx, obj, JSON_KIND_KEY, strlen(JSON_KIND_KEY), k) != PS_OK) {
    ps_value_release(k);
    ps_value_release(obj);
    return NULL;
  }
  ps_value_release(k);
  if (value) {
    if (ps_object_set_str(ctx, obj, JSON_VALUE_KEY, strlen(JSON_VALUE_KEY), value) != PS_OK) {
      ps_value_release(obj);
      return NULL;
    }
  }
  return obj;
}

static int json_value_kind(PS_Context *ctx, PS_Value *v, const char **kind, PS_Value **out_val) {
  if (!v || ps_typeof(v) != PS_T_OBJECT) return 0;
  PS_Value *k = ps_object_get_str(ctx, v, JSON_KIND_KEY, strlen(JSON_KIND_KEY));
  if (!k || ps_typeof(k) != PS_T_STRING) return 0;
  if (kind) *kind = ps_string_ptr(k);
  if (out_val) *out_val = ps_object_get_str(ctx, v, JSON_VALUE_KEY, strlen(JSON_VALUE_KEY));
  return 1;
}

static int json_encode_value(PS_Context *ctx, PS_Value *v, StrBuf *sb);

static int encode_string(PS_Context *ctx, const char *s, size_t len, StrBuf *sb) {
  (void)ctx;
  if (!sb_append_c(sb, '"')) return 0;
  for (size_t i = 0; i < len; i++) {
    unsigned char c = (unsigned char)s[i];
    switch (c) {
      case '"': if (!sb_append(sb, "\\\"", 2)) return 0; break;
      case '\\': if (!sb_append(sb, "\\\\", 2)) return 0; break;
      case '\b': if (!sb_append(sb, "\\b", 2)) return 0; break;
      case '\f': if (!sb_append(sb, "\\f", 2)) return 0; break;
      case '\n': if (!sb_append(sb, "\\n", 2)) return 0; break;
      case '\r': if (!sb_append(sb, "\\r", 2)) return 0; break;
      case '\t': if (!sb_append(sb, "\\t", 2)) return 0; break;
      default:
        if (c < 0x20) {
          char buf[7];
          snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)c);
          if (!sb_append(sb, buf, 6)) return 0;
        } else {
          if (!sb_append_c(sb, (char)c)) return 0;
        }
        break;
    }
  }
  return sb_append_c(sb, '"');
}

static int encode_number(PS_Context *ctx, double v, StrBuf *sb) {
  if (!isfinite(v)) {
    ps_throw(ctx, PS_ERR_TYPE, "invalid JSON number");
    return 0;
  }
  if (v == 0.0 && signbit(v)) return sb_append(sb, "-0", 2);
  char buf[64];
  int n = snprintf(buf, sizeof(buf), "%.17g", v);
  if (n <= 0) return 0;
  return sb_append(sb, buf, (size_t)n);
}

static int encode_object(PS_Context *ctx, PS_Value *obj, StrBuf *sb) {
  if (!obj || ps_typeof(obj) != PS_T_MAP) {
    ps_throw(ctx, PS_ERR_TYPE, "object expects map<string,JSONValue>");
    return 0;
  }
  size_t len = ps_map_len(obj);
  if (!sb_append_c(sb, '{')) return 0;
  for (size_t i = 0; i < len; i++) {
    PS_Value *key = NULL;
    PS_Value *val = NULL;
    if (ps_map_entry(ctx, obj, i, &key, &val) != PS_OK) return 0;
    if (!key || ps_typeof(key) != PS_T_STRING) {
      ps_throw(ctx, PS_ERR_TYPE, "object expects map<string,JSONValue>");
      return 0;
    }
    if (i > 0 && !sb_append_c(sb, ',')) return 0;
    if (!encode_string(ctx, ps_string_ptr(key), ps_string_len(key), sb)) return 0;
    if (!sb_append_c(sb, ':')) return 0;
    if (!json_encode_value(ctx, val, sb)) return 0;
  }
  return sb_append_c(sb, '}');
}

static int json_encode_value(PS_Context *ctx, PS_Value *v, StrBuf *sb) {
  if (!v) {
    ps_throw(ctx, PS_ERR_TYPE, "null value");
    return 0;
  }
  const char *kind = NULL;
  PS_Value *jval = NULL;
  if (json_value_kind(ctx, v, &kind, &jval)) {
    if (strcmp(kind, "null") == 0) return sb_append(sb, "null", 4);
    if (strcmp(kind, "bool") == 0) {
      if (!jval || ps_typeof(jval) != PS_T_BOOL) {
        ps_throw(ctx, PS_ERR_TYPE, "invalid JsonBool");
        return 0;
      }
      return sb_append(sb, ps_as_bool(jval) ? "true" : "false", ps_as_bool(jval) ? 4 : 5);
    }
    if (strcmp(kind, "number") == 0) {
      if (!jval || ps_typeof(jval) != PS_T_FLOAT) {
        ps_throw(ctx, PS_ERR_TYPE, "invalid JsonNumber");
        return 0;
      }
      return encode_number(ctx, ps_as_float(jval), sb);
    }
    if (strcmp(kind, "string") == 0) {
      if (!jval || ps_typeof(jval) != PS_T_STRING) {
        ps_throw(ctx, PS_ERR_TYPE, "invalid JsonString");
        return 0;
      }
      return encode_string(ctx, ps_string_ptr(jval), ps_string_len(jval), sb);
    }
    if (strcmp(kind, "array") == 0) {
      if (!jval || ps_typeof(jval) != PS_T_LIST) {
        ps_throw(ctx, PS_ERR_TYPE, "invalid JsonArray");
        return 0;
      }
      if (!sb_append_c(sb, '[')) return 0;
      size_t n = ps_list_len(jval);
      for (size_t i = 0; i < n; i++) {
        PS_Value *item = ps_list_get(ctx, jval, i);
        if (!item) return 0;
        if (i > 0 && !sb_append_c(sb, ',')) return 0;
        if (!json_encode_value(ctx, item, sb)) return 0;
      }
      return sb_append_c(sb, ']');
    }
    if (strcmp(kind, "object") == 0) {
      if (!jval || ps_typeof(jval) != PS_T_MAP) {
        ps_throw(ctx, PS_ERR_TYPE, "invalid JsonObject");
        return 0;
      }
      return encode_object(ctx, jval, sb);
    }
  }

  switch (ps_typeof(v)) {
    case PS_T_BOOL:
      return sb_append(sb, ps_as_bool(v) ? "true" : "false", ps_as_bool(v) ? 4 : 5);
    case PS_T_INT: {
      char buf[64];
      int n = snprintf(buf, sizeof(buf), "%lld", (long long)ps_as_int(v));
      if (n <= 0) return 0;
      return sb_append(sb, buf, (size_t)n);
    }
    case PS_T_FLOAT:
      return encode_number(ctx, ps_as_float(v), sb);
    case PS_T_STRING:
      return encode_string(ctx, ps_string_ptr(v), ps_string_len(v), sb);
    case PS_T_LIST: {
      if (!sb_append_c(sb, '[')) return 0;
      size_t n = ps_list_len(v);
      for (size_t i = 0; i < n; i++) {
        PS_Value *item = ps_list_get(ctx, v, i);
        if (!item) return 0;
        if (i > 0 && !sb_append_c(sb, ',')) return 0;
        if (!json_encode_value(ctx, item, sb)) return 0;
      }
      return sb_append_c(sb, ']');
    }
    case PS_T_MAP:
      return encode_object(ctx, v, sb);
    case PS_T_OBJECT:
      return encode_object(ctx, v, sb);
    default:
      ps_throw(ctx, PS_ERR_TYPE, "value not JSON-serializable");
      return 0;
  }
}

typedef struct {
  const char *src;
  size_t len;
  size_t pos;
  PS_Context *ctx;
} JsonParser;

static void skip_ws(JsonParser *p) {
  while (p->pos < p->len) {
    unsigned char c = (unsigned char)p->src[p->pos];
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r') p->pos++;
    else break;
  }
}

static int match(JsonParser *p, const char *kw) {
  size_t n = strlen(kw);
  if (p->pos + n > p->len) return 0;
  if (strncmp(p->src + p->pos, kw, n) != 0) return 0;
  p->pos += n;
  return 1;
}

static int append_utf8(StrBuf *sb, uint32_t cp) {
  if (cp <= 0x7F) {
    char c = (char)cp;
    return sb_append(sb, &c, 1);
  } else if (cp <= 0x7FF) {
    char out[2];
    out[0] = (char)(0xC0 | (cp >> 6));
    out[1] = (char)(0x80 | (cp & 0x3F));
    return sb_append(sb, out, 2);
  } else if (cp <= 0xFFFF) {
    char out[3];
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return sb_append(sb, out, 3);
  } else if (cp <= 0x10FFFF) {
    char out[4];
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return sb_append(sb, out, 4);
  }
  return 0;
}

static PS_Value *parse_string(JsonParser *p) {
  if (p->pos >= p->len || p->src[p->pos] != '"') return NULL;
  p->pos++;
  StrBuf sb;
  sb_init(&sb);
  while (p->pos < p->len) {
    unsigned char c = (unsigned char)p->src[p->pos++];
    if (c == '"') {
      PS_Value *s = ps_make_string_utf8(p->ctx, sb.buf ? sb.buf : "", sb.len);
      sb_free(&sb);
      return s;
    }
    if (c == '\\') {
      if (p->pos >= p->len) break;
      unsigned char e = (unsigned char)p->src[p->pos++];
      switch (e) {
        case '"': sb_append_c(&sb, '"'); break;
        case '\\': sb_append_c(&sb, '\\'); break;
        case '/': sb_append_c(&sb, '/'); break;
        case 'b': sb_append_c(&sb, '\b'); break;
        case 'f': sb_append_c(&sb, '\f'); break;
        case 'n': sb_append_c(&sb, '\n'); break;
        case 'r': sb_append_c(&sb, '\r'); break;
        case 't': sb_append_c(&sb, '\t'); break;
        case 'u': {
          if (p->pos + 4 > p->len) break;
          uint32_t cp = 0;
          for (int i = 0; i < 4; i++) {
            char h = p->src[p->pos++];
            cp <<= 4;
            if (h >= '0' && h <= '9') cp |= (uint32_t)(h - '0');
            else if (h >= 'a' && h <= 'f') cp |= (uint32_t)(10 + h - 'a');
            else if (h >= 'A' && h <= 'F') cp |= (uint32_t)(10 + h - 'A');
            else cp = 0xFFFFFFFF;
          }
          if (cp == 0xFFFFFFFF) break;
          if (cp >= 0xD800 && cp <= 0xDBFF) {
            if (p->pos + 6 <= p->len && p->src[p->pos] == '\\' && p->src[p->pos + 1] == 'u') {
              p->pos += 2;
              uint32_t lo = 0;
              for (int i = 0; i < 4; i++) {
                char h = p->src[p->pos++];
                lo <<= 4;
                if (h >= '0' && h <= '9') lo |= (uint32_t)(h - '0');
                else if (h >= 'a' && h <= 'f') lo |= (uint32_t)(10 + h - 'a');
                else if (h >= 'A' && h <= 'F') lo |= (uint32_t)(10 + h - 'A');
                else lo = 0xFFFFFFFF;
              }
              if (lo != 0xFFFFFFFF && lo >= 0xDC00 && lo <= 0xDFFF) {
                cp = 0x10000 + (((cp - 0xD800) << 10) | (lo - 0xDC00));
              }
            }
          }
          if (!append_utf8(&sb, cp)) break;
          break;
        }
        default:
          break;
      }
      continue;
    }
    if (c < 0x20) break;
    sb_append_c(&sb, (char)c);
  }
  sb_free(&sb);
  ps_throw(p->ctx, PS_ERR_TYPE, "invalid JSON string");
  return NULL;
}

static PS_Value *parse_value(JsonParser *p);

static PS_Value *parse_array(JsonParser *p) {
  if (p->src[p->pos] != '[') return NULL;
  p->pos++;
  skip_ws(p);
  PS_Value *list = ps_make_list(p->ctx);
  if (!list) return NULL;
  if (p->pos < p->len && p->src[p->pos] == ']') {
    p->pos++;
    PS_Value *out = json_make_value(p->ctx, "array", list);
    ps_value_release(list);
    return out;
  }
  while (p->pos < p->len) {
    skip_ws(p);
    PS_Value *val = parse_value(p);
    if (!val) {
      ps_value_release(list);
      return NULL;
    }
    ps_list_push(p->ctx, list, val);
    ps_value_release(val);
    skip_ws(p);
    if (p->pos < p->len && p->src[p->pos] == ',') {
      p->pos++;
      continue;
    }
    if (p->pos < p->len && p->src[p->pos] == ']') {
      p->pos++;
      PS_Value *out = json_make_value(p->ctx, "array", list);
      ps_value_release(list);
      return out;
    }
    break;
  }
  ps_value_release(list);
  ps_throw(p->ctx, PS_ERR_TYPE, "invalid JSON array");
  return NULL;
}

static PS_Value *parse_object(JsonParser *p) {
  if (p->src[p->pos] != '{') return NULL;
  p->pos++;
  skip_ws(p);
  PS_Value *obj = ps_make_map(p->ctx);
  if (!obj) return NULL;
  if (p->pos < p->len && p->src[p->pos] == '}') {
    p->pos++;
    PS_Value *out = json_make_value(p->ctx, "object", obj);
    ps_value_release(obj);
    return out;
  }
  while (p->pos < p->len) {
    skip_ws(p);
    PS_Value *key = parse_string(p);
    if (!key) {
      ps_value_release(obj);
      return NULL;
    }
    skip_ws(p);
    if (p->pos >= p->len || p->src[p->pos] != ':') {
      ps_value_release(key);
      ps_value_release(obj);
      ps_throw(p->ctx, PS_ERR_TYPE, "invalid JSON object");
      return NULL;
    }
    p->pos++;
    skip_ws(p);
    PS_Value *val = parse_value(p);
    if (!val) {
      ps_value_release(key);
      ps_value_release(obj);
      return NULL;
    }
    if (!ps_map_set(p->ctx, obj, key, val)) {
      ps_value_release(key);
      ps_value_release(val);
      ps_value_release(obj);
      return NULL;
    }
    ps_value_release(key);
    ps_value_release(val);
    skip_ws(p);
    if (p->pos < p->len && p->src[p->pos] == ',') {
      p->pos++;
      continue;
    }
    if (p->pos < p->len && p->src[p->pos] == '}') {
      p->pos++;
      PS_Value *out = json_make_value(p->ctx, "object", obj);
      ps_value_release(obj);
      return out;
    }
    break;
  }
  ps_value_release(obj);
  ps_throw(p->ctx, PS_ERR_TYPE, "invalid JSON object");
  return NULL;
}

static PS_Value *parse_number(JsonParser *p) {
  size_t start = p->pos;
  if (p->pos < p->len && p->src[p->pos] == '-') p->pos++;
  if (p->pos >= p->len) return NULL;
  if (p->src[p->pos] == '0') {
    p->pos++;
  } else if (p->src[p->pos] >= '1' && p->src[p->pos] <= '9') {
    while (p->pos < p->len && isdigit((unsigned char)p->src[p->pos])) p->pos++;
  } else {
    return NULL;
  }
  if (p->pos < p->len && p->src[p->pos] == '.') {
    p->pos++;
    if (p->pos >= p->len || !isdigit((unsigned char)p->src[p->pos])) return NULL;
    while (p->pos < p->len && isdigit((unsigned char)p->src[p->pos])) p->pos++;
  }
  if (p->pos < p->len && (p->src[p->pos] == 'e' || p->src[p->pos] == 'E')) {
    p->pos++;
    if (p->pos < p->len && (p->src[p->pos] == '+' || p->src[p->pos] == '-')) p->pos++;
    if (p->pos >= p->len || !isdigit((unsigned char)p->src[p->pos])) return NULL;
    while (p->pos < p->len && isdigit((unsigned char)p->src[p->pos])) p->pos++;
  }
  size_t end = p->pos;
  size_t n = end - start;
  if (n == 0) return NULL;
  char tmp[64];
  char *buf = tmp;
  if (n >= sizeof(tmp)) {
    buf = (char *)malloc(n + 1);
    if (!buf) {
      ps_throw(p->ctx, PS_ERR_OOM, "out of memory");
      return NULL;
    }
  }
  memcpy(buf, p->src + start, n);
  buf[n] = '\0';
  char *ep = NULL;
  double v = strtod(buf, &ep);
  if (buf != tmp) free(buf);
  if (!ep || *ep != '\0') {
    ps_throw(p->ctx, PS_ERR_TYPE, "invalid JSON number");
    return NULL;
  }
  PS_Value *fv = ps_make_float(p->ctx, v);
  if (!fv) return NULL;
  PS_Value *out = json_make_value(p->ctx, "number", fv);
  ps_value_release(fv);
  return out;
}

static PS_Value *parse_value(JsonParser *p) {
  skip_ws(p);
  if (p->pos >= p->len) return NULL;
  char c = p->src[p->pos];
  if (c == '"') {
    PS_Value *s = parse_string(p);
    if (!s) return NULL;
    PS_Value *out = json_make_value(p->ctx, "string", s);
    ps_value_release(s);
    return out;
  }
  if (c == '{') return parse_object(p);
  if (c == '[') return parse_array(p);
  if (c == 't' && match(p, "true")) {
    PS_Value *b = ps_make_bool(p->ctx, 1);
    PS_Value *out = json_make_value(p->ctx, "bool", b);
    ps_value_release(b);
    return out;
  }
  if (c == 'f' && match(p, "false")) {
    PS_Value *b = ps_make_bool(p->ctx, 0);
    PS_Value *out = json_make_value(p->ctx, "bool", b);
    ps_value_release(b);
    return out;
  }
  if (c == 'n' && match(p, "null")) {
    return json_make_value(p->ctx, "null", NULL);
  }
  if (c == '-' || (c >= '0' && c <= '9')) {
    return parse_number(p);
  }
  ps_throw(p->ctx, PS_ERR_TYPE, "invalid JSON value");
  return NULL;
}

static PS_Value *json_parse(PS_Context *ctx, const char *s, size_t len) {
  JsonParser p = {s, len, 0, ctx};
  PS_Value *v = parse_value(&p);
  if (!v) return NULL;
  skip_ws(&p);
  if (p.pos != p.len) {
    ps_value_release(v);
    ps_throw(ctx, PS_ERR_TYPE, "invalid JSON");
    return NULL;
  }
  return v;
}

static PS_Status mod_encode(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  StrBuf sb;
  sb_init(&sb);
  if (!json_encode_value(ctx, argv[0], &sb)) {
    sb_free(&sb);
    return PS_ERR;
  }
  PS_Value *s = ps_make_string_utf8(ctx, sb.buf ? sb.buf : "", sb.len);
  sb_free(&sb);
  if (!s) return PS_ERR;
  *out = s;
  return PS_OK;
}

static PS_Status mod_decode(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  PS_Value *sval = argv[0];
  if (sval && ps_typeof(sval) == PS_T_BYTES) {
    PS_Value *tmp = ps_bytes_to_utf8_string(ctx, sval);
    if (!tmp) return PS_ERR;
    sval = tmp;
  }
  if (sval && ps_typeof(sval) == PS_T_OBJECT) {
    const char *kind = NULL;
    PS_Value *jval = NULL;
    if (json_value_kind(ctx, sval, &kind, &jval) && kind && strcmp(kind, "string") == 0 &&
        jval && ps_typeof(jval) == PS_T_STRING) {
      sval = jval;
    }
  }
  if (!sval || ps_typeof(sval) != PS_T_STRING) {
    ps_throw(ctx, PS_ERR_TYPE, "decode expects string");
    return PS_ERR;
  }
  const char *s = ps_string_ptr(sval);
  size_t len = ps_string_len(sval);
  PS_Value *v = json_parse(ctx, s ? s : "", len);
  if (sval != argv[0]) ps_value_release(sval);
  if (!v) return PS_ERR;
  *out = v;
  return PS_OK;
}

static PS_Status mod_isvalid(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  PS_Value *sval = argv[0];
  if (sval && ps_typeof(sval) == PS_T_BYTES) {
    PS_Value *tmp = ps_bytes_to_utf8_string(ctx, sval);
    if (!tmp) return PS_ERR;
    sval = tmp;
  }
  if (sval && ps_typeof(sval) == PS_T_OBJECT) {
    const char *kind = NULL;
    PS_Value *jval = NULL;
    if (json_value_kind(ctx, sval, &kind, &jval) && kind && strcmp(kind, "string") == 0 &&
        jval && ps_typeof(jval) == PS_T_STRING) {
      sval = jval;
    }
  }
  if (!sval || ps_typeof(sval) != PS_T_STRING) {
    ps_throw(ctx, PS_ERR_TYPE, "isValid expects string");
    return PS_ERR;
  }
  const char *s = ps_string_ptr(sval);
  size_t len = ps_string_len(sval);
  PS_Value *v = json_parse(ctx, s ? s : "", len);
  if (sval != argv[0]) ps_value_release(sval);
  if (!v) {
    ps_clear_error(ctx);
    *out = ps_make_bool(ctx, 0);
    return *out ? PS_OK : PS_ERR;
  }
  ps_value_release(v);
  *out = ps_make_bool(ctx, 1);
  return *out ? PS_OK : PS_ERR;
}

static PS_Status mod_null(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  (void)argv;
  PS_Value *v = json_make_value(ctx, "null", NULL);
  if (!v) return PS_ERR;
  *out = v;
  return PS_OK;
}

static PS_Status mod_bool(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!argv[0] || ps_typeof(argv[0]) != PS_T_BOOL) {
    ps_throw(ctx, PS_ERR_TYPE, "bool expects bool");
    return PS_ERR;
  }
  PS_Value *b = ps_make_bool(ctx, ps_as_bool(argv[0]));
  if (!b) return PS_ERR;
  PS_Value *v = json_make_value(ctx, "bool", b);
  ps_value_release(b);
  if (!v) return PS_ERR;
  *out = v;
  return PS_OK;
}

static PS_Status mod_number(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  double x = 0.0;
  if (ps_typeof(argv[0]) == PS_T_INT) x = (double)ps_as_int(argv[0]);
  else if (ps_typeof(argv[0]) == PS_T_FLOAT) x = ps_as_float(argv[0]);
  else {
    ps_throw(ctx, PS_ERR_TYPE, "number expects float");
    return PS_ERR;
  }
  if (!isfinite(x)) {
    ps_throw(ctx, PS_ERR_TYPE, "invalid JSON number");
    return PS_ERR;
  }
  PS_Value *f = ps_make_float(ctx, x);
  if (!f) return PS_ERR;
  PS_Value *v = json_make_value(ctx, "number", f);
  ps_value_release(f);
  if (!v) return PS_ERR;
  *out = v;
  return PS_OK;
}

static PS_Status mod_string(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!argv[0] || ps_typeof(argv[0]) != PS_T_STRING) {
    ps_throw(ctx, PS_ERR_TYPE, "string expects string");
    return PS_ERR;
  }
  PS_Value *s = ps_make_string_utf8(ctx, ps_string_ptr(argv[0]), ps_string_len(argv[0]));
  if (!s) return PS_ERR;
  PS_Value *v = json_make_value(ctx, "string", s);
  ps_value_release(s);
  if (!v) return PS_ERR;
  *out = v;
  return PS_OK;
}

static PS_Status mod_array(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!argv[0] || ps_typeof(argv[0]) != PS_T_LIST) {
    ps_throw(ctx, PS_ERR_TYPE, "array expects list<JSONValue>");
    return PS_ERR;
  }
  size_t n = ps_list_len(argv[0]);
  for (size_t i = 0; i < n; i++) {
    PS_Value *it = ps_list_get(ctx, argv[0], i);
    if (!it) return PS_ERR;
    const char *kind = NULL;
    if (!json_value_kind(ctx, it, &kind, NULL)) {
      ps_throw(ctx, PS_ERR_TYPE, "array expects list<JSONValue>");
      return PS_ERR;
    }
  }
  PS_Value *v = json_make_value(ctx, "array", argv[0]);
  if (!v) return PS_ERR;
  *out = v;
  return PS_OK;
}

static PS_Status mod_object(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!argv[0] || ps_typeof(argv[0]) != PS_T_MAP) {
    ps_throw(ctx, PS_ERR_TYPE, "object expects map<string,JSONValue>");
    return PS_ERR;
  }
  size_t n = ps_map_len(argv[0]);
  for (size_t i = 0; i < n; i++) {
    PS_Value *key = NULL;
    PS_Value *val = NULL;
    if (ps_map_entry(ctx, argv[0], i, &key, &val) != PS_OK) return PS_ERR;
    if (!key || ps_typeof(key) != PS_T_STRING) {
      ps_throw(ctx, PS_ERR_TYPE, "object expects map<string,JSONValue>");
      return PS_ERR;
    }
    if (!json_value_kind(ctx, val, NULL, NULL)) {
      ps_throw(ctx, PS_ERR_TYPE, "object expects map<string,JSONValue>");
      return PS_ERR;
    }
  }
  PS_Value *v = json_make_value(ctx, "object", argv[0]);
  if (!v) return PS_ERR;
  *out = v;
  return PS_OK;
}

PS_Status ps_module_init(PS_Context *ctx, PS_Module *out) {
  (void)ctx;
  static PS_NativeFnDesc fns[] = {
      {.name = "encode", .fn = mod_encode, .arity = 1, .ret_type = PS_T_STRING, .param_types = NULL, .flags = 0},
      {.name = "decode", .fn = mod_decode, .arity = 1, .ret_type = PS_T_OBJECT, .param_types = NULL, .flags = 0},
      {.name = "isValid", .fn = mod_isvalid, .arity = 1, .ret_type = PS_T_BOOL, .param_types = NULL, .flags = 0},
      {.name = "null", .fn = mod_null, .arity = 0, .ret_type = PS_T_OBJECT, .param_types = NULL, .flags = 0},
      {.name = "bool", .fn = mod_bool, .arity = 1, .ret_type = PS_T_OBJECT, .param_types = NULL, .flags = 0},
      {.name = "number", .fn = mod_number, .arity = 1, .ret_type = PS_T_OBJECT, .param_types = NULL, .flags = 0},
      {.name = "string", .fn = mod_string, .arity = 1, .ret_type = PS_T_OBJECT, .param_types = NULL, .flags = 0},
      {.name = "array", .fn = mod_array, .arity = 1, .ret_type = PS_T_OBJECT, .param_types = NULL, .flags = 0},
      {.name = "object", .fn = mod_object, .arity = 1, .ret_type = PS_T_OBJECT, .param_types = NULL, .flags = 0},
  };
  out->module_name = "JSON";
  out->api_version = PS_API_VERSION;
  out->fn_count = sizeof(fns) / sizeof(fns[0]);
  out->fns = fns;
  return PS_OK;
}
