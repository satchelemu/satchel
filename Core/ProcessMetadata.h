#ifndef SATCHEL_PROCESS_METADATA_H
#define SATCHEL_PROCESS_METADATA_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
    SATCHEL_NPDM_FILE_LIMIT = 1024 * 1024,
    SATCHEL_PROCESS_STACK_LIMIT = 64 * 1024 * 1024
};

typedef struct {
    uint64_t program_id, address_space_size;
    uint32_t main_thread_stack_size, system_resource_size;
    uint8_t address_space_bits, address_space_type, main_thread_priority;
    uint8_t default_cpu_id, flags0, flags1;
    bool is_64_bit;
    /* Requested core/priority ranges, structurally checked against ACID. These
     * are emulated process limits, not host permissions or signature evidence.
     * Absent capability: flag false and both masks zero; never infer defaults. */
    uint64_t allowed_core_mask, allowed_priority_mask;
    bool has_core_priority;
} SatchelProcessMetadata;

/* Bounded structural metadata only, not ACID signature verification or access
 * control authorization. The caller must supply authenticated ExeFS bytes, match
 * program_id to the selected NCA, and reject unsupported execution features.
 * Known 32/36/39/42-bit address-space layouts are reported, not emulated here.
 * Only core/priority KAC descriptors are decoded; other capabilities are not
 * validated or granted. Duplicate core/priority descriptors are rejected in
 * either owner. Output is cleared on failure; errors contain no file contents. */
bool satchel_npdm_inspect(const uint8_t *bytes, size_t size, SatchelProcessMetadata *info,
                     char *error, size_t capacity);
#endif
