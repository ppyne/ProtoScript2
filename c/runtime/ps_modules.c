#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ps_modules.h"
#include "ps_runtime.h"

#ifdef PS_WASM
PS_Status ps_module_init_Io(PS_Context *ctx, PS_Module *out);
PS_Status ps_module_init_JSON(PS_Context *ctx, PS_Module *out);
PS_Status ps_module_init_Math(PS_Context *ctx, PS_Module *out);
#endif

static int module_record_exists(PS_Context *ctx, const char *name) {
  for (size_t i = 0; i < ctx->module_count; i++) {
    if (ctx->modules[i].desc.module_name && strcmp(ctx->modules[i].desc.module_name, name) == 0) return 1;
  }
  return 0;
}

static int ensure_module_cap(PS_Context *ctx) {
  if (ctx->module_count < ctx->module_cap) return 1;
  size_t new_cap = ctx->module_cap == 0 ? 4 : ctx->module_cap * 2;
  PS_ModuleRecord *n = (PS_ModuleRecord *)realloc(ctx->modules, sizeof(PS_ModuleRecord) * new_cap);
  if (!n) return 0;
  ctx->modules = n;
  ctx->module_cap = new_cap;
  return 1;
}

static char *module_name_to_path(const char *name, const char *dir) {
  size_t len = strlen(name);
  char *tmp = (char *)calloc(len + 1, 1);
  if (!tmp) return NULL;
  for (size_t i = 0; i < len; i++) tmp[i] = (name[i] == '.') ? '_' : name[i];
  const char *ext = ".so";
#if defined(__APPLE__)
  ext = ".dylib";
#endif
  size_t out_len = strlen(dir) + 1 + strlen("psmod_") + len + strlen(ext) + 1;
  char *out = (char *)calloc(out_len, 1);
  if (!out) {
    free(tmp);
    return NULL;
  }
  snprintf(out, out_len, "%s/psmod_%s%s", dir, tmp, ext);
  free(tmp);
  return out;
}

static PS_Status try_load_from_dir(PS_Context *ctx, const char *module_name, const char *dir) {
  char *path = module_name_to_path(module_name, dir);
  if (!path) return PS_ERR;
  PS_DynLib *lib = ps_dynlib_open(path);
  free(path);
  if (!lib) return PS_ERR;
  PS_Status (*init_fn)(PS_Context *, PS_Module *) = (PS_Status(*)(PS_Context*, PS_Module*))ps_dynlib_symbol(lib, "ps_module_init");
  if (!init_fn) {
    ps_dynlib_close(lib);
    ps_throw(ctx, PS_ERR_IMPORT, "module missing ps_module_init");
    return PS_ERR;
  }
  PS_Module mod;
  memset(&mod, 0, sizeof(mod));
  PS_Status st = init_fn(ctx, &mod);
  if (st != PS_OK) {
    ps_dynlib_close(lib);
    if (ps_last_error_code(ctx) == PS_ERR_NONE) ps_throw(ctx, PS_ERR_IMPORT, "module init failed");
    return PS_ERR;
  }
  if (mod.api_version != PS_API_VERSION) {
    ps_dynlib_close(lib);
    ps_throw(ctx, PS_ERR_IMPORT, "module ABI version mismatch");
    return PS_ERR;
  }
  if (!ensure_module_cap(ctx)) {
    ps_dynlib_close(lib);
    ps_throw(ctx, PS_ERR_OOM, "out of memory");
    return PS_ERR;
  }
  ctx->modules[ctx->module_count].desc = mod;
  ctx->modules[ctx->module_count].lib = lib;
  ctx->module_count += 1;
  return PS_OK;
}

PS_Status ps_module_load(PS_Context *ctx, const char *module_name) {
  if (module_record_exists(ctx, module_name)) return PS_OK;
#ifdef PS_WASM
  PS_Status (*init_fn)(PS_Context *, PS_Module *) = NULL;
  if (strcmp(module_name, "Io") == 0) init_fn = ps_module_init_Io;
  if (strcmp(module_name, "JSON") == 0) init_fn = ps_module_init_JSON;
  if (strcmp(module_name, "Math") == 0) init_fn = ps_module_init_Math;
  if (init_fn) {
    PS_Module mod;
    memset(&mod, 0, sizeof(mod));
    PS_Status st = init_fn(ctx, &mod);
    if (st != PS_OK) {
      if (ps_last_error_code(ctx) == PS_ERR_NONE) ps_throw(ctx, PS_ERR_IMPORT, "module init failed");
      return PS_ERR;
    }
    if (mod.api_version != PS_API_VERSION) {
      ps_throw(ctx, PS_ERR_IMPORT, "module ABI version mismatch");
      return PS_ERR;
    }
    if (!ensure_module_cap(ctx)) {
      ps_throw(ctx, PS_ERR_OOM, "out of memory");
      return PS_ERR;
    }
    ctx->modules[ctx->module_count].desc = mod;
    ctx->modules[ctx->module_count].lib = NULL;
    ctx->module_count += 1;
    return PS_OK;
  }
  ps_throw(ctx, PS_ERR_IMPORT, "module not found");
  return PS_ERR;
#else
  const char *env = getenv("PS_MODULE_PATH");
  if (env && *env) {
    char *dup = strdup(env);
    if (!dup) {
      ps_throw(ctx, PS_ERR_OOM, "out of memory");
      return PS_ERR;
    }
    char *save = NULL;
    for (char *tok = strtok_r(dup, ":", &save); tok; tok = strtok_r(NULL, ":", &save)) {
      if (try_load_from_dir(ctx, module_name, tok) == PS_OK) {
        free(dup);
        return PS_OK;
      }
    }
    free(dup);
  }
  // Fallback: ./modules and ./lib
  if (try_load_from_dir(ctx, module_name, "./modules") == PS_OK) return PS_OK;
  if (try_load_from_dir(ctx, module_name, "./lib") == PS_OK) return PS_OK;
  ps_throw(ctx, PS_ERR_IMPORT, "module not found");
  return PS_ERR;
#endif
}

const PS_NativeFnDesc *ps_module_find_fn(PS_Context *ctx, const char *module_name, const char *fn_name) {
  if (!module_name || !fn_name) return NULL;
  if (ps_module_load(ctx, module_name) != PS_OK) return NULL;
  for (size_t i = 0; i < ctx->module_count; i++) {
    PS_Module *m = &ctx->modules[i].desc;
    if (!m->module_name || strcmp(m->module_name, module_name) != 0) continue;
    for (size_t j = 0; j < m->fn_count; j++) {
      if (strcmp(m->fns[j].name, fn_name) == 0) return &m->fns[j];
    }
  }
  ps_throw(ctx, PS_ERR_IMPORT, "symbol not found");
  return NULL;
}
