#ifndef SATCHEL_SHADER_PROBE_H
#define SATCHEL_SHADER_PROBE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Scratch feasibility probe, resource-free compute shader only.
 * Positive result is SPIR-V byte count. -1 invalid arguments, -2 capacity,
 * -3 unsupported shader/translation error. Caller owns both buffers.
 * Size must be 32..65536 and multiple of 8; no partial output on error.
 * Do not use with arbitrary game shaders: no GPU descriptor snapshot yet. */
int satchel_shader_probe(const uint8_t *code, int size, uint8_t *output, int capacity);
/* Bit 0 dynamic code supported, bit 1 dynamic code compiled; AOT expects 0. */
int satchel_shader_runtime_features(void);
#ifdef __cplusplus
}
#endif
#endif
