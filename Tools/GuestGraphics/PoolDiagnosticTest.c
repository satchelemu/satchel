#include "GuestPoolDiagnostic.h"
#include "VulkanDevice.h"
#include "VulkanBuffer.h"
#include "VulkanSubmission.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static unsigned checks;
static void check(int value){++checks;if(!value){fprintf(stderr,"pool diagnostic check%u failed\n",checks);exit(1);}}
typedef struct {SatchelGuestPoolDiagnostic *report;unsigned stage,reenter;} Cancel;
static int cancel(void *context){Cancel *config=context;
    if(config->reenter){config->reenter=0;SatchelGuestPoolDiagnostic nested={0};check(!satchel_guest_pool_diagnostic(&nested,NULL,NULL) && nested.status==SATCHEL_GUEST_POOL_DIAG_BUSY && nested.resources_retained);}
    return config->stage && config->report->stage==config->stage;
}
static void verify(const SatchelGuestPoolDiagnostic *report){
    check(report->status==SATCHEL_GUEST_POOL_DIAG_OK && report->stage==10 && report->device_created && report->pool_created);
    check(report->initial_upload && report->flush_completed && report->gpu_completed && report->invalidate_completed);
    check(report->values_matched && report->guards_matched && report->state_preserved && report->pool_finalized && report->cleanup_matched && !report->resources_retained);
    check(report->baseline_bytes==report->final_bytes && report->baseline_buffers==report->final_buffers && report->baseline_jobs==report->final_jobs && report->baseline_devices==report->final_devices);
}
int main(void){
    check(!satchel_guest_pool_diagnostic(NULL,NULL,NULL));SatchelGuestPoolDiagnostic report={0};
    check(satchel_guest_pool_diagnostic(&report,NULL,NULL));verify(&report);check(!satchel_vulkan_device_live_count() && !satchel_vulkan_submission_live_count());
    const unsigned stages[]={1,2,3,4,5,7,8,9};
    for(unsigned i=0;i<sizeof(stages)/sizeof(stages[0]);i++){
        Cancel config={&report,stages[i],0};check(!satchel_guest_pool_diagnostic(&report,cancel,&config));
        check(report.status==SATCHEL_GUEST_POOL_DIAG_CANCELLED && report.stage==stages[i] && report.cleanup_matched && !report.resources_retained);
        check(!satchel_vulkan_device_live_count() && !satchel_vulkan_submission_live_count());
    }
    Cancel config={&report,6,1};check(!satchel_guest_pool_diagnostic(&report,cancel,&config));
    check(report.status==SATCHEL_GUEST_POOL_DIAG_CANCELLED && report.stage==6 && report.resources_retained && !report.cleanup_matched && !report.gpu_completed);
    check(satchel_vulkan_device_live_count()==1 && satchel_vulkan_submission_live_count()==1);SatchelVulkanBufferStats stats={0};check(satchel_vulkan_buffer_stats(&stats)==SATCHEL_VULKAN_OK && stats.live_buffers==1);
    // Next invocation owns reaping; caller never receives private tokens. It
    // cancels after cleanup so this check does not manufacture another job.
    config.stage=1;unsigned reaped=0;struct timespec pause={0,1000000};
    for(unsigned i=0;i<5000;i++){
        check(!satchel_guest_pool_diagnostic(&report,cancel,&config));
        if(report.status==SATCHEL_GUEST_POOL_DIAG_CANCELLED){check(report.prior_reaped && report.cleanup_matched && !report.resources_retained);reaped=1;break;}
        check(report.status==SATCHEL_GUEST_POOL_DIAG_BUSY && report.resources_retained);nanosleep(&pause,NULL);
    }
    check(reaped && !satchel_vulkan_device_live_count() && !satchel_vulkan_submission_live_count());
    // Unrelated parent/buffer/job ownership survives the fixed fixture.
    uint64_t device=0,buffer=0,job=0;SatchelGuestDeviceCaps caps={0};SatchelVulkanBufferInfo info={0};SatchelVulkanSubmissionReport completion={0};
    check(satchel_vulkan_device_create(0,&device,&caps)==SATCHEL_VULKAN_OK);check(satchel_vulkan_buffer_create(device,64,7,&buffer,&info)==SATCHEL_VULKAN_OK);
    check(satchel_vulkan_buffer_fill(device,buffer,0,64,0xaabbccdd,&job,NULL,NULL)==SATCHEL_VULKAN_OK && job);
    check(satchel_guest_pool_diagnostic(&report,NULL,NULL));verify(&report);check(report.baseline_devices==1 && report.baseline_buffers==1 && report.baseline_jobs==1);
    check(satchel_vulkan_submission_wait(device,job,5000,NULL,NULL,&completion)==SATCHEL_VULKAN_OK && completion.completed);check(satchel_vulkan_device_destroy(device)==SATCHEL_VULKAN_OK);
    // Buffer admission failure keeps its failure code through successful owner
    // cleanup and preserves every unrelated allocation.
    check(satchel_vulkan_device_create(0,&device,&caps)==SATCHEL_VULKAN_OK);
    uint64_t buffers[SATCHEL_VULKAN_BUFFER_LIMIT];for(unsigned i=0;i<SATCHEL_VULKAN_BUFFER_LIMIT;i++)check(satchel_vulkan_buffer_create(device,64,7,&buffers[i],&info)==SATCHEL_VULKAN_OK);
    check(!satchel_guest_pool_diagnostic(&report,NULL,NULL) && report.status==SATCHEL_GUEST_POOL_DIAG_FAILED && report.pool_status!=0 && report.cleanup_matched && !report.resources_retained);
    check(satchel_vulkan_buffer_stats(&stats)==SATCHEL_VULKAN_OK && stats.live_buffers==SATCHEL_VULKAN_BUFFER_LIMIT);check(satchel_vulkan_device_destroy(device)==SATCHEL_VULKAN_OK);
    // Full device registry fails admission without touching caller resources.
    uint64_t devices[8];for(unsigned i=0;i<8;i++)check(satchel_vulkan_device_create(0,&devices[i],&caps)==SATCHEL_VULKAN_OK);
    check(!satchel_guest_pool_diagnostic(&report,NULL,NULL) && report.status==SATCHEL_GUEST_POOL_DIAG_FAILED && report.cleanup_matched && !report.resources_retained);
    check(satchel_vulkan_device_live_count()==8);for(unsigned i=0;i<8;i++)check(satchel_vulkan_device_destroy(devices[i])==SATCHEL_VULKAN_OK);
    check(satchel_vulkan_buffer_stats(&stats)==SATCHEL_VULKAN_OK && !stats.live_buffers && !stats.allocated_bytes && !satchel_vulkan_device_live_count() && !satchel_vulkan_submission_live_count());
    printf("Original checked-memory pool diagnostic: %u ASan/UBSan assertions passed; GPU fill/fence, guarded cache copies, cancellation retention/reap, reentry and resource independence verified. No interpreted NVN pool call or game frame.\n",checks);
}
