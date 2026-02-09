#ifndef PS_PREPROCESS_H
#define PS_PREPROCESS_H

#include <stddef.h>

typedef struct {
  int enabled;
  char *tool;
  char **options;
  size_t option_len;
} PreprocessConfig;

void preprocess_config_init(PreprocessConfig *cfg);
void preprocess_config_free(PreprocessConfig *cfg);

int preprocess_source(
    const char *input,
    size_t input_len,
    char **output,
    size_t *output_len,
    const PreprocessConfig *config,
    const char *input_name,
    char **out_error
);

#endif
