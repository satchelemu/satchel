#include "GuestPoolDispatchDiagnostic.h"
#include "GuestDispatch.h"
#include "VulkanDevice.h"
#include "VulkanBuffer.h"
#include "VulkanSubmission.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static unsigned checks;
static void check_at(int value,unsigned line){++checks;if(!value){fprintf(stderr,"interpreted pool diagnostic check%u failed at line%u\n",checks,line);exit(1);}}
#define check(v) check_at((v),__LINE__)
typedef struct {SatchelGuestPoolDispatchDiagnostic *report;unsigned stage,reenter;} Cancel;
static int cancel(void *context){Cancel *c=context;
    if(c->reenter){c->reenter=0;SatchelGuestPoolDispatchDiagnostic nested={0};check(!satchel_guest_pool_dispatch_diagnostic(&nested,NULL,NULL) && nested.memory.status==SATCHEL_GUEST_POOL_DIAG_BUSY && nested.memory.resources_retained);}
    return c->stage && c->report->memory.stage==c->stage;
}
static void verify(const SatchelGuestPoolDispatchDiagnostic *report){const SatchelGuestPoolDiagnostic *r=&report->memory;
    check(r->status==SATCHEL_GUEST_POOL_DIAG_OK && r->stage==10 && r->device_created && report->device_finalized && r->pool_created && r->pool_finalized);
    check(r->initial_upload && r->flush_completed && r->gpu_completed && r->invalidate_completed && r->values_matched && r->guards_matched && r->state_preserved);
    check(r->cleanup_matched && !r->resources_retained && r->baseline_devices==r->final_devices && r->baseline_buffers==r->final_buffers && r->baseline_jobs==r->final_jobs && r->baseline_bytes==r->final_bytes);
    check(report->instructions==78 && report->dispatch_events==26 && report->dispatch_status==SATCHEL_GUEST_DISPATCH_RETURNED && report->cpu_stop_reason==SATCHEL_CPU_SVC);
}
int main(void){
    check(!satchel_guest_pool_dispatch_diagnostic(NULL,NULL,NULL));SatchelGuestPoolDispatchDiagnostic report={0};
    check(satchel_guest_pool_dispatch_diagnostic(&report,NULL,NULL));verify(&report);
    const unsigned stages[]={1,2,3,4,5,7,8,9};
    for(unsigned i=0;i<sizeof(stages)/sizeof(stages[0]);i++){
        Cancel c={&report,stages[i],0};check(!satchel_guest_pool_dispatch_diagnostic(&report,cancel,&c));
        check(report.memory.status==SATCHEL_GUEST_POOL_DIAG_CANCELLED && report.memory.stage==stages[i] && report.memory.cleanup_matched && !report.memory.resources_retained);
    }
    Cancel c={&report,6,1};check(!satchel_guest_pool_dispatch_diagnostic(&report,cancel,&c));
    check(report.memory.status==SATCHEL_GUEST_POOL_DIAG_CANCELLED && report.memory.resources_retained && !report.memory.cleanup_matched && !report.memory.gpu_completed);
    SatchelGuestRetirementReport retired={0};check(satchel_guest_dispatch_retirement_stats(&retired) && retired.retiring_contexts==1 && retired.pending_devices==1);
    check(satchel_vulkan_submission_live_count()==1 && satchel_vulkan_device_live_count()==1);
    c.stage=1;unsigned reaped=0;struct timespec pause={0,1000000};
    for(unsigned i=0;i<5000;i++){
        check(!satchel_guest_pool_dispatch_diagnostic(&report,cancel,&c));
        if(report.memory.status==SATCHEL_GUEST_POOL_DIAG_CANCELLED){check(report.memory.prior_reaped && report.memory.cleanup_matched && !report.memory.resources_retained);reaped=1;break;}
        check(report.memory.status==SATCHEL_GUEST_POOL_DIAG_BUSY && report.memory.resources_retained);nanosleep(&pause,NULL);
    }
    check(reaped && !satchel_vulkan_device_live_count() && !satchel_vulkan_submission_live_count());check(satchel_guest_dispatch_retirement_stats(&retired) && !retired.reserved_contexts);
    uint64_t device=0,buffer=0,job=0;SatchelGuestDeviceCaps caps={0};SatchelVulkanBufferInfo info={0};SatchelVulkanSubmissionReport done={0};
    check(satchel_vulkan_device_create(0,&device,&caps)==SATCHEL_VULKAN_OK);check(satchel_vulkan_buffer_create(device,64,7,&buffer,&info)==SATCHEL_VULKAN_OK);
    check(satchel_vulkan_buffer_fill(device,buffer,0,64,0xaabbccdd,&job,NULL,NULL)==SATCHEL_VULKAN_OK && job);
    check(satchel_guest_pool_dispatch_diagnostic(&report,NULL,NULL));verify(&report);check(report.memory.baseline_devices==1 && report.memory.baseline_buffers==1 && report.memory.baseline_jobs==1);
    check(satchel_vulkan_submission_wait(device,job,5000,NULL,NULL,&done)==SATCHEL_VULKAN_OK && done.completed);check(satchel_vulkan_device_destroy(device)==SATCHEL_VULKAN_OK);
    // Admission failures clean their own reserved dispatcher without consuming
    // other callers' devices, buffers or the collector's active owner slots.
    check(satchel_vulkan_device_create(0,&device,&caps)==SATCHEL_VULKAN_OK);uint64_t buffers[SATCHEL_VULKAN_BUFFER_LIMIT];
    for(unsigned i=0;i<SATCHEL_VULKAN_BUFFER_LIMIT;i++)check(satchel_vulkan_buffer_create(device,64,7,&buffers[i],&info)==SATCHEL_VULKAN_OK);
    check(!satchel_guest_pool_dispatch_diagnostic(&report,NULL,NULL) && report.memory.status==SATCHEL_GUEST_POOL_DIAG_FAILED && report.dispatch_status!=SATCHEL_GUEST_DISPATCH_RETURNED && report.memory.cleanup_matched && !report.memory.resources_retained);
    check(satchel_vulkan_device_destroy(device)==SATCHEL_VULKAN_OK);uint64_t devices[8];for(unsigned i=0;i<8;i++)check(satchel_vulkan_device_create(0,&devices[i],&caps)==SATCHEL_VULKAN_OK);
    check(!satchel_guest_pool_dispatch_diagnostic(&report,NULL,NULL) && report.memory.status==SATCHEL_GUEST_POOL_DIAG_FAILED && report.memory.cleanup_matched && !report.memory.resources_retained);
    for(unsigned i=0;i<8;i++)check(satchel_vulkan_device_destroy(devices[i])==SATCHEL_VULKAN_OK);
    SatchelGuestDispatch *owners[SATCHEL_GUEST_RETIREMENT_LIMIT];SatchelGuestDeviceBackendV2 backend={0};check(satchel_vulkan_guest_device_backend_v2(&backend));
    for(unsigned i=0;i<SATCHEL_GUEST_RETIREMENT_LIMIT;i++){owners[i]=satchel_guest_dispatch_create(0x100100,0x200000,1,30000);check(owners[i]!=NULL && satchel_guest_dispatch_set_device_backend_v2(owners[i],&backend));}
    check(!satchel_guest_pool_dispatch_diagnostic(&report,NULL,NULL) && report.memory.status==SATCHEL_GUEST_POOL_DIAG_FAILED && report.memory.cleanup_matched && !report.memory.resources_retained);
    check(satchel_guest_dispatch_retirement_stats(&retired) && retired.active_contexts==SATCHEL_GUEST_RETIREMENT_LIMIT);
    for(unsigned i=0;i<SATCHEL_GUEST_RETIREMENT_LIMIT;i++)check(satchel_guest_dispatch_handoff(&owners[i]));check(satchel_guest_dispatch_retirement_poll(NULL,NULL,&retired)==SATCHEL_GUEST_RETIRE_IDLE);
    SatchelVulkanBufferStats stats={0};check(satchel_vulkan_buffer_stats(&stats)==SATCHEL_VULKAN_OK && !stats.live_buffers && !stats.allocated_bytes && !satchel_vulkan_device_live_count() && !satchel_vulkan_submission_live_count());
    printf("Original interpreted pool phone fixture: %u ASan/UBSan assertions passed;78 instructions,26 dispatch events, real GPU cache result/guards, cancellation handoff/reap and cleanup verified. No guest queue submission or game frame.\n",checks);
}
