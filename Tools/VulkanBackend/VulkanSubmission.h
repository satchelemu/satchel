#ifndef SATCHEL_VULKAN_SUBMISSION_H
#define SATCHEL_VULKAN_SUBMISSION_H
#include "VulkanBackend.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { SATCHEL_VULKAN_SUBMISSION_LIMIT=4,SATCHEL_VULKAN_SUBMISSION_WAIT_LIMIT_MS=5000 };
typedef struct {uint32_t status,completed,resources_retained;int32_t vk_result;} SatchelVulkanSubmissionReport;
/* One real transfer command per job, max4 retained jobs and one per device.
 * Fill offset/size must be nonzero-size, four-byte aligned; no VK_WHOLE_SIZE.
 * Copy uses distinct buffers with checked logical byte ranges. Required buffer
 * transfer usages and live matching parent are enforced. No arbitrary command
 * stream or native pointer is accepted. Cancelling before submit yields token0.
 * IMPORTANT: any nonzero output token owns retained resources, EVEN ON ERROR
 * after vkQueueSubmit was attempted. Preserve it for wait/reap. */
SatchelVulkanStatus satchel_vulkan_buffer_fill(uint64_t device,uint64_t buffer,uint64_t offset,uint64_t size,
    uint32_t value,uint64_t *submission,SatchelVulkanCancelled cancelled,void *context);
SatchelVulkanStatus satchel_vulkan_buffer_copy(uint64_t device,uint64_t source,uint64_t source_offset,
    uint64_t destination,uint64_t destination_offset,uint64_t size,uint64_t *submission,
    SatchelVulkanCancelled cancelled,void *context);
/* A successful real fence consumes the token and unlocks resources. Zero ms
 * polls once; positive wait is bounded <=5000ms with <=10ms driver waits.
 * Cancellation is checked before polling and between waits; it leaves ownership
 * retained even if hardware may already have completed. Timeout and driver
 * errors retain too. Later wait may reap actual completion. No fake signal,
 * unbounded wait, or force-destroy exists; irrecoverable loss may retain until
 * process exit. Callback must not reenter. Submitted resources cannot be read,
 * written or destroyed; parent device destruction is BUSY until successful reap. */
SatchelVulkanStatus satchel_vulkan_submission_wait(uint64_t device,uint64_t submission,uint32_t timeout_ms,
    SatchelVulkanCancelled cancelled,void *context,SatchelVulkanSubmissionReport *report);
uint32_t satchel_vulkan_submission_live_count(void);
typedef struct {
    uint32_t status,stage,fill_completed,copy_completed,values_matched,guards_matched;
    uint32_t cleanup_matched,resources_retained,prior_reaped;
    int32_t vk_result;
    uint32_t baseline_jobs,final_jobs,baseline_buffers,final_buffers,baseline_devices,final_devices;
    uint64_t baseline_bytes,final_bytes;
} SatchelVulkanTransferReport;
/* Fixed original GPU fill/copy only, no imported payload. Owns its private
 * device/buffers/job across invocations if completion is uncertain. A new call
 * first polls/reaps prior work or returns BUSY with resources_retained. It never
 * drops a nonzero submission token. Driver calls remain synchronous; each
 * submitted wait is capped at5000ms and cancellation checked between stages.
 * The report contains booleans/counts only, no tokens or fixture bytes. */
SatchelVulkanStatus satchel_vulkan_transfer_diagnostic(SatchelVulkanTransferReport *report,
    SatchelVulkanCancelled cancelled,void *context);

#ifdef __cplusplus
}
#endif
#endif
