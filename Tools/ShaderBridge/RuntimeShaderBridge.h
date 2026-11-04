#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
// Install a SatchelShaderPublicationService from Swift before using this ABI.
// Prepare consumes borrowed valid bytes synchronously. Key is exactly 16 UTF-8
// hexadecimal bytes. Flags: bit 0 mapping-only tile fetch, bit 1 dual source.
// Success returns NVN stage 0/1/2/5. Negative statuses are fixed and contain no code.
int32_t satchel_shader_runtime_prepare(const void *control, size_t control_size,
    const void *code, size_t code_size, const char *key, size_t key_size,
    const void *facts_json, size_t facts_size, uint32_t pixel_locations, uint32_t flags);
// Preferred for rendering: returns this exact request's retained snapshot in
// one call. It cannot race another request's publication for the same key.
// On failure *snapshot is NULL. Release successful snapshots exactly once.
int32_t satchel_shader_runtime_prepare_snapshot(const void *control, size_t control_size,
    const void *code, size_t code_size, const char *key, size_t key_size,
    const void *facts_json, size_t facts_size, uint32_t pixel_locations, uint32_t flags,
    void **snapshot);
// A retained immutable publication snapshot. Release exactly once. It keeps the
// service alive through reconfiguration. Directory, identity and library calls
// all use this snapshot, never a second mutable key lookup. This key lookup
// returns the latest publication; use prepare_snapshot for an exact request.
void *satchel_shader_runtime_snapshot(const char *key, size_t key_size);
void satchel_shader_runtime_release_snapshot(void *snapshot);
int64_t satchel_shader_runtime_directory(void *snapshot, char *output, size_t capacity);
int64_t satchel_shader_runtime_identity(void *snapshot, char *output, size_t capacity);
uint64_t satchel_shader_runtime_revision(void *snapshot);
// Returns a retained Objective-C id<MTLLibrary>, or NULL. Transfer ownership to
// ARC with CFBridgingRelease. The caller never frees this with free().
void *satchel_shader_runtime_library(void *snapshot, const char *variant, size_t variant_size);
#ifdef __cplusplus
}
#endif
