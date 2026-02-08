#include <stddef.h>

#include "ps_dynlib.h"

struct PS_DynLib {
  int unused;
};

PS_DynLib *ps_dynlib_open(const char *path) {
  (void)path;
  return NULL;
}

void *ps_dynlib_symbol(PS_DynLib *lib, const char *name) {
  (void)lib;
  (void)name;
  return NULL;
}

void ps_dynlib_close(PS_DynLib *lib) {
  (void)lib;
}

const char *ps_dynlib_last_error(void) { return "dynamic modules not supported"; }
