#ifndef SATCHEL_RASTER_COMPILER_H
#define SATCHEL_RASTER_COMPILER_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { SATCHEL_RASTER_OK, SATCHEL_RASTER_INVALID, SATCHEL_RASTER_UNSUPPORTED, SATCHEL_RASTER_LIMIT };
enum { SATCHEL_RASTER_VERTEX=1, SATCHEL_RASTER_FRAGMENT=5 };
/* NVN stage bits are a separate ABI from SPH stage numbers above. */
enum { SATCHEL_RASTER_NVN_VERTEX_BIT=1, SATCHEL_RASTER_NVN_FRAGMENT_BIT=2 };
enum { SATCHEL_RASTER_CODE_BYTES=1024, SATCHEL_RASTER_MAX_OPS=64,
       SATCHEL_RASTER_SPIRV_WORDS=4096, SATCHEL_RASTER_ATTRIBUTES=8,
       SATCHEL_RASTER_STATE_KNOWN=255, SATCHEL_RASTER_STATE_Y_NEGATE=8,
       SATCHEL_RASTER_REQUIRED_STATE=247 };
/* Known bits0..7 correspond to the eight fields below. A cleared bit is
 * UNKNOWN, never an invented default. All facts except YNegate remain required.
 * Only a fully decoded admitted profile can prove YNegate unused; its reflection
 * then carries mask8 (bit3) in unused_state_mask and ignores the unknown field value.
 * Known YNegate must still be boolean. No other state fact is inferred here. */
typedef struct {
    uint32_t known;
    uint32_t single_vertex_program;       /* 1: no preceding VertexA */
    uint32_t viewport_transform_enabled;  /* 1 */
    uint32_t depth_zero_to_one;           /* 1 */
    uint32_t y_negate;                    /* ignored only with decoded unused proof */
    uint32_t origin_upper_left;           /* 1 */
    uint32_t alpha_test_always;           /* 1 */
    uint32_t alpha_to_coverage;           /* 0 */
    uint32_t transform_feedback;          /* 0 */
} SatchelRasterState;
typedef struct {
    uint32_t version,size,stage;
    SatchelRasterState state;
    /* Complete original 80-byte SPH, then separately bounded instructions.
     * Instruction slice starts at a Maxwell scheduling-word boundary.
     * No code/header/native pointer is retained by the compiler. */
    const uint8_t *sph; size_t sph_size;
    const uint8_t *code; size_t code_size;
} SatchelRasterStageInput;
typedef struct {
    uint32_t version,size,stage,spirv_words,semantic_ops;
    uint32_t input_components[SATCHEL_RASTER_ATTRIBUTES]; /* bitmask XYZW, float32 */
    uint32_t position_components; /* vertex requires 15 */
    uint32_t color_components;    /* fragment requires RGBA0 ==15 */
    uint32_t descriptor_count;    /* proved zero */
    uint32_t required_capabilities; /* bit0 Shader, exactly 1 */
    uint32_t unused_memory_mask;  /* 7: local/shared/global proven unused */
    uint32_t interstage_components; /* exactly zero for initial profile */
    uint32_t unused_state_mask; /* derived only after full decode: mask8 (bit3) YNegate */
} SatchelRasterReflection;
typedef struct {
    SatchelRasterReflection reflection;
    uint32_t words[SATCHEL_RASTER_SPIRV_WORDS];
} SatchelRasterModule;
/* State-independent source admission for ProgramSetShaders. Stage is the SPH
 * enum1/5, NOT an NVN stage ordinal or bitmask. No actual graphics state is
 * passed or invented here. Requirements must later be matched against recorded
 * actual state; copying requirements as if they were observed state is invalid. */
typedef struct {
    uint32_t version,size,stage;
    const uint8_t *sph; size_t sph_size;
    const uint8_t *code; size_t code_size;
} SatchelRasterCodeInput;
typedef struct {
    SatchelRasterReflection reflection; /* spirv_words==0: nothing emitted yet */
    SatchelRasterState state_requirements; /* required known bits247; constraints, NEVER actual state */
    uint32_t nvn_stage_bit; /* vertex1, fragment2; not SPH enum */
} SatchelRasterAdmission;
/* Validates actual complete code/SPH; no resources/pipeline/default state.
 * Failure leaves output unchanged. Later compile redecodes the same owned
 * immutable bytes and validates actual state before emitting SPIR-V. */
int satchel_raster_admit(const SatchelRasterCodeInput *,SatchelRasterAdmission *);
/* No allocation, callbacks, defaults or source diagnostics. Failure leaves
 * complete output unchanged. The caller owns immutable valid host spans.
 * Successful translation proves this bounded instruction profile, not a
 * pipeline, draw, game frame, or device feature/vertex-buffer validation. */
int satchel_raster_compile(const SatchelRasterStageInput *,SatchelRasterModule *);
/* Transactional paired translation: validates vertex/fragment state agreement
 * and the complete interface before changing either distinct output. */
int satchel_raster_compile_pair(const SatchelRasterStageInput *vertex,
                           const SatchelRasterStageInput *fragment,
                           SatchelRasterModule *vertex_output,
                           SatchelRasterModule *fragment_output);
#ifdef __cplusplus
}
#endif
#endif
