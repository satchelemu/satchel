#ifndef SATCHEL_VULKAN_BUFFER_H
#define SATCHEL_VULKAN_BUFFER_H
#include "VulkanBackend.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { SATCHEL_VULKAN_BUFFER_LIMIT=32, SATCHEL_VULKAN_BUFFER_MAX_SIZE=16*1024*1024,
    SATCHEL_VULKAN_BUFFER_MEMORY_LIMIT=128*1024*1024 };
enum { SATCHEL_VULKAN_BUFFER_STORAGE=1,SATCHEL_VULKAN_BUFFER_TRANSFER_SOURCE=2,
    SATCHEL_VULKAN_BUFFER_TRANSFER_DESTINATION=4,SATCHEL_VULKAN_BUFFER_UNIFORM=8,SATCHEL_VULKAN_BUFFER_VERTEX=16 };
typedef struct {
    uint64_t device,size,allocation_size;
    uint32_t usage,coherent_memory;
} SatchelVulkanBufferInfo;
typedef struct {uint32_t live_buffers,buffer_limit;uint64_t allocated_bytes,allocation_limit,max_buffer_size;} SatchelVulkanBufferStats;
/* Trusted host-only resource primitive, not an NVN memory pool. Nonzero size
 * and at least one reviewed usage bit required. All memory starts zeroed.
 * Parent/token pairs are checked; tokens are typed monotonic IDs, not pointers.
 * Budget charges actual VkMemoryRequirements.size, including padding. */
SatchelVulkanStatus satchel_vulkan_buffer_create(uint64_t device,uint64_t size,uint32_t usage,
    uint64_t *buffer,SatchelVulkanBufferInfo *info);
SatchelVulkanStatus satchel_vulkan_buffer_info(uint64_t device,uint64_t buffer,SatchelVulkanBufferInfo *info);
/* Copies only to/from valid caller-owned HOST memory. Never pass guest virtual
 * addresses as these pointers: an adapter must use checked CPU copy APIs.
 * Full logical range validated before access; invalid reads leave output alone.
 * Zero-length operations accept null host pointers at an in-range offset.
 * Noncoherent accesses flush/invalidate the complete mapped allocation. A
 * driver flush failure may leave copied bytes changed; no rollback is promised
 * for device/API failure. Access returns BUSY while this buffer participates
 * in a retained submission; only a successful fence reap removes the lock. */
SatchelVulkanStatus satchel_vulkan_buffer_write(uint64_t device,uint64_t buffer,uint64_t offset,const void *source,size_t size);
SatchelVulkanStatus satchel_vulkan_buffer_read(uint64_t device,uint64_t buffer,uint64_t offset,void *destination,size_t size);
SatchelVulkanStatus satchel_vulkan_buffer_destroy(uint64_t device,uint64_t buffer);
SatchelVulkanStatus satchel_vulkan_buffer_stats(SatchelVulkanBufferStats *stats);
typedef struct {
    uint32_t status,stage,created_buffers,zero_initialized,write_completed,read_completed;
    uint32_t guards_matched,bounds_rejected,explicit_destroy,parent_cleanup,cleanup_matched;
    uint32_t baseline_buffers,final_buffers,baseline_devices,final_devices;
    uint64_t allocated_bytes,baseline_bytes,final_bytes;
} SatchelVulkanBufferReport;
/* Fixed original fixture only; no guest input, shader or GPU submission.
 * Creates two small real buffers, checks guarded host copies, rejects a bounds
 * violation, explicitly frees one buffer and frees the other via its parent.
 * Call on the serialized worker; cancellation is checked between driver calls.
 * Driver allocation/mapping/destruction itself is synchronous. */
SatchelVulkanStatus satchel_vulkan_buffer_diagnostic(SatchelVulkanBufferReport *report,
    SatchelVulkanCancelled cancelled,void *context);
/* Calls share the device API's serialization gate. Parent device destruction
 * destroys all its children first, but returns BUSY if a submission is retained.
 * Buffer destruction also returns BUSY while that buffer is retained. */
#ifdef __cplusplus
}
#endif
#endif
