#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "ps_json.h"

typedef struct {
  const char *src;
  size_t len;
  size_t pos;
  const char *err;
} PS_JsonParser;

static void skip_ws(PS_JsonParser *p) {
  while (p->pos < p->len && isspace((unsigned char)p->src[p->pos])) p->pos++;
}

static int match(PS_JsonParser *p, const char *kw) {
  size_t n = strlen(kw);
  if (p->pos + n > p->len) return 0;
  if (strncmp(p->src + p->pos, kw, n) == 0) {
    p->pos += n;
    return 1;
  }
  return 0;
}

static PS_JsonValue *parse_value(PS_JsonParser *p);

static char *parse_string(PS_JsonParser *p) {
  if (p->src[p->pos] != '"') return NULL;
  p->pos++;
  char *buf = (char *)malloc(p->len - p->pos + 1);
  if (!buf) return NULL;
  size_t out = 0;
  while (p->pos < p->len) {
    char c = p->src[p->pos++];
    if (c == '"') break;
    if (c == '\\') {
      if (p->pos >= p->len) break;
      char e = p->src[p->pos++];
      switch (e) {
        case '"':
        case '\\':
        case '/':
          buf[out++] = e;
          break;
        case 'b':
          buf[out++] = '\b';
          break;
        case 'f':
          buf[out++] = '\f';
          break;
        case 'n':
          buf[out++] = '\n';
          break;
        case 'r':
          buf[out++] = '\r';
          break;
        case 't':
          buf[out++] = '\t';
          break;
        case 'u':
          // Minimal: copy as-is, no UTF-16 decoding (IR uses ASCII keys).
          if (p->pos + 4 <= p->len) {
            buf[out++] = '?';
            p->pos += 4;
          }
          break;
        default:
          buf[out++] = e;
          break;
      }
    } else {
      buf[out++] = c;
    }
  }
  buf[out] = '\0';
  return buf;
}

static PS_JsonValue *make_value(PS_JsonType t) {
  PS_JsonValue *v = (PS_JsonValue *)calloc(1, sizeof(PS_JsonValue));
  if (!v) return NULL;
  v->type = t;
  return v;
}

static PS_JsonValue *parse_array(PS_JsonParser *p) {
  if (p->src[p->pos] != '[') return NULL;
  p->pos++;
  skip_ws(p);
  PS_JsonValue *arr = make_value(PS_JSON_ARRAY);
  if (!arr) return NULL;
  arr->as.array_v.items = NULL;
  arr->as.array_v.len = 0;
  if (p->pos < p->len && p->src[p->pos] == ']') {
    p->pos++;
    return arr;
  }
  while (p->pos < p->len) {
    skip_ws(p);
    PS_JsonValue *item = parse_value(p);
    if (!item) break;
    size_t n = arr->as.array_v.len + 1;
    PS_JsonValue **items = (PS_JsonValue **)realloc(arr->as.array_v.items, sizeof(PS_JsonValue *) * n);
    if (!items) {
      ps_json_free(item);
      break;
    }
    arr->as.array_v.items = items;
    arr->as.array_v.items[arr->as.array_v.len++] = item;
    skip_ws(p);
    if (p->pos < p->len && p->src[p->pos] == ',') {
      p->pos++;
      continue;
    }
    if (p->pos < p->len && p->src[p->pos] == ']') {
      p->pos++;
      return arr;
    }
  }
  p->err = "invalid array";
  ps_json_free(arr);
  return NULL;
}

static PS_JsonValue *parse_object(PS_JsonParser *p) {
  if (p->src[p->pos] != '{') return NULL;
  p->pos++;
  skip_ws(p);
  PS_JsonValue *obj = make_value(PS_JSON_OBJECT);
  if (!obj) return NULL;
  obj->as.object_v.keys = NULL;
  obj->as.object_v.values = NULL;
  obj->as.object_v.len = 0;
  if (p->pos < p->len && p->src[p->pos] == '}') {
    p->pos++;
    return obj;
  }
  while (p->pos < p->len) {
    skip_ws(p);
    char *key = parse_string(p);
    if (!key) break;
    skip_ws(p);
    if (p->pos >= p->len || p->src[p->pos] != ':') {
      free(key);
      break;
    }
    p->pos++;
    skip_ws(p);
    PS_JsonValue *val = parse_value(p);
    if (!val) {
      free(key);
      break;
    }
    size_t n = obj->as.object_v.len + 1;
    char **keys = (char **)realloc(obj->as.object_v.keys, sizeof(char *) * n);
    PS_JsonValue **vals = (PS_JsonValue **)realloc(obj->as.object_v.values, sizeof(PS_JsonValue *) * n);
    if (!keys || !vals) {
      free(key);
      ps_json_free(val);
      break;
    }
    obj->as.object_v.keys = keys;
    obj->as.object_v.values = vals;
    obj->as.object_v.keys[obj->as.object_v.len] = key;
    obj->as.object_v.values[obj->as.object_v.len] = val;
    obj->as.object_v.len += 1;
    skip_ws(p);
    if (p->pos < p->len && p->src[p->pos] == ',') {
      p->pos++;
      continue;
    }
    if (p->pos < p->len && p->src[p->pos] == '}') {
      p->pos++;
      return obj;
    }
  }
  p->err = "invalid object";
  ps_json_free(obj);
  return NULL;
}

static PS_JsonValue *parse_number(PS_JsonParser *p) {
  size_t start = p->pos;
  if (p->src[p->pos] == '-') p->pos++;
  while (p->pos < p->len && isdigit((unsigned char)p->src[p->pos])) p->pos++;
  if (p->pos < p->len && p->src[p->pos] == '.') {
    p->pos++;
    while (p->pos < p->len && isdigit((unsigned char)p->src[p->pos])) p->pos++;
  }
  if (p->pos < p->len && (p->src[p->pos] == 'e' || p->src[p->pos] == 'E')) {
    p->pos++;
    if (p->src[p->pos] == '+' || p->src[p->pos] == '-') p->pos++;
    while (p->pos < p->len && isdigit((unsigned char)p->src[p->pos])) p->pos++;
  }
  size_t n = p->pos - start;
  char *tmp = (char *)malloc(n + 1);
  if (!tmp) return NULL;
  memcpy(tmp, p->src + start, n);
  tmp[n] = '\0';
  double v = strtod(tmp, NULL);
  free(tmp);
  PS_JsonValue *val = make_value(PS_JSON_NUMBER);
  if (!val) return NULL;
  val->as.num_v = v;
  return val;
}

static PS_JsonValue *parse_value(PS_JsonParser *p) {
  skip_ws(p);
  if (p->pos >= p->len) return NULL;
  char c = p->src[p->pos];
  if (c == '"') {
    char *s = parse_string(p);
    if (!s) return NULL;
    PS_JsonValue *v = make_value(PS_JSON_STRING);
    if (!v) {
      free(s);
      return NULL;
    }
    v->as.str_v = s;
    return v;
  }
  if (c == '{') return parse_object(p);
  if (c == '[') return parse_array(p);
  if (match(p, "true")) {
    PS_JsonValue *v = make_value(PS_JSON_BOOL);
    if (!v) return NULL;
    v->as.bool_v = 1;
    return v;
  }
  if (match(p, "false")) {
    PS_JsonValue *v = make_value(PS_JSON_BOOL);
    if (!v) return NULL;
    v->as.bool_v = 0;
    return v;
  }
  if (match(p, "null")) return make_value(PS_JSON_NULL);
  if (c == '-' || isdigit((unsigned char)c)) return parse_number(p);
  return NULL;
}

PS_JsonValue *ps_json_parse(const char *src, size_t len, const char **err) {
  PS_JsonParser p = {src, len, 0, NULL};
  PS_JsonValue *v = parse_value(&p);
  if (!v || p.err) {
    if (err) *err = p.err ? p.err : "invalid json";
    ps_json_free(v);
    return NULL;
  }
  return v;
}

void ps_json_free(PS_JsonValue *v) {
  if (!v) return;
  switch (v->type) {
    case PS_JSON_STRING:
      free(v->as.str_v);
      break;
    case PS_JSON_ARRAY:
      for (size_t i = 0; i < v->as.array_v.len; i++) ps_json_free(v->as.array_v.items[i]);
      free(v->as.array_v.items);
      break;
    case PS_JSON_OBJECT:
      for (size_t i = 0; i < v->as.object_v.len; i++) {
        free(v->as.object_v.keys[i]);
        ps_json_free(v->as.object_v.values[i]);
      }
      free(v->as.object_v.keys);
      free(v->as.object_v.values);
      break;
    default:
      break;
  }
  free(v);
}

PS_JsonValue *ps_json_obj_get(PS_JsonValue *obj, const char *key) {
  if (!obj || obj->type != PS_JSON_OBJECT) return NULL;
  for (size_t i = 0; i < obj->as.object_v.len; i++) {
    if (strcmp(obj->as.object_v.keys[i], key) == 0) return obj->as.object_v.values[i];
  }
  return NULL;
}
