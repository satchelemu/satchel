#include "GuestVulkanDiagnostic.h"
#include "VulkanDevice.h"
#include <stdio.h>
static int cancel(void *context){(void)context;return 1;}
int main(void){
    SatchelGuestVulkanDiagnostic report={0};
    if(satchel_guest_vulkan_diagnostic(NULL,NULL,NULL) || satchel_guest_vulkan_diagnostic(&report,cancel,NULL))return 1;
    if(!satchel_guest_vulkan_diagnostic(&report,NULL,NULL))return 2;
    if(report.instructions!=48 || report.dispatch_events!=14 || report.compute_queries!=7 || !report.device_created || !report.device_finalized || !report.guards_matched || !report.state_preserved || !report.cleanup_matched || satchel_vulkan_device_live_count()!=0)return 3;
    printf("Bounded original guest NVN/Vulkan diagnostic:48 instructions,14 dispatches,7 real-cap queries, real device create/finalize, guards/state/cleanup verified.\n");return 0;
}
