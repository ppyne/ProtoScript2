#ifndef PS_VALUE_IMPL_H
#define PS_VALUE_IMPL_H

#include <stddef.h>
#include <stdint.h>

#include "ps/ps_api.h"

typedef struct PS_Value PS_Value;

typedef enum {
  PS_V_BOOL,
  PS_V_INT,
  PS_V_FLOAT,
  PS_V_BYTE,
  PS_V_GLYPH,
  PS_V_STRING,
  PS_V_BYTES,
  PS_V_LIST,
  PS_V_OBJECT,
  PS_V_MAP,
  PS_V_VIEW,
  PS_V_ITER,
  PS_V_FILE,
  PS_V_VOID
} PS_ValueTag;

typedef struct {
  char *ptr;
  size_t len;
} PS_String;

typedef struct {
  uint8_t *ptr;
  size_t len;
} PS_Bytes;

typedef struct {
  PS_Value **items;
  size_t len;
  size_t cap;
} PS_List;

typedef struct {
  PS_String *keys;
  PS_Value **values;
  uint8_t *used;
  size_t cap;
  size_t len;
} PS_Object;

typedef struct {
  PS_Value **keys;
  PS_Value **values;
  uint8_t *used;
  size_t cap;
  size_t len;
} PS_Map;

typedef struct {
  PS_Value *source;
  size_t offset;
  size_t len;
  int readonly;
} PS_View;

typedef struct {
  PS_Value *source;
  int mode; // 0=of, 1=in
  size_t index;
} PS_Iter;

typedef struct {
  FILE *fp;
  uint32_t flags;
  int closed;
  int at_start;
} PS_File;

struct PS_Value {
  PS_ValueTag tag;
  int64_t refcount;
  union {
    int bool_v;
    int64_t int_v;
    double float_v;
    uint8_t byte_v;
    uint32_t glyph_v;
    PS_String string_v;
    PS_Bytes bytes_v;
    PS_List list_v;
    PS_Object object_v;
    PS_Map map_v;
    PS_View view_v;
    PS_Iter iter_v;
    PS_File file_v;
  } as;
};

#endif // PS_VALUE_IMPL_H
