#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "runtime/ps_list.h"
#include "runtime/ps_map.h"
#include "runtime/ps_object.h"
#include "runtime/ps_runtime.h"
#include "runtime/ps_modules.h"
#include "runtime/ps_vm.h"
#include "runtime/ps_vm_internal.h"
#include "modules/debug.h"

PS_Status ps_module_init_JSON(PS_Context *ctx, PS_Module *out);

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

static int register_module(PS_Context *ctx, const PS_Module *mod) {
  if (!ctx || !mod) return 0;
  if (ctx->module_count == ctx->module_cap) {
    size_t new_cap = ctx->module_cap == 0 ? 4 : ctx->module_cap * 2;
    PS_ModuleRecord *n = (PS_ModuleRecord *)realloc(ctx->modules, sizeof(PS_ModuleRecord) * new_cap);
    if (!n) return 0;
    ctx->modules = n;
    ctx->module_cap = new_cap;
  }
  ctx->modules[ctx->module_count].desc = *mod;
  ctx->modules[ctx->module_count].lib = NULL;
  ctx->module_count += 1;
  return 1;
}

static const PS_NativeFnDesc *find_fn(const PS_Module *mod, const char *name) {
  if (!mod || !name) return NULL;
  for (size_t i = 0; i < mod->fn_count; i++) {
    if (mod->fns[i].name && strcmp(mod->fns[i].name, name) == 0) return &mod->fns[i];
  }
  return NULL;
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
        "},"
        "{"
          "\"name\":\"ProtoC\","
          "\"parent\":\"ProtoB\","
          "\"fields\":["
            "{\"name\":\"c\",\"type\":{\"kind\":\"IRType\",\"name\":\"float\"}}"
          "],"
          "\"methods\":["
            "{"
              "\"name\":\"baz\","
              "\"params\":["
                "{\"name\":\"s\",\"type\":{\"kind\":\"IRType\",\"name\":\"string\"}},"
                "{\"name\":\"g\",\"type\":{\"kind\":\"IRType\",\"name\":\"glyph\"}}"
              "],"
              "\"returnType\":{\"kind\":\"IRType\",\"name\":\"string\"}"
            "}"
          "]"
        "},"
        "{"
          "\"name\":\"Simple\","
          "\"parent\":null,"
          "\"fields\":["
            "{\"name\":\"x\",\"type\":{\"kind\":\"IRType\",\"name\":\"int\"}},"
            "{\"name\":\"y\",\"type\":{\"kind\":\"IRType\",\"name\":\"string\"}}"
          "],"
          "\"methods\":["
            "{"
              "\"name\":\"ping\","
              "\"params\":[],"
              "\"returnType\":{\"kind\":\"IRType\",\"name\":\"int\"}"
            "}"
          "]"
        "},"
        "{"
          "\"name\":\"SealedChild\","
          "\"parent\":\"Simple\","
          "\"sealed\":true,"
          "\"fields\":["
            "{\"name\":\"z\",\"type\":{\"kind\":\"IRType\",\"name\":\"int\"}}"
          "],"
          "\"methods\":[]"
        "},"
        "{"
          "\"name\":\"P\","
          "\"parent\":null,"
          "\"sealed\":true,"
          "\"fields\":["
            "{\"name\":\"v\",\"type\":{\"kind\":\"IRType\",\"name\":\"int\"}}"
          "],"
          "\"methods\":["
            "{"
              "\"name\":\"init\","
              "\"params\":[],"
              "\"returnType\":{\"kind\":\"IRType\",\"name\":\"void\"}"
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

  PS_Module json_mod;
  memset(&json_mod, 0, sizeof(json_mod));
  if (ps_module_init_JSON(ctx, &json_mod) != PS_OK) return 1;
  if (!register_module(ctx, &json_mod)) return 1;

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

  fprintf(stderr, "-- typed list literal --\n");
  const char *ir_list =
    "{"
    "\"ir_version\":\"1.0.0\","
    "\"format\":\"ProtoScriptIR\","
    "\"module\":{"
      "\"kind\":\"Module\","
      "\"functions\":["
        "{"
          "\"kind\":\"Function\","
          "\"name\":\"main\","
          "\"params\":[],"
          "\"returnType\":{\"kind\":\"IRType\",\"name\":\"list<int>\"},"
          "\"blocks\":["
            "{"
              "\"kind\":\"Block\","
              "\"label\":\"entry\","
              "\"instrs\":["
                "{\"op\":\"const\",\"dst\":\"t0\",\"literalType\":\"int\",\"value\":\"0\"},"
                "{\"op\":\"const\",\"dst\":\"t1\",\"literalType\":\"int\",\"value\":\"1\"},"
                "{\"op\":\"const\",\"dst\":\"t2\",\"literalType\":\"int\",\"value\":\"2\"},"
                "{\"op\":\"const\",\"dst\":\"t3\",\"literalType\":\"int\",\"value\":\"3\"},"
                "{\"op\":\"make_list\",\"dst\":\"t4\",\"items\":[\"t0\",\"t1\",\"t2\",\"t3\"],\"type\":\"list<int>\"},"
                "{\"op\":\"ret\",\"value\":\"t4\",\"type\":{\"kind\":\"IRType\",\"name\":\"list<int>\"}}"
              "]"
            "}"
          "]"
        "}"
      "]"
    "}"
    "}";
  PS_IR_Module *ir_list_mod = ps_ir_load_json(ctx, ir_list, strlen(ir_list));
  if (ir_list_mod) {
    PS_IR_Module *saved = ctx->current_module;
    PS_Value *out_list = NULL;
    ctx->current_module = ir_list_mod;
    if (ps_vm_run_main(ctx, ir_list_mod, NULL, 0, &out_list) == 0 && out_list) {
      ctx->current_module = saved;
      debug_dump_value(ctx, &mod, out_list);
      ps_value_release(out_list);
    } else {
      ctx->current_module = saved;
    }
    ps_ir_free(ir_list_mod);
  }

  PS_Value *list_byte = ps_list_new(ctx);
  ps_list_set_type_name_internal(ctx, list_byte, "list<byte>");
  PS_Value *b0 = ps_make_byte(ctx, 0);
  PS_Value *b1 = ps_make_byte(ctx, 255);
  ps_list_push_internal(ctx, list_byte, b0);
  ps_list_push_internal(ctx, list_byte, b1);
  debug_dump_value(ctx, &mod, list_byte);

  PS_Value *list_str = ps_list_new(ctx);
  ps_list_set_type_name_internal(ctx, list_str, "list<string>");
  PS_Value *s1 = make_string(ctx, "a");
  PS_Value *s2 = make_string(ctx, "b");
  ps_list_push_internal(ctx, list_str, s1);
  ps_list_push_internal(ctx, list_str, s2);
  debug_dump_value(ctx, &mod, list_str);

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

  fprintf(stderr, "-- native json --\n");
  const PS_NativeFnDesc *fn_null = find_fn(&json_mod, "null");
  const PS_NativeFnDesc *fn_bool = find_fn(&json_mod, "bool");
  const PS_NativeFnDesc *fn_string = find_fn(&json_mod, "string");
  const PS_NativeFnDesc *fn_array = find_fn(&json_mod, "array");
  PS_Value *jnull = NULL;
  if (fn_null && fn_null->fn(ctx, 0, NULL, &jnull) == PS_OK) {
    debug_dump_value(ctx, &mod, jnull);
    ps_value_release(jnull);
  }
  PS_Value *jbool = NULL;
  PS_Value *bool_arg = ps_make_bool(ctx, 1);
  if (fn_bool && bool_arg) {
    PS_Value *args[1] = { bool_arg };
    if (fn_bool->fn(ctx, 1, args, &jbool) == PS_OK) {
      debug_dump_value(ctx, &mod, jbool);
      ps_value_release(jbool);
    }
  }
  if (bool_arg) ps_value_release(bool_arg);
  const PS_NativeFnDesc *fn_number = find_fn(&json_mod, "number");
  PS_Value *jnum = NULL;
  if (fn_number) {
    PS_Value *narg = ps_make_float(ctx, 2.25);
    if (narg) {
      PS_Value *args[1] = { narg };
      if (fn_number->fn(ctx, 1, args, &jnum) == PS_OK) {
        debug_dump_value(ctx, &mod, jnum);
        ps_value_release(jnum);
      }
      ps_value_release(narg);
    }
  }
  PS_Value *jstr = NULL;
  if (fn_string) {
    PS_Value *sarg = make_string(ctx, "Hello");
    if (sarg) {
      PS_Value *args[1] = { sarg };
      if (fn_string->fn(ctx, 1, args, &jstr) == PS_OK) {
        debug_dump_value(ctx, &mod, jstr);
        ps_value_release(jstr);
      }
      ps_value_release(sarg);
    }
  }
  PS_Value *list = ps_make_list(ctx);
  if (list && fn_null && fn_bool) {
    ps_list_set_type_name_internal(ctx, list, "list<JSONValue>");
    PS_Value *jn1 = NULL;
    if (fn_null->fn(ctx, 0, NULL, &jn1) == PS_OK && jn1) {
      ps_list_push(ctx, list, jn1);
      ps_value_release(jn1);
    }
    PS_Value *jb1 = NULL;
    PS_Value *barg = ps_make_bool(ctx, 0);
    if (barg) {
      PS_Value *args[1] = { barg };
      if (fn_bool->fn(ctx, 1, args, &jb1) == PS_OK && jb1) {
        ps_list_push(ctx, list, jb1);
        ps_value_release(jb1);
      }
      ps_value_release(barg);
    }
    if (fn_array) {
      PS_Value *jarr = NULL;
      PS_Value *args[1] = { list };
      if (fn_array->fn(ctx, 1, args, &jarr) == PS_OK && jarr) {
        debug_dump_value(ctx, &mod, jarr);
        ps_value_release(jarr);
      }
    }
  }
  if (list) ps_value_release(list);

  PS_Value *list_json = ps_make_list(ctx);
  if (list_json && fn_null && fn_bool) {
    ps_list_set_type_name_internal(ctx, list_json, "list<JSONValue>");
    PS_Value *jn2 = NULL;
    if (fn_null->fn(ctx, 0, NULL, &jn2) == PS_OK && jn2) {
      ps_list_push(ctx, list_json, jn2);
      ps_value_release(jn2);
    }
    PS_Value *jb2 = NULL;
    PS_Value *barg2 = ps_make_bool(ctx, 0);
    if (barg2) {
      PS_Value *args[1] = { barg2 };
      if (fn_bool->fn(ctx, 1, args, &jb2) == PS_OK && jb2) {
        ps_list_push(ctx, list_json, jb2);
        ps_value_release(jb2);
      }
      ps_value_release(barg2);
    }
    debug_dump_value(ctx, &mod, list_json);
  }
  if (list_json) ps_value_release(list_json);

  const PS_NativeFnDesc *fn_object = find_fn(&json_mod, "object");
  PS_Value *obj_map = ps_make_map(ctx);
  if (obj_map && fn_object && fn_null) {
    ps_map_set_type_name_internal(ctx, obj_map, "map<string,JSONValue>");
    PS_Value *jn3 = NULL;
    if (fn_null->fn(ctx, 0, NULL, &jn3) == PS_OK && jn3) {
      PS_Value *kobj = make_string(ctx, "null");
      if (kobj) {
        ps_map_set(ctx, obj_map, kobj, jn3);
        ps_value_release(kobj);
      }
      ps_value_release(jn3);
    }
    PS_Value *jobj = NULL;
    PS_Value *args[1] = { obj_map };
    if (fn_object->fn(ctx, 1, args, &jobj) == PS_OK && jobj) {
      debug_dump_value(ctx, &mod, jobj);
      ps_value_release(jobj);
    }
  }
  if (obj_map) ps_value_release(obj_map);

  PS_Value *map_json = ps_map_new(ctx);
  if (map_json && fn_null) {
    ps_map_set_type_name_internal(ctx, map_json, "map<string,JSONValue>");
    PS_Value *jv = NULL;
    if (fn_null->fn(ctx, 0, NULL, &jv) == PS_OK && jv) {
      PS_Value *ka = make_string(ctx, "a");
      if (ka) {
        ps_map_set(ctx, map_json, ka, jv);
        ps_value_release(ka);
      }
      ps_value_release(jv);
    }
    debug_dump_value(ctx, &mod, map_json);
  }
  if (map_json) ps_value_release(map_json);

  fprintf(stderr, "-- object --\n");
  PS_Value *obj = ps_object_new(ctx);
  ps_object_set_proto_name_internal(ctx, obj, "ProtoB");
  ps_object_set_str_internal(ctx, obj, "a", 1, v_int);
  ps_object_set_str_internal(ctx, obj, "b", 1, v_true);
  debug_dump_value(ctx, &mod, obj);

  fprintf(stderr, "-- proto chain --\n");
  PS_Value *objc = ps_object_new(ctx);
  ps_object_set_proto_name_internal(ctx, objc, "ProtoC");
  ps_object_set_str_internal(ctx, objc, "a", 1, v_int);
  ps_object_set_str_internal(ctx, objc, "b", 1, v_true);
  PS_Value *vfc = ps_make_float(ctx, 1.25);
  ps_object_set_str_internal(ctx, objc, "c", 1, vfc);
  ps_value_release(vfc);
  debug_dump_value(ctx, &mod, objc);

  fprintf(stderr, "-- proto simple --\n");
  PS_Value *simple = ps_object_new(ctx);
  ps_object_set_proto_name_internal(ctx, simple, "Simple");
  PS_Value *vx = ps_make_int(ctx, 10);
  PS_Value *vy = make_string(ctx, "first");
  ps_object_set_str_internal(ctx, simple, "x", 1, vx);
  ps_object_set_str_internal(ctx, simple, "y", 1, vy);
  ps_value_release(vx);
  ps_value_release(vy);
  debug_dump_value(ctx, &mod, simple);
  PS_Value *vy2 = make_string(ctx, "second");
  ps_object_set_str_internal(ctx, simple, "y", 1, vy2);
  ps_value_release(vy2);
  debug_dump_value(ctx, &mod, simple);

  fprintf(stderr, "-- proto sealed --\n");
  PS_Value *sobj = ps_object_new(ctx);
  ps_object_set_proto_name_internal(ctx, sobj, "SealedChild");
  PS_Value *vz = ps_make_int(ctx, 7);
  ps_object_set_str_internal(ctx, sobj, "z", 1, vz);
  ps_value_release(vz);
  debug_dump_value(ctx, &mod, sobj);

  fprintf(stderr, "-- determinism --\n");
  debug_dump_value(ctx, &mod, simple);
  debug_dump_value(ctx, &mod, simple);

  fprintf(stderr, "-- repro --\n");
  PS_Value *pobj = ps_object_new(ctx);
  ps_object_set_proto_name_internal(ctx, pobj, "P");
  debug_dump_value(ctx, &mod, pobj);
  PS_Value *v42 = ps_make_int(ctx, 42);
  ps_object_set_str_internal(ctx, pobj, "v", 1, v42);
  ps_value_release(v42);
  debug_dump_value(ctx, &mod, pobj);

  ps_ir_free(ir);
  ps_ctx_destroy(ctx);
  return 0;
}
