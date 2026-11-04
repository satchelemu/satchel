#ifndef SATCHEL_VULKAN_DEVICE_H
#define SATCHEL_VULKAN_DEVICE_H
#include "VulkanBackend.h"
#include "GuestDeviceBackend.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Persistent real Vulkan instance/device/compute queue, max8 live handles.
 * Tokens are opaque integers, not pointers. Feature snapshot distinguishes
 * hardware-supported and enabled features (currently no optional features).
 * Only flags0 is supported; NVN debug/deferred/sampler flags are not ignored.
 * Owned buffer children are destroyed before their parent device.
 * Destruction returns BUSY while a retained submission exists; reap actual
 * completion first. No unbounded device-idle wait is performed. Calls serialized. */
SatchelVulkanStatus satchel_vulkan_device_create(uint32_t flags,uint64_t *handle,SatchelGuestDeviceCaps *caps);
SatchelVulkanStatus satchel_vulkan_device_capabilities(uint64_t handle,SatchelGuestDeviceCaps *caps);
SatchelVulkanStatus satchel_vulkan_device_destroy(uint64_t handle);
uint32_t satchel_vulkan_device_live_count(void);
/* Fills a trusted host callback table for explicit GuestDispatch attachment. */
int satchel_vulkan_guest_device_backend(SatchelGuestDeviceBackend *backend);
/* Retirement-owner-only nonblocking drain: consumes any completed job on this
 * device, invalidating its submission token. BUSY/error preserves ownership.
 * Do not mix with a separate active submission owner for the same device. */
SatchelVulkanStatus satchel_vulkan_device_drain(uint64_t device);
int satchel_vulkan_guest_device_backend_v2(SatchelGuestDeviceBackendV2 *backend);

#ifdef __cplusplus
}
#endif
#endif
