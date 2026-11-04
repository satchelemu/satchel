#include "GuestPools.h"
#include "VulkanDevice.h"
#include "VulkanBuffer.h"
#include "VulkanSubmission.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
static void check_at(int value,unsigned line){++checks;if(!value){fprintf(stderr,"guest pool assertion failed at line%u (check%u)\n",line,checks);exit(1);}}
#define check(v) check_at((v),__LINE__)
enum {DEVICE=0x300000,BUILDER=0x304000,POOL=0x305000,STORAGE=0x400000,STORAGE_SIZE=18*1024*1024};
static void put32(uint8_t *p,uint32_t value){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(value>>(8*i));}
static void put64(uint8_t *p,uint64_t value){put32(p,(uint32_t)value);put32(p+4,(uint32_t)(value>>32));}
static uint64_t get64(const uint8_t *p){uint64_t value=0;for(unsigned i=0;i<8;i++)value|=(uint64_t)p[i]<<(8*i);return value;}
static unsigned fail_write,fail_destroy;static SatchelCPUProbe *unmap_after_read;
/* Test-only failure shims still allocate real buffers. They do not manufacture
 * completion or successful driver operations and are absent from the archive. */
SatchelVulkanStatus satchel_pool_test_buffer_write(uint64_t device,uint64_t buffer,uint64_t offset,const void *bytes,size_t size){
    if(fail_write){--fail_write;return SATCHEL_VULKAN_API_ERROR;}return satchel_vulkan_buffer_write(device,buffer,offset,bytes,size);
}
SatchelVulkanStatus satchel_pool_test_buffer_destroy(uint64_t device,uint64_t buffer){
    if(fail_destroy){--fail_destroy;return SATCHEL_VULKAN_API_ERROR;}return satchel_vulkan_buffer_destroy(device,buffer);
}
SatchelVulkanStatus satchel_pool_test_buffer_read(uint64_t device,uint64_t buffer,uint64_t offset,void *bytes,size_t size){
    SatchelVulkanStatus status=satchel_vulkan_buffer_read(device,buffer,offset,bytes,size);
    if(status==SATCHEL_VULKAN_OK && size && unmap_after_read){SatchelCPUProbe *cpu=unmap_after_read;unmap_after_read=NULL;check(satchel_cpu_probe_resize_rw(cpu,STORAGE,0)==SATCHEL_CPU_MAP_OK);}return status;
}
typedef struct {uint64_t token,generation;SatchelGuestPools *owner;unsigned calls,reenter;} Device;
typedef struct {SatchelCPUProbe *cpu;SatchelGuestPools *owner;Device device;} Fixture;
static int validate(void *context,SatchelCPUProbe *cpu,uint64_t address,uint64_t *token,uint64_t *generation){
    Device *device=context;++device->calls;
    if(device->reenter){
        device->reenter=0;SatchelGuestPools *same=device->owner;SatchelGuestPoolResult result={0};SatchelGuestPoolBinding binding={0};
        SatchelGuestPoolRequest request={SATCHEL_GUEST_POOL_BUILDER_DEFAULTS,BUILDER,0,0};
        check(satchel_guest_pools_execute(same,cpu,&request,&result)==SATCHEL_GUEST_POOL_BUSY && !result.value);
        check(satchel_guest_pools_resolve(same,cpu,DEVICE,UINT64_C(0x100000000),1,&binding)==SATCHEL_GUEST_POOL_BUSY && !binding.buffer);
        check(satchel_guest_pools_try_destroy(&same)==SATCHEL_GUEST_POOL_BUSY && same==device->owner);
    }
    uint8_t tag[16];SatchelGuestDeviceCaps caps={0};
    if(address!=DEVICE || !satchel_cpu_probe_check_range(cpu,address,0x3000,SATCHEL_CPU_READ) || !satchel_cpu_probe_read(cpu,address,tag,sizeof(tag)) ||
       get64(tag)!=UINT64_C(0x1504c4456) || get64(tag+8)!=device->generation ||
       satchel_vulkan_device_capabilities(device->token,&caps)!=SATCHEL_VULKAN_OK)return 0;
    *token=device->token;*generation=device->generation;return 1;
}
static void make(Fixture *f){
    memset(f,0,sizeof(*f));uint8_t code[4];put32(code,0xd4200000);
    SatchelCPUProbeRegion regions[]={{0x100000,code,4,4096,5},{DEVICE,NULL,0,65536,3},
        {0x2000000,NULL,0,65536,1},{0x2100000,NULL,0,32768,3},{0x2108000,NULL,0,32768,3}};
    char error[160]={0};f->cpu=satchel_cpu_probe_create(regions,5,0x100000,error,sizeof(error));check(f->cpu!=NULL);
    check(satchel_cpu_probe_resize_rw(f->cpu,STORAGE,STORAGE_SIZE)==SATCHEL_CPU_MAP_OK);
    SatchelGuestDeviceCaps caps={0};check(satchel_vulkan_device_create(0,&f->device.token,&caps)==SATCHEL_VULKAN_OK);f->device.generation=1;
    uint8_t tag[16]={0};put32(tag,0x504c4456);put32(tag+4,1);put64(tag+8,1);check(satchel_cpu_probe_write(f->cpu,DEVICE,tag,sizeof(tag)));
    f->owner=satchel_guest_pools_create(validate,&f->device);check(f->owner!=NULL);f->device.owner=f->owner;
    check(satchel_cpu_probe_set_x(f->cpu,19,UINT64_C(0x123456789abcdef0)));
    uint8_t vector[16];for(unsigned i=0;i<sizeof(vector);i++)vector[i]=(uint8_t)(i*3);check(satchel_cpu_probe_set_q(f->cpu,8,vector));
}
static void drop(Fixture *f){
    check(satchel_guest_pools_try_destroy(&f->owner)==SATCHEL_GUEST_POOL_OK && !f->owner);
    if(f->cpu){check(satchel_cpu_probe_try_destroy(f->cpu));f->cpu=NULL;}
    check(satchel_vulkan_device_destroy(f->device.token)==SATCHEL_VULKAN_OK);f->device.token=0;
}
static uint64_t call(Fixture *f,SatchelGuestPoolOperation operation,uint64_t object,uint64_t a,uint64_t b,SatchelGuestPoolStatus expected){
    SatchelGuestPoolRequest request={operation,object,a,b};SatchelGuestPoolResult result={.status=123,.value=UINT64_MAX};
    check(satchel_guest_pools_execute(f->owner,f->cpu,&request,&result)==expected && result.status==(uint32_t)expected);
    if(expected!=SATCHEL_GUEST_POOL_OK)check(!result.value);return result.value;
}
static void prepare(Fixture *f,uint64_t base,uint64_t size){
    call(f,SATCHEL_GUEST_POOL_BUILDER_DEFAULTS,BUILDER,0,0,SATCHEL_GUEST_POOL_OK);
    call(f,SATCHEL_GUEST_POOL_BUILDER_DEVICE,BUILDER,DEVICE,0,SATCHEL_GUEST_POOL_OK);
    call(f,SATCHEL_GUEST_POOL_BUILDER_STORAGE,BUILDER,base,size,SATCHEL_GUEST_POOL_OK);
    call(f,SATCHEL_GUEST_POOL_BUILDER_FLAGS,BUILDER,SATCHEL_GUEST_POOL_CACHED_FLAGS,0,SATCHEL_GUEST_POOL_OK);
}
static void bytes(SatchelCPUProbe *cpu,uint64_t address,void *output,size_t size){check(satchel_cpu_probe_read(cpu,address,output,size));}
static SatchelGuestPoolBinding resolve(Fixture *f,uint64_t gpu,uint64_t size,SatchelGuestPoolStatus expected){
    SatchelGuestPoolBinding binding;memset(&binding,0xff,sizeof(binding));check(satchel_guest_pools_resolve(f->owner,f->cpu,DEVICE,gpu,size,&binding)==expected);
    if(expected!=SATCHEL_GUEST_POOL_OK)check(!binding.buffer && !binding.offset && !binding.size && !binding.pool_generation);return binding;
}
static void complete(Fixture *f,uint64_t job){SatchelVulkanSubmissionReport report={0};check(satchel_vulkan_submission_wait(f->device.token,job,5000,NULL,NULL,&report)==SATCHEL_VULKAN_OK && report.completed && !report.resources_retained);}
static void unchanged(Fixture *f,uint64_t address,const void *before,size_t size){uint8_t copy[256];check(size<=sizeof(copy));bytes(f->cpu,address,copy,size);check(!memcmp(copy,before,size));}
static void baseline(void){SatchelVulkanBufferStats stats={0};check(satchel_vulkan_buffer_stats(&stats)==SATCHEL_VULKAN_OK && !stats.live_buffers && !stats.allocated_bytes && !satchel_vulkan_device_live_count() && !satchel_vulkan_submission_live_count());}
int main(void){
    baseline();check(!satchel_guest_pools_create(NULL,NULL));check(satchel_guest_pools_try_destroy(NULL)==SATCHEL_GUEST_POOL_INVALID);SatchelGuestPools *none=NULL;check(satchel_guest_pools_try_destroy(&none)==SATCHEL_GUEST_POOL_OK);
    Fixture f;make(&f);prepare(&f,STORAGE,65536);
    uint8_t pattern[64],copy[64],changed[16];for(unsigned i=0;i<64;i++)pattern[i]=(uint8_t)(i^0xa6);memset(changed,0x35,sizeof(changed));
    check(satchel_cpu_probe_write(f.cpu,STORAGE,pattern,sizeof(pattern)));
    f.device.reenter=1;check(call(&f,SATCHEL_GUEST_POOL_INITIALIZE,POOL,BUILDER,0,SATCHEL_GUEST_POOL_OK)==1 && !f.device.reenter);
    check(call(&f,SATCHEL_GUEST_POOL_MAP,POOL,0,0,SATCHEL_GUEST_POOL_OK)==STORAGE);
    const uint64_t gpu=call(&f,SATCHEL_GUEST_POOL_GPU_ADDRESS,POOL,0,0,SATCHEL_GUEST_POOL_OK);check(gpu>=UINT64_C(0x100000000) && gpu!=STORAGE);
    SatchelGuestPoolBinding binding=resolve(&f,gpu,65536,SATCHEL_GUEST_POOL_OK);check(binding.offset==0 && binding.size==65536 && binding.pool_generation);
    check(satchel_vulkan_buffer_read(f.device.token,binding.buffer,0,copy,sizeof(copy))==SATCHEL_VULKAN_OK && !memcmp(copy,pattern,sizeof(copy)));
    check(satchel_cpu_probe_write(f.cpu,STORAGE+16,changed,sizeof(changed)));
    check(satchel_vulkan_buffer_read(f.device.token,binding.buffer,0,copy,sizeof(copy))==SATCHEL_VULKAN_OK && !memcmp(copy,pattern,sizeof(copy)));
    call(&f,SATCHEL_GUEST_POOL_FLUSH,POOL,16,16,SATCHEL_GUEST_POOL_OK);memcpy(pattern+16,changed,16);
    check(satchel_vulkan_buffer_read(f.device.token,binding.buffer,0,copy,sizeof(copy))==SATCHEL_VULKAN_OK && !memcmp(copy,pattern,sizeof(copy)));
    uint8_t object[256];bytes(f.cpu,POOL,object,sizeof(object));uint64_t job=0;
    check(satchel_vulkan_buffer_fill(f.device.token,binding.buffer,32,16,0x12345678,&job,NULL,NULL)==SATCHEL_VULKAN_OK && job);
    call(&f,SATCHEL_GUEST_POOL_FLUSH,POOL,0,1,SATCHEL_GUEST_POOL_BUSY);call(&f,SATCHEL_GUEST_POOL_INVALIDATE,POOL,0,64,SATCHEL_GUEST_POOL_BUSY);
    call(&f,SATCHEL_GUEST_POOL_FINALIZE,POOL,0,0,SATCHEL_GUEST_POOL_BUSY);unchanged(&f,POOL,object,sizeof(object));unchanged(&f,STORAGE,pattern,sizeof(pattern));
    complete(&f,job);call(&f,SATCHEL_GUEST_POOL_INVALIDATE,POOL,32,16,SATCHEL_GUEST_POOL_OK);for(unsigned i=32;i<48;i+=4)put32(pattern+i,0x12345678);unchanged(&f,STORAGE,pattern,sizeof(pattern));
    resolve(&f,gpu+65535,1,SATCHEL_GUEST_POOL_OK);resolve(&f,gpu+65536,1,SATCHEL_GUEST_POOL_BAD_OBJECT);resolve(&f,gpu-1,2,SATCHEL_GUEST_POOL_BAD_OBJECT);
    resolve(&f,gpu,65537,SATCHEL_GUEST_POOL_BAD_OBJECT);resolve(&f,UINT64_MAX,2,SATCHEL_GUEST_POOL_INVALID);resolve(&f,gpu,0,SATCHEL_GUEST_POOL_INVALID);
    SatchelGuestPoolBinding wrong={0};check(satchel_guest_pools_resolve(f.owner,f.cpu,DEVICE+8,gpu,1,&wrong)==SATCHEL_GUEST_POOL_BAD_OBJECT && !wrong.buffer);
    call(&f,SATCHEL_GUEST_POOL_FLUSH,POOL,UINT64_MAX,1,SATCHEL_GUEST_POOL_INVALID);call(&f,SATCHEL_GUEST_POOL_INVALIDATE,POOL,65535,2,SATCHEL_GUEST_POOL_INVALID);
    call(&f,SATCHEL_GUEST_POOL_FLUSH,POOL,65536,0,SATCHEL_GUEST_POOL_OK);call(&f,SATCHEL_GUEST_POOL_INVALIDATE,POOL,65537,0,SATCHEL_GUEST_POOL_INVALID);
    call(&f,SATCHEL_GUEST_POOL_FLUSH,POOL,0,UINT64_MAX,SATCHEL_GUEST_POOL_INVALID);
    // Recording leases retain the exact generation; no native pointer enters
    // RAM, and a pool cannot finalize while referenced by recorded work.
    uint64_t leases[SATCHEL_GUEST_POOL_LEASE_LIMIT]={0};SatchelGuestPoolView view={0};
    check(satchel_guest_pools_retain_object(f.owner,f.cpu,DEVICE,POOL,&leases[0],&view)==SATCHEL_GUEST_POOL_OK && leases[0] && view.object==POOL && view.gpu_address==gpu && view.cpu_address==STORAGE && view.size==65536);
    check(satchel_guest_pools_retain_range(f.owner,f.cpu,DEVICE,gpu+65535,1,&leases[1],&view)==SATCHEL_GUEST_POOL_OK && leases[1]!=leases[0]);
    call(&f,SATCHEL_GUEST_POOL_FINALIZE,POOL,0,0,SATCHEL_GUEST_POOL_BUSY);unchanged(&f,POOL,object,sizeof(object));
    SatchelGuestPoolBinding leased={0};check(satchel_guest_pools_lease_binding(f.owner,f.cpu,leases[0],gpu+16,16,&leased)==SATCHEL_GUEST_POOL_OK && leased.buffer==binding.buffer && leased.offset==16 && leased.size==16);
    check(satchel_guest_pools_lease_binding(f.owner,f.cpu,leases[0],gpu+65535,2,&leased)==SATCHEL_GUEST_POOL_INVALID && !leased.buffer);
    check(satchel_guest_pools_lease_binding(f.owner,f.cpu,leases[0],UINT64_MAX,2,&leased)==SATCHEL_GUEST_POOL_INVALID && !leased.buffer);
    uint64_t rejected=9;check(satchel_guest_pools_retain_object(f.owner,f.cpu,DEVICE+8,POOL,&rejected,&view)==SATCHEL_GUEST_POOL_BAD_OBJECT && !rejected && !view.object);
    check(satchel_guest_pools_retain_range(f.owner,f.cpu,DEVICE,gpu+65536,1,&rejected,&view)==SATCHEL_GUEST_POOL_BAD_OBJECT && !rejected);
    for(unsigned i=2;i<SATCHEL_GUEST_POOL_LEASE_LIMIT;i++)check(satchel_guest_pools_retain_object(f.owner,f.cpu,DEVICE,POOL,&leases[i],&view)==SATCHEL_GUEST_POOL_OK && leases[i]>leases[i-1]);
    check(satchel_guest_pools_retain_object(f.owner,f.cpu,DEVICE,POOL,&rejected,&view)==SATCHEL_GUEST_POOL_LIMIT_REACHED && !rejected && !view.object);
    for(unsigned i=0;i<SATCHEL_GUEST_POOL_LEASE_LIMIT;i++)check(satchel_guest_pools_release(f.owner,leases[i])==SATCHEL_GUEST_POOL_OK);
    check(satchel_guest_pools_release(f.owner,leases[0])==SATCHEL_GUEST_POOL_BAD_OBJECT);check(satchel_guest_pools_lease_view(f.owner,f.cpu,leases[0],&view)==SATCHEL_GUEST_POOL_BAD_OBJECT && !view.object);
    // Finalize clears the guest object only if the owned real buffer releases.
    fail_destroy=1;call(&f,SATCHEL_GUEST_POOL_FINALIZE,POOL,0,0,SATCHEL_GUEST_POOL_BACKEND);unchanged(&f,POOL,object,sizeof(object));resolve(&f,gpu,1,SATCHEL_GUEST_POOL_OK);
    call(&f,SATCHEL_GUEST_POOL_FINALIZE,POOL,0,0,SATCHEL_GUEST_POOL_OK);uint8_t zeros[256]={0};unchanged(&f,POOL,zeros,sizeof(zeros));resolve(&f,gpu,1,SATCHEL_GUEST_POOL_BAD_OBJECT);
    check(call(&f,SATCHEL_GUEST_POOL_INITIALIZE,POOL,BUILDER,0,SATCHEL_GUEST_POOL_OK)==1);uint64_t newer=call(&f,SATCHEL_GUEST_POOL_GPU_ADDRESS,POOL,0,0,SATCHEL_GUEST_POOL_OK);check(newer!=gpu);
    SatchelGuestPoolBinding next=resolve(&f,newer,1,SATCHEL_GUEST_POOL_OK);check(next.pool_generation!=binding.pool_generation);
    uint8_t current[256];bytes(f.cpu,POOL,current,sizeof(current));check(satchel_cpu_probe_write(f.cpu,POOL,object,sizeof(object)));resolve(&f,newer,1,SATCHEL_GUEST_POOL_BAD_OBJECT);
    check(satchel_cpu_probe_write(f.cpu,POOL,current,sizeof(current)));resolve(&f,newer,1,SATCHEL_GUEST_POOL_OK);resolve(&f,gpu,1,SATCHEL_GUEST_POOL_BAD_OBJECT);
    uint64_t value=0;uint8_t vector[16];check(satchel_cpu_probe_get_x(f.cpu,19,&value) && value==UINT64_C(0x123456789abcdef0));check(satchel_cpu_probe_get_q(f.cpu,8,vector));for(unsigned i=0;i<16;i++)check(vector[i]==(uint8_t)(i*3));
    drop(&f);baseline();
    // Malformed profile, object/device overlap and storage permission boundaries.
    make(&f);prepare(&f,STORAGE,65536);bytes(f.cpu,BUILDER,copy,64);
    call(&f,SATCHEL_GUEST_POOL_BUILDER_FLAGS,BUILDER,0,0,SATCHEL_GUEST_POOL_UNSUPPORTED);
    uint8_t default_flags[64];memcpy(default_flags,copy,64);put32(default_flags+40,0);check(satchel_cpu_probe_write(f.cpu,BUILDER,default_flags,64));
    call(&f,SATCHEL_GUEST_POOL_INITIALIZE,POOL,BUILDER,0,SATCHEL_GUEST_POOL_UNSUPPORTED);check(satchel_cpu_probe_write(f.cpu,BUILDER,copy,64));
    for(unsigned bit=0;bit<32;bit++)call(&f,SATCHEL_GUEST_POOL_BUILDER_FLAGS,BUILDER,UINT64_C(1)<<bit,0,SATCHEL_GUEST_POOL_UNSUPPORTED);
    unchanged(&f,BUILDER,copy,64);call(&f,SATCHEL_GUEST_POOL_BUILDER_FLAGS,BUILDER,UINT64_C(0xffffffff00000024),0,SATCHEL_GUEST_POOL_OK);
    call(&f,SATCHEL_GUEST_POOL_BUILDER_STORAGE,BUILDER,STORAGE,0,SATCHEL_GUEST_POOL_UNSUPPORTED);
    call(&f,SATCHEL_GUEST_POOL_BUILDER_STORAGE,BUILDER,STORAGE,SATCHEL_GUEST_POOL_MAX_SIZE+65536,SATCHEL_GUEST_POOL_UNSUPPORTED);
    call(&f,SATCHEL_GUEST_POOL_BUILDER_STORAGE,BUILDER,STORAGE+1,65536,SATCHEL_GUEST_POOL_UNSUPPORTED);
    call(&f,SATCHEL_GUEST_POOL_BUILDER_STORAGE,BUILDER,STORAGE,65535,SATCHEL_GUEST_POOL_UNSUPPORTED);
    call(&f,SATCHEL_GUEST_POOL_BUILDER_STORAGE,BUILDER,0x2000000,65536,SATCHEL_GUEST_POOL_MEMORY);
    call(&f,SATCHEL_GUEST_POOL_BUILDER_STORAGE,BUILDER,0x2100000,65536,SATCHEL_GUEST_POOL_MEMORY);
    call(&f,SATCHEL_GUEST_POOL_BUILDER_STORAGE,BUILDER,UINT64_C(0xffffffffffff0000),65536,SATCHEL_GUEST_POOL_MEMORY);
    call(&f,SATCHEL_GUEST_POOL_BUILDER_DEVICE,BUILDER,DEVICE+8,0,SATCHEL_GUEST_POOL_BAD_OBJECT);
    call(&f,SATCHEL_GUEST_POOL_BUILDER_DEFAULTS,0x2000000,0,0,SATCHEL_GUEST_POOL_MEMORY);
    call(&f,SATCHEL_GUEST_POOL_INITIALIZE,DEVICE+64,BUILDER,0,SATCHEL_GUEST_POOL_BAD_OBJECT);
    call(&f,SATCHEL_GUEST_POOL_INITIALIZE,BUILDER,BUILDER,0,SATCHEL_GUEST_POOL_BAD_OBJECT);
    call(&f,SATCHEL_GUEST_POOL_INITIALIZE,STORAGE,BUILDER,0,SATCHEL_GUEST_POOL_BAD_OBJECT);
    // Forged builder inside the validated device must not overwrite any part of it.
    check(satchel_cpu_probe_write(f.cpu,DEVICE+128,copy,64));call(&f,SATCHEL_GUEST_POOL_BUILDER_DEVICE,DEVICE+128,DEVICE,0,SATCHEL_GUEST_POOL_BAD_OBJECT);unchanged(&f,DEVICE+128,copy,64);
    call(&f,SATCHEL_GUEST_POOL_INITIALIZE,POOL,DEVICE+128,0,SATCHEL_GUEST_POOL_BAD_OBJECT);
    // Even a forged builder does not bypass storage/device or builder/storage overlap.
    uint8_t forged[64];memcpy(forged,copy,64);put64(forged+24,DEVICE);put64(forged+32,65536);check(satchel_cpu_probe_write(f.cpu,BUILDER,forged,64));call(&f,SATCHEL_GUEST_POOL_INITIALIZE,POOL,BUILDER,0,SATCHEL_GUEST_POOL_BAD_OBJECT);
    check(satchel_cpu_probe_write(f.cpu,STORAGE,copy,64));call(&f,SATCHEL_GUEST_POOL_INITIALIZE,POOL,STORAGE,0,SATCHEL_GUEST_POOL_BAD_OBJECT);
    check(satchel_cpu_probe_write(f.cpu,BUILDER,copy,64));f.device.generation=2;call(&f,SATCHEL_GUEST_POOL_INITIALIZE,POOL,BUILDER,0,SATCHEL_GUEST_POOL_BAD_OBJECT);f.device.generation=1;
    // Preserve original8-pool/2MiB-range gate; expanded budgets have a separate test, independent complete ranges, no aliased pool storage.
    uint64_t addresses[8];for(unsigned i=0;i<8;i++){prepare(&f,STORAGE+(uint64_t)i*(2*1024*1024),(2*1024*1024));check(call(&f,SATCHEL_GUEST_POOL_INITIALIZE,POOL+4096*i,BUILDER,0,SATCHEL_GUEST_POOL_OK)==1);addresses[i]=call(&f,SATCHEL_GUEST_POOL_GPU_ADDRESS,POOL+4096*i,0,0,SATCHEL_GUEST_POOL_OK);resolve(&f,addresses[i]+(2*1024*1024)-1,1,SATCHEL_GUEST_POOL_OK);resolve(&f,addresses[i]+(2*1024*1024)-1,2,SATCHEL_GUEST_POOL_BAD_OBJECT);if(i)check(addresses[i]>addresses[i-1]+(2*1024*1024));}
    prepare(&f,STORAGE+8*(uint64_t)(2*1024*1024),(2*1024*1024));uint8_t ninth[256];bytes(f.cpu,POOL+8*4096,ninth,sizeof(ninth));
    call(&f,SATCHEL_GUEST_POOL_INITIALIZE,POOL+8*4096,BUILDER,0,SATCHEL_GUEST_POOL_LIMIT_REACHED);unchanged(&f,POOL+8*4096,ninth,sizeof(ninth));
    SatchelVulkanBufferStats stats={0};check(satchel_vulkan_buffer_stats(&stats)==SATCHEL_VULKAN_OK && stats.live_buffers==8 && stats.allocated_bytes>=8*(uint64_t)(2*1024*1024));
    call(&f,SATCHEL_GUEST_POOL_BUILDER_DEFAULTS,POOL,0,0,SATCHEL_GUEST_POOL_BAD_OBJECT);call(&f,SATCHEL_GUEST_POOL_BUILDER_DEFAULTS,STORAGE,0,0,SATCHEL_GUEST_POOL_BAD_OBJECT);
    prepare(&f,STORAGE,65536);call(&f,SATCHEL_GUEST_POOL_INITIALIZE,POOL+8*4096,BUILDER,0,SATCHEL_GUEST_POOL_BAD_OBJECT);
    drop(&f);baseline();
    // Initialization failure preserves provisional ownership if release fails.
    make(&f);prepare(&f,STORAGE,65536);bytes(f.cpu,POOL,object,sizeof(object));fail_write=1;fail_destroy=2;
    call(&f,SATCHEL_GUEST_POOL_INITIALIZE,POOL,BUILDER,0,SATCHEL_GUEST_POOL_INTERNAL);unchanged(&f,POOL,object,sizeof(object));
    check(satchel_vulkan_buffer_stats(&stats)==SATCHEL_VULKAN_OK && stats.live_buffers==1);check(satchel_guest_pools_try_destroy(&f.owner)==SATCHEL_GUEST_POOL_BACKEND && f.owner);
    call(&f,SATCHEL_GUEST_POOL_BUILDER_DEFAULTS,BUILDER,0,0,SATCHEL_GUEST_POOL_INVALID);drop(&f);baseline();
    // A normal failed upload releases backing and permits a clean retry.
    make(&f);prepare(&f,STORAGE,65536);fail_write=1;call(&f,SATCHEL_GUEST_POOL_INITIALIZE,POOL,BUILDER,0,SATCHEL_GUEST_POOL_BACKEND);
    check(satchel_vulkan_buffer_stats(&stats)==SATCHEL_VULKAN_OK && !stats.live_buffers);check(call(&f,SATCHEL_GUEST_POOL_INITIALIZE,POOL,BUILDER,0,SATCHEL_GUEST_POOL_OK)==1);
    newer=call(&f,SATCHEL_GUEST_POOL_GPU_ADDRESS,POOL,0,0,SATCHEL_GUEST_POOL_OK);check(newer>addresses[7]);binding=resolve(&f,newer,64,SATCHEL_GUEST_POOL_OK);
    // Removal between backend read and CPU commit rejects writeback. No raw CPU
    // pointer is kept by the owner, and teardown remains possible afterwards.
    unmap_after_read=f.cpu;call(&f,SATCHEL_GUEST_POOL_INVALIDATE,POOL,0,64,SATCHEL_GUEST_POOL_MEMORY);check(!unmap_after_read);
    call(&f,SATCHEL_GUEST_POOL_MAP,POOL,0,0,SATCHEL_GUEST_POOL_MEMORY);resolve(&f,newer,1,SATCHEL_GUEST_POOL_MEMORY);
    check(satchel_vulkan_buffer_info(f.device.token,binding.buffer,(SatchelVulkanBufferInfo[1]){{0}})==SATCHEL_VULKAN_OK);drop(&f);baseline();
    // CPU-free teardown cannot release in-flight backing. Retained pointer is
    // retried only after a real fence, with no callback/CPU dependencies.
    make(&f);prepare(&f,STORAGE,65536);check(call(&f,SATCHEL_GUEST_POOL_INITIALIZE,POOL,BUILDER,0,SATCHEL_GUEST_POOL_OK)==1);newer=call(&f,SATCHEL_GUEST_POOL_GPU_ADDRESS,POOL,0,0,SATCHEL_GUEST_POOL_OK);
    binding=resolve(&f,newer,64,SATCHEL_GUEST_POOL_OK);check(satchel_vulkan_buffer_fill(f.device.token,binding.buffer,0,64,0xaabbccdd,&job,NULL,NULL)==SATCHEL_VULKAN_OK && job);
    uint64_t retired_lease=0;check(satchel_guest_pools_retain_object(f.owner,f.cpu,DEVICE,POOL,&retired_lease,&view)==SATCHEL_GUEST_POOL_OK);
    unsigned calls=f.device.calls;check(satchel_cpu_probe_try_destroy(f.cpu));f.cpu=NULL;check(satchel_guest_pools_try_destroy(&f.owner)==SATCHEL_GUEST_POOL_BUSY && f.owner && f.device.calls==calls);
    check(satchel_vulkan_device_destroy(f.device.token)==SATCHEL_VULKAN_BUSY);complete(&f,job);
    check(satchel_guest_pools_try_destroy(&f.owner)==SATCHEL_GUEST_POOL_BUSY && f.owner);check(satchel_guest_pools_release(f.owner,retired_lease)==SATCHEL_GUEST_POOL_OK);drop(&f);check(f.device.calls==calls);baseline();
    printf("Original pool registry: %u ASan/UBSan assertions passed; real Vulkan backing/transfers, explicit cache copies, range/generation guards and retained cleanup verified. No guest NVN pool dispatch or game rendering tested.\n",checks);
}
