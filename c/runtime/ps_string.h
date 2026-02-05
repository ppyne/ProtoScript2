#ifndef PS_STRING_H
#define PS_STRING_H

#include <stddef.h>
#include <stdint.h>

#include "ps_runtime.h"

int ps_utf8_validate(const uint8_t *s, size_t len);
size_t ps_utf8_glyph_len(const uint8_t *s, size_t len);
uint32_t ps_utf8_glyph_at(const uint8_t *s, size_t len, size_t index);

PS_Value *ps_string_from_utf8(PS_Context *ctx, const char *s, size_t len);
PS_Value *ps_string_concat(PS_Context *ctx, PS_Value *a, PS_Value *b);
PS_Value *ps_string_substring(PS_Context *ctx, PS_Value *s, size_t start, size_t length);
int64_t ps_string_index_of(PS_Value *hay, PS_Value *needle);
int ps_string_starts_with(PS_Value *s, PS_Value *prefix);
int ps_string_ends_with(PS_Value *s, PS_Value *suffix);
PS_Value *ps_string_trim(PS_Context *ctx, PS_Value *s, int mode); // 0 both, 1 start, 2 end
PS_Value *ps_string_replace(PS_Context *ctx, PS_Value *s, PS_Value *from, PS_Value *to);
PS_Value *ps_string_to_upper(PS_Context *ctx, PS_Value *s);
PS_Value *ps_string_to_lower(PS_Context *ctx, PS_Value *s);
PS_Value *ps_string_split(PS_Context *ctx, PS_Value *s, PS_Value *sep);

#endif // PS_STRING_H
