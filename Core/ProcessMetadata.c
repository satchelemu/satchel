#include "ProcessMetadata.h"
#include <stdio.h>
#include <string.h>

/* Independently implemented file layout reader; no reference code copied.
 * ABI evidence: Atmosphere-NX/Atmosphere commit
 * 6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8,
 * libraries/libstratosphere/include/stratosphere/ldr/ldr_types.hpp:157-277;
 * stratosphere/loader/source/ldr_meta.cpp:78-136.
 * https://github.com/Atmosphere-NX/Atmosphere/tree/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8
 * The reference is GPL-2.0; only documented structure/ABI facts are used here.
 */
static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static uint64_t u64(const uint8_t *p) {
    return (uint64_t)u32(p) | (uint64_t)u32(p + 4) << 32;
}
static bool reject(char *error, size_t capacity, const char *message) {
    if (error && capacity) snprintf(error, capacity, "%s", message);
    return false;
}
static bool range(size_t offset, size_t length, size_t start, size_t end) {
    return offset >= start && offset <= end && length <= end - offset;
}
static bool overlap(size_t a, size_t an, size_t b, size_t bn) {
    /* All callers have already bounded these ranges within the 1-MiB file. */
    return an && bn && a < b + bn && b < a + an;
}
static bool children(const uint8_t *header, size_t fields, size_t minimum, size_t size) {
    size_t offsets[3], lengths[3];
    for (unsigned i = 0; i < 3; i++) {
        offsets[i] = u32(header + fields + i * 8);
        lengths[i] = u32(header + fields + i * 8 + 4);
        if (!range(offsets[i], lengths[i], minimum, size)) return false;
        for (unsigned j = 0; j < i; j++) {
            if (overlap(offsets[i], lengths[i], offsets[j], lengths[j])) return false;
        }
    }
    /* Kernel capabilities are arrays of 32-bit descriptors. */
    return lengths[2] % 4 == 0;
}

typedef struct {
    bool present;
    unsigned first_core, last_core, first_priority, last_priority;
} CorePriority;

/* Published capability id3: low nibble 0111; numeric priority maximum at
 * bits4..9, numeric minimum at10..15, core minimum16..23, maximum24..31.
 * Primary ABI facts only, independently implemented: Atmosphere pin above,
 * kern_k_capabilities.hpp CorePriority, kern_k_capabilities.cpp
 * SetCorePriorityCapability, loader/ldr_capabilities.cpp KernelFlags::IsValid.
 * The loader uses inverted-sounding priority field names; numerically the
 * requested interval must be contained in the restriction interval. */
static bool core_priority(const uint8_t *bytes, size_t size, CorePriority *out) {
    memset(out, 0, sizeof(*out));
    for (size_t offset = 0; offset < size; offset += 4) {
        uint32_t value = u32(bytes + offset);
        if ((value & 15) != 7) continue; // Other descriptors grant nothing here.
        if (out->present) return false; // No ambiguous first/last-wins policy.
        out->present = true;
        out->last_priority = (value >> 4) & 63;
        out->first_priority = (value >> 10) & 63;
        out->first_core = (value >> 16) & 255;
        out->last_core = value >> 24;
        if (out->first_priority > out->last_priority || out->first_core > out->last_core) return false;
    }
    return true;
}

bool satchel_npdm_inspect(const uint8_t *bytes, size_t size, SatchelProcessMetadata *info,
                     char *error, size_t capacity) {
    if (info) memset(info, 0, sizeof(*info));
    if (error && capacity) error[0] = 0;
    if (!bytes || !info || size < 0x80 || size > SATCHEL_NPDM_FILE_LIMIT)
        return reject(error, capacity, "NPDM length is outside the bounded metadata limits.");
    if (memcmp(bytes, "META", 4))
        return reject(error, capacity, "The process metadata has no META header.");

    uint32_t aci_offset = u32(bytes + 0x70), aci_size = u32(bytes + 0x74);
    uint32_t acid_offset = u32(bytes + 0x78), acid_size = u32(bytes + 0x7c);
    if (aci_size < 0x40 || acid_size < 0x240 ||
        !range(aci_offset, aci_size, 0x80, size) || !range(acid_offset, acid_size, 0x80, size) ||
        overlap(aci_offset, aci_size, acid_offset, acid_size))
        return reject(error, capacity, "NPDM ACI/ACID regions are truncated, overlapping, or out of bounds.");
    const uint8_t *aci = bytes + aci_offset, *acid = bytes + acid_offset;
    if (memcmp(aci, "ACI0", 4) || memcmp(acid + 0x200, "ACID", 4))
        return reject(error, capacity, "NPDM access-control headers are invalid.");

    uint32_t signed_size = u32(acid + 0x204);
    if (signed_size < 0x140 || signed_size > acid_size - 0x100)
        return reject(error, capacity, "The ACID signed-message extent is invalid.");
    /* Structure is checked against the declared signed extent. The signature
     * itself and requested service/kernel permissions are NOT verified/granted. */
    if (!children(aci, 0x20, 0x40, aci_size) || !children(acid, 0x220, 0x240, 0x100 + signed_size))
        return reject(error, capacity, "NPDM access-control subregions are malformed or overlap.");

    uint64_t program_id = u64(aci + 0x10), minimum_id = u64(acid + 0x210), maximum_id = u64(acid + 0x218);
    if (!program_id || minimum_id > maximum_id || program_id < minimum_id || program_id > maximum_id)
        return reject(error, capacity, "The NPDM program ID is outside its declared ACID range.");
    uint8_t flags0 = bytes[0xc], flags1 = bytes[0xd], type = (flags0 >> 1) & 7;
    unsigned bits;
    switch (type) {
        case 0: case 2: bits = 32; break;
        case 1: bits = 36; break;
        case 3: bits = 39; break;
        case 4: bits = 42; break;
        default: return reject(error, capacity, "The NPDM address-space type is unsupported.");
    }
    if (flags1 & ~1u)
        return reject(error, capacity, "The NPDM secondary flags are unsupported.");
    uint32_t stack_size = u32(bytes + 0x1c);
    if (!stack_size || stack_size % 4096 || stack_size > SATCHEL_PROCESS_STACK_LIMIT)
        return reject(error, capacity, "The requested main-thread stack must be page aligned and at most 64 MiB.");
    if (bytes[0xe] >= 64 || bytes[0xf] >= 4)
        return reject(error, capacity, "The requested thread priority or CPU is outside the Switch process limits.");
    uint32_t system_resource = u32(bytes + 0x14);
    if (system_resource % (2 * 1024 * 1024) || system_resource > 0x1fe00000)
        return reject(error, capacity, "The NPDM system-resource allocation is invalid.");

    CorePriority requested, restriction;
    if (!core_priority(aci + u32(aci + 0x30), u32(aci + 0x34), &requested) ||
        !core_priority(acid + u32(acid + 0x230), u32(acid + 0x234), &restriction))
        return reject(error, capacity, "NPDM core/priority capabilities are malformed or duplicated.");
    if (requested.present && (!restriction.present || requested.first_core < restriction.first_core ||
            requested.last_core > restriction.last_core || requested.first_priority < restriction.first_priority ||
            requested.last_priority > restriction.last_priority || requested.last_core >= 4 ||
            requested.first_priority < 4 || bytes[0xf] < requested.first_core || bytes[0xf] > requested.last_core ||
            bytes[0xe] < requested.first_priority || bytes[0xe] > requested.last_priority))
        return reject(error, capacity, "NPDM core/priority request exceeds its restriction or supported thread limits.");

    SatchelProcessMetadata output = {0};
    output.program_id = program_id;
    output.address_space_size = UINT64_C(1) << bits;
    output.main_thread_stack_size = stack_size;
    output.system_resource_size = system_resource;
    output.address_space_bits = (uint8_t)bits;
    output.address_space_type = type;
    output.main_thread_priority = bytes[0xe];
    output.default_cpu_id = bytes[0xf];
    output.flags0 = flags0; output.flags1 = flags1;
    output.is_64_bit = (flags0 & 1) != 0;
    output.has_core_priority = requested.present;
    if (requested.present) {
        for (unsigned core = requested.first_core; core <= requested.last_core; core++)
            output.allowed_core_mask |= UINT64_C(1) << core;
        for (unsigned priority = requested.first_priority; priority <= requested.last_priority; priority++)
            output.allowed_priority_mask |= UINT64_C(1) << priority;
    }
    *info = output;
    return true;
}
