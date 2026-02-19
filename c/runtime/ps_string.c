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
    ps_throw_diag(ctx, PS_ERR_UTF8, "invalid UTF-8 sequence", "byte stream", "valid UTF-8");
    return NULL;
  }
  PS_Value *v = ps_value_alloc(PS_V_STRING);
  if (!v) {
    ps_throw_diag(ctx, PS_ERR_OOM, "out of memory", "allocation failed", "available memory");
    return NULL;
  }
  v->as.string_v.ptr = (char *)malloc(len + 1);
  if (!v->as.string_v.ptr && len > 0) {
    ps_value_release(v);
    ps_throw_diag(ctx, PS_ERR_OOM, "out of memory", "allocation failed", "available memory");
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
    ps_throw_diag(ctx, PS_ERR_OOM, "out of memory", "allocation failed", "available memory");
    return NULL;
  }
  v->as.string_v.ptr = (char *)malloc(len + 1);
  if (!v->as.string_v.ptr && len > 0) {
    ps_value_release(v);
    ps_throw_diag(ctx, PS_ERR_OOM, "out of memory", "allocation failed", "available memory");
    return NULL;
  }
  memcpy(v->as.string_v.ptr, a->as.string_v.ptr, a->as.string_v.len);
  memcpy(v->as.string_v.ptr + a->as.string_v.len, b->as.string_v.ptr, b->as.string_v.len);
  v->as.string_v.ptr[len] = '\0';
  v->as.string_v.len = len;
  return v;
}

static int utf8_advance_glyphs(const uint8_t *buf, size_t len, size_t *byte_pos, size_t glyph_count) {
  size_t i = *byte_pos;
  uint32_t cp = 0;
  for (size_t g = 0; g < glyph_count; g += 1) {
    int r = utf8_next(buf, len, &i, &cp);
    if (r <= 0) return 0;
  }
  *byte_pos = i;
  return 1;
}

static int utf8_match_at(const char *hay_ptr, size_t hay_len, size_t byte_index, const char *needle_ptr, size_t needle_len) {
  if (byte_index + needle_len > hay_len) return 0;
  return memcmp(hay_ptr + byte_index, needle_ptr, needle_len) == 0;
}

PS_Value *ps_string_substring(PS_Context *ctx, PS_Value *s, int64_t start, int64_t length) {
  if (start < 0 || length < 0) {
    ps_throw_diag(ctx, PS_ERR_RANGE, "index out of bounds", "start/length", "start >= 0 and length >= 0");
    return NULL;
  }
  const uint8_t *buf = (const uint8_t *)s->as.string_v.ptr;
  size_t len = s->as.string_v.len;
  size_t byte_start = 0;
  size_t byte_end = 0;
  if (!utf8_advance_glyphs(buf, len, &byte_start, (size_t)start)) {
    char got[64];
    snprintf(got, sizeof(got), "start=%lld, length=%lld", (long long)start, (long long)length);
    ps_throw_diag(ctx, PS_ERR_RANGE, "index out of bounds", got, "range within string");
    return NULL;
  }
  byte_end = byte_start;
  if (!utf8_advance_glyphs(buf, len, &byte_end, (size_t)length)) {
    char got[64];
    snprintf(got, sizeof(got), "start=%lld, length=%lld", (long long)start, (long long)length);
    ps_throw_diag(ctx, PS_ERR_RANGE, "index out of bounds", got, "range within string");
    return NULL;
  }
  size_t out_len = byte_end - byte_start;
  return ps_string_from_utf8(ctx, s->as.string_v.ptr + byte_start, out_len);
}

int64_t ps_string_index_of(PS_Value *hay, PS_Value *needle) {
  const char *h = hay->as.string_v.ptr;
  const char *n = needle->as.string_v.ptr;
  size_t hlen = hay->as.string_v.len;
  size_t nlen = needle->as.string_v.len;
  if (!n || nlen == 0) return 0;
  size_t i = 0;
  int64_t g = 0;
  uint32_t cp = 0;
  while (i <= hlen) {
    if (utf8_match_at(h, hlen, i, n, nlen)) return g;
    if (i == hlen) break;
    int r = utf8_next((const uint8_t *)h, hlen, &i, &cp);
    if (r <= 0) return -1;
    g += 1;
  }
  return -1;
}

int ps_string_contains(PS_Value *hay, PS_Value *needle) {
  return ps_string_index_of(hay, needle) >= 0;
}

int64_t ps_string_last_index_of(PS_Value *hay, PS_Value *needle) {
  const char *h = hay->as.string_v.ptr;
  const char *n = needle->as.string_v.ptr;
  size_t hlen = hay->as.string_v.len;
  size_t nlen = needle->as.string_v.len;
  if (!n || nlen == 0) return (int64_t)ps_utf8_glyph_len((const uint8_t *)h, hlen);

  size_t i = 0;
  int64_t g = 0;
  int64_t last = -1;
  uint32_t cp = 0;
  while (i <= hlen) {
    if (utf8_match_at(h, hlen, i, n, nlen)) last = g;
    if (i == hlen) break;
    int r = utf8_next((const uint8_t *)h, hlen, &i, &cp);
    if (r <= 0) return -1;
    g += 1;
  }
  return last;
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
    ps_throw_diag(ctx, PS_ERR_OOM, "out of memory", "allocation failed", "available memory");
    return NULL;
  }
  v->as.string_v.ptr = (char *)malloc(new_len + 1);
  if (!v->as.string_v.ptr && new_len > 0) {
    ps_value_release(v);
    ps_throw_diag(ctx, PS_ERR_OOM, "out of memory", "allocation failed", "available memory");
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

PS_Value *ps_string_replace_all(PS_Context *ctx, PS_Value *s, PS_Value *from, PS_Value *to) {
  const char *h = s->as.string_v.ptr;
  const char *n = from->as.string_v.ptr;
  if (from->as.string_v.len == 0) {
    ps_throw_diag(ctx, PS_ERR_RANGE, "invalid argument", "oldValue=\"\"", "non-empty oldValue");
    return NULL;
  }
  size_t count = 0;
  size_t i = 0;
  uint32_t cp = 0;
  while (i <= s->as.string_v.len) {
    if (utf8_match_at(h, s->as.string_v.len, i, n, from->as.string_v.len)) {
      count += 1;
      i += from->as.string_v.len;
      continue;
    }
    if (i == s->as.string_v.len) break;
    int r = utf8_next((const uint8_t *)h, s->as.string_v.len, &i, &cp);
    if (r <= 0) {
      ps_throw_diag(ctx, PS_ERR_UTF8, "invalid UTF-8 sequence", "byte stream", "valid UTF-8");
      return NULL;
    }
  }
  if (count == 0) return ps_string_from_utf8(ctx, h, s->as.string_v.len);

  size_t new_len = s->as.string_v.len;
  if (to->as.string_v.len >= from->as.string_v.len) {
    new_len += count * (to->as.string_v.len - from->as.string_v.len);
  } else {
    new_len -= count * (from->as.string_v.len - to->as.string_v.len);
  }
  PS_Value *v = ps_value_alloc(PS_V_STRING);
  if (!v) {
    ps_throw_diag(ctx, PS_ERR_OOM, "out of memory", "allocation failed", "available memory");
    return NULL;
  }
  v->as.string_v.ptr = (char *)malloc(new_len + 1);
  if (!v->as.string_v.ptr && new_len > 0) {
    ps_value_release(v);
    ps_throw_diag(ctx, PS_ERR_OOM, "out of memory", "allocation failed", "available memory");
    return NULL;
  }
  size_t out = 0;
  i = 0;
  while (i <= s->as.string_v.len) {
    if (utf8_match_at(h, s->as.string_v.len, i, n, from->as.string_v.len)) {
      if (to->as.string_v.len > 0) {
        memcpy(v->as.string_v.ptr + out, to->as.string_v.ptr, to->as.string_v.len);
        out += to->as.string_v.len;
      }
      i += from->as.string_v.len;
      continue;
    }
    if (i == s->as.string_v.len) break;
    int r = utf8_next((const uint8_t *)h, s->as.string_v.len, &i, &cp);
    if (r <= 0) {
      ps_value_release(v);
      ps_throw_diag(ctx, PS_ERR_UTF8, "invalid UTF-8 sequence", "byte stream", "valid UTF-8");
      return NULL;
    }
    size_t adv = (size_t)r;
    memcpy(v->as.string_v.ptr + out, h + (i - adv), adv);
    out += adv;
  }
  v->as.string_v.ptr[out] = '\0';
  v->as.string_v.len = out;
  return v;
}

PS_Value *ps_string_glyph_at(PS_Context *ctx, PS_Value *s, int64_t index) {
  if (index < 0) {
    ps_throw_diag(ctx, PS_ERR_RANGE, "index out of bounds", "index", "index within string");
    return NULL;
  }
  const uint8_t *buf = (const uint8_t *)s->as.string_v.ptr;
  size_t i = 0;
  size_t g = 0;
  uint32_t cp = 0;
  while (i < s->as.string_v.len) {
    int r = utf8_next(buf, s->as.string_v.len, &i, &cp);
    if (r <= 0) {
      ps_throw_diag(ctx, PS_ERR_UTF8, "invalid UTF-8 sequence", "byte stream", "valid UTF-8");
      return NULL;
    }
    if (g == (size_t)index) return ps_make_glyph(ctx, cp);
    g += 1;
  }
  ps_throw_diag(ctx, PS_ERR_RANGE, "index out of bounds", "index", "index within string");
  return NULL;
}

PS_Value *ps_string_repeat(PS_Context *ctx, PS_Value *s, int64_t count) {
  if (count < 0) {
    ps_throw_diag(ctx, PS_ERR_RANGE, "invalid argument", "count < 0", "count >= 0");
    return NULL;
  }
  if (count == 0 || s->as.string_v.len == 0) return ps_string_from_utf8(ctx, "", 0);
  size_t rep = (size_t)count;
  size_t new_len = s->as.string_v.len * rep;
  PS_Value *v = ps_value_alloc(PS_V_STRING);
  if (!v) {
    ps_throw_diag(ctx, PS_ERR_OOM, "out of memory", "allocation failed", "available memory");
    return NULL;
  }
  v->as.string_v.ptr = (char *)malloc(new_len + 1);
  if (!v->as.string_v.ptr && new_len > 0) {
    ps_value_release(v);
    ps_throw_diag(ctx, PS_ERR_OOM, "out of memory", "allocation failed", "available memory");
    return NULL;
  }
  for (size_t i = 0; i < rep; i += 1) {
    memcpy(v->as.string_v.ptr + (i * s->as.string_v.len), s->as.string_v.ptr, s->as.string_v.len);
  }
  v->as.string_v.ptr[new_len] = '\0';
  v->as.string_v.len = new_len;
  return v;
}

static PS_Value *ps_string_pad_impl(PS_Context *ctx, PS_Value *s, int64_t target_len, PS_Value *pad, int is_start) {
  if (target_len < 0) {
    ps_throw_diag(ctx, PS_ERR_RANGE, "invalid argument", "targetLength < 0", "targetLength >= 0");
    return NULL;
  }
  size_t src_glyphs = ps_utf8_glyph_len((const uint8_t *)s->as.string_v.ptr, s->as.string_v.len);
  if ((size_t)target_len <= src_glyphs) return ps_string_from_utf8(ctx, s->as.string_v.ptr, s->as.string_v.len);

  size_t pad_glyphs = ps_utf8_glyph_len((const uint8_t *)pad->as.string_v.ptr, pad->as.string_v.len);
  if (pad_glyphs == 0) {
    ps_throw_diag(ctx, PS_ERR_RANGE, "invalid argument", "pad=\"\"", "non-empty pad when padding is required");
    return NULL;
  }
  size_t need = (size_t)target_len - src_glyphs;
  size_t full = need / pad_glyphs;
  size_t rem = need % pad_glyphs;
  size_t rem_bytes = 0;
  if (rem > 0) {
    size_t pos = 0;
    if (!utf8_advance_glyphs((const uint8_t *)pad->as.string_v.ptr, pad->as.string_v.len, &pos, rem)) {
      ps_throw_diag(ctx, PS_ERR_UTF8, "invalid UTF-8 sequence", "byte stream", "valid UTF-8");
      return NULL;
    }
    rem_bytes = pos;
  }
  size_t fill_bytes = full * pad->as.string_v.len + rem_bytes;
  size_t out_len = fill_bytes + s->as.string_v.len;
  PS_Value *v = ps_value_alloc(PS_V_STRING);
  if (!v) {
    ps_throw_diag(ctx, PS_ERR_OOM, "out of memory", "allocation failed", "available memory");
    return NULL;
  }
  v->as.string_v.ptr = (char *)malloc(out_len + 1);
  if (!v->as.string_v.ptr && out_len > 0) {
    ps_value_release(v);
    ps_throw_diag(ctx, PS_ERR_OOM, "out of memory", "allocation failed", "available memory");
    return NULL;
  }
  size_t off = 0;
  if (is_start) {
    for (size_t i = 0; i < full; i += 1) {
      memcpy(v->as.string_v.ptr + off, pad->as.string_v.ptr, pad->as.string_v.len);
      off += pad->as.string_v.len;
    }
    if (rem_bytes > 0) {
      memcpy(v->as.string_v.ptr + off, pad->as.string_v.ptr, rem_bytes);
      off += rem_bytes;
    }
    memcpy(v->as.string_v.ptr + off, s->as.string_v.ptr, s->as.string_v.len);
    off += s->as.string_v.len;
  } else {
    memcpy(v->as.string_v.ptr + off, s->as.string_v.ptr, s->as.string_v.len);
    off += s->as.string_v.len;
    for (size_t i = 0; i < full; i += 1) {
      memcpy(v->as.string_v.ptr + off, pad->as.string_v.ptr, pad->as.string_v.len);
      off += pad->as.string_v.len;
    }
    if (rem_bytes > 0) {
      memcpy(v->as.string_v.ptr + off, pad->as.string_v.ptr, rem_bytes);
      off += rem_bytes;
    }
  }
  v->as.string_v.ptr[off] = '\0';
  v->as.string_v.len = off;
  return v;
}

PS_Value *ps_string_pad_start(PS_Context *ctx, PS_Value *s, int64_t target_len, PS_Value *pad) {
  return ps_string_pad_impl(ctx, s, target_len, pad, 1);
}

PS_Value *ps_string_pad_end(PS_Context *ctx, PS_Value *s, int64_t target_len, PS_Value *pad) {
  return ps_string_pad_impl(ctx, s, target_len, pad, 0);
}

PS_Value *ps_string_to_upper(PS_Context *ctx, PS_Value *s) {
  PS_Value *v = ps_value_alloc(PS_V_STRING);
  if (!v) {
    ps_throw_diag(ctx, PS_ERR_OOM, "out of memory", "allocation failed", "available memory");
    return NULL;
  }
  size_t len = s->as.string_v.len;
  v->as.string_v.ptr = (char *)malloc(len + 1);
  if (!v->as.string_v.ptr && len > 0) {
    ps_value_release(v);
    ps_throw_diag(ctx, PS_ERR_OOM, "out of memory", "allocation failed", "available memory");
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
    ps_throw_diag(ctx, PS_ERR_OOM, "out of memory", "allocation failed", "available memory");
    return NULL;
  }
  size_t len = s->as.string_v.len;
  v->as.string_v.ptr = (char *)malloc(len + 1);
  if (!v->as.string_v.ptr && len > 0) {
    ps_value_release(v);
    ps_throw_diag(ctx, PS_ERR_OOM, "out of memory", "allocation failed", "available memory");
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
    ps_throw_diag(ctx, PS_ERR_OOM, "out of memory", "allocation failed", "available memory");
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
        ps_throw_diag(ctx, PS_ERR_UTF8, "invalid UTF-8 sequence", "byte stream", "valid UTF-8");
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
