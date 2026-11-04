#include "VulkanTransferBatch.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>
static unsigned checks;
static void check_at(bool value,unsigned line){++checks;if(!value){std::fprintf(stderr,"Vulkan batch check%u failed at line%u\n",checks,line);std::exit(1);}}
#define check(v) check_at((v),__LINE__)
static int cancel(void *){return 1;}
static int cancel_at(void *context){return --*static_cast<unsigned *>(context)==0;}
struct Mutation {SatchelVulkanTransferCommand *commands;bool changed;};
static int mutate(void *context){auto *m=static_cast<Mutation *>(context);m->commands[0].destination=0;m->commands[0].value=0;m->changed=true;return 0;}
static SatchelVulkanTransferCommand fill(uint64_t buffer,uint64_t offset,uint64_t size,uint32_t value){return {SATCHEL_VULKAN_TRANSFER_FILL,value,0,0,buffer,offset,size};}
static SatchelVulkanTransferCommand copy(uint64_t source,uint64_t so,uint64_t destination,uint64_t offset,uint64_t size){return {SATCHEL_VULKAN_TRANSFER_COPY,0,source,so,destination,offset,size};}
static void reap(uint64_t device,uint64_t job){SatchelVulkanSubmissionReport report={};check(satchel_vulkan_submission_wait(device,job,5000,nullptr,nullptr,&report)==SATCHEL_VULKAN_OK && report.completed && !report.resources_retained);}
static void put32(uint8_t *p,uint32_t value){for(unsigned i=0;i<4;i++)p[i]=static_cast<uint8_t>(value>>(i*8));}
int main(){
    uint64_t device=0,other=0,buffers[3]={},foreign=0,no_transfer=0,job=0;SatchelGuestDeviceCaps caps={};SatchelVulkanBufferInfo info={};
    check(satchel_vulkan_device_create(0,&device,&caps)==SATCHEL_VULKAN_OK);check(satchel_vulkan_device_create(0,&other,&caps)==SATCHEL_VULKAN_OK);
    for(auto &buffer:buffers)check(satchel_vulkan_buffer_create(device,64,7,&buffer,&info)==SATCHEL_VULKAN_OK);
    check(satchel_vulkan_buffer_create(other,64,7,&foreign,&info)==SATCHEL_VULKAN_OK);check(satchel_vulkan_buffer_create(device,64,1,&no_transfer,&info)==SATCHEL_VULKAN_OK);
    uint8_t expected[3][64],actual[64];for(auto &bytes:expected)std::memset(bytes,0xa5,sizeof(bytes));
    for(unsigned i=0;i<3;i++)check(satchel_vulkan_buffer_write(device,buffers[i],0,expected[i],64)==SATCHEL_VULKAN_OK);
    SatchelVulkanTransferCommand commands[]={fill(buffers[0],4,16,0x12345678),copy(buffers[0],4,buffers[1],8,16),
        fill(buffers[0],4,16,0xabcdef12),copy(buffers[1],8,buffers[2],12,16),copy(buffers[2],13,buffers[2],40,7),
        copy(buffers[0],5,buffers[1],32,9),fill(buffers[1],8,4,0x55667788)};
    for(const auto &c:commands){unsigned dst=0,src=0;while(buffers[dst]!=c.destination)++dst;
        if(c.kind==SATCHEL_VULKAN_TRANSFER_COPY){while(buffers[src]!=c.source)++src;std::memcpy(expected[dst]+c.destination_offset,expected[src]+c.source_offset,static_cast<size_t>(c.size));}
        else for(uint64_t offset=c.destination_offset;offset<c.destination_offset+c.size;offset+=4)put32(expected[dst]+offset,c.value);
    }
    check(satchel_vulkan_transfer_batch(device,commands,7,&job,nullptr,nullptr)==SATCHEL_VULKAN_OK && job);const uint64_t first=job;
    SatchelVulkanSubmissionReport report={};check(satchel_vulkan_submission_wait(device,job,5000,cancel,nullptr,&report)==SATCHEL_VULKAN_CANCELLED && report.resources_retained);
    for(auto buffer:buffers){check(satchel_vulkan_buffer_read(device,buffer,0,actual,64)==SATCHEL_VULKAN_BUSY);check(satchel_vulkan_buffer_write(device,buffer,0,actual,64)==SATCHEL_VULKAN_BUSY);check(satchel_vulkan_buffer_destroy(device,buffer)==SATCHEL_VULKAN_BUSY);}
    check(satchel_vulkan_device_destroy(device)==SATCHEL_VULKAN_BUSY);check(satchel_vulkan_transfer_batch(device,commands,7,&job,nullptr,nullptr)==SATCHEL_VULKAN_BUSY && !job);
    reap(device,first);for(unsigned i=0;i<3;i++)check(satchel_vulkan_buffer_read(device,buffers[i],0,actual,64)==SATCHEL_VULKAN_OK && !std::memcmp(actual,expected[i],64));
    // Entire input is validated before any early command can run.
    const SatchelVulkanTransferCommand good=fill(buffers[0],0,4,0x33445566);
    check(satchel_vulkan_transfer_batch(device,&good,1,nullptr,nullptr,nullptr)==SATCHEL_VULKAN_INVALID);
    check(satchel_vulkan_transfer_batch(device,nullptr,1,&job,nullptr,nullptr)==SATCHEL_VULKAN_INVALID && !job);
    check(satchel_vulkan_transfer_batch(device,&good,0,&job,nullptr,nullptr)==SATCHEL_VULKAN_INVALID && !job);
    check(satchel_vulkan_transfer_batch(device,&good,33,&job,nullptr,nullptr)==SATCHEL_VULKAN_INVALID && !job);
    SatchelVulkanTransferCommand bads[]={fill(buffers[0],1,4,1),fill(buffers[0],0,0,1),fill(buffers[0],0,3,1),fill(buffers[0],UINT64_MAX-3,4,1),
        fill(buffers[0],64,4,1),fill(foreign,0,4,1),fill(0,0,4,1),copy(buffers[0],60,buffers[1],0,8),copy(buffers[0],0,buffers[1],60,8),
        copy(0,0,buffers[1],0,4),copy(buffers[0],0,buffers[1],0,UINT64_MAX),copy(buffers[0],UINT64_MAX,buffers[1],0,4)};
    for(const auto &bad:bads){SatchelVulkanTransferCommand pair[]={good,bad};job=99;check(satchel_vulkan_transfer_batch(device,pair,2,&job,nullptr,nullptr)==SATCHEL_VULKAN_INVALID && !job);check(!satchel_vulkan_submission_live_count());check(satchel_vulkan_buffer_read(device,buffers[0],0,actual,64)==SATCHEL_VULKAN_OK && !std::memcmp(actual,expected[0],64));}
    SatchelVulkanTransferCommand malformed=good;malformed.kind=99;check(satchel_vulkan_transfer_batch(device,&malformed,1,&job,nullptr,nullptr)==SATCHEL_VULKAN_INVALID && !job);
    malformed=good;malformed.source=buffers[1];check(satchel_vulkan_transfer_batch(device,&malformed,1,&job,nullptr,nullptr)==SATCHEL_VULKAN_INVALID && !job);
    malformed=good;malformed.source_offset=4;check(satchel_vulkan_transfer_batch(device,&malformed,1,&job,nullptr,nullptr)==SATCHEL_VULKAN_INVALID && !job);
    malformed=copy(buffers[0],0,buffers[1],0,4);malformed.value=1;check(satchel_vulkan_transfer_batch(device,&malformed,1,&job,nullptr,nullptr)==SATCHEL_VULKAN_INVALID && !job);
    const SatchelVulkanTransferCommand unsupported[]={copy(buffers[0],0,buffers[0],1,8),copy(buffers[0],0,buffers[0],0,4),fill(no_transfer,0,4,1),copy(no_transfer,0,buffers[0],0,4),copy(buffers[0],0,no_transfer,0,4)};
    for(const auto &bad:unsupported){SatchelVulkanTransferCommand pair[]={good,bad};check(satchel_vulkan_transfer_batch(device,pair,2,&job,nullptr,nullptr)==SATCHEL_VULKAN_UNSUPPORTED && !job);}
    for(unsigned i=1;i<=2;i++){unsigned remaining=i;check(satchel_vulkan_transfer_batch(device,&good,1,&job,cancel_at,&remaining)==SATCHEL_VULKAN_CANCELLED && !job);check(satchel_vulkan_buffer_read(device,buffers[0],0,actual,64)==SATCHEL_VULKAN_OK && !std::memcmp(actual,expected[0],64));}
    // Caller changes after entry cannot alter the validated copied batch.
    SatchelVulkanTransferCommand mutable_command=good;Mutation mutation={&mutable_command,false};check(satchel_vulkan_transfer_batch(device,&mutable_command,1,&job,mutate,&mutation)==SATCHEL_VULKAN_OK && mutation.changed);reap(device,job);put32(expected[0],good.value);check(satchel_vulkan_buffer_read(device,buffers[0],0,actual,64)==SATCHEL_VULKAN_OK && !std::memcmp(actual,expected[0],64));
    // Maximum command count; repeated resources are retained once, not once per operation.
    SatchelVulkanTransferCommand maximum[SATCHEL_VULKAN_TRANSFER_COMMAND_LIMIT];for(unsigned i=0;i<SATCHEL_VULKAN_TRANSFER_COMMAND_LIMIT;i++)maximum[i]=fill(buffers[0],0,4,i);
    check(satchel_vulkan_transfer_batch(device,maximum,SATCHEL_VULKAN_TRANSFER_COMMAND_LIMIT,&job,nullptr,nullptr)==SATCHEL_VULKAN_OK);reap(device,job);put32(expected[0],31);
    check(satchel_vulkan_buffer_read(device,buffers[0],0,actual,64)==SATCHEL_VULKAN_OK && !std::memcmp(actual,expected[0],64));
    uint64_t large=0;check(satchel_vulkan_buffer_create(device,8*1024*1024,7,&large,&info)==SATCHEL_VULKAN_OK);
    SatchelVulkanTransferCommand work[5];for(auto &c:work)c=fill(large,0,8*1024*1024,0x13579bdf);
    check(satchel_vulkan_transfer_batch(device,work,5,&job,nullptr,nullptr)==SATCHEL_VULKAN_INVALID && !job);
    check(satchel_vulkan_transfer_batch(device,work,4,&job,nullptr,nullptr)==SATCHEL_VULKAN_OK);reap(device,job);uint32_t edge=0;
    check(satchel_vulkan_buffer_read(device,large,8*1024*1024-4,&edge,4)==SATCHEL_VULKAN_OK && edge==0x13579bdf);check(satchel_vulkan_buffer_destroy(device,large)==SATCHEL_VULKAN_OK);
    check(satchel_vulkan_device_destroy(other)==SATCHEL_VULKAN_OK);check(satchel_vulkan_device_destroy(device)==SATCHEL_VULKAN_OK);
    // Retain all32 unique buffer slots, including the device-retirement drain path.
    check(satchel_vulkan_device_create(0,&device,&caps)==SATCHEL_VULKAN_OK);uint64_t all[SATCHEL_VULKAN_BUFFER_LIMIT];
    for(unsigned i=0;i<SATCHEL_VULKAN_BUFFER_LIMIT;i++){check(satchel_vulkan_buffer_create(device,4,7,&all[i],&info)==SATCHEL_VULKAN_OK);maximum[i]=fill(all[i],0,4,i+10);}
    check(satchel_vulkan_transfer_batch(device,maximum,32,&job,nullptr,nullptr)==SATCHEL_VULKAN_OK);
    for(auto b:all)check(satchel_vulkan_buffer_destroy(device,b)==SATCHEL_VULKAN_BUSY);
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);SatchelVulkanStatus drained=SATCHEL_VULKAN_BUSY;
    while(drained==SATCHEL_VULKAN_BUSY && std::chrono::steady_clock::now()<deadline){drained=satchel_vulkan_device_drain(device);if(drained==SATCHEL_VULKAN_BUSY)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    check(drained==SATCHEL_VULKAN_OK);check(satchel_vulkan_submission_wait(device,job,0,nullptr,nullptr,&report)==SATCHEL_VULKAN_INVALID);
    for(unsigned i=0;i<SATCHEL_VULKAN_BUFFER_LIMIT;i++){check(satchel_vulkan_buffer_read(device,all[i],0,&edge,4)==SATCHEL_VULKAN_OK && edge==i+10);check(satchel_vulkan_buffer_destroy(device,all[i])==SATCHEL_VULKAN_OK);}
    // A stale token cannot be used again even when its slot is free.
    SatchelVulkanTransferCommand stale=fill(all[0],0,4,1);check(satchel_vulkan_transfer_batch(device,&stale,1,&job,nullptr,nullptr)==SATCHEL_VULKAN_INVALID && !job);
    check(satchel_vulkan_device_destroy(device)==SATCHEL_VULKAN_OK);
    uint64_t parents[5]={},children[5]={},jobs[5]={};
    for(unsigned i=0;i<5;i++){
        check(satchel_vulkan_device_create(0,&parents[i],&caps)==SATCHEL_VULKAN_OK);check(satchel_vulkan_buffer_create(parents[i],16,7,&children[i],&info)==SATCHEL_VULKAN_OK);
        const auto c=fill(children[i],0,16,i);check(satchel_vulkan_transfer_batch(parents[i],&c,1,&jobs[i],nullptr,nullptr)==(i<4?SATCHEL_VULKAN_OK:SATCHEL_VULKAN_BUSY));
    }
    check(!jobs[4] && satchel_vulkan_submission_live_count()==4);
    for(unsigned i=0;i<5;i++){if(i<4)reap(parents[i],jobs[i]);check(satchel_vulkan_device_destroy(parents[i])==SATCHEL_VULKAN_OK);}
    SatchelVulkanBufferStats stats={};check(satchel_vulkan_buffer_stats(&stats)==SATCHEL_VULKAN_OK && !stats.live_buffers && !stats.allocated_bytes && !satchel_vulkan_submission_live_count() && !satchel_vulkan_device_live_count());
    std::printf("Original Vulkan transfer batches: %u ASan/UBSan assertions passed; ordered dependent fills/copies, disjoint self-copy, atomic validation, all-buffer retention, bounds and real fence cleanup verified.\n",checks);
}
