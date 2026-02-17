#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ps/ps_api.h"
#include "modules/debug.h"
#include "runtime/ps_runtime.h"
#include "runtime/ps_modules.h"

PS_Status ps_module_init_Fs(PS_Context *ctx, PS_Module *out);

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

  PS_Module debug_mod;
  memset(&debug_mod, 0, sizeof(debug_mod));
  if (ps_module_init_Debug(ctx, &debug_mod) != PS_OK) return 1;

  PS_Module fs_mod;
  memset(&fs_mod, 0, sizeof(fs_mod));
  if (ps_module_init_Fs(ctx, &fs_mod) != PS_OK) return 1;
  if (!register_module(ctx, &fs_mod)) return 1;

  fprintf(stderr, "-- builtin handles c --\n");

  FILE *tfp = tmpfile();
  if (!tfp) return 1;
  PS_Value *text = ps_make_file(ctx, tfp, PS_FILE_WRITE, "/tmp/ps_debug_c_text.txt");
  if (!text) return 1;
  if (!debug_dump_value(ctx, &debug_mod, text)) return 1;

  FILE *bfp = tmpfile();
  if (!bfp) return 1;
  PS_Value *binary = ps_make_file(ctx, bfp, PS_FILE_WRITE | PS_FILE_BINARY, "/tmp/ps_debug_c_binary.bin");
  if (!binary) return 1;
  if (!debug_dump_value(ctx, &debug_mod, binary)) return 1;

  const PS_NativeFnDesc *open_dir = find_fn(&fs_mod, "openDir");
  const PS_NativeFnDesc *walk = find_fn(&fs_mod, "walk");
  if (!open_dir || !walk) return 1;

  PS_Value *dot = ps_make_string_utf8(ctx, ".", 1);
  if (!dot) return 1;

  PS_Value *dir = NULL;
  {
    PS_Value *args[1] = { dot };
    if (open_dir->fn(ctx, 1, args, &dir) != PS_OK || !dir) return 1;
  }
  if (!debug_dump_value(ctx, &debug_mod, dir)) return 1;

  PS_Value *walker = NULL;
  {
    PS_Value *md = ps_make_int(ctx, -1);
    PS_Value *follow = ps_make_bool(ctx, 0);
    if (!md || !follow) return 1;
    PS_Value *args[3] = { dot, md, follow };
    if (walk->fn(ctx, 3, args, &walker) != PS_OK || !walker) return 1;
    ps_value_release(md);
    ps_value_release(follow);
  }
  if (!debug_dump_value(ctx, &debug_mod, walker)) return 1;

  ps_value_release(walker);
  ps_value_release(dir);
  ps_value_release(dot);
  ps_value_release(binary);
  ps_value_release(text);

  ps_ctx_destroy(ctx);
  return 0;
}
