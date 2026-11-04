#include "GuestPoolDispatchDiagnostic.h"
#include "GuestVulkanPools.h"
#include "VulkanDevice.h"
#include "VulkanBuffer.h"
#include "VulkanSubmission.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
enum {CALLER=0x100000,BOOTSTRAP=0x100100,STUBS=0x200000,DATA=0x300000,DEVICE_BUILDER=0x300100,
    DEVICE=0x301000,BUILDER=0x304100,POOL=0x305000,STORAGE=0x800000};
static atomic_flag active=ATOMIC_FLAG_INIT;
static SatchelGuestDispatch *deferred_handoff;
static int pending_retirement;
static void put32(uint8_t *p,uint32_t value){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(value>>(8*i));}
static int snapshot(SatchelGuestPoolDiagnostic *report,int final){
    SatchelVulkanBufferStats stats={0};SatchelVulkanStatus status=satchel_vulkan_buffer_stats(&stats);if(status!=SATCHEL_VULKAN_OK){report->backend_status=status;return 0;}
    if(final){report->final_devices=satchel_vulkan_device_live_count();report->final_buffers=stats.live_buffers;report->final_jobs=satchel_vulkan_submission_live_count();report->final_bytes=stats.allocated_bytes;}
    else {report->baseline_devices=satchel_vulkan_device_live_count();report->baseline_buffers=stats.live_buffers;report->baseline_jobs=satchel_vulkan_submission_live_count();report->baseline_bytes=stats.allocated_bytes;}
    return 1;
}
static int reap(SatchelGuestPoolDiagnostic *report){
    if(deferred_handoff && !satchel_guest_dispatch_handoff(&deferred_handoff)){report->status=SATCHEL_GUEST_POOL_DIAG_BUSY;return 0;}
    if(pending_retirement){SatchelGuestRetirementReport retired={0};SatchelGuestRetirementStatus status=satchel_guest_dispatch_retirement_poll(NULL,NULL,&retired);
        if(status!=SATCHEL_GUEST_RETIRE_IDLE){report->status=(status==SATCHEL_GUEST_RETIRE_PENDING || status==SATCHEL_GUEST_RETIRE_BUSY)?SATCHEL_GUEST_POOL_DIAG_BUSY:SATCHEL_GUEST_POOL_DIAG_FAILED;return 0;}
        pending_retirement=0;
    }
    return 1;
}
static int cancelled(SatchelGuestPoolDiagnostic *report,SatchelGuestPoolCancelled callback,void *context){if(callback && callback(context)){report->status=SATCHEL_GUEST_POOL_DIAG_CANCELLED;return 1;}return 0;}
static int invoke(SatchelCPUProbe *cpu,SatchelGuestDispatch *dispatch,SatchelGuestPoolDispatchDiagnostic *report,uint64_t target,
    uint64_t x0,uint64_t x1,uint64_t x2,SatchelGuestPoolCancelled cancel,void *context,uint64_t *value){
    if(!satchel_cpu_probe_set_pc(cpu,CALLER) || !satchel_cpu_probe_set_x(cpu,9,target) || !satchel_cpu_probe_set_x(cpu,0,x0) || !satchel_cpu_probe_set_x(cpu,1,x1) || !satchel_cpu_probe_set_x(cpu,2,x2))return 0;
    SatchelCPUProbeResult stop=satchel_cpu_probe_run(cpu,32,cancel,context);report->instructions+=stop.steps;report->cpu_stop_reason=stop.reason;
    if(stop.reason!=SATCHEL_CPU_BREAKPOINT){if(stop.reason==SATCHEL_CPU_CANCELLED)report->memory.status=SATCHEL_GUEST_POOL_DIAG_CANCELLED;return 0;}
    SatchelGuestDispatchReport dispatched={0};SatchelGuestDispatchStatus status=satchel_guest_dispatch_handle(dispatch,cpu,&stop,cancel,context,&dispatched);
    report->dispatch_status=status;report->dispatch_events=dispatched.events;
    if(status!=SATCHEL_GUEST_DISPATCH_RETURNED){if(status==SATCHEL_GUEST_DISPATCH_CANCELLED)report->memory.status=SATCHEL_GUEST_POOL_DIAG_CANCELLED;return 0;}
    stop=satchel_cpu_probe_run(cpu,8,cancel,context);report->instructions+=stop.steps;report->cpu_stop_reason=stop.reason;
    if(stop.reason==SATCHEL_CPU_CANCELLED)report->memory.status=SATCHEL_GUEST_POOL_DIAG_CANCELLED;
    return stop.reason==SATCHEL_CPU_SVC && stop.svc==0x42 && satchel_cpu_probe_get_x(cpu,0,value);
}
static int call(SatchelCPUProbe *cpu,SatchelGuestDispatch *dispatch,SatchelGuestPoolDispatchDiagnostic *report,const char *name,
    uint64_t x0,uint64_t x1,uint64_t x2,SatchelGuestPoolCancelled cancel,void *context,uint64_t *value){
    uint64_t target=0;
    return satchel_cpu_probe_write(cpu,DATA,name,strlen(name)+1) && invoke(cpu,dispatch,report,BOOTSTRAP,DATA,0,0,cancel,context,&target) &&
        invoke(cpu,dispatch,report,target,x0,x1,x2,cancel,context,value);
}
int satchel_guest_pool_dispatch_diagnostic(SatchelGuestPoolDispatchDiagnostic *report,SatchelGuestPoolCancelled cancel,void *context){
    if(!report)return 0;memset(report,0,sizeof(*report));SatchelGuestPoolDiagnostic *r=&report->memory;r->status=SATCHEL_GUEST_POOL_DIAG_FAILED;r->stage=1;
    if(atomic_flag_test_and_set(&active)){r->status=SATCHEL_GUEST_POOL_DIAG_BUSY;r->resources_retained=1;return 0;}
    SatchelCPUProbe *cpu=NULL;SatchelGuestDispatch *dispatch=NULL;uint8_t *data=NULL,*storage=NULL,*stubs=NULL;
    uint8_t code[8],pattern[64],actual[64],modified[16],vector[16],read_vector[16],guards[2],object[256];
    SatchelGuestDispatchLayout layout={0};SatchelGuestDeviceBackendV2 backend={0};SatchelGuestPoolBinding binding={0};
    uint64_t value=0,gpu=0,device=0,job=0;char error[160]={0};int skip_reap=0;
    if(!snapshot(r,0))goto done;
    if(deferred_handoff || pending_retirement){if(!reap(r)){skip_reap=1;goto done;}r->prior_reaped=1;if(!snapshot(r,0))goto done;}
    if(cancelled(r,cancel,context))goto done;
    data=malloc(65536);storage=calloc(1,65536);if(!data || !storage || !satchel_guest_dispatch_build_stubs(NULL,0,&layout))goto done;
    stubs=malloc(layout.region_size);if(!stubs || !satchel_guest_dispatch_build_stubs(stubs,layout.region_size,&layout))goto done;
    memset(data,0x7a,65536);put32(code,0xd63f0120);put32(code+4,0xd4000841); /* Original BLR X9; SVC #0x42. */
    for(unsigned i=0;i<64;i++)pattern[i]=(uint8_t)(i^0xa6);memcpy(storage,pattern,sizeof(pattern));
    SatchelCPUProbeRegion regions[]={{CALLER,code,sizeof(code),4096,5},{STUBS,stubs,layout.region_size,layout.region_size,5},
        {DATA,data,65536,65536,3},{STORAGE,storage,65536,65536,3}};
    cpu=satchel_cpu_probe_create(regions,4,CALLER,error,sizeof(error));free(data);data=NULL;free(storage);storage=NULL;free(stubs);stubs=NULL;if(!cpu)goto done;
    dispatch=satchel_guest_dispatch_create(BOOTSTRAP,STUBS,1024,30000);if(!dispatch || !satchel_vulkan_guest_device_backend_v2(&backend) ||
        !satchel_guest_dispatch_set_device_backend_v2(dispatch,&backend) || !satchel_guest_dispatch_enable_vulkan_pools(dispatch))goto done;
    uint64_t points[2];if(!satchel_guest_dispatch_breakpoints(dispatch,points) || !satchel_cpu_probe_set_breakpoints(cpu,points,2))goto done;
    for(unsigned i=0;i<16;i++)vector[i]=(uint8_t)(i*7);
    if(!satchel_cpu_probe_set_x(cpu,19,UINT64_C(0x23456789abcdef01)) || !satchel_cpu_probe_set_q(cpu,8,vector))goto done;
    r->stage=2;if(cancelled(r,cancel,context))goto done;
    if(!call(cpu,dispatch,report,"nvnDeviceBuilderSetDefaults",DEVICE_BUILDER,0,0,cancel,context,&value) || value!=DEVICE_BUILDER ||
        !call(cpu,dispatch,report,"nvnDeviceInitialize",DEVICE,DEVICE_BUILDER,0,cancel,context,&value) || value!=1)goto done;
    r->device_created=satchel_guest_dispatch_device_count(dispatch)==1;if(!r->device_created)goto done;
    r->stage=3;if(cancelled(r,cancel,context))goto done;
    if(!call(cpu,dispatch,report,"nvnMemoryPoolBuilderSetDefaults",BUILDER,0,0,cancel,context,&value) || value!=BUILDER ||
        !call(cpu,dispatch,report,"nvnMemoryPoolBuilderSetDevice",BUILDER,DEVICE,0,cancel,context,&value) || value!=BUILDER ||
        !call(cpu,dispatch,report,"nvnMemoryPoolBuilderSetStorage",BUILDER,STORAGE,65536,cancel,context,&value) || value!=BUILDER ||
        !call(cpu,dispatch,report,"nvnMemoryPoolBuilderSetFlags",BUILDER,SATCHEL_GUEST_POOL_CACHED_FLAGS,0,cancel,context,&value) || value!=BUILDER ||
        !call(cpu,dispatch,report,"nvnMemoryPoolInitialize",POOL,BUILDER,0,cancel,context,&value) || value!=1)goto done;
    r->pool_created=1;
    if(!call(cpu,dispatch,report,"nvnMemoryPoolMap",POOL,0,0,cancel,context,&value) || value!=STORAGE ||
        !call(cpu,dispatch,report,"nvnMemoryPoolGetBufferAddress",POOL,0,0,cancel,context,&gpu))goto done;
    report->dispatch_status=satchel_guest_dispatch_vulkan_pool_binding(dispatch,cpu,DEVICE,gpu,sizeof(pattern),&binding,&device);if(report->dispatch_status!=SATCHEL_GUEST_DISPATCH_RETURNED)goto done;
    r->backend_status=satchel_vulkan_buffer_read(device,binding.buffer,0,actual,sizeof(actual));if(r->backend_status!=SATCHEL_VULKAN_OK || memcmp(actual,pattern,sizeof(pattern)))goto done;r->initial_upload=1;
    r->stage=4;if(cancelled(r,cancel,context))goto done;
    memset(modified,0x35,sizeof(modified));if(!satchel_cpu_probe_write(cpu,STORAGE+16,modified,sizeof(modified)))goto done;
    r->backend_status=satchel_vulkan_buffer_read(device,binding.buffer,0,actual,sizeof(actual));if(r->backend_status!=SATCHEL_VULKAN_OK || memcmp(actual,pattern,sizeof(pattern)))goto done;
    if(!call(cpu,dispatch,report,"nvnMemoryPoolFlushMappedRange",POOL,16,16,cancel,context,&value) || value!=POOL)goto done;memcpy(pattern+16,modified,sizeof(modified));
    r->backend_status=satchel_vulkan_buffer_read(device,binding.buffer,0,actual,sizeof(actual));if(r->backend_status!=SATCHEL_VULKAN_OK || memcmp(actual,pattern,sizeof(pattern)))goto done;r->flush_completed=1;
    r->stage=5;if(cancelled(r,cancel,context))goto done;
    r->backend_status=satchel_vulkan_buffer_fill(device,binding.buffer,32,16,0x12345678,&job,cancel,context);
    if(r->backend_status!=SATCHEL_VULKAN_OK){if(r->backend_status==SATCHEL_VULKAN_CANCELLED)r->status=SATCHEL_GUEST_POOL_DIAG_CANCELLED;goto done;}if(!job)goto done;
    r->stage=6;SatchelVulkanSubmissionReport completed={0};r->backend_status=satchel_vulkan_submission_wait(device,job,5000,cancel,context,&completed);
    if(r->backend_status!=SATCHEL_VULKAN_OK){if(r->backend_status==SATCHEL_VULKAN_CANCELLED)r->status=SATCHEL_GUEST_POOL_DIAG_CANCELLED;else if(r->backend_status==SATCHEL_VULKAN_TIMEOUT || r->backend_status==SATCHEL_VULKAN_BUSY)r->status=SATCHEL_GUEST_POOL_DIAG_BUSY;goto done;}
    job=0;if(!completed.completed || completed.resources_retained)goto done;r->gpu_completed=1;
    r->stage=7;if(cancelled(r,cancel,context))goto done;
    if(!satchel_cpu_probe_read(cpu,STORAGE,actual,sizeof(actual)) || memcmp(actual,pattern,sizeof(pattern)))goto done;
    if(!call(cpu,dispatch,report,"nvnMemoryPoolInvalidateMappedRange",POOL,32,16,cancel,context,&value) || value!=POOL)goto done;r->invalidate_completed=1;
    for(unsigned i=32;i<48;i+=4)put32(pattern+i,0x12345678);
    if(!satchel_cpu_probe_read(cpu,STORAGE,actual,sizeof(actual)))goto done;r->values_matched=!memcmp(actual+32,pattern+32,16);r->guards_matched=!memcmp(actual,pattern,32) && !memcmp(actual+48,pattern+48,16);
    if(!satchel_cpu_probe_read(cpu,POOL-1,guards,1) || !satchel_cpu_probe_read(cpu,POOL+256,guards+1,1))goto done;r->guards_matched=r->guards_matched && guards[0]==0x7a && guards[1]==0x7a;
    r->state_preserved=satchel_cpu_probe_get_x(cpu,19,&value) && value==UINT64_C(0x23456789abcdef01) && satchel_cpu_probe_get_q(cpu,8,read_vector) && !memcmp(vector,read_vector,sizeof(vector));
    if(!r->values_matched || !r->guards_matched || !r->state_preserved)goto done;
    r->stage=8;if(cancelled(r,cancel,context))goto done;
    if(!call(cpu,dispatch,report,"nvnMemoryPoolFinalize",POOL,0,0,cancel,context,&value) || value!=POOL)goto done;r->pool_finalized=1;
    if(!satchel_cpu_probe_read(cpu,POOL,object,sizeof(object)))goto done;for(unsigned i=0;i<sizeof(object);i++)if(object[i])goto done;
    if(!call(cpu,dispatch,report,"nvnDeviceFinalize",DEVICE,0,0,cancel,context,&value) || value!=DEVICE)goto done;report->device_finalized=satchel_guest_dispatch_device_count(dispatch)==0;
    if(!report->device_finalized || !satchel_cpu_probe_read(cpu,POOL-1,guards,1) || !satchel_cpu_probe_read(cpu,POOL+256,guards+1,1) || guards[0]!=0x7a || guards[1]!=0x7a)goto done;
    r->stage=9;if(cancelled(r,cancel,context))goto done;r->status=SATCHEL_GUEST_POOL_DIAG_OK;
 done:
    free(data);free(storage);free(stubs);
    if(dispatch){
        /* Preserve the owner even on unexpected handoff refusal; the deferred
         * pointer contains no CPU/provider borrow and is retried next time. */
        if(!satchel_guest_dispatch_handoff(&dispatch))deferred_handoff=dispatch;
        pending_retirement=1;
    }
    if(cpu)satchel_cpu_probe_destroy(cpu);
    if(job)skip_reap=1; /* The accepted retirement owner now owns fence reaping. */
    if(!skip_reap && (deferred_handoff || pending_retirement) && !reap(r) && r->status==SATCHEL_GUEST_POOL_DIAG_OK)r->status=SATCHEL_GUEST_POOL_DIAG_BUSY;
    r->resources_retained=deferred_handoff || pending_retirement;
    if(!snapshot(r,1))r->status=SATCHEL_GUEST_POOL_DIAG_FAILED;
    r->cleanup_matched=!r->resources_retained && r->baseline_devices==r->final_devices && r->baseline_buffers==r->final_buffers && r->baseline_jobs==r->final_jobs && r->baseline_bytes==r->final_bytes;
    if(r->status==SATCHEL_GUEST_POOL_DIAG_OK){if(!r->cleanup_matched)r->status=SATCHEL_GUEST_POOL_DIAG_FAILED;else r->stage=10;}
    const int success=r->status==SATCHEL_GUEST_POOL_DIAG_OK;atomic_flag_clear(&active);return success;
}
