#pragma once
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Immutable job JSON uses the pinned dmsh_xlate contract, with code supplied
   separately. Output is four little-endian int32 values (version=1, SPIR-V
   bytes, resource JSON bytes, reader SPIR-V bytes), followed by those payloads.
   Resources and approximation notes are private game data, never log them.
   Caller owns output until satchel_shader_translate_free. Status: 0 success,
   -1 invalid request, -2 capacity, -3 translation failure. */
int satchel_shader_translate_v1(const uint8_t *code, int code_size,
    const uint8_t *job, int job_size, uint8_t **output, int *output_size);
void satchel_shader_translate_free(void *output);
#ifdef __cplusplus
}
#endif
