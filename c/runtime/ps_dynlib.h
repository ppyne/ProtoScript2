#ifndef PS_DYNLIB_H
#define PS_DYNLIB_H

typedef struct PS_DynLib PS_DynLib;

PS_DynLib *ps_dynlib_open(const char *path);
void *ps_dynlib_symbol(PS_DynLib *lib, const char *name);
void ps_dynlib_close(PS_DynLib *lib);
const char *ps_dynlib_last_error(void);

#endif // PS_DYNLIB_H
