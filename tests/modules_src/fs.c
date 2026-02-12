#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>

#include "ps/ps_api.h"

typedef struct {
  DIR *dir;
  char *path;
  char *next;
  int done;
  int closed;
} FsDirState;

typedef struct {
  DIR *dir;
  char *path;
  int depth;
} FsWalkFrame;

typedef struct {
  FsWalkFrame *frames;
  size_t len;
  size_t cap;
  int max_depth;
  int follow_symlinks;
  int closed;
  int has_next;
  char *next_path;
  char *next_name;
  int next_depth;
  int next_is_dir;
  int next_is_file;
  int next_is_symlink;
} FsWalkerState;

static PS_Status fs_throw(PS_Context *ctx, const char *type, const char *msg) {
  char buf[256];
  const char *t = type ? type : "IOException";
  const char *m = msg ? msg : "";
  snprintf(buf, sizeof(buf), "fs:%s:%s", t, m);
  ps_throw(ctx, PS_ERR_INTERNAL, buf);
  return PS_ERR;
}

static PS_Status fs_throw_stat_error(PS_Context *ctx, int err) {
  if (err == ENOENT) return fs_throw(ctx, "FileNotFoundException", "file not found");
  if (err == EACCES || err == EPERM) return fs_throw(ctx, "PermissionDeniedException", "permission denied");
  if (err == ENOTDIR || err == EINVAL || err == ENAMETOOLONG || err == ELOOP) return fs_throw(ctx, "InvalidPathException", "invalid path");
  return fs_throw(ctx, "IOException", "io failed");
}

static PS_Status fs_throw_open_dir_error(PS_Context *ctx, int err) {
  if (err == ENOENT) return fs_throw(ctx, "FileNotFoundException", "file not found");
  if (err == ENOTDIR) return fs_throw(ctx, "NotADirectoryException", "not a directory");
  if (err == EACCES || err == EPERM) return fs_throw(ctx, "PermissionDeniedException", "permission denied");
  if (err == EINVAL || err == ENAMETOOLONG || err == ELOOP) return fs_throw(ctx, "InvalidPathException", "invalid path");
  return fs_throw(ctx, "IOException", "io failed");
}

static PS_Status fs_throw_dir_op_error(PS_Context *ctx, int err) {
  if (err == ENOENT) return fs_throw(ctx, "FileNotFoundException", "file not found");
  if (err == ENOTDIR) return fs_throw(ctx, "NotADirectoryException", "not a directory");
  if (err == ENOTEMPTY || err == EEXIST) return fs_throw(ctx, "DirectoryNotEmptyException", "directory not empty");
  if (err == EACCES || err == EPERM) return fs_throw(ctx, "PermissionDeniedException", "permission denied");
  if (err == EINVAL || err == ENAMETOOLONG || err == ELOOP) return fs_throw(ctx, "InvalidPathException", "invalid path");
  return fs_throw(ctx, "IOException", "io failed");
}

static PS_Status fs_throw_file_op_error(PS_Context *ctx, int err) {
  if (err == ENOENT) return fs_throw(ctx, "FileNotFoundException", "file not found");
  if (err == EISDIR) return fs_throw(ctx, "NotAFileException", "not a file");
  if (err == ENOTDIR) return fs_throw(ctx, "NotADirectoryException", "not a directory");
  if (err == EACCES || err == EPERM) return fs_throw(ctx, "PermissionDeniedException", "permission denied");
  if (err == EINVAL || err == ENAMETOOLONG || err == ELOOP) return fs_throw(ctx, "InvalidPathException", "invalid path");
  return fs_throw(ctx, "IOException", "io failed");
}

static PS_Status fs_throw_common_error(PS_Context *ctx, int err) {
  if (err == ENOENT) return fs_throw(ctx, "FileNotFoundException", "file not found");
  if (err == EACCES || err == EPERM) return fs_throw(ctx, "PermissionDeniedException", "permission denied");
  if (err == ENOTDIR || err == EINVAL || err == ENAMETOOLONG || err == ELOOP) return fs_throw(ctx, "InvalidPathException", "invalid path");
  return fs_throw(ctx, "IOException", "io failed");
}

static char *fs_cstr(PS_Value *v) {
  if (!v) return NULL;
  const char *ptr = ps_string_ptr(v);
  size_t len = ps_string_len(v);
  char *out = (char *)malloc(len + 1);
  if (!out) return NULL;
  if (len > 0) memcpy(out, ptr, len);
  out[len] = '\0';
  return out;
}

static PS_Value *fs_make_string(PS_Context *ctx, const char *s, size_t len) {
  return ps_make_string_utf8(ctx, s ? s : "", s ? len : 0);
}

static PS_Status fs_set_obj_field(PS_Context *ctx, PS_Value *obj, const char *key, PS_Value *val) {
  if (!obj) return PS_ERR;
  if (!val) return PS_ERR;
  if (ps_object_set_str(ctx, obj, key, strlen(key), val) != PS_OK) {
    ps_value_release(val);
    return PS_ERR;
  }
  ps_value_release(val);
  return PS_OK;
}

static FsDirState *fs_dir_state(PS_Context *ctx, PS_Value *dir_obj) {
  if (!dir_obj || ps_typeof(dir_obj) != PS_T_OBJECT) return NULL;
  PS_Value *ptr = ps_object_get_str(ctx, dir_obj, "__fs_dir_ptr", strlen("__fs_dir_ptr"));
  if (!ptr || ps_typeof(ptr) != PS_T_INT) return NULL;
  return (FsDirState *)(intptr_t)ps_as_int(ptr);
}

static FsWalkerState *fs_walker_state(PS_Context *ctx, PS_Value *walker_obj) {
  if (!walker_obj || ps_typeof(walker_obj) != PS_T_OBJECT) return NULL;
  PS_Value *ptr = ps_object_get_str(ctx, walker_obj, "__fs_walker_ptr", strlen("__fs_walker_ptr"));
  if (!ptr || ps_typeof(ptr) != PS_T_INT) return NULL;
  return (FsWalkerState *)(intptr_t)ps_as_int(ptr);
}

static char *fs_join(const char *base, const char *name) {
  if (!base || !name) return NULL;
  size_t base_len = strlen(base);
  size_t name_len = strlen(name);
  int need_sep = 1;
  if (base_len > 0 && base[base_len - 1] == '/') need_sep = 0;
  size_t out_len = base_len + (need_sep ? 1 : 0) + name_len;
  char *out = (char *)malloc(out_len + 1);
  if (!out) return NULL;
  memcpy(out, base, base_len);
  size_t off = base_len;
  if (need_sep) out[off++] = '/';
  memcpy(out + off, name, name_len);
  out[out_len] = '\0';
  return out;
}

static PS_Status fs_exists(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!out || !argv) return PS_ERR;
  if (ps_typeof(argv[0]) != PS_T_STRING) return fs_throw(ctx, "InvalidPathException", "invalid path");
  char *path = fs_cstr(argv[0]);
  if (!path || path[0] == '\0') {
    free(path);
    return fs_throw(ctx, "InvalidPathException", "invalid path");
  }
  struct stat st;
  int ok = lstat(path, &st) == 0;
  int err = errno;
  free(path);
  if (ok) {
    *out = ps_make_bool(ctx, 1);
    return *out ? PS_OK : PS_ERR;
  }
  if (err == ENOENT) {
    *out = ps_make_bool(ctx, 0);
    return *out ? PS_OK : PS_ERR;
  }
  if (err == ENOTDIR || err == EINVAL || err == ENAMETOOLONG || err == ELOOP) return fs_throw(ctx, "InvalidPathException", "invalid path");
  return fs_throw(ctx, "IOException", "io failed");
}

static PS_Status fs_is_file(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!out || !argv) return PS_ERR;
  if (ps_typeof(argv[0]) != PS_T_STRING) return fs_throw(ctx, "InvalidPathException", "invalid path");
  char *path = fs_cstr(argv[0]);
  if (!path || path[0] == '\0') {
    free(path);
    return fs_throw(ctx, "InvalidPathException", "invalid path");
  }
  struct stat st;
  int ok = lstat(path, &st) == 0;
  int err = errno;
  free(path);
  if (ok) {
    *out = ps_make_bool(ctx, S_ISREG(st.st_mode));
    return *out ? PS_OK : PS_ERR;
  }
  if (err == ENOENT) {
    *out = ps_make_bool(ctx, 0);
    return *out ? PS_OK : PS_ERR;
  }
  if (err == ENOTDIR || err == EINVAL || err == ENAMETOOLONG || err == ELOOP) return fs_throw(ctx, "InvalidPathException", "invalid path");
  return fs_throw(ctx, "IOException", "io failed");
}

static PS_Status fs_is_dir(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!out || !argv) return PS_ERR;
  if (ps_typeof(argv[0]) != PS_T_STRING) return fs_throw(ctx, "InvalidPathException", "invalid path");
  char *path = fs_cstr(argv[0]);
  if (!path || path[0] == '\0') {
    free(path);
    return fs_throw(ctx, "InvalidPathException", "invalid path");
  }
  struct stat st;
  int ok = lstat(path, &st) == 0;
  int err = errno;
  free(path);
  if (ok) {
    *out = ps_make_bool(ctx, S_ISDIR(st.st_mode));
    return *out ? PS_OK : PS_ERR;
  }
  if (err == ENOENT) {
    *out = ps_make_bool(ctx, 0);
    return *out ? PS_OK : PS_ERR;
  }
  if (err == ENOTDIR || err == EINVAL || err == ENAMETOOLONG || err == ELOOP) return fs_throw(ctx, "InvalidPathException", "invalid path");
  return fs_throw(ctx, "IOException", "io failed");
}

static PS_Status fs_is_symlink(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!out || !argv) return PS_ERR;
  if (ps_typeof(argv[0]) != PS_T_STRING) return fs_throw(ctx, "InvalidPathException", "invalid path");
  char *path = fs_cstr(argv[0]);
  if (!path || path[0] == '\0') {
    free(path);
    return fs_throw(ctx, "InvalidPathException", "invalid path");
  }
  struct stat st;
  int ok = lstat(path, &st) == 0;
  int err = errno;
  free(path);
  if (ok) {
    *out = ps_make_bool(ctx, S_ISLNK(st.st_mode));
    return *out ? PS_OK : PS_ERR;
  }
  if (err == ENOENT) {
    *out = ps_make_bool(ctx, 0);
    return *out ? PS_OK : PS_ERR;
  }
  if (err == ENOTDIR || err == EINVAL || err == ENAMETOOLONG || err == ELOOP) return fs_throw(ctx, "InvalidPathException", "invalid path");
  return fs_throw(ctx, "IOException", "io failed");
}

static PS_Status fs_is_readable(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!out || !argv) return PS_ERR;
  if (ps_typeof(argv[0]) != PS_T_STRING) return fs_throw(ctx, "InvalidPathException", "invalid path");
  char *path = fs_cstr(argv[0]);
  if (!path || path[0] == '\0') {
    free(path);
    return fs_throw(ctx, "InvalidPathException", "invalid path");
  }
  int ok = access(path, R_OK) == 0;
  int err = errno;
  free(path);
  if (ok) {
    *out = ps_make_bool(ctx, 1);
    return *out ? PS_OK : PS_ERR;
  }
  if (err == ENOENT || err == EACCES || err == EPERM) {
    *out = ps_make_bool(ctx, 0);
    return *out ? PS_OK : PS_ERR;
  }
  if (err == ENOTDIR || err == EINVAL || err == ENAMETOOLONG || err == ELOOP) return fs_throw(ctx, "InvalidPathException", "invalid path");
  return fs_throw(ctx, "IOException", "io failed");
}

static PS_Status fs_is_writable(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!out || !argv) return PS_ERR;
  if (ps_typeof(argv[0]) != PS_T_STRING) return fs_throw(ctx, "InvalidPathException", "invalid path");
  char *path = fs_cstr(argv[0]);
  if (!path || path[0] == '\0') {
    free(path);
    return fs_throw(ctx, "InvalidPathException", "invalid path");
  }
  int ok = access(path, W_OK) == 0;
  int err = errno;
  free(path);
  if (ok) {
    *out = ps_make_bool(ctx, 1);
    return *out ? PS_OK : PS_ERR;
  }
  if (err == ENOENT || err == EACCES || err == EPERM) {
    *out = ps_make_bool(ctx, 0);
    return *out ? PS_OK : PS_ERR;
  }
  if (err == ENOTDIR || err == EINVAL || err == ENAMETOOLONG || err == ELOOP) return fs_throw(ctx, "InvalidPathException", "invalid path");
  return fs_throw(ctx, "IOException", "io failed");
}

static PS_Status fs_is_executable(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!out || !argv) return PS_ERR;
  if (ps_typeof(argv[0]) != PS_T_STRING) return fs_throw(ctx, "InvalidPathException", "invalid path");
  char *path = fs_cstr(argv[0]);
  if (!path || path[0] == '\0') {
    free(path);
    return fs_throw(ctx, "InvalidPathException", "invalid path");
  }
  int ok = access(path, X_OK) == 0;
  int err = errno;
  free(path);
  if (ok) {
    *out = ps_make_bool(ctx, 1);
    return *out ? PS_OK : PS_ERR;
  }
  if (err == ENOENT || err == EACCES || err == EPERM) {
    *out = ps_make_bool(ctx, 0);
    return *out ? PS_OK : PS_ERR;
  }
  if (err == ENOTDIR || err == EINVAL || err == ENAMETOOLONG || err == ELOOP) return fs_throw(ctx, "InvalidPathException", "invalid path");
  return fs_throw(ctx, "IOException", "io failed");
}

static PS_Status fs_size(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!out || !argv) return PS_ERR;
  if (ps_typeof(argv[0]) != PS_T_STRING) return fs_throw(ctx, "InvalidPathException", "invalid path");
  char *path = fs_cstr(argv[0]);
  if (!path || path[0] == '\0') {
    free(path);
    return fs_throw(ctx, "InvalidPathException", "invalid path");
  }
  struct stat st;
  if (stat(path, &st) != 0) {
    int err = errno;
    free(path);
    return fs_throw_stat_error(ctx, err);
  }
  free(path);
  if (!S_ISREG(st.st_mode)) return fs_throw(ctx, "NotAFileException", "not a file");
  PS_Value *v = ps_make_int(ctx, (int64_t)st.st_size);
  if (!v) return PS_ERR;
  *out = v;
  return PS_OK;
}

static PS_Status fs_mkdir(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc; (void)out;
  if (!argv) return PS_ERR;
  if (ps_typeof(argv[0]) != PS_T_STRING) return fs_throw(ctx, "InvalidPathException", "invalid path");
  char *path = fs_cstr(argv[0]);
  if (!path || path[0] == '\0') {
    free(path);
    return fs_throw(ctx, "InvalidPathException", "invalid path");
  }
  if (mkdir(path, 0777) != 0) {
    int err = errno;
    free(path);
    if (err == EEXIST) return fs_throw(ctx, "IOException", "io failed");
    if (err == ENOTDIR) return fs_throw(ctx, "NotADirectoryException", "not a directory");
    if (err == ENOENT) return fs_throw(ctx, "FileNotFoundException", "file not found");
    if (err == EACCES || err == EPERM) return fs_throw(ctx, "PermissionDeniedException", "permission denied");
    if (err == EINVAL || err == ENAMETOOLONG || err == ELOOP) return fs_throw(ctx, "InvalidPathException", "invalid path");
    return fs_throw(ctx, "IOException", "io failed");
  }
  free(path);
  return PS_OK;
}

static PS_Status fs_rmdir(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc; (void)out;
  if (!argv) return PS_ERR;
  if (ps_typeof(argv[0]) != PS_T_STRING) return fs_throw(ctx, "InvalidPathException", "invalid path");
  char *path = fs_cstr(argv[0]);
  if (!path || path[0] == '\0') {
    free(path);
    return fs_throw(ctx, "InvalidPathException", "invalid path");
  }
  if (rmdir(path) != 0) {
    int err = errno;
    free(path);
    return fs_throw_dir_op_error(ctx, err);
  }
  free(path);
  return PS_OK;
}

static PS_Status fs_rm(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc; (void)out;
  if (!argv) return PS_ERR;
  if (ps_typeof(argv[0]) != PS_T_STRING) return fs_throw(ctx, "InvalidPathException", "invalid path");
  char *path = fs_cstr(argv[0]);
  if (!path || path[0] == '\0') {
    free(path);
    return fs_throw(ctx, "InvalidPathException", "invalid path");
  }
  if (unlink(path) != 0) {
    int err = errno;
    free(path);
    return fs_throw_file_op_error(ctx, err);
  }
  free(path);
  return PS_OK;
}

static PS_Status fs_cp(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc; (void)out;
  if (!argv) return PS_ERR;
  if (ps_typeof(argv[0]) != PS_T_STRING || ps_typeof(argv[1]) != PS_T_STRING) return fs_throw(ctx, "InvalidPathException", "invalid path");
  char *src = fs_cstr(argv[0]);
  char *dst = fs_cstr(argv[1]);
  if (!src || !dst || src[0] == '\0' || dst[0] == '\0') {
    free(src); free(dst);
    return fs_throw(ctx, "InvalidPathException", "invalid path");
  }
  struct stat st;
  if (stat(src, &st) != 0) {
    int err = errno;
    free(src); free(dst);
    return fs_throw_stat_error(ctx, err);
  }
  if (!S_ISREG(st.st_mode)) {
    free(src); free(dst);
    return fs_throw(ctx, "NotAFileException", "not a file");
  }
  const char *slash = strrchr(dst, '/');
  size_t dir_len = slash ? (size_t)(slash - dst) : 0;
  char tmp_name[128];
  int tmp_fd = -1;
  char *tmp_path = NULL;
  for (int attempt = 0; attempt < 16; attempt += 1) {
    snprintf(tmp_name, sizeof(tmp_name), ".ps_tmp_%d_%d", (int)getpid(), attempt);
    if (slash) {
      size_t tlen = dir_len + 1 + strlen(tmp_name);
      tmp_path = (char *)malloc(tlen + 1);
      if (!tmp_path) { free(src); free(dst); return fs_throw(ctx, "IOException", "io failed"); }
      memcpy(tmp_path, dst, dir_len);
      tmp_path[dir_len] = '/';
      strcpy(tmp_path + dir_len + 1, tmp_name);
    } else {
      tmp_path = strdup(tmp_name);
      if (!tmp_path) { free(src); free(dst); return fs_throw(ctx, "IOException", "io failed"); }
    }
    tmp_fd = open(tmp_path, O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (tmp_fd >= 0) break;
    int err = errno;
    free(tmp_path); tmp_path = NULL;
    if (err == EEXIST) continue;
    free(src); free(dst);
    return fs_throw_common_error(ctx, err);
  }
  if (tmp_fd < 0) {
    free(src); free(dst);
    return fs_throw(ctx, "IOException", "io failed");
  }
  int src_fd = open(src, O_RDONLY);
  if (src_fd < 0) {
    int err = errno;
    close(tmp_fd);
    if (tmp_path) { unlink(tmp_path); free(tmp_path); }
    free(src); free(dst);
    return fs_throw_common_error(ctx, err);
  }
  uint8_t buf[16384];
  int ok = 1;
  while (1) {
    ssize_t r = read(src_fd, buf, sizeof(buf));
    if (r == 0) break;
    if (r < 0) { ok = 0; break; }
    size_t off = 0;
    while (off < (size_t)r) {
      ssize_t w = write(tmp_fd, buf + off, (size_t)r - off);
      if (w <= 0) { ok = 0; break; }
      off += (size_t)w;
    }
    if (!ok) break;
  }
  int write_err = errno;
  close(src_fd);
  close(tmp_fd);
  if (!ok) {
    if (tmp_path) { unlink(tmp_path); free(tmp_path); }
    free(src); free(dst);
    return fs_throw_common_error(ctx, write_err);
  }
  if (rename(tmp_path, dst) != 0) {
    int err = errno;
    if (tmp_path) { unlink(tmp_path); free(tmp_path); }
    free(src); free(dst);
    return fs_throw_common_error(ctx, err);
  }
  free(tmp_path);
  free(src); free(dst);
  return PS_OK;
}

static PS_Status fs_mv(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc; (void)out;
  if (!argv) return PS_ERR;
  if (ps_typeof(argv[0]) != PS_T_STRING || ps_typeof(argv[1]) != PS_T_STRING) return fs_throw(ctx, "InvalidPathException", "invalid path");
  char *src = fs_cstr(argv[0]);
  char *dst = fs_cstr(argv[1]);
  if (!src || !dst || src[0] == '\0' || dst[0] == '\0') {
    free(src); free(dst);
    return fs_throw(ctx, "InvalidPathException", "invalid path");
  }
  if (rename(src, dst) != 0) {
    int err = errno;
    free(src); free(dst);
    if (err == EXDEV) return fs_throw(ctx, "IOException", "io failed");
    return fs_throw_common_error(ctx, err);
  }
  free(src); free(dst);
  return PS_OK;
}

static PS_Status fs_chmod(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc; (void)out;
  if (!argv) return PS_ERR;
  if (ps_typeof(argv[0]) != PS_T_STRING || ps_typeof(argv[1]) != PS_T_INT) return fs_throw(ctx, "InvalidPathException", "invalid path");
  char *path = fs_cstr(argv[0]);
  if (!path || path[0] == '\0') {
    free(path);
    return fs_throw(ctx, "InvalidPathException", "invalid path");
  }
  int64_t mode = ps_as_int(argv[1]);
  if (chmod(path, (mode_t)mode) != 0) {
    int err = errno;
    free(path);
    return fs_throw_common_error(ctx, err);
  }
  free(path);
  return PS_OK;
}

static PS_Status fs_cwd(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc; (void)argv;
  if (!out) return PS_ERR;
  char *buf = getcwd(NULL, 0);
  if (!buf) return fs_throw(ctx, "IOException", "io failed");
  PS_Value *s = ps_make_string_utf8(ctx, buf, strlen(buf));
  free(buf);
  if (!s) return PS_ERR;
  *out = s;
  return PS_OK;
}

static PS_Status fs_cd(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc; (void)out;
  if (!argv) return PS_ERR;
  if (ps_typeof(argv[0]) != PS_T_STRING) return fs_throw(ctx, "InvalidPathException", "invalid path");
  char *path = fs_cstr(argv[0]);
  if (!path || path[0] == '\0') {
    free(path);
    return fs_throw(ctx, "InvalidPathException", "invalid path");
  }
  if (chdir(path) != 0) {
    int err = errno;
    free(path);
    if (err == ENOENT) return fs_throw(ctx, "FileNotFoundException", "file not found");
    if (err == ENOTDIR) return fs_throw(ctx, "NotADirectoryException", "not a directory");
    if (err == EACCES || err == EPERM) return fs_throw(ctx, "PermissionDeniedException", "permission denied");
    if (err == EINVAL || err == ENAMETOOLONG || err == ELOOP) return fs_throw(ctx, "InvalidPathException", "invalid path");
    return fs_throw(ctx, "IOException", "io failed");
  }
  free(path);
  return PS_OK;
}

static PS_Status fs_path_info(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!out || !argv) return PS_ERR;
  if (ps_typeof(argv[0]) != PS_T_STRING) return fs_throw(ctx, "InvalidPathException", "invalid path");
  const char *ptr = ps_string_ptr(argv[0]);
  size_t len = ps_string_len(argv[0]);
  if (!ptr || len == 0) return fs_throw(ctx, "InvalidPathException", "invalid path");
  const char *slash = NULL;
  for (size_t i = 0; i < len; i++) {
    if (ptr[i] == '/') slash = ptr + i;
  }
  size_t dirname_len = 0;
  size_t basename_off = 0;
  if (slash) {
    size_t pos = (size_t)(slash - ptr);
    if (pos == 0) dirname_len = 1;
    else dirname_len = pos;
    basename_off = pos + 1;
  }
  size_t basename_len = len - basename_off;
  const char *basename_ptr = ptr + basename_off;
  size_t dot_pos = (size_t)-1;
  for (size_t i = 0; i < basename_len; i++) {
    if (basename_ptr[i] == '.') dot_pos = i;
  }
  size_t filename_len = basename_len;
  size_t ext_len = 0;
  if (dot_pos != (size_t)-1 && dot_pos > 0 && dot_pos + 1 < basename_len) {
    filename_len = dot_pos;
    ext_len = basename_len - dot_pos - 1;
  }
  PS_Value *obj = ps_make_object(ctx);
  if (!obj) return PS_ERR;
  PS_Value *v_dir = fs_make_string(ctx, ptr, dirname_len);
  PS_Value *v_base = fs_make_string(ctx, basename_ptr, basename_len);
  PS_Value *v_file = fs_make_string(ctx, basename_ptr, filename_len);
  PS_Value *v_ext = fs_make_string(ctx, basename_ptr + filename_len + (ext_len ? 1 : 0), ext_len);
  if (!v_dir || !v_base || !v_file || !v_ext) return PS_ERR;
  if (fs_set_obj_field(ctx, obj, "dirname", v_dir) != PS_OK) return PS_ERR;
  if (fs_set_obj_field(ctx, obj, "basename", v_base) != PS_OK) return PS_ERR;
  if (fs_set_obj_field(ctx, obj, "filename", v_file) != PS_OK) return PS_ERR;
  if (fs_set_obj_field(ctx, obj, "extension", v_ext) != PS_OK) return PS_ERR;
  *out = obj;
  return PS_OK;
}

static PS_Status fs_open_dir(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!out || !argv) return PS_ERR;
  if (ps_typeof(argv[0]) != PS_T_STRING) return fs_throw(ctx, "InvalidPathException", "invalid path");
  char *path = fs_cstr(argv[0]);
  if (!path || path[0] == '\0') {
    free(path);
    return fs_throw(ctx, "InvalidPathException", "invalid path");
  }
  DIR *d = opendir(path);
  if (!d) {
    int err = errno;
    free(path);
    return fs_throw_open_dir_error(ctx, err);
  }
  FsDirState *st = (FsDirState *)calloc(1, sizeof(FsDirState));
  if (!st) {
    closedir(d);
    free(path);
    return fs_throw(ctx, "IOException", "io failed");
  }
  st->dir = d;
  st->path = path;
  st->next = NULL;
  st->done = 0;
  st->closed = 0;
  PS_Value *obj = ps_make_object(ctx);
  if (!obj) return PS_ERR;
  PS_Value *ptr = ps_make_int(ctx, (int64_t)(intptr_t)st);
  if (!ptr) return PS_ERR;
  if (fs_set_obj_field(ctx, obj, "__fs_dir_ptr", ptr) != PS_OK) return PS_ERR;
  *out = obj;
  return PS_OK;
}

static int fs_dir_fill_next(PS_Context *ctx, FsDirState *st) {
  if (!st || st->closed || !st->dir) {
    fs_throw(ctx, "IOException", "dir closed");
    return 0;
  }
  if (st->next) return 1;
  if (st->done) return 0;
  errno = 0;
  while (1) {
    struct dirent *ent = readdir(st->dir);
    if (!ent) break;
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
    st->next = strdup(ent->d_name);
    if (!st->next) {
      fs_throw(ctx, "IOException", "io failed");
      return 0;
    }
    return 1;
  }
  if (errno != 0) {
    fs_throw(ctx, "IOException", "io failed");
    return 0;
  }
  st->done = 1;
  return 0;
}

static PS_Status fs_dir_has_next(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!out || !argv) return PS_ERR;
  FsDirState *st = fs_dir_state(ctx, argv[0]);
  if (!st) return fs_throw(ctx, "IOException", "invalid dir");
  int ok = fs_dir_fill_next(ctx, st);
  PS_Value *b = ps_make_bool(ctx, ok ? 1 : 0);
  if (!b) return PS_ERR;
  *out = b;
  return PS_OK;
}

static PS_Status fs_dir_next(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!out || !argv) return PS_ERR;
  FsDirState *st = fs_dir_state(ctx, argv[0]);
  if (!st) return fs_throw(ctx, "IOException", "invalid dir");
  if (!fs_dir_fill_next(ctx, st)) return fs_throw(ctx, "IOException", "no more entries");
  size_t len = strlen(st->next);
  PS_Value *s = ps_make_string_utf8(ctx, st->next, len);
  free(st->next);
  st->next = NULL;
  if (!s) return PS_ERR;
  *out = s;
  return PS_OK;
}

static PS_Status fs_dir_close(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc; (void)out;
  if (!argv) return PS_ERR;
  FsDirState *st = fs_dir_state(ctx, argv[0]);
  if (!st) return fs_throw(ctx, "IOException", "invalid dir");
  if (!st->closed) {
    if (st->dir) closedir(st->dir);
    st->dir = NULL;
    st->closed = 1;
    if (st->next) { free(st->next); st->next = NULL; }
  }
  return PS_OK;
}

static PS_Status fs_dir_reset(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc; (void)out;
  if (!argv) return PS_ERR;
  FsDirState *st = fs_dir_state(ctx, argv[0]);
  if (!st || st->closed || !st->dir) return fs_throw(ctx, "IOException", "dir closed");
  if (st->next) { free(st->next); st->next = NULL; }
  st->done = 0;
  rewinddir(st->dir);
  return PS_OK;
}

static void fs_walker_clear_next(FsWalkerState *w) {
  if (!w) return;
  if (w->next_path) { free(w->next_path); w->next_path = NULL; }
  if (w->next_name) { free(w->next_name); w->next_name = NULL; }
  w->has_next = 0;
}

static int fs_walker_push(FsWalkerState *w, DIR *dir, char *path, int depth) {
  if (!w) return 0;
  if (w->len >= w->cap) {
    size_t new_cap = w->cap == 0 ? 4 : w->cap * 2;
    FsWalkFrame *n = (FsWalkFrame *)realloc(w->frames, sizeof(FsWalkFrame) * new_cap);
    if (!n) return 0;
    w->frames = n;
    w->cap = new_cap;
  }
  w->frames[w->len].dir = dir;
  w->frames[w->len].path = path;
  w->frames[w->len].depth = depth;
  w->len += 1;
  return 1;
}

static void fs_walker_close_all(FsWalkerState *w) {
  if (!w) return;
  for (size_t i = 0; i < w->len; i++) {
    if (w->frames[i].dir) closedir(w->frames[i].dir);
    if (w->frames[i].path) free(w->frames[i].path);
  }
  free(w->frames);
  w->frames = NULL;
  w->len = 0;
  w->cap = 0;
  fs_walker_clear_next(w);
}

static int fs_walker_fill_next(PS_Context *ctx, FsWalkerState *w) {
  if (!w || w->closed) {
    fs_throw(ctx, "IOException", "walker closed");
    return 0;
  }
  if (w->has_next) return 1;
  while (w->len > 0) {
    FsWalkFrame *fr = &w->frames[w->len - 1];
    errno = 0;
    struct dirent *ent = readdir(fr->dir);
    if (!ent) {
      if (errno != 0) { fs_throw(ctx, "IOException", "io failed"); return 0; }
      closedir(fr->dir);
      free(fr->path);
      w->len -= 1;
      continue;
    }
    if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
    char *full = fs_join(fr->path, ent->d_name);
    if (!full) { fs_throw(ctx, "IOException", "io failed"); return 0; }
    struct stat st;
    if (lstat(full, &st) != 0) {
      int err = errno;
      free(full);
      fs_throw_stat_error(ctx, err);
      return 0;
    }
    int is_symlink = S_ISLNK(st.st_mode);
    int is_dir = S_ISDIR(st.st_mode);
    int is_file = S_ISREG(st.st_mode);
    if (is_symlink && w->follow_symlinks) {
      struct stat st2;
      if (stat(full, &st2) != 0) {
        int err = errno;
        if (err != ENOENT) {
          free(full);
          fs_throw_stat_error(ctx, err);
          return 0;
        }
        is_dir = 0;
        is_file = 0;
      } else {
        is_dir = S_ISDIR(st2.st_mode);
        is_file = S_ISREG(st2.st_mode);
      }
    } else if (is_symlink) {
      is_dir = 0;
      is_file = 0;
    }
    int depth = fr->depth;
    if (is_dir && (w->max_depth < 0 || depth < w->max_depth)) {
      DIR *child = opendir(full);
      if (!child) {
        int err = errno;
        free(full);
        fs_throw_open_dir_error(ctx, err);
        return 0;
      }
      char *child_path = strdup(full);
      if (!child_path || !fs_walker_push(w, child, child_path, depth + 1)) {
        if (child_path) free(child_path);
        closedir(child);
        free(full);
        fs_throw(ctx, "IOException", "io failed");
        return 0;
      }
    }
    w->next_path = full;
    w->next_name = strdup(ent->d_name);
    if (!w->next_name) {
      fs_throw(ctx, "IOException", "io failed");
      return 0;
    }
    w->next_depth = depth;
    w->next_is_dir = is_dir;
    w->next_is_file = is_file;
    w->next_is_symlink = is_symlink;
    w->has_next = 1;
    return 1;
  }
  return 0;
}

static PS_Status fs_walk(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!out || !argv) return PS_ERR;
  if (ps_typeof(argv[0]) != PS_T_STRING) return fs_throw(ctx, "InvalidPathException", "invalid path");
  if (ps_typeof(argv[1]) != PS_T_INT) return fs_throw(ctx, "IOException", "io failed");
  if (ps_typeof(argv[2]) != PS_T_BOOL) return fs_throw(ctx, "IOException", "io failed");
  char *path = fs_cstr(argv[0]);
  if (!path || path[0] == '\0') {
    free(path);
    return fs_throw(ctx, "InvalidPathException", "invalid path");
  }
  DIR *dir = opendir(path);
  if (!dir) {
    int err = errno;
    free(path);
    return fs_throw_open_dir_error(ctx, err);
  }
  FsWalkerState *w = (FsWalkerState *)calloc(1, sizeof(FsWalkerState));
  if (!w) {
    closedir(dir);
    free(path);
    return fs_throw(ctx, "IOException", "io failed");
  }
  w->max_depth = (int)ps_as_int(argv[1]);
  w->follow_symlinks = ps_as_bool(argv[2]) ? 1 : 0;
  w->closed = 0;
  w->has_next = 0;
  w->frames = NULL;
  w->len = 0;
  w->cap = 0;
  if (!fs_walker_push(w, dir, path, 0)) {
    closedir(dir);
    free(path);
    free(w);
    return fs_throw(ctx, "IOException", "io failed");
  }
  PS_Value *obj = ps_make_object(ctx);
  if (!obj) return PS_ERR;
  PS_Value *ptr = ps_make_int(ctx, (int64_t)(intptr_t)w);
  if (!ptr) return PS_ERR;
  if (fs_set_obj_field(ctx, obj, "__fs_walker_ptr", ptr) != PS_OK) return PS_ERR;
  *out = obj;
  return PS_OK;
}

static PS_Status fs_walker_has_next(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!out || !argv) return PS_ERR;
  FsWalkerState *w = fs_walker_state(ctx, argv[0]);
  if (!w) return fs_throw(ctx, "IOException", "invalid walker");
  int ok = fs_walker_fill_next(ctx, w);
  PS_Value *b = ps_make_bool(ctx, ok ? 1 : 0);
  if (!b) return PS_ERR;
  *out = b;
  return PS_OK;
}

static PS_Status fs_walker_next(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!out || !argv) return PS_ERR;
  FsWalkerState *w = fs_walker_state(ctx, argv[0]);
  if (!w) return fs_throw(ctx, "IOException", "invalid walker");
  if (!fs_walker_fill_next(ctx, w)) return fs_throw(ctx, "IOException", "no more entries");
  PS_Value *obj = ps_make_object(ctx);
  if (!obj) return PS_ERR;
  PS_Value *v_path = fs_make_string(ctx, w->next_path, strlen(w->next_path));
  PS_Value *v_name = fs_make_string(ctx, w->next_name, strlen(w->next_name));
  PS_Value *v_depth = ps_make_int(ctx, (int64_t)w->next_depth);
  PS_Value *v_is_dir = ps_make_bool(ctx, w->next_is_dir);
  PS_Value *v_is_file = ps_make_bool(ctx, w->next_is_file);
  PS_Value *v_is_symlink = ps_make_bool(ctx, w->next_is_symlink);
  if (!v_path || !v_name || !v_depth || !v_is_dir || !v_is_file || !v_is_symlink) return PS_ERR;
  if (fs_set_obj_field(ctx, obj, "path", v_path) != PS_OK) return PS_ERR;
  if (fs_set_obj_field(ctx, obj, "name", v_name) != PS_OK) return PS_ERR;
  if (fs_set_obj_field(ctx, obj, "depth", v_depth) != PS_OK) return PS_ERR;
  if (fs_set_obj_field(ctx, obj, "isDir", v_is_dir) != PS_OK) return PS_ERR;
  if (fs_set_obj_field(ctx, obj, "isFile", v_is_file) != PS_OK) return PS_ERR;
  if (fs_set_obj_field(ctx, obj, "isSymlink", v_is_symlink) != PS_OK) return PS_ERR;
  fs_walker_clear_next(w);
  *out = obj;
  return PS_OK;
}

static PS_Status fs_walker_close(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc; (void)out;
  if (!argv) return PS_ERR;
  FsWalkerState *w = fs_walker_state(ctx, argv[0]);
  if (!w) return fs_throw(ctx, "IOException", "invalid walker");
  if (!w->closed) {
    fs_walker_close_all(w);
    w->closed = 1;
  }
  return PS_OK;
}

PS_Status ps_module_init(PS_Context *ctx, PS_Module *out) {
  (void)ctx;
  static PS_NativeFnDesc fns[] = {
      {.name = "exists", .fn = fs_exists, .arity = 1, .ret_type = PS_T_BOOL, .param_types = NULL, .flags = 0},
      {.name = "isFile", .fn = fs_is_file, .arity = 1, .ret_type = PS_T_BOOL, .param_types = NULL, .flags = 0},
      {.name = "isDir", .fn = fs_is_dir, .arity = 1, .ret_type = PS_T_BOOL, .param_types = NULL, .flags = 0},
      {.name = "isSymlink", .fn = fs_is_symlink, .arity = 1, .ret_type = PS_T_BOOL, .param_types = NULL, .flags = 0},
      {.name = "isReadable", .fn = fs_is_readable, .arity = 1, .ret_type = PS_T_BOOL, .param_types = NULL, .flags = 0},
      {.name = "isWritable", .fn = fs_is_writable, .arity = 1, .ret_type = PS_T_BOOL, .param_types = NULL, .flags = 0},
      {.name = "isExecutable", .fn = fs_is_executable, .arity = 1, .ret_type = PS_T_BOOL, .param_types = NULL, .flags = 0},
      {.name = "size", .fn = fs_size, .arity = 1, .ret_type = PS_T_INT, .param_types = NULL, .flags = 0},
      {.name = "mkdir", .fn = fs_mkdir, .arity = 1, .ret_type = PS_T_VOID, .param_types = NULL, .flags = 0},
      {.name = "rmdir", .fn = fs_rmdir, .arity = 1, .ret_type = PS_T_VOID, .param_types = NULL, .flags = 0},
      {.name = "rm", .fn = fs_rm, .arity = 1, .ret_type = PS_T_VOID, .param_types = NULL, .flags = 0},
      {.name = "cp", .fn = fs_cp, .arity = 2, .ret_type = PS_T_VOID, .param_types = NULL, .flags = 0},
      {.name = "mv", .fn = fs_mv, .arity = 2, .ret_type = PS_T_VOID, .param_types = NULL, .flags = 0},
      {.name = "chmod", .fn = fs_chmod, .arity = 2, .ret_type = PS_T_VOID, .param_types = NULL, .flags = 0},
      {.name = "cwd", .fn = fs_cwd, .arity = 0, .ret_type = PS_T_STRING, .param_types = NULL, .flags = 0},
      {.name = "cd", .fn = fs_cd, .arity = 1, .ret_type = PS_T_VOID, .param_types = NULL, .flags = 0},
      {.name = "pathInfo", .fn = fs_path_info, .arity = 1, .ret_type = PS_T_OBJECT, .param_types = NULL, .flags = 0},
      {.name = "openDir", .fn = fs_open_dir, .arity = 1, .ret_type = PS_T_OBJECT, .param_types = NULL, .flags = 0},
      {.name = "walk", .fn = fs_walk, .arity = 3, .ret_type = PS_T_OBJECT, .param_types = NULL, .flags = 0},
      {.name = "__dir_hasNext", .fn = fs_dir_has_next, .arity = 1, .ret_type = PS_T_BOOL, .param_types = NULL, .flags = 0},
      {.name = "__dir_next", .fn = fs_dir_next, .arity = 1, .ret_type = PS_T_STRING, .param_types = NULL, .flags = 0},
      {.name = "__dir_close", .fn = fs_dir_close, .arity = 1, .ret_type = PS_T_VOID, .param_types = NULL, .flags = 0},
      {.name = "__dir_reset", .fn = fs_dir_reset, .arity = 1, .ret_type = PS_T_VOID, .param_types = NULL, .flags = 0},
      {.name = "__walker_hasNext", .fn = fs_walker_has_next, .arity = 1, .ret_type = PS_T_BOOL, .param_types = NULL, .flags = 0},
      {.name = "__walker_next", .fn = fs_walker_next, .arity = 1, .ret_type = PS_T_OBJECT, .param_types = NULL, .flags = 0},
      {.name = "__walker_close", .fn = fs_walker_close, .arity = 1, .ret_type = PS_T_VOID, .param_types = NULL, .flags = 0},
  };
  out->module_name = "Fs";
  out->api_version = PS_API_VERSION;
  out->fn_count = sizeof(fns) / sizeof(fns[0]);
  out->fns = fns;
  return PS_OK;
}
