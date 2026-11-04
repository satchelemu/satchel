#include "GuestVulkanTransfers.h"
#include "GuestVulkanPools.h"
#include "GuestTransfers.h"
#include "VulkanDevice.h"
#include "VulkanSubmission.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static unsigned checks;
static void check_at(int v,unsigned line){++checks;if(!v){fprintf(stderr,"interpreted transfer assertion failed line%u check%u\n",line,checks);exit(1);}}
#define check(v) check_at((v),__LINE__)
enum {CALLER=0x100000,BOOTSTRAP=0x100100,STUBS=0x200000,DEVICE=0x300000,DB=0x303000,BUILDER=0x304000,POOL=0x305000,QUEUE=0x308000,QBUILDER=0x30a000,COMMAND=0x30b000,CONTROL=0x30c000,HANDLES=0x30d000,STORAGE=0x400000,QMEM=0x600000};
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static void put64(uint8_t *p,uint64_t v){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(v>>(8*i));}
typedef struct {SatchelCPUProbe *cpu;SatchelGuestDispatch *dispatch;SatchelGuestDispatchLayout layout;uint64_t steps,events,gpu[3];} Fixture;
static SatchelGuestBackendStatus fake_create(void *c,uint32_t flags,uint64_t *token,SatchelGuestDeviceCaps *caps){(void)c;(void)flags;(void)token;(void)caps;return SATCHEL_GUEST_BACKEND_FAILURE;}
static void make(Fixture *f,int mode){memset(f,0,sizeof(*f));uint8_t code[8];put32(code,0xd63f0120);put32(code+4,0xd4000841);check(satchel_guest_dispatch_build_stubs(NULL,0,&f->layout));uint8_t *stubs=malloc(f->layout.region_size);check(stubs!=NULL);check(satchel_guest_dispatch_build_stubs(stubs,f->layout.region_size,&f->layout));
    SatchelCPUProbeRegion regions[]={{CALLER,code,8,4096,5},{STUBS,stubs,f->layout.region_size,f->layout.region_size,5},{DEVICE,NULL,0,131072,3},{STORAGE,NULL,0,262144,3},{QMEM,NULL,0,1048576,3}};char error[160]={0};f->cpu=satchel_cpu_probe_create(regions,5,CALLER,error,sizeof(error));free(stubs);check(f->cpu!=NULL);f->dispatch=satchel_guest_dispatch_create(BOOTSTRAP,STUBS,10000,30000);check(f->dispatch!=NULL);
    if(mode){SatchelGuestDeviceBackendV2 backend={0};check(satchel_vulkan_guest_device_backend_v2(&backend));if(mode==1){backend.device.version=1;check(satchel_guest_dispatch_set_device_backend(f->dispatch,&backend.device));}else{if(mode==3)backend.device.create=fake_create;check(satchel_guest_dispatch_set_device_backend_v2(f->dispatch,&backend));}}
    if(mode==4)check(satchel_guest_dispatch_enable_vulkan_pools(f->dispatch));if(mode==5)check(satchel_guest_dispatch_enable_vulkan_transfers(f->dispatch));
    uint64_t points[2];check(satchel_guest_dispatch_breakpoints(f->dispatch,points));check(satchel_cpu_probe_set_breakpoints(f->cpu,points,2));check(satchel_cpu_probe_set_x(f->cpu,19,0x123456789abcdef0ULL));uint8_t q[16];memset(q,0x7b,16);check(satchel_cpu_probe_set_q(f->cpu,8,q));
}
static uint64_t reg(Fixture *f,unsigned n){uint64_t v=0;check(satchel_cpu_probe_get_x(f->cpu,n,&v));return v;}
static uint64_t stub(Fixture *f,const char *name){for(unsigned i=1;i<=f->layout.stub_count;i++)if(!strcmp(satchel_guest_dispatch_name(i),name))return STUBS+16+(uint64_t)(i-1)*8;check(0);return 0;}
static SatchelCPUProbeResult start(Fixture *f,const char *name,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e){uint64_t args[]={a,b,c,d,e};check(satchel_cpu_probe_set_pc(f->cpu,CALLER));check(satchel_cpu_probe_set_x(f->cpu,9,stub(f,name)));for(unsigned i=0;i<5;i++)check(satchel_cpu_probe_set_x(f->cpu,i,args[i]));SatchelCPUProbeResult stop=satchel_cpu_probe_run(f->cpu,16,NULL,NULL);check(stop.reason==SATCHEL_CPU_BREAKPOINT && stop.steps==3 && stop.pc==STUBS);f->steps+=stop.steps;return stop;}
static uint64_t handle(Fixture *f,SatchelCPUProbeResult stop,SatchelGuestDispatchStatus expected,SatchelCPUProbeCancelled cancel,void *context){uint64_t args[4];for(unsigned i=0;i<4;i++)args[i]=reg(f,i+1);SatchelGuestDispatchReport report={0};SatchelGuestDispatchStatus actual=satchel_guest_dispatch_handle(f->dispatch,f->cpu,&stop,cancel,context,&report);if(actual!=expected)fprintf(stderr,"operation%u expected%u actual%u\n",report.operation_id,expected,actual);check(actual==expected && report.status==(uint32_t)expected);f->events=report.events;
    for(unsigned i=0;i<4;i++)check(reg(f,i+1)==args[i]);check(reg(f,19)==0x123456789abcdef0ULL);uint8_t q[16];check(satchel_cpu_probe_get_q(f->cpu,8,q));for(unsigned i=0;i<16;i++)check(q[i]==0x7b);
    if(expected==SATCHEL_GUEST_DISPATCH_RETURNED){SatchelCPUProbeResult done=satchel_cpu_probe_run(f->cpu,8,NULL,NULL);check(done.reason==SATCHEL_CPU_SVC && done.svc==0x42 && done.steps==1);f->steps+=done.steps;}else check(satchel_cpu_probe_run(f->cpu,0,NULL,NULL).pc==STUBS);return reg(f,0);
}
static uint64_t call(Fixture *f,const char *name,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,SatchelGuestDispatchStatus expected){return handle(f,start(f,name,a,b,c,d,e),expected,NULL,NULL);}
#define ok(f,n,a,b,c,d,e) call(f,n,a,b,c,d,e,SATCHEL_GUEST_DISPATCH_RETURNED)
static void collect(void){SatchelGuestRetirementReport report={0};SatchelGuestRetirementStatus status=SATCHEL_GUEST_RETIRE_PENDING;for(unsigned i=0;i<5000 && status==SATCHEL_GUEST_RETIRE_PENDING;i++){status=satchel_guest_dispatch_retirement_poll(NULL,NULL,&report);if(status==SATCHEL_GUEST_RETIRE_PENDING){struct timespec delay={0,1000000};nanosleep(&delay,NULL);}}check(status==SATCHEL_GUEST_RETIRE_IDLE && !report.retiring_contexts && !report.pending_devices);SatchelVulkanBufferStats stats={0};check(satchel_vulkan_buffer_stats(&stats)==SATCHEL_VULKAN_OK && !stats.live_buffers && !stats.allocated_bytes && !satchel_vulkan_submission_live_count() && !satchel_vulkan_device_live_count());}
static void drop(Fixture *f){check(satchel_guest_dispatch_handoff(&f->dispatch) && !f->dispatch);check(satchel_cpu_probe_try_destroy(f->cpu));f->cpu=NULL;collect();}
static void prepare(Fixture *f){ok(f,"nvnDeviceBuilderSetDefaults",DB,0,0,0,0);check(ok(f,"nvnDeviceInitialize",DEVICE,DB,0,0,0)==1);uint8_t bytes[256];memset(bytes,0x9c,256);
    for(unsigned i=0;i<3;i++){check(satchel_cpu_probe_write(f->cpu,STORAGE+65536*i,bytes,256));ok(f,"nvnMemoryPoolBuilderSetDefaults",BUILDER,0,0,0,0);ok(f,"nvnMemoryPoolBuilderSetDevice",BUILDER,DEVICE,0,0,0);ok(f,"nvnMemoryPoolBuilderSetStorage",BUILDER,STORAGE+65536*i,65536,0,0);ok(f,"nvnMemoryPoolBuilderSetFlags",BUILDER,0x24,0,0,0);check(ok(f,"nvnMemoryPoolInitialize",POOL+256*i,BUILDER,0,0,0)==1);f->gpu[i]=ok(f,"nvnMemoryPoolGetBufferAddress",POOL+256*i,0,0,0,0);}
    ok(f,"nvnQueueBuilderSetDefaults",QBUILDER,0,0,0,0);ok(f,"nvnQueueBuilderSetDevice",QBUILDER,DEVICE,0,0,0);ok(f,"nvnQueueBuilderSetFlags",QBUILDER,0,0,0,0);ok(f,"nvnQueueBuilderSetCommandMemorySize",QBUILDER,4096,0,0,0);ok(f,"nvnQueueBuilderSetComputeMemorySize",QBUILDER,0,0,0,0);ok(f,"nvnQueueBuilderSetControlMemorySize",QBUILDER,4096,0,0,0);check(ok(f,"nvnQueueBuilderGetQueueMemorySize",QBUILDER,0,0,0,0)==8192);ok(f,"nvnQueueBuilderSetQueueMemory",QBUILDER,QMEM,8192,0,0);check(ok(f,"nvnQueueInitialize",QUEUE,QBUILDER,0,0,0)==1);
    check(ok(f,"nvnCommandBufferInitialize",COMMAND,DEVICE,0,0,0)==1);ok(f,"nvnCommandBufferAddCommandMemory",COMMAND,POOL,0,4096,0);ok(f,"nvnCommandBufferAddControlMemory",COMMAND,CONTROL,128,0,0);
}
static uint64_t record(Fixture *f){ok(f,"nvnCommandBufferBeginRecording",COMMAND,0,0,0,0);ok(f,"nvnCommandBufferClearBuffer",COMMAND,f->gpu[1]+16,16,0x55667788,0);ok(f,"nvnCommandBufferCopyBufferToBuffer",COMMAND,f->gpu[1]+16,f->gpu[2]+32,16,0);uint64_t h=ok(f,"nvnCommandBufferEndRecording",COMMAND,0,0,0,0);uint8_t bytes[8];put64(bytes,h);check(satchel_cpu_probe_write(f->cpu,HANDLES,bytes,8));return h;}
static int always(void *p){(void)p;return 1;}
int main(void){collect();Fixture f;
    for(int mode=0;mode<5;mode++){make(&f,mode);if(mode!=2)check(!satchel_guest_dispatch_enable_vulkan_transfers(f.dispatch));call(&f,"nvnQueueBuilderSetDefaults",QBUILDER,0,0,0,0,SATCHEL_GUEST_DISPATCH_UNIMPLEMENTED);drop(&f);}
    make(&f,5);check(!satchel_guest_dispatch_enable_vulkan_transfers(f.dispatch));prepare(&f);
    // Genuine aliases share retained backing. The locked original spans still
    // protect private objects, control memory and queue storage. Disjoint byte
    // offsets in the same allocation remain usable (legitimate stack objects).
    check(satchel_cpu_probe_map_alias(f.cpu,0x900000,CONTROL,4096)==SATCHEL_CPU_MAP_OK);
    call(&f,"nvnQueueBuilderSetDefaults",0x900000,0,0,0,0,SATCHEL_GUEST_DISPATCH_BAD_OBJECT);
    call(&f,"nvnMemoryPoolBuilderSetDefaults",0x900000,0,0,0,0,SATCHEL_GUEST_DISPATCH_BAD_OBJECT);
    ok(&f,"nvnQueueBuilderSetDefaults",0x900400,0,0,0,0);
    check(satchel_cpu_probe_unmap_alias(f.cpu,0x900000,CONTROL,4096)==SATCHEL_CPU_MAP_OK);
    check(satchel_cpu_probe_map_alias(f.cpu,0x900000,POOL,4096)==SATCHEL_CPU_MAP_OK);
    call(&f,"nvnQueueBuilderSetDefaults",0x900000,0,0,0,0,SATCHEL_GUEST_DISPATCH_BAD_OBJECT);
    call(&f,"nvnMemoryPoolBuilderSetDefaults",0x900000,0,0,0,0,SATCHEL_GUEST_DISPATCH_BAD_OBJECT);
    check(satchel_cpu_probe_unmap_alias(f.cpu,0x900000,POOL,4096)==SATCHEL_CPU_MAP_OK);
    check(satchel_cpu_probe_map_alias(f.cpu,0x900000,DEVICE,4096)==SATCHEL_CPU_MAP_OK);
    call(&f,"nvnDeviceBuilderSetDefaults",0x900040,0,0,0,0,SATCHEL_GUEST_DISPATCH_BAD_OBJECT);
    call(&f,"nvnQueueBuilderSetDefaults",0x900040,0,0,0,0,SATCHEL_GUEST_DISPATCH_BAD_OBJECT);
    check(satchel_cpu_probe_unmap_alias(f.cpu,0x900000,DEVICE,4096)==SATCHEL_CPU_MAP_OK);
    check(satchel_cpu_probe_map_alias(f.cpu,0x900000,DEVICE+8192,4096)==SATCHEL_CPU_MAP_OK);
    call(&f,"nvnDeviceGetInteger",DEVICE,0x1c,0x900000,0,0,SATCHEL_GUEST_DISPATCH_BAD_OBJECT);
    check(satchel_cpu_probe_unmap_alias(f.cpu,0x900000,DEVICE+8192,4096)==SATCHEL_CPU_MAP_OK);
    check(satchel_cpu_probe_map_alias(f.cpu,0x900000,QMEM,8192)==SATCHEL_CPU_MAP_OK);
    call(&f,"nvnMemoryPoolBuilderSetDefaults",0x900000,0,0,0,0,SATCHEL_GUEST_DISPATCH_BAD_OBJECT);
    call(&f,"nvnCommandBufferInitialize",0x900000,DEVICE,0,0,0,SATCHEL_GUEST_DISPATCH_BAD_OBJECT);
    check(satchel_cpu_probe_unmap_alias(f.cpu,0x900000,QMEM,8192)==SATCHEL_CPU_MAP_OK);
    check(satchel_cpu_probe_map_alias(f.cpu,0x900000,STORAGE,65536)==SATCHEL_CPU_MAP_OK);
    ok(&f,"nvnMemoryPoolBuilderSetStorage",BUILDER,0x900000,65536,0,0);
    call(&f,"nvnMemoryPoolInitialize",POOL+768,BUILDER,0,0,0,SATCHEL_GUEST_DISPATCH_BAD_OBJECT);
    call(&f,"nvnQueueBuilderSetDefaults",0x900400,0,0,0,0,SATCHEL_GUEST_DISPATCH_BAD_OBJECT);
    check(satchel_cpu_probe_unmap_alias(f.cpu,0x900000,STORAGE,65536)==SATCHEL_CPU_MAP_OK);
    // A new, genuinely disjoint aliased allocation is valid storage; no blanket
    // ban on aliased stack/heap memory. Explicit flush copies its real bytes.
    check(satchel_cpu_probe_map_alias(f.cpu,0x900000,STORAGE+3*65536,65536)==SATCHEL_CPU_MAP_OK);
    ok(&f,"nvnMemoryPoolBuilderSetStorage",BUILDER,0x900000,65536,0,0);
    check(ok(&f,"nvnMemoryPoolInitialize",POOL+768,BUILDER,0,0,0)==1);
    check(ok(&f,"nvnMemoryPoolMap",POOL+768,0,0,0,0)==0x900000);
    uint8_t alias_value[4]={1,3,5,7};check(satchel_cpu_probe_write(f.cpu,0x900040,alias_value,4));
    ok(&f,"nvnMemoryPoolFlushMappedRange",POOL+768,64,4,0,0);
    ok(&f,"nvnMemoryPoolFinalize",POOL+768,0,0,0,0);
    check(satchel_cpu_probe_unmap_alias(f.cpu,0x900000,STORAGE+3*65536,65536)==SATCHEL_CPU_MAP_OK);
    uint8_t restored[4];check(satchel_cpu_probe_read(f.cpu,STORAGE+3*65536+64,restored,4) && !memcmp(restored,alias_value,4));
    uint64_t h=record(&f);
    const char *ops[]={"nvnQueueBuilderSetDefaults","nvnQueueBuilderSetDevice","nvnQueueBuilderSetFlags","nvnQueueBuilderSetCommandMemorySize","nvnQueueBuilderSetComputeMemorySize","nvnQueueBuilderSetControlMemorySize","nvnQueueBuilderGetQueueMemorySize","nvnQueueBuilderSetQueueMemory","nvnQueueInitialize","nvnQueueFinalize","nvnQueueSubmitCommands","nvnQueueFinish","nvnCommandBufferInitialize","nvnCommandBufferFinalize","nvnCommandBufferAddCommandMemory","nvnCommandBufferAddControlMemory","nvnCommandBufferBeginRecording","nvnCommandBufferEndRecording","nvnCommandBufferClearBuffer","nvnCommandBufferCopyBufferToBuffer","nvnDeviceFinalizeCommandHandle","nvnCommandBufferGetCommandMemorySize","nvnCommandBufferGetCommandMemoryUsed","nvnCommandBufferGetCommandMemoryFree","nvnCommandBufferGetControlMemorySize","nvnCommandBufferGetControlMemoryUsed","nvnCommandBufferGetControlMemoryFree","nvnCommandBufferIsRecording"};
    for(unsigned i=0;i<28;i++){SatchelCPUProbeResult stop=start(&f,ops[i],QUEUE,1,HANDLES,0,0);check(satchel_cpu_probe_set_x(f.cpu,30,1));handle(&f,stop,SATCHEL_GUEST_DISPATCH_BAD_RETURN,NULL,NULL);check(!satchel_vulkan_submission_live_count());}
    check(ok(&f,"nvnCommandBufferGetCommandMemorySize",COMMAND,0,0,0,0)==4096);check(ok(&f,"nvnCommandBufferGetCommandMemoryUsed",COMMAND,0,0,0,0)==112);check(ok(&f,"nvnCommandBufferGetCommandMemoryFree",COMMAND,0,0,0,0)==3984);check(ok(&f,"nvnCommandBufferGetControlMemorySize",COMMAND,0,0,0,0)==128);check(ok(&f,"nvnCommandBufferGetControlMemoryUsed",COMMAND,0,0,0,0)==64);check(ok(&f,"nvnCommandBufferGetControlMemoryFree",COMMAND,0,0,0,0)==64);check(ok(&f,"nvnCommandBufferIsRecording",COMMAND,0,0,0,0)==0);
    call(&f,"nvnDeviceFinalize",DEVICE,0,0,0,0,SATCHEL_GUEST_DISPATCH_RESOURCE_BUSY);call(&f,"nvnMemoryPoolFinalize",POOL+256,0,0,0,0,SATCHEL_GUEST_DISPATCH_RESOURCE_BUSY);
    call(&f,"nvnMemoryPoolBuilderSetDefaults",QUEUE,0,0,0,0,SATCHEL_GUEST_DISPATCH_BAD_OBJECT);call(&f,"nvnDeviceBuilderSetDefaults",CONTROL,0,0,0,0,SATCHEL_GUEST_DISPATCH_BAD_OBJECT);call(&f,"nvnMemoryPoolBuilderSetStorage",BUILDER,QMEM,65536,0,0,SATCHEL_GUEST_DISPATCH_BAD_OBJECT);
    // Unsupported callbacks/flags stay explicit; no host allocation fallback.
    call(&f,"nvnCommandBufferSetMemoryCallback",COMMAND,1,0,0,0,SATCHEL_GUEST_DISPATCH_UNIMPLEMENTED);call(&f,"nvnQueueBuilderSetFlags",QBUILDER,1,0,0,0,SATCHEL_GUEST_DISPATCH_UNSUPPORTED_POOL_PROFILE);
    ok(&f,"nvnQueueSubmitCommands",QUEUE,1,HANDLES,0,0);check(satchel_vulkan_submission_live_count()==1);handle(&f,start(&f,"nvnQueueFinish",QUEUE,0,0,0,0),SATCHEL_GUEST_DISPATCH_CANCELLED,always,NULL);check(satchel_vulkan_submission_live_count()==1);
    call(&f,"nvnDeviceFinalizeCommandHandle",DEVICE,h,0,0,0,SATCHEL_GUEST_DISPATCH_RESOURCE_BUSY);ok(&f,"nvnQueueFinish",QUEUE,0,0,0,0);ok(&f,"nvnMemoryPoolInvalidateMappedRange",POOL+512,0,256,0,0);
    uint8_t bytes[256],expected[256];memset(expected,0x9c,256);for(unsigned i=32;i<48;i+=4)put32(expected+i,0x55667788);check(satchel_cpu_probe_read(f.cpu,STORAGE+131072,bytes,256) && !memcmp(bytes,expected,256));
    ok(&f,"nvnDeviceFinalizeCommandHandle",DEVICE,h,0,0,0);ok(&f,"nvnCommandBufferFinalize",COMMAND,0,0,0,0);ok(&f,"nvnQueueFinalize",QUEUE,0,0,0,0);for(unsigned i=0;i<3;i++)ok(&f,"nvnMemoryPoolFinalize",POOL+256*i,0,0,0,0);ok(&f,"nvnDeviceFinalize",DEVICE,0,0,0,0);check(!satchel_guest_dispatch_device_count(f.dispatch));drop(&f);
    // Queued resources survive stopped CPU destruction. The queue retirement
    // prehook must consume its fence before generic drain/pool/device teardown.
    make(&f,5);prepare(&f);record(&f);ok(&f,"nvnQueueSubmitCommands",QUEUE,1,HANDLES,0,0);check(satchel_vulkan_submission_live_count()==1);drop(&f);
    printf("interpreted NVN transfer checks passed: %u ASan/UBSan assertions; real guest-recorded GPU fill/copy, fence, guards and retirement; no game rendering\n",checks);return 0;
}
