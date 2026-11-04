#include "VulkanBuffer.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
static unsigned checks;
static void check(bool value){++checks;if(!value){std::fprintf(stderr,"Vulkan buffer check%u failed\n",checks);std::exit(1);}}
static SatchelVulkanBufferStats stats(){SatchelVulkanBufferStats value={};check(satchel_vulkan_buffer_stats(&value)==SATCHEL_VULKAN_OK);return value;}
static int cancel_at(void *context){auto *poll=static_cast<unsigned *>(context);return --*poll==0;}
static int reenter(void *){SatchelVulkanBufferStats value={};check(satchel_vulkan_buffer_stats(&value)==SATCHEL_VULKAN_BUSY);return 1;}
int main(){
    check(stats().live_buffers==0 && stats().allocated_bytes==0);
    check(satchel_vulkan_buffer_stats(nullptr)==SATCHEL_VULKAN_INVALID);
    SatchelGuestDeviceCaps caps={};uint64_t device=0,other=0,token=0;
    check(satchel_vulkan_device_create(0,&device,&caps)==SATCHEL_VULKAN_OK);
    check(satchel_vulkan_device_create(0,&other,&caps)==SATCHEL_VULKAN_OK);
    SatchelVulkanBufferInfo info={};
    check(satchel_vulkan_buffer_create(device,1,1,nullptr,&info)==SATCHEL_VULKAN_INVALID);
    check(satchel_vulkan_buffer_create(device,1,1,&token,nullptr)==SATCHEL_VULKAN_INVALID);
    const uint64_t sizes[]={0,SATCHEL_VULKAN_BUFFER_MAX_SIZE+1,UINT64_MAX};
    for(auto size:sizes){token=99;check(satchel_vulkan_buffer_create(device,size,1,&token,&info)==SATCHEL_VULKAN_INVALID && token==0 && !info.size);}
    check(satchel_vulkan_buffer_create(device,1,0,&token,&info)==SATCHEL_VULKAN_INVALID && token==0);
    for(unsigned bit=5;bit<32;bit++)check(satchel_vulkan_buffer_create(device,1,1u<<bit,&token,&info)==SATCHEL_VULKAN_UNSUPPORTED && token==0);
    check(satchel_vulkan_buffer_create(0,1,1,&token,&info)==SATCHEL_VULKAN_INVALID);
    for(uint32_t usage=1;usage<32;usage++){
        check(satchel_vulkan_buffer_create(device,13,usage,&token,&info)==SATCHEL_VULKAN_OK && (token>>63)==1);
        check(info.device==device && info.size==13 && info.allocation_size>=13 && info.usage==usage && info.coherent_memory<=1);
        check(stats().allocated_bytes==info.allocation_size && stats().live_buffers==1);
        uint8_t bytes[13];std::memset(bytes,0xa5,sizeof(bytes));check(satchel_vulkan_buffer_read(device,token,0,bytes,sizeof(bytes))==SATCHEL_VULKAN_OK);
        for(auto byte:bytes)check(byte==0);
        check(satchel_vulkan_buffer_destroy(device,token)==SATCHEL_VULKAN_OK && stats().allocated_bytes==0);
    }
    check(satchel_vulkan_buffer_create(device,257,7,&token,&info)==SATCHEL_VULKAN_OK);
    SatchelVulkanBufferInfo copied={};check(satchel_vulkan_buffer_info(device,token,&copied)==SATCHEL_VULKAN_OK && copied.allocation_size==info.allocation_size && copied.usage==7);
    check(satchel_vulkan_buffer_info(other,token,&copied)==SATCHEL_VULKAN_INVALID && !copied.size);
    check(satchel_vulkan_buffer_info(device,token,nullptr)==SATCHEL_VULKAN_INVALID);
    uint8_t input[512],output[512],expected[257]={};for(unsigned i=0;i<512;i++)input[i]=static_cast<uint8_t>(i*13+5);
    check(satchel_vulkan_buffer_write(device,token,1,input,255)==SATCHEL_VULKAN_OK);std::memcpy(expected+1,input,255);
    check(satchel_vulkan_buffer_read(device,token,0,output,257)==SATCHEL_VULKAN_OK && !std::memcmp(expected,output,257));
    check(satchel_vulkan_buffer_write(other,token,0,input,1)==SATCHEL_VULKAN_INVALID);
    check(satchel_vulkan_buffer_read(other,token,0,output,1)==SATCHEL_VULKAN_INVALID);
    check(satchel_vulkan_buffer_destroy(other,token)==SATCHEL_VULKAN_INVALID && stats().live_buffers==1);
    check(satchel_vulkan_buffer_write(device,device,0,input,1)==SATCHEL_VULKAN_INVALID);
    check(satchel_vulkan_buffer_write(device,token,0,nullptr,1)==SATCHEL_VULKAN_INVALID);
    check(satchel_vulkan_buffer_read(device,token,0,nullptr,1)==SATCHEL_VULKAN_INVALID);
    check(satchel_vulkan_buffer_write(device,token,257,nullptr,0)==SATCHEL_VULKAN_OK);
    check(satchel_vulkan_buffer_read(device,token,257,nullptr,0)==SATCHEL_VULKAN_OK);
    check(satchel_vulkan_buffer_read(device,token,258,nullptr,0)==SATCHEL_VULKAN_INVALID);
    const uint64_t offsets[]={258,UINT64_MAX,UINT64_MAX-3};
    for(auto offset:offsets){
        std::memset(output,0xa5,sizeof(output));check(satchel_vulkan_buffer_write(device,token,offset,input,4)==SATCHEL_VULKAN_INVALID);
        check(satchel_vulkan_buffer_read(device,token,offset,output,4)==SATCHEL_VULKAN_INVALID);
        check(output[0]==0xa5 && output[3]==0xa5);
    }
    check(satchel_vulkan_buffer_write(device,token,0,input,SIZE_MAX)==SATCHEL_VULKAN_INVALID);
    check(satchel_vulkan_buffer_read(device,token,0,output,SIZE_MAX)==SATCHEL_VULKAN_INVALID);
    check(satchel_vulkan_buffer_read(device,token,0,output,257)==SATCHEL_VULKAN_OK && !std::memcmp(expected,output,257));
    // Original deterministic range matrix, with guards around the host output.
    uint32_t random=0x12345678;
    for(unsigned i=0;i<512;i++){
        random=random*1664525u+1013904223u;uint64_t offset=random%300;
        random=random*1664525u+1013904223u;size_t count=random%300;
        const bool valid=offset<=257 && count<=257-offset;
        check(satchel_vulkan_buffer_write(device,token,offset,input,count)==(valid?SATCHEL_VULKAN_OK:SATCHEL_VULKAN_INVALID));
        if(valid && count)std::memcpy(expected+offset,input,count);
        std::memset(output,0xa5,sizeof(output));check(satchel_vulkan_buffer_read(device,token,offset,output+1,count)==(valid?SATCHEL_VULKAN_OK:SATCHEL_VULKAN_INVALID));
        check(output[0]==0xa5 && output[count+1]==0xa5);
        if(valid)check(!std::memcmp(output+1,expected+offset,count));else for(size_t j=1;j<=count;j++)check(output[j]==0xa5);
    }
    check(satchel_vulkan_buffer_read(device,token,0,output,257)==SATCHEL_VULKAN_OK && !std::memcmp(expected,output,257));
    SatchelVulkanReport report={};check(satchel_vulkan_compute_diagnostic(&report,reenter,nullptr)==SATCHEL_VULKAN_CANCELLED);
    const uint64_t stale=token;check(satchel_vulkan_buffer_destroy(device,token)==SATCHEL_VULKAN_OK);
    check(satchel_vulkan_buffer_destroy(device,stale)==SATCHEL_VULKAN_INVALID);
    check(satchel_vulkan_buffer_create(device,1,1,&token,&info)==SATCHEL_VULKAN_OK && token>stale);
    check(satchel_vulkan_buffer_info(device,stale,&copied)==SATCHEL_VULKAN_INVALID);
    check(satchel_vulkan_buffer_destroy(device,token)==SATCHEL_VULKAN_OK);
    // Count exhaustion and exact allocation accounting, across both parents.
    uint64_t handles[SATCHEL_VULKAN_BUFFER_LIMIT]={},sum=0;
    for(unsigned i=0;i<SATCHEL_VULKAN_BUFFER_LIMIT;i++){
        check(satchel_vulkan_buffer_create(i%2?other:device,3,1,&handles[i],&info)==SATCHEL_VULKAN_OK);sum+=info.allocation_size;
    }
    check(stats().live_buffers==SATCHEL_VULKAN_BUFFER_LIMIT && stats().allocated_bytes==sum);
    check(satchel_vulkan_buffer_create(device,1,1,&token,&info)==SATCHEL_VULKAN_BUSY && token==0);
    check(stats().live_buffers==SATCHEL_VULKAN_BUFFER_LIMIT && stats().allocated_bytes==sum);
    SatchelVulkanBufferReport full_report={};check(satchel_vulkan_buffer_diagnostic(&full_report,nullptr,nullptr)==SATCHEL_VULKAN_BUSY);
    check(full_report.cleanup_matched && full_report.baseline_buffers==SATCHEL_VULKAN_BUFFER_LIMIT && full_report.final_buffers==SATCHEL_VULKAN_BUFFER_LIMIT && satchel_vulkan_device_live_count()==2);
    check(satchel_vulkan_device_destroy(other)==SATCHEL_VULKAN_OK && stats().live_buffers==SATCHEL_VULKAN_BUFFER_LIMIT/2);
    for(unsigned i=0;i<SATCHEL_VULKAN_BUFFER_LIMIT;i++)check(satchel_vulkan_buffer_destroy(i%2?other:device,handles[i])==(i%2?SATCHEL_VULKAN_INVALID:SATCHEL_VULKAN_OK));
    check(stats().allocated_bytes==0);
    // Allocation budget includes implementation padding; require a real BUSY
    // boundary, preserve accounting on rejection, and reclaim on parent teardown.
    sum=0;unsigned allocated=0;
    while(allocated<SATCHEL_VULKAN_BUFFER_LIMIT){
        const auto result=satchel_vulkan_buffer_create(device,SATCHEL_VULKAN_BUFFER_MAX_SIZE,7,&token,&info);
        if(result==SATCHEL_VULKAN_BUSY){check(token==0 && !info.size);break;}
        check(result==SATCHEL_VULKAN_OK);sum+=info.allocation_size;allocated++;
        check(sum<=SATCHEL_VULKAN_BUFFER_MEMORY_LIMIT && stats().allocated_bytes==sum);
        if(allocated==1){
            std::vector<uint8_t> large(SATCHEL_VULKAN_BUFFER_MAX_SIZE,0x6d),returned(large.size());
            check(satchel_vulkan_buffer_write(device,token,0,large.data(),large.size())==SATCHEL_VULKAN_OK);
            check(satchel_vulkan_buffer_read(device,token,0,returned.data(),returned.size())==SATCHEL_VULKAN_OK && returned==large);
        }
    }
    check(allocated>0 && allocated<SATCHEL_VULKAN_BUFFER_LIMIT && stats().live_buffers==allocated);
    check(stats().allocated_bytes==sum && sum<=SATCHEL_VULKAN_BUFFER_MEMORY_LIMIT);
    check(satchel_vulkan_device_destroy(device)==SATCHEL_VULKAN_OK && stats().live_buffers==0 && stats().allocated_bytes==0);
    check(satchel_vulkan_buffer_read(device,token,0,output,1)==SATCHEL_VULKAN_INVALID);
    check(satchel_vulkan_device_live_count()==0);
    check(satchel_vulkan_buffer_diagnostic(nullptr,nullptr,nullptr)==SATCHEL_VULKAN_INVALID);
    for(unsigned poll=1;poll<=7;poll++){
        unsigned remaining=poll;SatchelVulkanBufferReport report={};
        check(satchel_vulkan_buffer_diagnostic(&report,cancel_at,&remaining)==SATCHEL_VULKAN_CANCELLED && report.status==SATCHEL_VULKAN_CANCELLED);
        check(report.cleanup_matched && !report.final_buffers && !report.final_bytes && satchel_vulkan_device_live_count()==0);
    }
    SatchelVulkanBufferReport buffer_report={};
    check(satchel_vulkan_buffer_diagnostic(&buffer_report,nullptr,nullptr)==SATCHEL_VULKAN_OK && buffer_report.status==SATCHEL_VULKAN_OK && buffer_report.stage==9);
    check(buffer_report.created_buffers==2 && buffer_report.zero_initialized && buffer_report.write_completed && buffer_report.read_completed && buffer_report.guards_matched);
    check(buffer_report.bounds_rejected && buffer_report.explicit_destroy && buffer_report.parent_cleanup && buffer_report.cleanup_matched && buffer_report.allocated_bytes>=96);
    check(!buffer_report.final_buffers && !buffer_report.final_bytes && satchel_vulkan_device_live_count()==0);
    // Existing resources belong to another caller and survive the fixture.
    check(satchel_vulkan_device_create(0,&device,&caps)==SATCHEL_VULKAN_OK);
    check(satchel_vulkan_buffer_create(device,17,1,&token,&info)==SATCHEL_VULKAN_OK);
    check(satchel_vulkan_buffer_write(device,token,0,input,17)==SATCHEL_VULKAN_OK);
    check(satchel_vulkan_buffer_diagnostic(&buffer_report,nullptr,nullptr)==SATCHEL_VULKAN_OK && buffer_report.baseline_devices==1 && buffer_report.final_devices==1);
    check(buffer_report.baseline_buffers==1 && buffer_report.final_buffers==1 && buffer_report.cleanup_matched);
    check(satchel_vulkan_buffer_read(device,token,0,output,17)==SATCHEL_VULKAN_OK && !std::memcmp(input,output,17));
    check(satchel_vulkan_device_destroy(device)==SATCHEL_VULKAN_OK && !stats().live_buffers && satchel_vulkan_device_live_count()==0);
    std::printf("Real Vulkan bounded buffers: %u ASan/UBSan checks passed; zero-init, copy guards, parent/token checks, allocation/count limits and teardown verified.\n",checks);
}
