#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "frontend.h"

static void usage(void) {
  fprintf(stderr, "Usage:\n");
  fprintf(stderr, "  pscc --check <file.pts>\n");
  fprintf(stderr, "  pscc --check-c <file.pts>\n");
  fprintf(stderr, "  pscc --check-c-static <file.pts>\n");
  fprintf(stderr, "  pscc --ast-c <file.pts>\n");
  fprintf(stderr, "  pscc --emit-ir-c-json <file.pts>\n");
  fprintf(stderr, "  pscc --emit-ir <file.pts> [--opt]\n");
  fprintf(stderr, "  pscc --emit-c <file.pts> [--opt]\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "Note: this is the native C CLI bootstrap. It forwards to the\n");
  fprintf(stderr, "current reference frontend until full C frontend parity is reached.\n");
}

static int file_is_executable(const char *path) {
  struct stat st;
  if (stat(path, &st) != 0) return 0;
  if (!S_ISREG(st.st_mode)) return 0;
  return access(path, X_OK) == 0;
}

static int forward_to_reference(int argc, char **argv) {
  const char *candidates[] = {
      "./bin/protoscriptc",
      "bin/protoscriptc",
      NULL,
  };

  const char *target = NULL;
  for (int i = 0; candidates[i] != NULL; i++) {
    if (file_is_executable(candidates[i])) {
      target = candidates[i];
      break;
    }
  }

  if (!target) {
    fprintf(stderr, "pscc: cannot find reference compiler at ./bin/protoscriptc\n");
    return 2;
  }

  char **fwd = (char **)calloc((size_t)argc + 1, sizeof(char *));
  if (!fwd) {
    fprintf(stderr, "pscc: allocation failure\n");
    return 2;
  }

  fwd[0] = (char *)target;
  for (int i = 1; i < argc; i++) fwd[i] = argv[i];
  fwd[argc] = NULL;

  execv(target, fwd);

  fprintf(stderr, "pscc: failed to exec %s: %s\n", target, strerror(errno));
  free(fwd);
  return 2;
}

static void print_diag(FILE *out, const char *fallback_file, const PsDiag *d) {
  const char *file = (d && d->file) ? d->file : fallback_file;
  int line = d ? d->line : 1;
  int col = d ? d->col : 1;
  const char *code = (d && d->code) ? d->code : NULL;
  const char *category = (d && d->category) ? d->category : NULL;
  const char *msg = (d && d->message[0]) ? d->message : "unknown error";
  if (code && code[0] && category && category[0]) {
    fprintf(out, "%s:%d:%d %s %s: %s\n", file ? file : "<unknown>", line, col, code, category, msg);
  } else if (category && category[0]) {
    fprintf(out, "%s:%d:%d %s: %s\n", file ? file : "<unknown>", line, col, category, msg);
  } else if (code && code[0]) {
    fprintf(out, "%s:%d:%d %s: %s\n", file ? file : "<unknown>", line, col, code, msg);
  } else {
    fprintf(out, "%s:%d:%d Error: %s\n", file ? file : "<unknown>", line, col, msg);
  }
}

int main(int argc, char **argv) {
  if (argc < 3 || argc > 4) {
    usage();
    return 2;
  }

  char exe_buf[PATH_MAX];
  if (realpath(argv[0], exe_buf)) {
    char *slash = strrchr(exe_buf, '/');
    if (slash) {
      *slash = '\0';
      ps_set_registry_exe_dir(exe_buf);
    }
  } else {
    char *slash = strrchr(argv[0], '/');
    if (slash) {
      size_t n = (size_t)(slash - argv[0]);
      if (n >= sizeof(exe_buf)) n = sizeof(exe_buf) - 1;
      memcpy(exe_buf, argv[0], n);
      exe_buf[n] = '\0';
      ps_set_registry_exe_dir(exe_buf);
    }
  }

  const char *mode = argv[1];
  const char *input = argv[2];
  int opt_count = 0;
  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--opt") == 0) opt_count++;
  }
  if (opt_count > 1) {
    fprintf(stderr, "pscc: --opt provided multiple times\n");
    return 2;
  }

  if (!(strcmp(mode, "--check") == 0 || strcmp(mode, "--check-c") == 0 || strcmp(mode, "--check-c-static") == 0 ||
        strcmp(mode, "--ast-c") == 0 || strcmp(mode, "--emit-ir-c-json") == 0 ||
        strcmp(mode, "--emit-ir") == 0 ||
        strcmp(mode, "--emit-c") == 0)) {
    usage();
    return 2;
  }

  if ((strcmp(mode, "--check") == 0 || strcmp(mode, "--check-c") == 0 || strcmp(mode, "--check-c-static") == 0 ||
       strcmp(mode, "--ast-c") == 0 || strcmp(mode, "--emit-ir-c-json") == 0) &&
      opt_count > 0) {
    fprintf(stderr, "pscc: --opt is only valid with --emit-ir or --emit-c\n");
    return 2;
  }

  if (strcmp(mode, "--check-c-static") == 0) {
    PsDiag d;
    int rc = ps_check_file_static(input, &d);
    if (rc != 0) {
      print_diag(stderr, input, &d);
      return (rc == 2) ? 2 : 1;
    }
    return 0;
  }

  if (strcmp(mode, "--ast-c") == 0) {
    PsDiag d;
    int rc = ps_parse_file_ast(input, &d, stdout);
    if (rc != 0) {
      print_diag(stderr, input, &d);
      return (rc == 2) ? 2 : 1;
    }
    return 0;
  }

  if (strcmp(mode, "--emit-ir-c-json") == 0) {
    PsDiag d;
    int rc = ps_emit_ir_json(input, &d, stdout);
    if (rc != 0) {
      print_diag(stderr, input, &d);
      return (rc == 2) ? 2 : 1;
    }
    return 0;
  }

  if (strcmp(mode, "--check-c") == 0 || strcmp(mode, "--check") == 0) {
    PsDiag d;
    int rc = ps_parse_file_syntax(input, &d);
    if (rc != 0) {
      print_diag(stderr, input, &d);
      return (rc == 2) ? 2 : 1;
    }
    if (strcmp(mode, "--check-c") == 0) return 0;
  }

  return forward_to_reference(argc, argv);
}
