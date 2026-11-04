// Simulator UI fixture only. Container import needs no decompression. Refuse any
// attempt to decode NCZ rather than pretending a compressed payload was validated.
#include "SwitchCompression.h"
void *satchel_zstd_create(const void *dictionary, size_t size, int window_log_limit) { return NULL; }
void satchel_zstd_free(void *context) {}
uint32_t satchel_zstd_dictionary_id(const void *header, size_t size) { return 0; }
int satchel_zstd_step(void *context, const void *input, size_t *input_size, void *output, size_t *output_size) { return -1; }
