#include <ctype.h>
#include <limits.h>
#include <regex.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ps/ps_api.h"

typedef struct {
  int id;
  regex_t re;
  int valid;
  int cflags;
  char *pattern;
  char *pattern_posix;
  char flags[4];
  int logical_to_phys[100];
  int logical_groups;
} RegexEntry;

typedef struct {
  RegexEntry *items;
  size_t len;
  size_t cap;
  int next_id;
} RegexStore;

static RegexStore g_store = {0};

static PS_Status rx_throw(PS_Context *ctx, PS_ErrorCode code, const char *kind, const char *msg) {
  char buf[512];
  const char *k = kind ? kind : "RegExpError";
  const char *m = msg ? msg : "error";
  snprintf(buf, sizeof(buf), "%s: %s", k, m);
  ps_throw(ctx, code, buf);
  return PS_ERR;
}

static PS_Status rx_syntax(PS_Context *ctx, const char *msg) {
  return rx_throw(ctx, PS_ERR_RANGE, "RegExpSyntax", msg);
}

static PS_Status rx_limit(PS_Context *ctx, const char *msg) {
  return rx_throw(ctx, PS_ERR_RANGE, "RegExpLimit", msg);
}

static PS_Status rx_range(PS_Context *ctx, const char *msg) {
  return rx_throw(ctx, PS_ERR_RANGE, "RegExpRange", msg);
}

static int utf8_decode_one(const uint8_t *s, size_t len, size_t i, uint32_t *out_cp, size_t *out_next) {
  if (!s || i >= len || !out_cp || !out_next) return 0;
  uint8_t b0 = s[i];
  if (b0 < 0x80) {
    *out_cp = b0;
    *out_next = i + 1;
    return 1;
  }
  if ((b0 & 0xE0) == 0xC0) {
    if (i + 1 >= len) return 0;
    uint8_t b1 = s[i + 1];
    if ((b1 & 0xC0) != 0x80) return 0;
    uint32_t cp = ((uint32_t)(b0 & 0x1F) << 6) | (uint32_t)(b1 & 0x3F);
    if (cp < 0x80) return 0;
    *out_cp = cp;
    *out_next = i + 2;
    return 1;
  }
  if ((b0 & 0xF0) == 0xE0) {
    if (i + 2 >= len) return 0;
    uint8_t b1 = s[i + 1];
    uint8_t b2 = s[i + 2];
    if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) return 0;
    uint32_t cp = ((uint32_t)(b0 & 0x0F) << 12) | ((uint32_t)(b1 & 0x3F) << 6) | (uint32_t)(b2 & 0x3F);
    if (cp < 0x800) return 0;
    if (cp >= 0xD800 && cp <= 0xDFFF) return 0;
    *out_cp = cp;
    *out_next = i + 3;
    return 1;
  }
  if ((b0 & 0xF8) == 0xF0) {
    if (i + 3 >= len) return 0;
    uint8_t b1 = s[i + 1];
    uint8_t b2 = s[i + 2];
    uint8_t b3 = s[i + 3];
    if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) return 0;
    uint32_t cp = ((uint32_t)(b0 & 0x07) << 18) | ((uint32_t)(b1 & 0x3F) << 12) | ((uint32_t)(b2 & 0x3F) << 6) | (uint32_t)(b3 & 0x3F);
    if (cp < 0x10000 || cp > 0x10FFFF) return 0;
    *out_cp = cp;
    *out_next = i + 4;
    return 1;
  }
  return 0;
}

static int utf8_validate(const char *s, size_t len, size_t *out_glyphs) {
  if (!s) return 0;
  size_t i = 0;
  size_t g = 0;
  while (i < len) {
    uint32_t cp = 0;
    size_t next = 0;
    if (!utf8_decode_one((const uint8_t *)s, len, i, &cp, &next)) return 0;
    (void)cp;
    i = next;
    g += 1;
  }
  if (out_glyphs) *out_glyphs = g;
  return 1;
}

typedef struct {
  size_t *glyph_to_byte;
  size_t glyph_count;
} GlyphIndex;

static void glyph_index_free(GlyphIndex *idx) {
  if (!idx) return;
  free(idx->glyph_to_byte);
  idx->glyph_to_byte = NULL;
  idx->glyph_count = 0;
}

static int glyph_index_build(const char *s, size_t len, GlyphIndex *out) {
  if (!s || !out) return 0;
  memset(out, 0, sizeof(*out));
  size_t cap = 16;
  size_t *arr = (size_t *)malloc(sizeof(size_t) * cap);
  if (!arr) return 0;
  arr[0] = 0;
  size_t count = 0;
  size_t i = 0;
  while (i < len) {
    uint32_t cp = 0;
    size_t next = 0;
    if (!utf8_decode_one((const uint8_t *)s, len, i, &cp, &next)) {
      free(arr);
      return 0;
    }
    (void)cp;
    count += 1;
    if (count + 1 > cap) {
      size_t ncap = cap * 2;
      size_t *narr = (size_t *)realloc(arr, sizeof(size_t) * ncap);
      if (!narr) {
        free(arr);
        return 0;
      }
      arr = narr;
      cap = ncap;
    }
    arr[count] = next;
    i = next;
  }
  out->glyph_to_byte = arr;
  out->glyph_count = count;
  return 1;
}

static size_t glyph_to_byte_pos(const GlyphIndex *idx, size_t glyph_pos) {
  if (!idx || !idx->glyph_to_byte) return 0;
  if (glyph_pos > idx->glyph_count) return idx->glyph_to_byte[idx->glyph_count];
  return idx->glyph_to_byte[glyph_pos];
}

static size_t byte_to_glyph_pos(const GlyphIndex *idx, size_t byte_pos) {
  if (!idx || !idx->glyph_to_byte) return 0;
  size_t lo = 0;
  size_t hi = idx->glyph_count;
  while (lo < hi) {
    size_t mid = lo + (hi - lo) / 2;
    size_t b = idx->glyph_to_byte[mid];
    if (b < byte_pos) lo = mid + 1;
    else hi = mid;
  }
  if (lo <= idx->glyph_count && idx->glyph_to_byte[lo] == byte_pos) return lo;
  return lo;
}

static PS_Status get_string_arg(PS_Context *ctx, PS_Value *v, const char **s, size_t *len) {
  if (!v || ps_typeof(v) != PS_T_STRING) {
    ps_throw(ctx, PS_ERR_TYPE, "expected string");
    return PS_ERR;
  }
  *s = ps_string_ptr(v);
  *len = ps_string_len(v);
  return PS_OK;
}

static PS_Status get_int_arg(PS_Context *ctx, PS_Value *v, int64_t *out) {
  if (!v || ps_typeof(v) != PS_T_INT) {
    ps_throw(ctx, PS_ERR_TYPE, "expected int");
    return PS_ERR;
  }
  *out = ps_as_int(v);
  return PS_OK;
}

static RegexEntry *store_find(int id) {
  for (size_t i = 0; i < g_store.len; i++) {
    if (g_store.items[i].valid && g_store.items[i].id == id) return &g_store.items[i];
  }
  return NULL;
}

static RegexEntry *store_add(void) {
  if (g_store.len == g_store.cap) {
    size_t ncap = g_store.cap == 0 ? 8 : g_store.cap * 2;
    RegexEntry *n = (RegexEntry *)realloc(g_store.items, sizeof(RegexEntry) * ncap);
    if (!n) return NULL;
    g_store.items = n;
    g_store.cap = ncap;
  }
  RegexEntry *e = &g_store.items[g_store.len++];
  memset(e, 0, sizeof(*e));
  e->id = ++g_store.next_id;
  e->valid = 1;
  return e;
}

static int is_escaped(const char *s, size_t i) {
  size_t c = 0;
  while (i > 0 && s[i - 1] == '\\') {
    c += 1;
    i -= 1;
  }
  return (c % 2) != 0;
}

static int has_forbidden_meta(const char *p, size_t len) {
  for (size_t i = 0; i + 2 < len; i++) {
    if (p[i] == '(' && p[i + 1] == '?' && (p[i + 2] == '=' || p[i + 2] == '!')) return 1;
  }
  for (size_t i = 0; i + 3 < len; i++) {
    if (p[i] == '(' && p[i + 1] == '?' && p[i + 2] == '<' && (p[i + 3] == '=' || p[i + 3] == '!')) return 1;
  }
  for (size_t i = 0; i + 1 < len; i++) {
    if (p[i] == '\\' && !is_escaped(p, i) && p[i + 1] >= '1' && p[i + 1] <= '9') return 1;
  }
  return 0;
}

static int validate_basic_syntax(const char *p, size_t len, char *err, size_t err_cap) {
  int paren = 0;
  int in_class = 0;
  int class_has_content = 0;
  for (size_t i = 0; i < len; i++) {
    char c = p[i];
    if (c == '\\') {
      i += 1;
      continue;
    }
    if (!in_class) {
      if (c == '[') {
        in_class = 1;
        class_has_content = 0;
        continue;
      }
      if (c == '(') {
        paren += 1;
        continue;
      }
      if (c == ')') {
        if (paren == 0) {
          snprintf(err, err_cap, "unmatched ')' in pattern");
          return 0;
        }
        paren -= 1;
        continue;
      }
      if ((c == '*' || c == '+' || c == '?') && (i == 0 || p[i - 1] == '|' || p[i - 1] == '(')) {
        snprintf(err, err_cap, "quantifier without atom");
        return 0;
      }
    } else {
      if (c == ']') {
        if (!class_has_content) {
          snprintf(err, err_cap, "empty character class");
          return 0;
        }
        in_class = 0;
        continue;
      }
      class_has_content = 1;
      if (c == '-' && i > 0 && i + 1 < len && p[i - 1] != '[' && p[i + 1] != ']') {
        unsigned char a = (unsigned char)p[i - 1];
        unsigned char b = (unsigned char)p[i + 1];
        if (a > b) {
          snprintf(err, err_cap, "inverted range in class");
          return 0;
        }
      }
    }
  }
  if (in_class) {
    snprintf(err, err_cap, "unclosed character class");
    return 0;
  }
  if (paren != 0) {
    snprintf(err, err_cap, "unclosed parenthesis");
    return 0;
  }
  return 1;
}

static int normalize_flags(PS_Context *ctx, const char *in, size_t len, char out[4], int *out_cflags) {
  int has_i = 0, has_m = 0, has_s = 0;
  for (size_t i = 0; i < len; i++) {
    char c = in[i];
    if (c == 'i') has_i = 1;
    else if (c == 'm') has_m = 1;
    else if (c == 's') has_s = 1;
    else return rx_syntax(ctx, "unsupported flag");
  }
  size_t w = 0;
  if (has_i) out[w++] = 'i';
  if (has_m) out[w++] = 'm';
  if (has_s) out[w++] = 's';
  out[w] = '\0';
  int cflags = REG_EXTENDED;
  if (has_i) cflags |= REG_ICASE;
#ifdef REG_NEWLINE
  if (has_m) cflags |= REG_NEWLINE;
#endif
  (void)has_s;
  *out_cflags = cflags;
  return PS_OK;
}

static int convert_pattern_to_posix(const char *in, size_t len, char **out, int map[100], int *logical_groups, char *err, size_t err_cap) {
  size_t cap = len + 16;
  char *buf = (char *)malloc(cap);
  if (!buf) return 0;
  size_t w = 0;
  int phys = 0;
  int logical = 0;
  int in_class = 0;
  for (size_t i = 0; i < len; i++) {
    char c = in[i];
    if (c == '\\') {
      if (!in_class && i + 1 < len) {
        const char *mapped = NULL;
        char next = in[i + 1];
        if (next == 'd') mapped = "[0-9]";
        else if (next == 'D') mapped = "[^0-9]";
        else if (next == 'w') mapped = "[A-Za-z0-9_]";
        else if (next == 'W') mapped = "[^A-Za-z0-9_]";
        else if (next == 's') mapped = "[ \t\r\n\f\v]";
        else if (next == 'S') mapped = "[^ \t\r\n\f\v]";
        if (mapped) {
          size_t ml = strlen(mapped);
          if (w + ml + 1 >= cap) {
            size_t ncap = cap;
            while (w + ml + 1 >= ncap) ncap *= 2;
            char *nb = (char *)realloc(buf, ncap);
            if (!nb) {
              free(buf);
              return 0;
            }
            buf = nb;
            cap = ncap;
          }
          memcpy(buf + w, mapped, ml);
          w += ml;
          i += 1;
          continue;
        }
      }
      if (w + 2 >= cap) {
        size_t ncap = cap * 2;
        char *nb = (char *)realloc(buf, ncap);
        if (!nb) {
          free(buf);
          return 0;
        }
        buf = nb;
        cap = ncap;
      }
      buf[w++] = c;
      if (i + 1 < len) {
        buf[w++] = in[i + 1];
        i += 1;
      }
      continue;
    }
    if (!in_class && c == '[') in_class = 1;
    else if (in_class && c == ']') in_class = 0;

    if (!in_class && c == '(' && i + 2 < len && in[i + 1] == '?' && in[i + 2] == ':') {
      phys += 1;
      if (w + 1 >= cap) {
        cap *= 2;
        char *nb = (char *)realloc(buf, cap);
        if (!nb) {
          free(buf);
          return 0;
        }
        buf = nb;
      }
      buf[w++] = '(';
      i += 2;
      continue;
    }

    if (!in_class && c == '(') {
      phys += 1;
      logical += 1;
      if (logical >= 100) {
        snprintf(err, err_cap, "too many capturing groups (max 99)");
        free(buf);
        return 0;
      }
      map[logical] = phys;
    }

    if (w + 1 >= cap) {
      cap *= 2;
      char *nb = (char *)realloc(buf, cap);
      if (!nb) {
        free(buf);
        return 0;
      }
      buf = nb;
    }
    buf[w++] = c;
  }
  if (w + 1 >= cap) {
    char *nb = (char *)realloc(buf, w + 1);
    if (!nb) {
      free(buf);
      return 0;
    }
    buf = nb;
  }
  buf[w] = '\0';
  *out = buf;
  *logical_groups = logical;
  return 1;
}

static PS_Status regex_from_obj(PS_Context *ctx, PS_Value *obj, RegexEntry **out_entry) {
  if (!obj || ps_typeof(obj) != PS_T_OBJECT) {
    ps_throw(ctx, PS_ERR_TYPE, "expected RegExp");
    return PS_ERR;
  }
  PS_Value *v = ps_object_get_str(ctx, obj, "_rid", 4);
  if (!v || ps_typeof(v) != PS_T_INT) return rx_throw(ctx, PS_ERR_TYPE, "RegExpRange", "invalid RegExp handle");
  int64_t id64 = ps_as_int(v);
  if (id64 <= 0 || id64 > INT_MAX) return rx_throw(ctx, PS_ERR_TYPE, "RegExpRange", "invalid RegExp handle");
  RegexEntry *e = store_find((int)id64);
  if (!e) return rx_throw(ctx, PS_ERR_TYPE, "RegExpRange", "unknown RegExp handle");
  *out_entry = e;
  return PS_OK;
}

static PS_Status obj_set_owned(PS_Context *ctx, PS_Value *obj, const char *key, PS_Value *v) {
  if (!obj || !v) return PS_ERR;
  if (ps_object_set_str(ctx, obj, key, strlen(key), v) != PS_OK) {
    ps_value_release(v);
    return PS_ERR;
  }
  ps_value_release(v);
  return PS_OK;
}

static PS_Status make_match_obj(PS_Context *ctx, int ok, int64_t start, int64_t end, PS_Value *groups, PS_Value **out) {
  PS_Value *obj = ps_make_object(ctx);
  if (!obj) return PS_ERR;
  if (ps_object_set_proto_name(ctx, obj, "RegExpMatch") != PS_OK) {
    ps_value_release(obj);
    return PS_ERR;
  }
  PS_Value *v_ok = ps_make_bool(ctx, ok);
  PS_Value *v_start = ps_make_int(ctx, start);
  PS_Value *v_end = ps_make_int(ctx, end);
  if (!v_ok || !v_start || !v_end) {
    if (v_ok) ps_value_release(v_ok);
    if (v_start) ps_value_release(v_start);
    if (v_end) ps_value_release(v_end);
    ps_value_release(obj);
    return PS_ERR;
  }
  if (obj_set_owned(ctx, obj, "ok", v_ok) != PS_OK ||
      obj_set_owned(ctx, obj, "start", v_start) != PS_OK ||
      obj_set_owned(ctx, obj, "end", v_end) != PS_OK ||
      obj_set_owned(ctx, obj, "groups", ps_value_retain(groups)) != PS_OK) {
    ps_value_release(obj);
    return PS_ERR;
  }
  *out = obj;
  return PS_OK;
}

static PS_Status make_empty_match(PS_Context *ctx, PS_Value **out) {
  PS_Value *groups = ps_make_list(ctx);
  if (!groups) return PS_ERR;
  PS_Status st = make_match_obj(ctx, 0, 0, 0, groups, out);
  ps_value_release(groups);
  return st;
}

static PS_Value *make_utf8_string_slice(PS_Context *ctx, const char *s, size_t a, size_t b) {
  if (b < a) b = a;
  return ps_make_string_utf8(ctx, s + a, b - a);
}

static int resolve_capture_index(const RegexEntry *e, int logical_idx) {
  if (logical_idx < 0) return -1;
  if (logical_idx == 0) return 0;
  if (!e || logical_idx > e->logical_groups || logical_idx >= 100) return -1;
  return e->logical_to_phys[logical_idx];
}

static PS_Status run_find(PS_Context *ctx, RegexEntry *e, const char *input, size_t input_len, const GlyphIndex *idx, size_t start_glyph, PS_Value **out_match) {
  (void)input_len;
  size_t start_byte = glyph_to_byte_pos(idx, start_glyph);
  size_t nmatch = (size_t)e->re.re_nsub + 1;
  regmatch_t *pm = (regmatch_t *)calloc(nmatch, sizeof(regmatch_t));
  if (!pm) return rx_throw(ctx, PS_ERR_OOM, "RegExpLimit", "out of memory");

  int rc = regexec(&e->re, input + start_byte, nmatch, pm, 0);
  if (rc == REG_NOMATCH) {
    free(pm);
    return make_empty_match(ctx, out_match);
  }
  if (rc != 0) {
    char errbuf[256];
    regerror(rc, &e->re, errbuf, sizeof(errbuf));
    free(pm);
    return rx_throw(ctx, PS_ERR_INTERNAL, "RegExpLimit", errbuf);
  }

  if (pm[0].rm_so < 0 || pm[0].rm_eo < 0) {
    free(pm);
    return make_empty_match(ctx, out_match);
  }

  size_t m0s = start_byte + (size_t)pm[0].rm_so;
  size_t m0e = start_byte + (size_t)pm[0].rm_eo;
  int64_t g0s = (int64_t)byte_to_glyph_pos(idx, m0s);
  int64_t g0e = (int64_t)byte_to_glyph_pos(idx, m0e);

  PS_Value *groups = ps_make_list(ctx);
  if (!groups) {
    free(pm);
    return PS_ERR;
  }

  PS_Value *whole = make_utf8_string_slice(ctx, input, m0s, m0e);
  if (!whole || ps_list_push(ctx, groups, whole) != PS_OK) {
    if (whole) ps_value_release(whole);
    ps_value_release(groups);
    free(pm);
    return PS_ERR;
  }
  ps_value_release(whole);

  for (int li = 1; li <= e->logical_groups; li++) {
    int pi = resolve_capture_index(e, li);
    PS_Value *part = NULL;
    if (pi >= 0 && (size_t)pi < nmatch && pm[pi].rm_so >= 0 && pm[pi].rm_eo >= 0) {
      size_t bs = start_byte + (size_t)pm[pi].rm_so;
      size_t be = start_byte + (size_t)pm[pi].rm_eo;
      part = make_utf8_string_slice(ctx, input, bs, be);
    } else {
      part = ps_make_string_utf8(ctx, "", 0);
    }
    if (!part || ps_list_push(ctx, groups, part) != PS_OK) {
      if (part) ps_value_release(part);
      ps_value_release(groups);
      free(pm);
      return PS_ERR;
    }
    ps_value_release(part);
  }

  PS_Status st = make_match_obj(ctx, 1, g0s, g0e, groups, out_match);
  ps_value_release(groups);
  free(pm);
  return st;
}

static PS_Status replacement_expand(PS_Context *ctx, const char *replacement, size_t replacement_len, PS_Value *groups, char **out, size_t *out_len) {
  size_t cap = replacement_len + 32;
  char *buf = (char *)malloc(cap);
  if (!buf) return rx_throw(ctx, PS_ERR_OOM, "RegExpLimit", "out of memory");
  size_t w = 0;
  size_t glen = ps_list_len(groups);
  for (size_t i = 0; i < replacement_len; i++) {
    char c = replacement[i];
    if (c != '$') {
      if (w + 1 > cap) {
        cap *= 2;
        char *nb = (char *)realloc(buf, cap);
        if (!nb) {
          free(buf);
          return rx_throw(ctx, PS_ERR_OOM, "RegExpLimit", "out of memory");
        }
        buf = nb;
      }
      buf[w++] = c;
      continue;
    }

    if (i + 1 < replacement_len && replacement[i + 1] == '$') {
      if (w + 1 > cap) {
        cap *= 2;
        char *nb = (char *)realloc(buf, cap);
        if (!nb) {
          free(buf);
          return rx_throw(ctx, PS_ERR_OOM, "RegExpLimit", "out of memory");
        }
        buf = nb;
      }
      buf[w++] = '$';
      i += 1;
      continue;
    }

    if (i + 1 >= replacement_len || !isdigit((unsigned char)replacement[i + 1])) {
      if (w + 1 > cap) {
        cap *= 2;
        char *nb = (char *)realloc(buf, cap);
        if (!nb) {
          free(buf);
          return rx_throw(ctx, PS_ERR_OOM, "RegExpLimit", "out of memory");
        }
        buf = nb;
      }
      buf[w++] = '$';
      continue;
    }

    int idx = replacement[i + 1] - '0';
    i += 1;
    if (i + 1 < replacement_len && isdigit((unsigned char)replacement[i + 1])) {
      idx = idx * 10 + (replacement[i + 1] - '0');
      i += 1;
    }

    if (idx < 0 || (size_t)idx >= glen) continue;
    PS_Value *gv = ps_list_get(ctx, groups, (size_t)idx);
    if (!gv || ps_typeof(gv) != PS_T_STRING) continue;
    const char *gs = ps_string_ptr(gv);
    size_t gs_len = ps_string_len(gv);
    if (w + gs_len > cap) {
      size_t ncap = cap;
      while (w + gs_len > ncap) ncap *= 2;
      char *nb = (char *)realloc(buf, ncap);
      if (!nb) {
        free(buf);
        return rx_throw(ctx, PS_ERR_OOM, "RegExpLimit", "out of memory");
      }
      buf = nb;
      cap = ncap;
    }
    memcpy(buf + w, gs, gs_len);
    w += gs_len;
  }

  *out = buf;
  *out_len = w;
  return PS_OK;
}

static PS_Status mod_compile(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  const char *pattern = NULL, *flags = NULL;
  size_t pattern_len = 0, flags_len = 0;
  if (get_string_arg(ctx, argv[0], &pattern, &pattern_len) != PS_OK) return PS_ERR;
  if (get_string_arg(ctx, argv[1], &flags, &flags_len) != PS_OK) return PS_ERR;

  if (!utf8_validate(pattern, pattern_len, NULL)) return rx_syntax(ctx, "pattern must be valid UTF-8");
  if (!utf8_validate(flags, flags_len, NULL)) return rx_syntax(ctx, "flags must be valid UTF-8");
  if (has_forbidden_meta(pattern, pattern_len)) return rx_syntax(ctx, "forbidden metasyntax (backreference/lookaround)");

  char synerr[160];
  if (!validate_basic_syntax(pattern, pattern_len, synerr, sizeof(synerr))) return rx_syntax(ctx, synerr);

  RegexEntry *e = store_add();
  if (!e) return rx_throw(ctx, PS_ERR_OOM, "RegExpLimit", "out of memory");

  int cflags = 0;
  if (normalize_flags(ctx, flags, flags_len, e->flags, &cflags) != PS_OK) {
    e->valid = 0;
    return PS_ERR;
  }
  e->cflags = cflags;

  char conv_err[160] = {0};
  if (!convert_pattern_to_posix(pattern, pattern_len, &e->pattern_posix, e->logical_to_phys, &e->logical_groups, conv_err, sizeof(conv_err))) {
    e->valid = 0;
    if (conv_err[0]) return rx_limit(ctx, conv_err);
    return rx_throw(ctx, PS_ERR_OOM, "RegExpLimit", "out of memory");
  }

  e->pattern = (char *)malloc(pattern_len + 1);
  if (!e->pattern) {
    e->valid = 0;
    return rx_throw(ctx, PS_ERR_OOM, "RegExpLimit", "out of memory");
  }
  memcpy(e->pattern, pattern, pattern_len);
  e->pattern[pattern_len] = '\0';

  int rc = regcomp(&e->re, e->pattern_posix, cflags);
  if (rc != 0) {
    char errbuf[256];
    regerror(rc, &e->re, errbuf, sizeof(errbuf));
    e->valid = 0;
    return rx_syntax(ctx, errbuf);
  }

  PS_Value *obj = ps_make_object(ctx);
  if (!obj) return PS_ERR;
  if (ps_object_set_proto_name(ctx, obj, "RegExp") != PS_OK) {
    ps_value_release(obj);
    return PS_ERR;
  }

  PS_Value *v_id = ps_make_int(ctx, e->id);
  PS_Value *v_pat = ps_make_string_utf8(ctx, e->pattern, strlen(e->pattern));
  PS_Value *v_flags = ps_make_string_utf8(ctx, e->flags, strlen(e->flags));
  if (!v_id || !v_pat || !v_flags) {
    if (v_id) ps_value_release(v_id);
    if (v_pat) ps_value_release(v_pat);
    if (v_flags) ps_value_release(v_flags);
    ps_value_release(obj);
    return PS_ERR;
  }
  if (obj_set_owned(ctx, obj, "_rid", v_id) != PS_OK ||
      obj_set_owned(ctx, obj, "_pattern", v_pat) != PS_OK ||
      obj_set_owned(ctx, obj, "_flags", v_flags) != PS_OK) {
    ps_value_release(obj);
    return PS_ERR;
  }

  *out = obj;
  return PS_OK;
}

static PS_Status mod_test(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  RegexEntry *e = NULL;
  if (regex_from_obj(ctx, argv[0], &e) != PS_OK) return PS_ERR;

  const char *input = NULL;
  size_t input_len = 0;
  if (get_string_arg(ctx, argv[1], &input, &input_len) != PS_OK) return PS_ERR;

  int64_t start = 0;
  if (get_int_arg(ctx, argv[2], &start) != PS_OK) return PS_ERR;

  GlyphIndex idx;
  if (!glyph_index_build(input, input_len, &idx)) return rx_throw(ctx, PS_ERR_UTF8, "RegExpSyntax", "input must be valid UTF-8");
  if (start < 0 || (size_t)start > idx.glyph_count) {
    glyph_index_free(&idx);
    return rx_range(ctx, "start out of range");
  }

  PS_Value *m = NULL;
  PS_Status st = run_find(ctx, e, input, input_len, &idx, (size_t)start, &m);
  glyph_index_free(&idx);
  if (st != PS_OK) return PS_ERR;
  PS_Value *ok = ps_object_get_str(ctx, m, "ok", 2);
  int b = (ok && ps_typeof(ok) == PS_T_BOOL) ? ps_as_bool(ok) : 0;
  ps_value_release(m);
  *out = ps_make_bool(ctx, b);
  return *out ? PS_OK : PS_ERR;
}

static PS_Status mod_find(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  RegexEntry *e = NULL;
  if (regex_from_obj(ctx, argv[0], &e) != PS_OK) return PS_ERR;

  const char *input = NULL;
  size_t input_len = 0;
  if (get_string_arg(ctx, argv[1], &input, &input_len) != PS_OK) return PS_ERR;

  int64_t start = 0;
  if (get_int_arg(ctx, argv[2], &start) != PS_OK) return PS_ERR;

  GlyphIndex idx;
  if (!glyph_index_build(input, input_len, &idx)) return rx_throw(ctx, PS_ERR_UTF8, "RegExpSyntax", "input must be valid UTF-8");
  if (start < 0 || (size_t)start > idx.glyph_count) {
    glyph_index_free(&idx);
    return rx_range(ctx, "start out of range");
  }

  PS_Status st = run_find(ctx, e, input, input_len, &idx, (size_t)start, out);
  glyph_index_free(&idx);
  return st;
}

static PS_Status get_match_span(PS_Context *ctx, PS_Value *m, int64_t *out_start, int64_t *out_end, int *out_ok, PS_Value **out_groups) {
  PS_Value *ok = ps_object_get_str(ctx, m, "ok", 2);
  PS_Value *st = ps_object_get_str(ctx, m, "start", 5);
  PS_Value *en = ps_object_get_str(ctx, m, "end", 3);
  PS_Value *gr = ps_object_get_str(ctx, m, "groups", 6);
  if (!ok || !st || !en || !gr) return PS_ERR;
  if (ps_typeof(ok) != PS_T_BOOL || ps_typeof(st) != PS_T_INT || ps_typeof(en) != PS_T_INT || ps_typeof(gr) != PS_T_LIST) return PS_ERR;
  *out_ok = ps_as_bool(ok) ? 1 : 0;
  *out_start = ps_as_int(st);
  *out_end = ps_as_int(en);
  *out_groups = gr;
  return PS_OK;
}

static PS_Status mod_find_all(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  RegexEntry *e = NULL;
  if (regex_from_obj(ctx, argv[0], &e) != PS_OK) return PS_ERR;

  const char *input = NULL;
  size_t input_len = 0;
  if (get_string_arg(ctx, argv[1], &input, &input_len) != PS_OK) return PS_ERR;

  int64_t start = 0;
  int64_t max = 0;
  if (get_int_arg(ctx, argv[2], &start) != PS_OK) return PS_ERR;
  if (get_int_arg(ctx, argv[3], &max) != PS_OK) return PS_ERR;

  GlyphIndex idx;
  if (!glyph_index_build(input, input_len, &idx)) return rx_throw(ctx, PS_ERR_UTF8, "RegExpSyntax", "input must be valid UTF-8");
  if (start < 0 || (size_t)start > idx.glyph_count) {
    glyph_index_free(&idx);
    return rx_range(ctx, "start out of range");
  }
  if (max < -1) {
    glyph_index_free(&idx);
    return rx_range(ctx, "max out of range");
  }

  PS_Value *list = ps_make_list(ctx);
  if (!list) {
    glyph_index_free(&idx);
    return PS_ERR;
  }
  if (max == 0) {
    glyph_index_free(&idx);
    *out = list;
    return PS_OK;
  }

  size_t cur = (size_t)start;
  int64_t produced = 0;
  while (cur <= idx.glyph_count) {
    PS_Value *m = NULL;
    if (run_find(ctx, e, input, input_len, &idx, cur, &m) != PS_OK) {
      ps_value_release(list);
      glyph_index_free(&idx);
      return PS_ERR;
    }
    int ok = 0;
    int64_t ms = 0, me = 0;
    PS_Value *groups = NULL;
    if (get_match_span(ctx, m, &ms, &me, &ok, &groups) != PS_OK) {
      ps_value_release(m);
      ps_value_release(list);
      glyph_index_free(&idx);
      return PS_ERR;
    }
    if (!ok) {
      ps_value_release(m);
      break;
    }
    if (ps_list_push(ctx, list, m) != PS_OK) {
      ps_value_release(m);
      ps_value_release(list);
      glyph_index_free(&idx);
      return PS_ERR;
    }
    ps_value_release(m);
    produced += 1;
    if (max > 0 && produced >= max) break;
    if (me <= ms) {
      if ((size_t)me >= idx.glyph_count) break;
      cur = (size_t)me + 1;
    } else {
      cur = (size_t)me;
    }
  }

  glyph_index_free(&idx);
  *out = list;
  return PS_OK;
}

static PS_Status append_bytes(char **buf, size_t *len, size_t *cap, const char *src, size_t src_len) {
  if (*len + src_len > *cap) {
    size_t ncap = *cap;
    while (*len + src_len > ncap) ncap *= 2;
    char *nb = (char *)realloc(*buf, ncap);
    if (!nb) return PS_ERR;
    *buf = nb;
    *cap = ncap;
  }
  memcpy(*buf + *len, src, src_len);
  *len += src_len;
  return PS_OK;
}

static PS_Status replace_impl(PS_Context *ctx, RegexEntry *e, const char *input, size_t input_len, size_t start_glyph, const char *replacement, size_t replacement_len, int64_t max, int replace_all, PS_Value **out) {
  GlyphIndex idx;
  if (!glyph_index_build(input, input_len, &idx)) return rx_throw(ctx, PS_ERR_UTF8, "RegExpSyntax", "input must be valid UTF-8");

  size_t cap = input_len + replacement_len + 32;
  char *buf = (char *)malloc(cap);
  if (!buf) {
    glyph_index_free(&idx);
    return rx_throw(ctx, PS_ERR_OOM, "RegExpLimit", "out of memory");
  }
  size_t w = 0;

  size_t cursor_g = start_glyph;
  size_t cursor_b = glyph_to_byte_pos(&idx, start_glyph);
  if (append_bytes(&buf, &w, &cap, input, cursor_b) != PS_OK) {
    free(buf);
    glyph_index_free(&idx);
    return rx_throw(ctx, PS_ERR_OOM, "RegExpLimit", "out of memory");
  }

  int64_t done = 0;
  while (cursor_g <= idx.glyph_count) {
    PS_Value *m = NULL;
    if (run_find(ctx, e, input, input_len, &idx, cursor_g, &m) != PS_OK) {
      free(buf);
      glyph_index_free(&idx);
      return PS_ERR;
    }

    int ok = 0;
    int64_t ms = 0, me = 0;
    PS_Value *groups = NULL;
    if (get_match_span(ctx, m, &ms, &me, &ok, &groups) != PS_OK) {
      ps_value_release(m);
      free(buf);
      glyph_index_free(&idx);
      return PS_ERR;
    }
    if (!ok) {
      ps_value_release(m);
      break;
    }

    size_t mbs = glyph_to_byte_pos(&idx, (size_t)ms);
    size_t mbe = glyph_to_byte_pos(&idx, (size_t)me);
    if (mbs < cursor_b) mbs = cursor_b;

    if (append_bytes(&buf, &w, &cap, input + cursor_b, mbs - cursor_b) != PS_OK) {
      ps_value_release(m);
      free(buf);
      glyph_index_free(&idx);
      return rx_throw(ctx, PS_ERR_OOM, "RegExpLimit", "out of memory");
    }

    char *exp = NULL;
    size_t exp_len = 0;
    if (replacement_expand(ctx, replacement, replacement_len, groups, &exp, &exp_len) != PS_OK) {
      ps_value_release(m);
      free(buf);
      glyph_index_free(&idx);
      return PS_ERR;
    }
    PS_Status ap = append_bytes(&buf, &w, &cap, exp, exp_len);
    free(exp);
    if (ap != PS_OK) {
      ps_value_release(m);
      free(buf);
      glyph_index_free(&idx);
      return rx_throw(ctx, PS_ERR_OOM, "RegExpLimit", "out of memory");
    }

    done += 1;
    cursor_b = mbe;
    if (!replace_all || (max > 0 && done >= max)) {
      ps_value_release(m);
      break;
    }
    if (me <= ms) {
      if ((size_t)me >= idx.glyph_count) {
        ps_value_release(m);
        break;
      }
      size_t next_g = (size_t)me + 1;
      size_t next_b = glyph_to_byte_pos(&idx, next_g);
      if (append_bytes(&buf, &w, &cap, input + cursor_b, next_b - cursor_b) != PS_OK) {
        ps_value_release(m);
        free(buf);
        glyph_index_free(&idx);
        return rx_throw(ctx, PS_ERR_OOM, "RegExpLimit", "out of memory");
      }
      cursor_g = next_g;
      cursor_b = next_b;
    } else {
      cursor_g = (size_t)me;
    }
    ps_value_release(m);
  }

  if (cursor_b < input_len) {
    if (append_bytes(&buf, &w, &cap, input + cursor_b, input_len - cursor_b) != PS_OK) {
      free(buf);
      glyph_index_free(&idx);
      return rx_throw(ctx, PS_ERR_OOM, "RegExpLimit", "out of memory");
    }
  }

  PS_Value *res = ps_make_string_utf8(ctx, buf, w);
  free(buf);
  glyph_index_free(&idx);
  if (!res) return PS_ERR;
  *out = res;
  return PS_OK;
}

static PS_Status mod_replace_first(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  RegexEntry *e = NULL;
  if (regex_from_obj(ctx, argv[0], &e) != PS_OK) return PS_ERR;
  const char *input = NULL, *replacement = NULL;
  size_t input_len = 0, replacement_len = 0;
  int64_t start = 0;
  if (get_string_arg(ctx, argv[1], &input, &input_len) != PS_OK) return PS_ERR;
  if (get_string_arg(ctx, argv[2], &replacement, &replacement_len) != PS_OK) return PS_ERR;
  if (get_int_arg(ctx, argv[3], &start) != PS_OK) return PS_ERR;

  size_t glyphs = 0;
  if (!utf8_validate(input, input_len, &glyphs)) return rx_throw(ctx, PS_ERR_UTF8, "RegExpSyntax", "input must be valid UTF-8");
  if (start < 0 || (size_t)start > glyphs) return rx_range(ctx, "start out of range");

  return replace_impl(ctx, e, input, input_len, (size_t)start, replacement, replacement_len, 1, 0, out);
}

static PS_Status mod_replace_all(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  RegexEntry *e = NULL;
  if (regex_from_obj(ctx, argv[0], &e) != PS_OK) return PS_ERR;
  const char *input = NULL, *replacement = NULL;
  size_t input_len = 0, replacement_len = 0;
  int64_t start = 0, max = 0;
  if (get_string_arg(ctx, argv[1], &input, &input_len) != PS_OK) return PS_ERR;
  if (get_string_arg(ctx, argv[2], &replacement, &replacement_len) != PS_OK) return PS_ERR;
  if (get_int_arg(ctx, argv[3], &start) != PS_OK) return PS_ERR;
  if (get_int_arg(ctx, argv[4], &max) != PS_OK) return PS_ERR;

  size_t glyphs = 0;
  if (!utf8_validate(input, input_len, &glyphs)) return rx_throw(ctx, PS_ERR_UTF8, "RegExpSyntax", "input must be valid UTF-8");
  if (start < 0 || (size_t)start > glyphs) return rx_range(ctx, "start out of range");
  if (max < -1) return rx_range(ctx, "max out of range");
  if (max == 0) {
    *out = ps_make_string_utf8(ctx, input, input_len);
    return *out ? PS_OK : PS_ERR;
  }
  return replace_impl(ctx, e, input, input_len, (size_t)start, replacement, replacement_len, max, 1, out);
}

static PS_Status mod_split(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  RegexEntry *e = NULL;
  if (regex_from_obj(ctx, argv[0], &e) != PS_OK) return PS_ERR;

  const char *input = NULL;
  size_t input_len = 0;
  int64_t start = 0;
  int64_t max_parts = 0;
  if (get_string_arg(ctx, argv[1], &input, &input_len) != PS_OK) return PS_ERR;
  if (get_int_arg(ctx, argv[2], &start) != PS_OK) return PS_ERR;
  if (get_int_arg(ctx, argv[3], &max_parts) != PS_OK) return PS_ERR;

  GlyphIndex idx;
  if (!glyph_index_build(input, input_len, &idx)) return rx_throw(ctx, PS_ERR_UTF8, "RegExpSyntax", "input must be valid UTF-8");
  if (start < 0 || (size_t)start > idx.glyph_count) {
    glyph_index_free(&idx);
    return rx_range(ctx, "start out of range");
  }
  if (max_parts < -1) {
    glyph_index_free(&idx);
    return rx_range(ctx, "maxParts out of range");
  }

  PS_Value *list = ps_make_list(ctx);
  if (!list) {
    glyph_index_free(&idx);
    return PS_ERR;
  }
  if (max_parts == 0) {
    glyph_index_free(&idx);
    *out = list;
    return PS_OK;
  }

  size_t cur = (size_t)start;
  size_t cur_b = glyph_to_byte_pos(&idx, cur);
  int64_t parts = 0;

  if (max_parts == 1) {
    PS_Value *tail = ps_make_string_utf8(ctx, input + cur_b, input_len - cur_b);
    if (!tail || ps_list_push(ctx, list, tail) != PS_OK) {
      if (tail) ps_value_release(tail);
      ps_value_release(list);
      glyph_index_free(&idx);
      return PS_ERR;
    }
    ps_value_release(tail);
    glyph_index_free(&idx);
    *out = list;
    return PS_OK;
  }

  int64_t limit = (max_parts < 0) ? INT64_MAX : max_parts;
  while (cur <= idx.glyph_count && parts + 1 < limit) {
    PS_Value *m = NULL;
    if (run_find(ctx, e, input, input_len, &idx, cur, &m) != PS_OK) {
      ps_value_release(list);
      glyph_index_free(&idx);
      return PS_ERR;
    }
    int ok = 0;
    int64_t ms = 0, me = 0;
    PS_Value *groups = NULL;
    if (get_match_span(ctx, m, &ms, &me, &ok, &groups) != PS_OK) {
      ps_value_release(m);
      ps_value_release(list);
      glyph_index_free(&idx);
      return PS_ERR;
    }
    if (!ok) {
      ps_value_release(m);
      break;
    }

    size_t mbs = glyph_to_byte_pos(&idx, (size_t)ms);
    PS_Value *part = ps_make_string_utf8(ctx, input + cur_b, mbs - cur_b);
    if (!part || ps_list_push(ctx, list, part) != PS_OK) {
      if (part) ps_value_release(part);
      ps_value_release(m);
      ps_value_release(list);
      glyph_index_free(&idx);
      return PS_ERR;
    }
    ps_value_release(part);
    parts += 1;

    if (me <= ms) {
      if ((size_t)me >= idx.glyph_count) {
        cur = (size_t)me;
        cur_b = glyph_to_byte_pos(&idx, cur);
        ps_value_release(m);
        break;
      }
      cur = (size_t)me + 1;
      cur_b = glyph_to_byte_pos(&idx, cur);
    } else {
      cur = (size_t)me;
      cur_b = glyph_to_byte_pos(&idx, cur);
    }
    ps_value_release(m);
  }

  if (parts < limit) {
    PS_Value *tail = ps_make_string_utf8(ctx, input + cur_b, input_len - cur_b);
    if (!tail || ps_list_push(ctx, list, tail) != PS_OK) {
      if (tail) ps_value_release(tail);
      ps_value_release(list);
      glyph_index_free(&idx);
      return PS_ERR;
    }
    ps_value_release(tail);
  }

  glyph_index_free(&idx);
  *out = list;
  return PS_OK;
}

static PS_Status mod_pattern(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  RegexEntry *e = NULL;
  if (regex_from_obj(ctx, argv[0], &e) != PS_OK) return PS_ERR;
  *out = ps_make_string_utf8(ctx, e->pattern, strlen(e->pattern));
  return *out ? PS_OK : PS_ERR;
}

static PS_Status mod_flags(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  RegexEntry *e = NULL;
  if (regex_from_obj(ctx, argv[0], &e) != PS_OK) return PS_ERR;
  *out = ps_make_string_utf8(ctx, e->flags, strlen(e->flags));
  return *out ? PS_OK : PS_ERR;
}

static const PS_TypeTag params_compile[] = {PS_T_STRING, PS_T_STRING};
static const PS_TypeTag params_test[] = {PS_T_OBJECT, PS_T_STRING, PS_T_INT};
static const PS_TypeTag params_find[] = {PS_T_OBJECT, PS_T_STRING, PS_T_INT};
static const PS_TypeTag params_find_all[] = {PS_T_OBJECT, PS_T_STRING, PS_T_INT, PS_T_INT};
static const PS_TypeTag params_replace_first[] = {PS_T_OBJECT, PS_T_STRING, PS_T_STRING, PS_T_INT};
static const PS_TypeTag params_replace_all[] = {PS_T_OBJECT, PS_T_STRING, PS_T_STRING, PS_T_INT, PS_T_INT};
static const PS_TypeTag params_split[] = {PS_T_OBJECT, PS_T_STRING, PS_T_INT, PS_T_INT};
static const PS_TypeTag params_pattern[] = {PS_T_OBJECT};
static const PS_TypeTag params_flags[] = {PS_T_OBJECT};

PS_Status ps_module_init(PS_Context *ctx, PS_Module *out) {
  (void)ctx;
  if (!out) return PS_ERR;
  static const PS_NativeFnDesc fns[] = {
    {.name = "compile", .fn = mod_compile, .arity = 2, .ret_type = PS_T_OBJECT, .param_types = params_compile, .flags = 0},
    {.name = "test", .fn = mod_test, .arity = 3, .ret_type = PS_T_BOOL, .param_types = params_test, .flags = 0},
    {.name = "find", .fn = mod_find, .arity = 3, .ret_type = PS_T_OBJECT, .param_types = params_find, .flags = 0},
    {.name = "findAll", .fn = mod_find_all, .arity = 4, .ret_type = PS_T_LIST, .param_types = params_find_all, .flags = 0},
    {.name = "replaceFirst", .fn = mod_replace_first, .arity = 4, .ret_type = PS_T_STRING, .param_types = params_replace_first, .flags = 0},
    {.name = "replaceAll", .fn = mod_replace_all, .arity = 5, .ret_type = PS_T_STRING, .param_types = params_replace_all, .flags = 0},
    {.name = "split", .fn = mod_split, .arity = 4, .ret_type = PS_T_LIST, .param_types = params_split, .flags = 0},
    {.name = "pattern", .fn = mod_pattern, .arity = 1, .ret_type = PS_T_STRING, .param_types = params_pattern, .flags = 0},
    {.name = "flags", .fn = mod_flags, .arity = 1, .ret_type = PS_T_STRING, .param_types = params_flags, .flags = 0},
  };

  out->module_name = "RegExp";
  out->api_version = PS_API_VERSION;
  out->fn_count = sizeof(fns) / sizeof(fns[0]);
  out->fns = fns;
  out->proto_count = 0;
  out->protos = NULL;
  out->debug_dump = NULL;
  return PS_OK;
}
