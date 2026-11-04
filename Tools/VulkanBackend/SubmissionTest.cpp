#include "VulkanSubmission.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>
static unsigned checks;
static void check(bool value){++checks;if(!value){std::fprintf(stderr,"Vulkan submission check%u failed\n",checks);std::exit(1);}}
static int cancel(void *){return 1;}
static int cancel_at(void *context){auto *poll=static_cast<unsigned *>(context);return --*poll==0;}
static void reap(uint64_t device,uint64_t token){SatchelVulkanSubmissionReport report={};check(satchel_vulkan_submission_wait(device,token,5000,nullptr,nullptr,&report)==SATCHEL_VULKAN_OK && report.completed && !report.resources_retained && report.vk_result==0);}
int main(){
    check(satchel_vulkan_submission_live_count()==0);uint64_t device=0,other=0,source=0,destination=0,storage=0,job=0;SatchelGuestDeviceCaps caps={};SatchelVulkanBufferInfo info={};
    check(satchel_vulkan_device_create(0,&device,&caps)==SATCHEL_VULKAN_OK);check(satchel_vulkan_device_create(0,&other,&caps)==SATCHEL_VULKAN_OK);
    check(satchel_vulkan_buffer_create(device,24,7,&source,&info)==SATCHEL_VULKAN_OK);
    check(satchel_vulkan_buffer_create(device,24,7,&destination,&info)==SATCHEL_VULKAN_OK);
    check(satchel_vulkan_buffer_create(device,24,1,&storage,&info)==SATCHEL_VULKAN_OK);
    uint32_t guards[6];for(auto &word:guards)word=0xdeadbeef;uint32_t read[6]={};
    check(satchel_vulkan_buffer_write(device,source,0,guards,sizeof(guards))==SATCHEL_VULKAN_OK);
    check(satchel_vulkan_buffer_write(device,destination,0,guards,sizeof(guards))==SATCHEL_VULKAN_OK);
    check(satchel_vulkan_buffer_fill(device,source,4,8,0x13579bdf,nullptr,nullptr,nullptr)==SATCHEL_VULKAN_INVALID);
    const uint64_t offsets[]={1,UINT64_MAX-3,24};const uint64_t sizes[]={4,4,4};
    for(unsigned i=0;i<3;i++){job=99;check(satchel_vulkan_buffer_fill(device,source,offsets[i],sizes[i],0,&job,nullptr,nullptr)==SATCHEL_VULKAN_INVALID && !job);}
    check(satchel_vulkan_buffer_fill(device,source,0,0,0,&job,nullptr,nullptr)==SATCHEL_VULKAN_INVALID && !job);
    check(satchel_vulkan_buffer_fill(device,source,0,3,0,&job,nullptr,nullptr)==SATCHEL_VULKAN_INVALID && !job);
    check(satchel_vulkan_buffer_fill(other,source,0,4,0,&job,nullptr,nullptr)==SATCHEL_VULKAN_INVALID && !job);
    check(satchel_vulkan_buffer_fill(device,storage,0,4,0,&job,nullptr,nullptr)==SATCHEL_VULKAN_UNSUPPORTED && !job);
    check(satchel_vulkan_buffer_copy(device,source,0,source,8,4,&job,nullptr,nullptr)==SATCHEL_VULKAN_UNSUPPORTED && !job);
    check(satchel_vulkan_buffer_copy(device,storage,0,destination,0,4,&job,nullptr,nullptr)==SATCHEL_VULKAN_UNSUPPORTED && !job);
    check(satchel_vulkan_buffer_copy(device,source,0,storage,0,4,&job,nullptr,nullptr)==SATCHEL_VULKAN_UNSUPPORTED && !job);
    check(satchel_vulkan_buffer_copy(device,0,0,destination,0,4,&job,nullptr,nullptr)==SATCHEL_VULKAN_INVALID && !job);
    check(satchel_vulkan_buffer_copy(device,source,UINT64_MAX,destination,0,4,&job,nullptr,nullptr)==SATCHEL_VULKAN_INVALID && !job);
    check(satchel_vulkan_buffer_copy(device,source,0,destination,22,4,&job,nullptr,nullptr)==SATCHEL_VULKAN_INVALID && !job);
    for(unsigned poll=1;poll<=2;poll++){
        unsigned remaining=poll;check(satchel_vulkan_buffer_fill(device,source,4,8,0x13579bdf,&job,cancel_at,&remaining)==SATCHEL_VULKAN_CANCELLED && !job);
        check(satchel_vulkan_submission_live_count()==0);
        check(satchel_vulkan_buffer_read(device,source,0,read,sizeof(read))==SATCHEL_VULKAN_OK && !std::memcmp(read,guards,sizeof(read)));
    }
    check(satchel_vulkan_buffer_fill(device,source,4,8,0x13579bdf,&job,nullptr,nullptr)==SATCHEL_VULKAN_OK && (job>>62)==1);
    const uint64_t original=job;check(satchel_vulkan_submission_live_count()==1);
    SatchelVulkanSubmissionReport report={};
    check(satchel_vulkan_submission_wait(device,job,5000,cancel,nullptr,&report)==SATCHEL_VULKAN_CANCELLED && !report.completed && report.resources_retained);
    // Logical ownership remains until an actual completion is explicitly observed,
    // even if this small transfer may already have finished on the hardware.
    check(satchel_vulkan_buffer_read(device,source,0,read,sizeof(read))==SATCHEL_VULKAN_BUSY);
    check(satchel_vulkan_buffer_write(device,source,0,guards,sizeof(guards))==SATCHEL_VULKAN_BUSY);
    check(satchel_vulkan_buffer_destroy(device,source)==SATCHEL_VULKAN_BUSY);
    check(satchel_vulkan_device_destroy(device)==SATCHEL_VULKAN_BUSY);
    check(satchel_vulkan_buffer_fill(device,destination,0,4,0,&job,nullptr,nullptr)==SATCHEL_VULKAN_BUSY && !job);
    check(satchel_vulkan_submission_wait(other,original,5000,nullptr,nullptr,&report)==SATCHEL_VULKAN_INVALID);
    check(satchel_vulkan_submission_wait(device,original,5001,nullptr,nullptr,&report)==SATCHEL_VULKAN_INVALID);
    check(satchel_vulkan_submission_wait(device,original,0,nullptr,nullptr,nullptr)==SATCHEL_VULKAN_INVALID);
    reap(device,original);check(satchel_vulkan_submission_live_count()==0);
    check(satchel_vulkan_submission_wait(device,original,0,nullptr,nullptr,&report)==SATCHEL_VULKAN_INVALID);
    check(satchel_vulkan_buffer_read(device,source,0,read,sizeof(read))==SATCHEL_VULKAN_OK);
    check(read[0]==0xdeadbeef && read[1]==0x13579bdf && read[2]==0x13579bdf && read[3]==0xdeadbeef && read[4]==0xdeadbeef && read[5]==0xdeadbeef);
    check(satchel_vulkan_buffer_copy(device,source,4,destination,8,8,&job,nullptr,nullptr)==SATCHEL_VULKAN_OK && job>original);
    check(satchel_vulkan_buffer_read(device,source,0,read,sizeof(read))==SATCHEL_VULKAN_BUSY);
    check(satchel_vulkan_buffer_destroy(device,destination)==SATCHEL_VULKAN_BUSY);
    // A zero-time poll may legitimately observe immediate GPU success.
    const auto polled=satchel_vulkan_submission_wait(device,job,0,nullptr,nullptr,&report);
    check(polled==SATCHEL_VULKAN_OK || polled==SATCHEL_VULKAN_TIMEOUT);
    if(polled==SATCHEL_VULKAN_TIMEOUT){check(!report.completed && report.resources_retained);reap(device,job);}else check(report.completed && !report.resources_retained);
    check(satchel_vulkan_buffer_read(device,destination,0,read,sizeof(read))==SATCHEL_VULKAN_OK);
    check(read[0]==0xdeadbeef && read[1]==0xdeadbeef && read[2]==0x13579bdf && read[3]==0x13579bdf && read[4]==0xdeadbeef && read[5]==0xdeadbeef);
    uint8_t expected[24],actual[24],bytes[24];for(unsigned i=0;i<24;i++)bytes[i]=static_cast<uint8_t>(i*9+7);
    check(satchel_vulkan_buffer_write(device,source,0,bytes,sizeof(bytes))==SATCHEL_VULKAN_OK);
    check(satchel_vulkan_buffer_read(device,destination,0,expected,sizeof(expected))==SATCHEL_VULKAN_OK);std::memcpy(expected+5,bytes+3,9);
    check(satchel_vulkan_buffer_copy(device,source,3,destination,5,9,&job,nullptr,nullptr)==SATCHEL_VULKAN_OK);reap(device,job);
    check(satchel_vulkan_buffer_read(device,destination,0,actual,sizeof(actual))==SATCHEL_VULKAN_OK && !std::memcmp(expected,actual,sizeof(actual)));
    check(satchel_vulkan_device_destroy(device)==SATCHEL_VULKAN_OK);check(satchel_vulkan_device_destroy(other)==SATCHEL_VULKAN_OK);
    // Four globally retained jobs, one/device. No implicit polling hides the
    // bound: the fifth is refused until a caller observes/reaps a real fence.
    uint64_t devices[5]={},buffers[5]={},jobs[5]={};
    for(unsigned i=0;i<5;i++){
        check(satchel_vulkan_device_create(0,&devices[i],&caps)==SATCHEL_VULKAN_OK);
        check(satchel_vulkan_buffer_create(devices[i],16,7,&buffers[i],&info)==SATCHEL_VULKAN_OK);
        check(satchel_vulkan_buffer_fill(devices[i],buffers[i],0,16,0x2468ace0,&jobs[i],nullptr,nullptr)==(i<4?SATCHEL_VULKAN_OK:SATCHEL_VULKAN_BUSY));
    }
    check(satchel_vulkan_submission_live_count()==4 && !jobs[4]);
    SatchelVulkanTransferReport blocked={};check(satchel_vulkan_transfer_diagnostic(&blocked,nullptr,nullptr)==SATCHEL_VULKAN_BUSY && blocked.cleanup_matched && !blocked.resources_retained);
    check(blocked.baseline_jobs==4 && blocked.final_jobs==4 && blocked.baseline_devices==5 && blocked.final_devices==5);

    for(unsigned i=0;i<4;i++){
        check(satchel_vulkan_device_destroy(devices[i])==SATCHEL_VULKAN_BUSY);reap(devices[i],jobs[i]);
        check(satchel_vulkan_buffer_read(devices[i],buffers[i],0,read,16)==SATCHEL_VULKAN_OK);
        for(unsigned j=0;j<4;j++)check(read[j]==0x2468ace0);
        check(satchel_vulkan_device_destroy(devices[i])==SATCHEL_VULKAN_OK);
    }
    check(satchel_vulkan_device_destroy(devices[4])==SATCHEL_VULKAN_OK);
    SatchelVulkanBufferStats stats={};check(satchel_vulkan_buffer_stats(&stats)==SATCHEL_VULKAN_OK && !stats.live_buffers && !stats.allocated_bytes);
    check(!satchel_vulkan_submission_live_count() && !satchel_vulkan_device_live_count());
    check(satchel_vulkan_transfer_diagnostic(nullptr,nullptr,nullptr)==SATCHEL_VULKAN_INVALID);
    SatchelVulkanTransferReport transfer={};
    for(unsigned poll=1;poll<=4;poll++){
        unsigned remaining=poll;check(satchel_vulkan_transfer_diagnostic(&transfer,cancel_at,&remaining)==SATCHEL_VULKAN_CANCELLED);
        check(transfer.cleanup_matched && !transfer.resources_retained && !satchel_vulkan_submission_live_count() && !satchel_vulkan_device_live_count());
    }
    unsigned remaining=5;check(satchel_vulkan_transfer_diagnostic(&transfer,cancel_at,&remaining)==SATCHEL_VULKAN_CANCELLED);
    check(transfer.resources_retained && !transfer.cleanup_matched && !transfer.fill_completed && satchel_vulkan_submission_live_count()==1 && satchel_vulkan_device_live_count()==1);
    // A later invocation owns/reaps the exact retained fence; it may return BUSY
    // until real completion. Cancellation after reap avoids creating more work.
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    SatchelVulkanStatus recovered=SATCHEL_VULKAN_BUSY;
    while(recovered==SATCHEL_VULKAN_BUSY && std::chrono::steady_clock::now()<deadline){
        recovered=satchel_vulkan_transfer_diagnostic(&transfer,cancel,nullptr);
        if(recovered==SATCHEL_VULKAN_BUSY){check(transfer.resources_retained);std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    }
    check(recovered==SATCHEL_VULKAN_CANCELLED && transfer.prior_reaped && transfer.cleanup_matched && !transfer.resources_retained);
    check(!satchel_vulkan_submission_live_count() && !satchel_vulkan_device_live_count());
    check(satchel_vulkan_transfer_diagnostic(&transfer,nullptr,nullptr)==SATCHEL_VULKAN_OK && transfer.status==SATCHEL_VULKAN_OK && transfer.stage==9);
    check(transfer.fill_completed && transfer.copy_completed && transfer.values_matched && transfer.guards_matched && transfer.cleanup_matched && !transfer.resources_retained);
    check(!transfer.final_jobs && !transfer.final_buffers && !transfer.final_devices && !transfer.final_bytes);
    check(satchel_vulkan_device_create(0,&device,&caps)==SATCHEL_VULKAN_OK);
    check(satchel_vulkan_buffer_create(device,24,7,&source,&info)==SATCHEL_VULKAN_OK);
    check(satchel_vulkan_buffer_write(device,source,0,guards,sizeof(guards))==SATCHEL_VULKAN_OK);
    check(satchel_vulkan_transfer_diagnostic(&transfer,nullptr,nullptr)==SATCHEL_VULKAN_OK && transfer.cleanup_matched && transfer.baseline_devices==1 && transfer.final_devices==1 && transfer.baseline_buffers==1 && transfer.final_buffers==1);
    check(satchel_vulkan_buffer_read(device,source,0,read,sizeof(read))==SATCHEL_VULKAN_OK && !std::memcmp(read,guards,sizeof(read)));
    check(satchel_vulkan_device_destroy(device)==SATCHEL_VULKAN_OK && !satchel_vulkan_device_live_count());
    std::printf("Real Vulkan fill/copy submissions: %u ASan/UBSan assertions passed; GPU values/guards, fences, retained cancellation, locks, bounded jobs and cleanup verified.\n",checks);
}
