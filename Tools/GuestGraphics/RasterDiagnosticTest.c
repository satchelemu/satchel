#include "GuestNVNRasterDiagnostic.h"
#include "GuestDispatch.h"
#include "VulkanDevice.h"
#include "VulkanBuffer.h"
#include "VulkanRaster.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"raster diagnostic line%u: %s\n",__LINE__,#x);exit(1);}}while(0)
static void verify(SatchelGuestNVNRasterDiagnostic *r){CHECK(r->memory.status==SATCHEL_GUEST_POOL_DIAG_OK&&r->memory.stage==8);CHECK(r->instructions==558&&r->dispatch_events==186);CHECK(r->program_admissions==2&&r->pipelines_compiled==2&&r->commands_recorded==3&&r->fences_completed==1);CHECK(r->cpu_cache_preserved&&r->memory.values_matched&&r->memory.guards_matched&&r->memory.state_preserved&&r->memory.gpu_completed&&r->memory.flush_completed&&r->memory.invalidate_completed&&r->memory.cleanup_matched);CHECK(r->programs_finalized==2&&r->texture_finalized&&r->handle_finalized&&r->command_finalized&&r->queue_finalized&&r->device_finalized&&!r->memory.resources_retained);}
static int always(void *p){(void)p;return 1;}
static int during(void *p){SatchelGuestNVNRasterDiagnostic *r=p;return r->memory.stage>=5;}
static int nested(void *p){unsigned *calls=p;if(!(*calls)++){SatchelGuestNVNRasterDiagnostic r;CHECK(!satchel_guest_nvn_raster_diagnostic(&r,NULL,NULL)&&r.memory.status==SATCHEL_GUEST_POOL_DIAG_BUSY);}return 0;}
int main(void){SatchelGuestNVNRasterDiagnostic r;int ok=satchel_guest_nvn_raster_diagnostic(&r,NULL,NULL);if(!ok)fprintf(stderr,"diagnostic status%u stage%u dispatch%u cpu%u instructions%llu events%llu retained%u\n",r.memory.status,r.memory.stage,r.dispatch_status,r.cpu_stop_reason,(unsigned long long)r.instructions,(unsigned long long)r.dispatch_events,r.memory.resources_retained);CHECK(ok);verify(&r);CHECK(!satchel_guest_nvn_raster_diagnostic(&r,always,NULL)&&r.memory.status==SATCHEL_GUEST_POOL_DIAG_CANCELLED&&r.memory.cleanup_matched);
 CHECK(!satchel_guest_nvn_raster_diagnostic(&r,during,&r)&&r.memory.status==SATCHEL_GUEST_POOL_DIAG_CANCELLED);for(unsigned i=0;i<500;i++){ok=satchel_guest_nvn_raster_diagnostic(&r,NULL,NULL);if(ok)break;CHECK(r.memory.status==SATCHEL_GUEST_POOL_DIAG_BUSY);struct timespec t={0,1000000};nanosleep(&t,NULL);}CHECK(ok);verify(&r);unsigned calls=0;CHECK(satchel_guest_nvn_raster_diagnostic(&r,nested,&calls));CHECK(calls);verify(&r);SatchelVulkanBufferStats b;SatchelVulkanRasterStats p;CHECK(satchel_vulkan_buffer_stats(&b)==SATCHEL_VULKAN_OK&&!b.live_buffers&&!b.allocated_bytes);CHECK(satchel_vulkan_raster_stats(&p)==SATCHEL_VULKAN_OK&&!p.live_pipelines&&!satchel_vulkan_device_live_count());printf("Reusable NVN raster diagnostic: %u host assertions, normal/cancel-after-submit/reentry/reap; each successful original journey558 instructions186 events.\n",checks);return 0;}
