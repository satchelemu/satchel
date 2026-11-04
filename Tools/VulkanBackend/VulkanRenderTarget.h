#ifndef SATCHEL_VULKAN_RENDER_TARGET_H
#define SATCHEL_VULKAN_RENDER_TARGET_H
#include "VulkanBackend.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {SATCHEL_VULKAN_TARGET_RGBA8_UNORM=1,SATCHEL_VULKAN_TARGET_LIMIT=8,
 SATCHEL_VULKAN_TARGET_DIMENSION_LIMIT=2048,SATCHEL_VULKAN_TARGET_BYTE_LIMIT=8*1024*1024,
 SATCHEL_VULKAN_TARGET_MEMORY_LIMIT=32*1024*1024};
typedef struct {uint32_t version,size,format,width,height,flags;} SatchelVulkanTargetDescription;
typedef struct {uint32_t version,size,format,supported,queue_graphics,max_width,max_height,samples;
 uint64_t max_logical_bytes,max_resource_bytes;} SatchelVulkanTargetCaps;
typedef struct {uint64_t device,logical_bytes,allocation_size;uint32_t format,width,height,initialized,in_flight;} SatchelVulkanTargetInfo;
typedef struct {uint32_t live_targets,target_limit;uint64_t allocated_bytes,allocation_limit,max_target_bytes;} SatchelVulkanTargetStats;
/* Real VkImage+view, one2D RGBA8_UNORM mip/layer/sample, optimal tiling.
 * No NVN layout, guest pool alias, texture sampling, presentation or draw claim.
 * Caps reflect the existing queue identity, physical format features/properties
 * and explicit profile limits. No optional device feature is silently enabled.
 * Allocation accounting includes all VkMemoryRequirements padding. Contents are
 * undefined until a successful clear fence; info never claims initial zeroes.
 * Tokens are monotonic typed IDs; no native handles/pointers leave the backend. */
SatchelVulkanStatus satchel_vulkan_target_capabilities(uint64_t device,uint32_t format,SatchelVulkanTargetCaps *);
SatchelVulkanStatus satchel_vulkan_target_create(uint64_t device,const SatchelVulkanTargetDescription *,uint64_t *target,SatchelVulkanTargetInfo *);
SatchelVulkanStatus satchel_vulkan_target_info(uint64_t device,uint64_t target,SatchelVulkanTargetInfo *);
SatchelVulkanStatus satchel_vulkan_target_destroy(uint64_t device,uint64_t target);
SatchelVulkanStatus satchel_vulkan_target_stats(SatchelVulkanTargetStats *);
/* Full target render-pass load clear then image-to-buffer transfer, no CPU fill.
 * Four finite linear float components in[0,1]; caller provides valid host memory.
 * Buffer must have TRANSFER_DESTINATION usage and same parent. Offset/row pitch
 * multiples of4; row pitch>=width*4; entire checked last-row span must fit.
 * Image and buffer stay locked, with the same existing job/fence ownership rule:
 * nonzero job owns resources even on error; cancel/timeout cannot free/reuse them.
 * Only actual successful fence reap commits image layout/initialization state.
 * Existing satchel_vulkan_submission_wait consumes completion; it does not read pixels.
 * Readback requires the checked buffer-read API after that fence. */
SatchelVulkanStatus satchel_vulkan_target_clear_readback(uint64_t device,uint64_t target,uint64_t buffer,
 uint64_t offset,uint64_t row_pitch,const float rgba[4],uint64_t *submission,SatchelVulkanCancelled,void *);
#ifdef __cplusplus
}
#endif
#endif
