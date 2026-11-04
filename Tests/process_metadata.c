#include "ProcessMetadata.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { ACI = 0x80, ACI_SIZE = 0x4c, ACID = 0xd0, ACID_SIZE = 0x24c, FILE_SIZE = ACID + ACID_SIZE };
static const uint64_t program_id = UINT64_C(0x0100000000001000);
static unsigned checks;
static void check(bool value) { assert(value); checks++; }
static void put32(uint8_t *p, uint32_t n) {
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(n >> (8 * i));
}
static void put64(uint8_t *p, uint64_t n) { put32(p, (uint32_t)n); put32(p + 4, (uint32_t)(n >> 32)); }
static void fixture(uint8_t file[FILE_SIZE]) {
    memset(file, 0, FILE_SIZE);
    memcpy(file, "META", 4); file[0xc] = 7; file[0xe] = 44;
    put32(file + 0x14, 2 * 1024 * 1024); put32(file + 0x1c, 1024 * 1024);
    put32(file + 0x70, ACI); put32(file + 0x74, ACI_SIZE);
    put32(file + 0x78, ACID); put32(file + 0x7c, ACID_SIZE);
    memcpy(file + ACI, "ACI0", 4); put64(file + ACI + 0x10, program_id);
    memcpy(file + ACID + 0x200, "ACID", 4); put32(file + ACID + 0x204, ACID_SIZE - 0x100);
    file[ACID + 0x208] = 1; put32(file + ACID + 0x20c, 1);
    put64(file + ACID + 0x210, program_id); put64(file + ACID + 0x218, program_id);
    for (unsigned i = 0; i < 3; i++) {
        put32(file + ACI + 0x20 + i * 8, 0x40 + i * 4); put32(file + ACI + 0x24 + i * 8, 4);
        put32(file + ACID + 0x220 + i * 8, 0x240 + i * 4); put32(file + ACID + 0x224 + i * 8, 4);
    }
    // Descriptor payloads are original synthetic zeros, not permission grants.
}
static void reject(const uint8_t *file, size_t size) {
    SatchelProcessMetadata info, zero = {0}; char error[96];
    memset(&info, 0xa5, sizeof(info));
    memset(error, 0xa5, sizeof(error));
    check(!satchel_npdm_inspect(file, size, &info, error, sizeof(error)));
    check(!memcmp(&info, &zero, sizeof(info)) && error[0] != 0 && memchr(error, 0, sizeof(error)) != NULL);
}
static uint32_t capability(unsigned core_first, unsigned core_last, unsigned priority_first, unsigned priority_last) {
    return 7 | priority_last << 4 | priority_first << 10 | core_first << 16 | core_last << 24;
}
static void capabilities_fixture(uint8_t *file, uint32_t request, uint32_t restriction) {
    fixture(file);
    put32(file + ACI + 0x48, request);
    put32(file + ACID + 0x248, restriction);
}
static void test_capabilities(void) {
    uint8_t file[FILE_SIZE + 4] = {0};
    SatchelProcessMetadata info; char error[256];
    // The restriction may be broader than the supported guest CPU range; only
    // the requested cores become emulated permissions. No host cores involved.
    uint32_t broad = capability(0, 255, 0, 63);
    for (unsigned first_core = 0; first_core < 4; first_core++) {
        for (unsigned last_core = first_core; last_core < 4; last_core++) {
            for (unsigned first_priority = 4; first_priority < 64; first_priority++) {
                for (unsigned last_priority = first_priority; last_priority < 64; last_priority++) {
                    capabilities_fixture(file, capability(first_core, last_core, first_priority, last_priority), broad);
                    file[0xf] = (uint8_t)first_core; file[0xe] = (uint8_t)first_priority;
                    check(satchel_npdm_inspect(file, FILE_SIZE, &info, error, sizeof(error)));
                    uint64_t core_mask = (UINT64_MAX << first_core) & (UINT64_MAX >> (63 - last_core));
                    uint64_t priority_mask = (UINT64_MAX << first_priority) & (UINT64_MAX >> (63 - last_priority));
                    check(info.has_core_priority && info.allowed_core_mask == core_mask && info.allowed_priority_mask == priority_mask);
                }
            }
        }
    }
    // Numeric priority boundaries, not scheduler urgency, determine containment.
    uint32_t request = capability(1, 2, 20, 50), restriction = capability(0, 3, 10, 60);
    capabilities_fixture(file, request, restriction); file[0xf] = 1;
    check(satchel_npdm_inspect(file, FILE_SIZE, &info, error, sizeof(error)) && info.allowed_core_mask == 6);
    const struct { uint32_t request, restriction; } invalid[] = {
        {capability(0, 2, 20, 50), capability(1, 3, 10, 60)},
        {capability(1, 3, 20, 50), capability(0, 2, 10, 60)},
        {capability(1, 2, 9, 50), restriction}, {capability(1, 2, 20, 61), restriction},
        {capability(2, 1, 20, 50), broad}, {capability(1, 2, 50, 20), broad},
        {request, capability(3, 0, 10, 60)}, {request, capability(0, 3, 60, 10)},
        {capability(0, 4, 4, 63), broad}, {capability(0, 255, 4, 63), broad},
        {capability(0, 3, 0, 63), broad}, {capability(0, 3, 3, 63), broad},
        {request, UINT32_MAX}, {request, 0}
    };
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        capabilities_fixture(file, invalid[i].request, invalid[i].restriction); file[0xf] = 1;
        reject(file, FILE_SIZE);
    }
    capabilities_fixture(file, request, restriction); // Default CPU0 is outside1..2.
    reject(file, FILE_SIZE);
    capabilities_fixture(file, request, restriction); file[0xf] = 1; file[0xe] = 51;
    reject(file, FILE_SIZE);
    capabilities_fixture(file, request, restriction); file[0xf] = 1; file[0xe] = 19;
    reject(file, FILE_SIZE);
    // Duplicate requested descriptors reject even if byte-for-byte equal.
    for (unsigned different = 0; different < 2; different++) {
        capabilities_fixture(file, request, restriction); file[0xf] = 1;
        put32(file + 0x74, ACI_SIZE + 4); put32(file + ACI + 0x34, 8);
        put32(file + ACI + 0x4c, different ? capability(0, 3, 4, 63) : request);
        reject(file, FILE_SIZE);
        capabilities_fixture(file, request, restriction); file[0xf] = 1;
        put32(file + 0x7c, ACID_SIZE + 4); put32(file + ACID + 0x204, ACID_SIZE + 4 - 0x100);
        put32(file + ACID + 0x234, 8);
        put32(file + ACID + 0x24c, different ? broad : restriction);
        reject(file, sizeof(file));
    }
    // Padding/other descriptors cannot be mistaken for a core-priority grant.
    capabilities_fixture(file, UINT32_MAX, broad);
    check(satchel_npdm_inspect(file, FILE_SIZE, &info, error, sizeof(error)) && !info.has_core_priority &&
        !info.allowed_core_mask && !info.allowed_priority_mask);
    capabilities_fixture(file, 0xF, UINT32_MAX);
    check(satchel_npdm_inspect(file, FILE_SIZE, &info, error, sizeof(error)) && !info.has_core_priority &&
        !info.allowed_core_mask && !info.allowed_priority_mask);
}

int main(void) {
    uint8_t file[FILE_SIZE], original[FILE_SIZE]; SatchelProcessMetadata info; char error[256];
    test_capabilities();
    fixture(file); memcpy(original, file, sizeof(file));
    check(satchel_npdm_inspect(file, sizeof(file), &info, error, sizeof(error)));
    check(info.program_id == program_id && info.is_64_bit && info.address_space_type == 3 &&
          info.address_space_bits == 39 && info.address_space_size == (UINT64_C(1) << 39));
    check(info.main_thread_stack_size == 1024 * 1024 && info.system_resource_size == 2 * 1024 * 1024 &&
          info.main_thread_priority == 44 && info.default_cpu_id == 0 && info.flags0 == 7 && info.flags1 == 0);
    check(!info.has_core_priority && !info.allowed_core_mask && !info.allowed_priority_mask);
    const unsigned bits[] = {32, 36, 32, 39, 42};
    for (unsigned i = 0; i < 5; i++) {
        file[0xc] = (uint8_t)(1 | i << 1);
        check(satchel_npdm_inspect(file, sizeof(file), &info, error, sizeof(error)) && info.address_space_bits == bits[i]);
    }
    file[0xc] = 0;
    check(satchel_npdm_inspect(file, sizeof(file), &info, error, sizeof(error)) && !info.is_64_bit);
    fixture(file); file[0xd] = 1;
    check(satchel_npdm_inspect(file, sizeof(file), &info, error, sizeof(error)) && info.flags1 == 1);
    fixture(file); file[0xc] = 0xf7;
    check(satchel_npdm_inspect(file, sizeof(file), &info, error, sizeof(error)) && info.flags0 == 0xf7);
    fixture(file); put32(file + 0x1c, SATCHEL_PROCESS_STACK_LIMIT);
    check(satchel_npdm_inspect(file, sizeof(file), &info, error, sizeof(error)));
    fixture(file);

    // Every truncated prefix must fail before an out-of-bounds field access.
    for (size_t size = 0; size < sizeof(file); size++) reject(file, size);
    reject(NULL, sizeof(file)); reject(file, SATCHEL_NPDM_FILE_LIMIT + 1);
    check(!satchel_npdm_inspect(file, sizeof(file), NULL, error, sizeof(error)));
    check(!satchel_npdm_inspect(NULL, 0, &info, NULL, 0));
    char one[1] = {1};
    check(!satchel_npdm_inspect(NULL, 0, &info, one, sizeof(one)) && one[0] == 0);

    const struct { size_t offset; uint32_t value; } malformed[] = {
        {0, 0}, {0x70, UINT32_MAX}, {0x74, 0x3f}, {0x78, 0x10}, {0x7c, 0x23f},
        {0x78, ACI + 4}, {0x7c, UINT32_MAX}, {ACI, 0}, {ACID + 0x200, 0},
        {ACID + 0x204, 0x13f}, {ACID + 0x204, UINT32_MAX},
        {ACI + 0x20, 0x3f}, {ACI + 0x24, UINT32_MAX}, {ACI + 0x28, 0x40},
        {ACI + 0x30, UINT32_MAX}, {ACI + 0x34, 3},
        {ACID + 0x220, 0x23f}, {ACID + 0x224, UINT32_MAX}, {ACID + 0x228, 0x240},
        {ACID + 0x230, UINT32_MAX}, {ACID + 0x234, 3},
        {0x1c, 0}, {0x1c, 1}, {0x1c, SATCHEL_PROCESS_STACK_LIMIT + 4096},
        {0x14, 4096}, {0x14, 0x20000000}
    };
    for (unsigned i = 0; i < sizeof(malformed) / sizeof(malformed[0]); i++) {
        memcpy(file, original, sizeof(file)); put32(file + malformed[i].offset, malformed[i].value); reject(file, sizeof(file));
    }
    fixture(file); put64(file + ACI + 0x10, 0); reject(file, sizeof(file));
    fixture(file); put64(file + ACI + 0x10, program_id + 1); reject(file, sizeof(file));
    fixture(file); put64(file + ACID + 0x210, program_id + 2); reject(file, sizeof(file));
    fixture(file); file[0xc] = 1 | (5 << 1); reject(file, sizeof(file));
    fixture(file); file[0xd] = 2; reject(file, sizeof(file));
    fixture(file); file[0xe] = 64; reject(file, sizeof(file));
    fixture(file); file[0xf] = 4; reject(file, sizeof(file));

    // Empty capability tables remain bounded at the end of their owner; no
    // arbitrary service or kernel permissions are inferred from empty content.
    fixture(file);
    for (unsigned i = 0; i < 3; i++) {
        put32(file + ACI + 0x20 + i * 8, ACI_SIZE); put32(file + ACI + 0x24 + i * 8, 0);
        put32(file + ACID + 0x220 + i * 8, ACID_SIZE); put32(file + ACID + 0x224 + i * 8, 0);
    }
    check(satchel_npdm_inspect(file, sizeof(file), &info, error, sizeof(error)));

    uint32_t random = 0x76813a9b;
    for (unsigned i = 0; i < 2000; i++) {
        memcpy(file, original, sizeof(file));
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        file[random % sizeof(file)] ^= (uint8_t)(1 + random % 255);
        if (satchel_npdm_inspect(file, sizeof(file), &info, error, sizeof(error))) {
            assert(info.address_space_bits <= 42 && info.main_thread_stack_size <= SATCHEL_PROCESS_STACK_LIMIT);
            assert(info.program_id == program_id && info.main_thread_priority < 64 && info.default_cpu_id < 4);
        }
    }
    printf("Process metadata: %u synthetic assertions and 2000 sanitized mutations passed. No game data or signature claims.\n", checks);
    return 0;
}
