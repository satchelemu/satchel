#ifndef SATCHEL_VULKAN_COMPUTE_H
#define SATCHEL_VULKAN_COMPUTE_H
#include "VulkanBackend.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {SATCHEL_VULKAN_COMPUTE_PIPELINE_LIMIT=8,SATCHEL_VULKAN_COMPUTE_UNIFORM_BYTES=65536,
    SATCHEL_VULKAN_COMPUTE_SPIRV_LIMIT=16384};
typedef struct {
    uint32_t version,struct_size,max_uniform_range,max_storage_range;
    uint32_t max_set_uniform_buffers,max_set_storage_buffers,max_stage_uniform_buffers,max_stage_storage_buffers;
    uint32_t max_stage_resources,max_bound_sets,precise_math_requested,reserved;
    uint64_t uniform_offset_alignment,storage_offset_alignment;
} SatchelVulkanComputeLimits;
/* Versioned additional limits; preserves existing SatchelGuestDeviceCaps ABI. */
SatchelVulkanStatus satchel_vulkan_compute_limits(uint64_t device,SatchelVulkanComputeLimits *);
typedef struct {uint32_t version,struct_size,uniform_binding,storage_binding;} SatchelVulkanComputeProfile;
/* Trusted ORIGINAL scalar fixture only, not imported game SPIR-V admission.
 * Copies code before validation/callbacks. Bounded known program-shape gate;
 * one UBO and one writable SSBO, set0, main, local1^3. Unused support UBO is not
 * bound. A pipeline token owns real shader/layout/pipeline objects on its device.
 * Creation checks cancellation before/after synchronous driver compilation; it
 * cannot interrupt vkCreateComputePipelines itself. No source/payload logging. */
SatchelVulkanStatus satchel_vulkan_compute_pipeline_create_original(uint64_t device,
    const void *spirv,size_t spirv_size,const SatchelVulkanComputeProfile *,uint64_t *pipeline,
    SatchelVulkanCancelled,void *);
SatchelVulkanStatus satchel_vulkan_compute_pipeline_destroy(uint64_t device,uint64_t pipeline);
uint32_t satchel_vulkan_compute_pipeline_live_count(void);
typedef struct {
    uint64_t uniform_buffer,uniform_offset,uniform_size;
    uint64_t storage_buffer,storage_offset,storage_size;
} SatchelVulkanComputeBindings;
/* Single invocation of the admitted fixture. Full64KiB UBO binding, bounded
 * 16..4096-byte SSBO. Nonempty aligned in-range offsets and actual usage/parent
 * required; buffers are distinct. Descriptor set/pool is immutable per job.
 * Existing fence API consumes completion. Any nonzero job, including on an API
 * error, retains pipeline/descriptors/buffers. Cancel/timeout does not release.
 * Destroy/host access is BUSY while retained. Same4-job/one-device global gate. */
SatchelVulkanStatus satchel_vulkan_compute_submit_original(uint64_t device,uint64_t pipeline,
    const SatchelVulkanComputeBindings *,uint64_t *job,SatchelVulkanCancelled,void *);
#ifdef __cplusplus
}
#endif
#include "VulkanScalar.h"
#endif
