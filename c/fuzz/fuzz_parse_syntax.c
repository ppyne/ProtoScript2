#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../frontend.h"

static int g_fd = -1;
static char g_path[128];
static int g_init = 0;

static void cleanup_temp_file(void) {
  if (g_fd >= 0) {
    close(g_fd);
    g_fd = -1;
  }
  if (g_path[0] != '\0') {
    unlink(g_path);
    g_path[0] = '\0';
  }
}

static int ensure_temp_file(void) {
  if (g_fd >= 0) return 1;
  char tmpl[] = "/tmp/ps_fuzz_parse_XXXXXX.pts";
  g_fd = mkstemps(tmpl, 4);
  if (g_fd < 0) return 0;
  snprintf(g_path, sizeof(g_path), "%s", tmpl);
  return 1;
}

static int write_input_file(const uint8_t *data, size_t size) {
  if (!ensure_temp_file()) return 0;
  if (ftruncate(g_fd, 0) != 0) return 0;
  if (lseek(g_fd, 0, SEEK_SET) < 0) return 0;
  if (size > 0) {
    size_t off = 0;
    while (off < size) {
      ssize_t n = write(g_fd, data + off, size - off);
      if (n <= 0) return 0;
      off += (size_t)n;
    }
  }
  return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (!data) return 0;
  if (size > (1u << 20)) return 0;
  if (!g_init) {
    (void)atexit(cleanup_temp_file);
    g_init = 1;
  }

  if (!write_input_file(data, size)) return 0;

  PsDiag diag;
  memset(&diag, 0, sizeof(diag));
  (void)ps_parse_file_syntax(g_path, &diag);
  return 0;
}
