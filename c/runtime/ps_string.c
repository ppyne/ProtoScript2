#include <stdlib.h>
#include <string.h>

#include "ps_string.h"
#include "ps_list.h"

static int is_ascii_space(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
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

int ps_utf8_validate(const uint8_t *s, size_t len) {
  size_t i = 0;
  uint32_t cp = 0;
  while (i < len) {
    int r = utf8_next(s, len, &i, &cp);
    if (r <= 0) return 0;
  }
  return 1;
}

size_t ps_utf8_glyph_len(const uint8_t *s, size_t len) {
  size_t i = 0;
  size_t count = 0;
  uint32_t cp = 0;
  while (i < len) {
    int r = utf8_next(s, len, &i, &cp);
    if (r <= 0) return 0;
    count += 1;
  }
  return count;
}

uint32_t ps_utf8_glyph_at(const uint8_t *s, size_t len, size_t index) {
  size_t i = 0;
  size_t count = 0;
  uint32_t cp = 0;
  while (i < len) {
    int r = utf8_next(s, len, &i, &cp);
    if (r <= 0) return 0;
    if (count == index) return cp;
    count += 1;
  }
  return 0;
}

PS_Value *ps_string_from_utf8(PS_Context *ctx, const char *s, size_t len) {
  if (!ps_utf8_validate((const uint8_t *)s, len)) {
    ps_throw(ctx, PS_ERR_UTF8, "invalid UTF-8");
    return NULL;
  }
  PS_Value *v = ps_value_alloc(PS_V_STRING);
  if (!v) {
    ps_throw(ctx, PS_ERR_OOM, "out of memory");
    return NULL;
  }
  v->as.string_v.ptr = (char *)malloc(len + 1);
  if (!v->as.string_v.ptr && len > 0) {
    ps_value_release(v);
    ps_throw(ctx, PS_ERR_OOM, "out of memory");
    return NULL;
  }
  if (len > 0) memcpy(v->as.string_v.ptr, s, len);
  v->as.string_v.ptr[len] = '\0';
  v->as.string_v.len = len;
  return v;
}

PS_Value *ps_string_concat(PS_Context *ctx, PS_Value *a, PS_Value *b) {
  size_t len = a->as.string_v.len + b->as.string_v.len;
  PS_Value *v = ps_value_alloc(PS_V_STRING);
  if (!v) {
    ps_throw(ctx, PS_ERR_OOM, "out of memory");
    return NULL;
  }
  v->as.string_v.ptr = (char *)malloc(len + 1);
  if (!v->as.string_v.ptr && len > 0) {
    ps_value_release(v);
    ps_throw(ctx, PS_ERR_OOM, "out of memory");
    return NULL;
  }
  memcpy(v->as.string_v.ptr, a->as.string_v.ptr, a->as.string_v.len);
  memcpy(v->as.string_v.ptr + a->as.string_v.len, b->as.string_v.ptr, b->as.string_v.len);
  v->as.string_v.ptr[len] = '\0';
  v->as.string_v.len = len;
  return v;
}

PS_Value *ps_string_substring(PS_Context *ctx, PS_Value *s, size_t start, size_t length) {
  const uint8_t *buf = (const uint8_t *)s->as.string_v.ptr;
  size_t len = s->as.string_v.len;
  size_t i = 0;
  size_t glyph = 0;
  size_t byte_start = 0;
  size_t byte_end = 0;
  uint32_t cp = 0;
  while (i < len) {
    if (glyph == start) byte_start = i;
    int r = utf8_next(buf, len, &i, &cp);
    if (r <= 0) {
      ps_throw(ctx, PS_ERR_UTF8, "invalid UTF-8");
      return NULL;
    }
    glyph += 1;
    if (glyph == start + length) {
      byte_end = i;
      break;
    }
  }
  if (glyph < start + length) {
    ps_throw(ctx, PS_ERR_RANGE, "string index out of bounds");
    return NULL;
  }
  size_t out_len = byte_end - byte_start;
  return ps_string_from_utf8(ctx, s->as.string_v.ptr + byte_start, out_len);
}

int64_t ps_string_index_of(PS_Value *hay, PS_Value *needle) {
  const char *h = hay->as.string_v.ptr;
  const char *n = needle->as.string_v.ptr;
  if (!n || needle->as.string_v.len == 0) return 0;
  const char *pos = strstr(h, n);
  if (!pos) return -1;
  size_t byte_index = (size_t)(pos - h);
  return (int64_t)ps_utf8_glyph_len((const uint8_t *)h, byte_index);
}

int ps_string_starts_with(PS_Value *s, PS_Value *prefix) {
  if (prefix->as.string_v.len > s->as.string_v.len) return 0;
  return memcmp(s->as.string_v.ptr, prefix->as.string_v.ptr, prefix->as.string_v.len) == 0;
}

int ps_string_ends_with(PS_Value *s, PS_Value *suffix) {
  if (suffix->as.string_v.len > s->as.string_v.len) return 0;
  size_t off = s->as.string_v.len - suffix->as.string_v.len;
  return memcmp(s->as.string_v.ptr + off, suffix->as.string_v.ptr, suffix->as.string_v.len) == 0;
}

PS_Value *ps_string_trim(PS_Context *ctx, PS_Value *s, int mode) {
  const char *p = s->as.string_v.ptr;
  size_t len = s->as.string_v.len;
  size_t start = 0;
  size_t end = len;
  if (mode == 0 || mode == 1) {
    while (start < len && is_ascii_space(p[start])) start += 1;
  }
  if (mode == 0 || mode == 2) {
    while (end > start && is_ascii_space(p[end - 1])) end -= 1;
  }
  return ps_string_from_utf8(ctx, p + start, end - start);
}

PS_Value *ps_string_replace(PS_Context *ctx, PS_Value *s, PS_Value *from, PS_Value *to) {
  const char *h = s->as.string_v.ptr;
  const char *n = from->as.string_v.ptr;
  if (from->as.string_v.len == 0) return ps_string_from_utf8(ctx, h, s->as.string_v.len);
  const char *pos = strstr(h, n);
  if (!pos) return ps_string_from_utf8(ctx, h, s->as.string_v.len);
  size_t pre = (size_t)(pos - h);
  size_t new_len = pre + to->as.string_v.len + (s->as.string_v.len - pre - from->as.string_v.len);
  PS_Value *v = ps_value_alloc(PS_V_STRING);
  if (!v) {
    ps_throw(ctx, PS_ERR_OOM, "out of memory");
    return NULL;
  }
  v->as.string_v.ptr = (char *)malloc(new_len + 1);
  if (!v->as.string_v.ptr && new_len > 0) {
    ps_value_release(v);
    ps_throw(ctx, PS_ERR_OOM, "out of memory");
    return NULL;
  }
  memcpy(v->as.string_v.ptr, h, pre);
  memcpy(v->as.string_v.ptr + pre, to->as.string_v.ptr, to->as.string_v.len);
  memcpy(v->as.string_v.ptr + pre + to->as.string_v.len, pos + from->as.string_v.len,
         s->as.string_v.len - pre - from->as.string_v.len);
  v->as.string_v.ptr[new_len] = '\0';
  v->as.string_v.len = new_len;
  return v;
}

PS_Value *ps_string_to_upper(PS_Context *ctx, PS_Value *s) {
  PS_Value *v = ps_value_alloc(PS_V_STRING);
  if (!v) {
    ps_throw(ctx, PS_ERR_OOM, "out of memory");
    return NULL;
  }
  size_t len = s->as.string_v.len;
  v->as.string_v.ptr = (char *)malloc(len + 1);
  if (!v->as.string_v.ptr && len > 0) {
    ps_value_release(v);
    ps_throw(ctx, PS_ERR_OOM, "out of memory");
    return NULL;
  }
  for (size_t i = 0; i < len; i++) {
    char c = s->as.string_v.ptr[i];
    if (c >= 'a' && c <= 'z') c = (char)(c - ('a' - 'A'));
    v->as.string_v.ptr[i] = c;
  }
  v->as.string_v.ptr[len] = '\0';
  v->as.string_v.len = len;
  return v;
}

PS_Value *ps_string_to_lower(PS_Context *ctx, PS_Value *s) {
  PS_Value *v = ps_value_alloc(PS_V_STRING);
  if (!v) {
    ps_throw(ctx, PS_ERR_OOM, "out of memory");
    return NULL;
  }
  size_t len = s->as.string_v.len;
  v->as.string_v.ptr = (char *)malloc(len + 1);
  if (!v->as.string_v.ptr && len > 0) {
    ps_value_release(v);
    ps_throw(ctx, PS_ERR_OOM, "out of memory");
    return NULL;
  }
  for (size_t i = 0; i < len; i++) {
    char c = s->as.string_v.ptr[i];
    if (c >= 'A' && c <= 'Z') c = (char)(c + ('a' - 'A'));
    v->as.string_v.ptr[i] = c;
  }
  v->as.string_v.ptr[len] = '\0';
  v->as.string_v.len = len;
  return v;
}

PS_Value *ps_string_split(PS_Context *ctx, PS_Value *s, PS_Value *sep) {
  const char *h = s->as.string_v.ptr;
  const char *needle = sep->as.string_v.ptr;
  size_t nlen = sep->as.string_v.len;
  PS_Value *list = ps_value_alloc(PS_V_LIST);
  if (!list) {
    ps_throw(ctx, PS_ERR_OOM, "out of memory");
    return NULL;
  }
  list->as.list_v.items = NULL;
  list->as.list_v.len = 0;
  list->as.list_v.cap = 0;
  if (nlen == 0) {
    size_t i = 0;
    uint32_t cp = 0;
    while (i < s->as.string_v.len) {
      size_t start = i;
      int r = utf8_next((const uint8_t *)h, s->as.string_v.len, &i, &cp);
      if (r <= 0) {
        ps_value_release(list);
        ps_throw(ctx, PS_ERR_UTF8, "invalid UTF-8");
        return NULL;
      }
      PS_Value *part = ps_string_from_utf8(ctx, h + start, i - start);
      if (!part) {
        ps_value_release(list);
        return NULL;
      }
      ps_list_push_internal(ctx, list, part);
      ps_value_release(part);
    }
    return list;
  }
  const char *cur = h;
  const char *pos = strstr(cur, needle);
  while (pos) {
    PS_Value *part = ps_string_from_utf8(ctx, cur, (size_t)(pos - cur));
    if (!part) {
      ps_value_release(list);
      return NULL;
    }
    ps_list_push_internal(ctx, list, part);
    ps_value_release(part);
    cur = pos + nlen;
    pos = strstr(cur, needle);
  }
  PS_Value *part = ps_string_from_utf8(ctx, cur, strlen(cur));
  if (!part) {
    ps_value_release(list);
    return NULL;
  }
  ps_list_push_internal(ctx, list, part);
  ps_value_release(part);
  return list;
}
