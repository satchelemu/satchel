#ifndef SATCHEL_VULKAN_BACKEND_H
#define SATCHEL_VULKAN_BACKEND_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Original fixed SPIR-V compute diagnostic, not a game or Maxwell renderer. */
typedef enum { SATCHEL_VULKAN_OK=0, SATCHEL_VULKAN_INVALID, SATCHEL_VULKAN_BUSY, SATCHEL_VULKAN_UNSUPPORTED,
    SATCHEL_VULKAN_API_ERROR, SATCHEL_VULKAN_TIMEOUT, SATCHEL_VULKAN_CANCELLED, SATCHEL_VULKAN_MISMATCH,
    SATCHEL_VULKAN_INTERNAL_ERROR } SatchelVulkanStatus;
typedef struct {
    uint32_t status, stage;
    int32_t vk_result;
    uint32_t device_api_version, portability_subset_enabled, coherent_memory;
    uint32_t command_completed, value_matched, guard_words_matched, resources_retained;
} SatchelVulkanReport;
typedef int (*SatchelVulkanCancelled)(void *context);
/* Off-UI-thread only. Uses only original fixed shader/data; no imported payload.
 * CPU/driver pipeline compilation is synchronous; cancellation is checked
 * between stages. Submitted work has a five-second fence wait, polled every
 * 50ms for cancellation. A timed-out/cancelled in-flight job remains retained
 * (at most one); later calls poll/clean it before admitting any new job. No
 * pending Vulkan resource is destroyed and no fabricated completion is returned.
 * Calls are serialized; concurrent/reentrant calls return BUSY. The callback
 * must not reenter this API. Report contains metadata/status only. */
SatchelVulkanStatus satchel_vulkan_compute_diagnostic(SatchelVulkanReport *report,
    SatchelVulkanCancelled cancelled, void *context);
#ifdef __cplusplus
}
#endif
#include "VulkanDevice.h"
#include "VulkanBuffer.h"
#include "VulkanSubmission.h"
#include "VulkanTransferBatch.h"
#include "VulkanCompute.h"
#include "VulkanRenderTarget.h"
#include "VulkanRaster.h"
#include "VulkanRasterBatch.h"
#endif
