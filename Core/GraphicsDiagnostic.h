#ifndef SATCHEL_GRAPHICS_DIAGNOSTIC_H
#define SATCHEL_GRAPHICS_DIAGNOSTIC_H
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    uint32_t status, dynamic_code_features;
    uint32_t maxwell_spirv_bytes, maxwell_msl_bytes, store_msl_bytes;
    bool maxwell_completed, buffer_value_matched, guard_words_matched;
} SatchelGraphicsDiagnostic;
/* Runs only two original synthetic fixtures, never shaders from an imported
 * game. Call off the UI thread. Status 0 means both GPU commands completed,
 * the known buffer result matched, and dynamic CPU code generation is disabled.
 * This is compiler/Metal validation, not a guest NVN renderer. */
bool satchel_graphics_diagnostic(SatchelGraphicsDiagnostic *result);
#endif
