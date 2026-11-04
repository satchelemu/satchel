#ifndef SATCHEL_VULKAN_SCALAR_H
#define SATCHEL_VULKAN_SCALAR_H
#include "VulkanCompute.h"
#include "ScalarCompiler.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Separate bounded straight-line uint32 profile, not exact-fixture admission.
 * Copies caller code and reflection; independently validates the entire scalar
 * SPIR-V type/decorations/ID/use/access/operation schema and derived extents.
 * Actual compiler output is consumed; no static shader substitution. */
SatchelVulkanStatus satchel_vulkan_scalar_pipeline_create(uint64_t device,const void *spirv,size_t size,const SatchelScalarReflection *,uint64_t *pipeline,SatchelVulkanCancelled,void *);
typedef struct {SatchelVulkanComputeBindings buffers;uint64_t storage_guest_address;uint32_t guest_cb_storage_disjoint,reserved;} SatchelVulkanScalarBindings;
/* Trusted owner must resolve storage_guest_address to this actual retained
 * storage buffer/range and prove its original guest backing is disjoint from the
 * captured CB backing (guest_cb_storage_disjoint=1, reserved=0). Separate VkBuffers
 * alone do not prove guest-source disjointness. Backend verifies it matches compile reflection and the
 * current GPU-visible UBO pointer pair. Captured unknown metadata is NOT admitted.
 * All existing buffer/pipeline/fence/cancellation ownership rules apply. */
SatchelVulkanStatus satchel_vulkan_scalar_submit(uint64_t device,uint64_t pipeline,const SatchelVulkanScalarBindings *,uint64_t *job,SatchelVulkanCancelled,void *);
/* Host-only schema gate, no driver calls. Failure leaves derived output intact. */
int satchel_vulkan_scalar_validate(const void *spirv,size_t size,const SatchelScalarReflection *);
#ifdef __cplusplus
}
#endif
#endif
