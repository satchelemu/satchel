#include "SwitchResources.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void satchel_switch_resources_test_fail_allocation(unsigned ordinal);
static unsigned checks;
static void check(int okay, const char *message) {
    ++checks;
    if (!okay) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}
static int zero(const void *memory, size_t size) {
    const uint8_t *bytes = memory;
    for (size_t i = 0; i < size; ++i) if (bytes[i]) return 0;
    return 1;
}
typedef struct {
    uint64_t address, size;
    uint32_t state, permission, attributes, ipc_refs, device_refs, padding;
} OriginalMap;
_Static_assert(sizeof(OriginalMap) == 40, "original map fixture size");

int main(void) {
    const uint64_t page = SATCHEL_SWITCH_RESOURCE_PAGE_SIZE;
    check(satchel_switch_resources_capacity(NULL) == 0 && satchel_switch_resources_used(NULL) == 0,
          "absent resource arena reports no capacity or use");
    check(!satchel_switch_resources_allocate(NULL, 1), "null arena allocation rejected");
    satchel_switch_resources_destroy(NULL);
    const uint64_t invalid[] = {0, 1, 4095, 4097, SATCHEL_SWITCH_RESOURCE_CAPACITY_LIMIT + 1ull,
        SATCHEL_SWITCH_RESOURCE_CAPACITY_LIMIT + 4096ull, UINT64_MAX, UINT64_MAX - 4095};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i)
        check(!satchel_switch_resources_create(invalid[i]), "invalid capacity rejected before allocation");

    SatchelSwitchResources *arena = satchel_switch_resources_create(16 * 1024 * 1024);
    check(arena && satchel_switch_resources_capacity(arena) == 16 * 1024 * 1024 &&
          satchel_switch_resources_used(arena) == 0, "creation owns real capacity but assigns no pages");
    OriginalMap *maps = satchel_switch_resources_allocate(arena, 48 * sizeof(*maps));
    check(maps && (uintptr_t)maps % page == 0 && zero(maps, (size_t)page),
          "actual metadata receives an aligned zeroed page including padding");
    check(satchel_switch_resources_used(arena) == page, "1920-byte metadata uses exactly one page");
    for (size_t i = 0; i < 48; ++i) {
        maps[i].address = 0x100000 + i * page;
        maps[i].size = page;
        maps[i].state = 3;
        maps[i].permission = 5;
    }
    for (size_t i = 0; i < 48; ++i)
        check(maps[i].address == 0x100000 + i * page && maps[i].size == page &&
              maps[i].state == 3 && maps[i].permission == 5 && !maps[i].attributes,
              "page is usable owned host metadata storage");
    check(!satchel_switch_resources_allocate(arena, 0) && !satchel_switch_resources_allocate(arena, SIZE_MAX) &&
          satchel_switch_resources_used(arena) == page, "zero and overflowing requests preserve page usage");
    uint8_t *second = satchel_switch_resources_allocate(arena, 1);
    check(second && (uintptr_t)second % page == 0 && second == (uint8_t *)maps + page &&
          zero(second, (size_t)page), "successive allocations own distinct zeroed pages");
    memset(second, 0x5a, (size_t)page);
    check(maps[47].address == 0x100000 + 47 * page && satchel_switch_resources_used(arena) == 2 * page,
          "new allocation does not move or overwrite existing map records");
    satchel_switch_resources_destroy(arena);

    for (size_t bytes = 1; bytes <= 2 * page + 1; ++bytes) {
        arena = satchel_switch_resources_create(3 * page);
        check(arena != NULL, "rounding test arena");
        uint8_t *first = satchel_switch_resources_allocate(arena, bytes);
        uint64_t rounded = ((uint64_t)bytes + page - 1) / page * page;
        check(first && (uintptr_t)first % page == 0 && satchel_switch_resources_used(arena) == rounded &&
              satchel_switch_resources_capacity(arena) == 3 * page, "every boundary-adjacent byte count rounds to complete pages");
        first[0] = 0x12; first[rounded - 1] = 0x34;
        check(!satchel_switch_resources_allocate(arena, (size_t)(3 * page - rounded + 1)) &&
              satchel_switch_resources_used(arena) == rounded && first[0] == 0x12 && first[rounded - 1] == 0x34,
              "exhaustion preserves allocation edges and used count");
        if (rounded < 3 * page) {
            size_t remaining = (size_t)(3 * page - rounded);
            uint8_t *last = satchel_switch_resources_allocate(arena, remaining);
            check(last == first + rounded && zero(last, remaining) &&
                  satchel_switch_resources_used(arena) == 3 * page, "exact remaining capacity can be assigned without overlap");
            memset(last, 0x9b, remaining);
            check(first[0] == 0x12 && first[rounded - 1] == 0x34, "final allocation preserves earlier canaries");
        }
        check(!satchel_switch_resources_allocate(arena, 1) && satchel_switch_resources_used(arena) == 3 * page,
              "fully allocated arena cannot overcommit a page");
        satchel_switch_resources_destroy(arena);
    }

    // Preserve the existing profile range; large calloc-backed capacity is not
    // a claim that every page is physically resident or already in use.
    const uint64_t capacities[] = {page, 2 * 1024 * 1024, 254ull * 1024 * 1024,
                                  SATCHEL_SWITCH_RESOURCE_CAPACITY_LIMIT};
    for (size_t i = 0; i < sizeof(capacities) / sizeof(*capacities); ++i) {
        arena = satchel_switch_resources_create(capacities[i]);
        check(arena && satchel_switch_resources_capacity(arena) == capacities[i] &&
              satchel_switch_resources_used(arena) == 0, "entire allowed capacity range has real calloc backing");
        uint8_t *all = satchel_switch_resources_allocate(arena, (size_t)capacities[i]);
        check(all && (uintptr_t)all % page == 0 && all[0] == 0 && all[capacities[i] - 1] == 0 &&
              satchel_switch_resources_used(arena) == capacities[i], "maximum allocation reaches exact backed end");
        all[0] = 0x42; all[capacities[i] - 1] = 0x24;
        check(!satchel_switch_resources_allocate(arena, 1) && all[0] == 0x42 && all[capacities[i] - 1] == 0x24,
              "maximum-capacity failure leaves edge canaries unchanged");
        satchel_switch_resources_destroy(arena);
    }

    for (unsigned stage = 1; stage <= 2; ++stage) for (unsigned repeat = 0; repeat < 32; ++repeat) {
        satchel_switch_resources_test_fail_allocation(stage);
        check(!satchel_switch_resources_create(4096), "injected object/backing allocation failure returns no live arena");
        arena = satchel_switch_resources_create(4096);
        check(arena && satchel_switch_resources_used(arena) == 0, "one-shot allocation failure permits clean retry");
        uint8_t *memory = satchel_switch_resources_allocate(arena, 4096);
        check(memory && zero(memory, 4096), "failed creation cannot leak stale bytes into later arena");
        satchel_switch_resources_destroy(arena);
    }
    satchel_switch_resources_test_fail_allocation(1);
    check(!satchel_switch_resources_create(1), "invalid capacity checked before allocator seam");
    check(!satchel_switch_resources_create(4096), "invalid capacity does not consume allocator failure");
    satchel_switch_resources_test_fail_allocation(0);

    SatchelSwitchResources *a = satchel_switch_resources_create(8192), *b = satchel_switch_resources_create(8192);
    check(a && b, "separate resource owners");
    uint8_t *ap = satchel_switch_resources_allocate(a, 4096), *bp = satchel_switch_resources_allocate(b, 4096);
    check(ap && bp && ap != bp, "independent arenas never share an allocation");
    memset(ap, 0x33, 4096);
    check(zero(bp, 4096), "one process resource mutation cannot alter another arena");
    satchel_switch_resources_destroy(a);
    bp[4095] = 0x44;
    check(bp[4095] == 0x44 && satchel_switch_resources_capacity(b) == 8192 &&
          satchel_switch_resources_used(b) == 4096, "other arena remains alive after owner destruction");
    satchel_switch_resources_destroy(b);
    printf("Switch resources: %u original backing/allocation assertions passed.\n", checks);
    return 0;
}
