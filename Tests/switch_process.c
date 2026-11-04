#include "SwitchProcess.h"
#include "SwitchResources.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Two 0x3000-byte modules end at 0x8006000. With ASLR disabled,
// the 39-bit kernel-map reservation begins at 0x8200000 and spans 64 GiB.
static const uint64_t initial_stack39 = UINT64_C(0x1008200000);
#if defined(SATCHEL_SWITCH_RESOURCES_TESTING)
extern void satchel_switch_resources_test_fail_allocation(unsigned ordinal);
#endif
static unsigned checks;
#define CHECK(x) do { assert(x); checks++; } while (0)
static int cancel(void *context) { (void)context; return 1; }

static void metadata_word(uint8_t *p, uint32_t word) {
    for (unsigned i = 0; i < 4; i++) p[i] = (uint8_t)(word >> (8 * i));
}
static void test_capability_queries(void) {
    // Complete original NPDM fixture: request cores1..2/priorities20..50,
    // restricted by ACID cores0..3/priorities4..63. Test parser -> owned process
    // -> public SVC ABI instead of substituting arbitrary constant responses.
    uint8_t npdm[0x314] = {0};
    memcpy(npdm, "META", 4); npdm[0xc] = 7; npdm[0xe] = 37; npdm[0xf] = 1;
    metadata_word(npdm + 0x1c, 0x4000);
    metadata_word(npdm + 0x70, 0x80); metadata_word(npdm + 0x74, 0x44);
    metadata_word(npdm + 0x78, 0xd0); metadata_word(npdm + 0x7c, 0x244);
    uint8_t *aci = npdm + 0x80, *acid = npdm + 0xd0;
    memcpy(aci, "ACI0", 4); metadata_word(aci + 0x10, 1);
    memcpy(acid + 0x200, "ACID", 4); metadata_word(acid + 0x204, 0x144);
    metadata_word(acid + 0x210, 1); metadata_word(acid + 0x218, 1);
    for (unsigned i = 0; i < 3; i++) {
        metadata_word(aci + 0x20 + 8 * i, 0x40);
        metadata_word(acid + 0x220 + 8 * i, 0x240);
    }
    metadata_word(aci + 0x34, 4); metadata_word(acid + 0x234, 4);
    metadata_word(aci + 0x40, 7 | 50 << 4 | 20 << 10 | 1 << 16 | 2 << 24);
    metadata_word(acid + 0x240, 7 | 63 << 4 | 4 << 10 | 3 << 24);
    SatchelProcessMetadata metadata; char error[256];
    CHECK(satchel_npdm_inspect(npdm, sizeof(npdm), &metadata, error, sizeof(error)));
    CHECK(metadata.has_core_priority && metadata.allowed_core_mask == 6 &&
        metadata.allowed_priority_mask == UINT64_C(0x7FFFFFFF00000));
    uint8_t image[0x3000] = {0};
    for (unsigned i = 0; i < 8; i++) {
        const uint32_t svc = 0xd4000521;
        memcpy(image + i * 4, &svc, 4);
    }
    SatchelNSOInfo nso = {.image_size = sizeof(image), .bss_size = 0xFFC,
        .segments = {{0, 32, 0, 32}, {0, 4, 0x1000, 4}, {0, 4, 0x2000, 4}}};
    SatchelSwitchProcessModule modules[] = {{"rtld", nso, image, sizeof(image)}, {"main", nso, image, sizeof(image)}};
    const struct { uint64_t handle, subtype; uint32_t status; } requests[] = {
        {0xffff8001, 0, 0}, {UINT64_C(0x12345678ffff8001), 0, 0},
        {0, 0, 0xE401}, {0xffff8000, 0, 0xE401}, {0x80, 0, 0xE401},
        {0xffff8001, 1, 0xE801}, {0, UINT64_MAX, 0xE801}, {0xffff8001, 0, 0}
    };
    for (unsigned type = 0; type < 2; type++) {
        SatchelSwitchProcess *p = satchel_switch_process_create(modules, 2, &metadata, error, sizeof(error)); CHECK(p);
        SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
        for (unsigned i = 0; i < sizeof(requests) / sizeof(requests[0]); i++) {
            CHECK(satchel_cpu_probe_set_x(cpu, 0, UINT64_MAX));
            CHECK(satchel_cpu_probe_set_x(cpu, 1, UINT64_C(0xBADF00D000000000) | type));
            CHECK(satchel_cpu_probe_set_x(cpu, 2, requests[i].handle));
            CHECK(satchel_cpu_probe_set_x(cpu, 3, requests[i].subtype));
            SatchelCPUProbeResult result = satchel_switch_process_run(p, 1, NULL, NULL);
            CHECK(result.reason == SATCHEL_CPU_BUDGET && result.steps == 1);
            uint64_t value;
            CHECK(satchel_cpu_probe_get_x(cpu, 0, &value) && value == requests[i].status);
            CHECK(satchel_cpu_probe_get_x(cpu, 1, &value) && value == (requests[i].status ? 0 :
                (type ? metadata.allowed_priority_mask : metadata.allowed_core_mask)));
        }
        CHECK(satchel_switch_process_info_serviced(p) == 8);
        satchel_switch_process_destroy(p);
    }
    SatchelProcessMetadata bad = metadata;
    bad.has_core_priority = false; CHECK(!satchel_switch_process_metadata_supported(&bad));
    bad = metadata; bad.allowed_core_mask = 0; CHECK(!satchel_switch_process_metadata_supported(&bad));
    bad = metadata; bad.allowed_core_mask = 0x1F; CHECK(!satchel_switch_process_metadata_supported(&bad));
    bad = metadata; bad.allowed_core_mask = 0xB; CHECK(!satchel_switch_process_metadata_supported(&bad));
    bad = metadata; bad.allowed_core_mask = 4; CHECK(!satchel_switch_process_metadata_supported(&bad));
    bad = metadata; bad.allowed_priority_mask = 0; CHECK(!satchel_switch_process_metadata_supported(&bad));
    bad = metadata; bad.allowed_priority_mask = UINT64_MAX; CHECK(!satchel_switch_process_metadata_supported(&bad));
    bad = metadata; bad.allowed_priority_mask ^= UINT64_C(1) << 30; CHECK(!satchel_switch_process_metadata_supported(&bad));
    bad = metadata; bad.main_thread_priority = 51; CHECK(!satchel_switch_process_metadata_supported(&bad));
    // Absent capability explicitly blocks both calls; it cannot mean all cores.
    metadata.has_core_priority = false; metadata.allowed_core_mask = metadata.allowed_priority_mask = 0;
    for (unsigned type = 0; type < 2; type++) {
        SatchelSwitchProcess *p = satchel_switch_process_create(modules, 2, &metadata, error, sizeof(error)); CHECK(p);
        SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
        CHECK(satchel_cpu_probe_set_x(cpu, 0, 0xAABB)); CHECK(satchel_cpu_probe_set_x(cpu, 1, type));
        CHECK(satchel_cpu_probe_set_x(cpu, 2, 0xffff8001)); CHECK(satchel_cpu_probe_set_x(cpu, 3, 0));
        SatchelCPUProbeResult result = satchel_switch_process_run(p, 1, NULL, NULL);
        CHECK(result.reason == SATCHEL_CPU_SVC && result.svc == 0x29 && result.steps == 1);
        uint64_t value;
        CHECK(satchel_cpu_probe_get_x(cpu, 0, &value) && value == 0xAABB);
        CHECK(satchel_cpu_probe_get_x(cpu, 1, &value) && value == type);
        CHECK(satchel_switch_process_info_requests(p) == 1 && !satchel_switch_process_info_serviced(p));
        satchel_switch_process_destroy(p);
    }
}

static void test_memory_accounting(uint32_t system_reserved, int gaps, int with_dispatch) {
    uint8_t image[0x5000] = {0};
    for (unsigned i = 0; i < 48; i++) {
        const uint32_t svc = 0xd4000521;
        memcpy(image + i * 4, &svc, 4);
    }
    uint32_t image_size = gaps ? 0x5000 : 0x3000;
    SatchelNSOInfo nso = {.image_size = image_size, .bss_size = 0xFFC,
        .segments = {{0, 192, 0, 192}, {0, 4, gaps ? 0x2000 : 0x1000, 4}, {0, 4, gaps ? 0x4000 : 0x2000, 4}}};
    SatchelSwitchProcessModule modules[] = {{"rtld", nso, image, image_size}, {"main", nso, image, image_size}};
    SatchelProcessMetadata metadata = {.program_id = 1, .address_space_size = UINT64_C(1) << 39,
        .main_thread_stack_size = 0x4000, .address_space_bits = 39, .address_space_type = 3,
        .flags0 = 7, .is_64_bit = true, .system_resource_size = system_reserved};
    uint8_t stubs[8192] = {0}; const uint32_t trap = 0xd4200000; memcpy(stubs, &trap, 4);
    SatchelSwitchDispatchCode dispatch = {stubs, sizeof(stubs)};
    char error[256];
    SatchelSwitchProcess *p = satchel_switch_process_create_with_dispatch(modules, 2, &metadata,
        with_dispatch ? &dispatch : NULL, error, sizeof(error)); CHECK(p);
    SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
    uint64_t expected_total = SATCHEL_SWITCH_PROCESS_MEMORY_LIMIT - system_reserved - (gaps ? 0x4000 : 0);
    uint64_t expected_used = 0x6000 + 0x4000 + 0x1000 + (with_dispatch ? 8192 : 0);
    const struct { uint64_t handle, sub; uint32_t result; } requests[] = {
        {0xffff8001, 0, 0}, {UINT64_C(0x12345678ffff8001), 0, 0},
        {0, 0, 0xe401}, {0xffff8000, 0, 0xe401}, {0x80, 0, 0xe401},
        {0xffff8001, 1, 0xe801}, {0, UINT64_MAX, 0xe801}
    };
    uint8_t sentinel = 0xA9;
    CHECK(satchel_cpu_probe_write(cpu, 0x31000200, &sentinel, 1));
    const unsigned types[] = {6, 7, 16, 17, 21, 22};
    for (unsigned t = 0; t < sizeof(types) / sizeof(types[0]); t++) {
        unsigned type = types[t];
        for (unsigned i = 0; i < sizeof(requests) / sizeof(requests[0]); i++) {
            CHECK(satchel_cpu_probe_set_x(cpu, 0, UINT64_MAX));
            CHECK(satchel_cpu_probe_set_x(cpu, 1, UINT64_C(0xBA00000000000000) | type));
            CHECK(satchel_cpu_probe_set_x(cpu, 2, requests[i].handle));
            CHECK(satchel_cpu_probe_set_x(cpu, 3, requests[i].sub));
            SatchelCPUProbeResult result = satchel_switch_process_run(p, 1, NULL, NULL);
            CHECK(result.reason == SATCHEL_CPU_BUDGET && result.steps == 1);
            uint64_t value;
            CHECK(satchel_cpu_probe_get_x(cpu, 0, &value) && value == requests[i].result);
            CHECK(satchel_cpu_probe_get_x(cpu, 1, &value) && value == (requests[i].result ? 0 :
                (type == 6 ? expected_total + system_reserved : (type == 7 ? expected_used + system_reserved :
                    (type == 16 ? system_reserved : (type == 17 ? (system_reserved ? 4096 : 0) :
                    (type == 21 ? expected_total : expected_used)))))));
        }
    }
    CHECK(satchel_switch_process_info_serviced(p) == 42);
    CHECK(satchel_switch_process_system_resource_total(p) == system_reserved);
    CHECK(satchel_switch_process_system_resource_used(p) == (system_reserved ? 4096 : 0));
    CHECK(!satchel_switch_process_system_resource_total(NULL) && !satchel_switch_process_system_resource_used(NULL));
    CHECK(expected_used < expected_total);
    // Neither the resource budget nor an8GiB virtual heap creates guest pages.
    uint64_t heap = UINT64_C(0x2088200000);
    SatchelSwitchMemoryInfo info;
    CHECK(satchel_switch_process_query(p, heap, &info) && info.state == 0 && !info.permissions);
    CHECK(!satchel_cpu_probe_read(cpu, heap, &sentinel, 1));
    CHECK(satchel_cpu_probe_read(cpu, 0x31000200, &sentinel, 1) && sentinel == 0xA9);
    if (gaps) CHECK(!satchel_cpu_probe_read(cpu, 0x8001000, &sentinel, 1));
    satchel_switch_process_destroy(p);
    const unsigned unsupported[] = {8, 9};
    for (unsigned i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); i++) {
        p = satchel_switch_process_create(modules, 2, &metadata, error, sizeof(error)); CHECK(p);
        cpu = satchel_switch_process_cpu(p);
        CHECK(satchel_cpu_probe_set_x(cpu, 0, 0xAA)); CHECK(satchel_cpu_probe_set_x(cpu, 1, unsupported[i]));
        CHECK(satchel_cpu_probe_set_x(cpu, 2, 0xffff8001)); CHECK(satchel_cpu_probe_set_x(cpu, 3, 0));
        SatchelCPUProbeResult result = satchel_switch_process_run(p, 1, NULL, NULL);
        CHECK(result.reason == SATCHEL_CPU_SVC && result.svc == 0x29 && result.steps == 1);
        uint64_t value; CHECK(satchel_cpu_probe_get_x(cpu, 0, &value) && value == 0xAA);
        CHECK(satchel_switch_process_info_requests(p) == 1 && !satchel_switch_process_info_serviced(p));
        satchel_switch_process_destroy(p);
    }
#if defined(SATCHEL_SWITCH_RESOURCES_TESTING)
    // Failure of either actual arena allocation aborts process construction.
    // A retry succeeds with the same validated modules; no phantom resource.
    metadata.system_resource_size = 16 * 1024 * 1024;
    for (unsigned allocation = 1; allocation <= 2; allocation++) {
        satchel_switch_resources_test_fail_allocation(allocation);
        CHECK(!satchel_switch_process_create(modules, 2, &metadata, error, sizeof(error)));
        CHECK(strstr(error, "system-resource arena") != NULL);
    }
    satchel_switch_resources_test_fail_allocation(0);
    p = satchel_switch_process_create(modules, 2, &metadata, error, sizeof(error)); CHECK(p);
    CHECK(satchel_switch_process_system_resource_total(p) == 16 * 1024 * 1024 && satchel_switch_process_system_resource_used(p) == 4096);
    satchel_switch_process_destroy(p);
#endif
    metadata.system_resource_size = SATCHEL_SWITCH_PROCESS_MEMORY_LIMIT;
    CHECK(!satchel_switch_process_metadata_supported(&metadata));
    metadata.system_resource_size = 4096;
    CHECK(!satchel_switch_process_metadata_supported(&metadata));
}

#if defined(SATCHEL_CPU_TESTING)
extern void satchel_cpu_probe_test_mapping_allocation_failure(unsigned stage);
#endif
static void heap_call(SatchelSwitchProcess *p, uint64_t size, uint32_t status, uint64_t base) {
    SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
    CHECK(satchel_cpu_probe_set_pc(cpu, 0x8000000));
    CHECK(satchel_cpu_probe_set_x(cpu, 0, UINT64_MAX)); // Wrapper-owned pointer, not dereferenced by kernel.
    CHECK(satchel_cpu_probe_set_x(cpu, 1, size)); CHECK(satchel_cpu_probe_set_x(cpu, 2, 0xAABB));
    SatchelCPUProbeResult result = satchel_switch_process_run(p, 1, NULL, NULL);
    CHECK(result.reason == SATCHEL_CPU_BUDGET && result.steps == 1);
    uint64_t value;
    CHECK(satchel_cpu_probe_get_x(cpu, 0, &value) && value == status);
    CHECK(satchel_cpu_probe_get_x(cpu, 1, &value) && value == (status ? 0 : base));
    CHECK(satchel_cpu_probe_get_x(cpu, 2, &value) && value == 0xAABB);
}
static uint64_t heap_info(SatchelSwitchProcess *p, unsigned type) {
    SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
    CHECK(satchel_cpu_probe_set_pc(cpu, 0x8000004));
    CHECK(satchel_cpu_probe_set_x(cpu, 1, type)); CHECK(satchel_cpu_probe_set_x(cpu, 2, 0xffff8001));
    CHECK(satchel_cpu_probe_set_x(cpu, 3, 0));
    SatchelCPUProbeResult result = satchel_switch_process_run(p, 1, NULL, NULL);
    CHECK(result.reason == SATCHEL_CPU_BUDGET && result.steps == 1);
    uint64_t value;
    CHECK(satchel_cpu_probe_get_x(cpu, 0, &value) && !value);
    CHECK(satchel_cpu_probe_get_x(cpu, 1, &value));
    return value;
}
static void heap_query(SatchelSwitchProcess *p, uint64_t base, uint64_t size, uint64_t stack) {
    SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
    CHECK(satchel_cpu_probe_set_pc(cpu, 0x8000008));
    CHECK(satchel_cpu_probe_set_x(cpu, 0, stack)); CHECK(satchel_cpu_probe_set_x(cpu, 2, base));
    SatchelCPUProbeResult result = satchel_switch_process_run(p, 1, NULL, NULL);
    CHECK(result.reason == SATCHEL_CPU_BUDGET && result.steps == 1);
    SatchelSwitchMemoryInfo info;
    CHECK(satchel_cpu_probe_read(cpu, stack, &info, sizeof(info)));
    if (size) CHECK(info.address == base && info.size == size && info.state == 5 && info.permissions == 3);
    else CHECK(info.state == 0 && !info.permissions);
    CHECK(satchel_switch_process_heap_size(p) == size);
}
static void zero_guest_range(SatchelCPUProbe *cpu, uint64_t base, size_t size) {
    uint8_t bytes[4096], zero[4096] = {0};
    bool clean = true;
    for (size_t at = 0; at < size; at += sizeof(bytes)) {
        size_t count = size - at < sizeof(bytes) ? size - at : sizeof(bytes);
        if (!satchel_cpu_probe_read(cpu, base + at, bytes, count) || memcmp(bytes, zero, count)) { clean = false; break; }
    }
    CHECK(clean);
}
static void test_heap(unsigned bits) {
    const uint64_t mib2 = 2 * 1024 * 1024;
    uint8_t image[0x3000] = {0};
    const uint32_t code[] = {0xd4000021, 0xd4000521, 0xd40000c1, 0xf90000c5, 0xf94000c7, 0xd4000fe1};
    memcpy(image, code, sizeof(code));
    SatchelNSOInfo nso = {.image_size = sizeof(image), .bss_size = 0xFFC,
        .segments = {{0, sizeof(code), 0, sizeof(code)}, {0, 4, 0x1000, 4}, {0, 4, 0x2000, 4}}};
    SatchelSwitchProcessModule modules[] = {{"rtld", nso, image, sizeof(image)}, {"main", nso, image, sizeof(image)}};
    SatchelProcessMetadata metadata = {.program_id = 1, .address_space_size = UINT64_C(1) << bits,
        .main_thread_stack_size = 0x4000, .address_space_bits = (uint8_t)bits,
        .address_space_type = bits == 39 ? 3 : 1, .flags0 = bits == 39 ? 7 : 3, .is_64_bit = true,
        .system_resource_size = 16 * 1024 * 1024};
    char error[256];
    SatchelSwitchProcess *p = satchel_switch_process_create(modules, 2, &metadata, error, sizeof(error)); CHECK(p);
    SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
    uint64_t base = bits == 39 ? UINT64_C(0x2088200000) : UINT64_C(0x200000000);
    uint64_t stack = bits == 39 ? initial_stack39 : UINT64_C(0x30000000);
    CHECK(!satchel_switch_process_heap_requests(p) && !satchel_switch_process_heap_size(p));
    CHECK(!satchel_switch_process_heap_requests(NULL) && !satchel_switch_process_heap_size(NULL));
    uint64_t fixed = heap_info(p, 22), total = heap_info(p, 21);
    CHECK(fixed == 0xB000 && total == 240 * 1024 * 1024);
    CHECK(heap_info(p, 16) == metadata.system_resource_size && heap_info(p, 17) == 4096);
    CHECK(heap_info(p, 6) == total + metadata.system_resource_size && heap_info(p, 7) == fixed + metadata.system_resource_size);
    heap_call(p, 0, 0, base); heap_query(p, base, 0, stack);
    CHECK(heap_info(p, 22) == fixed);
#if defined(SATCHEL_CPU_TESTING)
    for (unsigned stage = 1; stage <= 2; stage++) {
        satchel_cpu_probe_test_mapping_allocation_failure(stage);
        heap_call(p, mib2, 0xD001, base);
        heap_query(p, base, 0, stack); CHECK(heap_info(p, 22) == fixed);
        uint64_t value; CHECK(!satchel_cpu_probe_read(cpu, base, &value, sizeof(value)));
    }
#endif
    heap_call(p, mib2, 0, base); heap_query(p, base, mib2, stack);
    zero_guest_range(cpu, base, (size_t)mib2);
    CHECK(heap_info(p, 22) == fixed + mib2 && heap_info(p, 21) == total);
    CHECK(!satchel_cpu_probe_set_pc(cpu, base)); // Heap is RW, never executable.
    // Execute real original guest STR/LDR instructions at both edges of the heap.
    const uint64_t marker = UINT64_C(0xFEDCBA9876543210);
    for (unsigned edge = 0; edge < 2; edge++) {
        CHECK(satchel_cpu_probe_set_pc(cpu, 0x800000C));
        CHECK(satchel_cpu_probe_set_x(cpu, 5, marker));
        CHECK(satchel_cpu_probe_set_x(cpu, 6, base + (edge ? mib2 - 8 : 0)));
        SatchelCPUProbeResult result = satchel_switch_process_run(p, 4, NULL, NULL);
        CHECK(result.reason == SATCHEL_CPU_SVC && result.svc == 0x7f && result.steps == 3);
        uint64_t value; CHECK(satchel_cpu_probe_get_x(cpu, 7, &value) && value == marker);
    }
#if defined(SATCHEL_CPU_TESTING)
    satchel_cpu_probe_test_mapping_allocation_failure(1);
    heap_call(p, mib2 * 2, 0xD001, base); heap_query(p, base, mib2, stack);
    uint64_t preserved; CHECK(satchel_cpu_probe_read(cpu, base, &preserved, sizeof(preserved)) && preserved == marker);
    CHECK(heap_info(p, 22) == fixed + mib2);
#endif
    heap_call(p, mib2, 0, base); // Unchanged size preserves bytes.
    heap_call(p, mib2 * 2, 0, base); heap_query(p, base, mib2 * 2, stack);
    CHECK(heap_info(p, 16) == metadata.system_resource_size && heap_info(p, 17) == 4096);
    zero_guest_range(cpu, base + mib2, (size_t)mib2);
    uint64_t value; CHECK(satchel_cpu_probe_read(cpu, base + mib2 - 8, &value, sizeof(value)) && value == marker);
    CHECK(satchel_cpu_probe_write(cpu, base + mib2 * 2 - 8, &marker, sizeof(marker)));
    const uint64_t invalid[] = {1, 4096, mib2 + 1, UINT64_MAX, UINT64_C(8) << 30};
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) heap_call(p, invalid[i], 0xCA01, base);
    const uint64_t over_budget[] = {240 * 1024 * 1024, UINT64_C(1) << 32, (UINT64_C(8) << 30) - mib2};
    for (unsigned i = 0; i < sizeof(over_budget) / sizeof(over_budget[0]); i++) heap_call(p, over_budget[i], 0xD001, base);
    heap_query(p, base, mib2 * 2, stack);
    CHECK(heap_info(p, 22) == fixed + mib2 * 2 && heap_info(p, 21) == total);
    CHECK(heap_info(p, 6) == total + metadata.system_resource_size &&
        heap_info(p, 7) == fixed + mib2 * 2 + metadata.system_resource_size);
    CHECK(satchel_cpu_probe_read(cpu, base + mib2 * 2 - 8, &value, sizeof(value)) && value == marker);
    heap_call(p, mib2, 0, base); heap_query(p, base, mib2, stack);
    CHECK(!satchel_cpu_probe_read(cpu, base + mib2, &value, sizeof(value)));
    CHECK(!satchel_cpu_probe_write(cpu, base + mib2 - 4, &marker, sizeof(marker)));
    CHECK(satchel_cpu_probe_read(cpu, base + mib2 - 8, &value, sizeof(value)) && value == marker);
    heap_call(p, mib2 * 2, 0, base); zero_guest_range(cpu, base + mib2, (size_t)mib2);
    heap_call(p, 0, 0, base); heap_query(p, base, 0, stack);
    CHECK(heap_info(p, 17) == 4096); // Existing metadata page still owns the map table.
    CHECK(heap_info(p, 22) == fixed && !satchel_cpu_probe_read(cpu, base, &value, sizeof(value)));
    heap_call(p, 0, 0, base);
    heap_call(p, mib2, 0, base); zero_guest_range(cpu, base, (size_t)mib2);
#if defined(SATCHEL_CPU_TESTING)
    CHECK(satchel_switch_process_heap_requests(p) == 20);
#else
    CHECK(satchel_switch_process_heap_requests(p) == 17);
#endif
    satchel_switch_process_destroy(p);
    // A254MiB reservation leaves under2MiB after the actual initial mappings.
    metadata.system_resource_size = 254 * 1024 * 1024;
    p = satchel_switch_process_create(modules, 2, &metadata, error, sizeof(error)); CHECK(p);
    cpu = satchel_switch_process_cpu(p);
    CHECK(satchel_cpu_probe_set_x(cpu, 1, mib2));
    SatchelCPUProbeResult cancelled = satchel_switch_process_run(p, 1, cancel, NULL);
    CHECK(cancelled.reason == SATCHEL_CPU_CANCELLED && cancelled.steps == 0 && !satchel_switch_process_heap_requests(p));
    heap_call(p, mib2, 0xD001, base); heap_query(p, base, 0, stack);
    CHECK(heap_info(p, 22) == fixed);
    satchel_switch_process_destroy(p);
}

static void physical_call(SatchelSwitchProcess *p, int unmap, uint64_t address, uint64_t size, uint32_t status) {
    SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
    CHECK(satchel_cpu_probe_set_pc(cpu, 0x8000000 + (unmap ? 4 : 0)));
    CHECK(satchel_cpu_probe_set_x(cpu, 0, address) && satchel_cpu_probe_set_x(cpu, 1, size) && satchel_cpu_probe_set_x(cpu, 2, 0xABCDEF));
    uint64_t accepted = unmap ? satchel_switch_process_physical_unmap_serviced(p) : satchel_switch_process_physical_map_serviced(p);
    SatchelCPUProbeResult result = satchel_switch_process_run(p, 1, NULL, NULL);
    CHECK(result.reason == SATCHEL_CPU_BUDGET && result.steps == 1);
    uint64_t value; CHECK(satchel_cpu_probe_get_x(cpu, 0, &value) && value == status);
    CHECK(satchel_cpu_probe_get_x(cpu, 1, &value) && value == size);
    CHECK(satchel_cpu_probe_get_x(cpu, 2, &value) && value == 0xABCDEF);
    CHECK((unmap ? satchel_switch_process_physical_unmap_serviced(p) : satchel_switch_process_physical_map_serviced(p)) == accepted + !status);
}
static void permission_call(SatchelSwitchProcess *p,uint64_t address,uint64_t size,uint64_t permission,uint32_t status) {
    SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);uint64_t value;
    uint64_t requests=satchel_switch_process_permission_requests(p),success=satchel_switch_process_permission_serviced(p);
    CHECK(satchel_cpu_probe_set_pc(cpu,0x8000028));
    CHECK(satchel_cpu_probe_set_x(cpu,0,address)&&satchel_cpu_probe_set_x(cpu,1,size)&&satchel_cpu_probe_set_x(cpu,2,permission)&&satchel_cpu_probe_set_x(cpu,3,0xabcdef));
    SatchelCPUProbeResult result=satchel_switch_process_run(p,1,NULL,NULL);
    if(result.reason!=SATCHEL_CPU_BUDGET)fprintf(stderr,"permission fixture stop reason%d: %s\n",result.reason,result.detail);
    CHECK(result.reason==SATCHEL_CPU_BUDGET&&result.steps==1);
    CHECK(satchel_cpu_probe_get_x(cpu,0,&value));
    if(value!=status)fprintf(stderr,"permission fixture status%x expected%x\n",(unsigned)value,status);
    CHECK(value==status);CHECK(satchel_cpu_probe_get_x(cpu,1,&value)&&value==size);
    CHECK(satchel_cpu_probe_get_x(cpu,2,&value)&&value==permission);CHECK(satchel_cpu_probe_get_x(cpu,3,&value)&&value==0xabcdef);
    CHECK(satchel_switch_process_permission_requests(p)==requests+1&&satchel_switch_process_permission_serviced(p)==success+!status);
}
static void attribute_call(SatchelSwitchProcess *p,uint64_t address,uint64_t size,uint64_t mask,uint64_t attributes,int stopped,uint32_t status) {
    SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);uint64_t value;
    uint64_t requests=satchel_switch_process_attribute_requests(p),success=satchel_switch_process_attribute_serviced(p);
    CHECK(satchel_cpu_probe_set_pc(cpu,0x800002c)&&satchel_cpu_probe_set_x(cpu,0,address)&&satchel_cpu_probe_set_x(cpu,1,size)&&
        satchel_cpu_probe_set_x(cpu,2,mask)&&satchel_cpu_probe_set_x(cpu,3,attributes)&&satchel_cpu_probe_set_x(cpu,4,0xabcdef));
    SatchelCPUProbeResult result=satchel_switch_process_run(p,1,NULL,NULL);
    CHECK(result.reason==(stopped?SATCHEL_CPU_SVC:SATCHEL_CPU_BUDGET)&&result.steps==1);
    if(stopped)CHECK(result.svc==3);
    CHECK(satchel_cpu_probe_get_x(cpu,0,&value)&&value==(stopped?address:status));
    CHECK(satchel_cpu_probe_get_x(cpu,1,&value)&&value==size);CHECK(satchel_cpu_probe_get_x(cpu,2,&value)&&value==mask);
    CHECK(satchel_cpu_probe_get_x(cpu,3,&value)&&value==attributes);CHECK(satchel_cpu_probe_get_x(cpu,4,&value)&&value==0xabcdef);
    CHECK(satchel_switch_process_attribute_requests(p)==requests+1&&satchel_switch_process_attribute_serviced(p)==success+(!stopped&&!status));
}
static uint64_t physical_info(SatchelSwitchProcess *p, uint32_t type) {
    SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
    CHECK(satchel_cpu_probe_set_pc(cpu, 0x8000008) && satchel_cpu_probe_set_x(cpu, 1, type));
    CHECK(satchel_cpu_probe_set_x(cpu, 2, 0xffff8001) && satchel_cpu_probe_set_x(cpu, 3, 0));
    CHECK(satchel_switch_process_run(p, 1, NULL, NULL).reason == SATCHEL_CPU_BUDGET);
    uint64_t value; CHECK(satchel_cpu_probe_get_x(cpu, 0, &value) && !value);
    CHECK(satchel_cpu_probe_get_x(cpu, 1, &value)); return value;
}
static void physical_heap(SatchelSwitchProcess *p, uint64_t size, uint64_t base) {
    SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
    CHECK(satchel_cpu_probe_set_pc(cpu, 0x8000010) && satchel_cpu_probe_set_x(cpu, 1, size));
    CHECK(satchel_switch_process_run(p, 1, NULL, NULL).reason == SATCHEL_CPU_BUDGET);
    uint64_t value; CHECK(satchel_cpu_probe_get_x(cpu, 0, &value) && !value);
    CHECK(satchel_cpu_probe_get_x(cpu, 1, &value) && value == base);
}
static void test_physical_memory(unsigned bits) {
    uint8_t image[0x3000] = {0};
    const uint32_t code[] = {0xd4000581, 0xd40005a1, 0xd4000521, 0xd40000c1, 0xd4000021, 0xf9400043, 0xf9000043};
    memcpy(image, code, sizeof(code));
    SatchelNSOInfo nso = {.image_size = sizeof(image), .bss_size = 0xFFC,
        .segments = {{0, sizeof(code), 0, sizeof(code)}, {0, 4, 0x1000, 4}, {0, 4, 0x2000, 4}}};
    SatchelSwitchProcessModule modules[] = {{"rtld", nso, image, sizeof(image)}, {"main", nso, image, sizeof(image)}};
    SatchelProcessMetadata metadata = {.program_id = 1, .address_space_size = UINT64_C(1) << bits,
        .main_thread_stack_size = 0x4000, .address_space_bits = (uint8_t)bits,
        .address_space_type = bits == 39 ? 3 : 1, .flags0 = bits == 39 ? 7 : 3, .is_64_bit = true};
    char error[256];
    uint64_t alias = bits == 39 ? UINT64_C(0x1088200000) : UINT64_C(0x80000000);
    uint64_t alias_size = (uint64_t)(bits == 39 ? 64 : 6) << 30, end = alias + alias_size;
    SatchelSwitchProcess *p = satchel_switch_process_create(modules, 2, &metadata, error, sizeof(error)); CHECK(p);
    CHECK(!satchel_switch_process_physical_memory_size(NULL) && !satchel_switch_process_physical_map_requests(NULL) &&
        !satchel_switch_process_physical_unmap_requests(NULL) && !satchel_switch_process_physical_map_serviced(NULL) &&
        !satchel_switch_process_physical_unmap_serviced(NULL));
    for (int unmap = 0; unmap < 2; unmap++) {
        physical_call(p, unmap, alias + 1, 0, 0xCC01);
        physical_call(p, unmap, alias, 0, 0xCA01);
        physical_call(p, unmap, UINT64_MAX - 4095, 4096, 0xDC01);
        physical_call(p, unmap, alias, 4096, 0xFA01);
        physical_call(p, unmap, 0x8000000, 4096, 0xFA01); // Resource precedes alias-range validation.
    }
    CHECK(!satchel_switch_process_physical_memory_size(p)); satchel_switch_process_destroy(p);
    metadata.system_resource_size = 16 * 1024 * 1024;
    p = satchel_switch_process_create(modules, 2, &metadata, error, sizeof(error)); CHECK(p);
    SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
    uint64_t baseline = physical_info(p, 22), total = physical_info(p, 6);
    CHECK(total == SATCHEL_SWITCH_PROCESS_MEMORY_LIMIT);
    for (int unmap = 0; unmap < 2; unmap++) {
        physical_call(p, unmap, alias + 1, 4097, 0xCC01);
        physical_call(p, unmap, alias, 4097, 0xCA01);
        physical_call(p, unmap, alias, 0, 0xCA01);
        physical_call(p, unmap, UINT64_MAX - 4095, 4096, 0xDC01);
        physical_call(p, unmap, alias - 4096, 4096, 0xDC01);
        physical_call(p, unmap, end, 4096, 0xDC01);
        physical_call(p, unmap, end - 4096, 8192, 0xDC01);
        physical_call(p, unmap, 0x8000000, 4096, 0xDC01);
    }
    CHECK(!satchel_switch_process_physical_memory_size(p));
    physical_call(p, 1, alias, 4096, 0); // Unmap of genuinely free space is a no-op.
    physical_call(p, 0, alias, 4096, 0); zero_guest_range(cpu, alias, 4096);
    CHECK(satchel_switch_process_physical_memory_size(p) == 4096 && physical_info(p, 22) == baseline + 4096);
    uint64_t marker = UINT64_C(0xABCDEF0123456789), actual;
    CHECK(satchel_cpu_probe_write(cpu, alias, &marker, sizeof(marker)));
    physical_call(p, 0, alias, 4096, 0); CHECK(satchel_cpu_probe_read(cpu, alias, &actual, 8) && actual == marker);
    CHECK(satchel_switch_process_physical_memory_size(p) == 4096);
    physical_call(p, 0, alias + 4096, 4096, 0); zero_guest_range(cpu, alias + 4096, 4096);
    SatchelSwitchMemoryInfo info; CHECK(satchel_switch_process_query(p, alias + 4096, &info));
    CHECK(info.address == alias && info.size == 8192 && info.state == 5 && info.permissions == 3);
    // Consecutive4KiB allocations must support a real unaligned guest access
    // across their old backing boundary, not merely merged QueryMemory labels.
    CHECK(satchel_cpu_probe_set_x(cpu, 2, alias + 4092) && satchel_cpu_probe_set_x(cpu, 3, marker));
    CHECK(satchel_cpu_probe_set_pc(cpu, 0x8000018)); CHECK(satchel_switch_process_run(p, 1, NULL, NULL).reason == SATCHEL_CPU_BUDGET);
    CHECK(satchel_cpu_probe_set_x(cpu, 3, 0) && satchel_cpu_probe_set_pc(cpu, 0x8000014));
    CHECK(satchel_switch_process_run(p, 1, NULL, NULL).reason == SATCHEL_CPU_BUDGET);
    CHECK(satchel_cpu_probe_get_x(cpu, 3, &actual) && actual == marker);
    physical_call(p, 0, alias + 8192, 4096, 0);
    CHECK(satchel_cpu_probe_write(cpu, alias + 8192, &marker, sizeof(marker)));
    CHECK(physical_info(p, 7) == baseline + 12288 + metadata.system_resource_size && physical_info(p, 17) == 4096);
#if defined(SATCHEL_CPU_TESTING)
    for (unsigned stage = 1; stage <= 2; stage++) {
        satchel_cpu_probe_test_mapping_allocation_failure(stage);
        physical_call(p, 0, alias + 12288, 4096, 0xD001);
        CHECK(satchel_switch_process_physical_memory_size(p) == 12288);
        CHECK(!satchel_cpu_probe_read(cpu, alias + 12288, &actual, 8));
        CHECK(satchel_cpu_probe_read(cpu, alias + 8192, &actual, 8) && actual == marker);
        satchel_cpu_probe_test_mapping_allocation_failure(stage);
        physical_call(p, 1, alias + 4096, 4096, 0xD001);
        CHECK(satchel_switch_process_query(p, alias + 4096, &info) && info.address == alias && info.size == 12288);
        CHECK(satchel_switch_process_physical_memory_size(p) == 12288);
    }
#endif
    physical_call(p, 1, alias + 4096, 4096, 0);
    CHECK(satchel_switch_process_physical_memory_size(p) == 8192 && physical_info(p, 22) == baseline + 8192);
    CHECK(!satchel_cpu_probe_read(cpu, alias + 4096, &actual, 8));
    CHECK(satchel_cpu_probe_read(cpu, alias, &actual, 8) && actual == marker);
    CHECK(satchel_cpu_probe_read(cpu, alias + 8192, &actual, 8) && actual == marker);
    CHECK(satchel_switch_process_query(p, alias, &info) && info.address == alias && info.size == 4096);
    CHECK(satchel_switch_process_query(p, alias + 8192, &info) && info.address == alias + 8192 && info.size == 4096);
    physical_call(p, 0, alias, 12288, 0); // Fill only the removed page; preserve both surviving pages.
    zero_guest_range(cpu, alias + 4096, 4096);
    CHECK(satchel_cpu_probe_read(cpu, alias, &actual, 8) && actual == marker);
    CHECK(satchel_cpu_probe_read(cpu, alias + 8192, &actual, 8) && actual == marker);
    CHECK(satchel_switch_process_physical_memory_size(p) == 12288);
    physical_call(p, 0, alias + 4096, 4096, 0);
    CHECK(satchel_switch_process_query(p, alias + 4096, &info) && info.address == alias && info.size == 12288);
    // Partial-overlap growth charges only the new page; suffix and prefix
    // unmaps retain surviving bytes. Unmapping gaps does not over-release budget.
    physical_call(p, 0, alias + 8192, 8192, 0);
    CHECK(satchel_switch_process_physical_memory_size(p) == 16384);
    physical_call(p, 1, alias, 4096, 0); physical_call(p, 1, alias + 12288, 4096, 0);
    CHECK(satchel_cpu_probe_read(cpu, alias + 8192, &actual, 8) && actual == marker);
    physical_call(p, 1, alias, 24576, 0);
    CHECK(!satchel_switch_process_physical_memory_size(p) && physical_info(p, 22) == baseline);
    physical_call(p, 0, alias, 240 * 1024 * 1024, 0x10801);
    CHECK(!satchel_switch_process_physical_memory_size(p) && !satchel_cpu_probe_read(cpu, alias, &actual, 8));
    // Physical pages at alias-end must not absorb a separately resized heap.
    physical_heap(p, 2 * 1024 * 1024, end);
    physical_call(p, 0, end - 4096, 4096, 0);
    physical_heap(p, 4 * 1024 * 1024, end);
    CHECK(satchel_cpu_probe_write(cpu, end - 8, &marker, 8) && satchel_cpu_probe_write(cpu, end, &marker, 8));
    physical_call(p, 1, end - 4096, 4096, 0);
    CHECK(satchel_cpu_probe_read(cpu, end, &actual, 8) && actual == marker);
    physical_heap(p, 0, end);
    CHECK(physical_info(p, 22) == baseline && physical_info(p, 6) == total);
    // Eight original CPU mappings leave24 slots. A25th disjoint physical
    // allocation reports OutOfResource with no partial memory/accounting change.
    for (unsigned i = 0; i < 24; i++) physical_call(p, 0, alias + i * 8192, 4096, 0);
    physical_call(p, 0, alias + 24 * 8192, 4096, 0xCE01);
    CHECK(satchel_switch_process_physical_memory_size(p) == 24 * 4096);
    CHECK(!satchel_cpu_probe_read(cpu, alias + 24 * 8192, &actual, 8));
    physical_call(p, 1, alias, 25 * 8192, 0);
    CHECK(!satchel_switch_process_physical_memory_size(p) && physical_info(p, 22) == baseline);
    CHECK(satchel_switch_process_physical_map_requests(p) > satchel_switch_process_physical_map_serviced(p));
    CHECK(satchel_switch_process_physical_unmap_requests(p) > satchel_switch_process_physical_unmap_serviced(p));
    satchel_switch_process_destroy(p);
}

static uint32_t port_call(SatchelSwitchProcess *p, uint64_t address, uint32_t expected) {
    SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
    CHECK(satchel_cpu_probe_set_pc(cpu, 0x8000000));
    CHECK(satchel_cpu_probe_set_x(cpu, 0, UINT64_MAX)); CHECK(satchel_cpu_probe_set_x(cpu, 1, address));
    CHECK(satchel_cpu_probe_set_x(cpu, 2, 0xAABBCCDD));
    SatchelCPUProbeResult result = satchel_switch_process_run(p, 1, NULL, NULL);
    CHECK(result.reason == SATCHEL_CPU_BUDGET && result.steps == 1);
    uint64_t value, handle;
    CHECK(satchel_cpu_probe_get_x(cpu, 0, &value) && value == expected);
    CHECK(satchel_cpu_probe_get_x(cpu, 1, &handle) && handle <= UINT32_MAX && (expected ? !handle : handle != 0));
    CHECK(satchel_cpu_probe_get_x(cpu, 2, &value) && value == 0xAABBCCDD);
    return (uint32_t)handle;
}
static void handle_close(SatchelSwitchProcess *p, uint64_t handle, uint32_t expected) {
    SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
    CHECK(satchel_cpu_probe_set_pc(cpu, 0x8000004));
    CHECK(satchel_cpu_probe_set_x(cpu, 0, handle)); CHECK(satchel_cpu_probe_set_x(cpu, 1, 0xCCDD));
    SatchelCPUProbeResult result = satchel_switch_process_run(p, 1, NULL, NULL);
    CHECK(result.reason == SATCHEL_CPU_BUDGET && result.steps == 1);
    uint64_t value;
    CHECK(satchel_cpu_probe_get_x(cpu, 0, &value) && value == expected);
    CHECK(satchel_cpu_probe_get_x(cpu, 1, &value) && value == 0xCCDD);
}
static uint64_t owned_thread_query(SatchelSwitchProcess *p, unsigned which, uint32_t handle, uint32_t expected) {
    SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
    CHECK(satchel_cpu_probe_set_pc(cpu, 0x8000008 + which * 4));
    CHECK(satchel_cpu_probe_set_x(cpu, which == 1 ? 2 : 1, handle));
    SatchelCPUProbeResult result = satchel_switch_process_run(p, 1, NULL, NULL);
    CHECK(result.reason == SATCHEL_CPU_BUDGET && result.steps == 1);
    uint64_t value;
    CHECK(satchel_cpu_probe_get_x(cpu, 0, &value) && value == expected);
    CHECK(satchel_cpu_probe_get_x(cpu, 1, &value));
    return value;
}
static void sm_request(uint8_t bytes[256], uint32_t command, int send_pid, const uint8_t argument[8],
                       uint32_t type, uint32_t version, uint32_t token) {
    memset(bytes, 0, 256);
    metadata_word(bytes, type); metadata_word(bytes + 4, 10 | (send_pid ? 0x80000000u : 0));
    size_t cmif = send_pid ? 32 : 16;
    if (send_pid) {
        metadata_word(bytes + 8, 1); // Request that kernel attach owned identity.
        memset(bytes + 12, 0xDA, 8); // Intentionally forged claim, never trusted.
    }
    metadata_word(bytes + cmif, 0x49434653); metadata_word(bytes + cmif + 4, version);
    metadata_word(bytes + cmif + 8, command); metadata_word(bytes + cmif + 12, token);
    memcpy(bytes + cmif + 16, argument, 8);
    memset(bytes + 64, 0xA7, 192); // Canary outside the protocol message.
}
static void sm_ipc(SatchelSwitchProcess *p, uint64_t handle, uint8_t request[256],
                   int replied, uint32_t expected_result, uint32_t token) {
    SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
    CHECK(satchel_cpu_probe_write(cpu, 0x31000000, request, 256));
    CHECK(satchel_cpu_probe_set_pc(cpu, 0x8000014)); CHECK(satchel_cpu_probe_set_x(cpu, 0, handle));
    CHECK(satchel_cpu_probe_set_x(cpu, 1, UINT64_C(0xACACACACACACACAC)));
    CHECK(satchel_cpu_probe_set_x(cpu, 2, 0xDEADBEEF));
    SatchelCPUProbeResult result = satchel_switch_process_run(p, 1, NULL, NULL);
    uint64_t value;
    CHECK(satchel_cpu_probe_get_x(cpu, 1, &value) && value == UINT64_C(0xACACACACACACACAC));
    CHECK(satchel_cpu_probe_get_x(cpu, 2, &value) && value == 0xDEADBEEF);
    uint8_t actual[256]; CHECK(satchel_cpu_probe_read(cpu, 0x31000000, actual, sizeof(actual)));
    CHECK(satchel_cpu_probe_get_x(cpu, 0, &value));
    if (replied) {
        CHECK(result.reason == SATCHEL_CPU_BUDGET && result.steps == 1 && value == 0);
        uint8_t expected[32] = {0};
        uint32_t wire_type = request[0] | (uint32_t)request[1] << 8;
        size_t reply_size = wire_type >= 16 ? 12 : 32;
        if (wire_type >= 16) {
            metadata_word(expected + 4, 1); metadata_word(expected + 8, expected_result);
        } else {
            metadata_word(expected + 4, 6); metadata_word(expected + 16, 0x4f434653);
            metadata_word(expected + 24, expected_result); metadata_word(expected + 28, token);
        }
        CHECK(!memcmp(actual, expected, reply_size));
        CHECK(!memcmp(actual + reply_size, request + reply_size, sizeof(actual) - reply_size));
    } else {
        CHECK(result.reason == SATCHEL_CPU_SVC && result.svc == 0x21 && result.steps == 1 && value == handle);
        CHECK(!memcmp(actual, request, sizeof(actual)));
    }
    uint32_t thread_handle;
    CHECK(satchel_cpu_probe_read(cpu, 0x31000110, &thread_handle, 4) && thread_handle == 0x80);
}
static uint32_t lm_reply(SatchelSwitchProcess *p, uint32_t handle, uint8_t request[256],
                         int moved_handle, const uint8_t *data, size_t data_size) {
    SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
    CHECK(satchel_cpu_probe_write(cpu, 0x31000000, request, 256));
    CHECK(satchel_cpu_probe_set_pc(cpu, 0x8000014)); CHECK(satchel_cpu_probe_set_x(cpu, 0, handle));
    SatchelCPUProbeResult result = satchel_switch_process_run(p, 1, NULL, NULL);
    CHECK(result.reason == SATCHEL_CPU_BUDGET && result.steps == 1);
    uint64_t value; CHECK(satchel_cpu_probe_get_x(cpu, 0, &value) && value == 0);
    uint8_t reply[256]; CHECK(satchel_cpu_probe_read(cpu, 0x31000000, reply, sizeof(reply)));
    uint32_t token = 0;
    if (moved_handle) for (unsigned i = 0; i < 4; i++) token |= (uint32_t)reply[12 + i] << (i * 8);
    CHECK(moved_handle ? token >= 0x81 : token == 0);
    bool tipc = request[0] >= 16;
    uint8_t expected[64] = {0};
    size_t raw = moved_handle ? 16 : 8;
    size_t payload = tipc ? raw + 4 : 32;
    size_t end = (payload + data_size + 3) & ~(size_t)3;
    metadata_word(expected + 4, (uint32_t)((end - raw) / 4) | (moved_handle ? 0x80000000u : 0));
    if (moved_handle) { metadata_word(expected + 8, 0x20); metadata_word(expected + 12, token); }
    if (!tipc) metadata_word(expected + 16, 0x4f434653);
    if (data_size) memcpy(expected + payload, data, data_size);
    CHECK(!memcmp(reply, expected, end));
    CHECK(!memcmp(reply + end, request + end, sizeof(reply) - end));
    return token;
}

static void test_sm_ipc(SatchelSwitchProcess *p) {
    CHECK(SATCHEL_SWITCH_IPC_SERVICE_OTHER == 8 && SATCHEL_SWITCH_IPC_SERVICE_CSRNG == 9);
    CHECK(!strcmp(satchel_switch_process_service_name(UINT32_MAX), "other"));
    CHECK(!strcmp(satchel_switch_process_service_name(SATCHEL_SWITCH_IPC_SERVICE_NONE), "none"));
    uint32_t handle = port_call(p, 0x8001000, 0), other = port_call(p, 0x8001000, 0);
    CHECK(!satchel_switch_process_is_sm_initialized(p, handle));
    CHECK(!satchel_switch_process_is_sm_initialized(NULL, handle));
    CHECK(!satchel_switch_process_is_sm_initialized(p, 0x80));
    uint8_t request[256], argument[8] = {0};
    uint64_t baseline = satchel_switch_process_ipc_serviced(p);
    SatchelSwitchIPCInfo info = {.parser_status = 0xAA, .kind = 0xBB, .command_id = 0xCC, .service_kind = 0xDD, .blocked_reason = 0xEE};
    CHECK(!satchel_switch_process_last_ipc(NULL, &info) && info.parser_status == 0xAA);
    CHECK(!satchel_switch_process_last_ipc(p, NULL));
    // A valid GetService on a fresh session is a real SM InvalidClient response.
    memcpy(argument, "fsp-srv", 7);
    sm_request(request, 1, 0, argument, 4, 0, 0);
    sm_ipc(p, handle, request, 1, 0x415, 0);
    CHECK(!satchel_switch_process_is_sm_initialized(p, handle));
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.command_id == 1 && !info.blocked_reason);
    // Initialize requires the actual send-PID schema, not a claimed user PID.
    sm_request(request, 0, 0, argument, 4, 0, 0);
    sm_ipc(p, handle, request, 0, 0, 0);
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_SCHEMA);
    CHECK(!satchel_switch_process_is_sm_initialized(p, handle));
    memset(argument, 0xAB, sizeof(argument)); // Placeholder is equally untrusted.
    sm_request(request, 0, 1, argument, 4, 0, 0);
    sm_ipc(p, UINT64_C(0x1200000000) | handle, request, 1, 0, 0); // Input handle is W0.
    CHECK(satchel_switch_process_is_sm_initialized(p, handle));
    CHECK(!satchel_switch_process_is_sm_initialized(p, other));
    // Reinitialization rebinds the same owned identity; v1 context token echoes.
    sm_request(request, 0, 1, argument, 6, 1, 0x12345678);
    sm_ipc(p, handle, request, 1, 0, 0x12345678);
    CHECK(satchel_switch_process_is_sm_initialized(p, handle));
    memset(argument, 0, sizeof(argument));
    sm_request(request, 1, 0, argument, 4, 0, 0);
    sm_ipc(p, handle, request, 1, 0xC15, 0);
    sm_ipc(p, other, request, 1, 0x415, 0); // Client validation precedes invalid name.
    argument[0] = 'a'; argument[2] = 'b'; // Nonzero after first NUL is malformed.
    sm_request(request, 1, 0, argument, 4, 0, 0);
    sm_ipc(p, handle, request, 1, 0xC15, 0);
    const struct { char name[9]; uint32_t kind; } services[] = {
        {"fsp-srv", SATCHEL_SWITCH_IPC_SERVICE_FSP}, {"nvdrv", SATCHEL_SWITCH_IPC_SERVICE_NVDRV},
        {"hid", SATCHEL_SWITCH_IPC_SERVICE_HID}, {"appletOE", SATCHEL_SWITCH_IPC_SERVICE_APPLET},
        {"vi:u", SATCHEL_SWITCH_IPC_SERVICE_VI}, {"set:sys", SATCHEL_SWITCH_IPC_SERVICE_SET_SYS},
        {"unknown", SATCHEL_SWITCH_IPC_SERVICE_OTHER}, // time:u now has a positive typed lifecycle below.
        {"csrng", SATCHEL_SWITCH_IPC_SERVICE_CSRNG}, {"psc:m", SATCHEL_SWITCH_IPC_SERVICE_PSC_M},
        {"lm", SATCHEL_SWITCH_IPC_SERVICE_LM}, {"spl:", SATCHEL_SWITCH_IPC_SERVICE_SPL},
        {"fatal:u", SATCHEL_SWITCH_IPC_SERVICE_FATAL}, {"ldr:ro", SATCHEL_SWITCH_IPC_SERVICE_LDR_RO},
        {"ro:1", SATCHEL_SWITCH_IPC_SERVICE_RO_1}, {"apm", SATCHEL_SWITCH_IPC_SERVICE_APM},
        {"apm:p", SATCHEL_SWITCH_IPC_SERVICE_APM_P}, {"acc:u0", SATCHEL_SWITCH_IPC_SERVICE_ACC_U0},
        {"ns:am", SATCHEL_SWITCH_IPC_SERVICE_NS_AM}, {"bpc", SATCHEL_SWITCH_IPC_SERVICE_BPC},
        {"audren:u", SATCHEL_SWITCH_IPC_SERVICE_AUDREN}, {"audout:u", SATCHEL_SWITCH_IPC_SERVICE_AUDOUT},
        {"audin:u", SATCHEL_SWITCH_IPC_SERVICE_AUDIN}, {"nifm:u", SATCHEL_SWITCH_IPC_SERVICE_NIFM},
        {"bsd:u", SATCHEL_SWITCH_IPC_SERVICE_BSD}, {"ssl", SATCHEL_SWITCH_IPC_SERVICE_SSL},
        {"pl:u", SATCHEL_SWITCH_IPC_SERVICE_PL}, // Exact pctl now has a dedicated owned-object journey below.
        {"friend:u", SATCHEL_SWITCH_IPC_SERVICE_FRIENDS}, {"sfdnsres", SATCHEL_SWITCH_IPC_SERVICE_SFDNSRES},
        {"apm:am", SATCHEL_SWITCH_IPC_SERVICE_APM_AM}, {"apm:sys", SATCHEL_SWITCH_IPC_SERVICE_APM_SYS},
        {"fgm", SATCHEL_SWITCH_IPC_SERVICE_FGM},
        {"nvdrv:a", SATCHEL_SWITCH_IPC_SERVICE_NVDRV_A},
        {"appletAE", SATCHEL_SWITCH_IPC_SERVICE_APPLET_AE},
        {"vi:m", SATCHEL_SWITCH_IPC_SERVICE_VI_M},
        {"time:s", SATCHEL_SWITCH_IPC_SERVICE_TIME_S},
        {"spl:mig", SATCHEL_SWITCH_IPC_SERVICE_SPL_MIG},
        {"spl:ssl", SATCHEL_SWITCH_IPC_SERVICE_SPL_SSL},
        {"spl:fs", SATCHEL_SWITCH_IPC_SERVICE_SPL_FS},
        {"spl:es", SATCHEL_SWITCH_IPC_SERVICE_SPL_ES},
        {"spl:manu", SATCHEL_SWITCH_IPC_SERVICE_SPL_MANU},
        {"ns:am2", SATCHEL_SWITCH_IPC_SERVICE_NS_AM2},
        {"bpc:c", SATCHEL_SWITCH_IPC_SERVICE_BPC_C},
        {"nifm:s", SATCHEL_SWITCH_IPC_SERVICE_NIFM_S},
        {"nifm:a", SATCHEL_SWITCH_IPC_SERVICE_NIFM_A},
        {"bsd:s", SATCHEL_SWITCH_IPC_SERVICE_BSD_S},
        {"ssl:s", SATCHEL_SWITCH_IPC_SERVICE_SSL_S},
        {"pl:s", SATCHEL_SWITCH_IPC_SERVICE_PL_S},
        {"friend:s", SATCHEL_SWITCH_IPC_SERVICE_FRIENDS_S},
        {"fgm:0", SATCHEL_SWITCH_IPC_SERVICE_FGM_0},
        {"fgm:9", SATCHEL_SWITCH_IPC_SERVICE_FGM_9},
        {"nvdrv:s", SATCHEL_SWITCH_IPC_SERVICE_NVDRV_S},
        {"nvdrv:t", SATCHEL_SWITCH_IPC_SERVICE_NVDRV_T},
        {"mm:u", SATCHEL_SWITCH_IPC_SERVICE_MM_U},
        {"caps:u", SATCHEL_SWITCH_IPC_SERVICE_CAPS_U},
        {"caps:a", SATCHEL_SWITCH_IPC_SERVICE_CAPS_A},
        {"caps:c", SATCHEL_SWITCH_IPC_SERVICE_CAPS_C},
        {"caps:dc", SATCHEL_SWITCH_IPC_SERVICE_CAPS_DC},
        {"caps:sc", SATCHEL_SWITCH_IPC_SERVICE_CAPS_SC},
        {"caps:su", SATCHEL_SWITCH_IPC_SERVICE_CAPS_SU},
        {"prepo:u", SATCHEL_SWITCH_IPC_SERVICE_PREPO_U},
        {"prepo:s", SATCHEL_SWITCH_IPC_SERVICE_PREPO_S},
        {"prepo:m", SATCHEL_SWITCH_IPC_SERVICE_PREPO_M},
        {"prepo:a", SATCHEL_SWITCH_IPC_SERVICE_PREPO_A},
        {"prepo:a2", SATCHEL_SWITCH_IPC_SERVICE_PREPO_A2},
        {"prepo:d", SATCHEL_SWITCH_IPC_SERVICE_PREPO_D},
        {"psc:c", SATCHEL_SWITCH_IPC_SERVICE_PSC_C},
        {"psc:l", SATCHEL_SWITCH_IPC_SERVICE_PSC_L}
    };
    uint64_t sessions = satchel_switch_process_sm_sessions(p);
    for (size_t i = 0; i < sizeof(services) / sizeof(services[0]); i++) {
        sm_request(request, 1, 0, (const uint8_t *)services[i].name, 4, 0, 0);
        if (services[i].kind == SATCHEL_SWITCH_IPC_SERVICE_LM || services[i].kind == SATCHEL_SWITCH_IPC_SERVICE_APM ||
            !memcmp(services[i].name, "appletOE", 8)) {
            uint32_t lm = lm_reply(p, handle, request, 1, NULL, 0);
            CHECK(satchel_switch_process_is_lm_session(p, lm) == (services[i].kind == SATCHEL_SWITCH_IPC_SERVICE_LM));
            CHECK(satchel_switch_process_last_ipc(p, &info) && !info.blocked_reason && info.service_kind == services[i].kind);
            handle_close(p, lm, 0);
        } else {
            sm_ipc(p, handle, request, 0, 0, 0);
            CHECK(satchel_switch_process_last_ipc(p, &info) && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_SERVICE &&
                info.service_kind == services[i].kind && info.command_id == 1);
        }
        CHECK(!strcmp(satchel_switch_process_service_name(info.service_kind),
            info.service_kind == SATCHEL_SWITCH_IPC_SERVICE_OTHER ? "other" :
            (info.service_kind == SATCHEL_SWITCH_IPC_SERVICE_SET ? "set" : services[i].name)));
        CHECK(satchel_switch_process_sm_sessions(p) == sessions);
    }
    sm_request(request, 1, 1, argument, 4, 0, 0);
    sm_ipc(p, handle, request, 0, 0, 0);
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_SCHEMA);
    sm_request(request, 0, 1, argument, 4, 0, 0); metadata_word(request + 4, 0x80000009);
    sm_ipc(p, other, request, 0, 0, 0); CHECK(!satchel_switch_process_is_sm_initialized(p, other));
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_SCHEMA);
    sm_request(request, 0, 1, argument, 4, 0, 0); metadata_word(request + 32, 0);
    sm_ipc(p, other, request, 0, 0, 0);
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_PARSE && info.parser_status != 0);
    sm_request(request, 0, 1, argument, 16, 0, 0);
    metadata_word(request + 4, 0x80000000u | 5u << 20 | 2u << 14 | 2u << 10 | 17u);
    sm_ipc(p, other, request, 0, 0, 0);
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.wire_type == 16 && info.receive_list_mode == 2 &&
        info.receive_list_offset == 5 && info.reserved_header_bits == 2 && info.raw_word_count == 17 && info.has_special_header == 1);
    sm_request(request, 99, 0, argument, 4, 0, 0);
    sm_ipc(p, handle, request, 0, 0, 0);
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_COMMAND && info.command_id == 99);
    sm_request(request, 3, 0, argument, 5, 0, 0);
    sm_ipc(p, handle, request, 0, 0, 0);
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
    CHECK(satchel_switch_process_ipc_serviced(p) == baseline + 9);
    handle_close(p, handle, 0); CHECK(!satchel_switch_process_is_sm_initialized(p, handle));
    // Closed and wrong-type handles return a kernel error without touching TLS.
    const uint32_t invalid[] = {handle, 0x80, 0xffff8000, 0xffff8001, 0};
    SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        CHECK(satchel_cpu_probe_write(cpu, 0x31000000, request, sizeof(request)));
        CHECK(satchel_cpu_probe_set_pc(cpu, 0x8000014)); CHECK(satchel_cpu_probe_set_x(cpu, 0, invalid[i]));
        SatchelCPUProbeResult result = satchel_switch_process_run(p, 1, NULL, NULL);
        uint64_t value;
        CHECK(result.reason == SATCHEL_CPU_BUDGET && result.steps == 1);
        CHECK(satchel_cpu_probe_get_x(cpu, 0, &value) && value == 0xE401);
        uint8_t actual[256]; CHECK(satchel_cpu_probe_read(cpu, 0x31000000, actual, sizeof(actual)) && !memcmp(actual, request, sizeof(actual)));
    }
    CHECK(satchel_switch_process_ipc_serviced(p) == baseline + 9);
    handle_close(p, other, 0);
}

static void tipc_sm_request(uint8_t bytes[256], uint32_t command, int send_pid,
                            const uint8_t *argument, uint32_t argument_size) {
    memset(bytes, 0, 256);
    metadata_word(bytes, 16 + command);
    metadata_word(bytes + 4, (argument_size / 4) | (send_pid ? 0x80000000u : 0));
    size_t raw = send_pid ? 20 : 8;
    if (send_pid) { metadata_word(bytes + 8, 1); memset(bytes + 12, 0xD7, 8); }
    if (argument_size) memcpy(bytes + raw, argument, argument_size);
    memset(bytes + 64, 0xA7, 192);
}
static void test_tipc_sm(SatchelSwitchProcess *p) {
    uint32_t session = port_call(p, 0x8001000, 0), other = port_call(p, 0x8001000, 0);
    uint8_t bytes[256], name[12] = {'f','s','p','-','s','r','v',0};
    uint64_t before = satchel_switch_process_ipc_serviced(p);
    SatchelSwitchIPCInfo info;
    tipc_sm_request(bytes, 1, 0, name, 8);
    sm_ipc(p, session, bytes, 1, 0x415, 0);
    CHECK(!satchel_switch_process_is_sm_initialized(p, session));
    // Exact real stage18 shape: type16, special PID1, zero raw words.
    tipc_sm_request(bytes, 0, 1, NULL, 0);
    sm_ipc(p, session, bytes, 1, 0, 0);
    CHECK(satchel_switch_process_is_sm_initialized(p, session));
    CHECK(!satchel_switch_process_is_sm_initialized(p, other));
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.wire_type == 16 && info.has_special_header == 1 &&
        !info.raw_word_count && info.kind == 5 && info.command_id == 0 && !info.blocked_reason);
    // Protocol choice belongs to each message; initialization belongs to session.
    sm_request(bytes, 1, 0, name, 4, 0, 0);
    sm_ipc(p, session, bytes, 0, 0, 0);
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.service_kind == SATCHEL_SWITCH_IPC_SERVICE_FSP &&
        info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_SERVICE);
    tipc_sm_request(bytes, 0, 1, NULL, 0); memset(bytes + 12, 0, 8);
    sm_ipc(p, session, bytes, 1, 0, 0); // User PID0 claim cannot unset owned identity.
    CHECK(satchel_switch_process_is_sm_initialized(p, session));
    tipc_sm_request(bytes, 1, 0, name, 8);
    sm_ipc(p, session, bytes, 0, 0, 0);
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.kind == 5 && info.command_id == 1 &&
        info.service_kind == SATCHEL_SWITCH_IPC_SERVICE_FSP && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_SERVICE);
    tipc_sm_request(bytes, 0, 0, NULL, 0);
    sm_ipc(p, other, bytes, 0, 0, 0);
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_SCHEMA);
    tipc_sm_request(bytes, 0, 1, name, 8); // CMIF placeholder is invalid for TIPC.
    sm_ipc(p, other, bytes, 0, 0, 0);
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_SCHEMA);
    CHECK(!satchel_switch_process_is_sm_initialized(p, other));
    const unsigned wrong_lengths[] = {0, 4, 12};
    for (size_t i = 0; i < sizeof(wrong_lengths) / sizeof(wrong_lengths[0]); i++) {
        tipc_sm_request(bytes, 1, 0, name, wrong_lengths[i]);
        sm_ipc(p, session, bytes, 0, 0, 0);
        CHECK(satchel_switch_process_last_ipc(p, &info) && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_SCHEMA);
    }
    tipc_sm_request(bytes, 1, 1, name, 8);
    sm_ipc(p, session, bytes, 0, 0, 0);
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_SCHEMA);
    memset(name, 0, sizeof(name));
    tipc_sm_request(bytes, 1, 0, name, 8); sm_ipc(p, session, bytes, 1, 0xC15, 0);
    sm_ipc(p, other, bytes, 1, 0x415, 0);
    name[0] = 'a'; name[3] = 'b';
    tipc_sm_request(bytes, 1, 0, name, 8); sm_ipc(p, session, bytes, 1, 0xC15, 0);
    // Static public labels are diagnostic only: no session or guest payload
    // is returned for these unsupported exact interfaces/personas.
    const struct {char name[9];uint32_t kind;} catalog[] = {
        {"nvdrv:a", SATCHEL_SWITCH_IPC_SERVICE_NVDRV_A},
        {"appletAE", SATCHEL_SWITCH_IPC_SERVICE_APPLET_AE},
        {"vi:m", SATCHEL_SWITCH_IPC_SERVICE_VI_M},
        {"set:sys", SATCHEL_SWITCH_IPC_SERVICE_SET_SYS},
        {"time:s", SATCHEL_SWITCH_IPC_SERVICE_TIME_S},
        {"spl:mig", SATCHEL_SWITCH_IPC_SERVICE_SPL_MIG},
        {"spl:ssl", SATCHEL_SWITCH_IPC_SERVICE_SPL_SSL},
        {"spl:fs", SATCHEL_SWITCH_IPC_SERVICE_SPL_FS},
        {"spl:es", SATCHEL_SWITCH_IPC_SERVICE_SPL_ES},
        {"spl:manu", SATCHEL_SWITCH_IPC_SERVICE_SPL_MANU},
        {"ns:am2", SATCHEL_SWITCH_IPC_SERVICE_NS_AM2},
        {"bpc:c", SATCHEL_SWITCH_IPC_SERVICE_BPC_C},
        {"nifm:s", SATCHEL_SWITCH_IPC_SERVICE_NIFM_S},
        {"nifm:a", SATCHEL_SWITCH_IPC_SERVICE_NIFM_A},
        {"bsd:s", SATCHEL_SWITCH_IPC_SERVICE_BSD_S},
        {"ssl:s", SATCHEL_SWITCH_IPC_SERVICE_SSL_S},
        {"pl:s", SATCHEL_SWITCH_IPC_SERVICE_PL_S},
        {"friend:s", SATCHEL_SWITCH_IPC_SERVICE_FRIENDS_S},
        {"fgm:0", SATCHEL_SWITCH_IPC_SERVICE_FGM_0},
        {"fgm:9", SATCHEL_SWITCH_IPC_SERVICE_FGM_9},
        {"nvdrv:s", SATCHEL_SWITCH_IPC_SERVICE_NVDRV_S},
        {"nvdrv:t", SATCHEL_SWITCH_IPC_SERVICE_NVDRV_T},
        {"mm:u", SATCHEL_SWITCH_IPC_SERVICE_MM_U},
        {"caps:u", SATCHEL_SWITCH_IPC_SERVICE_CAPS_U},
        {"caps:a", SATCHEL_SWITCH_IPC_SERVICE_CAPS_A},
        {"caps:c", SATCHEL_SWITCH_IPC_SERVICE_CAPS_C},
        {"caps:dc", SATCHEL_SWITCH_IPC_SERVICE_CAPS_DC},
        {"caps:sc", SATCHEL_SWITCH_IPC_SERVICE_CAPS_SC},
        {"caps:su", SATCHEL_SWITCH_IPC_SERVICE_CAPS_SU},
        {"prepo:u", SATCHEL_SWITCH_IPC_SERVICE_PREPO_U},
        {"prepo:s", SATCHEL_SWITCH_IPC_SERVICE_PREPO_S},
        {"prepo:m", SATCHEL_SWITCH_IPC_SERVICE_PREPO_M},
        {"prepo:a", SATCHEL_SWITCH_IPC_SERVICE_PREPO_A},
        {"prepo:a2", SATCHEL_SWITCH_IPC_SERVICE_PREPO_A2},
        {"prepo:d", SATCHEL_SWITCH_IPC_SERVICE_PREPO_D},
        {"psc:c", SATCHEL_SWITCH_IPC_SERVICE_PSC_C},
        {"psc:l", SATCHEL_SWITCH_IPC_SERVICE_PSC_L},
    {"acc:aa", SATCHEL_SWITCH_IPC_SERVICE_ACC_AA},
    {"acc:su", SATCHEL_SWITCH_IPC_SERVICE_ACC_SU},
    {"acc:u1", SATCHEL_SWITCH_IPC_SERVICE_ACC_U1},
    {"arp:r", SATCHEL_SWITCH_IPC_SERVICE_ARP_R},
    {"arp:w", SATCHEL_SWITCH_IPC_SERVICE_ARP_W},
    {"aud:a", SATCHEL_SWITCH_IPC_SERVICE_AUD_A},
    {"aud:d", SATCHEL_SWITCH_IPC_SERVICE_AUD_D},
    {"audctl", SATCHEL_SWITCH_IPC_SERVICE_AUDCTL},
    {"audout:a", SATCHEL_SWITCH_IPC_SERVICE_AUDOUT_A},
    {"audout:d", SATCHEL_SWITCH_IPC_SERVICE_AUDOUT_D},
    {"audrec:u", SATCHEL_SWITCH_IPC_SERVICE_AUDREC_U},
    {"avm", SATCHEL_SWITCH_IPC_SERVICE_AVM},
    {"bcat:a", SATCHEL_SWITCH_IPC_SERVICE_BCAT_A},
    {"bcat:m", SATCHEL_SWITCH_IPC_SERVICE_BCAT_M},
    {"bcat:s", SATCHEL_SWITCH_IPC_SERVICE_BCAT_S},
    {"bcat:u", SATCHEL_SWITCH_IPC_SERVICE_BCAT_U},
    {"bgtc:sc", SATCHEL_SWITCH_IPC_SERVICE_BGTC_SC},
    {"bgtc:t", SATCHEL_SWITCH_IPC_SERVICE_BGTC_T},
    {"bpc:r", SATCHEL_SWITCH_IPC_SERVICE_BPC_R},
    {"bsdcfg", SATCHEL_SWITCH_IPC_SERVICE_BSDCFG},
    {"bt", SATCHEL_SWITCH_IPC_SERVICE_BT},
    {"btdrv", SATCHEL_SWITCH_IPC_SERVICE_BTDRV},
    {"btm", SATCHEL_SWITCH_IPC_SERVICE_BTM},
    {"btm:dbg", SATCHEL_SWITCH_IPC_SERVICE_BTM_DBG},
    {"btm:sys", SATCHEL_SWITCH_IPC_SERVICE_BTM_SYS},
    {"btm:u", SATCHEL_SWITCH_IPC_SERVICE_BTM_U},
    {"capmtp", SATCHEL_SWITCH_IPC_SERVICE_CAPMTP},
    {"caps:ss", SATCHEL_SWITCH_IPC_SERVICE_CAPS_SS},
    {"cec-mgr", SATCHEL_SWITCH_IPC_SERVICE_CEC_MGR},
    {"clkrst", SATCHEL_SWITCH_IPC_SERVICE_CLKRST},
    {"clkrst:a", SATCHEL_SWITCH_IPC_SERVICE_CLKRST_A},
    {"clkrst:i", SATCHEL_SWITCH_IPC_SERVICE_CLKRST_I},
    {"dauth:0", SATCHEL_SWITCH_IPC_SERVICE_DAUTH_0},
    {"ectx:r", SATCHEL_SWITCH_IPC_SERVICE_ECTX_R},
    {"ectx:w", SATCHEL_SWITCH_IPC_SERVICE_ECTX_W},
    {"erpt:c", SATCHEL_SWITCH_IPC_SERVICE_ERPT_C},
    {"erpt:r", SATCHEL_SWITCH_IPC_SERVICE_ERPT_R},
    {"es", SATCHEL_SWITCH_IPC_SERVICE_ES},
    {"ethc:c", SATCHEL_SWITCH_IPC_SERVICE_ETHC_C},
    {"ethc:i", SATCHEL_SWITCH_IPC_SERVICE_ETHC_I},
    {"eupld:c", SATCHEL_SWITCH_IPC_SERVICE_EUPLD_C},
    {"eupld:r", SATCHEL_SWITCH_IPC_SERVICE_EUPLD_R},
    {"fan", SATCHEL_SWITCH_IPC_SERVICE_FAN},
    {"fatal:p", SATCHEL_SWITCH_IPC_SERVICE_FATAL_P},
    {"fgm:dbg", SATCHEL_SWITCH_IPC_SERVICE_FGM_DBG},
    {"friend:a", SATCHEL_SWITCH_IPC_SERVICE_FRIEND_A},
    {"friend:m", SATCHEL_SWITCH_IPC_SERVICE_FRIEND_M},
    {"friend:v", SATCHEL_SWITCH_IPC_SERVICE_FRIEND_V},
    {"fsp-ldr", SATCHEL_SWITCH_IPC_SERVICE_FSP_LDR},
    {"fsp-pr", SATCHEL_SWITCH_IPC_SERVICE_FSP_PR},
    {"gpio", SATCHEL_SWITCH_IPC_SERVICE_GPIO},
    {"grc:c", SATCHEL_SWITCH_IPC_SERVICE_GRC_C},
    {"grc:d", SATCHEL_SWITCH_IPC_SERVICE_GRC_D},
    {"hid:dbg", SATCHEL_SWITCH_IPC_SERVICE_HID_DBG},
    {"hid:sys", SATCHEL_SWITCH_IPC_SERVICE_HID_SYS},
    {"hidbus", SATCHEL_SWITCH_IPC_SERVICE_HIDBUS},
    {"hshl:set", SATCHEL_SWITCH_IPC_SERVICE_HSHL_SET},
    {"hshl:sys", SATCHEL_SWITCH_IPC_SERVICE_HSHL_SYS},
    {"htcs", SATCHEL_SWITCH_IPC_SERVICE_HTCS},
    {"hwopus", SATCHEL_SWITCH_IPC_SERVICE_HWOPUS},
    {"i2c", SATCHEL_SWITCH_IPC_SERVICE_I2C},
    {"idle:sys", SATCHEL_SWITCH_IPC_SERVICE_IDLE_SYS},
    {"ins:r", SATCHEL_SWITCH_IPC_SERVICE_INS_R},
    {"ins:s", SATCHEL_SWITCH_IPC_SERVICE_INS_S},
    {"irs", SATCHEL_SWITCH_IPC_SERVICE_IRS},
    {"irs:sys", SATCHEL_SWITCH_IPC_SERVICE_IRS_SYS},
    {"lbl", SATCHEL_SWITCH_IPC_SERVICE_LBL},
    {"ldn:m", SATCHEL_SWITCH_IPC_SERVICE_LDN_M},
    {"ldn:s", SATCHEL_SWITCH_IPC_SERVICE_LDN_S},
    {"ldn:u", SATCHEL_SWITCH_IPC_SERVICE_LDN_U},
    {"ldr:dmnt", SATCHEL_SWITCH_IPC_SERVICE_LDR_DMNT},
    {"ldr:pm", SATCHEL_SWITCH_IPC_SERVICE_LDR_PM},
    {"ldr:shel", SATCHEL_SWITCH_IPC_SERVICE_LDR_SHEL},
    {"lp2p:app", SATCHEL_SWITCH_IPC_SERVICE_LP2P_APP},
    {"lp2p:sys", SATCHEL_SWITCH_IPC_SERVICE_LP2P_SYS},
    {"lr", SATCHEL_SWITCH_IPC_SERVICE_LR},
    {"mig:usr", SATCHEL_SWITCH_IPC_SERVICE_MIG_USR},
    {"mii:e", SATCHEL_SWITCH_IPC_SERVICE_MII_E},
    {"mii:u", SATCHEL_SWITCH_IPC_SERVICE_MII_U},
    {"miiimg", SATCHEL_SWITCH_IPC_SERVICE_MIIIMG},
    {"mnpp:app", SATCHEL_SWITCH_IPC_SERVICE_MNPP_APP},
    {"ncm", SATCHEL_SWITCH_IPC_SERVICE_NCM},
    {"news:a", SATCHEL_SWITCH_IPC_SERVICE_NEWS_A},
    {"news:c", SATCHEL_SWITCH_IPC_SERVICE_NEWS_C},
    {"news:m", SATCHEL_SWITCH_IPC_SERVICE_NEWS_M},
    {"news:p", SATCHEL_SWITCH_IPC_SERVICE_NEWS_P},
    {"news:v", SATCHEL_SWITCH_IPC_SERVICE_NEWS_V},
    {"nfc:am", SATCHEL_SWITCH_IPC_SERVICE_NFC_AM},
    {"nfc:mf:u", SATCHEL_SWITCH_IPC_SERVICE_NFC_MF_U},
    {"nfc:sys", SATCHEL_SWITCH_IPC_SERVICE_NFC_SYS},
    {"nfc:user", SATCHEL_SWITCH_IPC_SERVICE_NFC_USER},
    {"nfp:dbg", SATCHEL_SWITCH_IPC_SERVICE_NFP_DBG},
    {"nfp:sys", SATCHEL_SWITCH_IPC_SERVICE_NFP_SYS},
    {"nfp:user", SATCHEL_SWITCH_IPC_SERVICE_NFP_USER},
    {"ngc:u", SATCHEL_SWITCH_IPC_SERVICE_NGC_U},
    {"ngct:s", SATCHEL_SWITCH_IPC_SERVICE_NGCT_S},
    {"ngct:u", SATCHEL_SWITCH_IPC_SERVICE_NGCT_U},
    {"nim", SATCHEL_SWITCH_IPC_SERVICE_NIM},
    {"nim:eca", SATCHEL_SWITCH_IPC_SERVICE_NIM_ECA},
    {"nim:ecas", SATCHEL_SWITCH_IPC_SERVICE_NIM_ECAS},
    {"nim:shp", SATCHEL_SWITCH_IPC_SERVICE_NIM_SHP},
    {"notif:a", SATCHEL_SWITCH_IPC_SERVICE_NOTIF_A},
    {"notif:s", SATCHEL_SWITCH_IPC_SERVICE_NOTIF_S},
    {"npns:s", SATCHEL_SWITCH_IPC_SERVICE_NPNS_S},
    {"npns:u", SATCHEL_SWITCH_IPC_SERVICE_NPNS_U},
    {"ns:dev", SATCHEL_SWITCH_IPC_SERVICE_NS_DEV},
    {"ns:ec", SATCHEL_SWITCH_IPC_SERVICE_NS_EC},
    {"ns:rid", SATCHEL_SWITCH_IPC_SERVICE_NS_RID},
    {"ns:rt", SATCHEL_SWITCH_IPC_SERVICE_NS_RT},
    {"ns:su", SATCHEL_SWITCH_IPC_SERVICE_NS_SU},
    {"ns:vm", SATCHEL_SWITCH_IPC_SERVICE_NS_VM},
    {"ns:web", SATCHEL_SWITCH_IPC_SERVICE_NS_WEB},
    {"nsd:a", SATCHEL_SWITCH_IPC_SERVICE_NSD_A},
    {"nsd:u", SATCHEL_SWITCH_IPC_SERVICE_NSD_U},
    {"ntc", SATCHEL_SWITCH_IPC_SERVICE_NTC},
    {"nvdrvdbg", SATCHEL_SWITCH_IPC_SERVICE_NVDRVDBG},
    {"nvgem:c", SATCHEL_SWITCH_IPC_SERVICE_NVGEM_C},
    {"nvgem:cd", SATCHEL_SWITCH_IPC_SERVICE_NVGEM_CD},
    {"olsc:s", SATCHEL_SWITCH_IPC_SERVICE_OLSC_S},
    {"olsc:u", SATCHEL_SWITCH_IPC_SERVICE_OLSC_U},
    {"omm", SATCHEL_SWITCH_IPC_SERVICE_OMM},
    {"ovln:rcv", SATCHEL_SWITCH_IPC_SERVICE_OVLN_RCV},
    {"ovln:snd", SATCHEL_SWITCH_IPC_SERVICE_OVLN_SND},
    {"pcie", SATCHEL_SWITCH_IPC_SERVICE_PCIE},
    {"pcie:log", SATCHEL_SWITCH_IPC_SERVICE_PCIE_LOG},
    {"pcm", SATCHEL_SWITCH_IPC_SERVICE_PCM},
    {"pcv", SATCHEL_SWITCH_IPC_SERVICE_PCV},
    {"pdm:ntfy", SATCHEL_SWITCH_IPC_SERVICE_PDM_NTFY},
    {"pdm:qry", SATCHEL_SWITCH_IPC_SERVICE_PDM_QRY},
    {"pgl", SATCHEL_SWITCH_IPC_SERVICE_PGL},
    {"pm:bm", SATCHEL_SWITCH_IPC_SERVICE_PM_BM},
    {"pm:dmnt", SATCHEL_SWITCH_IPC_SERVICE_PM_DMNT},
    {"pm:info", SATCHEL_SWITCH_IPC_SERVICE_PM_INFO},
    {"pm:shell", SATCHEL_SWITCH_IPC_SERVICE_PM_SHELL},
    {"psm", SATCHEL_SWITCH_IPC_SERVICE_PSM},
    {"rgltr", SATCHEL_SWITCH_IPC_SERVICE_RGLTR},
    {"ro:dmnt", SATCHEL_SWITCH_IPC_SERVICE_RO_DMNT},
    {"rtc", SATCHEL_SWITCH_IPC_SERVICE_RTC},
    {"set:cal", SATCHEL_SWITCH_IPC_SERVICE_SET_CAL},
    {"set:fd", SATCHEL_SWITCH_IPC_SERVICE_SET_FD},
    {"sm:m", SATCHEL_SWITCH_IPC_SERVICE_SM_M},
    {"spsm", SATCHEL_SWITCH_IPC_SERVICE_SPSM},
    {"srepo:a", SATCHEL_SWITCH_IPC_SERVICE_SREPO_A},
    {"srepo:u", SATCHEL_SWITCH_IPC_SERVICE_SREPO_U},
    {"tc", SATCHEL_SWITCH_IPC_SERVICE_TC},
    {"tcap", SATCHEL_SWITCH_IPC_SERVICE_TCAP},
    {"time:a", SATCHEL_SWITCH_IPC_SERVICE_TIME_A},
    {"time:al", SATCHEL_SWITCH_IPC_SERVICE_TIME_AL},
    {"time:m", SATCHEL_SWITCH_IPC_SERVICE_TIME_M},
    {"time:p", SATCHEL_SWITCH_IPC_SERVICE_TIME_P},
    {"time:r", SATCHEL_SWITCH_IPC_SERVICE_TIME_R},
    {"time:su", SATCHEL_SWITCH_IPC_SERVICE_TIME_SU},
    {"ts", SATCHEL_SWITCH_IPC_SERVICE_TS},
    {"uart", SATCHEL_SWITCH_IPC_SERVICE_UART},
    {"usb:ds", SATCHEL_SWITCH_IPC_SERVICE_USB_DS},
    {"usb:hs", SATCHEL_SWITCH_IPC_SERVICE_USB_HS},
    {"usb:hs:a", SATCHEL_SWITCH_IPC_SERVICE_USB_HS_A},
    {"usb:obsv", SATCHEL_SWITCH_IPC_SERVICE_USB_OBSV},
    {"usb:pd", SATCHEL_SWITCH_IPC_SERVICE_USB_PD},
    {"usb:pd:c", SATCHEL_SWITCH_IPC_SERVICE_USB_PD_C},
    {"usb:pd:m", SATCHEL_SWITCH_IPC_SERVICE_USB_PD_M},
    {"usb:pm", SATCHEL_SWITCH_IPC_SERVICE_USB_PM},
    {"usb:qdb", SATCHEL_SWITCH_IPC_SERVICE_USB_QDB},
    {"vi:s", SATCHEL_SWITCH_IPC_SERVICE_VI_S},
    {"wlan", SATCHEL_SWITCH_IPC_SERVICE_WLAN},
    {"wlan:dtc", SATCHEL_SWITCH_IPC_SERVICE_WLAN_DTC},
    {"wlan:inf", SATCHEL_SWITCH_IPC_SERVICE_WLAN_INF},
    {"wlan:lcl", SATCHEL_SWITCH_IPC_SERVICE_WLAN_LCL},
    {"wlan:lg", SATCHEL_SWITCH_IPC_SERVICE_WLAN_LG},
    {"wlan:lga", SATCHEL_SWITCH_IPC_SERVICE_WLAN_LGA},
    {"wlan:nd", SATCHEL_SWITCH_IPC_SERVICE_WLAN_ND},
    {"wlan:p", SATCHEL_SWITCH_IPC_SERVICE_WLAN_P},
    {"wlan:sg", SATCHEL_SWITCH_IPC_SERVICE_WLAN_SG},
    {"wlan:soc", SATCHEL_SWITCH_IPC_SERVICE_WLAN_SOC},
    {"xcd:sys", SATCHEL_SWITCH_IPC_SERVICE_XCD_SYS},
    };
    uint64_t sessions_before=satchel_switch_process_sm_sessions(p);
    for(size_t i=0;i<sizeof(catalog)/sizeof(catalog[0]);i++) {
        tipc_sm_request(bytes,1,0,(const uint8_t*)catalog[i].name,8);sm_ipc(p,session,bytes,0,0,0);
        CHECK(satchel_switch_process_last_ipc(p,&info) && info.service_kind==catalog[i].kind && info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_SERVICE);
        CHECK(!strcmp(satchel_switch_process_service_name(info.service_kind),catalog[i].name));
        CHECK(satchel_switch_process_sm_sessions(p)==sessions_before);
        // The same exact public label is diagnostic-only through CMIF too.
        sm_request(bytes,1,0,(const uint8_t*)catalog[i].name,4,0,0);sm_ipc(p,session,bytes,0,0,0);
        CHECK(satchel_switch_process_last_ipc(p,&info) && info.service_kind==catalog[i].kind && info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_SERVICE);
        CHECK(!strcmp(satchel_switch_process_service_name(info.service_kind),catalog[i].name));
        CHECK(satchel_switch_process_sm_sessions(p)==sessions_before);
        for(size_t j=0;j<i;j++) CHECK(catalog[i].kind!=catalog[j].kind && strcmp(catalog[i].name,catalog[j].name));
    }
    CHECK(sizeof(info.service_kind)==sizeof(uint32_t));
    CHECK(!strcmp(satchel_switch_process_service_name(UINT32_MAX),"other"));
    CHECK(SATCHEL_SWITCH_IPC_SERVICE_PSC_L==73 && SATCHEL_SWITCH_IPC_SERVICE_ACC_AA==74);
    const uint8_t hostile_names[][8]={{'M','M',':','U'},{'p','r','e','p','o',':','x'},{0xf1,0xf2,0xf3,0xf4,0xf5,0xf6,0xf7,0xf8}};
    for(size_t i=0;i<sizeof(hostile_names)/sizeof(hostile_names[0]);i++) {
        tipc_sm_request(bytes,1,0,hostile_names[i],8);sm_ipc(p,session,bytes,0,0,0);
        CHECK(satchel_switch_process_last_ipc(p,&info)&&info.service_kind==SATCHEL_SWITCH_IPC_SERVICE_OTHER&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_SERVICE);
        CHECK(!strcmp(satchel_switch_process_service_name(info.service_kind),"other"));
    }
    CHECK(SATCHEL_SWITCH_IPC_SERVICE_FGM==33 && SATCHEL_SWITCH_IPC_SERVICE_NVDRV_A==34);
    tipc_sm_request(bytes, 99, 0, NULL, 0); sm_ipc(p, session, bytes, 0, 0, 0);
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.command_id == 99 && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
    memset(bytes, 0, sizeof(bytes)); metadata_word(bytes, 15);
    sm_ipc(p, session, bytes, 0, 0, 0); // No false close-handshake implementation.
    CHECK(satchel_switch_process_is_sm_initialized(p, session));
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.kind == 6 && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
    CHECK(satchel_switch_process_ipc_serviced(p) == before + 6);
    handle_close(p, session, 0); handle_close(p, other, 0);
}

#if defined(SATCHEL_CPU_TESTING)
extern int satchel_switch_process_test_log_matches(const SatchelSwitchProcess *, unsigned, const uint8_t *, size_t);
#endif
static void lm_log_request(uint8_t bytes[256], uint64_t address, uint32_t size, int empty_static) {
    memset(bytes, 0, 256);
    metadata_word(bytes, 4 | (1u << 20) | (empty_static ? 1u << 16 : 0));
    metadata_word(bytes + 4, 8);
    size_t descriptor = empty_static ? 16 : 8;
    metadata_word(bytes + descriptor, size);
    metadata_word(bytes + descriptor + 4, (uint32_t)address);
    metadata_word(bytes + descriptor + 8, (uint32_t)(address >> 32) << 28);
    metadata_word(bytes + 32, 0x49434653);
    memset(bytes + 64, 0xA7, 192);
}
static void lm_packet(uint8_t *packet, size_t size) {
    memset(packet, 0xA6, size);
    packet[16] = 7; packet[17] = 0; packet[18] = 1; packet[19] = 0;
    metadata_word(packet + 20, (uint32_t)(size - 24));
}
static void test_lm(SatchelSwitchProcess *p) {
    uint32_t sm = port_call(p, 0x8001000, 0);
    uint8_t request[256], argument[8] = {'l','m'};
    tipc_sm_request(request, 0, 1, NULL, 0); sm_ipc(p, sm, request, 1, 0, 0);
    tipc_sm_request(request, 1, 0, argument, 8);
    uint32_t lm = lm_reply(p, sm, request, 1, NULL, 0);
    CHECK(satchel_switch_process_is_lm_session(p, lm) && !satchel_switch_process_is_logger(p, lm));
    CHECK(!satchel_switch_process_is_lm_session(NULL, lm) && !satchel_switch_process_is_logger(NULL, lm));
    CHECK(!satchel_switch_process_is_sm_session(p, lm));
    SatchelSwitchIPCInfo info;
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.service_kind == SATCHEL_SWITCH_IPC_SERVICE_LM && info.object_kind == SATCHEL_SWITCH_IPC_OBJECT_SM);
    // Real zero-capacity pointer-buffer query on an ordinary session.
    sm_request(request, 3, 0, argument, 5, 0, 0); metadata_word(request + 4, 8);
    const uint8_t no_pointer_buffer[2] = {0};
    lm_reply(p, lm, request, 0, no_pointer_buffer, sizeof(no_pointer_buffer));
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.object_kind == SATCHEL_SWITCH_IPC_OBJECT_LM);
    sm_request(request, 2, 0, argument, 5, 0, 0); metadata_word(request + 4, 8);
    sm_ipc(p, lm, request, 0, 0, 0); // Clone remains an explicit stop.
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
    sm_request(request, 0, 0, argument, 4, 0, 0);
    sm_ipc(p, lm, request, 0, 0, 0); // OpenLogger must have kernel PID attachment.
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_SCHEMA);
    sm_request(request, 0, 1, argument, 4, 0, 0);
    uint32_t logger = lm_reply(p, lm, request, 1, NULL, 0);
    CHECK(satchel_switch_process_is_logger(p, logger) && !satchel_switch_process_is_lm_session(p, logger));
    handle_close(p, lm, 0); handle_close(p, sm, 0); // Logger keeps its own identity/reference.
    CHECK(satchel_switch_process_is_logger(p, logger));
    CHECK(!satchel_switch_process_log_packets(p) && !satchel_switch_process_log_retained_bytes(p) &&
        !satchel_switch_process_log_dropped_packets(p) && satchel_switch_process_log_destination(p) == 1);
    CHECK(!satchel_switch_process_log_packets(NULL) && !satchel_switch_process_log_retained_bytes(NULL) &&
        !satchel_switch_process_log_dropped_packets(NULL) && !satchel_switch_process_log_destination(NULL));
    SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
    uint8_t packet[SATCHEL_SWITCH_LOG_PACKET_LIMIT];
    size_t lengths[SATCHEL_SWITCH_LOG_PACKET_SLOTS] = {0}, retained = 0;
    const uint64_t address = 0x8002400;
    for (unsigned i = 0; i < 6; i++) {
        size_t size = 32 + i * 8; lm_packet(packet, size);
        CHECK(satchel_cpu_probe_write(cpu, address, packet, size));
        lm_log_request(request, address, (uint32_t)size, i & 1);
        lm_reply(p, logger, request, 0, NULL, 0);
        size_t slot = i % SATCHEL_SWITCH_LOG_PACKET_SLOTS;
        retained -= lengths[slot]; retained += size; lengths[slot] = size;
        CHECK(satchel_switch_process_log_packets(p) == i + 1 && satchel_switch_process_log_retained_bytes(p) == retained);
        CHECK(satchel_switch_process_log_dropped_packets(p) == (i < 4 ? 0 : i - 3));
        CHECK(satchel_switch_process_last_ipc(p, &info) && info.object_kind == SATCHEL_SWITCH_IPC_OBJECT_LOGGER &&
            info.command_id == 0 && info.service_kind == SATCHEL_SWITCH_IPC_SERVICE_LM && !info.blocked_reason);
#if defined(SATCHEL_CPU_TESTING)
        CHECK(satchel_switch_process_test_log_matches(p, (unsigned)slot, packet, size));
#endif
        uint8_t original[80]; CHECK(satchel_cpu_probe_read(cpu, address, original, size) && !memcmp(original, packet, size));
    }
    CHECK(retained == 240);
    // Disabling the sink is actual destination state, with validated drop count.
    memset(argument, 0, sizeof(argument));
    sm_request(request, 1, 0, argument, 4, 0, 0); metadata_word(request + 4, 9);
    lm_reply(p, logger, request, 0, NULL, 0); CHECK(satchel_switch_process_log_destination(p) == 0);
    lm_packet(packet, 32); CHECK(satchel_cpu_probe_write(cpu, address, packet, 32));
    lm_log_request(request, address, 32, 0); lm_reply(p, logger, request, 0, NULL, 0);
    CHECK(satchel_switch_process_log_packets(p) == 7 && satchel_switch_process_log_dropped_packets(p) == 3 &&
        satchel_switch_process_log_retained_bytes(p) == retained);
    argument[0] = 2;
    sm_request(request, 1, 0, argument, 4, 0, 0); metadata_word(request + 4, 9);
    sm_ipc(p, logger, request, 0, 0, 0); CHECK(satchel_switch_process_log_destination(p) == 0);
    argument[0] = 1;
    sm_request(request, 1, 0, argument, 4, 0, 0); metadata_word(request + 4, 9);
    lm_reply(p, logger, request, 0, NULL, 0);
    lm_packet(packet, sizeof(packet)); CHECK(satchel_cpu_probe_write(cpu, address, packet, sizeof(packet)));
    lm_log_request(request, address, sizeof(packet), 1); lm_reply(p, logger, request, 0, NULL, 0);
    retained = retained - lengths[2] + sizeof(packet);
    CHECK(satchel_switch_process_log_packets(p) == 8 && satchel_switch_process_log_dropped_packets(p) == 4 &&
        satchel_switch_process_log_retained_bytes(p) == retained);
#if defined(SATCHEL_CPU_TESTING)
    CHECK(satchel_switch_process_test_log_matches(p, 2, packet, sizeof(packet)));
#endif
    // Every malformed/unsupported packet must preserve sink state and TLS.
    const uint32_t bad_sizes[] = {0, 23, SATCHEL_SWITCH_LOG_PACKET_LIMIT + 1};
    for (size_t i = 0; i < sizeof(bad_sizes) / sizeof(bad_sizes[0]); i++) {
        lm_log_request(request, address, bad_sizes[i], 0); sm_ipc(p, logger, request, 0, 0, 0);
    }
    lm_log_request(request, 0x30000000, 32, 0); sm_ipc(p, logger, request, 0, 0, 0);
    lm_packet(packet, 32); metadata_word(packet + 20, 9);
    CHECK(satchel_cpu_probe_write(cpu, address, packet, 32));
    lm_log_request(request, address, 32, 0); sm_ipc(p, logger, request, 0, 0, 0);
    lm_packet(packet, 32); packet[16] = 3; // Unsupported byte order.
    CHECK(satchel_cpu_probe_write(cpu, address, packet, 32)); sm_ipc(p, logger, request, 0, 0, 0);
    lm_packet(packet, 32); packet[17] = 1;
    CHECK(satchel_cpu_probe_write(cpu, address, packet, 32)); sm_ipc(p, logger, request, 0, 0, 0);
    lm_packet(packet, 32); CHECK(satchel_cpu_probe_write(cpu, address, packet, 32));
    lm_log_request(request, address, 32, 1);
    metadata_word(request + 8, 32 << 16); metadata_word(request + 12, (uint32_t)address);
    sm_ipc(p, logger, request, 0, 0, 0); // Populated X cannot use unallocated pointer buffer.
    lm_log_request(request, address, 32, 0); metadata_word(request, 4 | (1u << 24));
    sm_ipc(p, logger, request, 0, 0, 0); // Receive instead of send descriptor.
    CHECK(satchel_switch_process_log_packets(p) == 8 && satchel_switch_process_log_dropped_packets(p) == 4 &&
        satchel_switch_process_log_retained_bytes(p) == retained);
    handle_close(p, logger, 0); CHECK(!satchel_switch_process_is_logger(p, logger));
    CHECK(satchel_switch_process_log_retained_bytes(p) == retained); // Process-owned sink outlives logger.
}

// Original wire fixtures, independently spelling the public CMIF layout rather
// than asking the production parser/encoder to construct their own test inputs.
static void domain_request(uint8_t bytes[256], uint32_t id, uint32_t command,
                           int pid, const void *argument, size_t size, uint64_t buffer, uint32_t buffer_size) {
    memset(bytes, 0, 256);
    metadata_word(bytes, 4 | (buffer ? 1u << 20 : 0));
    metadata_word(bytes + 4, (uint32_t)((48 + size + 3) / 4) | (pid ? 0x80000000u : 0));
    size_t at = 8;
    if (pid) { metadata_word(bytes + at, 1); at += 12; }
    if (buffer) {
        metadata_word(bytes + at, buffer_size); metadata_word(bytes + at + 4, (uint32_t)buffer);
        metadata_word(bytes + at + 8, (uint32_t)(buffer >> 32) << 28); at += 12;
    }
    at = (at + 15) & ~(size_t)15;
    bytes[at] = 1; bytes[at + 2] = (uint8_t)(16 + size);
    metadata_word(bytes + at + 4, id); metadata_word(bytes + at + 16, 0x49434653);
    metadata_word(bytes + at + 24, command);
    if (size) memcpy(bytes + at + 32, argument, size);
    memset(bytes + 112, 0xA7, 144);
}
static void domain_close_request(uint8_t bytes[256], uint32_t id) {
    memset(bytes, 0, 256); metadata_word(bytes, 4); metadata_word(bytes + 4, 8);
    bytes[16] = 2; metadata_word(bytes + 20, id); memset(bytes + 48, 0xA7, 208);
}
static void domain_reply(SatchelSwitchProcess *p, uint32_t handle, uint8_t bytes[256], uint32_t output_id, int close) {
    SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
    CHECK(satchel_cpu_probe_write(cpu, 0x31000000, bytes, 256));
    CHECK(satchel_cpu_probe_set_pc(cpu, 0x8000014) && satchel_cpu_probe_set_x(cpu, 0, handle));
    SatchelCPUProbeResult result = satchel_switch_process_run(p, 1, NULL, NULL);
    CHECK(result.reason == SATCHEL_CPU_BUDGET && result.steps == 1);
    uint64_t value; CHECK(satchel_cpu_probe_get_x(cpu, 0, &value) && value == 0);
    uint8_t actual[256], expected[52] = {0};
    CHECK(satchel_cpu_probe_read(cpu, 0x31000000, actual, 256));
    if (close) { CHECK(!memcmp(actual, bytes, sizeof(actual))); return; }
    size_t end = output_id ? 52 : 48;
    {
        metadata_word(expected + 4, (uint32_t)((end - 8) / 4));
        metadata_word(expected + 16, output_id ? 1 : 0);
        metadata_word(expected + 32, 0x4f434653);
        if (output_id) metadata_word(expected + 48, output_id);
    }
    CHECK(!memcmp(actual, expected, end));
    CHECK(!memcmp(actual + end, bytes + end, 256 - end));
}
static void test_lm_domain(SatchelSwitchProcess *p) {
    uint8_t bytes[256], argument[8] = {'l','m'};
    uint32_t sm = port_call(p, 0x8001000, 0);
    tipc_sm_request(bytes, 0, 1, NULL, 0); sm_ipc(p, sm, bytes, 1, 0, 0);
    tipc_sm_request(bytes, 1, 0, argument, 8); uint32_t lm = lm_reply(p, sm, bytes, 1, NULL, 0);
    tipc_sm_request(bytes, 1, 0, argument, 8); uint32_t other = lm_reply(p, sm, bytes, 1, NULL, 0);
    CHECK(!satchel_switch_process_is_domain_session(p, lm) && !satchel_switch_process_domain_objects(p, lm));
    CHECK(!satchel_switch_process_is_domain_session(NULL, lm) && !satchel_switch_process_domain_objects(NULL, lm));
    CHECK(!satchel_switch_process_is_domain_session(p, 0) && !satchel_switch_process_domain_objects(p, 0x80));
    // Contextual control is still an ordinary CMIF request, including the
    // actual wireType7/rawWords8 observed on the phone.
    sm_request(bytes, 0, 0, argument, 7, 0, 0); metadata_word(bytes + 4, 8);
    const uint8_t root[4] = {1, 0, 0, 0}; lm_reply(p, lm, bytes, 0, root, 4);
    CHECK(satchel_switch_process_is_domain_session(p, lm) && satchel_switch_process_domain_objects(p, lm) == 1);
    sm_request(bytes, 0, 0, argument, 5, 0, 0); metadata_word(bytes + 4, 8);
    sm_ipc(p, lm, bytes, 0, 0, 0); // Nested domain conversion is unsupported.
    CHECK(satchel_switch_process_domain_objects(p, lm) == 1);
    sm_request(bytes, 3, 0, argument, 5, 0, 0); metadata_word(bytes + 4, 8);
    const uint8_t zero[2] = {0}; lm_reply(p, lm, bytes, 0, zero, 2);
    sm_request(bytes, 4, 0, argument, 5, 0, 0); metadata_word(bytes + 4, 8);
    sm_ipc(p, lm, bytes, 0, 0, 0); // Tagged clone managers remain unsupported.
    sm_request(bytes, 0, 1, argument, 4, 0, 0); sm_ipc(p, lm, bytes, 0, 0, 0); // Wrong protocol after conversion.
    SatchelSwitchIPCInfo info;
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_PARSE);
    // Root and children consume domain IDs, never kernel handle-table entries.
    domain_request(bytes, 1, 0, 0, argument, 8, 0, 0); sm_ipc(p, lm, bytes, 0, 0, 0);
    CHECK(satchel_switch_process_domain_objects(p, lm) == 1);
    for (uint32_t id = 2; id <= SATCHEL_SWITCH_DOMAIN_OBJECT_LIMIT; id++) {
        domain_request(bytes, 1, 0, 1, argument, 8, 0, 0); domain_reply(p, lm, bytes, id, 0);
        CHECK(satchel_switch_process_domain_objects(p, lm) == id);
    }
    domain_request(bytes, 1, 0, 1, argument, 8, 0, 0); sm_ipc(p, lm, bytes, 0, 0, 0);
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_RESOURCE);
    CHECK(satchel_switch_process_domain_objects(p, lm) == SATCHEL_SWITCH_DOMAIN_OBJECT_LIMIT);
    domain_close_request(bytes, 2); domain_reply(p, lm, bytes, 0, 1);
    CHECK(satchel_switch_process_domain_objects(p, lm) == SATCHEL_SWITCH_DOMAIN_OBJECT_LIMIT - 1);
    domain_request(bytes, 1, 0, 1, argument, 8, 0, 0); domain_reply(p, lm, bytes, 33, 0);
    domain_request(bytes, 2, 1, 0, zero, 0, 0, 0); sm_ipc(p, lm, bytes, 0, 0, 0);
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_OBJECT);
    // A second converted session owns a different table. IDs are scoped there.
    sm_request(bytes, 0, 0, argument, 5, 0, 0); metadata_word(bytes + 4, 8); lm_reply(p, other, bytes, 0, root, 4);
    domain_request(bytes, 33, 1, 0, zero, 0, 0, 0); sm_ipc(p, other, bytes, 0, 0, 0);
    CHECK(satchel_switch_process_domain_objects(p, other) == 1);
    // Root release leaves the other live domain objects and transport alive.
    domain_close_request(bytes, 1); domain_reply(p, lm, bytes, 0, 1);
    CHECK(satchel_switch_process_domain_objects(p, lm) == 31);
    domain_request(bytes, 1, 0, 1, argument, 8, 0, 0); sm_ipc(p, lm, bytes, 0, 0, 0);
    uint8_t destination[4] = {1};
    domain_request(bytes, 33, 1, 0, destination, 4, 0, 0);
    metadata_word(bytes, 6); metadata_word(bytes + 28, 0x12345678); metadata_word(bytes + 36, 1);
    domain_reply(p, lm, bytes, 0, 0); // Domain context is not echoed as a reply token.
    uint8_t packet[32]; lm_packet(packet, sizeof(packet));
    CHECK(satchel_cpu_probe_write(satchel_switch_process_cpu(p), 0x8002400, packet, sizeof(packet)));
    uint64_t before = satchel_switch_process_log_packets(p);
    domain_request(bytes, 33, 0, 0, NULL, 0, 0x8002400, sizeof(packet)); domain_reply(p, lm, bytes, 0, 0);
    CHECK(satchel_switch_process_log_packets(p) == before + 1);
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.object_kind == SATCHEL_SWITCH_IPC_OBJECT_LOGGER && info.kind == 3);
    // Domain schema size is exact: extra payload is not accepted as padding.
    domain_request(bytes, 33, 1, 0, argument, 8, 0, 0); sm_ipc(p, lm, bytes, 0, 0, 0);
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_SCHEMA);
    domain_close_request(bytes, 33); metadata_word(bytes + 4, 9); sm_ipc(p, lm, bytes, 0, 0, 0);
    CHECK(satchel_switch_process_domain_objects(p, lm) == 31);
    domain_close_request(bytes, 33); domain_reply(p, lm, bytes, 0, 1);
    domain_close_request(bytes, 33); domain_reply(p, lm, bytes, 0, 1); // Idempotent public close.
    CHECK(satchel_switch_process_domain_objects(p, lm) == 30);
    handle_close(p, lm, 0); CHECK(!satchel_switch_process_is_domain_session(p, lm) && !satchel_switch_process_domain_objects(p, lm));
    CHECK(satchel_switch_process_domain_objects(p, other) == 1);
    handle_close(p, other, 0); handle_close(p, sm, 0);
    // Reallocation of a released table begins a fresh session namespace.
    sm = port_call(p, 0x8001000, 0);
    tipc_sm_request(bytes, 0, 1, NULL, 0); sm_ipc(p, sm, bytes, 1, 0, 0);
    tipc_sm_request(bytes, 1, 0, argument, 8); lm = lm_reply(p, sm, bytes, 1, NULL, 0);
    sm_request(bytes, 0, 0, argument, 5, 0, 0); metadata_word(bytes + 4, 8); lm_reply(p, lm, bytes, 0, root, 4);
    CHECK(satchel_switch_process_domain_objects(p, lm) == 1);
    handle_close(p, lm, 0);
    // A standalone ILogger can itself become the domain root. Converting it
    // must preserve the typed object and its owned process identity.
    tipc_sm_request(bytes, 1, 0, argument, 8); lm = lm_reply(p, sm, bytes, 1, NULL, 0);
    sm_request(bytes, 0, 1, argument, 4, 0, 0); uint32_t logger = lm_reply(p, lm, bytes, 1, NULL, 0);
    sm_request(bytes, 0, 1, argument, 5, 0, 0); metadata_word(bytes + 4, 8u | 0x80000000u);
    sm_ipc(p, logger, bytes, 0, 0, 0); // Conversion rejects a PID attachment atomically.
    CHECK(!satchel_switch_process_is_domain_session(p, logger));
    sm_request(bytes, 0, 0, argument, 7, 1, 0); metadata_word(bytes + 4, 8);
    lm_reply(p, logger, bytes, 0, root, 4);
    CHECK(satchel_switch_process_is_logger(p, logger) && satchel_switch_process_domain_objects(p, logger) == 1);
    handle_close(p, lm, 0); handle_close(p, sm, 0);
    domain_request(bytes, 1, 1, 0, destination, 4, 0, 0); domain_reply(p, logger, bytes, 0, 0);
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.object_kind == SATCHEL_SWITCH_IPC_OBJECT_LOGGER);
    domain_close_request(bytes, 1); domain_reply(p, logger, bytes, 0, 1);
    CHECK(satchel_switch_process_is_domain_session(p, logger) && !satchel_switch_process_domain_objects(p, logger));
    domain_request(bytes, 1, 1, 0, destination, 4, 0, 0); sm_ipc(p, logger, bytes, 0, 0, 0);
    sm_request(bytes, 3, 0, argument, 5, 0, 0); metadata_word(bytes + 4, 8);
    lm_reply(p, logger, bytes, 0, zero, 2); // The empty session still owns transport capacity.
    handle_close(p, logger, 0); CHECK(!satchel_switch_process_is_domain_session(p, logger));
}

typedef struct { uint64_t size, calls; SatchelSwitchStorageStatus fail; } TestStorage;
static uint8_t storage_pattern(uint64_t offset) { return (uint8_t)(offset ^ (offset >> 32) ^ 0x5A); }
static SatchelSwitchStorageStatus test_storage_read(void *context, uint64_t offset, void *output, size_t count) {
    TestStorage *storage = context; storage->calls++;
    CHECK(offset <= storage->size && count <= storage->size - offset && count <= SATCHEL_SWITCH_STORAGE_READ_LIMIT);
    // Deliberately dirty host staging even on failure: the kernel must never
    // commit this unverified partial provider result to either guest output.
    for (size_t i = 0; i < count; i++) ((uint8_t *)output)[i] = storage_pattern(offset + i);
    return storage->fail;
}
static void storage_request_bytes(uint8_t bytes[256], uint32_t id, uint32_t command,
                                  uint64_t offset, uint64_t count, uint64_t output, uint64_t capacity, unsigned mode) {
    uint8_t arguments[16];
    for (unsigned i = 0; i < 8; i++) { arguments[i] = (uint8_t)(offset >> (8 * i)); arguments[8 + i] = (uint8_t)(count >> (8 * i)); }
    if (id) {
        domain_request(bytes, id, command, 0, arguments, command == 0 ? 16 : 0, output, (uint32_t)capacity);
        if (command == 0) {
            metadata_word(bytes, 4 | 1u << 24);
            metadata_word(bytes + 8, (uint32_t)capacity);
            metadata_word(bytes + 12, (uint32_t)output);
            metadata_word(bytes + 16, mode | (uint32_t)(output >> 32) << 28 | (uint32_t)(capacity >> 32) << 24);
        }
    } else {
        memset(bytes, 0, 256); metadata_word(bytes, 4 | (command == 0 ? 1u << 24 : 0));
        metadata_word(bytes + 4, command == 0 ? 12 : 8);
        size_t cmif = command == 0 ? 32 : 16;
        if (command == 0) {
            metadata_word(bytes + 8, (uint32_t)capacity); metadata_word(bytes + 12, (uint32_t)output);
            metadata_word(bytes + 16, mode | (uint32_t)(output >> 32) << 28 | (uint32_t)(capacity >> 32) << 24);
        }
        metadata_word(bytes + cmif, 0x49434653); metadata_word(bytes + cmif + 8, command);
        if (command == 0) memcpy(bytes + cmif + 16, arguments, 16);
        memset(bytes + 112, 0xA7, 144);
    }
}
static void storage_reply(SatchelSwitchProcess *p, uint32_t handle, uint8_t bytes[256], int domain, int size_reply, uint64_t size) {
    SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
    CHECK(satchel_cpu_probe_write(cpu, 0x31000000, bytes, 256));
    CHECK(satchel_cpu_probe_set_pc(cpu, 0x8000014) && satchel_cpu_probe_set_x(cpu, 0, handle));
    SatchelCPUProbeResult result = satchel_switch_process_run(p, 1, NULL, NULL);
    CHECK(result.reason == SATCHEL_CPU_BUDGET && result.steps == 1);
    uint64_t value; CHECK(satchel_cpu_probe_get_x(cpu, 0, &value) && value == 0);
    uint8_t actual[256], expected[56] = {0};
    CHECK(satchel_cpu_probe_read(cpu, 0x31000000, actual, 256));
    size_t cmif = domain ? 32 : 16, end = cmif + 16 + (size_reply ? 8 : 0);
    metadata_word(expected + 4, (uint32_t)((end - 8) / 4)); metadata_word(expected + cmif, 0x4f434653);
    if (size_reply) for (unsigned i = 0; i < 8; i++) expected[cmif + 16 + i] = (uint8_t)(size >> (8 * i));
    CHECK(!memcmp(actual, expected, end)); CHECK(!memcmp(actual + end, bytes + end, 256 - end));
}
#if defined(SATCHEL_CPU_TESTING)
extern void satchel_cpu_probe_test_write_allocation_failure(int fail);
#endif
static void test_storage_ipc(void) {
    const size_t image_size = 0x102000;
    uint8_t *image = calloc(image_size, 1); CHECK(image);
    const uint32_t code[] = {0xd40003e1, 0xd40002c1, 0xd4000181, 0xd40001c1, 0xd40004a1, 0xd4000421};
    memcpy(image, code, sizeof(code)); memcpy(image + 0x1000, "sm:", 4);
    SatchelNSOInfo nso = {.image_size = (uint32_t)image_size, .bss_size = 0xFFFF0,
        .segments = {{0, sizeof(code), 0, sizeof(code)}, {0, 4096, 0x1000, 4096}, {0, 16, 0x2000, 16}}};
    SatchelSwitchProcessModule modules[] = {{"rtld", nso, image, image_size}, {"main", nso, image, image_size}};
    SatchelProcessMetadata metadata = {.program_id = 1, .address_space_size = UINT64_C(1) << 39,
        .main_thread_stack_size = 0x4000, .address_space_bits = 39, .address_space_type = 3,
        .flags0 = 7, .is_64_bit = true, .main_thread_priority = 37, .default_cpu_id = 2};
    char error[256]; SatchelSwitchProcess *p = satchel_switch_process_create(modules, 2, &metadata, error, sizeof(error)); CHECK(p);
    SatchelSwitchProcess *late = satchel_switch_process_create(modules, 2, &metadata, error, sizeof(error)); CHECK(late);
    CHECK(satchel_switch_process_applet_resource_id(p) && satchel_switch_process_applet_resource_id(p) != satchel_switch_process_applet_resource_id(late));
    free(image); SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
    TestStorage backing = {.size = UINT64_C(0x140000000)};
    SatchelSwitchStorage provider = {&backing, backing.size, test_storage_read};
    CHECK(satchel_switch_process_run(late, 0, NULL, NULL).reason == SATCHEL_CPU_BUDGET);
    CHECK(!satchel_switch_process_attach_storage(late, &provider)); satchel_switch_process_destroy(late);
    CHECK(!satchel_switch_process_attach_storage(NULL, &provider) && !satchel_switch_process_attach_storage(p, NULL));
    SatchelSwitchStorage invalid = provider; invalid.read = NULL; CHECK(!satchel_switch_process_attach_storage(p, &invalid));
    invalid = provider; invalid.size = UINT64_MAX; CHECK(!satchel_switch_process_attach_storage(p, &invalid));
    CHECK(satchel_switch_process_attach_storage(p, &provider)); CHECK(!satchel_switch_process_attach_storage(p, &provider));
    provider.read = NULL; provider.size = 0; // Descriptor was copied, not borrowed.
    CHECK(!satchel_switch_process_storage_reads(p) && !satchel_switch_process_storage_bytes(p));
    CHECK(!satchel_switch_process_storage_reads(NULL) && !satchel_switch_process_storage_bytes(NULL));
    uint8_t bytes[256], argument[8] = {'f','s','p','-','s','r','v'};
    uint32_t sm = port_call(p, 0x8001000, 0);
    tipc_sm_request(bytes, 0, 1, NULL, 0); sm_ipc(p, sm, bytes, 1, 0, 0);
    tipc_sm_request(bytes, 1, 0, argument, 8); uint32_t fsp = lm_reply(p, sm, bytes, 1, NULL, 0);
    SatchelSwitchIPCInfo info;
    sm_request(bytes, 200, 0, argument, 4, 0, 0); metadata_word(bytes + 4, 8); sm_ipc(p, fsp, bytes, 0, 0, 0);
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.service_kind == SATCHEL_SWITCH_IPC_SERVICE_FSP && info.object_kind == SATCHEL_SWITCH_IPC_OBJECT_FSP);
    CHECK(info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_SERVICE);
    sm_request(bytes, 1, 0, argument, 4, 0, 0); sm_ipc(p, fsp, bytes, 0, 0, 0);
    sm_request(bytes, 1, 1, argument, 4, 0, 0); lm_reply(p, fsp, bytes, 0, NULL, 0);
    sm_request(bytes, 200, 0, argument, 4, 0, 0); metadata_word(bytes + 4, 8); uint32_t storage = lm_reply(p, fsp, bytes, 1, NULL, 0);
    handle_close(p, fsp, 0); // Child storage owns its provider reference through process lifetime.
    storage_request_bytes(bytes, 0, 4, 0, 0, 0, 0, 0); storage_reply(p, storage, bytes, 0, 1, backing.size);
    CHECK(!backing.calls && !satchel_switch_process_storage_reads(p));
    const uint64_t output = 0x8002400, offset = UINT64_C(0x100000123);
    uint8_t before[128], actual[128], expected[128]; memset(before, 0xCC, sizeof(before));
    CHECK(satchel_cpu_probe_write(cpu, output, before, sizeof(before)));
    storage_request_bytes(bytes, 0, 0, offset, 80, output, 128, 1); storage_reply(p, storage, bytes, 0, 0, 0);
    CHECK(satchel_cpu_probe_read(cpu, output, actual, sizeof(actual)));
    memcpy(expected, before, sizeof(expected)); for (size_t i = 0; i < 80; i++) expected[i] = storage_pattern(offset + i);
    CHECK(!memcmp(actual, expected, sizeof(actual)));
    CHECK(backing.calls == 1 && satchel_switch_process_storage_reads(p) == 1 && satchel_switch_process_storage_bytes(p) == 80);
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.object_kind == SATCHEL_SWITCH_IPC_OBJECT_STORAGE && !info.storage_status);
    // All guest bytes, including TLS, remain unchanged on provider failure.
    for (unsigned failure = SATCHEL_SWITCH_STORAGE_INVALID; failure <= SATCHEL_SWITCH_STORAGE_CANCELLED; failure++) {
        backing.fail = (SatchelSwitchStorageStatus)failure;
        storage_request_bytes(bytes, 0, 0, offset + 8, 80, output, 128, 0); sm_ipc(p, storage, bytes, 0, 0, 0);
        CHECK(satchel_cpu_probe_read(cpu, output, actual, sizeof(actual)) && !memcmp(actual, expected, sizeof(actual)));
        CHECK(satchel_switch_process_last_ipc(p, &info) && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_STORAGE && info.storage_status == failure);
        CHECK(satchel_switch_process_storage_reads(p) == 1 && satchel_switch_process_storage_bytes(p) == 80);
    }
    backing.fail = SATCHEL_SWITCH_STORAGE_OK;
#if defined(SATCHEL_CPU_TESTING)
    storage_request_bytes(bytes, 0, 0, offset + 8, 80, output, 128, 0);
    satchel_cpu_probe_test_write_allocation_failure(1); sm_ipc(p, storage, bytes, 0, 0, 0);
    CHECK(satchel_cpu_probe_read(cpu, output, actual, sizeof(actual)) && !memcmp(actual, expected, sizeof(actual)));
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.storage_write_status == SATCHEL_CPU_WRITE_ALLOCATION);
    CHECK(satchel_switch_process_storage_reads(p) == 1);
#endif
    uint64_t provider_calls = backing.calls;
    const struct { uint64_t offset, count, address, capacity; unsigned mode; } invalid_reads[] = {
        {0, 4, 0x8001000, 4, 0}, // Read-only page.
        {0, 4, 0x8101FFC, 8, 0}, // Prefix valid, advertised tail crosses mapping.
        {0, 4, 0x31000100, 4, 0}, // TLS outside the256-byte IPC envelope.
        {0, 4, 0x31000000, 4, 0}, {0, 0, 0x31000FFF, 1, 0},
        {0, 4, 0x50000000, 4, 0}, {0, 4, output, 3, 0},
        {UINT64_MAX, 1, output, 1, 0}, {backing.size, 1, output, 1, 0},
        {0, SATCHEL_SWITCH_STORAGE_READ_LIMIT + 1, output, SATCHEL_SWITCH_STORAGE_READ_LIMIT + 1, 0},
        {0, 4, output, 4, 3}
    };
    for (size_t i = 0; i < sizeof(invalid_reads) / sizeof(invalid_reads[0]); i++) {
        storage_request_bytes(bytes, 0, 0, invalid_reads[i].offset, invalid_reads[i].count,
            invalid_reads[i].address, invalid_reads[i].capacity, invalid_reads[i].mode);
        sm_ipc(p, storage, bytes, 0, 0, 0); CHECK(backing.calls == provider_calls);
    }
    // Empty EOF reads consult provider/cancellation without writing any data.
    storage_request_bytes(bytes, 0, 0, backing.size, 0, 0, 0, 0); storage_reply(p, storage, bytes, 0, 0, 0);
    CHECK(backing.calls == provider_calls + 1 && satchel_switch_process_storage_reads(p) == 2);
    CHECK(satchel_switch_process_storage_bytes(p) == 80);
    // Actual1MiB limit is accepted and fully copied; no pointer truncation.
    storage_request_bytes(bytes, 0, 0, offset, SATCHEL_SWITCH_STORAGE_READ_LIMIT, 0x8002000, SATCHEL_SWITCH_STORAGE_READ_LIMIT, 0);
    storage_reply(p, storage, bytes, 0, 0, 0);
    uint8_t *large = malloc(SATCHEL_SWITCH_STORAGE_READ_LIMIT); CHECK(large);
    CHECK(satchel_cpu_probe_read(cpu, 0x8002000, large, SATCHEL_SWITCH_STORAGE_READ_LIMIT));
    int matches = 1; for (size_t i = 0; i < SATCHEL_SWITCH_STORAGE_READ_LIMIT; i++) matches &= large[i] == storage_pattern(offset + i);
    CHECK(matches); free(large);
    handle_close(p, storage, 0);
    provider = (SatchelSwitchStorage){&backing, backing.size, test_storage_read}; CHECK(!satchel_switch_process_attach_storage(p, &provider));
    // Domain FSP initialization is shared by cloned sessions, not copied state.
    tipc_sm_request(bytes, 1, 0, argument, 8); fsp = lm_reply(p, sm, bytes, 1, NULL, 0);
    sm_request(bytes, 0, 0, argument, 5, 0, 0); metadata_word(bytes + 4, 8);
    const uint8_t root[4] = {1}; lm_reply(p, fsp, bytes, 0, root, 4);
    sm_request(bytes, 2, 0, argument, 5, 0, 0); metadata_word(bytes + 4, 8); uint32_t clone = lm_reply(p, fsp, bytes, 1, NULL, 0);
    CHECK(satchel_switch_process_domain_objects(p, clone) == 1);
    domain_request(bytes, 1, 200, 0, NULL, 0, 0, 0); sm_ipc(p, clone, bytes, 0, 0, 0);
    domain_request(bytes, 1, 1, 1, argument, 8, 0, 0); domain_reply(p, fsp, bytes, 0, 0);
    domain_request(bytes, 1, 200, 0, NULL, 0, 0, 0); domain_reply(p, clone, bytes, 2, 0);
    CHECK(satchel_switch_process_domain_objects(p, fsp) == 2 && satchel_switch_process_domain_objects(p, clone) == 2);
    handle_close(p, fsp, 0); CHECK(satchel_switch_process_domain_objects(p, clone) == 2);
    storage_request_bytes(bytes, 2, 4, 0, 0, 0, 0, 0); storage_reply(p, clone, bytes, 1, 1, backing.size);
    storage_request_bytes(bytes, 2, 0, offset, 16, output, 16, 1); storage_reply(p, clone, bytes, 1, 0, 0);
    CHECK(satchel_cpu_probe_read(cpu, output, actual, 16));
    matches = 1; for (size_t i = 0; i < 16; i++) matches &= actual[i] == storage_pattern(offset + i); CHECK(matches);
    // Cloning at a full kernel-handle table fails without consuming a token or
    // changing the shared domain; freeing one slot permits the exact retry.
    uint32_t fillers[SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT - 3];
    for (size_t i = 0; i < sizeof(fillers) / sizeof(fillers[0]); i++) fillers[i] = port_call(p, 0x8001000, 0);
    sm_request(bytes, 2, 0, argument, 5, 0, 0); metadata_word(bytes + 4, 8); sm_ipc(p, clone, bytes, 0, 0, 0);
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_RESOURCE);
    CHECK(satchel_switch_process_domain_objects(p, clone) == 2);
    handle_close(p, fillers[0], 0);
    sm_request(bytes, 2, 0, argument, 5, 0, 0); metadata_word(bytes + 4, 8);
    uint32_t replacement = lm_reply(p, clone, bytes, 1, NULL, 0);
    CHECK(replacement == fillers[sizeof(fillers) / sizeof(fillers[0]) - 1] + 1);
    handle_close(p, clone, 0); clone = replacement;
    CHECK(satchel_switch_process_domain_objects(p, clone) == 2);
    for (size_t i = 1; i < sizeof(fillers) / sizeof(fillers[0]); i++) handle_close(p, fillers[i], 0);
    domain_close_request(bytes, 1); domain_reply(p, clone, bytes, 0, 1);
    storage_request_bytes(bytes, 2, 4, 0, 0, 0, 0, 0); storage_reply(p, clone, bytes, 1, 1, backing.size);
    domain_close_request(bytes, 2); domain_reply(p, clone, bytes, 0, 1);
    storage_request_bytes(bytes, 2, 4, 0, 0, 0, 0, 0); sm_ipc(p, clone, bytes, 0, 0, 0);
    handle_close(p, clone, 0); handle_close(p, sm, 0);
    CHECK(!satchel_switch_process_domain_objects(p, clone));
    satchel_switch_process_destroy(p);
}

static void apm_mode_reply(SatchelSwitchProcess *p, uint32_t handle, uint8_t request[256], int domain) {
    if (!domain) { const uint8_t mode[4] = {0}; lm_reply(p, handle, request, 0, mode, sizeof(mode)); return; }
    SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
    CHECK(satchel_cpu_probe_write(cpu, 0x31000000, request, 256));
    CHECK(satchel_cpu_probe_set_pc(cpu, 0x8000014) && satchel_cpu_probe_set_x(cpu, 0, handle));
    CHECK(satchel_switch_process_run(p, 1, NULL, NULL).reason == SATCHEL_CPU_BUDGET);
    uint64_t status; CHECK(satchel_cpu_probe_get_x(cpu, 0, &status) && !status);
    uint8_t reply[256], expected[52] = {0};
    metadata_word(expected + 4, 11); metadata_word(expected + 32, 0x4f434653);
    CHECK(satchel_cpu_probe_read(cpu, 0x31000000, reply, sizeof(reply)));
    CHECK(!memcmp(reply, expected, sizeof(expected)));
    CHECK(!memcmp(reply + sizeof(expected), request + sizeof(expected), 256 - sizeof(expected)));
}
static void test_apm(SatchelSwitchProcess *p) {
    uint8_t request[256], name[8] = {'a','p','m'};
    uint32_t sm = port_call(p, 0x8001000, 0);
    tipc_sm_request(request, 0, 1, NULL, 0); sm_ipc(p, sm, request, 1, 0, 0);
    tipc_sm_request(request, 1, 0, name, 8); uint32_t apm = lm_reply(p, sm, request, 1, NULL, 0);
    CHECK(!satchel_switch_process_is_sm_session(p, apm) && !satchel_switch_process_is_lm_session(p, apm));
    CHECK(satchel_switch_process_performance_mode(p) == SATCHEL_SWITCH_PERFORMANCE_NORMAL);
    CHECK(satchel_switch_process_performance_mode(NULL) == UINT32_MAX);
    sm_request(request, 1, 0, name, 4, 0, 0); metadata_word(request + 4, 8);
    apm_mode_reply(p, apm, request, 0); // Mode can be queried without eagerly creating ISession.
    SatchelSwitchIPCInfo info; CHECK(satchel_switch_process_last_ipc(p, &info) &&
        info.service_kind == SATCHEL_SWITCH_IPC_SERVICE_APM && info.object_kind == SATCHEL_SWITCH_IPC_OBJECT_APM && info.command_id == 1);
    sm_request(request, 1, 1, name, 4, 0, 0); sm_ipc(p, apm, request, 0, 0, 0);
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_SCHEMA);
    sm_request(request, 1, 0, name, 4, 0, 0); sm_ipc(p, apm, request, 0, 0, 0); // Extra args are not ignored.
    lm_log_request(request, 0x8002400, 32, 0); sm_ipc(p, apm, request, 0, 0, 0);
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_SCHEMA);
    sm_request(request, 99, 0, name, 4, 0, 0); metadata_word(request + 4, 8); sm_ipc(p, apm, request, 0, 0, 0);
    sm_request(request, 0, 0, name, 4, 0, 0); metadata_word(request + 4, 8);
    uint32_t session = lm_reply(p, apm, request, 1, NULL, 0);
    // No invented configuration defaults or host frequency changes. Both
    // documented ISession methods remain explicit stops until IDs are modeled.
    sm_request(request, 0, 0, name, 4, 0, 0); sm_ipc(p, session, request, 0, 0, 0);
    sm_request(request, 1, 0, name, 4, 0, 0); metadata_word(request + 4, 9); sm_ipc(p, session, request, 0, 0, 0);
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.object_kind == SATCHEL_SWITCH_IPC_OBJECT_APM_SESSION && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
    CHECK(satchel_switch_process_performance_mode(p) == 0);
    // Four live handles including the main thread leave28 free slots.
    uint32_t fillers[SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT - 4];
    for (size_t i = 0; i < sizeof(fillers) / sizeof(fillers[0]); i++) fillers[i] = port_call(p, 0x8001000, 0);
    sm_request(request, 0, 0, name, 4, 0, 0); metadata_word(request + 4, 8); sm_ipc(p, apm, request, 0, 0, 0);
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_RESOURCE);
    handle_close(p, fillers[0], 0);
    sm_request(request, 0, 0, name, 4, 0, 0); metadata_word(request + 4, 8);
    uint32_t retry = lm_reply(p, apm, request, 1, NULL, 0);
    CHECK(retry == fillers[sizeof(fillers) / sizeof(fillers[0]) - 1] + 1);
    handle_close(p, apm, 0); // Sessions remain owned after their root handle closes.
    sm_request(request, 1, 0, name, 4, 0, 0); metadata_word(request + 4, 9); sm_ipc(p, retry, request, 0, 0, 0);
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.object_kind == SATCHEL_SWITCH_IPC_OBJECT_APM_SESSION);
    handle_close(p, session, 0); handle_close(p, retry, 0);
    for (size_t i = 1; i < sizeof(fillers) / sizeof(fillers[0]); i++) handle_close(p, fillers[i], 0);
    tipc_sm_request(request, 1, 0, name, 8); apm = lm_reply(p, sm, request, 1, NULL, 0);
    sm_request(request, 0, 0, name, 7, 1, 0); metadata_word(request + 4, 8);
    const uint8_t root[4] = {1}; lm_reply(p, apm, request, 0, root, 4);
    domain_request(request, 1, 1, 0, NULL, 0, 0, 0); apm_mode_reply(p, apm, request, 1);
    domain_request(request, 1, 0, 0, NULL, 0, 0, 0); domain_reply(p, apm, request, 2, 0);
    CHECK(satchel_switch_process_domain_objects(p, apm) == 2);
    sm_request(request, 2, 0, name, 5, 0, 0); metadata_word(request + 4, 8);
    uint32_t clone = lm_reply(p, apm, request, 1, NULL, 0); handle_close(p, apm, 0);
    domain_request(request, 1, 1, 0, NULL, 0, 0, 0); apm_mode_reply(p, clone, request, 1);
    domain_close_request(request, 1); domain_reply(p, clone, request, 0, 1);
    domain_request(request, 2, 1, 0, name, 4, 0, 0); sm_ipc(p, clone, request, 0, 0, 0);
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.object_kind == SATCHEL_SWITCH_IPC_OBJECT_APM_SESSION);
    domain_close_request(request, 2); domain_reply(p, clone, request, 0, 1);
    handle_close(p, clone, 0); handle_close(p, sm, 0);
}

// Replaces the former unsupported-catalog aoc:u assertion: creating the owned
// manager is real, while every unimplemented inventory/event method still stops.
static void test_aoc_root(SatchelSwitchProcess *p) {
    uint8_t bytes[256], name[8]={'a','o','c',':','u'}; SatchelSwitchIPCInfo info;
    uint64_t events_before=satchel_switch_process_readable_event_handles(p);
    uint32_t sm=port_call(p,0x8001000,0);
    tipc_sm_request(bytes,0,1,NULL,0);sm_ipc(p,sm,bytes,1,0,0);
    uint32_t roots[2];
    for(unsigned protocol=0;protocol<2;protocol++) {
        if(protocol) sm_request(bytes,1,0,name,4,0,0);
        else tipc_sm_request(bytes,1,0,name,8);
        roots[protocol]=lm_reply(p,sm,bytes,1,NULL,0);
        CHECK(satchel_switch_process_last_ipc(p,&info) && info.service_kind==SATCHEL_SWITCH_IPC_SERVICE_AOC_U);
        CHECK(!strcmp(satchel_switch_process_service_name(info.service_kind),"aoc:u"));
        CHECK(!satchel_switch_process_is_sm_session(p,roots[protocol]) && !satchel_switch_process_is_lm_session(p,roots[protocol]));
        const uint32_t commands[]={0,1,2,3,4,5,6,7,8,9,10,11,12,50,100,101,110,UINT32_MAX};
        for(size_t i=0;i<sizeof(commands)/sizeof(commands[0]);i++) {
            sm_request(bytes,commands[i],0,name,4,0,0);metadata_word(bytes+4,8);
            sm_ipc(p,roots[protocol],bytes,0,0,0);
            CHECK(satchel_switch_process_last_ipc(p,&info) && info.command_id==commands[i] &&
                info.service_kind==SATCHEL_SWITCH_IPC_SERVICE_AOC_U && info.object_kind==SATCHEL_SWITCH_IPC_OBJECT_AOC &&
                info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
            CHECK(satchel_switch_process_readable_event_handles(p)==events_before);
        }
    }
    // Actual domain conversion and clone share one table; closing a handle does
    // not erase its sibling, but domain close removes the object for all clones.
    sm_request(bytes,0,0,name,7,1,0);metadata_word(bytes+4,8);
    const uint8_t root_id[4]={1};lm_reply(p,roots[0],bytes,0,root_id,4);
    CHECK(satchel_switch_process_domain_objects(p,roots[0])==1);
    sm_request(bytes,2,0,name,5,0,0);metadata_word(bytes+4,8);
    uint32_t clone=lm_reply(p,roots[0],bytes,1,NULL,0);handle_close(p,roots[0],0);
    domain_request(bytes,1,8,0,NULL,0,0,0);sm_ipc(p,clone,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info) && info.object_kind==SATCHEL_SWITCH_IPC_OBJECT_AOC && info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
    domain_close_request(bytes,1);domain_reply(p,clone,bytes,0,1);
    CHECK(!satchel_switch_process_domain_objects(p,clone));
    domain_request(bytes,1,8,0,NULL,0,0,0);sm_ipc(p,clone,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info) && info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_OBJECT);
    // Handle exhaustion is atomic and closed tokens are never reused.
    uint32_t fillers[SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT-4];
    for(size_t i=0;i<sizeof(fillers)/sizeof(fillers[0]);i++)fillers[i]=port_call(p,0x8001000,0);
    tipc_sm_request(bytes,1,0,name,8);sm_ipc(p,sm,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info) && info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_RESOURCE);
    handle_close(p,fillers[0],0);tipc_sm_request(bytes,1,0,name,8);
    uint32_t retry=lm_reply(p,sm,bytes,1,NULL,0);CHECK(retry>fillers[sizeof(fillers)/sizeof(fillers[0])-1]);
    handle_close(p,retry,0);handle_close(p,retry,0xE401);
    for(size_t i=1;i<sizeof(fillers)/sizeof(fillers[0]);i++)handle_close(p,fillers[i],0);
    handle_close(p,roots[1],0);handle_close(p,clone,0);handle_close(p,sm,0);
    CHECK(satchel_switch_process_readable_event_handles(p)==events_before);
}

// Owned application error-context connection; no writer-method success.
static void test_ectx_application(SatchelSwitchProcess *p) {
    uint8_t bytes[256], name[8]={'e','c','t','x',':','a','w'}; SatchelSwitchIPCInfo info;
    uint64_t events_before=satchel_switch_process_readable_event_handles(p);
    uint32_t sm=port_call(p,0x8001000,0);
    tipc_sm_request(bytes,0,1,NULL,0);sm_ipc(p,sm,bytes,1,0,0);
    uint32_t roots[2];
    for(unsigned protocol=0;protocol<2;protocol++) {
        if(protocol) sm_request(bytes,1,0,name,4,0,0);
        else tipc_sm_request(bytes,1,0,name,8);
        roots[protocol]=lm_reply(p,sm,bytes,1,NULL,0);
        CHECK(satchel_switch_process_last_ipc(p,&info) && info.service_kind==SATCHEL_SWITCH_IPC_SERVICE_ECTX_AW);
        CHECK(!strcmp(satchel_switch_process_service_name(info.service_kind),"ectx:aw"));
        CHECK(!satchel_switch_process_is_sm_session(p,roots[protocol]) && !satchel_switch_process_is_lm_session(p,roots[protocol]));
        const uint32_t commands[]={0,1,2,3,4,8,10,UINT32_MAX};
        for(size_t i=0;i<sizeof(commands)/sizeof(commands[0]);i++) {
            sm_request(bytes,commands[i],0,name,4,0,0);metadata_word(bytes+4,8);
            sm_ipc(p,roots[protocol],bytes,0,0,0);
            CHECK(satchel_switch_process_last_ipc(p,&info) && info.command_id==commands[i] &&
                info.service_kind==SATCHEL_SWITCH_IPC_SERVICE_ECTX_AW && info.object_kind==SATCHEL_SWITCH_IPC_OBJECT_ECTX_APPLICATION &&
                info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
            CHECK(satchel_switch_process_readable_event_handles(p)==events_before);
        }
    }
    // Personas remain distinct; writer registration cannot unlock system access.
    const uint8_t aliases[2][8]={{'e','c','t','x',':','r'},{'e','c','t','x',':','w'}};
    const uint32_t kinds[2]={SATCHEL_SWITCH_IPC_SERVICE_ECTX_R,SATCHEL_SWITCH_IPC_SERVICE_ECTX_W};
    for(unsigned i=0;i<2;i++) {
        tipc_sm_request(bytes,1,0,aliases[i],8);sm_ipc(p,sm,bytes,0,0,0);
        CHECK(satchel_switch_process_last_ipc(p,&info) && info.service_kind==kinds[i] && info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_SERVICE);
    }
    // Actual domain conversion and clone share one table; closing a handle does
    // not erase its sibling, but domain close removes the object for all clones.
    sm_request(bytes,0,0,name,7,1,0);metadata_word(bytes+4,8);
    const uint8_t root_id[4]={1};lm_reply(p,roots[0],bytes,0,root_id,4);
    CHECK(satchel_switch_process_domain_objects(p,roots[0])==1);
    sm_request(bytes,2,0,name,5,0,0);metadata_word(bytes+4,8);
    uint32_t clone=lm_reply(p,roots[0],bytes,1,NULL,0);handle_close(p,roots[0],0);
    domain_request(bytes,1,8,0,NULL,0,0,0);sm_ipc(p,clone,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info) && info.object_kind==SATCHEL_SWITCH_IPC_OBJECT_ECTX_APPLICATION && info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
    domain_close_request(bytes,1);domain_reply(p,clone,bytes,0,1);
    CHECK(!satchel_switch_process_domain_objects(p,clone));
    domain_request(bytes,1,8,0,NULL,0,0,0);sm_ipc(p,clone,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info) && info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_OBJECT);
    // Handle exhaustion is atomic and closed tokens are never reused.
    uint32_t fillers[SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT-4];
    for(size_t i=0;i<sizeof(fillers)/sizeof(fillers[0]);i++)fillers[i]=port_call(p,0x8001000,0);
    tipc_sm_request(bytes,1,0,name,8);sm_ipc(p,sm,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info) && info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_RESOURCE);
    handle_close(p,fillers[0],0);tipc_sm_request(bytes,1,0,name,8);
    uint32_t retry=lm_reply(p,sm,bytes,1,NULL,0);CHECK(retry>fillers[sizeof(fillers)/sizeof(fillers[0])-1]);
    handle_close(p,retry,0);handle_close(p,retry,0xE401);
    for(size_t i=1;i<sizeof(fillers)/sizeof(fillers[0]);i++)handle_close(p,fillers[i],0);
    handle_close(p,roots[1],0);handle_close(p,clone,0);handle_close(p,sm,0);
    CHECK(satchel_switch_process_readable_event_handles(p)==events_before);
}

// Exact application persona only, with a real but uninitialized child. The
// former pctl catalog-refusal assertion is replaced by this positive journey.
static void test_pctl(SatchelSwitchProcess *p) {
    uint8_t bytes[256],name[8]={'p','c','t','l'},reserved[8];memset(reserved,0xD3,sizeof(reserved));
    SatchelSwitchIPCInfo info;uint64_t events_before=satchel_switch_process_readable_event_handles(p);
    uint32_t sm=port_call(p,0x8001000,0);
    tipc_sm_request(bytes,0,1,NULL,0);sm_ipc(p,sm,bytes,1,0,0);
    uint32_t root=0;
    for(unsigned protocol=0;protocol<2;protocol++) {
        if(protocol)sm_request(bytes,1,0,name,4,0,0);else tipc_sm_request(bytes,1,0,name,8);
        root=lm_reply(p,sm,bytes,1,NULL,0);
        CHECK(satchel_switch_process_last_ipc(p,&info)&&info.service_kind==SATCHEL_SWITCH_IPC_SERVICE_PCTL);
        CHECK(!satchel_switch_process_is_sm_session(p,root)&&!satchel_switch_process_is_lm_session(p,root));
        // Exact public PID+u64 wire schema; forged claims/reserved values cannot
        // choose another process or enable a more privileged factory persona.
        sm_request(bytes,1,1,reserved,4,0,0);uint32_t child=lm_reply(p,root,bytes,1,NULL,0);
        CHECK(satchel_switch_process_last_ipc(p,&info)&&info.object_kind==SATCHEL_SWITCH_IPC_OBJECT_PCTL);
        handle_close(p,root,0);
        const uint32_t commands[]={0,1001,1013,1017,1018,1031,1061,1062,1063,1064,1065,UINT32_MAX};
        for(size_t i=0;i<sizeof(commands)/sizeof(commands[0]);i++) {
            sm_request(bytes,commands[i],0,reserved,4,0,0);metadata_word(bytes+4,8);
            sm_ipc(p,child,bytes,0,0,0);
            CHECK(satchel_switch_process_last_ipc(p,&info)&&info.command_id==commands[i]&&
                info.object_kind==SATCHEL_SWITCH_IPC_OBJECT_PCTL_CHILD&&info.service_kind==SATCHEL_SWITCH_IPC_SERVICE_PCTL&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
        }
        handle_close(p,child,0);handle_close(p,child,0xE401);
    }
    const uint8_t aliases[2][8]={{'p','c','t','l',':','a'},{'p','c','t','l',':','r'}};
    const uint32_t kinds[]={SATCHEL_SWITCH_IPC_SERVICE_PCTL_A,SATCHEL_SWITCH_IPC_SERVICE_PCTL_R};
    for(unsigned i=0;i<2;i++) {
        tipc_sm_request(bytes,1,0,aliases[i],8);
        if(kinds[i]==SATCHEL_SWITCH_IPC_SERVICE_PCTL_A) {
            uint32_t admin=lm_reply(p,sm,bytes,1,NULL,0);
            CHECK(satchel_switch_process_last_ipc(p,&info)&&info.service_kind==SATCHEL_SWITCH_IPC_SERVICE_PCTL_A);
            sm_request(bytes,0,1,reserved,4,0,0);sm_ipc(p,admin,bytes,0,0,0);
            CHECK(satchel_switch_process_last_ipc(p,&info)&&info.object_kind==SATCHEL_SWITCH_IPC_OBJECT_PCTL_ADMIN&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
            handle_close(p,admin,0);
        } else {
            uint32_t readonly=lm_reply(p,sm,bytes,1,NULL,0);
            CHECK(satchel_switch_process_last_ipc(p,&info)&&info.service_kind==SATCHEL_SWITCH_IPC_SERVICE_PCTL_R);
            sm_request(bytes,0,1,reserved,4,0,0);sm_ipc(p,readonly,bytes,0,0,0);
            CHECK(satchel_switch_process_last_ipc(p,&info)&&info.object_kind==SATCHEL_SWITCH_IPC_OBJECT_PCTL_READONLY&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
            handle_close(p,readonly,0);
        }
    }
    tipc_sm_request(bytes,1,0,name,8);root=lm_reply(p,sm,bytes,1,NULL,0);
    sm_request(bytes,0,1,reserved,4,0,0);sm_ipc(p,root,bytes,0,0,0); // Legacy implicit initialization is not implemented.
    CHECK(satchel_switch_process_last_ipc(p,&info)&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
    sm_request(bytes,1,0,reserved,4,0,0);sm_ipc(p,root,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info)&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_SCHEMA);
    sm_request(bytes,1,1,reserved,4,0,0);metadata_word(bytes+4,8|0x80000000u);sm_ipc(p,root,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info)&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_SCHEMA);
    sm_request(bytes,1,1,reserved,4,0,0);metadata_word(bytes+4,11|0x80000000u);sm_ipc(p,root,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info)&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_SCHEMA);
    // Exhaustion leaves the root intact and does not consume the next token.
    uint32_t fillers[SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT-3];
    for(size_t i=0;i<sizeof(fillers)/sizeof(fillers[0]);i++)fillers[i]=port_call(p,0x8001000,0);
    sm_request(bytes,1,1,reserved,4,0,0);sm_ipc(p,root,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info)&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_RESOURCE);
    handle_close(p,fillers[0],0);sm_request(bytes,1,1,reserved,4,0,0);
    uint32_t retry=lm_reply(p,root,bytes,1,NULL,0);CHECK(retry>fillers[sizeof(fillers)/sizeof(fillers[0])-1]);handle_close(p,retry,0);
    for(size_t i=1;i<sizeof(fillers)/sizeof(fillers[0]);i++)handle_close(p,fillers[i],0);
    // Domain children survive root-ID closure; clones share the same identity.
    sm_request(bytes,0,0,name,7,1,0);metadata_word(bytes+4,8);const uint8_t root_id[4]={1};lm_reply(p,root,bytes,0,root_id,4);
    domain_request(bytes,1,1,1,reserved,8,0,0);domain_reply(p,root,bytes,2,0);
    CHECK(satchel_switch_process_domain_objects(p,root)==2);
    sm_request(bytes,2,0,name,5,0,0);metadata_word(bytes+4,8);uint32_t clone=lm_reply(p,root,bytes,1,NULL,0);handle_close(p,root,0);
    domain_close_request(bytes,1);domain_reply(p,clone,bytes,0,1);
    domain_request(bytes,2,1001,0,NULL,0,0,0);sm_ipc(p,clone,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info)&&info.object_kind==SATCHEL_SWITCH_IPC_OBJECT_PCTL_CHILD&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
    domain_close_request(bytes,2);domain_reply(p,clone,bytes,0,1);CHECK(!satchel_switch_process_domain_objects(p,clone));
    handle_close(p,clone,0);handle_close(p,sm,0);CHECK(satchel_switch_process_readable_event_handles(p)==events_before);
}

// Separate system persona: same verified construction ABI, distinct owned types.
static void test_pctl_system(SatchelSwitchProcess *p) {
    uint8_t bytes[256],name[8]={'p','c','t','l',':','s'},reserved[8];memset(reserved,0xD3,sizeof(reserved));
    SatchelSwitchIPCInfo info;uint64_t events_before=satchel_switch_process_readable_event_handles(p);
    uint32_t sm=port_call(p,0x8001000,0);
    tipc_sm_request(bytes,0,1,NULL,0);sm_ipc(p,sm,bytes,1,0,0);
    uint32_t root=0;
    for(unsigned protocol=0;protocol<2;protocol++) {
        if(protocol)sm_request(bytes,1,0,name,4,0,0);else tipc_sm_request(bytes,1,0,name,8);
        root=lm_reply(p,sm,bytes,1,NULL,0);
        CHECK(satchel_switch_process_last_ipc(p,&info)&&info.service_kind==SATCHEL_SWITCH_IPC_SERVICE_PCTL_S);
        CHECK(!satchel_switch_process_is_sm_session(p,root)&&!satchel_switch_process_is_lm_session(p,root));
        // Exact public PID+u64 wire schema; forged claims/reserved values cannot
        // choose another process or change the distinct system factory persona.
        sm_request(bytes,1,1,reserved,4,0,0);uint32_t child=lm_reply(p,root,bytes,1,NULL,0);
        CHECK(satchel_switch_process_last_ipc(p,&info)&&info.object_kind==SATCHEL_SWITCH_IPC_OBJECT_PCTL_SYSTEM);
        handle_close(p,root,0);
        const uint32_t commands[]={0,1001,1013,1017,1018,1031,1061,1062,1063,1064,1065,UINT32_MAX};
        for(size_t i=0;i<sizeof(commands)/sizeof(commands[0]);i++) {
            sm_request(bytes,commands[i],0,reserved,4,0,0);metadata_word(bytes+4,8);
            sm_ipc(p,child,bytes,0,0,0);
            CHECK(satchel_switch_process_last_ipc(p,&info)&&info.command_id==commands[i]&&
                info.object_kind==SATCHEL_SWITCH_IPC_OBJECT_PCTL_SYSTEM_CHILD&&info.service_kind==SATCHEL_SWITCH_IPC_SERVICE_PCTL_S&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
        }
        handle_close(p,child,0);handle_close(p,child,0xE401);
    }
    const uint8_t aliases[2][8]={{'p','c','t','l',':','a'},{'p','c','t','l',':','r'}};
    const uint32_t kinds[]={SATCHEL_SWITCH_IPC_SERVICE_PCTL_A,SATCHEL_SWITCH_IPC_SERVICE_PCTL_R};
    for(unsigned i=0;i<2;i++) {
        tipc_sm_request(bytes,1,0,aliases[i],8);
        if(kinds[i]==SATCHEL_SWITCH_IPC_SERVICE_PCTL_A) {
            uint32_t admin=lm_reply(p,sm,bytes,1,NULL,0);
            CHECK(satchel_switch_process_last_ipc(p,&info)&&info.service_kind==SATCHEL_SWITCH_IPC_SERVICE_PCTL_A);
            sm_request(bytes,0,1,reserved,4,0,0);sm_ipc(p,admin,bytes,0,0,0);
            CHECK(satchel_switch_process_last_ipc(p,&info)&&info.object_kind==SATCHEL_SWITCH_IPC_OBJECT_PCTL_ADMIN&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
            handle_close(p,admin,0);
        } else {
            uint32_t readonly=lm_reply(p,sm,bytes,1,NULL,0);
            CHECK(satchel_switch_process_last_ipc(p,&info)&&info.service_kind==SATCHEL_SWITCH_IPC_SERVICE_PCTL_R);
            sm_request(bytes,0,1,reserved,4,0,0);sm_ipc(p,readonly,bytes,0,0,0);
            CHECK(satchel_switch_process_last_ipc(p,&info)&&info.object_kind==SATCHEL_SWITCH_IPC_OBJECT_PCTL_READONLY&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
            handle_close(p,readonly,0);
        }
    }
    tipc_sm_request(bytes,1,0,name,8);root=lm_reply(p,sm,bytes,1,NULL,0);
    sm_request(bytes,0,1,reserved,4,0,0);sm_ipc(p,root,bytes,0,0,0); // Legacy implicit initialization is not implemented.
    CHECK(satchel_switch_process_last_ipc(p,&info)&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
    sm_request(bytes,1,0,reserved,4,0,0);sm_ipc(p,root,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info)&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_SCHEMA);
    sm_request(bytes,1,1,reserved,4,0,0);metadata_word(bytes+4,8|0x80000000u);sm_ipc(p,root,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info)&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_SCHEMA);
    sm_request(bytes,1,1,reserved,4,0,0);metadata_word(bytes+4,11|0x80000000u);sm_ipc(p,root,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info)&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_SCHEMA);
    // Exhaustion leaves the root intact and does not consume the next token.
    uint32_t fillers[SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT-3];
    for(size_t i=0;i<sizeof(fillers)/sizeof(fillers[0]);i++)fillers[i]=port_call(p,0x8001000,0);
    sm_request(bytes,1,1,reserved,4,0,0);sm_ipc(p,root,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info)&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_RESOURCE);
    handle_close(p,fillers[0],0);sm_request(bytes,1,1,reserved,4,0,0);
    uint32_t retry=lm_reply(p,root,bytes,1,NULL,0);CHECK(retry>fillers[sizeof(fillers)/sizeof(fillers[0])-1]);handle_close(p,retry,0);
    for(size_t i=1;i<sizeof(fillers)/sizeof(fillers[0]);i++)handle_close(p,fillers[i],0);
    // Domain children survive root-ID closure; clones share the same identity.
    sm_request(bytes,0,0,name,7,1,0);metadata_word(bytes+4,8);const uint8_t root_id[4]={1};lm_reply(p,root,bytes,0,root_id,4);
    domain_request(bytes,1,1,1,reserved,8,0,0);domain_reply(p,root,bytes,2,0);
    CHECK(satchel_switch_process_domain_objects(p,root)==2);
    sm_request(bytes,2,0,name,5,0,0);metadata_word(bytes+4,8);uint32_t clone=lm_reply(p,root,bytes,1,NULL,0);handle_close(p,root,0);
    domain_close_request(bytes,1);domain_reply(p,clone,bytes,0,1);
    domain_request(bytes,2,1001,0,NULL,0,0,0);sm_ipc(p,clone,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info)&&info.object_kind==SATCHEL_SWITCH_IPC_OBJECT_PCTL_SYSTEM_CHILD&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
    domain_close_request(bytes,2);domain_reply(p,clone,bytes,0,1);CHECK(!satchel_switch_process_domain_objects(p,clone));
    handle_close(p,clone,0);handle_close(p,sm,0);CHECK(satchel_switch_process_readable_event_handles(p)==events_before);
}

// Admin persona0x83BE: construction only, with no policy approvals.
static void test_pctl_admin(SatchelSwitchProcess *p) {
    uint8_t bytes[256],name[8]={'p','c','t','l',':','a'},reserved[8];memset(reserved,0xD3,sizeof(reserved));
    SatchelSwitchIPCInfo info;uint64_t events_before=satchel_switch_process_readable_event_handles(p);
    uint32_t sm=port_call(p,0x8001000,0);
    tipc_sm_request(bytes,0,1,NULL,0);sm_ipc(p,sm,bytes,1,0,0);
    uint32_t root=0;
    for(unsigned protocol=0;protocol<2;protocol++) {
        if(protocol)sm_request(bytes,1,0,name,4,0,0);else tipc_sm_request(bytes,1,0,name,8);
        root=lm_reply(p,sm,bytes,1,NULL,0);
        CHECK(satchel_switch_process_last_ipc(p,&info)&&info.service_kind==SATCHEL_SWITCH_IPC_SERVICE_PCTL_A);
        CHECK(!satchel_switch_process_is_sm_session(p,root)&&!satchel_switch_process_is_lm_session(p,root));
        // Exact public PID+u64 wire schema; forged claims/reserved values cannot
        // choose another process or change the distinct admin factory persona.
        sm_request(bytes,1,1,reserved,4,0,0);uint32_t child=lm_reply(p,root,bytes,1,NULL,0);
        CHECK(satchel_switch_process_last_ipc(p,&info)&&info.object_kind==SATCHEL_SWITCH_IPC_OBJECT_PCTL_ADMIN);
        handle_close(p,root,0);
        const uint32_t commands[]={0,1001,1013,1017,1018,1031,1061,1062,1063,1064,1065,UINT32_MAX};
        for(size_t i=0;i<sizeof(commands)/sizeof(commands[0]);i++) {
            sm_request(bytes,commands[i],0,reserved,4,0,0);metadata_word(bytes+4,8);
            sm_ipc(p,child,bytes,0,0,0);
            CHECK(satchel_switch_process_last_ipc(p,&info)&&info.command_id==commands[i]&&
                info.object_kind==SATCHEL_SWITCH_IPC_OBJECT_PCTL_ADMIN_CHILD&&info.service_kind==SATCHEL_SWITCH_IPC_SERVICE_PCTL_A&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
        }
        handle_close(p,child,0);handle_close(p,child,0xE401);
    }
    const uint8_t aliases[2][8]={{'p','c','t','l',':','a'},{'p','c','t','l',':','r'}};
    const uint32_t kinds[]={SATCHEL_SWITCH_IPC_SERVICE_PCTL_A,SATCHEL_SWITCH_IPC_SERVICE_PCTL_R};
    for(unsigned i=0;i<2;i++) {
        tipc_sm_request(bytes,1,0,aliases[i],8);
        if(kinds[i]==SATCHEL_SWITCH_IPC_SERVICE_PCTL_A) {
            uint32_t admin=lm_reply(p,sm,bytes,1,NULL,0);
            CHECK(satchel_switch_process_last_ipc(p,&info)&&info.service_kind==SATCHEL_SWITCH_IPC_SERVICE_PCTL_A);
            sm_request(bytes,0,1,reserved,4,0,0);sm_ipc(p,admin,bytes,0,0,0);
            CHECK(satchel_switch_process_last_ipc(p,&info)&&info.object_kind==SATCHEL_SWITCH_IPC_OBJECT_PCTL_ADMIN&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
            handle_close(p,admin,0);
        } else {
            uint32_t readonly=lm_reply(p,sm,bytes,1,NULL,0);
            CHECK(satchel_switch_process_last_ipc(p,&info)&&info.service_kind==SATCHEL_SWITCH_IPC_SERVICE_PCTL_R);
            sm_request(bytes,0,1,reserved,4,0,0);sm_ipc(p,readonly,bytes,0,0,0);
            CHECK(satchel_switch_process_last_ipc(p,&info)&&info.object_kind==SATCHEL_SWITCH_IPC_OBJECT_PCTL_READONLY&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
            handle_close(p,readonly,0);
        }
    }
    tipc_sm_request(bytes,1,0,name,8);root=lm_reply(p,sm,bytes,1,NULL,0);
    sm_request(bytes,0,1,reserved,4,0,0);sm_ipc(p,root,bytes,0,0,0); // Legacy implicit initialization is not implemented.
    CHECK(satchel_switch_process_last_ipc(p,&info)&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
    sm_request(bytes,1,0,reserved,4,0,0);sm_ipc(p,root,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info)&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_SCHEMA);
    sm_request(bytes,1,1,reserved,4,0,0);metadata_word(bytes+4,8|0x80000000u);sm_ipc(p,root,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info)&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_SCHEMA);
    sm_request(bytes,1,1,reserved,4,0,0);metadata_word(bytes+4,11|0x80000000u);sm_ipc(p,root,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info)&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_SCHEMA);
    // Exhaustion leaves the root intact and does not consume the next token.
    uint32_t fillers[SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT-3];
    for(size_t i=0;i<sizeof(fillers)/sizeof(fillers[0]);i++)fillers[i]=port_call(p,0x8001000,0);
    sm_request(bytes,1,1,reserved,4,0,0);sm_ipc(p,root,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info)&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_RESOURCE);
    handle_close(p,fillers[0],0);sm_request(bytes,1,1,reserved,4,0,0);
    uint32_t retry=lm_reply(p,root,bytes,1,NULL,0);CHECK(retry>fillers[sizeof(fillers)/sizeof(fillers[0])-1]);handle_close(p,retry,0);
    for(size_t i=1;i<sizeof(fillers)/sizeof(fillers[0]);i++)handle_close(p,fillers[i],0);
    // Domain children survive root-ID closure; clones share the same identity.
    sm_request(bytes,0,0,name,7,1,0);metadata_word(bytes+4,8);const uint8_t root_id[4]={1};lm_reply(p,root,bytes,0,root_id,4);
    domain_request(bytes,1,1,1,reserved,8,0,0);domain_reply(p,root,bytes,2,0);
    CHECK(satchel_switch_process_domain_objects(p,root)==2);
    sm_request(bytes,2,0,name,5,0,0);metadata_word(bytes+4,8);uint32_t clone=lm_reply(p,root,bytes,1,NULL,0);handle_close(p,root,0);
    domain_close_request(bytes,1);domain_reply(p,clone,bytes,0,1);
    domain_request(bytes,2,1001,0,NULL,0,0,0);sm_ipc(p,clone,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info)&&info.object_kind==SATCHEL_SWITCH_IPC_OBJECT_PCTL_ADMIN_CHILD&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
    domain_close_request(bytes,2);domain_reply(p,clone,bytes,0,1);CHECK(!satchel_switch_process_domain_objects(p,clone));
    handle_close(p,clone,0);handle_close(p,sm,0);CHECK(satchel_switch_process_readable_event_handles(p)==events_before);
}

// Read-only persona0x8040: construction only, with no policy approvals.
static void test_pctl_readonly(SatchelSwitchProcess *p) {
    uint8_t bytes[256],name[8]={'p','c','t','l',':','r'},reserved[8];memset(reserved,0xD3,sizeof(reserved));
    SatchelSwitchIPCInfo info;uint64_t events_before=satchel_switch_process_readable_event_handles(p);
    uint32_t sm=port_call(p,0x8001000,0);
    tipc_sm_request(bytes,0,1,NULL,0);sm_ipc(p,sm,bytes,1,0,0);
    uint32_t root=0;
    for(unsigned protocol=0;protocol<2;protocol++) {
        if(protocol)sm_request(bytes,1,0,name,4,0,0);else tipc_sm_request(bytes,1,0,name,8);
        root=lm_reply(p,sm,bytes,1,NULL,0);
        CHECK(satchel_switch_process_last_ipc(p,&info)&&info.service_kind==SATCHEL_SWITCH_IPC_SERVICE_PCTL_R);
        CHECK(!satchel_switch_process_is_sm_session(p,root)&&!satchel_switch_process_is_lm_session(p,root));
        // Exact public PID+u64 wire schema; forged claims/reserved values cannot
        // choose another process or change the distinct read-only factory persona.
        sm_request(bytes,1,1,reserved,4,0,0);uint32_t child=lm_reply(p,root,bytes,1,NULL,0);
        CHECK(satchel_switch_process_last_ipc(p,&info)&&info.object_kind==SATCHEL_SWITCH_IPC_OBJECT_PCTL_READONLY);
        handle_close(p,root,0);
        const uint32_t commands[]={0,1001,1013,1017,1018,1031,1061,1062,1063,1064,1065,UINT32_MAX};
        for(size_t i=0;i<sizeof(commands)/sizeof(commands[0]);i++) {
            sm_request(bytes,commands[i],0,reserved,4,0,0);metadata_word(bytes+4,8);
            sm_ipc(p,child,bytes,0,0,0);
            CHECK(satchel_switch_process_last_ipc(p,&info)&&info.command_id==commands[i]&&
                info.object_kind==SATCHEL_SWITCH_IPC_OBJECT_PCTL_READONLY_CHILD&&info.service_kind==SATCHEL_SWITCH_IPC_SERVICE_PCTL_R&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
        }
        handle_close(p,child,0);handle_close(p,child,0xE401);
    }
    const uint8_t aliases[2][8]={{'p','c','t','l',':','a'},{'p','c','t','l',':','r'}};
    const uint32_t kinds[]={SATCHEL_SWITCH_IPC_SERVICE_PCTL_A,SATCHEL_SWITCH_IPC_SERVICE_PCTL_R};
    for(unsigned i=0;i<2;i++) {
        tipc_sm_request(bytes,1,0,aliases[i],8);
        if(kinds[i]==SATCHEL_SWITCH_IPC_SERVICE_PCTL_A) {
            uint32_t admin=lm_reply(p,sm,bytes,1,NULL,0);
            CHECK(satchel_switch_process_last_ipc(p,&info)&&info.service_kind==SATCHEL_SWITCH_IPC_SERVICE_PCTL_A);
            sm_request(bytes,0,1,reserved,4,0,0);sm_ipc(p,admin,bytes,0,0,0);
            CHECK(satchel_switch_process_last_ipc(p,&info)&&info.object_kind==SATCHEL_SWITCH_IPC_OBJECT_PCTL_ADMIN&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
            handle_close(p,admin,0);
        } else {
            uint32_t readonly=lm_reply(p,sm,bytes,1,NULL,0);
            CHECK(satchel_switch_process_last_ipc(p,&info)&&info.service_kind==SATCHEL_SWITCH_IPC_SERVICE_PCTL_R);
            sm_request(bytes,0,1,reserved,4,0,0);sm_ipc(p,readonly,bytes,0,0,0);
            CHECK(satchel_switch_process_last_ipc(p,&info)&&info.object_kind==SATCHEL_SWITCH_IPC_OBJECT_PCTL_READONLY&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
            handle_close(p,readonly,0);
        }
    }
    tipc_sm_request(bytes,1,0,name,8);root=lm_reply(p,sm,bytes,1,NULL,0);
    sm_request(bytes,0,1,reserved,4,0,0);sm_ipc(p,root,bytes,0,0,0); // Legacy implicit initialization is not implemented.
    CHECK(satchel_switch_process_last_ipc(p,&info)&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
    sm_request(bytes,1,0,reserved,4,0,0);sm_ipc(p,root,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info)&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_SCHEMA);
    sm_request(bytes,1,1,reserved,4,0,0);metadata_word(bytes+4,8|0x80000000u);sm_ipc(p,root,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info)&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_SCHEMA);
    sm_request(bytes,1,1,reserved,4,0,0);metadata_word(bytes+4,11|0x80000000u);sm_ipc(p,root,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info)&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_SCHEMA);
    // Exhaustion leaves the root intact and does not consume the next token.
    uint32_t fillers[SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT-3];
    for(size_t i=0;i<sizeof(fillers)/sizeof(fillers[0]);i++)fillers[i]=port_call(p,0x8001000,0);
    sm_request(bytes,1,1,reserved,4,0,0);sm_ipc(p,root,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info)&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_RESOURCE);
    handle_close(p,fillers[0],0);sm_request(bytes,1,1,reserved,4,0,0);
    uint32_t retry=lm_reply(p,root,bytes,1,NULL,0);CHECK(retry>fillers[sizeof(fillers)/sizeof(fillers[0])-1]);handle_close(p,retry,0);
    for(size_t i=1;i<sizeof(fillers)/sizeof(fillers[0]);i++)handle_close(p,fillers[i],0);
    // Domain children survive root-ID closure; clones share the same identity.
    sm_request(bytes,0,0,name,7,1,0);metadata_word(bytes+4,8);const uint8_t root_id[4]={1};lm_reply(p,root,bytes,0,root_id,4);
    domain_request(bytes,1,1,1,reserved,8,0,0);domain_reply(p,root,bytes,2,0);
    CHECK(satchel_switch_process_domain_objects(p,root)==2);
    sm_request(bytes,2,0,name,5,0,0);metadata_word(bytes+4,8);uint32_t clone=lm_reply(p,root,bytes,1,NULL,0);handle_close(p,root,0);
    domain_close_request(bytes,1);domain_reply(p,clone,bytes,0,1);
    domain_request(bytes,2,1001,0,NULL,0,0,0);sm_ipc(p,clone,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info)&&info.object_kind==SATCHEL_SWITCH_IPC_OBJECT_PCTL_READONLY_CHILD&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
    domain_close_request(bytes,2);domain_reply(p,clone,bytes,0,1);CHECK(!satchel_switch_process_domain_objects(p,clone));
    handle_close(p,clone,0);handle_close(p,sm,0);CHECK(satchel_switch_process_readable_event_handles(p)==events_before);
}

static void session_close_call(SatchelSwitchProcess *p,uint32_t handle,int first,uint32_t expected) {
    uint8_t request[256],actual[256];memset(request,0xa7,sizeof(request));metadata_word(request,2);metadata_word(request+4,0);
    SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);uint64_t before=satchel_switch_process_ipc_serviced(p),closed=satchel_switch_process_session_closes(p),value;
    CHECK(satchel_cpu_probe_write(cpu,0x31000000,request,sizeof(request)));
    CHECK(satchel_cpu_probe_set_pc(cpu,0x8000014)&&satchel_cpu_probe_set_x(cpu,0,handle)&&satchel_cpu_probe_set_x(cpu,1,0x7654321));
    SatchelCPUProbeResult result=satchel_switch_process_run(p,1,NULL,NULL);
    CHECK(result.reason==SATCHEL_CPU_BUDGET && result.steps==1);
    CHECK(satchel_cpu_probe_get_x(cpu,0,&value)&&value==expected);
    CHECK(satchel_cpu_probe_get_x(cpu,1,&value)&&value==0x7654321);
    CHECK(satchel_cpu_probe_read(cpu,0x31000000,actual,sizeof(actual))&&!memcmp(request,actual,sizeof(actual)));
    CHECK(satchel_switch_process_ipc_serviced(p)==before+(first?1:0));
    CHECK(satchel_switch_process_session_closes(p)==closed+(first?1:0));
    if(first) {SatchelSwitchIPCInfo info;CHECK(satchel_switch_process_last_ipc(p,&info)&&info.kind==2&&!info.blocked_reason);}
}
static void test_hipc_session_close(SatchelSwitchProcess *p) {
    uint8_t bytes[256],name[8]={'p','c','t','l',':','s'},reserved[8]={0};SatchelSwitchIPCInfo info;
    CHECK(!satchel_switch_process_session_closes(NULL)&&!satchel_switch_process_closed_sessions(NULL)&&!satchel_switch_process_is_session_closed(NULL,0x80));
    uint32_t sm=port_call(p,0x8001000,0);tipc_sm_request(bytes,0,1,NULL,0);sm_ipc(p,sm,bytes,1,0,0);
    tipc_sm_request(bytes,1,0,name,8);uint32_t root=lm_reply(p,sm,bytes,1,NULL,0);
    sm_request(bytes,1,1,reserved,4,0,0);uint32_t child=lm_reply(p,root,bytes,1,NULL,0);
    // Every nonempty close header is rejected without destroying its endpoint.
    for(uint32_t bad=1;bad<=4;bad++) {
        memset(bytes,0,sizeof(bytes));metadata_word(bytes,2);metadata_word(bytes+4,bad);
        sm_ipc(p,root,bytes,0,0,0);CHECK(!satchel_switch_process_is_session_closed(p,root));
        CHECK(satchel_switch_process_last_ipc(p,&info)&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_PARSE);
    }
    session_close_call(p,root,1,0xF601);
    CHECK(satchel_switch_process_is_session_closed(p,root)&&satchel_switch_process_closed_sessions(p)==1);
    session_close_call(p,root,0,0xF601); // Already closed is a kernel error, not another server dispatch.
    // Child has its own endpoint and survives the factory closure; policy stays unsupported.
    sm_request(bytes,1001,0,reserved,4,0,0);metadata_word(bytes+4,8);sm_ipc(p,child,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info)&&info.object_kind==SATCHEL_SWITCH_IPC_OBJECT_PCTL_SYSTEM_CHILD&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
    handle_close(p,root,0);CHECK(!satchel_switch_process_is_session_closed(p,root)&&!satchel_switch_process_closed_sessions(p));
    session_close_call(p,root,0,0xE401);handle_close(p,root,0xE401);
    session_close_call(p,child,1,0xF601);handle_close(p,child,0);
    // Domain clones are separate sessions referencing one actual object table.
    tipc_sm_request(bytes,1,0,name,8);root=lm_reply(p,sm,bytes,1,NULL,0);
    sm_request(bytes,0,0,reserved,5,0,0);metadata_word(bytes+4,8);const uint8_t root_id[4]={1};lm_reply(p,root,bytes,0,root_id,4);
    domain_request(bytes,1,1,1,reserved,8,0,0);domain_reply(p,root,bytes,2,0);
    sm_request(bytes,2,0,reserved,5,0,0);metadata_word(bytes+4,8);uint32_t clone=lm_reply(p,root,bytes,1,NULL,0);
    session_close_call(p,root,1,0xF601);CHECK(!satchel_switch_process_domain_objects(p,root)&&satchel_switch_process_domain_objects(p,clone)==2);
    domain_request(bytes,2,1001,0,NULL,0,0,0);sm_ipc(p,clone,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info)&&info.object_kind==SATCHEL_SWITCH_IPC_OBJECT_PCTL_SYSTEM_CHILD);
    handle_close(p,root,0);CHECK(satchel_switch_process_domain_objects(p,clone)==2); // No double-release.
    domain_close_request(bytes,1);domain_reply(p,clone,bytes,0,1);CHECK(satchel_switch_process_domain_objects(p,clone)==1);
    session_close_call(p,clone,1,0xF601);CHECK(!satchel_switch_process_domain_objects(p,clone));
    handle_close(p,clone,0);CHECK(!satchel_switch_process_closed_sessions(p));
    // Closed client handles consume real slots until explicitly removed.
    tipc_sm_request(bytes,1,0,name,8);root=lm_reply(p,sm,bytes,1,NULL,0);session_close_call(p,root,1,0xF601);
    uint32_t fillers[SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT-3];
    for(size_t i=0;i<sizeof(fillers)/sizeof(fillers[0]);i++)fillers[i]=port_call(p,0x8001000,0);
    port_call(p,0x8001000,0xD201);handle_close(p,root,0);
    uint32_t replacement=port_call(p,0x8001000,0);CHECK(replacement>fillers[sizeof(fillers)/sizeof(fillers[0])-1]&&!satchel_switch_process_is_session_closed(p,replacement));
    handle_close(p,replacement,0);for(size_t i=0;i<sizeof(fillers)/sizeof(fillers[0]);i++)handle_close(p,fillers[i],0);
    const uint8_t admin_name[8]={'p','c','t','l',':','a'};
    tipc_sm_request(bytes,1,0,admin_name,8);uint32_t admin=lm_reply(p,sm,bytes,1,NULL,0);
    session_close_call(p,admin,1,0xF601);CHECK(satchel_switch_process_is_session_closed(p,admin));
    handle_close(p,admin,0);CHECK(!satchel_switch_process_is_session_closed(p,admin));
    const uint8_t readonly_name[8]={'p','c','t','l',':','r'};
    tipc_sm_request(bytes,1,0,readonly_name,8);uint32_t readonly=lm_reply(p,sm,bytes,1,NULL,0);
    session_close_call(p,readonly,1,0xF601);CHECK(satchel_switch_process_is_session_closed(p,readonly));
    handle_close(p,readonly,0);CHECK(!satchel_switch_process_is_session_closed(p,readonly));
    session_close_call(p,sm,1,0xF601);CHECK(satchel_switch_process_is_sm_session(p,sm)&&!satchel_switch_process_is_sm_initialized(p,sm));
    handle_close(p,sm,0);CHECK(!satchel_switch_process_closed_sessions(p));
    session_close_call(p,0x80,0,0xE401); // Thread handles are never service endpoints.
}

#if defined(SATCHEL_CPU_TESTING)
extern void satchel_switch_process_test_fail_next_ipc_reply(void);
extern int satchel_switch_process_test_parental_matches(const SatchelSwitchProcess *,uint32_t,uint32_t,const SatchelSwitchApplicationControl *,uint32_t,uint32_t);
#endif
static SatchelSwitchParentalInfo parental_info(SatchelSwitchProcess *p) {
    SatchelSwitchParentalInfo info;memset(&info,0xcc,sizeof(info));CHECK(satchel_switch_process_parental_info(p,&info));
    CHECK(info.policy==SATCHEL_SWITCH_PARENTAL_POLICY_UNCONFIGURED);return info;
}
static void parental_noio(uint8_t bytes[256],uint32_t id,int domain) {
    const uint8_t pad[8]={0};
    if(domain) {domain_request(bytes,id,1,0,NULL,0,0,0);metadata_word(bytes,6);metadata_word(bytes+28,0x12345678);metadata_word(bytes+36,1);}
    else {sm_request(bytes,1,0,pad,6,1,0);metadata_word(bytes+4,8);}
}
static void test_parental_initialize(SatchelSwitchProcess *p,const SatchelSwitchApplicationControl *metadata) {
    uint8_t bytes[256],reserved[8]={0};SatchelSwitchIPCInfo ipc;
    SatchelSwitchParentalInfo baseline=parental_info(p),bad=baseline;
    CHECK(!satchel_switch_process_parental_info(NULL,&bad)&&!memcmp(&bad,&baseline,sizeof(bad)));
    CHECK(!satchel_switch_process_parental_info(p,NULL));CHECK(!baseline.live_children);
    const uint8_t names[4][8]={{'p','c','t','l'},{'p','c','t','l',':','s'},{'p','c','t','l',':','a'},{'p','c','t','l',':','r'}};
    const uint32_t capabilities[4]={0x303,0x838e,0x83be,0x8040};
    uint32_t sm=port_call(p,0x8001000,0);tipc_sm_request(bytes,0,1,NULL,0);sm_ipc(p,sm,bytes,1,0,0);
    for(unsigned persona=0;persona<4;persona++) {
        tipc_sm_request(bytes,1,0,names[persona],8);uint32_t root=lm_reply(p,sm,bytes,1,NULL,0);
        sm_request(bytes,1,1,reserved,4,0,0);uint32_t child=lm_reply(p,root,bytes,1,NULL,0);
        SatchelSwitchParentalInfo before=parental_info(p);CHECK(before.live_children==1&&!before.initialized_children&&!before.metadata_bound_children);
        // Initialize has no fresh PID or arguments; malformed schemas never bind state.
        sm_request(bytes,1,1,reserved,4,0,0);metadata_word(bytes+4,8|0x80000000u);sm_ipc(p,child,bytes,0,0,0);
        CHECK(satchel_switch_process_last_ipc(p,&ipc)&&ipc.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_SCHEMA);
        sm_request(bytes,1,0,reserved,4,0,0);sm_ipc(p,child,bytes,0,0,0);
        CHECK(satchel_switch_process_last_ipc(p,&ipc)&&ipc.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_SCHEMA);
        int can_initialize=metadata!=NULL || persona==3;
        parental_noio(bytes,0,0);
        if(can_initialize) {
#if defined(SATCHEL_CPU_TESTING)
            satchel_switch_process_test_fail_next_ipc_reply();sm_ipc(p,child,bytes,0,0,0);
            CHECK(satchel_switch_process_last_ipc(p,&ipc)&&ipc.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_TLS);
            SatchelSwitchParentalInfo failed=parental_info(p);CHECK(!failed.initialized_children&&!failed.metadata_bound_children&&failed.initialize_serviced==before.initialize_serviced);
#endif
            sm_ipc(p,child,bytes,1,0,0);SatchelSwitchParentalInfo done=parental_info(p);
            CHECK(done.live_children==1&&done.initialized_children==1&&done.metadata_bound_children==(persona!=3));
            parental_noio(bytes,0,0);sm_ipc(p,child,bytes,1,0,0);SatchelSwitchParentalInfo repeated=parental_info(p);
            CHECK(repeated.initialized_children==1&&repeated.metadata_bound_children==done.metadata_bound_children&&repeated.initialize_serviced==done.initialize_serviced+1);
        } else {
            sm_ipc(p,child,bytes,0,0,0);CHECK(satchel_switch_process_last_ipc(p,&ipc)&&ipc.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_SERVICE);
            CHECK(!parental_info(p).initialized_children);
        }
#if defined(SATCHEL_CPU_TESTING)
        CHECK(satchel_switch_process_test_parental_matches(p,child,0,can_initialize&&persona!=3?metadata:NULL,can_initialize,capabilities[persona]));
#endif
        handle_close(p,root,0);CHECK(parental_info(p).live_children==1); // factory is not child ownership
        sm_request(bytes,0,0,reserved,5,0,0);metadata_word(bytes+4,8);const uint8_t id[4]={1};lm_reply(p,child,bytes,0,id,4);
        CHECK(parental_info(p).live_children==1); // direct reference transferred into domain
        sm_request(bytes,2,0,reserved,5,0,0);metadata_word(bytes+4,8);uint32_t clone=lm_reply(p,child,bytes,1,NULL,0);
#if defined(SATCHEL_CPU_TESTING)
        CHECK(satchel_switch_process_test_parental_matches(p,child,1,can_initialize&&persona!=3?metadata:NULL,can_initialize,capabilities[persona]));
        CHECK(satchel_switch_process_test_parental_matches(p,clone,1,can_initialize&&persona!=3?metadata:NULL,can_initialize,capabilities[persona]));
#endif
        parental_noio(bytes,1,1);
        if(can_initialize)domain_reply(p,clone,bytes,0,0);else sm_ipc(p,clone,bytes,0,0,0);
        CHECK(parental_info(p).live_children==1&&parental_info(p).initialized_children==(uint32_t)can_initialize);
        // CopyFromCurrentDomain is deliberately unsupported, no phantom reference.
        sm_request(bytes,1,0,reserved,5,0,0);metadata_word(bytes+4,8);sm_ipc(p,clone,bytes,0,0,0);
        CHECK(satchel_switch_process_last_ipc(p,&ipc)&&ipc.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
        session_close_call(p,child,1,0xf601);handle_close(p,child,0);CHECK(parental_info(p).live_children==1);
        // Alias sees the same initialized record after the original endpoint closes.
#if defined(SATCHEL_CPU_TESTING)
        CHECK(satchel_switch_process_test_parental_matches(p,clone,1,can_initialize&&persona!=3?metadata:NULL,can_initialize,capabilities[persona]));
#endif
        domain_close_request(bytes,1);domain_reply(p,clone,bytes,0,1);CHECK(!parental_info(p).live_children);
        domain_close_request(bytes,1);domain_reply(p,clone,bytes,0,1);CHECK(!parental_info(p).live_children);
        handle_close(p,clone,0);
        // Direct child protocol-close releases once; later handle-close must not touch another record.
        tipc_sm_request(bytes,1,0,names[persona],8);root=lm_reply(p,sm,bytes,1,NULL,0);
        sm_request(bytes,1,1,reserved,4,0,0);child=lm_reply(p,root,bytes,1,NULL,0);
        if(can_initialize) {parental_noio(bytes,0,0);sm_ipc(p,child,bytes,1,0,0);CHECK(parental_info(p).initialized_children==1);}
        session_close_call(p,child,1,0xf601);CHECK(!parental_info(p).live_children&&!parental_info(p).initialized_children);
        sm_request(bytes,1,1,reserved,4,0,0);uint32_t other=lm_reply(p,root,bytes,1,NULL,0);
        handle_close(p,child,0);CHECK(parental_info(p).live_children==1&&!parental_info(p).initialized_children);
        handle_close(p,other,0);handle_close(p,root,0);CHECK(!parental_info(p).live_children);
        // Actual phone shape: factory creates domain child, contextual Initialize
        // commits that shared record while a sibling remains uninitialized.
        tipc_sm_request(bytes,1,0,names[persona],8);root=lm_reply(p,sm,bytes,1,NULL,0);
        sm_request(bytes,0,0,reserved,5,0,0);metadata_word(bytes+4,8);lm_reply(p,root,bytes,0,id,4);
        domain_request(bytes,1,1,1,reserved,8,0,0);domain_reply(p,root,bytes,2,0);
        domain_request(bytes,1,1,1,reserved,8,0,0);domain_reply(p,root,bytes,3,0);
        CHECK(parental_info(p).live_children==2&&!parental_info(p).initialized_children);
        parental_noio(bytes,2,1);
        if(can_initialize) {
#if defined(SATCHEL_CPU_TESTING)
            uint64_t serviced=parental_info(p).initialize_serviced;
            satchel_switch_process_test_fail_next_ipc_reply();sm_ipc(p,root,bytes,0,0,0);
            CHECK(satchel_switch_process_last_ipc(p,&ipc)&&ipc.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_TLS);
            CHECK(!parental_info(p).initialized_children&&!parental_info(p).metadata_bound_children&&parental_info(p).initialize_serviced==serviced);
#endif
            domain_reply(p,root,bytes,0,0);
        } else sm_ipc(p,root,bytes,0,0,0);
        sm_request(bytes,2,0,reserved,5,0,0);metadata_word(bytes+4,8);clone=lm_reply(p,root,bytes,1,NULL,0);
#if defined(SATCHEL_CPU_TESTING)
        CHECK(satchel_switch_process_test_parental_matches(p,clone,2,can_initialize&&persona!=3?metadata:NULL,can_initialize,capabilities[persona]));
        CHECK(satchel_switch_process_test_parental_matches(p,clone,3,NULL,0,capabilities[persona]));
#endif
        session_close_call(p,root,1,0xf601);handle_close(p,root,0);
        CHECK(parental_info(p).live_children==2&&parental_info(p).initialized_children==(uint32_t)can_initialize);
        domain_close_request(bytes,2);domain_reply(p,clone,bytes,0,1);
        CHECK(parental_info(p).live_children==1&&!parental_info(p).initialized_children);
        handle_close(p,clone,0);CHECK(!parental_info(p).live_children);
    }
    //32 live records across separate domain tables; exhaustion commits no ID/reference.
    uint32_t roots[2];for(unsigned j=0;j<2;j++) {
        tipc_sm_request(bytes,1,0,names[0],8);roots[j]=lm_reply(p,sm,bytes,1,NULL,0);
        sm_request(bytes,0,0,reserved,5,0,0);metadata_word(bytes+4,8);const uint8_t id[4]={1};lm_reply(p,roots[j],bytes,0,id,4);
    }
    for(unsigned i=0;i<31;i++){domain_request(bytes,1,1,1,reserved,8,0,0);domain_reply(p,roots[0],bytes,i+2,0);}
    domain_request(bytes,1,1,1,reserved,8,0,0);domain_reply(p,roots[1],bytes,2,0);
    CHECK(parental_info(p).live_children==32);SatchelSwitchParentalInfo full=parental_info(p);
    domain_request(bytes,1,1,1,reserved,8,0,0);sm_ipc(p,roots[1],bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&ipc)&&ipc.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_RESOURCE);
    CHECK(parental_info(p).created==full.created&&satchel_switch_process_domain_objects(p,roots[1])==2);
    domain_close_request(bytes,2);domain_reply(p,roots[0],bytes,0,1);CHECK(parental_info(p).live_children==31);
#if defined(SATCHEL_CPU_TESTING)
    domain_request(bytes,1,1,1,reserved,8,0,0);satchel_switch_process_test_fail_next_ipc_reply();sm_ipc(p,roots[1],bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&ipc)&&ipc.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_TLS);
    CHECK(parental_info(p).live_children==31&&parental_info(p).created==full.created&&satchel_switch_process_domain_objects(p,roots[1])==2);
#endif
    domain_request(bytes,1,1,1,reserved,8,0,0);domain_reply(p,roots[1],bytes,3,0);
    CHECK(parental_info(p).live_children==32&&!parental_info(p).initialized_children);
    handle_close(p,roots[0],0);CHECK(parental_info(p).live_children==2);
    session_close_call(p,roots[1],1,0xf601);CHECK(!parental_info(p).live_children);handle_close(p,roots[1],0);
    handle_close(p,sm,0);CHECK(!parental_info(p).live_children&&!parental_info(p).metadata_bound_children);
}

// Owned time:u connection only: no clock/shared-memory methods yet.
static void test_time_user(SatchelSwitchProcess *p) {
    uint8_t bytes[256], name[8]={'t','i','m','e',':','u'}; SatchelSwitchIPCInfo info;
    uint64_t events_before=satchel_switch_process_readable_event_handles(p);
    uint32_t sm=port_call(p,0x8001000,0);
    tipc_sm_request(bytes,0,1,NULL,0);sm_ipc(p,sm,bytes,1,0,0);
    uint32_t roots[2];
    for(unsigned protocol=0;protocol<2;protocol++) {
        if(protocol) sm_request(bytes,1,0,name,4,0,0);
        else tipc_sm_request(bytes,1,0,name,8);
        roots[protocol]=lm_reply(p,sm,bytes,1,NULL,0);
        CHECK(satchel_switch_process_last_ipc(p,&info) && info.service_kind==SATCHEL_SWITCH_IPC_SERVICE_TIME);
        CHECK(!strcmp(satchel_switch_process_service_name(info.service_kind),"time:u"));
        CHECK(!satchel_switch_process_is_sm_session(p,roots[protocol]) && !satchel_switch_process_is_lm_session(p,roots[protocol]));
        const uint32_t commands[]={5,20,50,51,100,101,102,200,201,300,400,401,500,501,UINT32_MAX};
        for(size_t i=0;i<sizeof(commands)/sizeof(commands[0]);i++) {
            sm_request(bytes,commands[i],0,name,4,0,0);metadata_word(bytes+4,8);
            sm_ipc(p,roots[protocol],bytes,0,0,0);
            CHECK(satchel_switch_process_last_ipc(p,&info) && info.command_id==commands[i] &&
                info.service_kind==SATCHEL_SWITCH_IPC_SERVICE_TIME && info.object_kind==SATCHEL_SWITCH_IPC_OBJECT_TIME_USER &&
                info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
            CHECK(satchel_switch_process_readable_event_handles(p)==events_before);
        }
    }
    // Exact user registration grants no system/admin/repair/glue persona.
    const uint8_t aliases[][8]={{'t','i','m','e',':','a'},{'t','i','m','e',':','r'},{'t','i','m','e',':','s'},
        {'t','i','m','e',':','s','u'},{'t','i','m','e',':','m'},{'t','i','m','e',':','p'},{'t','i','m','e',':','a','l'}};
    const uint32_t kinds[]={SATCHEL_SWITCH_IPC_SERVICE_TIME_A,SATCHEL_SWITCH_IPC_SERVICE_TIME_R,SATCHEL_SWITCH_IPC_SERVICE_TIME_S,
        SATCHEL_SWITCH_IPC_SERVICE_TIME_SU,SATCHEL_SWITCH_IPC_SERVICE_TIME_M,SATCHEL_SWITCH_IPC_SERVICE_TIME_P,SATCHEL_SWITCH_IPC_SERVICE_TIME_AL};
    for(size_t i=0;i<sizeof(kinds)/sizeof(kinds[0]);i++) {
        tipc_sm_request(bytes,1,0,aliases[i],8);sm_ipc(p,sm,bytes,0,0,0);
        CHECK(satchel_switch_process_last_ipc(p,&info)&&info.service_kind==kinds[i]&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_SERVICE);
    }
    // Actual domain conversion and clone share one table; closing a handle does
    // not erase its sibling, but domain close removes the object for all clones.
    sm_request(bytes,0,0,name,7,1,0);metadata_word(bytes+4,8);
    const uint8_t root_id[4]={1};lm_reply(p,roots[0],bytes,0,root_id,4);
    CHECK(satchel_switch_process_domain_objects(p,roots[0])==1);
    sm_request(bytes,2,0,name,5,0,0);metadata_word(bytes+4,8);
    uint32_t clone=lm_reply(p,roots[0],bytes,1,NULL,0);handle_close(p,roots[0],0);
    domain_request(bytes,1,8,0,NULL,0,0,0);sm_ipc(p,clone,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info) && info.object_kind==SATCHEL_SWITCH_IPC_OBJECT_TIME_USER && info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
    domain_close_request(bytes,1);domain_reply(p,clone,bytes,0,1);
    CHECK(!satchel_switch_process_domain_objects(p,clone));
    domain_request(bytes,1,8,0,NULL,0,0,0);sm_ipc(p,clone,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info) && info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_OBJECT);
    // Handle exhaustion is atomic and closed tokens are never reused.
    uint32_t fillers[SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT-4];
    for(size_t i=0;i<sizeof(fillers)/sizeof(fillers[0]);i++)fillers[i]=port_call(p,0x8001000,0);
    tipc_sm_request(bytes,1,0,name,8);sm_ipc(p,sm,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info) && info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_RESOURCE);
    handle_close(p,fillers[0],0);tipc_sm_request(bytes,1,0,name,8);
    uint32_t retry=lm_reply(p,sm,bytes,1,NULL,0);CHECK(retry>fillers[sizeof(fillers)/sizeof(fillers[0])-1]);
    handle_close(p,retry,0);handle_close(p,retry,0xE401);
    for(size_t i=1;i<sizeof(fillers)/sizeof(fillers[0]);i++)handle_close(p,fillers[i],0);
    session_close_call(p,roots[1],1,0xf601);CHECK(satchel_switch_process_is_session_closed(p,roots[1]));
    session_close_call(p,roots[1],0,0xf601);handle_close(p,roots[1],0);
    CHECK(!satchel_switch_process_is_session_closed(p,roots[1]));handle_close(p,clone,0);handle_close(p,sm,0);
    CHECK(satchel_switch_process_readable_event_handles(p)==events_before);
}

static void test_time_children(SatchelSwitchProcess *p) {
    uint8_t bytes[256],name[8]={'t','i','m','e',':','u'};SatchelSwitchIPCInfo info;
    uint64_t events=satchel_switch_process_readable_event_handles(p);
    const uint32_t kinds[5]={SATCHEL_SWITCH_IPC_OBJECT_TIME_USER_CLOCK,SATCHEL_SWITCH_IPC_OBJECT_TIME_NETWORK_CLOCK,
        SATCHEL_SWITCH_IPC_OBJECT_TIME_STEADY_CLOCK,SATCHEL_SWITCH_IPC_OBJECT_TIME_ZONE,SATCHEL_SWITCH_IPC_OBJECT_TIME_LOCAL_CLOCK};
    uint32_t sm=port_call(p,0x8001000,0);tipc_sm_request(bytes,0,1,NULL,0);sm_ipc(p,sm,bytes,1,0,0);
    for(unsigned domain=0;domain<2;domain++) {
        tipc_sm_request(bytes,1,0,name,8);uint32_t root=lm_reply(p,sm,bytes,1,NULL,0),children[5];
        if(domain) {sm_request(bytes,0,0,name,7,1,0);metadata_word(bytes+4,8);const uint8_t id[4]={1};lm_reply(p,root,bytes,0,id,4);}
        for(unsigned command=0;command<5;command++) {
            // Exact no-input schema: a PID or extra payload cannot create a child.
            if(domain)domain_request(bytes,1,command,1,NULL,0,0,0);
            else {sm_request(bytes,command,1,name,4,0,0);metadata_word(bytes+4,8|0x80000000u);}
            sm_ipc(p,root,bytes,0,0,0);CHECK(satchel_switch_process_last_ipc(p,&info)&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_SCHEMA);
            if(domain)domain_request(bytes,1,command,0,name,8,0,0);
            else sm_request(bytes,command,0,name,4,0,0);
            sm_ipc(p,root,bytes,0,0,0);CHECK(satchel_switch_process_last_ipc(p,&info)&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_SCHEMA);
            if(domain) {domain_request(bytes,1,command,0,NULL,0,0,0);metadata_word(bytes,6);children[command]=command+2;}
            else {sm_request(bytes,command,0,name,6,1,0);metadata_word(bytes+4,8);}
#if defined(SATCHEL_CPU_TESTING)
            satchel_switch_process_test_fail_next_ipc_reply();sm_ipc(p,root,bytes,0,0,0);
            CHECK(satchel_switch_process_last_ipc(p,&info)&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_TLS);
            if(domain)CHECK(satchel_switch_process_domain_objects(p,root)==command+1);
#endif
            if(domain)domain_reply(p,root,bytes,children[command],0);else children[command]=lm_reply(p,root,bytes,1,NULL,0);
        }
        uint32_t endpoint=root;
        if(domain) {sm_request(bytes,2,0,name,5,0,0);metadata_word(bytes+4,8);endpoint=lm_reply(p,root,bytes,1,NULL,0);
            session_close_call(p,root,1,0xf601);handle_close(p,root,0);CHECK(satchel_switch_process_domain_objects(p,endpoint)==6);
            domain_close_request(bytes,1);domain_reply(p,endpoint,bytes,0,1);
        } else {session_close_call(p,root,1,0xf601);handle_close(p,root,0);}
        for(unsigned i=0;i<5;i++) {
            const uint32_t methods[]={0,1,2,3,4,100,101,102,200,201,UINT32_MAX};
            for(size_t j=0;j<sizeof(methods)/sizeof(methods[0]);j++) {
                if(domain)domain_request(bytes,children[i],methods[j],0,NULL,0,0,0);
                else {sm_request(bytes,methods[j],0,name,4,0,0);metadata_word(bytes+4,8);}
                sm_ipc(p,domain?endpoint:children[i],bytes,0,0,0);
                CHECK(satchel_switch_process_last_ipc(p,&info)&&info.object_kind==kinds[i]&&info.service_kind==SATCHEL_SWITCH_IPC_SERVICE_TIME&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
            }
            if(domain){domain_close_request(bytes,children[i]);domain_reply(p,endpoint,bytes,0,1);}
            else {session_close_call(p,children[i],1,0xf601);handle_close(p,children[i],0);}
        }
        if(domain){CHECK(!satchel_switch_process_domain_objects(p,endpoint));handle_close(p,endpoint,0);}
    }
    // Actual getter3 exhausted-handle failure preserves parent and token namespace.
    tipc_sm_request(bytes,1,0,name,8);uint32_t root=lm_reply(p,sm,bytes,1,NULL,0);
    uint32_t fillers[SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT-3];for(size_t i=0;i<sizeof(fillers)/sizeof(fillers[0]);i++)fillers[i]=port_call(p,0x8001000,0);
    sm_request(bytes,3,0,name,4,0,0);metadata_word(bytes+4,8);sm_ipc(p,root,bytes,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&info)&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_RESOURCE);
    handle_close(p,fillers[0],0);sm_request(bytes,3,0,name,4,0,0);metadata_word(bytes+4,8);uint32_t child=lm_reply(p,root,bytes,1,NULL,0);
    CHECK(child>fillers[sizeof(fillers)/sizeof(fillers[0])-1]);handle_close(p,child,0);
    for(size_t i=1;i<sizeof(fillers)/sizeof(fillers[0]);i++)handle_close(p,fillers[i],0);
    handle_close(p,root,0);handle_close(p,sm,0);CHECK(satchel_switch_process_readable_event_handles(p)==events);
}

static void applet_proxy_request(uint8_t bytes[256], uint32_t object_id, uint32_t process_handle,
                                 int pid, unsigned copies, unsigned moves) {
    memset(bytes, 0, 256); metadata_word(bytes, 4);
    metadata_word(bytes + 4, (object_id ? 14u : 10u) | 0x80000000u);
    metadata_word(bytes + 8, (unsigned)pid | copies << 1 | moves << 5);
    size_t at = 12;
    if (pid) { memset(bytes + at, 0xED, 8); at += 8; } // Forged claim is not an identity.
    for (unsigned i = 0; i < copies + moves; i++, at += 4) metadata_word(bytes + at, process_handle);
    at = (at + 15) & ~(size_t)15;
    if (object_id) {
        bytes[at] = 1; bytes[at + 2] = 24; metadata_word(bytes + at + 4, object_id); at += 16;
    }
    metadata_word(bytes + at, 0x49434653); memset(bytes + at + 16, 0xA9, 8);
    memset(bytes + 112, 0xA7, 144);
}
// Independent public-wire expectation: GetEventHandle uses a COPY handle even
// on a domain service; it never returns a moved session or a domain object ID.
static uint32_t applet_event_reply(SatchelSwitchProcess *p, uint32_t handle, uint8_t request[256], int domain) {
    SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
    CHECK(satchel_cpu_probe_write(cpu, 0x31000000, request, 256));
    CHECK(satchel_cpu_probe_set_pc(cpu, 0x8000014) && satchel_cpu_probe_set_x(cpu, 0, handle));
    SatchelCPUProbeResult result = satchel_switch_process_run(p, 1, NULL, NULL);
    CHECK(result.reason == SATCHEL_CPU_BUDGET && result.steps == 1);
    uint64_t value; CHECK(satchel_cpu_probe_get_x(cpu, 0, &value) && value == 0);
    uint8_t actual[256], expected[48] = {0};
    CHECK(satchel_cpu_probe_read(cpu, 0x31000000, actual, 256));
    uint32_t copied = 0; memcpy(&copied, actual + 12, 4);
    CHECK(copied && satchel_switch_process_is_readable_event(p, copied));
    size_t end = domain ? 48 : 32;
    metadata_word(expected + 4, 0x80000000u | (uint32_t)((end - 16) / 4));
    metadata_word(expected + 8, 2); metadata_word(expected + 12, copied);
    metadata_word(expected + (domain ? 32 : 16), 0x4f434653);
    CHECK(!memcmp(actual, expected, end));
    CHECK(!memcmp(actual + end, request + end, 256 - end));
    CHECK(satchel_switch_process_applet_event_signaled(p) == 0);
    return copied;
}
static void applet_empty_message_reply(SatchelSwitchProcess *p, uint32_t handle, uint8_t request[256], int domain) {
    SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
    CHECK(satchel_cpu_probe_write(cpu,0x31000000,request,256));
    CHECK(satchel_cpu_probe_set_pc(cpu,0x8000014) && satchel_cpu_probe_set_x(cpu,0,handle));
    CHECK(satchel_cpu_probe_set_x(cpu,1,UINT64_C(0xACACACACACACACAC)) && satchel_cpu_probe_set_x(cpu,2,0xDEADBEEF));
    SatchelCPUProbeResult result = satchel_switch_process_run(p,1,NULL,NULL);
    uint64_t value; CHECK(result.reason == SATCHEL_CPU_BUDGET && result.steps == 1 && satchel_cpu_probe_get_x(cpu,0,&value) && value == 0);
    uint8_t actual[256], expected[48] = {0}; size_t end = domain ? 48 : 32;
    metadata_word(expected+4,(uint32_t)((end-8)/4));metadata_word(expected+(domain?32:16),0x4f434653);
    metadata_word(expected+(domain?40:24),0x680);
    CHECK(satchel_cpu_probe_read(cpu,0x31000000,actual,sizeof(actual)) && !memcmp(actual,expected,end));
    CHECK(!memcmp(actual+end,request+end,256-end));
    CHECK(satchel_cpu_probe_get_x(cpu,1,&value) && value == UINT64_C(0xACACACACACACACAC));
    CHECK(satchel_cpu_probe_get_x(cpu,2,&value) && value == 0xDEADBEEF);
    uint32_t thread_handle; CHECK(satchel_cpu_probe_read(cpu,0x31000110,&thread_handle,4) && thread_handle == 0x80);
    CHECK(satchel_switch_process_applet_event_signaled(p) == 0);
}
static void test_applet_objects(SatchelSwitchProcess *p) {
    uint8_t request[256], name[8] = {'a','p','p','l','e','t','O','E'};
    uint32_t sm = port_call(p, 0x8001000, 0);
    tipc_sm_request(request, 0, 1, NULL, 0); sm_ipc(p, sm, request, 1, 0, 0);
    uint8_t ae[8] = {'a','p','p','l','e','t','A','E'};
    tipc_sm_request(request, 1, 0, ae, 8); sm_ipc(p, sm, request, 0, 0, 0); // Same family label never registers system applet endpoint.
    SatchelSwitchIPCInfo info; CHECK(satchel_switch_process_last_ipc(p, &info) && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_SERVICE);
    tipc_sm_request(request, 1, 0, name, 8); uint32_t root = lm_reply(p, sm, request, 1, NULL, 0);
    uint64_t resource = satchel_switch_process_applet_resource_id(p);
    CHECK(resource > UINT32_MAX && !satchel_switch_process_applet_resource_id(NULL));
    const uint32_t invalid_handles[] = {0, 0x80, sm, root, 0xFFFF8000, UINT32_MAX};
    for (unsigned i = 0; i < sizeof(invalid_handles) / sizeof(invalid_handles[0]); i++) {
        applet_proxy_request(request, 0, invalid_handles[i], 1, 1, 0); sm_ipc(p, root, request, 0, 0, 0);
        CHECK(satchel_switch_process_last_ipc(p, &info) && info.object_kind == SATCHEL_SWITCH_IPC_OBJECT_APPLET && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_HANDLE);
    }
    CHECK(owned_thread_query(p, 0, 0x80, 0) == 37); // Rejected copy never consumes the original reference.
    applet_proxy_request(request, 0, 0xffff8001, 0, 1, 0); sm_ipc(p, root, request, 0, 0, 0);
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_SCHEMA);
    applet_proxy_request(request, 0, 0xffff8001, 1, 0, 0); sm_ipc(p, root, request, 0, 0, 0);
    applet_proxy_request(request, 0, 0xffff8001, 1, 2, 0); sm_ipc(p, root, request, 0, 0, 0);
    applet_proxy_request(request, 0, 0xffff8001, 1, 0, 1); sm_ipc(p, root, request, 0, 0, 0);
    applet_proxy_request(request, 0, 0xffff8001, 1, 1, 0); metadata_word(request + 4, 0x80000009u); sm_ipc(p, root, request, 0, 0, 0);
    applet_proxy_request(request, 0, 0xffff8001, 1, 1, 0); uint32_t proxy = lm_reply(p, root, request, 1, NULL, 0);
    sm_request(request, 2, 0, name, 4, 0, 0); metadata_word(request + 4, 8);
    uint32_t window = lm_reply(p, proxy, request, 1, NULL, 0);
    uint8_t resource_bytes[8]; for (unsigned i = 0; i < 8; i++) resource_bytes[i] = (uint8_t)(resource >> (8 * i));
    sm_request(request, 1, 0, name, 4, 0, 0); metadata_word(request + 4, 8);
    lm_reply(p, window, request, 0, resource_bytes, sizeof(resource_bytes));
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.object_kind == SATCHEL_SWITCH_IPC_OBJECT_WINDOW_CONTROLLER && info.service_kind == SATCHEL_SWITCH_IPC_SERVICE_APPLET);
    // Published command1 is implemented; command0 is not silently substituted.
    sm_request(request, 0, 0, name, 4, 0, 0); metadata_word(request + 4, 8); sm_ipc(p, window, request, 0, 0, 0);
    CHECK(!satchel_switch_process_applet_exit_to_library_enabled(p));
    CHECK(!satchel_switch_process_applet_exit_to_library_enabled(NULL));
    CHECK(satchel_switch_process_applet_event_signaled(p) == -1 && satchel_switch_process_applet_event_signaled(NULL) == -1);
    CHECK(!satchel_switch_process_readable_event_handles(p) && !satchel_switch_process_readable_event_handles(NULL));
    CHECK(!satchel_switch_process_is_readable_event(NULL, 1) && !satchel_switch_process_is_readable_event(p, window));
    sm_request(request, 0, 0, name, 4, 0, 0); metadata_word(request + 4, 8);
    uint32_t common = lm_reply(p, proxy, request, 1, NULL, 0);
    sm_request(request, 900, 1, name, 4, 0, 0); sm_ipc(p, common, request, 0, 0, 0);
    CHECK(!satchel_switch_process_applet_exit_to_library_enabled(p));
    sm_request(request, 0, 0, name, 4, 0, 0); metadata_word(request + 4, 8);
    uint32_t event = applet_event_reply(p, common, request, 0);
    CHECK(satchel_switch_process_readable_event_handles(p) == 1);
    handle_close(p, common, 0); // Service lifetime is independent of its copied event.
    CHECK(satchel_switch_process_is_readable_event(p, event));
    handle_close(p, event, 0); handle_close(p, event, 0xE401);
    CHECK(!satchel_switch_process_readable_event_handles(p) && satchel_switch_process_applet_event_signaled(p) == 0);
    // SelfController91 is a separate accumulated-suspension change event.
    CHECK(!satchel_switch_process_suspended_ticks(p) && !satchel_switch_process_suspended_ticks(NULL));
    CHECK(satchel_switch_process_suspended_tick_event_signaled(p) == -1 && satchel_switch_process_suspended_tick_event_signaled(NULL) == -1);
    CHECK(!satchel_switch_process_is_suspended_tick_event(NULL, 1) && !satchel_switch_process_is_suspended_tick_event(p, window));
    sm_request(request, 1, 0, name, 4, 0, 0); metadata_word(request + 4, 8);
    uint32_t self = lm_reply(p, proxy, request, 1, NULL, 0);
    sm_request(request, 91, 1, name, 4, 0, 0); sm_ipc(p, self, request, 0, 0, 0);
    CHECK(satchel_switch_process_suspended_tick_event_signaled(p) == -1);
    sm_request(request, 91, 0, name, 4, 0, 0); sm_ipc(p, self, request, 0, 0, 0); // Unexpected inline bytes.
    CHECK(satchel_switch_process_suspended_tick_event_signaled(p) == -1);
    sm_request(request, 91, 0, name, 4, 0, 0); metadata_word(request + 4, 8);
    uint32_t tick_event = applet_event_reply(p, self, request, 0);
    CHECK(satchel_switch_process_is_suspended_tick_event(p, tick_event) && satchel_switch_process_suspended_tick_event_signaled(p) == 0);
    uint8_t zero_ticks[8] = {0};
    sm_request(request, 90, 0, name, 4, 0, 0); metadata_word(request + 4, 8);
    lm_reply(p, self, request, 0, zero_ticks, sizeof(zero_ticks));
    handle_close(p, self, 0); CHECK(satchel_switch_process_is_suspended_tick_event(p, tick_event));
    handle_close(p, tick_event, 0); CHECK(!satchel_switch_process_is_suspended_tick_event(p, tick_event));
    CHECK(!satchel_switch_process_readable_event_handles(p) && satchel_switch_process_suspended_tick_event_signaled(p) == 0);
    // Root, proxy and its controllers have independent owned lifetimes.
    uint32_t fillers[SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT - 5];
    for (size_t i = 0; i < sizeof(fillers) / sizeof(fillers[0]); i++) fillers[i] = port_call(p, 0x8001000, 0);
    applet_proxy_request(request, 0, 0xffff8001, 1, 1, 0); sm_ipc(p, root, request, 0, 0, 0);
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_RESOURCE);
    handle_close(p, fillers[0], 0);
    applet_proxy_request(request, 0, 0xffff8001, 1, 1, 0); uint32_t retry = lm_reply(p, root, request, 1, NULL, 0);
    CHECK(retry == fillers[sizeof(fillers) / sizeof(fillers[0]) - 1] + 1);
    handle_close(p, root, 0); handle_close(p, proxy, 0); handle_close(p, retry, 0);
    sm_request(request, 1, 0, name, 4, 0, 0); metadata_word(request + 4, 8); lm_reply(p, window, request, 0, resource_bytes, 8);
    handle_close(p, window, 0);
    for (size_t i = 1; i < sizeof(fillers) / sizeof(fillers[0]); i++) handle_close(p, fillers[i], 0);
    // Actual public application startup graph in a domain, with real typed IDs.
    tipc_sm_request(request, 1, 0, name, 8); root = lm_reply(p, sm, request, 1, NULL, 0);
    sm_request(request, 0, 0, name, 7, 1, 0); metadata_word(request + 4, 8);
    const uint8_t root_id[4] = {1}; lm_reply(p, root, request, 0, root_id, 4);
    applet_proxy_request(request, 1, 0xffff8001, 1, 1, 0); domain_reply(p, root, request, 2, 0);
    const uint32_t getters[] = {0, 1, 2, 3, 4, 11, 20, 1000};
    const uint32_t kinds[] = {SATCHEL_SWITCH_IPC_OBJECT_COMMON_STATE, SATCHEL_SWITCH_IPC_OBJECT_SELF_CONTROLLER,
        SATCHEL_SWITCH_IPC_OBJECT_WINDOW_CONTROLLER, SATCHEL_SWITCH_IPC_OBJECT_AUDIO_CONTROLLER,
        SATCHEL_SWITCH_IPC_OBJECT_DISPLAY_CONTROLLER, SATCHEL_SWITCH_IPC_OBJECT_LIBRARY_APPLET_CREATOR,
        SATCHEL_SWITCH_IPC_OBJECT_APPLICATION_FUNCTIONS, SATCHEL_SWITCH_IPC_OBJECT_DEBUG_FUNCTIONS};
    for (unsigned i = 0; i < sizeof(getters) / sizeof(getters[0]); i++) {
        domain_request(request, 2, getters[i], 0, NULL, 0, 0, 0); domain_reply(p, root, request, i + 3, 0);
        domain_request(request, i + 3, 0xffff, 0, NULL, 0, 0, 0); sm_ipc(p, root, request, 0, 0, 0);
        CHECK(satchel_switch_process_last_ipc(p, &info) && info.object_kind == kinds[i] && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
    }
    CHECK(satchel_switch_process_domain_objects(p, root) == 10);
    domain_request(request, 2, 2, 1, name, 8, 0, 0); sm_ipc(p, root, request, 0, 0, 0);
    CHECK(satchel_switch_process_domain_objects(p, root) == 10);
    // Actual observed CommonStateGetter900 owns a policy, not a platform-region value.
    domain_request(request, 3, 900, 0, name, 8, 0, 0); sm_ipc(p, root, request, 0, 0, 0);
    CHECK(!satchel_switch_process_applet_exit_to_library_enabled(p));
    domain_request(request, 3, 900, 1, NULL, 0, 0, 0); sm_ipc(p, root, request, 0, 0, 0);
    CHECK(!satchel_switch_process_applet_exit_to_library_enabled(p));
    domain_request(request, 3, 900, 0, NULL, 0, 0x8002400, 8); sm_ipc(p, root, request, 0, 0, 0);
    CHECK(!satchel_switch_process_applet_exit_to_library_enabled(p));
    for (unsigned i = 0; i < 2; i++) {
        domain_request(request, 3, 900, 0, NULL, 0, 0, 0); domain_reply(p, root, request, 0, 0);
        CHECK(satchel_switch_process_applet_exit_to_library_enabled(p));
    }
    uint8_t execute_args[16] = {0};
    domain_request(request, 9, 120, 0, execute_args, sizeof(execute_args), 0, 0); sm_ipc(p, root, request, 0, 0, 0);
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.object_kind == SATCHEL_SWITCH_IPC_OBJECT_APPLICATION_FUNCTIONS && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
    CHECK(satchel_switch_process_applet_exit_to_library_enabled(p)); // No fake next-program execution consumes the policy.
    domain_request(request, 3, 0, 1, NULL, 0, 0, 0); sm_ipc(p, root, request, 0, 0, 0);
    CHECK(!satchel_switch_process_readable_event_handles(p));
    domain_request(request, 3, 0, 0, NULL, 0, 0, 0); event = applet_event_reply(p, root, request, 1);
    uint32_t event_fillers[SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT - 4];
    for (size_t i = 0; i < sizeof(event_fillers) / sizeof(event_fillers[0]); i++) event_fillers[i] = port_call(p, 0x8001000, 0);
    domain_request(request, 3, 0, 0, NULL, 0, 0, 0); sm_ipc(p, root, request, 0, 0, 0);
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_RESOURCE);
    CHECK(satchel_switch_process_readable_event_handles(p) == 1 && satchel_switch_process_applet_event_signaled(p) == 0);
    handle_close(p, event_fillers[0], 0);
    uint32_t second_event = applet_event_reply(p, root, request, 1);
    CHECK(second_event == event_fillers[sizeof(event_fillers) / sizeof(event_fillers[0]) - 1] + 1);
    handle_close(p, event, 0); CHECK(satchel_switch_process_is_readable_event(p, second_event));
    for (size_t i = 1; i < sizeof(event_fillers) / sizeof(event_fillers[0]); i++) handle_close(p, event_fillers[i], 0);
    handle_close(p, second_event, 0);
    // Exceed the helper's4096 simultaneous-reference limit over time: closed
    // copied handles must release their references while the context survives.
    for (unsigned i = 0; i < 4100; i++) {
        domain_request(request, 3, 0, 0, NULL, 0, 0, 0);
        uint32_t copied = applet_event_reply(p, root, request, 1);
        CHECK(copied > second_event); second_event = copied;
        handle_close(p, copied, 0);
    }
    CHECK(!satchel_switch_process_readable_event_handles(p) && satchel_switch_process_applet_event_signaled(p) == 0);
    domain_request(request, 3, 0, 0, NULL, 0, 0, 0); event = applet_event_reply(p, root, request, 1);
    // No queued message, focus transition or signal is manufactured.
    domain_request(request, 3, 1, 0, NULL, 0, 0, 0); applet_empty_message_reply(p, root, request, 1);
    SatchelSwitchAppletMessageInfo empty_messages;
    CHECK(satchel_switch_process_applet_message_info(p,&empty_messages) && empty_messages.phase == SATCHEL_SWITCH_APPLET_CREATED && !empty_messages.pending && !empty_messages.enqueued && !empty_messages.received);
    CHECK(satchel_switch_process_last_ipc(p, &info) && info.object_kind == SATCHEL_SWITCH_IPC_OBJECT_COMMON_STATE);
    domain_request(request, 4, 91, 1, NULL, 0, 0, 0); sm_ipc(p, root, request, 0, 0, 0);
    domain_request(request, 4, 91, 0, name, 8, 0, 0); sm_ipc(p, root, request, 0, 0, 0);
    domain_request(request, 4, 91, 0, NULL, 0, 0x8002400, 8); sm_ipc(p, root, request, 0, 0, 0);
    CHECK(satchel_switch_process_readable_event_handles(p) == 1);
    domain_request(request, 4, 91, 0, NULL, 0, 0, 0); tick_event = applet_event_reply(p, root, request, 1);
    CHECK(tick_event != event && satchel_switch_process_is_suspended_tick_event(p, tick_event));
    CHECK(!satchel_switch_process_is_suspended_tick_event(p, event));
    CHECK(satchel_switch_process_readable_event_handles(p) == 2 && satchel_switch_process_suspended_tick_event_signaled(p) == 0);
    domain_request(request, 4, 90, 0, NULL, 0, 0, 0); storage_reply(p, root, request, 1, 1, 0);
    domain_request(request, 4, 90, 1, NULL, 0, 0, 0); sm_ipc(p, root, request, 0, 0, 0);
    domain_request(request, 4, 90, 0, name, 8, 0, 0); sm_ipc(p, root, request, 0, 0, 0);
    // A cancelled diagnostic is not a guest suspend/resume transition.
    SatchelCPUProbeResult cancelled = satchel_switch_process_run(p, 1, cancel, NULL);
    CHECK(cancelled.reason == SATCHEL_CPU_CANCELLED && cancelled.steps == 0);
    CHECK(!satchel_switch_process_suspended_ticks(p) && satchel_switch_process_suspended_tick_event_signaled(p) == 0);
    domain_request(request, 5, 1, 0, NULL, 0, 0, 0); storage_reply(p, root, request, 1, 1, resource);
    sm_request(request, 2, 0, name, 5, 0, 0); metadata_word(request + 4, 8);
    uint32_t clone = lm_reply(p, root, request, 1, NULL, 0); handle_close(p, root, 0);
    domain_close_request(request, 1); domain_reply(p, clone, request, 0, 1);
    domain_close_request(request, 2); domain_reply(p, clone, request, 0, 1);
    domain_request(request, 5, 1, 0, NULL, 0, 0, 0); storage_reply(p, clone, request, 1, 1, resource);
    CHECK(satchel_switch_process_domain_objects(p, clone) == 8 && satchel_switch_process_applet_resource_id(p) == resource);
    handle_close(p, clone, 0); handle_close(p, sm, 0);
    CHECK(satchel_switch_process_is_readable_event(p, event) && satchel_switch_process_applet_event_signaled(p) == 0);
    handle_close(p, event, 0);
    CHECK(satchel_switch_process_is_suspended_tick_event(p, tick_event) && satchel_switch_process_readable_event_handles(p) == 1);
    handle_close(p, tick_event, 0); handle_close(p, tick_event, 0xE401);
    CHECK(!satchel_switch_process_readable_event_handles(p) && satchel_switch_process_applet_exit_to_library_enabled(p));
    CHECK(!satchel_switch_process_suspended_ticks(p) && satchel_switch_process_suspended_tick_event_signaled(p) == 0);
}

static void applet_profile_request(uint8_t bytes[256], uint32_t domain_id, uint32_t command,
                                    const void *data, size_t size) {
    if (domain_id) { domain_request(bytes, domain_id, command, 0, data, size, 0, 0); return; }
    uint8_t args[8] = {0}; CHECK(size <= sizeof(args));
    if (size) memcpy(args, data, size);
    sm_request(bytes, command, 0, args, 4, 0, 0);
    metadata_word(bytes + 4, (uint32_t)((32 + size + 3) / 4));
}
static void applet_value_reply(SatchelSwitchProcess *p, uint32_t handle, uint8_t bytes[256], int domain,
                               const void *data, size_t size) {
    if (!domain) { lm_reply(p, handle, bytes, 0, data, size); return; }
    SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
    CHECK(satchel_cpu_probe_write(cpu, 0x31000000, bytes, 256));
    CHECK(satchel_cpu_probe_set_pc(cpu, 0x8000014) && satchel_cpu_probe_set_x(cpu, 0, handle));
    SatchelCPUProbeResult result = satchel_switch_process_run(p, 1, NULL, NULL);
    CHECK(result.reason == SATCHEL_CPU_BUDGET && result.steps == 1);
    uint64_t value; CHECK(satchel_cpu_probe_get_x(cpu, 0, &value) && value == 0);
    uint8_t actual[256], expected[64] = {0}; size_t end = (48 + size + 3) & ~(size_t)3;
    CHECK(end <= sizeof(expected));
    metadata_word(expected + 4, (uint32_t)((end - 8) / 4)); metadata_word(expected + 32, 0x4f434653);
    if (size) memcpy(expected + 48, data, size);
    CHECK(satchel_cpu_probe_read(cpu, 0x31000000, actual, 256));
    CHECK(!memcmp(actual, expected, end)); CHECK(!memcmp(actual + end, bytes + end, 256 - end));
}
static void test_applet_profile(void) {
    // Original fixture, independent sessions/domains and early/late real fault
    // reports. No Switch binaries, device events or backend self-tests involved.
    for (unsigned variant = 0; variant < 4; variant++) {
        int domain = variant & 1, early_fault = variant >> 1;
        uint8_t image[0x8000] = {0};
        const uint32_t code[] = {0xd40003e1,0xd40002c1,0xd4000181,0xd40001c1,0xd40004a1,0xd4000421};
        memcpy(image, code, sizeof(code)); memcpy(image + 0x1000, "sm:", 4);
        SatchelNSOInfo nso = {.image_size = sizeof(image), .bss_size = 0x5ff0,
            .segments = {{0,sizeof(code),0,sizeof(code)},{0,4096,0x1000,4096},{0,16,0x2000,16}}};
        SatchelSwitchProcessModule modules[] = {{"rtld",nso,image,sizeof(image)},{"main",nso,image,sizeof(image)}};
        SatchelProcessMetadata metadata = {.program_id=1,.address_space_size=UINT64_C(1)<<39,
            .main_thread_stack_size=0x4000,.address_space_bits=39,.address_space_type=3,.flags0=7,.is_64_bit=true};
        char error[256]; SatchelSwitchProcess *p = satchel_switch_process_create(modules,2,&metadata,error,sizeof(error)); CHECK(p);
        SatchelSwitchAppletState state, expected = {.focus_state=3,.operation_mode=0};
        CHECK(satchel_switch_process_applet_state(p,&state) && !memcmp(&state,&expected,sizeof(state)));
        CHECK(!satchel_switch_process_applet_state(NULL,&state) && !memcmp(&state,&expected,sizeof(state)));
        CHECK(!satchel_switch_process_applet_state(p,NULL));
        CHECK(!satchel_switch_process_report_gpu_error(NULL) && !satchel_switch_process_gpu_error_detected(NULL));
        CHECK(satchel_switch_process_gpu_error_event_signaled(NULL)==-1 && !satchel_switch_process_is_gpu_error_event(NULL,1));
        CHECK(!satchel_switch_process_gpu_error_detected(p) && satchel_switch_process_gpu_error_event_signaled(p)==-1);
        if (early_fault) CHECK(satchel_switch_process_report_gpu_error(p));
        CHECK(satchel_switch_process_gpu_error_detected(p)==early_fault && satchel_switch_process_gpu_error_event_signaled(p)==-1);
        uint8_t bytes[256], name[8]={'a','p','p','l','e','t','O','E'};
        uint32_t sm=port_call(p,0x8001000,0);
        tipc_sm_request(bytes,0,1,NULL,0); sm_ipc(p,sm,bytes,1,0,0);
        tipc_sm_request(bytes,1,0,name,8); uint32_t root=lm_reply(p,sm,bytes,1,NULL,0), proxy;
        if (domain) {
            sm_request(bytes,0,0,name,5,0,0); metadata_word(bytes+4,8);
            const uint8_t id[4]={1}; lm_reply(p,root,bytes,0,id,4);
            applet_proxy_request(bytes,1,0xffff8001,1,1,0); domain_reply(p,root,bytes,2,0); proxy=2;
        } else { applet_proxy_request(bytes,0,0xffff8001,1,1,0); proxy=lm_reply(p,root,bytes,1,NULL,0); }
        uint32_t children[4], commands[4]={0,1,2,20};
        for (unsigned i=0;i<4;i++) {
            applet_profile_request(bytes,domain?proxy:0,commands[i],NULL,0);
            if (domain) { children[i]=3+i; domain_reply(p,root,bytes,children[i],0); }
            else children[i]=lm_reply(p,proxy,bytes,1,NULL,0);
        }
        // Typed read-only profile uses u8 operation/focus versus u32 performance.
        uint32_t query_ids[3]={5,6,9}; uint8_t outputs[3][4]={{0},{0},{3}};
        for (unsigned i=0;i<3;i++) {
            applet_profile_request(bytes,domain?children[0]:0,query_ids[i],NULL,0);
            applet_value_reply(p,domain?root:children[0],bytes,domain,outputs[i],i==1?4:1);
            applet_profile_request(bytes,domain?children[0]:0,query_ids[i],name,4);
            sm_ipc(p,domain?root:children[0],bytes,0,0,0);
        }
        applet_profile_request(bytes,domain?children[0]:0,0,NULL,0);
        uint32_t message=applet_event_reply(p,domain?root:children[0],bytes,domain);
        applet_profile_request(bytes,domain?children[1]:0,91,NULL,0);
        uint32_t suspended=applet_event_reply(p,domain?root:children[1],bytes,domain);
        CHECK(!satchel_switch_process_is_gpu_error_event(p,message) && !satchel_switch_process_is_gpu_error_event(p,suspended));
        // Wrong130 schema cannot publish an event, even with a sticky fault.
        applet_profile_request(bytes,domain?children[3]:0,130,name,4);
        sm_ipc(p,domain?root:children[3],bytes,0,0,0);
        CHECK(satchel_switch_process_gpu_error_event_signaled(p)==-1);
        uint32_t fillers[32]; size_t count=32-(domain?5:10);
        for(size_t i=0;i<count;i++) fillers[i]=port_call(p,0x8001000,0);
        applet_profile_request(bytes,domain?children[3]:0,130,NULL,0);
        sm_ipc(p,domain?root:children[3],bytes,0,0,0);
        SatchelSwitchIPCInfo info; CHECK(satchel_switch_process_last_ipc(p,&info) && info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_RESOURCE);
        CHECK(satchel_switch_process_gpu_error_event_signaled(p)==-1 && satchel_switch_process_readable_event_handles(p)==2);
        handle_close(p,fillers[0],0);
        uint32_t gpu=applet_event_reply(p,domain?root:children[3],bytes,domain);
        CHECK(gpu==fillers[count-1]+1 && satchel_switch_process_is_gpu_error_event(p,gpu));
        for(size_t i=1;i<count;i++) handle_close(p,fillers[i],0);
        CHECK(satchel_switch_process_gpu_error_event_signaled(p)==early_fault);
        CHECK(satchel_switch_process_report_gpu_error(p) && satchel_switch_process_report_gpu_error(p));
        CHECK(satchel_switch_process_gpu_error_detected(p) && satchel_switch_process_gpu_error_event_signaled(p)==1);
        CHECK(satchel_switch_process_applet_event_signaled(p)==0 && satchel_switch_process_suspended_tick_event_signaled(p)==0);
        // Known policy setters own exact boolean bytes, ignoring transport pad.
        uint32_t setters[4]={11,12,13,16};
        for(unsigned pass=0;pass<2;pass++) for(unsigned i=0;i<4;i++) {
            uint8_t values[3]={(uint8_t)pass,(uint8_t)!pass,(uint8_t)pass}; size_t size=i==2?3:1;
            applet_profile_request(bytes,domain?children[1]:0,setters[i],values,size);
            // Nonzero padding is legal and must not become another policy bit.
            bytes[(domain?48:32)+size]=0xe7;
            applet_value_reply(p,domain?root:children[1],bytes,domain,NULL,0);
            if(i==0) expected.operation_notifications=values[0];
            if(i==1) expected.performance_notifications=values[0];
            if(i==2) {memcpy(expected.focus_handling,values,3);expected.focus_handling_set=1;}
            if(i==3) {expected.out_of_focus_suspending=values[0];expected.out_of_focus_suspending_set=1;}
            CHECK(satchel_switch_process_applet_state(p,&state) && !memcmp(&state,&expected,sizeof(state)));
            values[0]=2; applet_profile_request(bytes,domain?children[1]:0,setters[i],values,size);
            sm_ipc(p,domain?root:children[1],bytes,0,0,0);
            CHECK(satchel_switch_process_applet_state(p,&state) && !memcmp(&state,&expected,sizeof(state)));
            applet_profile_request(bytes,domain?children[1]:0,setters[i],NULL,0);
            sm_ipc(p,domain?root:children[1],bytes,0,0,0);
            CHECK(satchel_switch_process_applet_state(p,&state) && !memcmp(&state,&expected,sizeof(state)));
        }
        for(unsigned i=0;i<2;i++) {
            applet_profile_request(bytes,domain?children[2]:0,10,NULL,0);
            applet_value_reply(p,domain?root:children[2],bytes,domain,NULL,0);
            expected.foreground_rights=1;
            CHECK(satchel_switch_process_applet_state(p,&state) && !memcmp(&state,&expected,sizeof(state)));
        }
        applet_profile_request(bytes,domain?children[3]:0,40,NULL,0); sm_ipc(p,domain?root:children[3],bytes,0,0,0);
        CHECK(satchel_switch_process_last_ipc(p,&info) && info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
        CHECK(satchel_switch_process_applet_event_signaled(p)==0 && satchel_switch_process_suspended_tick_event_signaled(p)==0);
        if(domain) handle_close(p,root,0);
        else {for(unsigned i=0;i<4;i++) handle_close(p,children[i],0);handle_close(p,proxy,0);handle_close(p,root,0);}
        CHECK(satchel_switch_process_is_gpu_error_event(p,gpu));
        handle_close(p,gpu,0); handle_close(p,gpu,0xE401);
        CHECK(!satchel_switch_process_is_gpu_error_event(p,gpu) && satchel_switch_process_gpu_error_event_signaled(p)==1);
        handle_close(p,message,0);handle_close(p,suspended,0);handle_close(p,sm,0);
        CHECK(!satchel_switch_process_readable_event_handles(p));
        satchel_switch_process_destroy(p);
    }
}

#if defined(SATCHEL_CPU_TESTING)
extern int satchel_switch_process_test_application_control_matches(const SatchelSwitchProcess *, const SatchelSwitchApplicationControl *);
#endif
static void test_application_control_attachment(void) {
    uint8_t image[0x8000]={0};const uint32_t code[]={0xd40003e1,0xd40002c1,0xd4000181,0xd40001c1,0xd40004a1,0xd4000421};
    memcpy(image,code,sizeof(code));memcpy(image+0x1000,"sm:",4);
    SatchelNSOInfo nso={.image_size=sizeof(image),.bss_size=0x5ff0,
        .segments={{0,sizeof(code),0,sizeof(code)},{0,4096,0x1000,4096},{0,16,0x2000,16}}};
    SatchelSwitchProcessModule modules[]={{"rtld",nso,image,sizeof(image)},{"main",nso,image,sizeof(image)}};
    SatchelProcessMetadata metadata={.program_id=UINT64_C(0x0100000000001000),.address_space_size=UINT64_C(1)<<39,
        .main_thread_stack_size=0x4000,.address_space_bits=39,.address_space_type=3,.flags0=7,.is_64_bit=true};
    char error[256];SatchelSwitchProcess *p=satchel_switch_process_create(modules,2,&metadata,error,sizeof(error));CHECK(p);
    SatchelSwitchApplicationControl input={metadata.program_id,metadata.program_id+0x1000,UINT32_MAX,UINT32_MAX,{0}};
    for(unsigned i=0;i<32;i++)input.rating_ages[i]=(int8_t)((int)i-128);
    const SatchelSwitchApplicationControl expected=input;
    SatchelSwitchApplicationControlInfo info={0xaaaa,0xbbbb,UINT64_MAX},sentinel=info;
    CHECK(!satchel_switch_process_application_control_info(NULL,&info)&&!memcmp(&info,&sentinel,sizeof(info)));
    CHECK(!satchel_switch_process_application_control_info(p,NULL));
    CHECK(satchel_switch_process_application_control_info(p,&info)&&!info.attached&&!info.attachment_count&&info.parental_policy==SATCHEL_SWITCH_PARENTAL_POLICY_UNCONFIGURED);
    CHECK(!satchel_switch_process_attach_application_control(NULL,&input)&&!satchel_switch_process_attach_application_control(p,NULL));
    input.application_id=0;CHECK(!satchel_switch_process_attach_application_control(p,&input));
    input.application_id=expected.application_id+1;CHECK(!satchel_switch_process_attach_application_control(p,&input));
    CHECK(satchel_switch_process_application_control_info(p,&info)&&!info.attached&&!info.attachment_count);
    input=expected;CHECK(satchel_switch_process_attach_application_control(p,&input));
    CHECK(!satchel_switch_process_attach_application_control(p,&input));
    memset(&input,0,sizeof(input)); // Input storage/lifetime is independent after copy.
#if defined(SATCHEL_CPU_TESTING)
    CHECK(satchel_switch_process_test_application_control_matches(p,&expected));
    CHECK(!satchel_switch_process_test_application_control_matches(p,&input));
#endif
    CHECK(satchel_switch_process_application_control_info(p,&info)&&info.attached==1&&info.attachment_count==1&&info.parental_policy==SATCHEL_SWITCH_PARENTAL_POLICY_UNCONFIGURED);
    // Matching verified metadata still cannot approve or initialize any persona.
    test_pctl(p);test_pctl_system(p);test_pctl_admin(p);test_pctl_readonly(p);
    test_parental_initialize(p,&expected);
    CHECK(!satchel_switch_process_attach_application_control(p,&expected));
    CHECK(satchel_switch_process_application_control_info(p,&info)&&info.attached==1&&info.attachment_count==1&&info.parental_policy==SATCHEL_SWITCH_PARENTAL_POLICY_UNCONFIGURED);
    satchel_switch_process_destroy(p);
    p=satchel_switch_process_create(modules,2,&metadata,error,sizeof(error));CHECK(p);
    (void)satchel_switch_process_run(p,0,NULL,NULL); // Any attempted run closes configuration, even with zero budget.
    CHECK(!satchel_switch_process_attach_application_control(p,&expected));
    CHECK(satchel_switch_process_application_control_info(p,&info)&&!info.attached&&!info.attachment_count&&info.parental_policy==SATCHEL_SWITCH_PARENTAL_POLICY_UNCONFIGURED);
    satchel_switch_process_destroy(p);
}

// Original seeded time/shared-memory fixtures. No captured game requests/data.
static uint64_t time_read64(const uint8_t *bytes) {
    uint64_t value=0;for(unsigned i=0;i<8;i++)value|=(uint64_t)bytes[i]<<(8*i);return value;
}
static int64_t time_reference_floor(__int128 numerator,__int128 denominator) {
    __int128 q=numerator/denominator,r=numerator%denominator;return (int64_t)(q-(r<0));
}
static SatchelSwitchProcess *time_fixture(unsigned bits,uint32_t resource,uint32_t stack) {
    uint8_t image[0x8000]={0};
    const uint32_t code[]={0xd40003e1,0xd40002c1,0xd4000181,0xd40001c1,0xd40004a1,0xd4000421,
        0xd4000261,0xd4000281,0xd4000521,0xd4000021,0xd4000041,0xd4000061};
    memcpy(image,code,sizeof(code));memcpy(image+0x1000,"sm:",4);
    SatchelNSOInfo nso={.image_size=sizeof(image),.bss_size=0x5ff0,
        .segments={{0,sizeof(code),0,sizeof(code)},{0,4096,0x1000,4096},{0,16,0x2000,16}}};
    SatchelSwitchProcessModule modules[]={{"rtld",nso,image,sizeof(image)},{"main",nso,image,sizeof(image)}};
    SatchelProcessMetadata metadata={.program_id=1,.address_space_size=UINT64_C(1)<<bits,
        .main_thread_stack_size=stack,.address_space_bits=(uint8_t)bits,.address_space_type=bits==39?3:1,
        .flags0=bits==39?7:3,.is_64_bit=true,.system_resource_size=resource};
    char error[256];SatchelSwitchProcess *p=satchel_switch_process_create(modules,2,&metadata,error,sizeof(error));CHECK(p);return p;
}
static SatchelSwitchTimeSeed time_seed(SatchelSwitchProcess *p) {
    SatchelSwitchTimeSeed seed={.version=1,.size=64,.unix_seconds=1700000000,.unix_nanoseconds=987654321};
    memcpy(seed.location_name,"Etc/UTC",8);CHECK(satchel_switch_process_get_system_tick(p,&seed.system_tick));return seed;
}
static void time_shared_call(SatchelSwitchProcess *p,int unmap,uint64_t handle,uint64_t address,uint64_t size,uint64_t permission,uint32_t expected) {
    SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);uint64_t value;
    CHECK(satchel_cpu_probe_set_pc(cpu,unmap?0x800001c:0x8000018));
    CHECK(satchel_cpu_probe_set_x(cpu,0,handle)&&satchel_cpu_probe_set_x(cpu,1,address)&&satchel_cpu_probe_set_x(cpu,2,size)&&
        satchel_cpu_probe_set_x(cpu,3,permission)&&satchel_cpu_probe_set_x(cpu,4,UINT64_C(0xabbacddcbaab)));
    SatchelCPUProbeResult result=satchel_switch_process_run(p,1,NULL,NULL);CHECK(result.reason==SATCHEL_CPU_BUDGET&&result.steps==1);
    CHECK(satchel_cpu_probe_get_x(cpu,0,&value));
    if(value!=expected)fprintf(stderr,"shared fixture mismatch unmap%d address%llx size%llu permission%llu actual%x expected%x\n",unmap,(unsigned long long)address,(unsigned long long)size,(unsigned long long)permission,(unsigned)value,expected);
    CHECK(value==expected);
    CHECK(satchel_cpu_probe_get_x(cpu,1,&value)&&value==address);CHECK(satchel_cpu_probe_get_x(cpu,2,&value)&&value==size);
    CHECK(satchel_cpu_probe_get_x(cpu,3,&value)&&value==permission);CHECK(satchel_cpu_probe_get_x(cpu,4,&value)&&value==UINT64_C(0xabbacddcbaab));
}
static uint64_t time_memory_info(SatchelSwitchProcess *p,unsigned type) {
    SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);uint64_t value;
    CHECK(satchel_cpu_probe_set_pc(cpu,0x8000020)&&satchel_cpu_probe_set_x(cpu,1,type)&&satchel_cpu_probe_set_x(cpu,2,0xffff8001)&&satchel_cpu_probe_set_x(cpu,3,0));
    SatchelCPUProbeResult result=satchel_switch_process_run(p,1,NULL,NULL);CHECK(result.reason==SATCHEL_CPU_BUDGET&&result.steps==1);
    CHECK(satchel_cpu_probe_get_x(cpu,0,&value)&&value==0);CHECK(satchel_cpu_probe_get_x(cpu,1,&value));return value;
}
static void time_message(uint8_t bytes[256],uint32_t object,uint32_t command,const void *input,size_t size) {
    if(object){domain_request(bytes,object,command,0,input,size,0,0);metadata_word(bytes,6);}
    else {
        CHECK(size<=64&&!(size&3));memset(bytes,0,256);metadata_word(bytes,4);metadata_word(bytes+4,(uint32_t)(8+size/4));
        metadata_word(bytes+16,0x49434653);metadata_word(bytes+24,command);if(size)memcpy(bytes+32,input,size);
        memset(bytes+112,0xa7,144);
    }
}
static uint32_t time_reply(SatchelSwitchProcess *p,uint32_t handle,uint8_t request[256],int domain,
                           uint32_t result,void *output,size_t size,int copied) {
    SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);uint64_t status;uint8_t bytes[256],expected[256]={0};
    CHECK(satchel_cpu_probe_write(cpu,0x31000000,request,256)&&satchel_cpu_probe_set_pc(cpu,0x8000014)&&satchel_cpu_probe_set_x(cpu,0,handle));
    SatchelCPUProbeResult stop=satchel_switch_process_run(p,1,NULL,NULL);CHECK(stop.reason==SATCHEL_CPU_BUDGET&&stop.steps==1);
    CHECK(satchel_cpu_probe_get_x(cpu,0,&status)&&status==0&&satchel_cpu_probe_read(cpu,0x31000000,bytes,256));
    size_t data_at=domain?48:32,end=(data_at+size+3)&~(size_t)3;
    CHECK(end<=256);uint32_t token=0;if(copied)memcpy(&token,bytes+12,4);
    CHECK(!copied||token>=0x81);
    metadata_word(expected+4,(uint32_t)((end-(copied?16:8))/4)|(copied?0x80000000u:0));
    if(copied){metadata_word(expected+8,2);metadata_word(expected+12,token);}
    metadata_word(expected+data_at-16,0x4f434653);metadata_word(expected+data_at-8,result);
    if(size){memcpy(output,bytes+data_at,size);memcpy(expected+data_at,output,size);}
    CHECK(!memcmp(bytes,expected,end));CHECK(!memcmp(bytes+end,request+end,256-end));return token;
}
#if defined(SATCHEL_CPU_TESTING)
extern int satchel_cpu_probe_test_set_clock(SatchelCPUProbe*,uint64_t,int);
extern int satchel_switch_process_test_time_values(int64_t,int64_t,uint64_t,int64_t*,int64_t*);
static void test_time_math(void) {
    const int64_t bases[]={-999999999,-999999900,-1,0,1,500000000,999999999};
    const int64_t offsets[]={-1700000000,0,1700000000};
    uint64_t state=UINT64_C(0x4d595df4d0f33173);
    for(unsigned i=0;i<40000;i++) {
        state=state*UINT64_C(6364136223846793005)+1;
        uint64_t tick=i<24?i:state%UINT64_C(100000000000000000);
        int64_t base=bases[i%7],offset=offsets[i%3],steady=0,wall=0;
        CHECK(satchel_switch_process_test_time_values(base,offset,tick,&steady,&wall));
        __int128 ns=(__int128)tick*625/12;
        int64_t expected=time_reference_floor(ns+base,1000000000);
        CHECK(steady==expected&&wall==expected+offset);
    }
    int64_t steady=123,wall=456;
    CHECK(!satchel_switch_process_test_time_values(0,0,UINT64_MAX,&steady,&wall)&&steady==123&&wall==456);
    CHECK(!satchel_switch_process_test_time_values(0,INT64_MAX,19200000,&steady,&wall)&&steady==123&&wall==456);
    CHECK(!satchel_switch_process_test_time_values(-1,INT64_MIN,0,&steady,&wall)&&steady==123&&wall==456);
    CHECK(satchel_switch_process_test_time_values(-1,0,0,&steady,&wall)&&steady==-1&&wall==-1);
}
#endif
static void test_time_seed_validation(void) {
    SatchelSwitchProcess *p=time_fixture(39,0,0x4000);SatchelSwitchTimeSeed original=time_seed(p),seed=original;
    SatchelSwitchTimeInfo info,canary;memset(&info,0xa7,sizeof(info));canary=info;
    CHECK(!satchel_switch_process_time_info(NULL,&info)&&!memcmp(&info,&canary,sizeof(info)));
    CHECK(!satchel_switch_process_time_info(p,NULL));CHECK(!satchel_switch_process_attach_time_seed(NULL,&seed));CHECK(!satchel_switch_process_attach_time_seed(p,NULL));
    for(unsigned i=0;i<10;i++) {
        seed=original;
        switch(i){case 0:seed.version=2;break;case 1:seed.size=63;break;case 2:seed.unix_nanoseconds=1000000000;break;
        case 3:memset(seed.location_name,0,36);break;case 4:memset(seed.location_name,'a',36);break;
        case 5:seed.location_name[1]=(char)0xff;break;case 6:seed.location_name[1]=' ';break;
        case 7:seed.location_name[35]='x';break;case 8:seed.system_tick=UINT64_MAX;break;
        case 9:seed.system_tick=original.system_tick+192000000;break;}
        CHECK(!satchel_switch_process_attach_time_seed(p,&seed));CHECK(satchel_switch_process_time_info(p,&info)&&!info.configured&&!info.seed_attachments&&!info.shared_backing_bytes);
    }
#if defined(SATCHEL_CPU_TESTING)
    for(unsigned stage=1;stage<=2;stage++) {
        seed=time_seed(p);satchel_cpu_probe_test_mapping_allocation_failure(stage);
        CHECK(!satchel_switch_process_attach_time_seed(p,&seed));
        CHECK(satchel_switch_process_time_info(p,&info)&&!info.configured&&!info.shared_backing_bytes);
    }
#endif
    seed=time_seed(p);CHECK(satchel_switch_process_attach_time_seed(p,&seed));CHECK(!satchel_switch_process_attach_time_seed(p,&seed));
    CHECK(satchel_switch_process_time_info(p,&info)&&info.configured==1&&info.seed_attachments==1&&info.shared_backing_bytes==4096&&
        !info.network_initialized&&info.timezone_profile==SATCHEL_SWITCH_TIME_ZONE_HOST_NAME_ONLY&&!info.shared_handles&&!info.shared_mappings);
    satchel_switch_process_destroy(p);
    p=time_fixture(39,0,0x4000);seed=time_seed(p);(void)satchel_switch_process_run(p,0,NULL,NULL);
    CHECK(!satchel_switch_process_attach_time_seed(p,&seed));satchel_switch_process_destroy(p);
    // All fixed user bytes plus actual254MiB resource arena consume the256MiB
    // budget; attaching an additional real page must fail without overcommit.
    p=time_fixture(39,254*1024*1024,0x1ef000);seed=time_seed(p);
    CHECK(!satchel_switch_process_attach_time_seed(p,&seed));CHECK(satchel_switch_process_time_info(p,&info)&&!info.configured);
    satchel_switch_process_destroy(p);
#if defined(SATCHEL_CPU_TESTING)
    p=time_fixture(39,0,0x4000);SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);
    CHECK(satchel_cpu_probe_test_set_clock(cpu,UINT64_C(3000000000),0));seed=time_seed(p);
    seed.system_tick=SATCHEL_CPU_SYSTEM_TICK_FREQUENCY;CHECK(!satchel_switch_process_attach_time_seed(p,&seed));
    seed.system_tick=2*SATCHEL_CPU_SYSTEM_TICK_FREQUENCY-1;CHECK(!satchel_switch_process_attach_time_seed(p,&seed));
    seed.system_tick=2*SATCHEL_CPU_SYSTEM_TICK_FREQUENCY;seed.unix_seconds=INT64_MIN;CHECK(!satchel_switch_process_attach_time_seed(p,&seed));
    seed.unix_seconds=1700000000;CHECK(satchel_cpu_probe_test_set_clock(cpu,UINT64_C(3000000000),1));
    CHECK(!satchel_switch_process_attach_time_seed(p,&seed));CHECK(satchel_cpu_probe_test_set_clock(cpu,UINT64_C(3000000000),0));
    CHECK(satchel_switch_process_attach_time_seed(p,&seed));satchel_switch_process_destroy(p);
#endif
}
static void test_time_model(unsigned bits,int domain) {
    SatchelSwitchProcess *p=time_fixture(bits,0,0x4000);SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);
#if defined(SATCHEL_CPU_TESTING)
    CHECK(satchel_cpu_probe_test_set_clock(cpu,UINT64_C(2750000000),0));
#endif
    SatchelSwitchTimeSeed seed=time_seed(p);if(domain)seed.unix_nanoseconds=0;SatchelSwitchTimeSeed original=seed;
    CHECK(satchel_switch_process_attach_time_seed(p,&seed));memset(&seed,0,sizeof(seed));
    uint8_t request[256],name[8]={'t','i','m','e',':','u'},page[4096],expected_page[4096]={0},output[64];
    SatchelSwitchTimeInfo info;SatchelSwitchIPCInfo ipc;
    uint32_t sm=port_call(p,0x8001000,0);tipc_sm_request(request,0,1,NULL,0);sm_ipc(p,sm,request,1,0,0);
    tipc_sm_request(request,1,0,name,8);uint32_t root=lm_reply(p,sm,request,1,NULL,0),children[5];
    if(domain){sm_request(request,0,0,name,7,1,0);metadata_word(request+4,8);const uint8_t id[4]={1};lm_reply(p,root,request,0,id,4);}
    for(unsigned i=0;i<5;i++) {
        time_message(request,domain?1:0,i,NULL,0);
        if(domain){children[i]=i+2;domain_reply(p,root,request,children[i],0);}else children[i]=lm_reply(p,root,request,1,NULL,0);
    }
    time_message(request,domain?1:0,20,NULL,0);
#if defined(SATCHEL_CPU_TESTING)
    satchel_switch_process_test_fail_next_ipc_reply();sm_ipc(p,root,request,0,0,0);
    CHECK(satchel_switch_process_time_info(p,&info)&&!info.shared_handles&&!info.shared_mappings&&info.shared_backing_bytes==4096);
#endif
    uint32_t shared=time_reply(p,root,request,domain,0,NULL,0,1);
    CHECK(!satchel_switch_process_is_readable_event(p,shared));CHECK(satchel_switch_process_time_info(p,&info)&&info.shared_handles==1);
    uint64_t used=time_memory_info(p,22);CHECK(used==0x16000); // two32KiB modules+stack+TLS+shared.
    const uint64_t address=UINT64_C(0x40000000),second=address+0x10000;
    // Error precedence is the public SVC order, before any mapping mutation.
    const struct {uint64_t h,a,n,perm;uint32_t error;} invalid[]={
        {0,address+1,0,0,0xcc01},{0,address,0,0,0xca01},{0,address,1,0,0xca01},
        {0,UINT64_MAX-4095,4096,0,0xd401},{0,address,4096,0,0xd801},{0,address,4096,1,0xe401},
        {0x80,address,4096,1,0xe401},{0xffff8001,address,4096,1,0xe401},
        {shared,0,4096,1,0xdc01},{shared,UINT64_C(1)<<bits,4096,1,0xdc01},
        {shared,address,8192,3,0xca01},{shared,address,4096,3,0xd801},{shared,0x8000000,4096,1,0xd401},
        {shared,0x31000000,4096,1,0xd401}};
    for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++)time_shared_call(p,0,invalid[i].h,invalid[i].a,invalid[i].n,invalid[i].perm,invalid[i].error);
    uint64_t alias=bits==39?UINT64_C(0x1088200000):UINT64_C(0x80000000);
    uint64_t heap=bits==39?UINT64_C(0x2088200000):UINT64_C(0x200000000);
    time_shared_call(p,0,shared,alias,4096,1,0xdc01);time_shared_call(p,0,shared,heap,4096,1,0xdc01);
#if defined(SATCHEL_CPU_TESTING)
    for(unsigned failure=1;failure<=2;failure++){
        satchel_cpu_probe_test_mapping_allocation_failure(failure);time_shared_call(p,0,shared,address,4096,1,0xd001);
        SatchelSwitchMemoryInfo map;CHECK(satchel_switch_process_query(p,address,&map)&&map.state==0);
        CHECK(satchel_switch_process_time_info(p,&info)&&!info.shared_mappings&&info.shared_backing_bytes==4096);
    }
#endif
    time_shared_call(p,0,shared,address,4096,1,0);time_shared_call(p,0,shared,second,4096,1,0);
    attribute_call(p,address,4096,16,16,0,0xd401);
    attribute_call(p,address,4096,8,8,0,0xd401); // State validation precedes unsupported cache semantics.
    attribute_call(p,address,4096,0,0,0,0);
    permission_call(p,address,4096,1,0xd401); // Even unchanged Shared permission is not re-protectable.
    permission_call(p,address,4096,0,0xd401);permission_call(p,address,4096,3,0xd401);
    CHECK(time_memory_info(p,22)==used);CHECK(satchel_switch_process_time_info(p,&info)&&info.shared_mappings==2&&info.shared_maps_serviced==2);
    SatchelSwitchMemoryInfo mapping;CHECK(satchel_switch_process_query(p,address+4095,&mapping)&&mapping.address==address&&mapping.size==4096&&mapping.state==6&&mapping.permissions==1&&!mapping.attributes);
    CHECK(satchel_cpu_probe_read(cpu,address,page,4096));uint8_t duplicate[4096];CHECK(satchel_cpu_probe_read(cpu,second,duplicate,4096)&&!memcmp(page,duplicate,4096));
    CHECK(!satchel_cpu_probe_write(cpu,address,duplicate,4096));CHECK(!satchel_cpu_probe_check_range(cpu,address,4096,SATCHEL_CPU_WRITE));
    CHECK(satchel_cpu_probe_check_range(cpu,address,4096,SATCHEL_CPU_READ));
    int overlap=0;CHECK(satchel_cpu_probe_physical_overlap(cpu,address,4096,second,4096,&overlap)&&overlap);
    // Both seqlock slots use the same counter epoch/source. Build every defined
    // byte independently from the seed and compare all reserved bytes as well.
    int64_t ns=(int64_t)((__int128)original.system_tick*625/12),base=(int64_t)original.unix_nanoseconds-ns%1000000000;
    int64_t offset=original.unix_seconds-ns/1000000000;uint8_t source[16];memcpy(source,page+16,16);
    uint8_t zero[16]={0};CHECK(memcmp(source,zero,16)!=0);
    memcpy(expected_page+8,&base,8);memcpy(expected_page+16,source,16);memcpy(expected_page+32,expected_page+8,24);
    memcpy(expected_page+0x40,&offset,8);int64_t anchor=ns/1000000000;memcpy(expected_page+0x48,&anchor,8);memcpy(expected_page+0x50,source,16);
    memcpy(expected_page+0x60,expected_page+0x40,32);memcpy(expected_page+0xd8,&ns,8);
    memcpy(expected_page+0xf0,&base,8);memcpy(expected_page+0xf8,&base,8);memcpy(expected_page+0x100,source,16);
    memcpy(expected_page+0x110,expected_page+0xd8,56);CHECK(!memcmp(page,expected_page,4096));
    // Independent consumer arithmetic for the later continuous record, rather
    // than merely comparing serialized fields. Exact scale0 clamp applies across
    // second boundaries, including negative phase and the process epoch.
    const uint64_t samples[]={0,1,19199999,19200000,19200001,52799999,52800000,71999999,72000000,72000001,96000000};
    for(size_t i=0;i<sizeof(samples)/sizeof(samples[0]);i++) {
        __int128 tick_ns=(__int128)samples[i]*625/12;
        __int128 rtc=(int64_t)time_read64(page+0xd8),scale=(int64_t)time_read64(page+0xe0);
        unsigned shift=(unsigned)time_read64(page+0xe8);CHECK(shift<64);
        __int128 lower=(int64_t)time_read64(page+0xf0),upper=(int64_t)time_read64(page+0xf8);
        __int128 adjustment=((tick_ns-rtc)*scale>>shift)+lower;
        if(adjustment<lower)adjustment=lower;if(adjustment>upper)adjustment=upper;
        int64_t continuous=time_reference_floor(tick_ns+adjustment,1000000000);
        int64_t ordinary=time_reference_floor(tick_ns+(int64_t)time_read64(page+8),1000000000);
        CHECK(continuous==ordinary);
        CHECK(ordinary+offset==original.unix_seconds+time_reference_floor((__int128)original.unix_nanoseconds+tick_ns-ns,1000000000));
    }
#if defined(SATCHEL_CPU_TESTING)
    CHECK(satchel_cpu_probe_test_set_clock(cpu,UINT64_C(3800000000),0));
#endif
    for(unsigned i=0;i<5;i++) {
        uint32_t endpoint=domain?root:children[i],object=domain?children[i]:0;
        time_message(request,object,0,NULL,0);uint64_t before,after;
        CHECK(satchel_switch_process_get_system_tick(p,&before));
        time_reply(p,endpoint,request,domain,i==1?0xce74:0,output,i==1?0:i==2?24:i==3?36:8,0);
        CHECK(satchel_switch_process_get_system_tick(p,&after));
        if(i==3)CHECK(!memcmp(output,original.location_name,36));
        else if(i!=1){int64_t lo=time_reference_floor((__int128)before*625/12+base,1000000000),hi=time_reference_floor((__int128)after*625/12+base,1000000000);
            if(i!=2){lo+=offset;hi+=offset;}int64_t actual=(int64_t)time_read64(output);CHECK(actual>=lo&&actual<=hi);
            if(i==2)CHECK(!memcmp(output+8,source,16));}
        if(i==0||i==1||i==4){time_message(request,object,2,NULL,0);time_reply(p,endpoint,request,domain,i==1?0xce74:0,output,i==1?0:32,0);
            if(i!=1)CHECK(!memcmp(output,page+0x40,32));
            for(unsigned setter=1;setter<=3;setter+=2){uint8_t args[32]={0};time_message(request,object,setter,args,setter==1?8:32);
                time_reply(p,endpoint,request,domain,0x274,NULL,0,0);}
        }
        // Validly encoded unknown commands and malformed known commands stop.
        time_message(request,object,UINT32_MAX,NULL,0);sm_ipc(p,endpoint,request,0,0,0);
        CHECK(satchel_switch_process_last_ipc(p,&ipc)&&ipc.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_COMMAND);
        time_message(request,object,0,name,8);sm_ipc(p,endpoint,request,0,0,0);
        CHECK(satchel_switch_process_last_ipc(p,&ipc)&&ipc.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_SCHEMA);
    }
    CHECK(satchel_cpu_probe_read(cpu,address,duplicate,4096)&&!memcmp(page,duplicate,4096));
    CHECK(!memcmp(page+0x98,zero,16)&&memcmp(page+16,page+0x98,16)); // Network source differs: libnx shared-path TimeMismatch102, IPC103.
    time_shared_call(p,0,shared,address,4096,1,0xd401);
    time_shared_call(p,1,shared,address,8192,0,0xca01);time_shared_call(p,1,shared,address+0x2000,4096,0,0xd401);
    time_shared_call(p,1,shared,0x31000000,4096,0,0xd401);
    handle_close(p,shared,0);CHECK(satchel_cpu_probe_read(cpu,address,duplicate,4096)&&!memcmp(duplicate,page,4096));
    time_shared_call(p,1,shared,address,4096,0,0xe401);
    time_message(request,domain?1:0,20,NULL,0);uint32_t replacement=time_reply(p,root,request,domain,0,NULL,0,1);CHECK(replacement>shared);
    time_shared_call(p,1,replacement,address,4096,0,0);CHECK(!satchel_cpu_probe_read(cpu,address,duplicate,1));
    CHECK(satchel_cpu_probe_read(cpu,second,duplicate,4096)&&!memcmp(page,duplicate,4096));time_shared_call(p,1,replacement,second,4096,0,0);
    CHECK(time_memory_info(p,22)==used); // Server still owns the actual unmapped page.
    CHECK(satchel_switch_process_time_info(p,&info)&&!info.shared_mappings&&info.shared_maps_serviced==2&&info.shared_unmaps_serviced==2&&info.clock_reads==5&&info.timezone_reads==1);
    // Distinct sibling contexts see the same immutable backing and lifetime.
    time_shared_call(p,0,replacement,address,4096,1,0);
    char error[128];SatchelCPUProbe *sibling=satchel_cpu_probe_create_shared(cpu,0x8000000,error,sizeof(error));CHECK(sibling);
    CHECK(satchel_cpu_probe_read(sibling,address,duplicate,4096)&&!memcmp(duplicate,page,4096));
    CHECK(!satchel_cpu_probe_write(sibling,address,duplicate,1));satchel_cpu_probe_destroy(sibling);
    // Kernel shared handles are not IPC endpoints or waitable event handles.
    CHECK(satchel_cpu_probe_write(cpu,0x31000000,request,256)&&satchel_cpu_probe_set_pc(cpu,0x8000014)&&satchel_cpu_probe_set_x(cpu,0,replacement));
    SatchelCPUProbeResult stop=satchel_switch_process_run(p,1,NULL,NULL);uint64_t value;CHECK(stop.reason==SATCHEL_CPU_BUDGET&&satchel_cpu_probe_get_x(cpu,0,&value)&&value==0xe401);
    CHECK(satchel_cpu_probe_read(cpu,0x31000000,duplicate,256)&&!memcmp(duplicate,request,256));
    // Copied-handle exhaustion cannot consume a token or lose server ownership.
    size_t filler_count=SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT-(domain?4:9);uint32_t fillers[SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT];
    for(size_t i=0;i<filler_count;i++)fillers[i]=port_call(p,0x8001000,0);
    time_message(request,domain?1:0,20,NULL,0);sm_ipc(p,root,request,0,0,0);
    CHECK(satchel_switch_process_last_ipc(p,&ipc)&&ipc.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_RESOURCE);
    CHECK(satchel_switch_process_time_info(p,&info)&&info.shared_handles==1&&info.shared_mappings==1);
    handle_close(p,fillers[0],0);time_message(request,domain?1:0,20,NULL,0);
    uint32_t retry=time_reply(p,root,request,domain,0,NULL,0,1);CHECK(retry>fillers[filler_count-1]);handle_close(p,retry,0);
    for(size_t i=1;i<filler_count;i++)handle_close(p,fillers[i],0);
    // Eight immutable initial CPU views plus24 shared aliases fill the actual
    // CPU32-view limit. The next map fails atomically without charging bytes.
    for(unsigned i=0;i<23;i++)time_shared_call(p,0,replacement,UINT64_C(0x50000000)+4096*i,4096,1,0);
    time_shared_call(p,0,replacement,UINT64_C(0x50000000)+4096*23,4096,1,0xce01);
    CHECK(time_memory_info(p,22)==used);
    CHECK(satchel_switch_process_query(p,UINT64_C(0x50000000),&mapping)&&mapping.size==23*4096&&mapping.state==6&&mapping.permissions==1);
    // QueryMemory truthfully merges like properties. Current CPU crossing of
    // repeated physical-page aliases is unsupported and fails without output.
    uint8_t crossing[2]={0xa7,0xb8};CHECK(!satchel_cpu_probe_read(cpu,UINT64_C(0x50000fff),crossing,2)&&crossing[0]==0xa7&&crossing[1]==0xb8);
    for(unsigned i=0;i<23;i++)time_shared_call(p,1,replacement,UINT64_C(0x50000000)+4096*i,4096,0,0);
    handle_close(p,replacement,0); // Final client close leaves mapped/server references intact.
    CHECK(satchel_switch_process_time_info(p,&info)&&!info.shared_handles&&info.shared_mappings==1);
    satchel_switch_process_destroy(p); // Final CPU owner reclaims token and mapped backing.
}

static void test_named_port_handles(void) {
    uint8_t image[0x8000] = {0};
    const uint32_t code[] = {0xd40003e1, 0xd40002c1, 0xd4000181, 0xd40001c1, 0xd40004a1, 0xd4000421};
    memcpy(image, code, sizeof(code)); memcpy(image + 0x1000, "sm:", 4);
    memcpy(image + 0x1ffe, "sm:", 4); // Readable string crosses two CPU regions.
    SatchelNSOInfo nso = {.image_size = sizeof(image), .bss_size = 0x5FF0,
        .segments = {{0, sizeof(code), 0, sizeof(code)}, {0, 4096, 0x1000, 4096}, {0, 16, 0x2000, 16}}};
    SatchelSwitchProcessModule modules[] = {{"rtld", nso, image, sizeof(image)}, {"main", nso, image, sizeof(image)}};
    SatchelProcessMetadata metadata = {.program_id = 1, .address_space_size = UINT64_C(1) << 39,
        .main_thread_stack_size = 0x4000, .address_space_bits = 39, .address_space_type = 3,
        .flags0 = 7, .is_64_bit = true, .main_thread_priority = 37, .default_cpu_id = 2};
    char error[256];
    SatchelSwitchProcess *p = satchel_switch_process_create(modules, 2, &metadata, error, sizeof(error)); CHECK(p);
    SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
    CHECK(!satchel_switch_process_is_sm_session(NULL, 0x80) && !satchel_switch_process_sm_sessions(NULL));
    CHECK(!satchel_switch_process_port_requests(NULL) && !satchel_switch_process_port_requests(p));
    CHECK(!satchel_switch_process_is_sm_session(p, 0x80) && !satchel_switch_process_is_sm_session(p, 0xffff8000));
    SatchelSwitchIPCInfo initial_info = {0};
    CHECK(!satchel_switch_process_last_ipc(p, &initial_info));
    CHECK(!satchel_switch_process_ipc_requests(NULL) && !satchel_switch_process_ipc_serviced(NULL));
    test_sm_ipc(p);
    test_tipc_sm(p);
    test_lm(p);
    test_lm_domain(p);
    test_apm(p);
    test_aoc_root(p);
    test_ectx_application(p);
    test_time_user(p);
    test_time_children(p);
    test_pctl(p);
    test_pctl_system(p);
    test_pctl_admin(p);
    test_pctl_readonly(p);
    test_hipc_session_close(p);
    test_parental_initialize(p,NULL);
    test_applet_objects(p);
    test_applet_profile();
    uint64_t thread_id = owned_thread_query(p, 2, 0x80, 0);
    CHECK(thread_id > UINT32_MAX);
    uint32_t first = port_call(p, 0x8001000, 0); CHECK(satchel_switch_process_is_sm_session(p, first));
    CHECK(satchel_switch_process_sm_sessions(p) == 1);
    CHECK(owned_thread_query(p, 0, first, 0xE401) == 0);
    uint32_t second = port_call(p, 0x8001ffe, 0); CHECK(second > first);
    // CopyString stops at NUL; it must not read nonexistent trailing bytes.
    CHECK(satchel_cpu_probe_write(cpu, 0x31000ffc, "sm:", 4));
    uint32_t third = port_call(p, 0x31000ffc, 0); CHECK(third > second);
    CHECK(satchel_cpu_probe_write(cpu, 0x31000ffd, "sm:", 3));
    port_call(p, 0x31000ffd, 0xE601);
    port_call(p, UINT64_MAX, 0xE601); port_call(p, 0, 0xE601);
    port_call(p, UINT64_C(0x1000000031000000), 0xE601); // Full64-bit pointer, no truncation.
    uint64_t text = 0x8002100;
    const char unknown[] = "not-sm";
    CHECK(satchel_cpu_probe_write(cpu, text, unknown, sizeof(unknown))); port_call(p, text, 0xF201);
    CHECK(satchel_cpu_probe_write(cpu, text, "", 1)); port_call(p, text, 0xF201);
    char name[13]; memset(name, 'x', sizeof(name)); name[11] = 0;
    CHECK(satchel_cpu_probe_write(cpu, text, name, sizeof(name))); port_call(p, text, 0xF201);
    name[11] = 'x'; name[12] = 0;
    CHECK(satchel_cpu_probe_write(cpu, text, name, sizeof(name))); port_call(p, text, 0xEE01);
    CHECK(satchel_switch_process_sm_sessions(p) == 3);
    handle_close(p, first, 0); CHECK(!satchel_switch_process_is_sm_session(p, first));
    handle_close(p, first, 0xE401); handle_close(p, 0, 0xE401);
    handle_close(p, 0xffff8000, 0xE401); handle_close(p, 0xffff8001, 0xE401);
    uint32_t replacement = port_call(p, 0x8001000, 0); CHECK(replacement > third && replacement != first);
    CHECK(!satchel_switch_process_is_sm_session(p, first) && satchel_switch_process_is_sm_session(p, replacement));
    // No successful IPC response may be invented just because the session exists.
    CHECK(satchel_cpu_probe_set_pc(cpu, 0x8000014)); CHECK(satchel_cpu_probe_set_x(cpu, 0, replacement));
    SatchelCPUProbeResult result = satchel_switch_process_run(p, 1, NULL, NULL);
    CHECK(result.reason == SATCHEL_CPU_SVC && result.svc == 0x21 && result.steps == 1);
    uint64_t value; CHECK(satchel_cpu_probe_get_x(cpu, 0, &value) && value == replacement);
    handle_close(p, UINT64_C(0x1234567800000000) | replacement, 0); // Close input is W0.
    CHECK(!satchel_switch_process_is_sm_session(p, replacement));
    // Exhaustion leaves all live objects intact and consumes no token.
    uint32_t live[SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT]; size_t count = 0;
    live[count++] = second; live[count++] = third;
    while (count < SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT - 1) live[count++] = port_call(p, 0x8001000, 0);
    uint32_t last = live[count - 1];
    port_call(p, 0x8001000, 0xD201);
    CHECK(satchel_switch_process_sm_sessions(p) == SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT - 1);
    for (size_t i = 0; i < count; i++) CHECK(satchel_switch_process_is_sm_session(p, live[i]));
    // Lookup errors take precedence over capacity; neither consumes a handle.
    port_call(p, text, 0xEE01); port_call(p, 0x8002110, 0xF201);
    handle_close(p, live[0], 0);
    uint32_t fresh = port_call(p, 0x8001000, 0); CHECK(fresh == last + 1);
    CHECK(!satchel_switch_process_is_sm_session(p, live[0]));
    // Closing the main-thread reference keeps the running object/pseudo alive.
    handle_close(p, 0x80, 0); handle_close(p, 0x80, 0xE401);
    for (unsigned which = 0; which < 3; which++) {
        CHECK(owned_thread_query(p, which, 0x80, 0xE401) == 0);
        uint64_t expected = which == 0 ? 37 : (which == 1 ? 2 : thread_id);
        CHECK(owned_thread_query(p, which, 0xffff8000, 0) == expected);
    }
    uint32_t after_thread = port_call(p, 0x8001000, 0); CHECK(after_thread > fresh);
    CHECK(satchel_switch_process_sm_sessions(p) == SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT);
    port_call(p, 0x8001000, 0xD201);
    CHECK(satchel_switch_process_port_requests(p) > SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT);
    // SM->LM and LM->ILogger reserve no handles until reply commit. Exhaustion
    // must preserve TLS/state and the next token for the later successful call.
    uint8_t request[256], lm_name[8] = {'l','m'};
    tipc_sm_request(request, 0, 1, NULL, 0); sm_ipc(p, after_thread, request, 1, 0, 0);
    tipc_sm_request(request, 1, 0, lm_name, 8); sm_ipc(p, after_thread, request, 0, 0, 0);
    SatchelSwitchIPCInfo ipc;
    CHECK(satchel_switch_process_last_ipc(p, &ipc) && ipc.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_RESOURCE);
    handle_close(p, live[1], 0);
    uint32_t lm = lm_reply(p, after_thread, request, 1, NULL, 0);
    CHECK(lm == after_thread + 1 && satchel_switch_process_is_lm_session(p, lm));
    sm_request(request, 0, 1, lm_name, 4, 0, 0); sm_ipc(p, lm, request, 0, 0, 0);
    CHECK(satchel_switch_process_last_ipc(p, &ipc) && ipc.blocked_reason == SATCHEL_SWITCH_IPC_BLOCK_RESOURCE);
    handle_close(p, live[2], 0);
    uint32_t logger = lm_reply(p, lm, request, 1, NULL, 0);
    CHECK(logger == lm + 1 && satchel_switch_process_is_logger(p, logger));
    handle_close(p, lm, 0); CHECK(satchel_switch_process_is_logger(p, logger));
    handle_close(p, logger, 0); CHECK(!satchel_switch_process_is_logger(p, logger));
    satchel_switch_process_destroy(p);
}

static void alias_call(SatchelSwitchProcess *p, int unmap, uint64_t destination, uint64_t source,
                        uint64_t size, uint32_t status) {
    SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);
    CHECK(satchel_cpu_probe_set_pc(cpu,0x800001c+(unmap?4:0)));
    CHECK(satchel_cpu_probe_set_x(cpu,0,destination) && satchel_cpu_probe_set_x(cpu,1,source) && satchel_cpu_probe_set_x(cpu,2,size));
    uint64_t serviced=unmap?satchel_switch_process_alias_unmap_serviced(p):satchel_switch_process_alias_map_serviced(p);
    SatchelCPUProbeResult result=satchel_switch_process_run(p,1,NULL,NULL);
    CHECK(result.reason==SATCHEL_CPU_BUDGET && result.steps==1);
    uint64_t value; CHECK(satchel_cpu_probe_get_x(cpu,0,&value) && value==status);
    CHECK(satchel_cpu_probe_get_x(cpu,1,&value) && value==source);
    CHECK(satchel_cpu_probe_get_x(cpu,2,&value) && value==size);
    CHECK((unmap?satchel_switch_process_alias_unmap_serviced(p):satchel_switch_process_alias_map_serviced(p))==serviced+!status);
}
static void alias_heap_request(SatchelSwitchProcess *p,uint64_t size,int stopped,uint32_t status) {
    SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);
    CHECK(satchel_cpu_probe_set_pc(cpu,0x8000010) && satchel_cpu_probe_set_x(cpu,0,0xabc) && satchel_cpu_probe_set_x(cpu,1,size));
    SatchelCPUProbeResult result=satchel_switch_process_run(p,1,NULL,NULL); uint64_t value;
    CHECK(result.reason==(stopped?SATCHEL_CPU_SVC:SATCHEL_CPU_BUDGET) && result.steps==1);
    CHECK(satchel_cpu_probe_get_x(cpu,0,&value) && value==(stopped?0xabc:status));
    if(stopped) CHECK(satchel_cpu_probe_get_x(cpu,1,&value) && value==size);
}
static void test_alias_memory(unsigned bits) {
    uint8_t image[0x12000]={0};
    const uint32_t code[]={0xd4000581,0xd40005a1,0xd4000521,0xd40000c1,0xd4000021,
        0xf9400043,0xf9000043,0xd4000081,0xd40000a1};
    memcpy(image,code,sizeof(code));
    SatchelNSOInfo nso={.image_size=sizeof(image),.bss_size=sizeof(image)-0x2010,
        .segments={{0,sizeof(code),0,sizeof(code)},{0,4,0x1000,4},{0,16,0x2000,16}}};
    SatchelSwitchProcessModule modules[]={{"rtld",nso,image,sizeof(image)},{"main",nso,image,sizeof(image)}};
    SatchelProcessMetadata metadata={.program_id=1,.address_space_size=UINT64_C(1)<<bits,.main_thread_stack_size=0x4000,
        .address_space_bits=(uint8_t)bits,.address_space_type=bits==39?3:1,.flags0=bits==39?7:3,.is_64_bit=true,
        .system_resource_size=16*1024*1024};
    char error[256]; SatchelSwitchProcess *p=satchel_switch_process_create(modules,2,&metadata,error,sizeof(error)); CHECK(p);
    SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);
    uint64_t stack=bits==39?physical_info(p,14):UINT64_C(0x30000000);
    uint64_t destination=stack+0x20000,source=0x8003000;
    uint64_t baseline=physical_info(p,22),budget=physical_info(p,21),physical=physical_info(p,2),heap=physical_info(p,4);
    CHECK(!satchel_switch_process_alias_memory_size(NULL) && !satchel_switch_process_alias_map_requests(NULL) &&
        !satchel_switch_process_alias_unmap_requests(NULL) && !satchel_switch_process_alias_map_serviced(NULL) && !satchel_switch_process_alias_unmap_serviced(NULL));
    for(int unmap=0;unmap<2;unmap++) {
        alias_call(p,unmap,destination+1,source+1,0,0xCC01);
        alias_call(p,unmap,destination,source+1,0,0xCC01);
        alias_call(p,unmap,destination,source,0,0xCA01);
        alias_call(p,unmap,destination,source,4097,0xCA01);
        alias_call(p,unmap,destination,UINT64_MAX-4095,4096,0xD401);
        alias_call(p,unmap,UINT64_MAX-4095,source,4096,0xD401);
        alias_call(p,unmap,destination,metadata.address_space_size,4096,0xD401);
        alias_call(p,unmap,physical,source,4096,0xDC01);
        alias_call(p,unmap,destination,0x8000000,4096,0xD401); // Executable Code cannot alias.
        alias_call(p,unmap,destination,0x31000000,4096,0xD401); // TLS cannot alias.
        alias_call(p,unmap,destination,stack,4096,0xD401); // Existing Stack cannot alias.
    }
    uint64_t marker=UINT64_C(0x12345678abcdef01),actual=0;
    for(unsigned i=0;i<3;i++) CHECK(satchel_cpu_probe_write(cpu,source+i*4096,&marker,8));
    alias_call(p,0,stack,source,4096,0xD401); // Occupied destination.
    SatchelSwitchMemoryInfo info;
#if defined(SATCHEL_CPU_TESTING)
    for(unsigned stage=1;stage<=2;stage++) {
        satchel_cpu_probe_test_mapping_allocation_failure(stage);
        alias_call(p,0,destination,source,3*4096,0xD001);
        CHECK(!satchel_switch_process_alias_memory_size(p) && physical_info(p,22)==baseline);
        CHECK(satchel_switch_process_query(p,source,&info) && info.permissions==3 && !info.attributes);
        CHECK(satchel_cpu_probe_read(cpu,source,&actual,8) && actual==marker);
        CHECK(!satchel_cpu_probe_read(cpu,destination,&actual,8));
    }
#endif
    alias_call(p,0,destination,source,3*4096,0);
    CHECK(satchel_switch_process_alias_memory_size(p)==3*4096 && physical_info(p,22)==baseline && physical_info(p,21)==budget);
    CHECK(satchel_switch_process_query(p,source,&info) && info.address==source && info.size==3*4096 && info.state==4 && !info.permissions && info.attributes==1);
    CHECK(satchel_switch_process_query(p,source-1,&info) && info.permissions==3 && !info.attributes && info.size==4096);
    CHECK(satchel_switch_process_query(p,destination,&info) && info.address==destination && info.size==3*4096 && info.state==0xb && info.permissions==3 && !info.attributes);
    CHECK(!satchel_cpu_probe_read(cpu,source,&actual,8) && !satchel_cpu_probe_write(cpu,source,&marker,8));
    CHECK(!satchel_cpu_probe_check_range(cpu,source,4096,SATCHEL_CPU_READ));
    CHECK(satchel_cpu_probe_read(cpu,destination,&actual,8) && actual==marker);
    // Real interpreted writes cross an original page boundary through alias.
    uint64_t changed=UINT64_C(0xfedcba9876543210);
    CHECK(satchel_cpu_probe_set_x(cpu,2,destination+4092) && satchel_cpu_probe_set_x(cpu,3,changed) && satchel_cpu_probe_set_pc(cpu,0x8000018));
    CHECK(satchel_switch_process_run(p,1,NULL,NULL).reason==SATCHEL_CPU_BUDGET);
    CHECK(satchel_cpu_probe_read(cpu,destination+4092,&actual,8) && actual==changed);
    alias_call(p,0,destination+0x10000,source,4096,0xD401); // Already locked, not a no-op.
    alias_call(p,0,destination,source+3*4096,4096,0xD401); // Overlapping destination, not a no-op.
    alias_call(p,1,destination,source+4096,4096,0xDC01); // Both valid states but wrong backing offsets.
#if defined(SATCHEL_CPU_TESTING)
    for(unsigned stage=1;stage<=2;stage++) {
        satchel_cpu_probe_test_mapping_allocation_failure(stage);
        alias_call(p,1,destination+4096,source+4096,4096,0xD001);
        CHECK(satchel_switch_process_alias_memory_size(p)==3*4096 && physical_info(p,22)==baseline);
        CHECK(satchel_switch_process_query(p,source+4096,&info) && !info.permissions && info.attributes==1);
        CHECK(satchel_cpu_probe_read(cpu,destination+4092,&actual,8) && actual==changed);
    }
#endif
    alias_call(p,1,destination+4096,source+4096,4096,0);
    CHECK(satchel_switch_process_alias_memory_size(p)==8192 && physical_info(p,22)==baseline);
    CHECK(satchel_switch_process_query(p,source+4096,&info) && info.address==source+4096 && info.size==4096 && info.permissions==3 && !info.attributes);
    CHECK(satchel_switch_process_query(p,destination+4096,&info) && !info.state && !info.permissions);
    uint32_t word; CHECK(satchel_cpu_probe_read(cpu,source+4096,&word,4) && word==(uint32_t)(changed>>32));
    CHECK(!satchel_cpu_probe_read(cpu,destination+4096,&actual,8));
    alias_call(p,1,destination,source,4096,0);alias_call(p,1,destination+8192,source+8192,4096,0);
    CHECK(!satchel_switch_process_alias_memory_size(p) && satchel_cpu_probe_read(cpu,source+4092,&actual,8) && actual==changed);
    CHECK(satchel_switch_process_query(p,source,&info) && info.address==0x8002000 && info.size==0x10000 && info.permissions==3 && !info.attributes);
    alias_call(p,1,destination,source,4096,0xD401); // Not an unmap no-op.
    // Dynamic physical memory uses identical backing/lock policy.
    physical_call(p,0,physical,3*4096,0);
    CHECK(satchel_cpu_probe_write(cpu,physical+4096,&marker,8));
    alias_call(p,0,destination,physical+4096,4096,0);
    CHECK(satchel_cpu_probe_read(cpu,destination,&actual,8) && actual==marker);
    CHECK(satchel_switch_process_query(p,physical+4096,&info) && info.state==5 && !info.permissions && info.attributes==1);
    physical_call(p,1,physical+4096,4096,0xD401);
    physical_call(p,0,physical+4096,4096,0); // Existing locked allocation: genuine no-op, still inaccessible.
    CHECK(satchel_cpu_probe_set_pc(cpu,0x8000004) && satchel_cpu_probe_set_x(cpu,0,physical) && satchel_cpu_probe_set_x(cpu,1,4096));
    SatchelCPUProbeResult result=satchel_switch_process_run(p,1,NULL,NULL);
    CHECK(result.reason==SATCHEL_CPU_SVC && result.svc==0x2d && result.steps==1);
    CHECK(satchel_cpu_probe_get_x(cpu,0,&actual) && actual==physical); // Valid but currently unsupported retained-allocation replacement.
    alias_call(p,1,destination,physical+4096,4096,0);
    CHECK(satchel_cpu_probe_read(cpu,physical+4096,&actual,8) && actual==marker);
    physical_call(p,1,physical,3*4096,0);
    CHECK(physical_info(p,22)==baseline);
    // Heap cannot discard locked pages; valid growth with live aliases stops.
    physical_heap(p,2*1024*1024,heap);
    CHECK(satchel_cpu_probe_write(cpu,heap+4096,&marker,8));
    alias_call(p,0,destination,heap+4096,4096,0);
    alias_heap_request(p,0,0,0xD401);alias_heap_request(p,4*1024*1024,1,0);
    CHECK(satchel_switch_process_heap_size(p)==2*1024*1024 && physical_info(p,22)==baseline+2*1024*1024);
    alias_call(p,1,destination,heap+4096,4096,0);
    physical_heap(p,4*1024*1024,heap);
    CHECK(satchel_cpu_probe_read(cpu,heap+4096,&actual,8) && actual==marker);
    physical_heap(p,0,heap);
    CHECK(physical_info(p,22)==baseline && !satchel_switch_process_alias_memory_size(p));
    // View-capacity failure is atomic even though no new physical bytes are
    // requested. Separate destination pages consume genuine bounded views.
    unsigned aliases = 0; int limited = 0;
    for (unsigned i = 0; i < 32; i++) {
        uint64_t src = satchel_switch_process_module_address(p,i/16)+0x2000+(i%16)*4096;
        uint64_t dst = destination+0x40000+i*8192;
        CHECK(satchel_cpu_probe_set_pc(cpu,0x800001c) && satchel_cpu_probe_set_x(cpu,0,dst) &&
            satchel_cpu_probe_set_x(cpu,1,src) && satchel_cpu_probe_set_x(cpu,2,4096));
        CHECK(satchel_switch_process_run(p,1,NULL,NULL).reason==SATCHEL_CPU_BUDGET);
        CHECK(satchel_cpu_probe_get_x(cpu,0,&actual));
        if (actual) {
            CHECK(actual==0xCE01); limited=1;
            CHECK(satchel_switch_process_query(p,src,&info) && info.permissions==3 && !info.attributes);
            CHECK(satchel_switch_process_query(p,dst,&info) && !info.state && !info.permissions);
            CHECK(satchel_switch_process_alias_memory_size(p)==aliases*4096 && physical_info(p,22)==baseline);
            break;
        }
        aliases++;
        CHECK(satchel_switch_process_alias_memory_size(p)==aliases*4096 && physical_info(p,22)==baseline);
    }
    CHECK(limited && aliases>0);
    for (unsigned i=0;i<aliases;i++) {
        uint64_t src=satchel_switch_process_module_address(p,i/16)+0x2000+(i%16)*4096;
        alias_call(p,1,destination+0x40000+i*8192,src,4096,0);
    }
    CHECK(!satchel_switch_process_alias_memory_size(p) && physical_info(p,22)==baseline);
    CHECK(satchel_switch_process_alias_map_requests(p)>satchel_switch_process_alias_map_serviced(p));
    CHECK(satchel_switch_process_alias_unmap_requests(p)>satchel_switch_process_alias_unmap_serviced(p));
    satchel_switch_process_destroy(p);
}

static void test_thread_queries(unsigned priority, unsigned core) {
    const unsigned services[] = {0xC, 0xE, 0x25};
    const uint64_t handles[] = {0x80, 0xFFFF8000, UINT64_C(0x1234567800000080),
        0, 0x81, 0xFFFF8001, 0xFFFF8002, UINT32_MAX};
    for (unsigned s = 0; s < 3; s++) {
        uint8_t image[0x3000] = {0};
        for (unsigned i = 0; i < 8; i++) {
            uint32_t instruction = 0xD4000001 | (services[s] << 5);
            memcpy(image + i * 4, &instruction, 4);
        }
        SatchelNSOInfo nso = {.image_size = sizeof(image), .bss_size = 0xFFC,
            .segments = {{0, 32, 0, 32}, {0, 4, 0x1000, 4}, {0, 4, 0x2000, 4}}};
        SatchelSwitchProcessModule modules[] = {{"rtld", nso, image, sizeof(image)}, {"main", nso, image, sizeof(image)}};
        SatchelProcessMetadata metadata = {.program_id = 1, .address_space_size = UINT64_C(1) << 39,
            .main_thread_stack_size = 0x4000, .address_space_bits = 39, .address_space_type = 3,
            .flags0 = 7, .is_64_bit = true, .main_thread_priority = (uint8_t)priority, .default_cpu_id = (uint8_t)core};
        char error[256];
        SatchelSwitchProcess *p = satchel_switch_process_create(modules, 2, &metadata, error, sizeof(error)); CHECK(p);
        SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
        uint8_t canary[32]; memset(canary, 0x8D, sizeof(canary));
        CHECK(satchel_cpu_probe_write(cpu, initial_stack39, canary, sizeof(canary)));
        uint64_t identity = 0;
        for (unsigned i = 0; i < 8; i++) {
            // For core-mask, both X0/X1 are wrapper-owned output pointers;
            // input handle is W2. For priority/ID the input handle is W1.
            CHECK(satchel_cpu_probe_set_x(cpu, 0, UINT64_MAX));
            CHECK(satchel_cpu_probe_set_x(cpu, 1, services[s] == 0xE ? UINT64_MAX : handles[i]));
            CHECK(satchel_cpu_probe_set_x(cpu, 2, services[s] == 0xE ? handles[i] : 0x1234));
            SatchelCPUProbeResult result = satchel_switch_process_run(p, 1, NULL, NULL);
            CHECK(result.reason == SATCHEL_CPU_BUDGET && result.steps == 1);
            uint64_t reg;
            CHECK(satchel_cpu_probe_get_x(cpu, 0, &reg) && reg == (i < 3 ? 0 : 0xE401));
            CHECK(satchel_cpu_probe_get_x(cpu, 1, &reg));
            if (i < 3) {
                if (services[s] == 0xC) CHECK(reg == priority);
                else if (services[s] == 0xE) CHECK(reg == core);
                else {
                    if (i == 0) identity = reg;
                    CHECK(reg == identity && identity > UINT32_MAX && identity != 0x80);
                }
            }
            CHECK(satchel_cpu_probe_get_x(cpu, 2, &reg));
            CHECK(reg == (services[s] == 0xE ? (i < 3 ? UINT64_C(1) << core : 0) : 0x1234));
        }
        CHECK(satchel_switch_process_thread_queries(p) == 8);
        CHECK(satchel_switch_process_info_requests(p) == 0);
        uint8_t actual[32];
        CHECK(satchel_cpu_probe_read(cpu, initial_stack39, actual, sizeof(actual)) && !memcmp(actual, canary, sizeof(actual)));
        if (services[s] == 0x25) {
            SatchelSwitchProcess *other = satchel_switch_process_create(modules, 2, &metadata, error, sizeof(error)); CHECK(other);
            SatchelCPUProbe *other_cpu = satchel_switch_process_cpu(other);
            CHECK(satchel_cpu_probe_set_x(other_cpu, 1, 0x80));
            SatchelCPUProbeResult result = satchel_switch_process_run(other, 1, NULL, NULL);
            CHECK(result.reason == SATCHEL_CPU_BUDGET && result.steps == 1);
            uint64_t other_id;
            CHECK(satchel_cpu_probe_get_x(other_cpu, 1, &other_id) && other_id != identity && other_id > UINT32_MAX);
            satchel_switch_process_destroy(other);
        }
        satchel_switch_process_destroy(p);
    }
}

static void test_regions(unsigned bits, int large_code) {
    size_t size = large_code ? 0x203000 : 0x3000;
    uint8_t *image = calloc(1, size); CHECK(image);
    for (unsigned i = 0; i < 16; i++) {
        uint32_t instruction = 0xd4000521;
        memcpy(image + i * 4, &instruction, 4);
    }
    SatchelNSOInfo nso = {.image_size = (uint32_t)size, .bss_size = (uint32_t)size - 0x2004,
        .segments = {{0, 64, 0, 64}, {0, 4, 0x1000, 4}, {0, 4, 0x2000, 4}}};
    SatchelSwitchProcessModule modules[] = {{"rtld", nso, image, size}, {"main", nso, image, 0x3000}};
    modules[1].info.image_size = 0x3000; modules[1].info.bss_size = 0xFFC;
    SatchelProcessMetadata metadata = {.program_id = 1, .address_space_size = UINT64_C(1) << bits,
        .main_thread_stack_size = 0x4000, .address_space_bits = (uint8_t)bits,
        .address_space_type = bits == 39 ? 3 : 1, .flags0 = bits == 39 ? 7 : 3, .is_64_bit = true};
    char error[256];
    SatchelSwitchProcess *p = satchel_switch_process_create(modules, 2, &metadata, error, sizeof(error)); CHECK(p);
    SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
    // Expected placement is anchored to the public no-ASLR region layout. A
    // second fixture crosses a 2-MiB code boundary to test size-derived bases.
    uint64_t stack39 = large_code ? UINT64_C(0x1008400000) : initial_stack39;
    uint64_t alias = bits == 39 ? stack39 + UINT64_C(0x80000000) : UINT64_C(0x80000000);
    uint64_t heap = bits == 39 ? alias + UINT64_C(0x1000000000) : UINT64_C(0x200000000);
    uint64_t stack = bits == 39 ? stack39 : UINT64_C(0x08000000);
    uint64_t allocated_stack = bits == 39 ? stack39 : UINT64_C(0x30000000);
    const uint32_t types[] = {2, 3, 4, 5, 12, 13, 14, 15, 28};
    const uint64_t expected[] = {alias, bits == 39 ? UINT64_C(0x1000000000) : UINT64_C(0x180000000),
        heap, UINT64_C(0x200000000), UINT64_C(0x08000000), metadata.address_space_size - 0x8000000,
        stack, bits == 39 ? UINT64_C(0x80000000) : UINT64_C(0x78000000), 0};
    for (unsigned i = 0; i < sizeof(types) / sizeof(types[0]); i++) {
        CHECK(satchel_cpu_probe_set_x(cpu, 0, UINT64_MAX));
        CHECK(satchel_cpu_probe_set_x(cpu, 1, types[i]));
        CHECK(satchel_cpu_probe_set_x(cpu, 2, 0xFFFF8001));
        CHECK(satchel_cpu_probe_set_x(cpu, 3, 0));
        SatchelCPUProbeResult result = satchel_switch_process_run(p, 1, NULL, NULL);
        CHECK(result.reason == SATCHEL_CPU_BUDGET && result.steps == 1);
        uint64_t reg;
        CHECK(satchel_cpu_probe_get_x(cpu, 0, &reg) && reg == 0);
        CHECK(satchel_cpu_probe_get_x(cpu, 1, &reg) && reg == expected[i]);
    }
    struct { uint32_t handle; uint64_t subtype; uint32_t result; } invalid[] = {
        {0, 0, 0xE401}, {0xFFFF8000, 0, 0xE401}, {0xFFFF8001, 1, 0xE801},
        {0, UINT64_MAX, 0xE801}, {0x80, 0, 0xE401}
    };
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        CHECK(satchel_cpu_probe_set_x(cpu, 1, 28));
        CHECK(satchel_cpu_probe_set_x(cpu, 2, invalid[i].handle));
        CHECK(satchel_cpu_probe_set_x(cpu, 3, invalid[i].subtype));
        SatchelCPUProbeResult result = satchel_switch_process_run(p, 1, NULL, NULL);
        CHECK(result.reason == SATCHEL_CPU_BUDGET && result.steps == 1);
        uint64_t reg;
        CHECK(satchel_cpu_probe_get_x(cpu, 0, &reg) && reg == invalid[i].result);
    }
    CHECK(satchel_switch_process_info_serviced(p) == 14);
    SatchelSwitchMemoryInfo info;
    CHECK(satchel_switch_process_query(p, heap, &info) && info.state == 0 && info.permissions == 0);
    CHECK(satchel_switch_process_query(p, heap + UINT64_C(0x1FFFFFFFF), &info) && info.state == 0);
    CHECK(satchel_switch_process_query(p, alias, &info) && info.state == 0);
    CHECK(satchel_switch_process_query(p, allocated_stack, &info) && info.state == 0xB && info.size == 0x4000);
    CHECK(satchel_switch_process_query(p, allocated_stack + 0x4000, &info) && info.state == 0);
    uint8_t byte;
    CHECK(!satchel_cpu_probe_read(cpu, heap, &byte, 1)); // A reservation is not a mapped heap.
    satchel_switch_process_destroy(p); free(image);
}

static void test_get_info(void) {
    uint8_t image[0x3000] = {0};
    for (unsigned i = 0; i < 16; i++) {
        uint32_t instruction = 0xd4000521; // Original SVC 0x29 fixture.
        memcpy(image + i * 4, &instruction, 4);
    }
    SatchelNSOInfo nso = {.image_size = sizeof(image), .bss_size = 0xFFC,
        .segments = {{0, 64, 0, 64}, {0, 4, 0x1000, 4}, {0, 4, 0x2000, 4}}};
    SatchelSwitchProcessModule modules[] = {{"rtld", nso, image, sizeof(image)}, {"main", nso, image, sizeof(image)}};
    SatchelProcessMetadata metadata = {.program_id = 1, .address_space_size = UINT64_C(1) << 39,
        .main_thread_stack_size = 0x4000, .address_space_bits = 39, .address_space_type = 3, .flags0 = 7, .is_64_bit = true};
    char error[256];
    SatchelSwitchProcess *p = satchel_switch_process_create(modules, 2, &metadata, error, sizeof(error)); CHECK(p);
    SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
    uint32_t type = 0xAA, handle = 0xBB; uint64_t subtype = 0xCC, reg;
    CHECK(!satchel_switch_process_last_info(p, &type, &handle, &subtype));
    CHECK(type == 0xAA && handle == 0xBB && subtype == 0xCC);
    CHECK(satchel_switch_process_info_requests(p) == 0 && satchel_switch_process_info_serviced(p) == 0);
    SatchelCPUProbeResult result = satchel_switch_process_run(p, 100, cancel, NULL);
    CHECK(result.reason == SATCHEL_CPU_CANCELLED && result.steps == 0);
    CHECK(satchel_switch_process_info_requests(p) == 0);

    uint8_t canary[32]; memset(canary, 0xA7, sizeof(canary));
    CHECK(satchel_cpu_probe_write(cpu, initial_stack39, canary, sizeof(canary)));
    uint64_t words[4] = {0};
    for (unsigned pass = 0; pass < 2; pass++) {
        for (unsigned i = 0; i < 4; i++) {
            // X0 is deliberately unmapped: GetInfo returns its value in X1.
            CHECK(satchel_cpu_probe_set_x(cpu, 0, UINT64_MAX));
            CHECK(satchel_cpu_probe_set_x(cpu, 1, 11));
            CHECK(satchel_cpu_probe_set_x(cpu, 2, 0));
            CHECK(satchel_cpu_probe_set_x(cpu, 3, i));
            result = satchel_switch_process_run(p, 1, NULL, NULL);
            CHECK(result.reason == SATCHEL_CPU_BUDGET && result.steps == 1);
            CHECK(satchel_cpu_probe_get_x(cpu, 0, &reg) && reg == 0);
            CHECK(satchel_cpu_probe_get_x(cpu, 1, &reg));
            if (pass == 0) words[i] = reg;
            else CHECK(reg == words[i]); // Stable process state; no random-value assumptions.
            CHECK(satchel_switch_process_last_info(p, &type, &handle, &subtype));
            CHECK(type == 11 && handle == 0 && subtype == i);
        }
    }
    struct { uint32_t handle; uint64_t subtype; uint32_t result; } invalid[] = {
        {0xFFFF8001, 2, 0xE401}, {0, 4, 0xE801},
        {1, UINT64_MAX, 0xE401}, {0, UINT64_MAX, 0xE801}
    };
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        CHECK(satchel_cpu_probe_set_x(cpu, 1, 11));
        CHECK(satchel_cpu_probe_set_x(cpu, 2, invalid[i].handle));
        CHECK(satchel_cpu_probe_set_x(cpu, 3, invalid[i].subtype));
        result = satchel_switch_process_run(p, 1, NULL, NULL);
        CHECK(result.reason == SATCHEL_CPU_BUDGET && result.steps == 1);
        CHECK(satchel_cpu_probe_get_x(cpu, 0, &reg) && reg == invalid[i].result);
        CHECK(satchel_switch_process_last_info(p, &type, &handle, &subtype));
        CHECK(type == 11 && handle == invalid[i].handle && subtype == invalid[i].subtype);
    }
    // No fabricated allowed-core mask, address-space answer, or future-info error.
    const uint32_t unsupported[] = {0, 36, UINT32_MAX};
    for (unsigned i = 0; i < 3; i++) {
        CHECK(satchel_cpu_probe_set_x(cpu, 0, 0x1234));
        CHECK(satchel_cpu_probe_set_x(cpu, 1, unsupported[i]));
        result = satchel_switch_process_run(p, 1, NULL, NULL);
        CHECK(result.reason == SATCHEL_CPU_SVC && result.svc == 0x29 && result.steps == 1);
        CHECK(satchel_cpu_probe_get_x(cpu, 0, &reg) && reg == 0x1234);
        CHECK(satchel_cpu_probe_get_x(cpu, 1, &reg) && reg == unsupported[i]);
    }
    // ABI type/handle are 32-bit; subtype remains the complete 64-bit value.
    CHECK(satchel_cpu_probe_set_x(cpu, 1, UINT64_C(0xABCDEF000000000B)));
    CHECK(satchel_cpu_probe_set_x(cpu, 2, UINT64_C(0x1234567800000000)));
    CHECK(satchel_cpu_probe_set_x(cpu, 3, 2));
    result = satchel_switch_process_run(p, 1, NULL, NULL);
    CHECK(result.reason == SATCHEL_CPU_BUDGET && result.steps == 1 && result.pc == 0x8000040);
    CHECK(satchel_cpu_probe_get_x(cpu, 0, &reg) && reg == 0);
    CHECK(satchel_cpu_probe_get_x(cpu, 1, &reg) && reg == words[2]);
    CHECK(satchel_switch_process_last_info(p, &type, &handle, &subtype));
    CHECK(type == 11 && handle == 0 && subtype == 2);
    CHECK(satchel_switch_process_info_requests(p) == 16 && satchel_switch_process_info_serviced(p) == 13);
    CHECK(satchel_switch_process_queries(p) == 0);
    uint8_t actual[32]; CHECK(satchel_cpu_probe_read(cpu, initial_stack39, actual, sizeof(actual)) && !memcmp(actual, canary, sizeof(actual)));
    type = 0xAA; handle = 0xBB;
    CHECK(!satchel_switch_process_last_info(p, &type, &handle, NULL) && type == 0xAA && handle == 0xBB);
    CHECK(!satchel_switch_process_last_info(NULL, &type, &handle, &subtype));
    satchel_switch_process_destroy(p);

    // Exercise process-configured guest TLS through instructions and checked RAM.
    const uint32_t tls_code[] = {0xd53bd065, 0xb94110a6, 0xd53bd047, 0xd4000fe1};
    memcpy(image, tls_code, sizeof(tls_code));
    p = satchel_switch_process_create(modules, 2, &metadata, error, sizeof(error)); CHECK(p);
    cpu = satchel_switch_process_cpu(p);
    uint64_t ro = 0, rw = 1;
    CHECK(satchel_cpu_probe_get_tls(cpu, &ro, &rw) && ro == 0x31000000 && rw == 0);
    result = satchel_switch_process_run(p, 100, NULL, NULL);
    CHECK(result.reason == SATCHEL_CPU_SVC && result.svc == 0x7f && result.steps == 4);
    CHECK(satchel_cpu_probe_get_x(cpu, 5, &reg) && reg == ro);
    CHECK(satchel_cpu_probe_get_x(cpu, 6, &reg) && reg == 0x80);
    CHECK(satchel_cpu_probe_get_x(cpu, 7, &reg) && reg == 0);
    CHECK(satchel_switch_process_info_requests(p) == 0);
    satchel_switch_process_destroy(p);
}

// Independent loader-style consumer: discover initial NSO text mappings via
// QueryMemory's Code/RX category, then read each synthetic MOD0 header. Generated
// RX code must remain visible but must not become a candidate NSO. This is not
// the proprietary runtime linker and does not establish actual-game progress.
static void check_nso_discovery(SatchelSwitchProcess *p, uint64_t address_space_size) {
    uint64_t cursor = 0;
    unsigned modules = 0, generated = 0, iterations = 0;
    while (cursor < address_space_size) {
        SatchelSwitchMemoryInfo info;
        CHECK(++iterations <= 48);
        CHECK(satchel_switch_process_query(p, cursor, &info));
        CHECK(info.address == cursor && info.size && info.size <= address_space_size - cursor);
        if (info.state == 3 && info.permissions == 5) {
            uint32_t offset = 0, magic = 0;
            CHECK(modules < 2 && info.address == satchel_switch_process_module_address(p, modules));
            CHECK(satchel_cpu_probe_read(satchel_switch_process_cpu(p), info.address + 4, &offset, sizeof(offset)));
            CHECK(offset <= info.size - 4);
            CHECK(satchel_cpu_probe_read(satchel_switch_process_cpu(p), info.address + offset, &magic, sizeof(magic)));
            CHECK(magic == 0x30444F4D);
            modules++;
        } else if (info.state == 0x14) {
            CHECK(info.address == satchel_switch_process_dispatch_address(p));
            CHECK(info.size == satchel_switch_process_dispatch_size(p) && info.permissions == 5);
            generated++;
        }
        cursor += info.size;
    }
    CHECK(modules == 2 && generated == 1);
}

// Original synthetic guest code; neither a game module nor an NVN success stub.
static void test_dispatch(unsigned bits, int large_code) {
    size_t image_size = large_code ? 0x203000 : 0x3000;
    uint8_t *image = calloc(1, image_size); CHECK(image);
    uint64_t expected_base = large_code ? 0x8400000 : 0x8200000;
    uint32_t branch = 0x14000000 | (uint32_t)((expected_base + 16 - 0x8000000) / 4);
    memcpy(image, &branch, 4);
    const uint32_t mod_offset = 32, mod_magic = 0x30444F4D;
    memcpy(image + 4, &mod_offset, 4); memcpy(image + mod_offset, &mod_magic, 4);
    SatchelNSOInfo nso = {.image_size = (uint32_t)image_size, .bss_size = (uint32_t)image_size - 0x2004,
        .segments = {{0, 36, 0, 36}, {0, 4, 0x1000, 4}, {0, 4, 0x2000, 4}}};
    SatchelSwitchProcessModule modules[] = {{"rtld", nso, image, image_size}, {"main", nso, image, 0x3000}};
    modules[1].info.image_size = 0x3000; modules[1].info.bss_size = 0xFFC;
    SatchelProcessMetadata metadata = {.program_id = 1, .address_space_size = UINT64_C(1) << bits,
        .main_thread_stack_size = 0x4000, .address_space_bits = (uint8_t)bits,
        .address_space_type = bits == 39 ? 3 : 1, .flags0 = bits == 39 ? 7 : 3, .is_64_bit = true};
    char error[256];
    SatchelSwitchProcess *p = satchel_switch_process_create_with_dispatch(modules, 2, &metadata, NULL, error, sizeof(error));
    CHECK(p && !satchel_switch_process_dispatch_address(p) && !satchel_switch_process_dispatch_size(p));
    satchel_switch_process_destroy(p);
    CHECK(!satchel_switch_process_dispatch_address(NULL) && !satchel_switch_process_dispatch_size(NULL));
    uint8_t *source = malloc(SATCHEL_SWITCH_PROCESS_DISPATCH_LIMIT + 1); CHECK(source);
    // Deliberately unaligned host source: only guest instruction alignment matters.
    uint8_t *stub = source + 1;
    const uint32_t instructions[] = {0xD4200000, 0, 0, 0, 0xD2800030, 0x17FFFFFB};
    const size_t sizes[] = {24, 4096, 4100, SATCHEL_SWITCH_PROCESS_DISPATCH_LIMIT};
    for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        memset(stub, 0xA7, SATCHEL_SWITCH_PROCESS_DISPATCH_LIMIT);
        memcpy(stub, instructions, sizeof(instructions));
        SatchelSwitchDispatchCode dispatch = {stub, sizes[i]};
        p = satchel_switch_process_create_with_dispatch(modules, 2, &metadata, &dispatch, error, sizeof(error)); CHECK(p);
        SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
        uint64_t rounded = (sizes[i] + 4095) & ~(uint64_t)4095;
        CHECK(satchel_switch_process_dispatch_address(p) == expected_base);
        CHECK(satchel_switch_process_dispatch_size(p) == rounded);
        CHECK(satchel_switch_process_module_address(p, 0) == 0x8000000);
        CHECK(satchel_switch_process_module_address(p, 1) == 0x8000000 + image_size);
        SatchelSwitchMemoryInfo info;
        CHECK(satchel_switch_process_query(p, expected_base, &info) && info.address == expected_base &&
            info.size == rounded && info.state == 0x14 && info.permissions == 5);
        check_nso_discovery(p, metadata.address_space_size);
        CHECK(satchel_switch_process_query(p, expected_base - 1, &info) && info.state == 0 && info.permissions == 0);
        CHECK(satchel_switch_process_query(p, expected_base + rounded, &info) && info.address == expected_base + rounded &&
            info.state == 0 && info.permissions == 0);
        CHECK(satchel_switch_process_query(p, 0x31000110, &info) && info.state == 0xC && info.permissions == 3);
        uint64_t stack = bits == 39 ? expected_base + UINT64_C(0x1000000000) : 0x30000000;
        CHECK(satchel_switch_process_query(p, stack, &info) && info.state == 0xB && info.permissions == 3 && info.size == 0x4000);
        uint8_t byte = 0xFF;
        if (sizes[i] < rounded) {
            CHECK(satchel_cpu_probe_read(cpu, expected_base + sizes[i], &byte, 1) && byte == 0);
            CHECK(satchel_cpu_probe_read(cpu, expected_base + rounded - 1, &byte, 1) && byte == 0);
        }
        CHECK(!satchel_cpu_probe_read(cpu, expected_base + rounded, &byte, 1));
        CHECK(!satchel_cpu_probe_write(cpu, expected_base, &byte, 1));
        CHECK(satchel_cpu_probe_resize_rw(cpu, expected_base, rounded) == SATCHEL_CPU_MAP_INVALID);
        memset(stub, 0, SATCHEL_SWITCH_PROCESS_DISPATCH_LIMIT);
        uint32_t copied[6];
        CHECK(satchel_cpu_probe_read(cpu, expected_base, copied, sizeof(copied)) && !memcmp(copied, instructions, sizeof(copied)));
        CHECK(satchel_cpu_probe_set_breakpoints(cpu, &expected_base, 1));
        SatchelCPUProbeResult result = satchel_switch_process_run(p, 10, NULL, NULL);
        CHECK(result.reason == SATCHEL_CPU_BREAKPOINT && result.pc == expected_base && result.steps == 3);
        uint64_t id = 0;
        CHECK(satchel_cpu_probe_get_x(cpu, 16, &id) && id == 1);
        CHECK(satchel_switch_process_queries(p) == 0 && satchel_switch_process_info_requests(p) == 0);
        satchel_switch_process_destroy(p);
    }
    const size_t invalid_sizes[] = {0, 1, 3, 5, SATCHEL_SWITCH_PROCESS_DISPATCH_LIMIT + 4, SIZE_MAX};
    for (unsigned i = 0; i < sizeof(invalid_sizes) / sizeof(invalid_sizes[0]); i++) {
        SatchelSwitchDispatchCode dispatch = {stub, invalid_sizes[i]};
        CHECK(!satchel_switch_process_create_with_dispatch(modules, 2, &metadata, &dispatch, error, sizeof(error)));
    }
    SatchelSwitchDispatchCode dispatch = {NULL, 4};
    CHECK(!satchel_switch_process_create_with_dispatch(modules, 2, &metadata, &dispatch, error, sizeof(error)));
    dispatch = (SatchelSwitchDispatchCode){stub, 4};
    p = satchel_switch_process_create_with_dispatch(modules, 2, &metadata, &dispatch, error, sizeof(error)); CHECK(p);
    free(source); // No host source lifetime dependence after construction.
    uint32_t value = 1;
    CHECK(satchel_cpu_probe_read(satchel_switch_process_cpu(p), expected_base, &value, sizeof(value)) && value == 0);
    satchel_switch_process_destroy(p);
    free(image);
}

static void test_memory_permission(unsigned bits) {
    uint8_t image[0x61000]={0};
    const uint32_t code[]={0xd4000581,0xd40005a1,0xd4000521,0xd40000c1,0xd4000021,
        0xf9400043,0xf9000043,0xd4000081,0xd40000a1,0xd4000fe1,0xd4000041};
    memcpy(image,code,sizeof(code));
    SatchelNSOInfo nso={.image_size=sizeof(image),.bss_size=sizeof(image)-0x2010,
        .segments={{0,sizeof(code),0,sizeof(code)},{0,4,0x1000,4},{0,16,0x2000,16}}};
    SatchelSwitchProcessModule modules[]={{"rtld",nso,image,sizeof(image)},{"main",nso,image,sizeof(image)}};
    SatchelProcessMetadata metadata={.program_id=1,.address_space_size=UINT64_C(1)<<bits,.main_thread_stack_size=0x4000,
        .address_space_bits=(uint8_t)bits,.address_space_type=bits==39?3:1,.flags0=bits==39?7:3,.is_64_bit=true,
        .system_resource_size=16*1024*1024};
    const uint32_t trap=0xd4200000;SatchelSwitchDispatchCode dispatch={(const uint8_t*)&trap,sizeof(trap)};
    char error[256];SatchelSwitchProcess *p=satchel_switch_process_create_with_dispatch(modules,2,&metadata,&dispatch,error,sizeof(error));CHECK(p);
    SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);const uint64_t data=0x8002000;
    uint64_t used=physical_info(p,22),budget=physical_info(p,21),physical=physical_info(p,2),heap=physical_info(p,4);
    const uint64_t stack=bits==39?UINT64_C(0x1008200000):UINT64_C(0x30000000);
    CHECK(!satchel_switch_process_permission_requests(NULL)&&!satchel_switch_process_permission_serviced(NULL)&&!satchel_switch_process_permission_changed_bytes(NULL));
    // Exact public validation precedence and the32-bit permission argument.
    permission_call(p,data+1,0,7,0xcc01);permission_call(p,data,0,7,0xca01);
    permission_call(p,data,4097,7,0xca01);permission_call(p,UINT64_MAX-4095,4096,7,0xd401);
    permission_call(p,metadata.address_space_size,4096,7,0xd801);
    permission_call(p,metadata.address_space_size,4096,3,0xd401);
    permission_call(p,0,4096,3,0xd401);
    for(unsigned perm=2;perm<8;perm++)if(perm!=3)permission_call(p,data,4096,perm,0xd801);
    permission_call(p,data,4096,UINT64_C(0xbadcafe000000003),0);
    const uint64_t forbidden[]={0x8000000,0x8001000,0x31000000,stack,
        satchel_switch_process_dispatch_address(p),0x70000000};
    for(size_t i=0;i<sizeof(forbidden)/sizeof(forbidden[0]);i++)permission_call(p,forbidden[i],4096,1,0xd401);
    permission_call(p,data-4096,8192,1,0xd401); // Code/CodeData mixed, even contiguous.
    uint8_t original[3*4096],actual[3*4096];for(size_t i=0;i<sizeof(original);i++)original[i]=(uint8_t)(i*31+17);
    CHECK(satchel_cpu_probe_write(cpu,data,original,sizeof(original)));
    const uint64_t middle=data+4096;
#if defined(SATCHEL_CPU_TESTING)
    for(unsigned failure=1;failure<=2;failure++){
        satchel_cpu_probe_test_mapping_allocation_failure(failure);permission_call(p,middle,4096,1,0xce01);
        SatchelSwitchMemoryInfo info;CHECK(satchel_switch_process_query(p,middle,&info)&&info.permissions==3&&info.state==4);
        CHECK(satchel_cpu_probe_read(cpu,data,actual,sizeof(actual))&&!memcmp(actual,original,sizeof(actual)));
        CHECK(!satchel_switch_process_permission_changed_bytes(p));
    }
#endif
    // Original synthetic equivalent of the observed76-page prefix of a95-page
    // CodeData mapping; no game addresses, data or instructions are required.
    permission_call(p,data,0x4c000,1,0);
    SatchelSwitchMemoryInfo prefix,tail;
    CHECK(satchel_switch_process_query(p,data,&prefix)&&prefix.address==data&&prefix.size==0x4c000&&prefix.state==4&&prefix.permissions==1&&!prefix.attributes);
    CHECK(satchel_switch_process_query(p,data+0x4c000,&tail)&&tail.address==data+0x4c000&&tail.size==0x13000&&tail.permissions==3);
    CHECK(physical_info(p,22)==used);permission_call(p,data,0x4c000,3,0);
    CHECK(satchel_switch_process_permission_changed_bytes(p)==2*UINT64_C(0x4c000));
    permission_call(p,middle,4096,1,0);
    SatchelSwitchMemoryInfo info;CHECK(satchel_switch_process_query(p,middle,&info)&&info.address==middle&&info.size==4096&&info.state==4&&info.permissions==1&&!info.attributes);
    CHECK(satchel_cpu_probe_read(cpu,middle,actual,4096)&&!memcmp(actual,original+4096,4096));
    CHECK(!satchel_cpu_probe_write(cpu,middle,original,1));
    CHECK(satchel_cpu_probe_check_range(cpu,middle,4096,SATCHEL_CPU_READ)&&!satchel_cpu_probe_check_range(cpu,middle,4096,SATCHEL_CPU_WRITE));
    // A real shared CPU context observes the changed page permission while
    // fetching its original RX instruction stream; failed STR cannot write it.
    SatchelCPUProbe *reader=satchel_cpu_probe_create_shared(cpu,0x8000014,error,sizeof(error));CHECK(reader);
    CHECK(satchel_cpu_probe_set_x(reader,2,middle));CHECK(satchel_cpu_probe_run(reader,1,NULL,NULL).reason==SATCHEL_CPU_BUDGET);
    CHECK(satchel_cpu_probe_set_x(reader,3,0));SatchelCPUProbeResult denied=satchel_cpu_probe_run(reader,1,NULL,NULL);
    CHECK(denied.reason==SATCHEL_CPU_MEMORY_FAULT&&denied.steps==0);
    CHECK(satchel_cpu_probe_read(cpu,middle,actual,8)&&!memcmp(actual,original+4096,8));satchel_cpu_probe_destroy(reader);
    permission_call(p,data,8192,1,0xd401); // Different existing permissions cannot be normalized by this SVC.
    permission_call(p,middle,4096,0,0);
    CHECK(satchel_switch_process_query(p,middle,&info)&&info.address==middle&&info.size==4096&&info.state==4&&!info.permissions&&!info.attributes);
    memset(actual,0xa5,sizeof(actual));CHECK(!satchel_cpu_probe_read(cpu,middle,actual,4096));
    for(size_t i=0;i<sizeof(actual);i++)CHECK(actual[i]==0xa5);
    CHECK(!satchel_cpu_probe_write(cpu,middle,original,1));
    reader=satchel_cpu_probe_create_shared(cpu,0x8000014,error,sizeof(error));CHECK(reader);
    CHECK(satchel_cpu_probe_set_x(reader,2,middle)&&satchel_cpu_probe_set_x(reader,3,0xabcdef));
    denied=satchel_cpu_probe_run(reader,1,NULL,NULL);uint64_t retained;
    CHECK(denied.reason==SATCHEL_CPU_MEMORY_FAULT&&denied.steps==0);
    CHECK(satchel_cpu_probe_get_x(reader,3,&retained)&&retained==0xabcdef);satchel_cpu_probe_destroy(reader);
    uint64_t changed=satchel_switch_process_permission_changed_bytes(p);permission_call(p,middle,4096,0,0);
    CHECK(satchel_switch_process_permission_changed_bytes(p)==changed);
    permission_call(p,middle,4096,3,0);
    CHECK(satchel_cpu_probe_read(cpu,data,actual,sizeof(actual))&&!memcmp(actual,original,sizeof(actual)));
    CHECK(satchel_switch_process_query(p,middle,&info)&&info.address==data&&info.size==sizeof(image)-0x2000&&info.permissions==3);
    CHECK(satchel_switch_process_permission_changed_bytes(p)==2*UINT64_C(0x4c000)+3*4096);
    CHECK(physical_info(p,22)==used&&physical_info(p,21)==budget);
    // Locked source and live Stack alias never become permission-mutable.
    uint64_t alias=stack+0x20000;alias_call(p,0,alias,data,4096,0);
    permission_call(p,data,4096,0,0xd401);permission_call(p,data,4096,3,0xd401);permission_call(p,alias,4096,1,0xd401);
    alias_call(p,1,alias,data,4096,0);
    // Physical Normal backing remains owned/charged while inaccessible.
    physical_call(p,0,physical,3*4096,0);uint64_t allocated=physical_info(p,22);
    CHECK(satchel_cpu_probe_write(cpu,physical,original,sizeof(original)));
    permission_call(p,physical+4096,4096,0,0);physical_call(p,0,physical,3*4096,0);
    CHECK(physical_info(p,22)==allocated&&satchel_switch_process_physical_memory_size(p)==3*4096);
    CHECK(satchel_cpu_probe_set_pc(cpu,0x8000004)&&satchel_cpu_probe_set_x(cpu,0,physical)&&satchel_cpu_probe_set_x(cpu,1,3*4096));
    SatchelCPUProbeResult stop=satchel_switch_process_run(p,1,NULL,NULL);CHECK(stop.reason==SATCHEL_CPU_SVC&&stop.svc==0x2d);
    uint64_t value;CHECK(satchel_cpu_probe_get_x(cpu,0,&value)&&value==physical);
    permission_call(p,physical+4096,4096,3,0);CHECK(satchel_cpu_probe_read(cpu,physical,actual,sizeof(actual))&&!memcmp(actual,original,sizeof(actual)));
    physical_call(p,1,physical,3*4096,0);CHECK(physical_info(p,22)==used);
    // A protected heap cannot be silently replaced; restoringRW re-enables resize.
    physical_heap(p,2*1024*1024,heap);permission_call(p,heap+4096,4096,1,0);
    alias_heap_request(p,4*1024*1024,1,0);permission_call(p,heap+4096,4096,3,0);
    physical_heap(p,4*1024*1024,heap);physical_heap(p,0,heap);CHECK(physical_info(p,22)==used);
    // Splitting reaches the bounded CPU view limit with atomic refusal, then
    // restoring prior fragments removes that pressure without losing data.
    unsigned protected_pages=0;int exhausted=0;
    for(unsigned i=1;i<32;i+=2){
        uint64_t address=data+(uint64_t)i*4096;
        CHECK(satchel_cpu_probe_set_pc(cpu,0x8000028)&&satchel_cpu_probe_set_x(cpu,0,address)&&satchel_cpu_probe_set_x(cpu,1,4096)&&satchel_cpu_probe_set_x(cpu,2,1));
        SatchelCPUProbeResult result=satchel_switch_process_run(p,1,NULL,NULL);CHECK(result.reason==SATCHEL_CPU_BUDGET&&result.steps==1);
        CHECK(satchel_cpu_probe_get_x(cpu,0,&value));
        CHECK(satchel_switch_process_query(p,address,&info));
        if(value){CHECK(value==0xce01&&info.permissions==3);exhausted=1;break;}
        CHECK(info.permissions==1);protected_pages++;
    }
    CHECK(exhausted&&protected_pages);
    for(unsigned i=0;i<protected_pages;i++)permission_call(p,data+(2*(uint64_t)i+1)*4096,4096,3,0);
    CHECK(satchel_cpu_probe_read(cpu,data,actual,sizeof(actual))&&!memcmp(actual,original,sizeof(actual)));
    CHECK(physical_info(p,22)==used&&physical_info(p,21)==budget);
    CHECK(satchel_switch_process_permission_requests(p)>satchel_switch_process_permission_serviced(p));
    satchel_switch_process_destroy(p);
}
static SatchelSwitchProcess *attribute_fixture(unsigned bits) {
    uint8_t image[0x61000]={0};
    const uint32_t code[]={0xd4000581,0xd40005a1,0xd4000521,0xd40000c1,0xd4000021,
        0xf9400043,0xf9000043,0xd4000081,0xd40000a1,0xd4000fe1,0xd4000041,0xd4000061};
    memcpy(image,code,sizeof(code));
    SatchelNSOInfo nso={.image_size=sizeof(image),.bss_size=sizeof(image)-0x2010,
        .segments={{0,sizeof(code),0,sizeof(code)},{0,4,0x1000,4},{0,16,0x2000,16}}};
    SatchelSwitchProcessModule modules[]={{"rtld",nso,image,sizeof(image)},{"main",nso,image,sizeof(image)}};
    SatchelProcessMetadata metadata={.program_id=1,.address_space_size=UINT64_C(1)<<bits,.main_thread_stack_size=0x4000,
        .address_space_bits=(uint8_t)bits,.address_space_type=bits==39?3:1,.flags0=bits==39?7:3,.is_64_bit=true,
        .system_resource_size=16*1024*1024};
    const uint32_t trap=0xd4200000;SatchelSwitchDispatchCode dispatch={(const uint8_t*)&trap,sizeof(trap)};
    char error[256];SatchelSwitchProcess *p=satchel_switch_process_create_with_dispatch(modules,2,&metadata,&dispatch,error,sizeof(error));CHECK(p);return p;
}
static void test_memory_attribute(unsigned bits) {
    SatchelSwitchProcess *p=attribute_fixture(bits);SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);
    const uint64_t data=0x8002000,stack=bits==39?UINT64_C(0x1008200000):UINT64_C(0x30000000);
    uint64_t used=physical_info(p,22),budget=physical_info(p,21),physical=physical_info(p,2),heap=physical_info(p,4);
    CHECK(!satchel_switch_process_attribute_requests(NULL)&&!satchel_switch_process_attribute_serviced(NULL)&&!satchel_switch_process_permission_locked_bytes(NULL));
    attribute_call(p,data+1,0,1,2,0,0xcc01);attribute_call(p,data,0,1,2,0,0xca01);
    attribute_call(p,data,4097,1,2,0,0xca01);attribute_call(p,UINT64_MAX-4095,4096,1,2,0,0xd401);
    attribute_call(p,UINT64_C(1)<<bits,4096,1,2,0,0xe801);attribute_call(p,UINT64_C(1)<<bits,4096,16,16,0,0xd401);
    const struct{uint32_t mask,attr;} invalid[]={{0,16},{8,16},{16,0},{24,8},{1,1},{2,2},{4,4},{32,32},{0x80000000,0x80000000}};
    for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++)attribute_call(p,data,4096,invalid[i].mask,invalid[i].attr,0,0xe801);
    const uint64_t forbidden[]={0x8000000,0x8001000,0x31000000,stack,satchel_switch_process_dispatch_address(p),0x70000000};
    for(size_t i=0;i<sizeof(forbidden)/sizeof(forbidden[0]);i++)attribute_call(p,forbidden[i],4096,16,16,0,0xd401);
    attribute_call(p,data-4096,8192,16,16,0,0xd401);
    attribute_call(p,0x70000000,4096,0,0,0,0); // Uniform mapped-state checks also permit an unchanged Free range.
    alias_call(p,0,stack+0x20000,data,4096,0);attribute_call(p,data,4096,16,16,0,0xd401);
    attribute_call(p,data,4096,0,0,0,0xd401);attribute_call(p,stack+0x20000,4096,16,16,0,0xd401);
    alias_call(p,1,stack+0x20000,data,4096,0);
    physical_call(p,0,physical,4096,0);attribute_call(p,physical,4096,16,16,0,0xd401);
    attribute_call(p,physical,4096,8,8,1,0);physical_call(p,1,physical,4096,0);
    physical_heap(p,2*1024*1024,heap);attribute_call(p,heap,4096,16,16,0,0xd401);physical_heap(p,0,heap);
    // Genuine Uncached changes AND clears need cache semantics, so neither can
    // return success merely because the requested bit already has that value.
    attribute_call(p,data,4096,8,8,1,0);attribute_call(p,data,4096,8,0,1,0);
    attribute_call(p,data,4096,24,16,1,0);attribute_call(p,data,4096,24,24,1,0);
    SatchelSwitchMemoryInfo info;CHECK(satchel_switch_process_query(p,data,&info)&&!info.attributes&&info.permissions==3);
    uint8_t original[4096],actual[4096];for(size_t i=0;i<sizeof(original);i++)original[i]=(uint8_t)(17+13*i);
    CHECK(satchel_cpu_probe_write(cpu,data,original,sizeof(original)));permission_call(p,data,0x4c000,1,0);
    // Exact observed76-of95-page prefix: lock the already-R view without any
    // backing or access-right change. HighW2/W3 bits are ignored by the ABI.
    attribute_call(p,data,0x4c000,UINT64_C(0xabcdef0000000010),UINT64_C(0x1234560000000010),0,0);
    CHECK(satchel_switch_process_query(p,data,&info)&&info.address==data&&info.size==0x4c000&&info.state==4&&info.permissions==1&&info.attributes==16);
    CHECK(satchel_switch_process_query(p,data+0x4c000,&info)&&info.address==data+0x4c000&&info.size==0x13000&&info.permissions==3&&!info.attributes);
    CHECK(satchel_switch_process_permission_locked_bytes(p)==0x4c000&&physical_info(p,22)==used&&physical_info(p,21)==budget);
    CHECK(satchel_cpu_probe_read(cpu,data,actual,sizeof(actual))&&!memcmp(actual,original,sizeof(actual)));
    CHECK(!satchel_cpu_probe_write(cpu,data,original,1));
    for(unsigned perm=0;perm<=3;perm++)if(perm!=2)permission_call(p,data,4096,perm,0xd401);
    attribute_call(p,data,4096,16,0,0,0xe801);attribute_call(p,data,0x4c000,16,16,0,0);
    attribute_call(p,data,4096,0,0,0,0);CHECK(satchel_switch_process_permission_locked_bytes(p)==0x4c000);
    // Mixed old permissions must fail before changing any later unlocked part.
    attribute_call(p,data,0x5f000,16,16,0,0xd401);
    CHECK(satchel_switch_process_query(p,data+0x4c000,&info)&&!info.attributes);
    permission_call(p,data+0x4c000,0x13000,1,0);
    attribute_call(p,data,0x5f000,16,16,0,0); // Mixed old lock bits are allowed and merged.
    CHECK(satchel_switch_process_query(p,data,&info)&&info.size==0x5f000&&info.attributes==16&&info.permissions==1);
    CHECK(satchel_switch_process_permission_locked_bytes(p)==0x5f000);
    // Locking RW or NONE is metadata only, not an implicit protect/unprotect.
    uint64_t other=0x8063000;CHECK(satchel_cpu_probe_write(cpu,other,original,4096));
    attribute_call(p,other,4096,16,16,0,0);CHECK(satchel_cpu_probe_write(cpu,other,original,4096));
    permission_call(p,other,4096,3,0xd401);
    permission_call(p,other+4096,4096,0,0);attribute_call(p,other+4096,4096,16,16,0,0);
    CHECK(satchel_switch_process_query(p,other+4096,&info)&&info.permissions==0&&info.attributes==16&&info.state==4);
    CHECK(!satchel_cpu_probe_read(cpu,other+4096,actual,1));permission_call(p,other+4096,4096,3,0xd401);
    CHECK(satchel_switch_process_permission_locked_bytes(p)==0x61000&&physical_info(p,22)==used);
    satchel_switch_process_destroy(p);
    // Metadata-only splits can exhaust the owned48-entry table independently
    // of CPUviews. The first failure is atomic; locking the uniform whole range
    // coalesces existing lock fragments and recovers table capacity.
    p=attribute_fixture(bits);cpu=satchel_switch_process_cpu(p);unsigned accepted=0;int exhausted=0;
    for(unsigned i=1;i<95;i+=2){
        uint64_t a=data+(uint64_t)i*4096,value,before=satchel_switch_process_permission_locked_bytes(p);
        CHECK(satchel_cpu_probe_set_pc(cpu,0x800002c)&&satchel_cpu_probe_set_x(cpu,0,a)&&satchel_cpu_probe_set_x(cpu,1,4096)&&satchel_cpu_probe_set_x(cpu,2,16)&&satchel_cpu_probe_set_x(cpu,3,16));
        SatchelCPUProbeResult result=satchel_switch_process_run(p,1,NULL,NULL);CHECK(result.reason==SATCHEL_CPU_BUDGET&&result.steps==1);
        CHECK(satchel_cpu_probe_get_x(cpu,0,&value)&&satchel_switch_process_query(p,a,&info));
        if(value){CHECK(value==0xce01&&!info.attributes&&satchel_switch_process_permission_locked_bytes(p)==before);exhausted=1;break;}
        CHECK(info.attributes==16&&satchel_switch_process_permission_locked_bytes(p)==before+4096);accepted++;
    }
    CHECK(exhausted&&accepted);attribute_call(p,data,0x5f000,16,16,0,0);
    CHECK(satchel_switch_process_query(p,data,&info)&&info.size==0x5f000&&info.attributes==16);
    CHECK(satchel_switch_process_permission_locked_bytes(p)==0x5f000);
    attribute_call(p,other,4096,16,16,0,0);CHECK(satchel_switch_process_permission_locked_bytes(p)==0x60000);
    CHECK(satchel_switch_process_attribute_requests(p)>satchel_switch_process_attribute_serviced(p));
    satchel_switch_process_destroy(p);
}

// Original opaque identity page fixtures; no VI or imported-game objects.
static SatchelSwitchProcess *identity_fixture(unsigned bits,int identity,size_t dispatch_size,uint32_t resource,uint32_t stack) {
    uint8_t image[0x8000]={0},dispatch[8192]={0};
    const uint32_t early[]={0xd40003e1,0xd40002c1,0xd4000181,0xd40001c1,0xd40004a1,0xd4000421,
        0xd4000261,0xd4000281,0xd4000521,0xd4000021,0xd4000041,0xd4000061};
    memcpy(image,early,sizeof(early));memcpy(image+0x1000,"sm:",4);
    for(unsigned svc=0;svc<64;svc++)metadata_word(image+0x100+svc*4,0xd4000001u|(svc<<5));
    metadata_word(image+0x400,0xc85f7c01);metadata_word(image+0x404,0xc8027c03); // LDXR/STXR original RAM.
    metadata_word(image+0x408,0xd4000841); // Deliberate unsupported SVC42 stops child.
    SatchelNSOInfo nso={.image_size=sizeof(image),.bss_size=0x5ff0,
        .segments={{0,0x40c,0,0x40c},{0,4096,0x1000,4096},{0,16,0x2000,16}}};
    SatchelSwitchProcessModule modules[]={{"rtld",nso,image,sizeof(image)},{"main",nso,image,sizeof(image)}};
    SatchelProcessMetadata metadata={.program_id=1,.address_space_size=UINT64_C(1)<<bits,
        .main_thread_stack_size=stack,.address_space_bits=(uint8_t)bits,.address_space_type=bits==39?3:1,
        .flags0=bits==39?7:3,.is_64_bit=true,.system_resource_size=resource,.main_thread_priority=44,
        .has_core_priority=true,.allowed_core_mask=7,.allowed_priority_mask=UINT64_C(0xfffffffffffffff0)};
    SatchelSwitchDispatchCode code={dispatch,dispatch_size};char error[256];
    CHECK(dispatch_size<=sizeof(dispatch));
    return identity?satchel_switch_process_create_with_dispatch_and_identity(modules,2,&metadata,dispatch_size?&code:NULL,error,sizeof(error)):
        satchel_switch_process_create_with_dispatch(modules,2,&metadata,dispatch_size?&code:NULL,error,sizeof(error));
}
static uint64_t identity_svc(SatchelSwitchProcess *p,unsigned svc,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t f,uint32_t expected) {
    SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);uint64_t args[]={a,b,c,d,e,f},status,value;
    CHECK(satchel_cpu_probe_set_pc(cpu,0x8000100+svc*4));
    for(unsigned i=0;i<6;i++)CHECK(satchel_cpu_probe_set_x(cpu,i,args[i]));
    SatchelCPUProbeResult stop=satchel_switch_process_run(p,1,NULL,NULL);
    CHECK(stop.reason==SATCHEL_CPU_BUDGET&&stop.steps==1);
    CHECK(satchel_cpu_probe_get_x(cpu,0,&status));
    if(status!=expected)fprintf(stderr,"identity fixture SVC%x status%x expected%x\n",svc,(unsigned)status,expected);
    CHECK(status==expected);CHECK(satchel_cpu_probe_get_x(cpu,1,&value));return value;
}
static void identity_intact(SatchelSwitchProcess *p,SatchelCPUProbe *cpu,const SatchelSwitchIdentityReservation *d) {
    SatchelSwitchIdentityReservation got;SatchelSwitchMemoryInfo m;
    CHECK(satchel_switch_process_identity_reservation(p,&got)&&!memcmp(&got,d,sizeof(got)));
    CHECK(satchel_switch_process_validate_identity(p,cpu,d));
    CHECK(satchel_switch_process_query(p,d->address,&m)&&m.address==d->address&&m.size==4096&&m.state==0x10&&
        !m.permissions&&!m.attributes&&!m.ipc_refcount&&!m.device_refcount);
    uint8_t byte=0xa7;CHECK(!satchel_cpu_probe_read(cpu,d->address,&byte,1)&&byte==0xa7);
    CHECK(!satchel_cpu_probe_write(cpu,d->address,&byte,1));
    for(unsigned perm=1;perm<=7;perm++)CHECK(!satchel_cpu_probe_check_range(cpu,d->address,4096,perm));
}
typedef struct {SatchelSwitchProcess *process;SatchelCPUProbe *cpu;SatchelSwitchIdentityReservation descriptor;unsigned calls;} IdentityCallback;
static int identity_during_run(void *context) {
    IdentityCallback *x=context;x->calls++;
    CHECK(!satchel_switch_process_validate_identity(x->process,x->cpu,&x->descriptor));
    CHECK(!satchel_switch_process_claim_identity(x->process,x->cpu,&x->descriptor));return 1;
}
static void test_identity_reservation(unsigned bits,size_t dispatch_size) {
    SatchelSwitchProcess *plain=identity_fixture(bits,0,dispatch_size,0,0x4000);CHECK(plain);
    SatchelSwitchIdentityReservation canary,before;memset(&canary,0xa9,sizeof(canary));before=canary;
    CHECK(!satchel_switch_process_identity_reservation(plain,&canary)&&!memcmp(&canary,&before,sizeof(canary)));
    uint64_t baseline=identity_svc(plain,0x29,0,22,0xffff8001,0,0,0,0);
    satchel_switch_process_destroy(plain);
    SatchelSwitchProcess *p=identity_fixture(bits,1,dispatch_size,2*1024*1024,0x4000);CHECK(p);
    SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);SatchelSwitchIdentityReservation d;
    CHECK(satchel_switch_process_identity_reservation(p,&d)&&d.version==1&&d.size==40&&d.process_id&&d.generation&&d.length==4096);
    uint64_t expected=UINT64_C(0x8200000)+((dispatch_size+4095)&~(uint64_t)4095);
    CHECK(d.address==expected);CHECK(satchel_switch_process_module_address(p,0)==0x8000000&&satchel_switch_process_module_address(p,1)==0x8008000);
    identity_intact(p,cpu,&d);
    CHECK(!satchel_switch_process_identity_reservation(NULL,&canary)&&!memcmp(&canary,&before,sizeof(canary)));
    CHECK(!satchel_switch_process_identity_reservation(p,NULL));
    _Alignas(8) uint8_t unaligned[sizeof(d)+8];memset(unaligned,0xab,sizeof(unaligned));
    CHECK(!satchel_switch_process_identity_reservation(p,(void*)(unaligned+4)));
    for(size_t i=0;i<sizeof(unaligned);i++)CHECK(unaligned[i]==0xab);
    CHECK(!satchel_switch_process_validate_identity(p,cpu,(void*)(unaligned+4)));
    CHECK(!satchel_switch_process_validate_identity(NULL,cpu,&d)&&!satchel_switch_process_validate_identity(p,NULL,&d));
    for(unsigned field=0;field<6;field++) {
        SatchelSwitchIdentityReservation bad=d;
        switch(field){case 0:bad.version++;break;case 1:bad.size--;break;case 2:bad.process_id++;break;
            case 3:bad.generation++;break;case 4:bad.address+=4096;break;case 5:bad.length++;break;}
        CHECK(!satchel_switch_process_validate_identity(p,cpu,&bad)&&!satchel_switch_process_claim_identity(p,cpu,&bad));
    }
    SatchelSwitchProcess *other=identity_fixture(bits,1,dispatch_size,0,0x4000);CHECK(other);
    SatchelSwitchIdentityReservation od;CHECK(satchel_switch_process_identity_reservation(other,&od)&&od.generation!=d.generation&&od.process_id!=d.process_id);
    CHECK(!satchel_switch_process_validate_identity(p,satchel_switch_process_cpu(other),&d));
    CHECK(!satchel_switch_process_validate_identity(other,cpu,&od)&&!satchel_switch_process_validate_identity(p,cpu,&od));
    satchel_switch_process_destroy(other);
    char error[128];SatchelCPUProbe *unowned=satchel_cpu_probe_create_shared(cpu,0x8000000,error,sizeof(error));CHECK(unowned);
    CHECK(!satchel_switch_process_validate_identity(p,unowned,&d));uintptr_t expired=(uintptr_t)unowned;satchel_cpu_probe_destroy(unowned);
    CHECK(!satchel_switch_process_validate_identity(p,(SatchelCPUProbe*)expired,&d));
    // Active/reentrant validation cannot accidentally burn the later real claim.
    IdentityCallback callback={p,cpu,d,0};SatchelCPUProbeResult stopped=satchel_cpu_probe_run(cpu,1,identity_during_run,&callback);
    CHECK(stopped.reason==SATCHEL_CPU_CANCELLED&&callback.calls);identity_intact(p,cpu,&d);
    // Real shared-memory handle, rather than invalid-handle short-circuit tests.
    SatchelSwitchTimeSeed seed=time_seed(p);CHECK(satchel_switch_process_attach_time_seed(p,&seed));
    uint8_t request[256],name[8]={'t','i','m','e',':','u'};
    uint32_t sm=port_call(p,0x8001000,0);tipc_sm_request(request,0,1,NULL,0);sm_ipc(p,sm,request,1,0,0);
    tipc_sm_request(request,1,0,name,8);uint32_t root=lm_reply(p,sm,request,1,NULL,0);
    time_message(request,0,20,NULL,0);uint32_t shared=time_reply(p,root,request,0,0,NULL,0,1);
    CHECK(identity_svc(p,0x29,0,22,0xffff8001,0,0,0,0)==baseline+8192); // identity + immutable clock page.
    CHECK(satchel_switch_process_system_resource_total(p)==2*1024*1024&&satchel_switch_process_system_resource_used(p)==4096);
    for(unsigned perm=0;perm<=3;perm++)if(perm!=2)identity_svc(p,2,d.address,4096,perm,0,0,0,0xd401);
    identity_svc(p,2,d.address-4096,8192,1,0,0,0,0xd401);
    identity_svc(p,3,d.address,4096,16,16,0,0,0xd401);
    identity_svc(p,3,d.address,4096,8,8,0,0,0xd401);
    identity_svc(p,3,d.address,4096,0,0,0,0,0); // Genuine harmless mask-zero no-op.
    const uint64_t stack=bits==39?initial_stack39:UINT64_C(0x30000000),free_stack=stack+0x100000;
    identity_svc(p,4,free_stack,d.address,4096,0,0,0,0xd401);
    identity_svc(p,5,free_stack,d.address,4096,0,0,0,0xd401);
    identity_svc(p,4,d.address,0x8002000,4096,0,0,0,bits==39?0xdc01:0xd401);
    identity_svc(p,5,d.address,0x8002000,4096,0,0,0,bits==39?0xdc01:0xd401);
    identity_svc(p,0x2c,d.address,4096,0,0,0,0,0xdc01);
    identity_svc(p,0x2d,d.address,4096,0,0,0,0,0xdc01);
    time_shared_call(p,0,shared,d.address,4096,1,0xd401);time_shared_call(p,1,shared,d.address,4096,0,0xd401);
    uint64_t heap=identity_svc(p,0x29,0,4,0xffff8001,0,0,0,0);
    CHECK(identity_svc(p,1,0,2*1024*1024,0,0,0,0,0)==heap);identity_intact(p,cpu,&d);
    CHECK(identity_svc(p,1,0,0,0,0,0,0,0)==heap);identity_intact(p,cpu,&d);
    CHECK(identity_svc(p,0x29,0,22,0xffff8001,0,0,0,0)==baseline+8192);
    // Initial backing cannot be removed/replaced, including after host exposure.
    uint64_t changed=0x1234;
    CHECK(satchel_cpu_probe_resize_rw(cpu,d.address,0)==SATCHEL_CPU_MAP_INVALID);
    CHECK(satchel_cpu_probe_unmap_rw(cpu,d.address,4096,&changed)==SATCHEL_CPU_MAP_INVALID&&changed==0x1234);
    CHECK(satchel_cpu_probe_map_missing_rw(cpu,d.address,4096,&changed)==SATCHEL_CPU_MAP_INVALID&&changed==0x1234);
    for(unsigned perm=1;perm<=3;perm+=2) {
        CHECK(satchel_cpu_probe_set_permissions(cpu,d.address,4096,0,perm)==SATCHEL_CPU_MAP_OK);
        CHECK(!satchel_switch_process_validate_identity(p,cpu,&d)&&!satchel_switch_process_claim_identity(p,cpu,&d));
        CHECK(satchel_cpu_probe_set_permissions(cpu,d.address,4096,perm,0)==SATCHEL_CPU_MAP_OK);identity_intact(p,cpu,&d);
    }
    CHECK(satchel_cpu_probe_set_permissions(cpu,d.address,4096,0,3)==SATCHEL_CPU_MAP_OK);
    uint64_t marker=UINT64_C(0x987654321abcdef0);CHECK(satchel_cpu_probe_write(cpu,d.address,&marker,8));
    uint64_t alias=UINT64_C(0x70000000);
    CHECK(satchel_cpu_probe_map_alias(cpu,alias,d.address,4096)==SATCHEL_CPU_MAP_OK);
    CHECK(!satchel_cpu_probe_check_range(cpu,d.address,4096,1)); // Failed reads alone would miss the alias!
    CHECK(!satchel_switch_process_validate_identity(p,cpu,&d)&&!satchel_switch_process_claim_identity(p,cpu,&d));
    uint64_t read=0;CHECK(satchel_cpu_probe_read(cpu,alias,&read,8)&&read==marker);
    CHECK(satchel_cpu_probe_unmap_alias(cpu,alias,d.address,4096)==SATCHEL_CPU_MAP_OK);
    CHECK(!satchel_switch_process_validate_identity(p,cpu,&d)); // Unmap restores RW, still exposed.
    CHECK(satchel_cpu_probe_set_permissions(cpu,d.address,4096,3,0)==SATCHEL_CPU_MAP_OK);identity_intact(p,cpu,&d);
    // Pure validation and the one-way claim must preserve a live reservation.
    CHECK(satchel_cpu_probe_set_pc(cpu,0x8000400)&&satchel_cpu_probe_set_x(cpu,0,0x8002000)&&satchel_cpu_probe_set_x(cpu,3,marker));
    CHECK(satchel_cpu_probe_run(cpu,1,NULL,NULL).reason==SATCHEL_CPU_BUDGET);
    CHECK(satchel_switch_process_validate_identity(p,cpu,&d)&&satchel_switch_process_claim_identity(p,cpu,&d));
    CHECK(!satchel_switch_process_claim_identity(p,cpu,&d));
    CHECK(satchel_cpu_probe_run(cpu,1,NULL,NULL).reason==SATCHEL_CPU_BUDGET);
    CHECK(satchel_cpu_probe_get_x(cpu,2,&read)&&!read&&satchel_cpu_probe_read(cpu,0x8002000,&read,8)&&read==marker);
#if defined(SATCHEL_CPU_TESTING)
    // Validation does not consume either allocation seam; it reaches no allocator.
    for(unsigned stage=1;stage<=2;stage++) {
        satchel_cpu_probe_test_mapping_allocation_failure(stage);CHECK(satchel_switch_process_validate_identity(p,cpu,&d));
        CHECK(satchel_cpu_probe_set_permissions(cpu,d.address,4096,0,1)==SATCHEL_CPU_MAP_ALLOCATION);
        identity_intact(p,cpu,&d);
    }
#endif
    // Namespace remains burned after repeated queries/host state restoration.
    CHECK(!satchel_switch_process_claim_identity(p,cpu,&d));identity_intact(p,cpu,&d);
    satchel_switch_process_destroy(p);
}
static void test_identity_child(void) {
    SatchelSwitchProcess *p=identity_fixture(39,1,0,0,0x4000);CHECK(p);SatchelSwitchIdentityReservation d;
    CHECK(satchel_switch_process_identity_reservation(p,&d));SatchelCPUProbe *main_cpu=satchel_switch_process_cpu(p);
    uint32_t child=(uint32_t)identity_svc(p,8,0,0x8000408,0,initial_stack39+0x2000,28,0,0);
    identity_svc(p,9,child,0,0,0,0,0,0);
    SatchelSwitchRunResult stop=satchel_switch_process_run_scheduled(p,1,NULL,NULL);SatchelCPUProbe *child_cpu=satchel_switch_process_cpu(p);
    CHECK(stop.state==SATCHEL_SWITCH_RUN_CPU_STOP&&stop.cpu.reason==SATCHEL_CPU_SVC&&stop.cpu.svc==0x42&&child_cpu!=main_cpu);
    CHECK(satchel_switch_process_validate_identity(p,child_cpu,&d)&&satchel_switch_process_validate_identity(p,main_cpu,&d));
    identity_svc(p,0x16,child,0,0,0,0,0,0);
    CHECK(satchel_cpu_probe_set_pc(child_cpu,0x8000100+0xa*4));uintptr_t expired=(uintptr_t)child_cpu;
    stop=satchel_switch_process_run_scheduled(p,1,NULL,NULL);CHECK(stop.cpu.steps==1&&satchel_switch_process_thread_count(p)==1);
    CHECK(!satchel_switch_process_validate_identity(p,(SatchelCPUProbe*)expired,&d));
    CHECK(satchel_switch_process_validate_identity(p,main_cpu,&d)&&satchel_switch_process_claim_identity(p,main_cpu,&d));
    satchel_switch_process_destroy(p);
}
static void test_identity_failures(void) {
    // Old constructor fits the exact fixed budget; one real extra page cannot.
    SatchelSwitchProcess *p=identity_fixture(39,0,0,254*1024*1024,0x1ef000);CHECK(p);satchel_switch_process_destroy(p);
    CHECK(!identity_fixture(39,1,0,254*1024*1024,0x1ef000));
#if defined(SATCHEL_CPU_TESTING)
    for(unsigned stage=1;stage<=2;stage++)for(uint32_t resource=0;resource<=2*1024*1024;resource+=2*1024*1024) {
        satchel_cpu_probe_test_mapping_allocation_failure(stage);
        CHECK(!identity_fixture(39,1,0,resource,0x4000)); // CPU RW->NONE failed; no Process escapes.
        p=identity_fixture(39,1,0,resource,0x4000);CHECK(p);SatchelSwitchIdentityReservation d;
        CHECK(satchel_switch_process_identity_reservation(p,&d));identity_intact(p,satchel_switch_process_cpu(p),&d);satchel_switch_process_destroy(p);
    }
#endif
    // Maximum module count plus dispatch/identity/stack/TLS uses28 original
    // CPU views; bounded construction must not overflow the old27-entry array.
    uint8_t image[0x3000]={0};metadata_word(image,0xd4000841);
    SatchelNSOInfo nso={.image_size=sizeof(image),.bss_size=0xffc,.segments={{0,4,0,4},{0,4,0x1000,4},{0,4,0x2000,4}}};
    const char *names[]={"rtld","main","subsdk0","subsdk1","subsdk2","subsdk3","subsdk4","sdk"};
    SatchelSwitchProcessModule modules[8];for(unsigned i=0;i<8;i++)modules[i]=(SatchelSwitchProcessModule){names[i],nso,image,sizeof(image)};
    SatchelProcessMetadata metadata={.program_id=1,.address_space_size=UINT64_C(1)<<39,.main_thread_stack_size=0x4000,
        .address_space_bits=39,.address_space_type=3,.flags0=7,.is_64_bit=true};
    uint32_t stub=0xd4200000;SatchelSwitchDispatchCode code={(const uint8_t*)&stub,4};char error[256];
    p=satchel_switch_process_create_with_dispatch_and_identity(modules,8,&metadata,&code,error,sizeof(error));CHECK(p);
    SatchelSwitchIdentityReservation id;CHECK(satchel_switch_process_identity_reservation(p,&id)&&id.address==0x8201000);
    identity_intact(p,satchel_switch_process_cpu(p),&id);
    for(unsigned i=0;i<8;i++)CHECK(satchel_switch_process_module_address(p,i)==0x8000000+i*sizeof(image));
    satchel_switch_process_destroy(p);
    test_identity_child();
}

int main(int argc,char **argv) {
    if(argc==2&&!strcmp(argv[1],"--identity-only")){test_identity_reservation(39,0);test_identity_reservation(36,0);test_identity_reservation(39,4100);test_identity_reservation(36,4100);test_identity_failures();printf("%u identity reservation assertions passed\n",checks);return 0;}

    // Original synthetic program: QueryMemory, then unsupported SVC 2.
    uint8_t image[0x3000] = {0};
    const uint32_t code[] = {0xd40000c1, 0xd4000fe1};
    memcpy(image, code, sizeof(code)); image[0x2000] = 0xA5;
    SatchelNSOInfo nso = {.image_size = sizeof(image), .bss_size = 0xFFC,
        .segments = {{0, 8, 0, 8}, {0, 4, 0x1000, 4}, {0, 4, 0x2000, 4}}};
    SatchelSwitchProcessModule modules[] = {{"rtld", nso, image, sizeof(image)}, {"main", nso, image, sizeof(image)}};
    SatchelProcessMetadata metadata = {.program_id = 1, .address_space_size = UINT64_C(1) << 39,
        .main_thread_stack_size = 0x4000, .address_space_bits = 39, .address_space_type = 3, .flags0 = 7, .is_64_bit = true};
    char error[256];
    SatchelSwitchProcess *p = satchel_switch_process_create(modules, 2, &metadata, error, sizeof(error));
    CHECK(p != NULL);
    CHECK(satchel_switch_process_module_address(p, 0) == 0x8000000);
    CHECK(satchel_switch_process_module_address(p, 1) == 0x8003000);
    CHECK(satchel_switch_process_module_address(p, 2) == 0);
    SatchelSwitchMemoryInfo info;
    CHECK(satchel_switch_process_query(p, 0, &info) && info.address == 0 && info.size == 0x8000000 && info.state == 0);
    CHECK(satchel_switch_process_query(p, 0x8000008, &info) && info.address == 0x8000000 && info.size == 0x1000 && info.state == 3 && info.permissions == 5);
    CHECK(satchel_switch_process_query(p, 0x8001FFF, &info) && info.address == 0x8001000 && info.size == 0x1000 && info.state == 3 && info.permissions == 1);
    CHECK(satchel_switch_process_query(p, 0x8002FFF, &info) && info.address == 0x8002000 && info.size == 0x1000 && info.state == 4 && info.permissions == 3);
    CHECK(satchel_switch_process_query(p, 0x8003000, &info) && info.address == 0x8003000 && info.state == 3 && info.permissions == 5);
    CHECK(satchel_switch_process_query(p, 0x8006000, &info) && info.address == 0x8006000 && info.size == 0x31000000 - 0x8006000 && info.state == 0);
    CHECK(satchel_switch_process_query(p, initial_stack39, &info) && info.state == 0xB && info.size == 0x4000 && info.permissions == 3);
    CHECK(satchel_switch_process_query(p, 0x31000110, &info) && info.state == 0xC && info.size == 0x1000 && info.permissions == 3);
    CHECK(satchel_switch_process_query(p, UINT64_MAX, &info) && info.address == metadata.address_space_size && info.size == 0 - metadata.address_space_size && info.state == 0x10 && info.permissions == 0);
    SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
    uint64_t reg = 0;
    CHECK(satchel_cpu_probe_get_x(cpu, 1, &reg) && reg == 0x80);
    CHECK(satchel_cpu_probe_get_x(cpu, 18, &reg) && (reg & 1));
    uint8_t vector[16], zero_vector[16] = {0};
    for (unsigned i = 0; i < 32; i++) {
        memset(vector, 0xa7, sizeof(vector));
        CHECK(satchel_cpu_probe_get_q(cpu, i, vector) && !memcmp(vector, zero_vector, sizeof(vector)));
    }

    uint8_t padding[32]; memset(padding, 0xFF, sizeof(padding));
    CHECK(satchel_cpu_probe_read(cpu, 0x8000008, padding, sizeof(padding)));
    for (unsigned i = 0; i < sizeof(padding); i++) CHECK(padding[i] == 0);
    uint32_t handle = 0;
    CHECK(satchel_cpu_probe_read(cpu, 0x31000110, &handle, 4) && handle == 0x80);
    CHECK(satchel_cpu_probe_set_x(cpu, 0, initial_stack39));
    CHECK(satchel_cpu_probe_set_x(cpu, 1, UINT64_MAX)); // This is NOT a guest pointer.
    CHECK(satchel_cpu_probe_set_x(cpu, 2, 0x8003000));
    SatchelCPUProbeResult result = satchel_switch_process_run(p, 100, NULL, NULL);
    CHECK(result.reason == SATCHEL_CPU_SVC && result.svc == 0x7f && result.steps == 2);
    CHECK(satchel_switch_process_queries(p) == 1);
    CHECK(satchel_cpu_probe_get_x(cpu, 0, &reg) && reg == 0);
    CHECK(satchel_cpu_probe_get_x(cpu, 1, &reg) && reg == 0);
    CHECK(satchel_cpu_probe_read(cpu, initial_stack39, &info, sizeof(info)) && info.address == 0x8003000 && info.state == 3 && info.permissions == 5);
    satchel_switch_process_destroy(p);

    p = satchel_switch_process_create(modules, 2, &metadata, error, sizeof(error)); CHECK(p);
    cpu = satchel_switch_process_cpu(p);
    uint8_t before[16]; memset(before, 0xAB, sizeof(before));
    CHECK(satchel_cpu_probe_write(cpu, (initial_stack39 + 0x3FF0), before, sizeof(before)));
    CHECK(satchel_cpu_probe_set_x(cpu, 0, (initial_stack39 + 0x3FF0)));
    CHECK(satchel_cpu_probe_set_x(cpu, 2, 0x8003000));
    result = satchel_switch_process_run(p, 1, NULL, NULL);
    CHECK(result.reason == SATCHEL_CPU_BUDGET && result.steps == 1 && result.pc == 0x8000004);
    CHECK(satchel_cpu_probe_get_x(cpu, 0, &reg) && reg == 0xE601);
    CHECK(satchel_cpu_probe_read(cpu, (initial_stack39 + 0x3FF0), padding, sizeof(before)) && !memcmp(padding, before, sizeof(before)));
    result = satchel_switch_process_run(p, 10, cancel, NULL);
    CHECK(result.reason == SATCHEL_CPU_CANCELLED && result.steps == 0 && result.pc == 0x8000004);
    satchel_switch_process_destroy(p);

    // Whole-page holes are allocated Code with no user permission, not free.
    uint8_t gapped[0x5000] = {0}; memcpy(gapped, code, sizeof(code));
    modules[0].info.segments[1].memory_offset = 0x2000;
    modules[0].info.segments[2].memory_offset = 0x4000;
    modules[0].info.image_size = sizeof(gapped); modules[0].image = gapped; modules[0].image_size = sizeof(gapped);
    p = satchel_switch_process_create(modules, 2, &metadata, error, sizeof(error)); CHECK(p);
    CHECK(satchel_switch_process_query(p, 0x8001000, &info) && info.state == 3 && info.permissions == 0 && info.size == 0x1000);
    CHECK(!satchel_cpu_probe_read(satchel_switch_process_cpu(p), 0x8001000, padding, 1));
    CHECK(satchel_switch_process_module_address(p, 1) == 0x8005000);
    satchel_switch_process_destroy(p);

    modules[0] = (SatchelSwitchProcessModule){"rtld", nso, image, sizeof(image)};
    modules[1].name = "rtld";
    CHECK(!satchel_switch_process_create(modules, 2, &metadata, error, sizeof(error)));
    modules[1].name = "main";
    modules[0].info.flags = 64; // Execute-only is not silently widened.
    CHECK(!satchel_switch_process_create(modules, 2, &metadata, error, sizeof(error)));
    modules[0].info.flags = 0;
    modules[0].info.segments[1].memory_offset = 0;
    CHECK(!satchel_switch_process_create(modules, 2, &metadata, error, sizeof(error)));
    modules[0].info = nso;
    metadata.flags1 = 1;
    CHECK(!satchel_switch_process_create(modules, 2, &metadata, error, sizeof(error)));
    metadata.flags1 = 0;
    metadata.flags0 |= 0x40;
    CHECK(!satchel_switch_process_metadata_supported(&metadata));
    metadata.flags0 ^= 0xC0;
    CHECK(!satchel_switch_process_metadata_supported(&metadata));
    metadata.flags0 &= 0x3F;
    metadata.address_space_size--;
    CHECK(!satchel_switch_process_metadata_supported(&metadata));
    metadata.address_space_size++;
    metadata.main_thread_stack_size = 32 * 1024 * 1024;
    CHECK(!satchel_switch_process_create(modules, 2, &metadata, error, sizeof(error)));
    metadata.main_thread_stack_size = 0x4000; metadata.is_64_bit = false;
    CHECK(!satchel_switch_process_create(modules, 2, &metadata, error, sizeof(error)));
    CHECK(!satchel_switch_process_query(NULL, 0, &info));
    metadata.is_64_bit = true;
    const char *names[] = {"rtld", "main", "subsdk0", "subsdk1", "subsdk2", "subsdk3", "subsdk4", "sdk"};
    SatchelSwitchProcessModule many[8];
    for (unsigned i = 0; i < 8; i++) {
        many[i] = (SatchelSwitchProcessModule){names[i], nso, gapped, sizeof(gapped)};
        many[i].info.image_size = sizeof(gapped);
        many[i].info.segments[1].memory_offset = 0x2000;
        many[i].info.segments[2].memory_offset = 0x4000;
    }
    p = satchel_switch_process_create(many, 8, &metadata, error, sizeof(error)); CHECK(p);
    CHECK(satchel_switch_process_module_address(p, 7) == 0x8000000 + 7 * 0x5000);
    CHECK(satchel_switch_process_query(p, 0x8000000 + 7 * 0x5000 + 0x3000, &info) && info.state == 3 && !info.permissions);
    satchel_switch_process_destroy(p);
    const uint32_t original_trap = 0xD4200000;
    SatchelSwitchDispatchCode dispatch = {(const uint8_t *)&original_trap, sizeof(original_trap)};
    p = satchel_switch_process_create_with_dispatch(many, 8, &metadata, &dispatch, error, sizeof(error)); CHECK(p);
    CHECK(satchel_switch_process_dispatch_address(p) == 0x8200000);
    CHECK(satchel_switch_process_query(p, 0x8200000, &info) && info.state == 0x14 && info.permissions == 5);
    CHECK(satchel_switch_process_query(p, 0x8000000 + 7 * 0x5000 + 0x3000, &info) && info.state == 3 && !info.permissions);
    satchel_switch_process_destroy(p);
    CHECK(!satchel_switch_process_create(many, 9, &metadata, error, sizeof(error)));
    // Both module ranges fit individually, but leave no room for stack/TLS.
    // The source buffer is valid; rejection must occur before the CPU copies it.
    size_t large_size = 128 * 1024 * 1024;
    uint8_t *large = calloc(1, large_size); CHECK(large);
    SatchelNSOInfo large_info = nso;
    large_info.image_size = (uint32_t)large_size;
    large_info.bss_size = (uint32_t)large_size - 0x2004;
    SatchelSwitchProcessModule large_modules[] = {{"rtld", large_info, large, large_size}, {"main", large_info, large, large_size}};
    CHECK(!satchel_switch_process_create(large_modules, 2, &metadata, error, sizeof(error)));
    // Existing stack/TLS plus both images exactly fill the budget. One dispatch
    // page must be refused before any image copies, despite individually valid inputs.
    large_modules[1].image_size -= metadata.main_thread_stack_size + 4096;
    large_modules[1].info.image_size = (uint32_t)large_modules[1].image_size;
    large_modules[1].info.bss_size = large_modules[1].info.image_size - 0x2004;
    CHECK(!satchel_switch_process_create_with_dispatch(large_modules, 2, &metadata, &dispatch, error, sizeof(error)));
    // A requested16MiB system-resource reservation is budget, not backed pages;
    // nevertheless it cannot be spent by NSO/stack/TLS allocations.
    metadata.system_resource_size = 16 * 1024 * 1024;
    large_modules[1].image_size -= metadata.system_resource_size - 4096;
    large_modules[1].info.image_size = (uint32_t)large_modules[1].image_size;
    large_modules[1].info.bss_size = large_modules[1].info.image_size - 0x2004;
    CHECK(!satchel_switch_process_create(large_modules, 2, &metadata, error, sizeof(error)));
    free(large);
    test_memory_accounting(0, 0, 0);
    test_memory_accounting(16 * 1024 * 1024, 1, 1);
    test_memory_accounting(254 * 1024 * 1024, 0, 1);
    test_dispatch(39, 0);
    test_dispatch(36, 0);
    test_dispatch(39, 1);
    test_dispatch(36, 1);
    test_get_info();
    test_regions(39, 0);
    test_regions(36, 0);
    test_regions(39, 1);
    test_application_control_attachment();
    test_named_port_handles();
#if defined(SATCHEL_CPU_TESTING)
    test_time_math();
#endif
    test_time_seed_validation();
    test_time_model(39,0);test_time_model(39,1);test_time_model(36,0);test_time_model(36,1);
    test_storage_ipc();
    test_memory_attribute(39);test_memory_attribute(36);
    test_memory_permission(39);test_memory_permission(36);
    test_alias_memory(39);
    test_alias_memory(36);
    test_physical_memory(39);
    test_physical_memory(36);
    test_identity_reservation(39,0);test_identity_reservation(36,0);
    test_identity_reservation(39,4100);test_identity_reservation(36,4100);test_identity_failures();
    test_heap(39);
    test_heap(36);
    test_capability_queries();
    test_thread_queries(37, 2);
    test_thread_queries(63, 3);
    test_thread_queries(0, 0);
    printf("%u process mapping/SVC assertions passed\n", checks);
    return 0;
}
