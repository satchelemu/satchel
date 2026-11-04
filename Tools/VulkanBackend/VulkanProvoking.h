#ifndef SATCHEL_VULKAN_PROVOKING_H
#define SATCHEL_VULKAN_PROVOKING_H
#include "VulkanFlat.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Renderer modes, NOT unverified NVN enum values. Native LAST is not enabled.
 * The additive LAST path uses immutable cyclic indices on the independently
 * admitted pure VS/flat-FS triangle-list profile. No strips/restart/VertexID,
 * side effects, geometry stages, multiple instances or nonzero firstVertex. */
enum {SATCHEL_VULKAN_PROVOKING_FIRST=0,SATCHEL_VULKAN_PROVOKING_LAST=1,
 SATCHEL_VULKAN_PROVOKING_INDEX_COUNT=4095,SATCHEL_VULKAN_PROVOKING_ALLOCATION_MAX=128*1024,
 SATCHEL_VULKAN_PROVOKING_MEMORY_LIMIT=1024*1024};
typedef struct {uint32_t version,size,graphics_queue,native_extension,native_last_supported,native_last_enabled,
 first_supported,rotated_last_supported,max_vertex_count;uint64_t per_pipeline_allocation_limit,aggregate_allocation_limit;} SatchelVulkanProvokingCaps;
typedef struct {uint32_t version,size,live_index_buffers,pipeline_limit;uint64_t allocated_bytes,allocation_limit;} SatchelVulkanProvokingStats;
/* Actual physical feature/extension is separate from enabled native behavior.
 * Capability does not promise allocation success. Failure preserves output. */
SatchelVulkanStatus satchel_vulkan_provoking_capabilities(uint64_t,SatchelVulkanProvokingCaps *);
SatchelVulkanStatus satchel_vulkan_provoking_stats(SatchelVulkanProvokingStats *);
/* Same copied typed schema/state/ownership contract as flat_create. Old API
 * remains FIRST-only. Here explicit LAST owns a private index allocation counted
 * by provoking_stats (separate from public buffer registry). Existing pipeline
 * references retain it through fence/error/cancellation and device teardown.
 * Draw counts must be3..4095 divisible by3; existing validation refuses others.
 * No compiler-source state proof or NVN-state normalization is implied. */
SatchelVulkanStatus satchel_vulkan_flat_create_provoking(uint64_t,const SatchelVulkanFlatDescription *,const SatchelVulkanFlatModule *,const SatchelVulkanFlatModule *,uint64_t *,SatchelVulkanRasterInfo *,SatchelVulkanFlatPair *);
#ifdef __cplusplus
}
#endif
#endif
