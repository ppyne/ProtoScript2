#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../frontend.h"

static int write_input_file(const uint8_t *data, size_t size, char *out_path, size_t out_path_sz) {
  char tmpl[] = "/tmp/ps_fuzz_static_XXXXXX.pts";
  int fd = mkstemps(tmpl, 4);
  if (fd < 0) return 0;
  FILE *f = fdopen(fd, "wb");
  if (!f) {
    close(fd);
    unlink(tmpl);
    return 0;
  }
  if (size > 0) fwrite(data, 1, size, f);
  fclose(f);
  snprintf(out_path, out_path_sz, "%s", tmpl);
  return 1;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  if (!data) return 0;
  if (size > (1u << 20)) return 0;

  char path[128];
  if (!write_input_file(data, size, path, sizeof(path))) return 0;

  PsDiag diag;
  memset(&diag, 0, sizeof(diag));
  (void)ps_check_file_static(path, &diag);
  unlink(path);
  return 0;
}
