#include "SwitchResources.h"
#include <stdlib.h>

struct SatchelSwitchResources {
    void *allocation;
    uint8_t *pages;
    uint64_t capacity, used;
};

#if defined(SATCHEL_SWITCH_RESOURCES_TESTING)
/* Private deterministic host-test seam; never exported by production builds. */
static unsigned fail_allocation;
void satchel_switch_resources_test_fail_allocation(unsigned ordinal) {
    fail_allocation = ordinal;
}
#endif
static void *resource_calloc(size_t count, size_t size) {
#if defined(SATCHEL_SWITCH_RESOURCES_TESTING)
    if (fail_allocation && --fail_allocation == 0) return NULL;
#endif
    return calloc(count, size);
}
SatchelSwitchResources *satchel_switch_resources_create(uint64_t capacity) {
    const uint64_t alignment = SATCHEL_SWITCH_RESOURCE_PAGE_SIZE;
    if (!capacity || capacity > SATCHEL_SWITCH_RESOURCE_CAPACITY_LIMIT || capacity % alignment)
        return NULL;
    /* The fixed cap makes this addition safe even on a 32-bit host. */
    if (capacity > SIZE_MAX - (alignment - 1)) return NULL;
    SatchelSwitchResources *resources = resource_calloc(1, sizeof(*resources));
    if (!resources) return NULL;
    resources->allocation = resource_calloc(1, (size_t)(capacity + alignment - 1));
    if (!resources->allocation) { free(resources); return NULL; }
    uintptr_t address = (uintptr_t)resources->allocation;
    address = (address + alignment - 1) & ~(uintptr_t)(alignment - 1);
    resources->pages = (uint8_t *)address;
    resources->capacity = capacity;
    return resources;
}
void satchel_switch_resources_destroy(SatchelSwitchResources *resources) {
    if (!resources) return;
    free(resources->allocation);
    free(resources);
}
void *satchel_switch_resources_allocate(SatchelSwitchResources *resources, size_t bytes) {
    if (!resources || !bytes || bytes > resources->capacity - resources->used)
        return NULL;
    const uint64_t page = SATCHEL_SWITCH_RESOURCE_PAGE_SIZE;
    /* bytes is already bounded by the 256 MiB capacity, so rounding cannot wrap. */
    const uint64_t rounded = ((uint64_t)bytes + page - 1) & ~(page - 1);
    if (rounded > resources->capacity - resources->used) return NULL;
    void *result = resources->pages + (size_t)resources->used;
    resources->used += rounded;
    return result;
}
uint64_t satchel_switch_resources_capacity(const SatchelSwitchResources *resources) {
    return resources ? resources->capacity : 0;
}
uint64_t satchel_switch_resources_used(const SatchelSwitchResources *resources) {
    return resources ? resources->used : 0;
}
