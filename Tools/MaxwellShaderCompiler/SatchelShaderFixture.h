#ifndef SATCHEL_SHADER_FIXTURE_H
#define SATCHEL_SHADER_FIXTURE_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Original fixed compute fixture only, NOT a game shader compiler. All native
 * pointers must refer to valid caller-owned storage for the duration of the call.
 * Only the independently encoded 64-byte fixture in fixture_compute.c is admitted.
 * State and code are copied before translation; no pointers are retained.
 * The existing SatchelShaderProbe API and canonical framework are unchanged. */
typedef struct {
    uint32_t version, struct_size;
    const uint8_t *code;
    const uint8_t *constant_buffer;
    uint32_t code_size, constant_buffer_size;
    uint32_t local_size_x, local_size_y, local_size_z;
    uint32_t local_memory_size, shared_memory_size;
    uint32_t constant_buffer_slot, uniform_binding, storage_binding;
    uint32_t storage_size, storage_offset_alignment;
    uint64_t storage_gpu_address;
    uint32_t reserved[4];
} SatchelShaderFixtureInput;
typedef struct {
    uint32_t kind, set, binding, slot;
    uint32_t address_cb_slot, address_cb_byte_offset, flags, required_bytes;
} SatchelShaderFixtureBuffer;
enum { SATCHEL_SHADER_FIXTURE_UNIFORM=1, SATCHEL_SHADER_FIXTURE_STORAGE=2 };
enum { SATCHEL_SHADER_FIXTURE_ACTIVE=1, SATCHEL_SHADER_FIXTURE_WRITE=2 };
typedef struct {
    uint32_t version, struct_size, spirv_size, buffer_count;
    uint32_t active_buffer_count, local_size_x, local_size_y, local_size_z;
    uint32_t required_capability, reserved[3];
    SatchelShaderFixtureBuffer buffers[3];
} SatchelShaderFixtureReflection;
enum {
    SATCHEL_SHADER_FIXTURE_OK=0, SATCHEL_SHADER_FIXTURE_INVALID=1,
    SATCHEL_SHADER_FIXTURE_CAPACITY=2, SATCHEL_SHADER_FIXTURE_UNSUPPORTED=3,
    SATCHEL_SHADER_FIXTURE_BUSY=4
};
/* Atomic outputs on failure. Output and reflection destinations must not overlap.
 * Fixed state: local size 1^3, no local/shared memory, CB3 of 32 bytes containing
 * [addressLow,addressHigh,storageSize,inputA,inputB,0,0,0]. Storage is 16..4096
 * bytes with an aligned 40-bit address. Bindings are distinct 1..7 (0 reserved).
 * The single invocation stores uint32(inputA+inputB) at storage byte offset 0.
 * Reflection distinguishes the declared-but-unused support UBO from active
 * resources. This function does not create/execute a Vulkan pipeline. */
int satchel_shader_fixture_compile(const SatchelShaderFixtureInput *, uint8_t *output,
    uint32_t capacity, SatchelShaderFixtureReflection *reflection);
#ifdef SATCHEL_SHADER_FIXTURE_TESTING
/* Host test-only entry point. Never built into the staged iOS framework. */
int satchel_shader_fixture_test_raw(const SatchelShaderFixtureInput *, uint8_t *, uint32_t);
int satchel_shader_fixture_test_normalize(const SatchelShaderFixtureInput *, const uint8_t *, uint32_t,
    uint8_t *, uint32_t, SatchelShaderFixtureReflection *);
int satchel_shader_fixture_test_log(void);
#endif
#ifdef __cplusplus
}
#endif
#endif
