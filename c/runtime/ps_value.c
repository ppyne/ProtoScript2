#include <stdlib.h>
#include <string.h>

#include "ps_runtime.h"

static void ps_list_free(PS_List *l);
static void ps_object_free(PS_Object *o);
static void ps_map_free(PS_Map *m);

PS_Value *ps_value_alloc(PS_ValueTag tag) {
  PS_Value *v = (PS_Value *)calloc(1, sizeof(PS_Value));
  if (!v) return NULL;
  v->tag = tag;
  v->refcount = 1;
  return v;
}

PS_Value *ps_value_retain(PS_Value *v) {
  if (!v) return NULL;
  v->refcount += 1;
  return v;
}

void ps_value_release(PS_Value *v) {
  if (!v) return;
  v->refcount -= 1;
  if (v->refcount > 0) return;
  ps_value_free(v);
}

void ps_value_free(PS_Value *v) {
  if (!v) return;
  switch (v->tag) {
    case PS_V_STRING:
      free(v->as.string_v.ptr);
      break;
    case PS_V_BYTES:
      free(v->as.bytes_v.ptr);
      break;
    case PS_V_LIST:
      ps_list_free(&v->as.list_v);
      break;
    case PS_V_OBJECT:
      ps_object_free(&v->as.object_v);
      break;
    case PS_V_MAP:
      ps_map_free(&v->as.map_v);
      break;
    case PS_V_VIEW:
      if (v->as.view_v.source) ps_value_release(v->as.view_v.source);
      break;
    case PS_V_ITER:
      if (v->as.iter_v.source) ps_value_release(v->as.iter_v.source);
      break;
    default:
      break;
  }
  free(v);
}

static void ps_list_free(PS_List *l) {
  if (!l || !l->items) return;
  for (size_t i = 0; i < l->len; i++) {
    if (l->items[i]) ps_value_release(l->items[i]);
  }
  free(l->items);
  l->items = NULL;
  l->len = 0;
  l->cap = 0;
}

static void ps_object_free(PS_Object *o) {
  if (!o) return;
  if (o->keys) {
    for (size_t i = 0; i < o->cap; i++) {
      if (o->used && o->used[i]) {
        free(o->keys[i].ptr);
        if (o->values && o->values[i]) ps_value_release(o->values[i]);
      }
    }
  }
  free(o->keys);
  free(o->values);
  free(o->used);
  o->keys = NULL;
  o->values = NULL;
  o->used = NULL;
  o->cap = 0;
  o->len = 0;
}

static void ps_map_free(PS_Map *m) {
  if (!m) return;
  if (m->keys) {
    for (size_t i = 0; i < m->cap; i++) {
      if (m->used && m->used[i]) {
        if (m->keys[i]) ps_value_release(m->keys[i]);
        if (m->values && m->values[i]) ps_value_release(m->values[i]);
      }
    }
  }
  free(m->keys);
  free(m->values);
  free(m->used);
  m->keys = NULL;
  m->values = NULL;
  m->used = NULL;
  m->cap = 0;
  m->len = 0;
}
