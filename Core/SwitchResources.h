#ifndef SATCHEL_SWITCH_RESOURCES_H
#define SATCHEL_SWITCH_RESOURCES_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { SATCHEL_SWITCH_RESOURCE_PAGE_SIZE = 4096,
       SATCHEL_SWITCH_RESOURCE_CAPACITY_LIMIT = 256 * 1024 * 1024 };
typedef struct SatchelSwitchResources SatchelSwitchResources;
/* Host-owned page arena for actual emulated process metadata, not guest RAM or
 * fabricated hardware page tables. Capacity must be nonzero, 4096-aligned and
 * at most 256 MiB. Process handles a zero/default resource profile separately.
 * Backing is calloc-owned; at most 4095 extra bytes provide alignment. Capacity
 * excludes that alignment slack and allocator metadata; it is not host RSS.
 * Returns NULL on invalid capacity or allocation failure, with no live arena. */
SatchelSwitchResources *satchel_switch_resources_create(uint64_t capacity);
void satchel_switch_resources_destroy(SatchelSwitchResources *resources);
/* Rounds nonzero bytes up to 4096 and returns zeroed, 4096-aligned host storage.
 * Allocations consume whole pages and remain valid until arena destruction.
 * No individual free or guest pointer conversion. Bounds/overflow/exhaustion
 * failures return NULL without changing usage or existing bytes. The caller
 * owns pointer lifetimes and serializes all calls on the process worker. */
void *satchel_switch_resources_allocate(SatchelSwitchResources *resources, size_t bytes);
/* Byte counts: actual arena capacity and pages assigned to live allocations.
 * Merely creating an arena consumes zero pages; NULL reports zero. */
uint64_t satchel_switch_resources_capacity(const SatchelSwitchResources *resources);
uint64_t satchel_switch_resources_used(const SatchelSwitchResources *resources);
#ifdef __cplusplus
}
#endif
#endif
