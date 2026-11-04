#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void *satchel_zstd_create(const void *dictionary, size_t size, int window_log_limit);
void satchel_zstd_free(void *context);
uint32_t satchel_zstd_dictionary_id(const void *header, size_t size);
// Sizes are available bytes on input, consumed/produced bytes on output.
// Return 0 at frame end, 1 for more data, negative on failure.
int satchel_zstd_step(void *context, const void *input, size_t *input_size, void *output, size_t *output_size);
#ifdef __cplusplus
}
#endif
