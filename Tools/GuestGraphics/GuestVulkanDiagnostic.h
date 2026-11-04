#ifndef SATCHEL_GUEST_VULKAN_DIAGNOSTIC_H
#define SATCHEL_GUEST_VULKAN_DIAGNOSTIC_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    uint32_t status,stage,dispatch_status,cpu_stop_reason,api_version;
    uint32_t device_created,device_finalized,guards_matched,state_preserved,cleanup_matched;
    uint32_t baseline_devices,final_devices;
    uint64_t instructions,dispatch_events;
    uint32_t compute_queries;
} SatchelGuestVulkanDiagnostic;
typedef int (*SatchelGuestVulkanCancelled)(void *context);
/* Fixed original caller/stubs in a separate bounded interpreter context.
 * Executes bootstrap -> builder defaults -> real DeviceInitialize/Finalize
 * through checked guest dispatch, including all seven compute-limit queries.
 * Never accepts or inspects game data/CPU.
 * Call on worker outside any active CPU run; optional cancel callback only.
 * Device creation/destruction are synchronous driver operations; no shader or
 * frame is submitted. All resources are owned and cleaned before return.
 * Success proves interpreted lifecycle linkage, not game NVN compatibility. */
int satchel_guest_vulkan_diagnostic(SatchelGuestVulkanDiagnostic *report,
    SatchelGuestVulkanCancelled cancelled,void *context);
#ifdef __cplusplus
}
#endif
#endif
