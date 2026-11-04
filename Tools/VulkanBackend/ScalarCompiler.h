#ifndef SATCHEL_SCALAR_COMPILER_H
#define SATCHEL_SCALAR_COMPILER_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Experimental bounded scalar compute profile. Caller spans must be valid,
 * immutable for this synchronous call. No callbacks, allocation, game logging,
 * translator defaults, or retained native pointers. Unknown metadata rejects. */
enum { SATCHEL_SCALAR_OK, SATCHEL_SCALAR_INVALID, SATCHEL_SCALAR_UNSUPPORTED, SATCHEL_SCALAR_LIMIT };
enum { SATCHEL_SCALAR_KNOWN_WORKGROUP=1, SATCHEL_SCALAR_KNOWN_BINDINGS=2, SATCHEL_SCALAR_KNOWN_CAPS=4,
       SATCHEL_SCALAR_KNOWN_LOCAL=8, SATCHEL_SCALAR_KNOWN_SHARED=16 };
enum { SATCHEL_SCALAR_CODE_BYTES=512, SATCHEL_SCALAR_MAX_OPS=32, SATCHEL_SCALAR_MAX_STORES=16,
       SATCHEL_SCALAR_SPIRV_WORDS=4096 };
typedef struct {
    uint32_t version, size, known;
    uint32_t local_x,local_y,local_z,local_bytes,shared_bytes;
    uint32_t group_x,group_y,group_z;
    uint32_t shader_enabled,max_uniform_range,max_storage_range;
    uint32_t max_uniform_descriptors,max_storage_descriptors,max_stage_resources;
    uint32_t uniform_alignment,storage_alignment;
    uint32_t cb_slot,uniform_binding,storage_binding;
    uint32_t storage_cb_disjoint; /* trusted owner verified physical disjointness */
    const uint8_t *code;size_t code_size;
    const uint8_t *constant_buffer;size_t constant_buffer_size;
    uint64_t storage_address;uint32_t storage_size;
} SatchelScalarInput;
typedef struct {
    uint32_t offset,size;
} SatchelScalarWrite;
typedef struct {
    uint32_t version,size,spirv_words,semantic_ops;
    uint32_t cb_slot,uniform_binding,storage_binding,uniform_declared_bytes,uniform_required_bytes;
    uint32_t storage_required_bytes,pointer_cb_offset,store_count;
    uint64_t expected_storage_address;
    uint32_t unused_memory_mask; /* bit0 local, bit1 shared: proved by complete opcode admission */
    SatchelScalarWrite stores[SATCHEL_SCALAR_MAX_STORES];
} SatchelScalarReflection;
/* Failure preserves both destinations. Output and reflection may not overlap.
 * Reflection's pointer pair must match the same immutable CB/pool snapshot at
 * submission; changing arbitrary operand words is allowed, pointer words are not.
 * Initial profile admits only known1x1x1 dispatch/local dimensions. Local/shared
 * sizes may be unknown because the entire admitted opcode set cannot access them;
 * values are not defaulted or passed to a generic translator. Caller must attest
 * physically disjoint source CB/storage spans (copies alone are not proof).
 * Initial resources are
 * one CB<=64KiB and one aligned storage mapping<=1MiB. All accesses are checked.
 * Output is dynamically generated from decoded program IR, never fixture substitution. */
int satchel_scalar_compile(const SatchelScalarInput *,uint32_t *output,size_t capacity_words,SatchelScalarReflection *);
/* Code admission has no constant-buffer values, addresses, dispatch groups or
 * device defaults. The owner supplies the actual compute instruction slice and
 * copied control metadata; header/C1 parsing and stage identity precede this API.
 * Known must contain WORKGROUP, optionally LOCAL/SHARED only. Sizes may be unknown
 * only because this entire admitted opcode set proves both spaces unused. */
typedef struct {
    uint32_t version,size,known;
    uint32_t local_x,local_y,local_z,local_bytes,shared_bytes;
    const uint8_t *code;size_t code_size;
} SatchelScalarProgramInput;
enum { SATCHEL_SCALAR_IR_OPS=64 };
typedef struct { uint32_t kind,a,b,offset; } SatchelScalarIROp;
typedef struct {
    uint32_t cb_slot,uniform_declared_bytes,uniform_required_bytes;
    uint32_t storage_required_bytes,pointer_cb_offset,store_count,unused_memory_mask;
    SatchelScalarWrite stores[SATCHEL_SCALAR_MAX_STORES];
} SatchelScalarRequirements;
/* Immutable caller-owned bounded IR. No retained pointers. The copied code is
 * private in-memory program state: never log or serialize imported program data.
 * Requirements are deferred obligations, NOT resolved resources or a pipeline.
 * Current emitter needs Shader/Vulkan1.1, one64KiB UBO and oneSSBO; actual device
 * capability admission remains the owner's responsibility before NVNsuccess.
 * compile_program re-decodes and compares every field before trusting this IR. */
typedef struct {
    uint32_t version,size,known;
    uint32_t local_x,local_y,local_z,local_bytes,shared_bytes;
    uint32_t semantic_ops,op_count,code_size;
    SatchelScalarRequirements requirements;
    SatchelScalarIROp ops[SATCHEL_SCALAR_IR_OPS];
    uint8_t code[SATCHEL_SCALAR_CODE_BYTES];
} SatchelScalarProgram;
/* Failure preserves output, including unsupported code/metadata/resource shape.
 * One CB slot is derived from actual LDC instructions, never supplied/defaulted. */
int satchel_scalar_admit(const SatchelScalarProgramInput *,SatchelScalarProgram *);
/* Actual dispatch resources/caps/groups come from dispatch. Its code/local
 * metadata fields are ignored; immutable admitted program supplies those fields.
 * Binding/caps known bits must be present. CBslot must match admission. Live CB
 * extent, pointer value, storage extent/alignment, device limits and physical
 * disjointness remain mandatory. A successful call emits SPIR-V, not a pipeline. */
int satchel_scalar_compile_program(const SatchelScalarProgram *,const SatchelScalarInput *dispatch,
                             uint32_t *output,size_t capacity_words,SatchelScalarReflection *);
#ifdef __cplusplus
}
#endif
#endif
