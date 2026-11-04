#ifndef SATCHEL_VULKAN_CAPTURED_HOST_H
#define SATCHEL_VULKAN_CAPTURED_HOST_H
#include "VulkanBackend.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Snapshot for the strict flat VS/FS compiler profile only, not general hardware
 * capability discovery. Unknown driver bugs remain unknown. Query requires an
 * actual live same-parent RGBA8 target; no caller-supplied native format fact. */
typedef struct {
 uint32_t version,size,profile;
 uint64_t device_known,policy_known,unknown,values;
 uint32_t storage_alignment,physical_bgra8_color_attachment;
 uint32_t admitted_color_format,current_color_format;
} SatchelVulkanCapturedHost;
/* Failure preserves output; no pointers/resources retained. Caller must keep
 * device/target alive and serialize later compile/submit with their owner. */
SatchelVulkanStatus satchel_vulkan_captured_host(uint64_t device,uint64_t target,SatchelVulkanCapturedHost *);
#ifdef __cplusplus
}
#endif
#endif
