#include "GuestPoolDiagnostic.h"
#include "GuestPools.h"
#include "VulkanDevice.h"
#include "VulkanBuffer.h"
#include "VulkanSubmission.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
enum {DEVICE=0x300000,BUILDER=0x304000,POOL=0x305000,STORAGE=0x400000};
typedef struct {SatchelGuestPools *pools;uint64_t device,job;} Owner;
static Owner owner;
static atomic_flag active=ATOMIC_FLAG_INIT;
static void put32(uint8_t *p,uint32_t value){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(value>>(8*i));}
static uint64_t get64(const uint8_t *p){uint64_t value=0;for(unsigned i=0;i<8;i++)value|=(uint64_t)p[i]<<(8*i);return value;}
static int validate(void *context,SatchelCPUProbe *cpu,uint64_t address,uint64_t *token,uint64_t *generation){
    Owner *state=context;uint8_t tag[16];SatchelGuestDeviceCaps caps={0};
    if(address!=DEVICE || !satchel_cpu_probe_check_range(cpu,address,0x3000,SATCHEL_CPU_READ) ||
        !satchel_cpu_probe_read(cpu,address,tag,sizeof(tag)) || get64(tag)!=UINT64_C(0x1504c4456) || get64(tag+8)!=1 ||
        satchel_vulkan_device_capabilities(state->device,&caps)!=SATCHEL_VULKAN_OK)return 0;
    *token=state->device;*generation=1;return 1;
}
static int snapshot(SatchelGuestPoolDiagnostic *report,int final){
    SatchelVulkanBufferStats stats={0};SatchelVulkanStatus status=satchel_vulkan_buffer_stats(&stats);
    if(status!=SATCHEL_VULKAN_OK){report->backend_status=status;return 0;}
    if(final){report->final_devices=satchel_vulkan_device_live_count();report->final_buffers=stats.live_buffers;report->final_jobs=satchel_vulkan_submission_live_count();report->final_bytes=stats.allocated_bytes;}
    else {report->baseline_devices=satchel_vulkan_device_live_count();report->baseline_buffers=stats.live_buffers;report->baseline_jobs=satchel_vulkan_submission_live_count();report->baseline_bytes=stats.allocated_bytes;}
    return 1;
}
static int cleanup(SatchelGuestPoolDiagnostic *report){
    if(owner.job)return 0;
    if(owner.pools){SatchelGuestPoolStatus status=satchel_guest_pools_try_destroy(&owner.pools);if(status!=SATCHEL_GUEST_POOL_OK){report->pool_status=status;return 0;}}
    if(owner.device){SatchelVulkanStatus status=satchel_vulkan_device_destroy(owner.device);if(status!=SATCHEL_VULKAN_OK){report->backend_status=status;return 0;}owner.device=0;}
    return 1;
}
static int cancelled(SatchelGuestPoolDiagnostic *report,SatchelGuestPoolCancelled callback,void *context){
    if(callback && callback(context)){report->status=SATCHEL_GUEST_POOL_DIAG_CANCELLED;return 1;}return 0;
}
static int operation(SatchelCPUProbe *cpu,SatchelGuestPoolDiagnostic *report,SatchelGuestPoolOperation opcode,uint64_t object,uint64_t a,uint64_t b,uint64_t *value){
    SatchelGuestPoolRequest request={opcode,object,a,b};SatchelGuestPoolResult result={0};
    const SatchelGuestPoolStatus status=satchel_guest_pools_execute(owner.pools,cpu,&request,&result);report->pool_status=status;
    if(value)*value=result.value;return status==SATCHEL_GUEST_POOL_OK;
}
int satchel_guest_pool_diagnostic(SatchelGuestPoolDiagnostic *report,SatchelGuestPoolCancelled cancel,void *context){
    if(!report)return 0;memset(report,0,sizeof(*report));report->status=SATCHEL_GUEST_POOL_DIAG_FAILED;report->stage=1;
    if(atomic_flag_test_and_set(&active)){report->status=SATCHEL_GUEST_POOL_DIAG_BUSY;report->resources_retained=1;return 0;}
    SatchelCPUProbe *cpu=NULL;uint8_t *data=NULL,*storage=NULL;uint8_t code[4],pattern[64],actual[64],vector[16],read_vector[16],tag[16]={0};
    uint64_t gpu=0,value=0;SatchelGuestPoolBinding binding={0};SatchelGuestDeviceCaps caps={0};char error[160]={0};
    if(!snapshot(report,0))goto done;
    if(owner.device || owner.pools || owner.job){
        if(owner.job){
            SatchelVulkanSubmissionReport completion={0};SatchelVulkanStatus status=satchel_vulkan_submission_wait(owner.device,owner.job,0,NULL,NULL,&completion);report->backend_status=status;
            if(status!=SATCHEL_VULKAN_OK){report->status=(status==SATCHEL_VULKAN_TIMEOUT || status==SATCHEL_VULKAN_BUSY)?SATCHEL_GUEST_POOL_DIAG_BUSY:SATCHEL_GUEST_POOL_DIAG_FAILED;goto done;}
            owner.job=0;
        }
        if(!cleanup(report))goto done;report->prior_reaped=1;
        if(!snapshot(report,0))goto done;
    }
    if(cancelled(report,cancel,context))goto done;
    data=malloc(65536);storage=calloc(1,65536);if(!data || !storage)goto done;memset(data,0x7a,65536);
    put32(code,0xd4200000); /* Original inert BRK word; no instruction execution. */
    for(unsigned i=0;i<64;i++)pattern[i]=(uint8_t)(i^0xa6);memcpy(storage,pattern,sizeof(pattern));
    put32(tag,0x504c4456);put32(tag+4,1);put32(tag+8,1);memcpy(data,tag,sizeof(tag));
    SatchelCPUProbeRegion regions[]={{0x100000,code,sizeof(code),4096,5},{DEVICE,data,65536,65536,3},{STORAGE,storage,65536,65536,3}};
    cpu=satchel_cpu_probe_create(regions,3,0x100000,error,sizeof(error));free(data);data=NULL;free(storage);storage=NULL;if(!cpu)goto done;
    for(unsigned i=0;i<16;i++)vector[i]=(uint8_t)(i*7);
    if(!satchel_cpu_probe_set_x(cpu,19,UINT64_C(0x23456789abcdef01)) || !satchel_cpu_probe_set_q(cpu,8,vector))goto done;
    report->stage=2;if(cancelled(report,cancel,context))goto done;
    report->backend_status=satchel_vulkan_device_create(0,&owner.device,&caps);if(report->backend_status!=SATCHEL_VULKAN_OK)goto done;report->device_created=1;
    owner.pools=satchel_guest_pools_create(validate,&owner);if(!owner.pools)goto done;
    report->stage=3;if(cancelled(report,cancel,context))goto done;
    if(!operation(cpu,report,SATCHEL_GUEST_POOL_BUILDER_DEFAULTS,BUILDER,0,0,NULL) ||
        !operation(cpu,report,SATCHEL_GUEST_POOL_BUILDER_DEVICE,BUILDER,DEVICE,0,NULL) ||
        !operation(cpu,report,SATCHEL_GUEST_POOL_BUILDER_STORAGE,BUILDER,STORAGE,65536,NULL) ||
        !operation(cpu,report,SATCHEL_GUEST_POOL_BUILDER_FLAGS,BUILDER,SATCHEL_GUEST_POOL_CACHED_FLAGS,0,NULL) ||
        !operation(cpu,report,SATCHEL_GUEST_POOL_INITIALIZE,POOL,BUILDER,0,&value) || value!=1)goto done;
    report->pool_created=1;
    if(!operation(cpu,report,SATCHEL_GUEST_POOL_GPU_ADDRESS,POOL,0,0,&gpu))goto done;
    report->pool_status=satchel_guest_pools_resolve(owner.pools,cpu,DEVICE,gpu,sizeof(pattern),&binding);if(report->pool_status!=SATCHEL_GUEST_POOL_OK)goto done;
    report->backend_status=satchel_vulkan_buffer_read(owner.device,binding.buffer,0,actual,sizeof(actual));
    if(report->backend_status!=SATCHEL_VULKAN_OK || memcmp(actual,pattern,sizeof(pattern)))goto done;report->initial_upload=1;
    report->stage=4;if(cancelled(report,cancel,context))goto done;
    uint8_t modified[16];memset(modified,0x35,sizeof(modified));
    if(!satchel_cpu_probe_write(cpu,STORAGE+16,modified,sizeof(modified)))goto done;
    report->backend_status=satchel_vulkan_buffer_read(owner.device,binding.buffer,0,actual,sizeof(actual));
    if(report->backend_status!=SATCHEL_VULKAN_OK || memcmp(actual,pattern,sizeof(pattern)))goto done;
    if(!operation(cpu,report,SATCHEL_GUEST_POOL_FLUSH,POOL,16,sizeof(modified),NULL))goto done;memcpy(pattern+16,modified,sizeof(modified));
    report->backend_status=satchel_vulkan_buffer_read(owner.device,binding.buffer,0,actual,sizeof(actual));
    if(report->backend_status!=SATCHEL_VULKAN_OK || memcmp(actual,pattern,sizeof(pattern)))goto done;report->flush_completed=1;
    report->stage=5;if(cancelled(report,cancel,context))goto done;
    report->backend_status=satchel_vulkan_buffer_fill(owner.device,binding.buffer,32,16,0x12345678,&owner.job,cancel,context);
    if(report->backend_status!=SATCHEL_VULKAN_OK){if(report->backend_status==SATCHEL_VULKAN_CANCELLED)report->status=SATCHEL_GUEST_POOL_DIAG_CANCELLED;goto done;}
    if(!owner.job)goto done;
    report->stage=6;
    SatchelVulkanSubmissionReport completion={0};report->backend_status=satchel_vulkan_submission_wait(owner.device,owner.job,5000,cancel,context,&completion);
    if(report->backend_status!=SATCHEL_VULKAN_OK){if(report->backend_status==SATCHEL_VULKAN_CANCELLED)report->status=SATCHEL_GUEST_POOL_DIAG_CANCELLED;else if(report->backend_status==SATCHEL_VULKAN_TIMEOUT || report->backend_status==SATCHEL_VULKAN_BUSY)report->status=SATCHEL_GUEST_POOL_DIAG_BUSY;goto done;}
    owner.job=0;if(!completion.completed || completion.resources_retained)goto done;report->gpu_completed=1;
    report->stage=7;if(cancelled(report,cancel,context))goto done;
    if(!satchel_cpu_probe_read(cpu,STORAGE,actual,sizeof(actual)) || memcmp(actual,pattern,sizeof(pattern)))goto done;
    if(!operation(cpu,report,SATCHEL_GUEST_POOL_INVALIDATE,POOL,32,16,NULL))goto done;report->invalidate_completed=1;
    for(unsigned i=32;i<48;i+=4)put32(pattern+i,0x12345678);
    if(!satchel_cpu_probe_read(cpu,STORAGE,actual,sizeof(actual)))goto done;
    report->values_matched=!memcmp(actual+32,pattern+32,16);report->guards_matched=!memcmp(actual,pattern,32) && !memcmp(actual+48,pattern+48,16);
    uint8_t guards[2];if(!satchel_cpu_probe_read(cpu,POOL-1,guards,1) || !satchel_cpu_probe_read(cpu,POOL+256,guards+1,1))goto done;
    report->guards_matched=report->guards_matched && guards[0]==0x7a && guards[1]==0x7a;
    report->state_preserved=satchel_cpu_probe_get_x(cpu,19,&value) && value==UINT64_C(0x23456789abcdef01) && satchel_cpu_probe_get_q(cpu,8,read_vector) && !memcmp(vector,read_vector,sizeof(vector));
    if(!report->values_matched || !report->guards_matched || !report->state_preserved)goto done;
    report->stage=8;if(cancelled(report,cancel,context))goto done;
    if(!operation(cpu,report,SATCHEL_GUEST_POOL_FINALIZE,POOL,0,0,NULL))goto done;report->pool_finalized=1;
    uint8_t object[256];if(!satchel_cpu_probe_read(cpu,POOL,object,sizeof(object)))goto done;for(unsigned i=0;i<sizeof(object);i++)if(object[i])goto done;
    if(!satchel_cpu_probe_read(cpu,POOL-1,guards,1) || !satchel_cpu_probe_read(cpu,POOL+256,guards+1,1) || guards[0]!=0x7a || guards[1]!=0x7a)goto done;
    report->stage=9;if(cancelled(report,cancel,context))goto done;report->status=SATCHEL_GUEST_POOL_DIAG_OK;
 done:
    free(data);free(storage);
    /* No CPU has executed or escaped this invocation, so it cannot be active. */
    if(cpu)satchel_cpu_probe_destroy(cpu);
    if(!owner.job && !cleanup(report))report->status=SATCHEL_GUEST_POOL_DIAG_FAILED;
    report->resources_retained=owner.device || owner.pools || owner.job;
    if(!snapshot(report,1))report->status=SATCHEL_GUEST_POOL_DIAG_FAILED;
    report->cleanup_matched=!report->resources_retained && report->baseline_devices==report->final_devices && report->baseline_buffers==report->final_buffers && report->baseline_jobs==report->final_jobs && report->baseline_bytes==report->final_bytes;
    if(report->status==SATCHEL_GUEST_POOL_DIAG_OK){if(!report->cleanup_matched)report->status=SATCHEL_GUEST_POOL_DIAG_FAILED;else report->stage=10;}
    const int success=report->status==SATCHEL_GUEST_POOL_DIAG_OK;atomic_flag_clear(&active);return success;
}
