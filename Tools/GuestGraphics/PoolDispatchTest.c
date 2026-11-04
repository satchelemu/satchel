#include "GuestVulkanPools.h"
#include "GuestPoolServices.h"
#include "VulkanDevice.h"
#include "VulkanBuffer.h"
#include "VulkanSubmission.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static unsigned checks;
static void check_at(int value,unsigned line){++checks;if(!value){fprintf(stderr,"interpreted pool check%u failed at line%u\n",checks,line);exit(1);}}
#define check(v) check_at((v),__LINE__)
enum {CALLER=0x100000,BOOTSTRAP=0x100100,STUBS=0x200000,DATA=0x300000,DEVICE_BUILDER=0x300100,DEVICE=0x301000,
    BUILDER=0x304100,POOL=0x305000,DEVICE2=0x310000,STORAGE=0x800000};
static void put32(uint8_t *p,uint32_t value){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(value>>(8*i));}
typedef struct {SatchelCPUProbe *cpu;SatchelGuestDispatch *dispatch;SatchelGuestDispatchLayout layout;uint64_t steps,events;} Fixture;
static SatchelGuestBackendStatus fake_create(void *context,uint32_t flags,uint64_t *handle,SatchelGuestDeviceCaps *caps){(void)context;(void)flags;(void)handle;(void)caps;return SATCHEL_GUEST_BACKEND_FAILURE;}
static void make(Fixture *f,int backend,int enable){
    memset(f,0,sizeof(*f));uint8_t code[8],*data=malloc(160*1024);check(data!=NULL);memset(data,0x7a,160*1024);
    put32(code,0xd63f0120);put32(code+4,0xd4000841);check(satchel_guest_dispatch_build_stubs(NULL,0,&f->layout));
    uint8_t *stubs=malloc(f->layout.region_size);check(stubs!=NULL);check(satchel_guest_dispatch_build_stubs(stubs,f->layout.region_size,&f->layout));
    SatchelCPUProbeRegion regions[]={{CALLER,code,sizeof(code),4096,5},{STUBS,stubs,f->layout.region_size,f->layout.region_size,5},
        {DATA,data,160*1024,160*1024,3},{STORAGE,NULL,0,262144,3},{0x900000,NULL,0,65536,1}};
    char error[160]={0};f->cpu=satchel_cpu_probe_create(regions,5,CALLER,error,sizeof(error));free(data);free(stubs);check(f->cpu!=NULL);
    f->dispatch=satchel_guest_dispatch_create(BOOTSTRAP,STUBS,10000,30000);check(f->dispatch!=NULL);
    if(backend){SatchelGuestDeviceBackendV2 actual={0};check(satchel_vulkan_guest_device_backend_v2(&actual));
        if(backend==1){actual.device.version=1;check(satchel_guest_dispatch_set_device_backend(f->dispatch,&actual.device));}
        else {if(backend==3)actual.device.create=fake_create;check(satchel_guest_dispatch_set_device_backend_v2(f->dispatch,&actual));}}
    if(enable)check(satchel_guest_dispatch_enable_vulkan_pools(f->dispatch));
    uint64_t points[2];check(satchel_guest_dispatch_breakpoints(f->dispatch,points));check(satchel_cpu_probe_set_breakpoints(f->cpu,points,2));
    check(satchel_cpu_probe_set_x(f->cpu,19,UINT64_C(0x987654321abcdef0)));uint8_t vector[16];for(unsigned i=0;i<16;i++)vector[i]=(uint8_t)(i*9);check(satchel_cpu_probe_set_q(f->cpu,8,vector));
}
static uint64_t reg(Fixture *f,unsigned index){uint64_t value=0;check(satchel_cpu_probe_get_x(f->cpu,index,&value));return value;}
static uint64_t stub(Fixture *f,const char *name){for(uint32_t i=1;i<=f->layout.stub_count;i++)if(!strcmp(satchel_guest_dispatch_name(i),name))return STUBS+16+(uint64_t)(i-1)*8;check(0);return 0;}
static SatchelCPUProbeResult start(Fixture *f,const char *name,uint64_t x0,uint64_t x1,uint64_t x2){
    check(satchel_cpu_probe_set_pc(f->cpu,CALLER));check(satchel_cpu_probe_set_x(f->cpu,9,stub(f,name)));check(satchel_cpu_probe_set_x(f->cpu,0,x0));check(satchel_cpu_probe_set_x(f->cpu,1,x1));check(satchel_cpu_probe_set_x(f->cpu,2,x2));
    SatchelCPUProbeResult stop=satchel_cpu_probe_run(f->cpu,16,NULL,NULL);check(stop.reason==SATCHEL_CPU_BREAKPOINT && stop.steps==3 && stop.pc==STUBS);f->steps+=stop.steps;return stop;
}
static uint64_t handle(Fixture *f,SatchelCPUProbeResult stop,SatchelGuestDispatchStatus expected){
    SatchelGuestDispatchReport report={0};const uint64_t x1=reg(f,1),x2=reg(f,2);uint8_t before[16],after[16];check(satchel_cpu_probe_get_q(f->cpu,8,before));
    check(satchel_guest_dispatch_handle(f->dispatch,f->cpu,&stop,NULL,NULL,&report)==expected && report.status==(uint32_t)expected);f->events=report.events;
    check(reg(f,1)==x1 && reg(f,2)==x2 && reg(f,19)==UINT64_C(0x987654321abcdef0));check(satchel_cpu_probe_get_q(f->cpu,8,after) && !memcmp(before,after,16));
    if(expected==SATCHEL_GUEST_DISPATCH_RETURNED){SatchelCPUProbeResult returned=satchel_cpu_probe_run(f->cpu,8,NULL,NULL);check(returned.reason==SATCHEL_CPU_SVC && returned.svc==0x42 && returned.steps==1);f->steps+=returned.steps;}
    else check(satchel_cpu_probe_run(f->cpu,0,NULL,NULL).pc==STUBS);
    return reg(f,0);
}
static uint64_t call(Fixture *f,const char *name,uint64_t x0,uint64_t x1,uint64_t x2,SatchelGuestDispatchStatus expected){return handle(f,start(f,name,x0,x1,x2),expected);}
static void device(Fixture *f,uint64_t address){check(call(f,"nvnDeviceBuilderSetDefaults",DEVICE_BUILDER,0,0,SATCHEL_GUEST_DISPATCH_RETURNED)==DEVICE_BUILDER);check(call(f,"nvnDeviceInitialize",address,DEVICE_BUILDER,0,SATCHEL_GUEST_DISPATCH_RETURNED)==1);}
static void builder(Fixture *f,uint64_t storage){
    check(call(f,"nvnMemoryPoolBuilderSetDefaults",BUILDER,0,0,SATCHEL_GUEST_DISPATCH_RETURNED)==BUILDER);
    check(call(f,"nvnMemoryPoolBuilderSetDevice",BUILDER,DEVICE,0,SATCHEL_GUEST_DISPATCH_RETURNED)==BUILDER);
    check(call(f,"nvnMemoryPoolBuilderSetStorage",BUILDER,storage,65536,SATCHEL_GUEST_DISPATCH_RETURNED)==BUILDER);
    check(call(f,"nvnMemoryPoolBuilderSetFlags",BUILDER,UINT64_C(0xffffffff00000024),0,SATCHEL_GUEST_DISPATCH_RETURNED)==BUILDER);
}
static SatchelGuestPoolBinding binding(Fixture *f,uint64_t gpu,uint64_t *device){SatchelGuestPoolBinding result={0};check(satchel_guest_dispatch_vulkan_pool_binding(f->dispatch,f->cpu,DEVICE,gpu,64,&result,device)==SATCHEL_GUEST_DISPATCH_RETURNED && *device && result.buffer && result.size==64);return result;}
static void collect(void){SatchelGuestRetirementReport report={0};SatchelGuestRetirementStatus status=SATCHEL_GUEST_RETIRE_PENDING;struct timespec pause={0,1000000};for(unsigned i=0;i<5000 && status==SATCHEL_GUEST_RETIRE_PENDING;i++){status=satchel_guest_dispatch_retirement_poll(NULL,NULL,&report);if(status==SATCHEL_GUEST_RETIRE_PENDING)nanosleep(&pause,NULL);}check(status==SATCHEL_GUEST_RETIRE_IDLE && !report.pending_devices && !report.retiring_contexts);}
static void drop(Fixture *f){check(satchel_guest_dispatch_handoff(&f->dispatch) && !f->dispatch);check(satchel_cpu_probe_try_destroy(f->cpu));f->cpu=NULL;collect();}
static void unchanged(Fixture *f,uint64_t address,const void *before,size_t size){uint8_t copy[256];check(size<=sizeof(copy) && satchel_cpu_probe_read(f->cpu,address,copy,size) && !memcmp(copy,before,size));}
static unsigned hold_release;
static void *service_create(SatchelGuestPoolDeviceValidator v,SatchelGuestPoolRangeGuard g,void *c){return satchel_guest_pools_create_protected(v,g,c);}
static SatchelGuestPoolStatus service_destroy(void **p){if(hold_release)return hold_release==1?SATCHEL_GUEST_POOL_BUSY:SATCHEL_GUEST_POOL_BACKEND;SatchelGuestPools *owner=*p;SatchelGuestPoolStatus status=satchel_guest_pools_try_destroy(&owner);*p=owner;return status;}
static SatchelGuestPoolStatus service_execute(void *p,SatchelCPUProbe *cpu,const SatchelGuestPoolRequest *q,SatchelGuestPoolResult *r){return satchel_guest_pools_execute(p,cpu,q,r);}
static SatchelGuestPoolStatus service_resolve(void *p,SatchelCPUProbe *cpu,uint64_t d,uint64_t a,uint64_t n,SatchelGuestPoolBinding *b){return satchel_guest_pools_resolve(p,cpu,d,a,n,b);}
static SatchelGuestPoolStatus service_children(void *p,uint64_t d,uint32_t *c){return satchel_guest_pools_device_children(p,d,c);}
static SatchelGuestPoolStatus service_range(void *p,uint64_t a,uint64_t n){return satchel_guest_pools_check_unowned_range(p,a,n);}
int main(void){
    Fixture f;SatchelVulkanBufferStats stats={0};check(!satchel_vulkan_device_live_count());
    // No automatic Vulkan interpretation for absent, v1, fake or non-opted-in backends.
    for(int backend=0;backend<4;backend++){
        make(&f,backend,0);if(backend!=2)check(!satchel_guest_dispatch_enable_vulkan_pools(f.dispatch));
        check(call(&f,"nvnMemoryPoolBuilderSetDefaults",BUILDER,0,0,SATCHEL_GUEST_DISPATCH_UNIMPLEMENTED)==BUILDER);drop(&f);
    }
    make(&f,2,1);check(!satchel_guest_dispatch_enable_vulkan_pools(f.dispatch));device(&f,DEVICE);check(!satchel_guest_dispatch_enable_vulkan_pools(f.dispatch));builder(&f,STORAGE);
    uint8_t pattern[64],copy[64],changed[16],tag[16],object[256],build[64];for(unsigned i=0;i<64;i++)pattern[i]=(uint8_t)(i^0x6b);memset(changed,0x39,sizeof(changed));
    check(satchel_cpu_probe_write(f.cpu,STORAGE,pattern,sizeof(pattern)));
    check(call(&f,"nvnMemoryPoolInitialize",POOL,BUILDER,0,SATCHEL_GUEST_DISPATCH_RETURNED)==1);
    check(call(&f,"nvnMemoryPoolMap",POOL,0,0,SATCHEL_GUEST_DISPATCH_RETURNED)==STORAGE);
    uint64_t gpu=call(&f,"nvnMemoryPoolGetBufferAddress",POOL,0,0,SATCHEL_GUEST_DISPATCH_RETURNED),token=0;SatchelGuestPoolBinding view=binding(&f,gpu,&token);
    check(satchel_vulkan_buffer_read(token,view.buffer,0,copy,sizeof(copy))==SATCHEL_VULKAN_OK && !memcmp(copy,pattern,64));
    check(satchel_cpu_probe_write(f.cpu,STORAGE+16,changed,sizeof(changed)));check(satchel_vulkan_buffer_read(token,view.buffer,0,copy,64)==SATCHEL_VULKAN_OK && !memcmp(copy,pattern,64));
    check(call(&f,"nvnMemoryPoolFlushMappedRange",POOL,16,16,SATCHEL_GUEST_DISPATCH_RETURNED)==POOL);memcpy(pattern+16,changed,16);
    check(satchel_vulkan_buffer_read(token,view.buffer,0,copy,64)==SATCHEL_VULKAN_OK && !memcmp(copy,pattern,64));
    check(satchel_cpu_probe_read(f.cpu,DEVICE,tag,sizeof(tag)));check(satchel_cpu_probe_read(f.cpu,POOL,object,sizeof(object)));check(satchel_cpu_probe_read(f.cpu,BUILDER,build,sizeof(build)));
    check(call(&f,"nvnDeviceFinalize",DEVICE,0,0,SATCHEL_GUEST_DISPATCH_RESOURCE_BUSY)==DEVICE);unchanged(&f,DEVICE,tag,sizeof(tag));
    // All ten operations validate LR before output/allocation/driver mutation.
    const char *operations[]={"nvnMemoryPoolBuilderSetDefaults","nvnMemoryPoolBuilderSetDevice","nvnMemoryPoolBuilderSetStorage","nvnMemoryPoolBuilderSetFlags",
        "nvnMemoryPoolInitialize","nvnMemoryPoolFinalize","nvnMemoryPoolMap","nvnMemoryPoolGetBufferAddress","nvnMemoryPoolFlushMappedRange","nvnMemoryPoolInvalidateMappedRange"};
    for(unsigned i=0;i<10;i++){SatchelCPUProbeResult stop=start(&f,operations[i],i<4?BUILDER:POOL,0,64);check(satchel_cpu_probe_set_x(f.cpu,30,1));check(handle(&f,stop,SATCHEL_GUEST_DISPATCH_BAD_RETURN)==(i<4?BUILDER:POOL));unchanged(&f,POOL,object,sizeof(object));unchanged(&f,BUILDER,build,sizeof(build));unchanged(&f,STORAGE,pattern,sizeof(pattern));}
    uint64_t job=0;check(satchel_vulkan_buffer_fill(token,view.buffer,32,16,0x12345678,&job,NULL,NULL)==SATCHEL_VULKAN_OK && job);
    check(call(&f,"nvnMemoryPoolInvalidateMappedRange",POOL,32,16,SATCHEL_GUEST_DISPATCH_RESOURCE_BUSY)==POOL);unchanged(&f,STORAGE,pattern,64);
    check(call(&f,"nvnMemoryPoolFinalize",POOL,0,0,SATCHEL_GUEST_DISPATCH_RESOURCE_BUSY)==POOL);unchanged(&f,POOL,object,256);
    SatchelVulkanSubmissionReport completion={0};check(satchel_vulkan_submission_wait(token,job,5000,NULL,NULL,&completion)==SATCHEL_VULKAN_OK && completion.completed);
    check(call(&f,"nvnMemoryPoolInvalidateMappedRange",POOL,32,16,SATCHEL_GUEST_DISPATCH_RETURNED)==POOL);for(unsigned i=32;i<48;i+=4)put32(pattern+i,0x12345678);unchanged(&f,STORAGE,pattern,64);
    // Profile and pointer rejection preserves LR return state, object and bytes.
    call(&f,"nvnMemoryPoolBuilderSetFlags",BUILDER,0,0,SATCHEL_GUEST_DISPATCH_UNSUPPORTED_POOL_PROFILE);unchanged(&f,BUILDER,build,64);
    call(&f,"nvnMemoryPoolFlushMappedRange",POOL,UINT64_MAX,1,SATCHEL_GUEST_DISPATCH_INVALID);
    call(&f,"nvnMemoryPoolInvalidateMappedRange",POOL,65535,2,SATCHEL_GUEST_DISPATCH_INVALID);
    call(&f,"nvnMemoryPoolBuilderSetStorage",BUILDER,0x900000,65536,SATCHEL_GUEST_DISPATCH_MEMORY_FAULT);unchanged(&f,BUILDER,build,64);
    call(&f,"nvnMemoryPoolBuilderSetDefaults",DEVICE+64,0,0,SATCHEL_GUEST_DISPATCH_BAD_OBJECT);
    call(&f,"nvnDeviceBuilderSetDefaults",POOL,0,0,SATCHEL_GUEST_DISPATCH_BAD_OBJECT);
    call(&f,"nvnDeviceBuilderSetDefaults",STORAGE,0,0,SATCHEL_GUEST_DISPATCH_BAD_OBJECT);
    call(&f,"nvnDeviceInitialize",STORAGE,DEVICE_BUILDER,0,SATCHEL_GUEST_DISPATCH_BAD_OBJECT);
    call(&f,"nvnDeviceGetInteger",DEVICE,0x1c,POOL,SATCHEL_GUEST_DISPATCH_BAD_OBJECT);unchanged(&f,POOL,object,256);
    // Protect every live device, not just the pool's selected parent.
    device(&f,DEVICE2);call(&f,"nvnMemoryPoolBuilderSetDefaults",DEVICE2+64,0,0,SATCHEL_GUEST_DISPATCH_BAD_OBJECT);
    device(&f,STORAGE+65536);call(&f,"nvnMemoryPoolBuilderSetStorage",BUILDER,STORAGE+65536,65536,SATCHEL_GUEST_DISPATCH_BAD_OBJECT);
    call(&f,"nvnDeviceFinalize",STORAGE+65536,0,0,SATCHEL_GUEST_DISPATCH_RETURNED);call(&f,"nvnDeviceFinalize",DEVICE2,0,0,SATCHEL_GUEST_DISPATCH_RETURNED);
    SatchelGuestPoolBinding stale={0};uint64_t wrong=0;check(satchel_guest_dispatch_vulkan_pool_binding(f.dispatch,f.cpu,DEVICE2,gpu,64,&stale,&wrong)==SATCHEL_GUEST_DISPATCH_BAD_OBJECT && !stale.buffer && !wrong);
    check(call(&f,"nvnMemoryPoolFinalize",POOL,0,0,SATCHEL_GUEST_DISPATCH_RETURNED)==POOL);
    check(satchel_guest_dispatch_vulkan_pool_binding(f.dispatch,f.cpu,DEVICE,gpu,64,&stale,&wrong)==SATCHEL_GUEST_DISPATCH_BAD_OBJECT && !stale.buffer && !wrong);
    check(call(&f,"nvnMemoryPoolInitialize",POOL,BUILDER,0,SATCHEL_GUEST_DISPATCH_RETURNED)==1);uint64_t newer=call(&f,"nvnMemoryPoolGetBufferAddress",POOL,0,0,SATCHEL_GUEST_DISPATCH_RETURNED);check(newer!=gpu);
    check(call(&f,"nvnMemoryPoolFinalize",POOL,0,0,SATCHEL_GUEST_DISPATCH_RETURNED)==POOL);call(&f,"nvnDeviceFinalize",DEVICE,0,0,SATCHEL_GUEST_DISPATCH_RETURNED);check(!satchel_guest_dispatch_device_count(f.dispatch));drop(&f);
    // Original interpreted calls own the pending pool. Handoff releases the CPU
    // first; collection must reap real completion before pool then device cleanup.
    make(&f,2,1);device(&f,DEVICE);builder(&f,STORAGE);call(&f,"nvnMemoryPoolInitialize",POOL,BUILDER,0,SATCHEL_GUEST_DISPATCH_RETURNED);
    gpu=call(&f,"nvnMemoryPoolGetBufferAddress",POOL,0,0,SATCHEL_GUEST_DISPATCH_RETURNED);view=binding(&f,gpu,&token);
    check(satchel_vulkan_buffer_fill(token,view.buffer,0,64,0xaabbccdd,&job,NULL,NULL)==SATCHEL_VULKAN_OK && job);
    check(satchel_guest_dispatch_handoff(&f.dispatch) && !f.dispatch);check(satchel_cpu_probe_try_destroy(f.cpu));f.cpu=NULL;
    SatchelGuestRetirementReport report={0};check(satchel_guest_dispatch_retirement_stats(&report) && report.retiring_contexts==1 && report.pending_devices==1);
    check(satchel_vulkan_device_destroy(token)==SATCHEL_VULKAN_BUSY);collect();check(!satchel_vulkan_device_live_count() && !satchel_vulkan_submission_live_count());
    // Test-only service-release failures exercise retained pool-before-parent
    // ordering. The wrapped pool still owns actual Vulkan allocations.
    make(&f,2,0);SatchelGuestDeviceBackendV2 expected={0};check(satchel_vulkan_guest_device_backend_v2(&expected));
    const SatchelGuestPoolServices services={1,service_create,service_destroy,service_execute,service_resolve,service_children,service_range};
    check(satchel_guest_dispatch_attach_pool_services(f.dispatch,&expected,&services));device(&f,DEVICE);builder(&f,STORAGE);call(&f,"nvnMemoryPoolInitialize",POOL,BUILDER,0,SATCHEL_GUEST_DISPATCH_RETURNED);
    check(satchel_guest_dispatch_handoff(&f.dispatch));check(satchel_cpu_probe_try_destroy(f.cpu));f.cpu=NULL;
    for(unsigned failure=1;failure<=2;failure++){hold_release=failure;check(satchel_guest_dispatch_retirement_poll(NULL,NULL,&report)==SATCHEL_GUEST_RETIRE_PENDING && report.pending_devices==1 && report.backend_failures==1);check(satchel_vulkan_buffer_stats(&stats)==SATCHEL_VULKAN_OK && stats.live_buffers==1 && satchel_vulkan_device_live_count()==1);}
    hold_release=0;collect();check(satchel_vulkan_buffer_stats(&stats)==SATCHEL_VULKAN_OK && !stats.live_buffers && !stats.allocated_bytes && !satchel_vulkan_device_live_count());
    printf("Original interpreted NVN pools: %u ASan/UBSan assertions passed; explicit backend opt-in, ten methods, real GPU cache copies, LR/state guards and CPU-free retirement verified. No game graphics.\n",checks);
}
