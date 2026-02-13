#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime/ps_list.h"
#include "runtime/ps_map.h"
#include "runtime/ps_object.h"
#include "runtime/ps_runtime.h"
#include "runtime/ps_vm.h"
#include "runtime/ps_vm_internal.h"
#include "modules/debug.h"

static PS_Value *make_string(PS_Context *ctx, const char *s) {
  return ps_make_string_utf8(ctx, s, strlen(s));
}

static PS_Value *make_repeated_string(PS_Context *ctx, char ch, size_t count) {
  char *buf = (char *)calloc(count + 1, 1);
  if (!buf) return NULL;
  memset(buf, (unsigned char)ch, count);
  buf[count] = '\0';
  PS_Value *v = ps_make_string_utf8(ctx, buf, count);
  free(buf);
  return v;
}

static int debug_dump_value(PS_Context *ctx, const PS_Module *mod, PS_Value *v) {
  if (!ctx || !mod || mod->fn_count == 0) return 0;
  PS_Value *out = NULL;
  PS_Value *args[1] = { v };
  PS_Status st = mod->fns[0].fn(ctx, 1, args, &out);
  if (out) ps_value_release(out);
  return st == PS_OK;
}

int main(void) {
  PS_Context *ctx = ps_ctx_create();
  if (!ctx) return 1;

  const char *ir_json =
    "{"
    "\"ir_version\":\"1.0.0\","
    "\"format\":\"ProtoScriptIR\","
    "\"module\":{"
      "\"kind\":\"Module\","
      "\"functions\":[],"
      "\"prototypes\":["
        "{"
          "\"name\":\"ProtoA\","
          "\"parent\":null,"
          "\"sealed\":true,"
          "\"fields\":["
            "{\"name\":\"a\",\"type\":{\"kind\":\"IRType\",\"name\":\"int\"},\"const\":true},"
            "{\"name\":\"name\",\"type\":{\"kind\":\"IRType\",\"name\":\"string\"}},"
            "{\"name\":\"self\",\"type\":{\"kind\":\"IRType\",\"name\":\"ProtoA\"}}"
          "],"
          "\"methods\":["
            "{"
              "\"name\":\"foo\","
              "\"params\":[{\"name\":\"x\",\"type\":{\"kind\":\"IRType\",\"name\":\"int\"}}],"
              "\"returnType\":{\"kind\":\"IRType\",\"name\":\"int\"}"
            "},"
            "{"
              "\"name\":\"bar\","
              "\"params\":[{\"name\":\"vals\",\"type\":{\"kind\":\"IRType\",\"name\":\"int\"},\"variadic\":true}],"
              "\"returnType\":{\"kind\":\"IRType\",\"name\":\"void\"}"
            "}"
          "]"
        "},"
        "{"
          "\"name\":\"ProtoB\","
          "\"parent\":\"ProtoA\","
          "\"fields\":["
            "{\"name\":\"b\",\"type\":{\"kind\":\"IRType\",\"name\":\"bool\"}}"
          "],"
          "\"methods\":["
            "{"
              "\"name\":\"foo\","
              "\"params\":[{\"name\":\"x\",\"type\":{\"kind\":\"IRType\",\"name\":\"int\"}}],"
              "\"returnType\":{\"kind\":\"IRType\",\"name\":\"int\"}"
            "},"
            "{"
              "\"name\":\"baz\","
              "\"params\":["
                "{\"name\":\"s\",\"type\":{\"kind\":\"IRType\",\"name\":\"string\"}},"
                "{\"name\":\"g\",\"type\":{\"kind\":\"IRType\",\"name\":\"glyph\"}}"
              "],"
              "\"returnType\":{\"kind\":\"IRType\",\"name\":\"string\"}"
            "}"
          "]"
        "}"
      "],"
      "\"groups\":["
        "{"
          "\"name\":\"Color\","
          "\"baseType\":{\"kind\":\"IRType\",\"name\":\"int\"},"
          "\"members\":["
            "{\"name\":\"Black\",\"literalType\":\"int\",\"value\":\"0\"},"
            "{\"name\":\"Cyan\",\"literalType\":\"int\",\"value\":\"65535\"}"
          "]"
        "}"
      "]"
    "}"
    "}";

  PS_IR_Module *ir = ps_ir_load_json(ctx, ir_json, strlen(ir_json));
  if (!ir) return 1;
  ctx->current_module = ir;

  PS_Module mod;
  memset(&mod, 0, sizeof(mod));
  if (ps_module_init_Debug(ctx, &mod) != PS_OK) return 1;

  fprintf(stderr, "-- scalars --\n");
  PS_Value *v_true = ps_make_bool(ctx, 1);
  debug_dump_value(ctx, &mod, v_true);
  PS_Value *v_byte = ps_make_byte(ctx, 255);
  debug_dump_value(ctx, &mod, v_byte);
  PS_Value *v_int = ps_make_int(ctx, 1234);
  debug_dump_value(ctx, &mod, v_int);
  PS_Value *v_float = ps_make_float(ctx, 3.5);
  debug_dump_value(ctx, &mod, v_float);
  PS_Value *v_glyph = ps_make_glyph(ctx, 0x41);
  debug_dump_value(ctx, &mod, v_glyph);
  PS_Value *v_str = make_string(ctx, "Hello");
  debug_dump_value(ctx, &mod, v_str);
  PS_Value *v_long = make_repeated_string(ctx, 'a', 205);
  debug_dump_value(ctx, &mod, v_long);

  fprintf(stderr, "-- collections --\n");
  PS_Value *list_small = ps_list_new(ctx);
  ps_list_set_type_name_internal(ctx, list_small, "list<int>");
  PS_Value *one = ps_make_int(ctx, 1);
  PS_Value *two = ps_make_int(ctx, 2);
  PS_Value *three = ps_make_int(ctx, 3);
  ps_list_push_internal(ctx, list_small, one);
  ps_list_push_internal(ctx, list_small, two);
  ps_list_push_internal(ctx, list_small, three);
  debug_dump_value(ctx, &mod, list_small);

  PS_Value *list_byte = ps_list_new(ctx);
  ps_list_set_type_name_internal(ctx, list_byte, "list<byte>");
  PS_Value *b0 = ps_make_byte(ctx, 0);
  PS_Value *b1 = ps_make_byte(ctx, 255);
  ps_list_push_internal(ctx, list_byte, b0);
  ps_list_push_internal(ctx, list_byte, b1);
  debug_dump_value(ctx, &mod, list_byte);

  PS_Value *map_small = ps_map_new(ctx);
  ps_map_set_type_name_internal(ctx, map_small, "map<string,int>");
  PS_Value *ka = make_string(ctx, "a");
  PS_Value *kb = make_string(ctx, "b");
  ps_map_set(ctx, map_small, ka, one);
  ps_map_set(ctx, map_small, kb, two);
  debug_dump_value(ctx, &mod, map_small);

  PS_Value *list_view_src = ps_list_new(ctx);
  ps_list_set_type_name_internal(ctx, list_view_src, "list<int>");
  PS_Value *v10 = ps_make_int(ctx, 10);
  PS_Value *v11 = ps_make_int(ctx, 11);
  PS_Value *v12 = ps_make_int(ctx, 12);
  PS_Value *v13 = ps_make_int(ctx, 13);
  ps_list_push_internal(ctx, list_view_src, v10);
  ps_list_push_internal(ctx, list_view_src, v11);
  ps_list_push_internal(ctx, list_view_src, v12);
  ps_list_push_internal(ctx, list_view_src, v13);
  PS_Value *view = ps_value_alloc(PS_V_VIEW);
  view->as.view_v.source = ps_value_retain(list_view_src);
  view->as.view_v.offset = 1;
  view->as.view_v.len = 2;
  view->as.view_v.readonly = 1;
  view->as.view_v.version = list_view_src->as.list_v.version;
  view->as.view_v.type_name = strdup("view<int>");
  debug_dump_value(ctx, &mod, view);

  PS_Value *list_large = ps_list_new(ctx);
  ps_list_set_type_name_internal(ctx, list_large, "list<int>");
  for (int i = 0; i < 105; i++) {
    PS_Value *iv = ps_make_int(ctx, i);
    ps_list_push_internal(ctx, list_large, iv);
    ps_value_release(iv);
  }
  debug_dump_value(ctx, &mod, list_large);

  PS_Value *list0 = ps_list_new(ctx);
  PS_Value *list1 = ps_list_new(ctx);
  PS_Value *list2 = ps_list_new(ctx);
  PS_Value *list3 = ps_list_new(ctx);
  PS_Value *list4 = ps_list_new(ctx);
  PS_Value *list5 = ps_list_new(ctx);
  PS_Value *list6 = ps_list_new(ctx);
  ps_list_set_type_name_internal(ctx, list0, "list<int>");
  ps_list_set_type_name_internal(ctx, list1, "list<int>");
  ps_list_set_type_name_internal(ctx, list2, "list<int>");
  ps_list_set_type_name_internal(ctx, list3, "list<int>");
  ps_list_set_type_name_internal(ctx, list4, "list<int>");
  ps_list_set_type_name_internal(ctx, list5, "list<int>");
  ps_list_set_type_name_internal(ctx, list6, "list<int>");
  PS_Value *deep = ps_make_int(ctx, 42);
  ps_list_push_internal(ctx, list6, deep);
  ps_list_push_internal(ctx, list5, list6);
  ps_list_push_internal(ctx, list4, list5);
  ps_list_push_internal(ctx, list3, list4);
  ps_list_push_internal(ctx, list2, list3);
  ps_list_push_internal(ctx, list1, list2);
  ps_list_push_internal(ctx, list0, list1);
  debug_dump_value(ctx, &mod, list0);

  fprintf(stderr, "-- cycles --\n");
  PS_Value *list_cyc = ps_list_new(ctx);
  ps_list_set_type_name_internal(ctx, list_cyc, "list<unknown>");
  ps_list_push_internal(ctx, list_cyc, list_cyc);
  debug_dump_value(ctx, &mod, list_cyc);

  PS_Value *map_cyc = ps_map_new(ctx);
  ps_map_set_type_name_internal(ctx, map_cyc, "map<string,map>");
  PS_Value *kself = make_string(ctx, "self");
  ps_map_set(ctx, map_cyc, kself, map_cyc);
  debug_dump_value(ctx, &mod, map_cyc);

  fprintf(stderr, "-- groups --\n");
  PS_Value *group_val = ps_make_int(ctx, 65535);
  debug_dump_value(ctx, &mod, group_val);
  PS_Value *group_type = ps_value_alloc(PS_V_GROUP);
  group_type->as.group_v.group = ps_ir_find_group(ir, "Color");
  debug_dump_value(ctx, &mod, group_type);

  fprintf(stderr, "-- object --\n");
  PS_Value *obj = ps_object_new(ctx);
  ps_object_set_proto_name_internal(ctx, obj, "ProtoB");
  ps_object_set_str_internal(ctx, obj, "a", 1, v_int);
  ps_object_set_str_internal(ctx, obj, "b", 1, v_true);
  debug_dump_value(ctx, &mod, obj);

  ps_ir_free(ir);
  ps_ctx_destroy(ctx);
  return 0;
}
