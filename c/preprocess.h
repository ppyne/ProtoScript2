#ifndef PS_PREPROCESS_H
#define PS_PREPROCESS_H

#include <stddef.h>

typedef struct {
  int enabled;
  char *tool;
  char **options;
  size_t option_len;
} PreprocessConfig;

typedef struct {
  size_t len;
  size_t cap;
  const char **files;
  int *lines;
  char **owned_files;
  size_t owned_len;
  size_t owned_cap;
} PreprocessLineMap;

void preprocess_config_init(PreprocessConfig *cfg);
void preprocess_config_free(PreprocessConfig *cfg);
void preprocess_line_map_free(PreprocessLineMap *map);

int preprocess_source(
    const char *input,
    size_t input_len,
    char **output,
    size_t *output_len,
    const PreprocessConfig *config,
    const char *input_name,
    PreprocessLineMap *out_map,
    char **out_error
);

#endif
