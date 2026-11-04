#ifndef SATCHEL_SHADER_BRIDGE_H
#define SATCHEL_SHADER_BRIDGE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

enum { SATCHEL_SHADER_MAX_SPIRV_BYTES = 256 * 1024, SATCHEL_SHADER_MAX_MSL_BYTES = 2 * 1024 * 1024,
       SATCHEL_SHADER_MAX_BINDINGS = 24 };
typedef enum { SATCHEL_SHADER_OK = 0, SATCHEL_SHADER_INVALID = 1, SATCHEL_SHADER_UNSUPPORTED = 2,
               SATCHEL_SHADER_CAPACITY = 3, SATCHEL_SHADER_COMPILER_ERROR = 4 } SatchelShaderStatus;
typedef enum { SATCHEL_SHADER_VERTEX = 0, SATCHEL_SHADER_FRAGMENT = 1, SATCHEL_SHADER_COMPUTE = 2 } SatchelShaderStage;
typedef enum { SATCHEL_SHADER_BUFFER = 0, SATCHEL_SHADER_TEXTURE = 1, SATCHEL_SHADER_SAMPLER = 2 } SatchelShaderResourceKind;
typedef struct { uint32_t kind, binding, active; /* active is 0 or 1: statically accessed by the entry point. */ } SatchelShaderBinding;
typedef struct {
    uint32_t stage, local_size[3], binding_count;
    SatchelShaderBinding bindings[SATCHEL_SHADER_MAX_BINDINGS];
    char entry_point[64];
    size_t source_bytes; /* UTF-8 bytes excluding the terminating NUL. */
} SatchelShaderInfo;

/* Convert one translator-produced SPIR-V entry point to iOS MSL 2.3.
 * Caller supplies output storage; no source, input payload, or compiler error
 * text is logged. After validating nonnull, disjoint output buffers, info and output[0]
 * are cleared before translation; invalid pointer/size/overlap arguments are
 * rejected without writing caller memory. All lengths
 * include readable/writable buffers owned by the caller; pointers must not
 * overlap. Descriptor set zero only, explicit bindings 0..23; no push constants,
 * combined samplers, descriptor arrays, specialization constants, implicit
 * auxiliary Metal buffers, or geometry/tessellation.
 * Bounds and framing checks are not complete semantic SPIR-V validation or a
 * compiler sandbox. Do not feed arbitrary imported bytes directly to this API.
 * Use the reviewed translator output, off the UI thread. */
SatchelShaderStatus satchel_shader_spirv_to_msl(const void *spirv, size_t spirv_bytes,
    char *source, size_t source_capacity, SatchelShaderInfo *info);
#ifdef __cplusplus
}
#endif
#endif
