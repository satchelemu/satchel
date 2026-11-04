#ifndef SATCHEL_VULKAN_TRANSFER_BATCH_H
#define SATCHEL_VULKAN_TRANSFER_BATCH_H
#include "VulkanBackend.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {SATCHEL_VULKAN_TRANSFER_FILL=1,SATCHEL_VULKAN_TRANSFER_COPY=2,
    SATCHEL_VULKAN_TRANSFER_COMMAND_LIMIT=32,SATCHEL_VULKAN_TRANSFER_WORK_LIMIT=32*1024*1024};
typedef struct {
    uint32_t kind,value;
    uint64_t source,source_offset,destination,destination_offset,size;
} SatchelVulkanTransferCommand;
/* Trusted host-only bounded batch, not a guest command stream. Copies and fully
 * validates1..32 descriptors before creating driver commands/submitting. Fill
 * requires source/source_offset0 and4-byte-aligned destination/size; copy requires
 * value0. Same-buffer copy is supported only for disjoint source/destination
 * spans. Every span is nonempty/in-range with actual matching device/usage.
 * Total transfer work is <=32MiB (fill=size,copy=2*size), up to32 unique buffers.
 * Real barriers preserve descriptor order, including RAW/WAR/WAW dependencies.
 * Uses one existing job/fence: max4 global/one per device. ALL referenced buffers
 * stay locked until a real successful fence reap. Cancellation before submit
 * returns token0; any nonzero token after attempted submit retains ownership,
 * including API_ERROR. Use the existing wait/device-retirement APIs to reap.
 * Callback must not reenter; descriptor/host pointers must be valid for their
 * declared size. No native resource/descriptor pointer enters guest memory. */
SatchelVulkanStatus satchel_vulkan_transfer_batch(uint64_t device,const SatchelVulkanTransferCommand *commands,size_t count,
    uint64_t *submission,SatchelVulkanCancelled cancelled,void *context);
#ifdef __cplusplus
}
#endif
#endif
