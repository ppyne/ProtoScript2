#include <dlfcn.h>
#include <stdlib.h>

#include "ps_dynlib.h"

struct PS_DynLib {
  void *handle;
};

PS_DynLib *ps_dynlib_open(const char *path) {
  void *h = dlopen(path, RTLD_LAZY);
  if (!h) return NULL;
  PS_DynLib *lib = (PS_DynLib *)calloc(1, sizeof(PS_DynLib));
  if (!lib) {
    dlclose(h);
    return NULL;
  }
  lib->handle = h;
  return lib;
}

void *ps_dynlib_symbol(PS_DynLib *lib, const char *name) {
  if (!lib) return NULL;
  return dlsym(lib->handle, name);
}

void ps_dynlib_close(PS_DynLib *lib) {
  if (!lib) return;
  if (lib->handle) dlclose(lib->handle);
  free(lib);
}

const char *ps_dynlib_last_error(void) { return dlerror(); }
