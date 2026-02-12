#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

#include "ps/ps_api.h"

extern char **environ;

static PS_Status sys_throw(PS_Context *ctx, const char *type, const char *msg) {
  char buf[256];
  const char *t = type ? type : "IOException";
  const char *m = msg ? msg : "";
  snprintf(buf, sizeof(buf), "sys:%s:%s", t, m);
  ps_throw(ctx, PS_ERR_INTERNAL, buf);
  return PS_ERR;
}

static PS_Status sys_invalid_name(PS_Context *ctx) {
  return sys_throw(ctx, "InvalidEnvironmentNameException", "invalid environment name");
}

static PS_Status sys_invalid_arg(PS_Context *ctx, const char *msg) {
  return sys_throw(ctx, "InvalidArgumentException", msg ? msg : "invalid argument");
}

static PS_Status sys_io_error(PS_Context *ctx, const char *msg) {
  return sys_throw(ctx, "IOException", msg ? msg : "io failed");
}

static PS_Status sys_get_name(PS_Context *ctx, PS_Value *v, char **out_name) {
  if (!out_name) return PS_ERR;
  *out_name = NULL;
  if (!v || ps_typeof(v) != PS_T_STRING) return sys_invalid_name(ctx);
  const char *name = ps_string_ptr(v);
  size_t len = ps_string_len(v);
  if (len == 0) return sys_invalid_name(ctx);
  if (memchr(name, '=', len) != NULL) return sys_invalid_name(ctx);
  char *dup = (char *)malloc(len + 1);
  if (!dup) {
    ps_throw(ctx, PS_ERR_OOM, "out of memory");
    return PS_ERR;
  }
  memcpy(dup, name, len);
  dup[len] = '\0';
  *out_name = dup;
  return PS_OK;
}

static PS_Status sys_has_env(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!argv || !out) return PS_ERR;
  char *name = NULL;
  if (sys_get_name(ctx, argv[0], &name) != PS_OK) return PS_ERR;
  const char *val = getenv(name);
  free(name);
  *out = ps_make_bool(ctx, val != NULL);
  return *out ? PS_OK : PS_ERR;
}

static PS_Status sys_env(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  (void)argc;
  if (!argv || !out) return PS_ERR;
  char *name = NULL;
  if (sys_get_name(ctx, argv[0], &name) != PS_OK) return PS_ERR;
  const char *val = getenv(name);
  free(name);
  if (!val) return sys_throw(ctx, "EnvironmentAccessException", "variable not found");
  size_t len = strlen(val);
  PS_Value *s = ps_make_string_utf8(ctx, val, len);
  if (!s) {
    if (ps_last_error_code(ctx) == PS_ERR_UTF8) {
      return sys_throw(ctx, "EnvironmentAccessException", "invalid utf8");
    }
    return PS_ERR;
  }
  *out = s;
  return PS_OK;
}

static int sys_set_nonblocking(int fd) {
  int flags = fcntl(fd, F_GETFL, 0);
  if (flags < 0) return 0;
  if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) return 0;
  return 1;
}

static int sys_set_cloexec(int fd) {
  int flags = fcntl(fd, F_GETFD, 0);
  if (flags < 0) return 0;
  if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) != 0) return 0;
  return 1;
}

static PS_Status sys_set_obj_field(PS_Context *ctx, PS_Value *obj, const char *key, PS_Value *val) {
  if (!obj || !val) return PS_ERR;
  if (ps_object_set_str(ctx, obj, key, strlen(key), val) != PS_OK) {
    ps_value_release(val);
    return PS_ERR;
  }
  ps_value_release(val);
  return PS_OK;
}

static PS_Status sys_make_bytes_list(PS_Context *ctx, const uint8_t *buf, size_t len, PS_Value **out_list) {
  if (!out_list) return PS_ERR;
  PS_Value *list = ps_make_list(ctx);
  if (!list) return PS_ERR;
  for (size_t i = 0; i < len; i++) {
    PS_Value *b = ps_make_byte(ctx, buf[i]);
    if (!b) {
      ps_value_release(list);
      return PS_ERR;
    }
    if (ps_list_push(ctx, list, b) != PS_OK) {
      ps_value_release(b);
      ps_value_release(list);
      return PS_ERR;
    }
    ps_value_release(b);
  }
  *out_list = list;
  return PS_OK;
}

static PS_Status sys_make_event(PS_Context *ctx, int stream, const uint8_t *buf, size_t len, PS_Value **out_event) {
  if (!out_event) return PS_ERR;
  PS_Value *obj = ps_make_object(ctx);
  if (!obj) return PS_ERR;
  PS_Value *v_stream = ps_make_int(ctx, (int64_t)stream);
  if (!v_stream) {
    ps_value_release(obj);
    return PS_ERR;
  }
  PS_Value *bytes = NULL;
  if (sys_make_bytes_list(ctx, buf, len, &bytes) != PS_OK) {
    ps_value_release(v_stream);
    ps_value_release(obj);
    return PS_ERR;
  }
  if (sys_set_obj_field(ctx, obj, "stream", v_stream) != PS_OK) {
    ps_value_release(bytes);
    ps_value_release(obj);
    return PS_ERR;
  }
  if (sys_set_obj_field(ctx, obj, "data", bytes) != PS_OK) {
    ps_value_release(obj);
    return PS_ERR;
  }
  *out_event = obj;
  return PS_OK;
}

static PS_Status sys_make_result(PS_Context *ctx, int exit_code, PS_Value *events, PS_Value **out) {
  if (!out) return PS_ERR;
  PS_Value *obj = ps_make_object(ctx);
  if (!obj) return PS_ERR;
  PS_Value *v_exit = ps_make_int(ctx, (int64_t)exit_code);
  if (!v_exit) {
    ps_value_release(obj);
    return PS_ERR;
  }
  if (sys_set_obj_field(ctx, obj, "exitCode", v_exit) != PS_OK) {
    ps_value_release(obj);
    return PS_ERR;
  }
  if (sys_set_obj_field(ctx, obj, "events", ps_value_retain(events)) != PS_OK) {
    ps_value_release(obj);
    return PS_ERR;
  }
  *out = obj;
  return PS_OK;
}

static PS_Status sys_collect_args(PS_Context *ctx, PS_Value *args_list, char ***out_argv, size_t *out_argc) {
  if (!out_argv || !out_argc) return PS_ERR;
  *out_argv = NULL;
  *out_argc = 0;
  if (!args_list || ps_typeof(args_list) != PS_T_LIST) return sys_invalid_arg(ctx, "invalid args");
  size_t len = ps_list_len(args_list);
  char **argv = (char **)calloc(len + 2, sizeof(char *));
  if (!argv) {
    ps_throw(ctx, PS_ERR_OOM, "out of memory");
    return PS_ERR;
  }
  for (size_t i = 0; i < len; i++) {
    PS_Value *v = ps_list_get(ctx, args_list, i);
    if (!v || ps_typeof(v) != PS_T_STRING) {
      for (size_t j = 0; j < i; j++) free(argv[j]);
      free(argv);
      return sys_invalid_arg(ctx, "invalid args");
    }
    const char *s = ps_string_ptr(v);
    size_t slen = ps_string_len(v);
    char *dup = (char *)malloc(slen + 1);
    if (!dup) {
      for (size_t j = 0; j < i; j++) free(argv[j]);
      free(argv);
      ps_throw(ctx, PS_ERR_OOM, "out of memory");
      return PS_ERR;
    }
    memcpy(dup, s, slen);
    dup[slen] = '\0';
    argv[i + 1] = dup;
  }
  *out_argv = argv;
  *out_argc = len + 1;
  return PS_OK;
}

static void sys_free_args(char **argv, size_t argc) {
  if (!argv) return;
  for (size_t i = 1; i < argc; i++) free(argv[i]);
  free(argv);
}

static PS_Status sys_collect_input(PS_Context *ctx, PS_Value *input_list, uint8_t **out_buf, size_t *out_len) {
  if (!out_buf || !out_len) return PS_ERR;
  *out_buf = NULL;
  *out_len = 0;
  if (!input_list || ps_typeof(input_list) != PS_T_LIST) return sys_invalid_arg(ctx, "invalid input");
  size_t len = ps_list_len(input_list);
  if (len == 0) return PS_OK;
  uint8_t *buf = (uint8_t *)malloc(len);
  if (!buf) {
    ps_throw(ctx, PS_ERR_OOM, "out of memory");
    return PS_ERR;
  }
  for (size_t i = 0; i < len; i++) {
    PS_Value *v = ps_list_get(ctx, input_list, i);
    if (!v || ps_typeof(v) != PS_T_BYTE) {
      free(buf);
      return sys_invalid_arg(ctx, "invalid input");
    }
    buf[i] = ps_as_byte(v);
  }
  *out_buf = buf;
  *out_len = len;
  return PS_OK;
}

static PS_Status sys_throw_exec_errno(PS_Context *ctx, int err) {
  if (err == ENOENT || err == ENOTDIR) return sys_throw(ctx, "InvalidExecutableException", "invalid executable");
  if (err == EACCES) return sys_throw(ctx, "ProcessPermissionException", "permission denied");
  return sys_throw(ctx, "ProcessExecutionException", "execution failed");
}

static PS_Status sys_execute(PS_Context *ctx, int argc, PS_Value **argv, PS_Value **out) {
  if (!argv || !out || argc < 5) return PS_ERR;
  if (ps_typeof(argv[0]) != PS_T_STRING) return sys_invalid_arg(ctx, "invalid program");
  const char *program = ps_string_ptr(argv[0]);
  size_t program_len = ps_string_len(argv[0]);
  if (!program || program_len == 0) return sys_throw(ctx, "InvalidExecutableException", "invalid executable");

  char **exec_argv = NULL;
  size_t exec_argc = 0;
  if (sys_collect_args(ctx, argv[1], &exec_argv, &exec_argc) != PS_OK) return PS_ERR;

  uint8_t *input_buf = NULL;
  size_t input_len = 0;
  if (sys_collect_input(ctx, argv[2], &input_buf, &input_len) != PS_OK) {
    if (exec_argv) {
      sys_free_args(exec_argv, exec_argc);
    }
    return PS_ERR;
  }

  if (ps_typeof(argv[3]) != PS_T_BOOL || ps_typeof(argv[4]) != PS_T_BOOL) {
    free(input_buf);
    if (exec_argv) {
      sys_free_args(exec_argv, exec_argc);
    }
    return sys_invalid_arg(ctx, "invalid capture flags");
  }
  int capture_stdout = ps_as_bool(argv[3]) ? 1 : 0;
  int capture_stderr = ps_as_bool(argv[4]) ? 1 : 0;

  int in_pipe[2] = {-1, -1};
  int out_pipe[2] = {-1, -1};
  int err_pipe[2] = {-1, -1};
  int exec_pipe[2] = {-1, -1};

  if (pipe(in_pipe) != 0) {
    free(input_buf);
    if (exec_argv) {
      sys_free_args(exec_argv, exec_argc);
    }
    return sys_io_error(ctx, "pipe failed");
  }
  if (capture_stdout && pipe(out_pipe) != 0) {
    close(in_pipe[0]);
    close(in_pipe[1]);
    free(input_buf);
    if (exec_argv) {
      sys_free_args(exec_argv, exec_argc);
    }
    return sys_io_error(ctx, "pipe failed");
  }
  if (capture_stderr && pipe(err_pipe) != 0) {
    close(in_pipe[0]);
    close(in_pipe[1]);
    if (capture_stdout) {
      close(out_pipe[0]);
      close(out_pipe[1]);
    }
    free(input_buf);
    if (exec_argv) {
      sys_free_args(exec_argv, exec_argc);
    }
    return sys_io_error(ctx, "pipe failed");
  }
  if (pipe(exec_pipe) != 0) {
    close(in_pipe[0]);
    close(in_pipe[1]);
    if (capture_stdout) {
      close(out_pipe[0]);
      close(out_pipe[1]);
    }
    if (capture_stderr) {
      close(err_pipe[0]);
      close(err_pipe[1]);
    }
    free(input_buf);
    if (exec_argv) {
      sys_free_args(exec_argv, exec_argc);
    }
    return sys_io_error(ctx, "pipe failed");
  }

  if (!sys_set_cloexec(exec_pipe[1])) {
    close(exec_pipe[0]);
    close(exec_pipe[1]);
    close(in_pipe[0]);
    close(in_pipe[1]);
    if (capture_stdout) {
      close(out_pipe[0]);
      close(out_pipe[1]);
    }
    if (capture_stderr) {
      close(err_pipe[0]);
      close(err_pipe[1]);
    }
    free(input_buf);
    if (exec_argv) {
      sys_free_args(exec_argv, exec_argc);
    }
    return sys_io_error(ctx, "fcntl failed");
  }

  pid_t pid = fork();
  if (pid < 0) {
    close(exec_pipe[0]);
    close(exec_pipe[1]);
    close(in_pipe[0]);
    close(in_pipe[1]);
    if (capture_stdout) {
      close(out_pipe[0]);
      close(out_pipe[1]);
    }
    if (capture_stderr) {
      close(err_pipe[0]);
      close(err_pipe[1]);
    }
    free(input_buf);
    if (exec_argv) {
      sys_free_args(exec_argv, exec_argc);
    }
    return sys_throw(ctx, "ProcessCreationException", "fork failed");
  }

  if (pid == 0) {
    close(exec_pipe[0]);
    if (dup2(in_pipe[0], STDIN_FILENO) < 0) _exit(127);
    close(in_pipe[0]);
    close(in_pipe[1]);

    if (capture_stdout) {
      if (dup2(out_pipe[1], STDOUT_FILENO) < 0) _exit(127);
      close(out_pipe[0]);
      close(out_pipe[1]);
    }
    if (capture_stderr) {
      if (dup2(err_pipe[1], STDERR_FILENO) < 0) _exit(127);
      close(err_pipe[0]);
      close(err_pipe[1]);
    }

    exec_argv[0] = (char *)program;
    exec_argv[exec_argc] = NULL;
    execve(program, exec_argv, environ);
    int err = errno;
    (void)write(exec_pipe[1], &err, sizeof(err));
    _exit(127);
  }

  close(exec_pipe[1]);
  close(in_pipe[0]);
  if (capture_stdout) close(out_pipe[1]);
  if (capture_stderr) close(err_pipe[1]);

  int exec_err = 0;
  ssize_t exec_read = read(exec_pipe[0], &exec_err, sizeof(exec_err));
  close(exec_pipe[0]);
  if (exec_read > 0) {
    if (capture_stdout) close(out_pipe[0]);
    if (capture_stderr) close(err_pipe[0]);
    close(in_pipe[1]);
    free(input_buf);
    if (exec_argv) {
      sys_free_args(exec_argv, exec_argc);
    }
    waitpid(pid, NULL, 0);
    return sys_throw_exec_errno(ctx, exec_err);
  }

  if (!sys_set_nonblocking(in_pipe[1])) {
    if (capture_stdout) close(out_pipe[0]);
    if (capture_stderr) close(err_pipe[0]);
    close(in_pipe[1]);
    free(input_buf);
    if (exec_argv) {
      sys_free_args(exec_argv, exec_argc);
    }
    return sys_io_error(ctx, "fcntl failed");
  }
  if (capture_stdout && !sys_set_nonblocking(out_pipe[0])) {
    if (capture_stdout) close(out_pipe[0]);
    if (capture_stderr) close(err_pipe[0]);
    close(in_pipe[1]);
    free(input_buf);
    if (exec_argv) {
      sys_free_args(exec_argv, exec_argc);
    }
    return sys_io_error(ctx, "fcntl failed");
  }
  if (capture_stderr && !sys_set_nonblocking(err_pipe[0])) {
    if (capture_stdout) close(out_pipe[0]);
    if (capture_stderr) close(err_pipe[0]);
    close(in_pipe[1]);
    free(input_buf);
    if (exec_argv) {
      sys_free_args(exec_argv, exec_argc);
    }
    return sys_io_error(ctx, "fcntl failed");
  }

  PS_Value *events = ps_make_list(ctx);
  if (!events) {
    if (capture_stdout) close(out_pipe[0]);
    if (capture_stderr) close(err_pipe[0]);
    close(in_pipe[1]);
    free(input_buf);
    if (exec_argv) {
      sys_free_args(exec_argv, exec_argc);
    }
    return PS_ERR;
  }

  size_t input_off = 0;
  int stdin_open = 1;
  int stdout_open = capture_stdout ? 1 : 0;
  int stderr_open = capture_stderr ? 1 : 0;

  if (input_len == 0) {
    close(in_pipe[1]);
    stdin_open = 0;
  }

  uint8_t buf[4096];
  while (stdin_open || stdout_open || stderr_open) {
    struct pollfd fds[3];
    nfds_t nfds = 0;

    if (stdin_open && input_off < input_len) {
      fds[nfds].fd = in_pipe[1];
      fds[nfds].events = POLLOUT;
      fds[nfds].revents = 0;
      nfds++;
    }
    if (stdout_open) {
      fds[nfds].fd = out_pipe[0];
      fds[nfds].events = POLLIN;
      fds[nfds].revents = 0;
      nfds++;
    }
    if (stderr_open) {
      fds[nfds].fd = err_pipe[0];
      fds[nfds].events = POLLIN;
      fds[nfds].revents = 0;
      nfds++;
    }

    if (nfds == 0) break;
    int pr = poll(fds, nfds, -1);
    if (pr < 0) {
      if (errno == EINTR) continue;
      if (stdout_open) close(out_pipe[0]);
      if (stderr_open) close(err_pipe[0]);
      if (stdin_open) close(in_pipe[1]);
      ps_value_release(events);
      free(input_buf);
      if (exec_argv) {
        sys_free_args(exec_argv, exec_argc);
      }
      return sys_io_error(ctx, "poll failed");
    }

    nfds_t idx = 0;
    if (stdin_open && input_off < input_len) {
      if (fds[idx].revents & POLLOUT) {
        ssize_t w = write(in_pipe[1], input_buf + input_off, input_len - input_off);
        if (w > 0) {
          input_off += (size_t)w;
          if (input_off >= input_len) {
            close(in_pipe[1]);
            stdin_open = 0;
          }
        } else if (w < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
          close(in_pipe[1]);
          stdin_open = 0;
        }
      }
      idx++;
    }

    if (stdout_open) {
      if (fds[idx].revents & (POLLIN | POLLHUP)) {
        ssize_t r = read(out_pipe[0], buf, sizeof(buf));
        if (r > 0) {
          PS_Value *ev = NULL;
          if (sys_make_event(ctx, 1, buf, (size_t)r, &ev) != PS_OK) {
            close(out_pipe[0]);
            if (stderr_open) close(err_pipe[0]);
            if (stdin_open) close(in_pipe[1]);
            ps_value_release(events);
            free(input_buf);
            if (exec_argv) {
              sys_free_args(exec_argv, exec_argc);
            }
            return PS_ERR;
          }
          if (ps_list_push(ctx, events, ev) != PS_OK) {
            ps_value_release(ev);
            close(out_pipe[0]);
            if (stderr_open) close(err_pipe[0]);
            if (stdin_open) close(in_pipe[1]);
            ps_value_release(events);
            free(input_buf);
            if (exec_argv) {
              sys_free_args(exec_argv, exec_argc);
            }
            return PS_ERR;
          }
          ps_value_release(ev);
        } else if (r == 0) {
          close(out_pipe[0]);
          stdout_open = 0;
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
          close(out_pipe[0]);
          stdout_open = 0;
        }
      }
      idx++;
    }

    if (stderr_open) {
      if (fds[idx].revents & (POLLIN | POLLHUP)) {
        ssize_t r = read(err_pipe[0], buf, sizeof(buf));
        if (r > 0) {
          PS_Value *ev = NULL;
          if (sys_make_event(ctx, 2, buf, (size_t)r, &ev) != PS_OK) {
            close(err_pipe[0]);
            if (stdout_open) close(out_pipe[0]);
            if (stdin_open) close(in_pipe[1]);
            ps_value_release(events);
            free(input_buf);
            if (exec_argv) {
              sys_free_args(exec_argv, exec_argc);
            }
            return PS_ERR;
          }
          if (ps_list_push(ctx, events, ev) != PS_OK) {
            ps_value_release(ev);
            close(err_pipe[0]);
            if (stdout_open) close(out_pipe[0]);
            if (stdin_open) close(in_pipe[1]);
            ps_value_release(events);
            free(input_buf);
            if (exec_argv) {
              sys_free_args(exec_argv, exec_argc);
            }
            return PS_ERR;
          }
          ps_value_release(ev);
        } else if (r == 0) {
          close(err_pipe[0]);
          stderr_open = 0;
        } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
          close(err_pipe[0]);
          stderr_open = 0;
        }
      }
      idx++;
    }
  }

  int status = 0;
  if (waitpid(pid, &status, 0) < 0) {
    ps_value_release(events);
    free(input_buf);
    if (exec_argv) {
      sys_free_args(exec_argv, exec_argc);
    }
    return sys_io_error(ctx, "waitpid failed");
  }

  int exit_code = 0;
  if (WIFEXITED(status)) exit_code = WEXITSTATUS(status);
  else if (WIFSIGNALED(status)) exit_code = 128 + WTERMSIG(status);

  free(input_buf);
  if (exec_argv) {
    sys_free_args(exec_argv, exec_argc);
  }

  PS_Value *res = NULL;
  if (sys_make_result(ctx, exit_code, events, &res) != PS_OK) {
    ps_value_release(events);
    return PS_ERR;
  }
  ps_value_release(events);
  *out = res;
  return PS_OK;
}

PS_Status ps_module_init(PS_Context *ctx, PS_Module *out) {
  (void)ctx;
  static PS_TypeTag params_str[] = {PS_T_STRING};
  static PS_TypeTag params_execute[] = {PS_T_STRING, PS_T_LIST, PS_T_LIST, PS_T_BOOL, PS_T_BOOL};
  static PS_NativeFnDesc fns[] = {
      {.name = "hasEnv", .fn = sys_has_env, .arity = 1, .ret_type = PS_T_BOOL, .param_types = params_str, .flags = 0},
      {.name = "env", .fn = sys_env, .arity = 1, .ret_type = PS_T_STRING, .param_types = params_str, .flags = 0},
      {.name = "execute", .fn = sys_execute, .arity = 5, .ret_type = PS_T_OBJECT, .param_types = params_execute, .flags = 0},
  };
  out->module_name = "Sys";
  out->api_version = PS_API_VERSION;
  out->fn_count = sizeof(fns) / sizeof(fns[0]);
  out->fns = fns;
  return PS_OK;
}
