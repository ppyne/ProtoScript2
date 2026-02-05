#ifndef PS_JSON_H
#define PS_JSON_H

#include <stddef.h>

typedef enum {
  PS_JSON_NULL,
  PS_JSON_BOOL,
  PS_JSON_NUMBER,
  PS_JSON_STRING,
  PS_JSON_ARRAY,
  PS_JSON_OBJECT
} PS_JsonType;

typedef struct PS_JsonValue PS_JsonValue;

struct PS_JsonValue {
  PS_JsonType type;
  union {
    int bool_v;
    double num_v;
    char *str_v;
    struct {
      PS_JsonValue **items;
      size_t len;
    } array_v;
    struct {
      char **keys;
      PS_JsonValue **values;
      size_t len;
    } object_v;
  } as;
};

PS_JsonValue *ps_json_parse(const char *src, size_t len, const char **err);
void ps_json_free(PS_JsonValue *v);
PS_JsonValue *ps_json_obj_get(PS_JsonValue *obj, const char *key);

#endif // PS_JSON_H
