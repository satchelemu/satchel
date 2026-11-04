#include "GuestVulkanCapturedRaster.h"
#include "GuestVulkanTransfers.h"
#include "GuestVulkanCompute.h"
#include "GuestCompute.h"
#include "VulkanScalar.h"
#include "GuestVulkanPools.h"
#include "GuestTransfers.h"
#include "VulkanDevice.h"
#include "VulkanSubmission.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static unsigned fail_write,fail_submit,fail_destroy;
SatchelCPUWriteResult nvn_test_writev(SatchelCPUProbe *cpu,const SatchelCPUProbeWrite *w,size_t n){if(fail_write && !--fail_write)return SATCHEL_CPU_WRITE_ALLOCATION;return satchel_cpu_probe_writev(cpu,w,n);}
SatchelVulkanStatus nvn_test_submit(uint64_t d,uint64_t p,const SatchelVulkanScalarBindings *b,uint64_t *job,SatchelVulkanCancelled c,void *ctx){unsigned mode=fail_submit;fail_submit=0;if(mode==1){*job=0;return SATCHEL_VULKAN_API_ERROR;}SatchelVulkanStatus s=satchel_vulkan_scalar_submit(d,p,b,job,c,ctx);return mode==2 && s==SATCHEL_VULKAN_OK?SATCHEL_VULKAN_API_ERROR:s;}
SatchelVulkanStatus nvn_test_destroy(uint64_t d,uint64_t b){if(fail_destroy){--fail_destroy;return SATCHEL_VULKAN_API_ERROR;}return satchel_vulkan_buffer_destroy(d,b);}
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
    if(mode==4)check(satchel_guest_dispatch_enable_vulkan_pools(f->dispatch));if(mode==5)check(satchel_guest_dispatch_enable_vulkan_transfers(f->dispatch));if(mode==6)check(satchel_guest_dispatch_enable_vulkan_captured_raster(f->dispatch));
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
    ok(f,"nvnQueueBuilderSetDefaults",QBUILDER,0,0,0,0);ok(f,"nvnQueueBuilderSetDevice",QBUILDER,DEVICE,0,0,0);ok(f,"nvnQueueBuilderSetFlags",QBUILDER,0,0,0,0);ok(f,"nvnQueueBuilderSetCommandMemorySize",QBUILDER,4096,0,0,0);ok(f,"nvnQueueBuilderSetComputeMemorySize",QBUILDER,4096,0,0,0);ok(f,"nvnQueueBuilderSetControlMemorySize",QBUILDER,4096,0,0,0);check(ok(f,"nvnQueueBuilderGetQueueMemorySize",QBUILDER,0,0,0,0)==12288);ok(f,"nvnQueueBuilderSetQueueMemory",QBUILDER,QMEM,12288,0,0);check(ok(f,"nvnQueueInitialize",QUEUE,QBUILDER,0,0,0)==1);
    check(ok(f,"nvnCommandBufferInitialize",COMMAND,DEVICE,0,0,0)==1);ok(f,"nvnCommandBufferAddCommandMemory",COMMAND,POOL,0,4096,0);ok(f,"nvnCommandBufferAddControlMemory",COMMAND,CONTROL,128,0,0);
}
static int always(void *p){(void)p;return 1;}

enum {PROGRAM=0x30e000,SHADERDATA=0x30f000,SHADERCONTROL=0x310000};
static uint64_t ldc(unsigned d,unsigned bytes,unsigned offset){return UINT64_C(0xef90000000000000)|((uint64_t)(bytes==8?5:4)<<48)|(UINT64_C(3)<<36)|((uint64_t)offset<<20)|(UINT64_C(7)<<16)|(UINT64_C(255)<<8)|d;}
static void shader(Fixture *f,unsigned offset){uint8_t ctl[0x880]={0},code[128]={0},sd[16];put32(ctl,0x98761234);put32(ctl+0x20,sizeof(ctl));put32(ctl+0x700,64);put32(ctl+0x704,128);put32(ctl+0x748,1);put32(ctl+0x74c,1);put32(ctl+0x750,1);
 uint64_t words[8]={0,ldc(10,8,8),ldc(20,4,24),ldc(21,4,28),0,UINT64_C(0x5c10000000070000)|(UINT64_C(21)<<20)|(UINT64_C(20)<<8)|22,UINT64_C(0xeedc200000070000)|((uint64_t)offset<<20)|(UINT64_C(10)<<8)|22,UINT64_C(0xe30000000007000f)};memcpy(code,words,64);put64(sd,f->gpu[1]);put64(sd+8,SHADERCONTROL);check(satchel_cpu_probe_write(f->cpu,SHADERCONTROL,ctl,sizeof(ctl)));check(satchel_cpu_probe_write(f->cpu,STORAGE+65536,code,sizeof(code)));check(satchel_cpu_probe_write(f->cpu,SHADERDATA,sd,16));ok(f,"nvnMemoryPoolFlushMappedRange",POOL+256,0,128,0,0);check(ok(f,"nvnProgramSetShaders",PROGRAM,1,SHADERDATA,0,0)==1);check(!satchel_vulkan_compute_pipeline_live_count());}
static void operands(Fixture *f,uint32_t a,uint32_t b,unsigned cb_offset,int flush){uint8_t cb[64]={0};put64(cb+8,f->gpu[1]+0x2000);put32(cb+24,a);put32(cb+28,b);check(satchel_cpu_probe_write(f->cpu,STORAGE+65536+cb_offset,cb,64));if(flush)ok(f,"nvnMemoryPoolFlushMappedRange",POOL+256,cb_offset,64,0,0);}
static void output_clear(Fixture *f){uint8_t out[96];memset(out,0xa5,96);check(satchel_cpu_probe_write(f->cpu,STORAGE+65536+0x1ff0,out,96));ok(f,"nvnMemoryPoolFlushMappedRange",POOL+256,0x1ff0,96,0,0);}
static void output_check(Fixture *f,unsigned offset,uint32_t expected_value){uint8_t actual[96],expected[96];memset(expected,0xa5,96);put32(expected+16+offset,expected_value);ok(f,"nvnMemoryPoolInvalidateMappedRange",POOL+256,0x1ff0,96,0,0);check(satchel_cpu_probe_read(f->cpu,STORAGE+65536+0x1ff0,actual,96) && !memcmp(actual,expected,96));}
static uint64_t compute_record(Fixture *f,unsigned cb_offset){ok(f,"nvnCommandBufferBeginRecording",COMMAND,0,0,0,0);ok(f,"nvnCommandBufferBindProgram",COMMAND,PROGRAM,0x20,0,0);ok(f,"nvnCommandBufferBindUniformBuffer",COMMAND,5,0,f->gpu[1]+cb_offset,64);ok(f,"nvnCommandBufferBindStorageBuffer",COMMAND,5,0,f->gpu[1]+0x2000,64);ok(f,"nvnCommandBufferDispatchCompute",COMMAND,1,1,1,0);uint64_t h=ok(f,"nvnCommandBufferEndRecording",COMMAND,0,0,0,0);uint8_t bytes[8];put64(bytes,h);check(satchel_cpu_probe_write(f->cpu,HANDLES,bytes,8));return h;}
static void program_prepare(Fixture *f){make(f,6);prepare(f);check(ok(f,"nvnProgramInitialize",PROGRAM,DEVICE,0,0,0)==1);shader(f,12);operands(f,5,7,0x1000,1);output_clear(f);}
static void negative_record(Fixture *f,unsigned mode){
 ok(f,"nvnCommandBufferBeginRecording",COMMAND,0,0,0,0);ok(f,"nvnCommandBufferBindProgram",COMMAND,PROGRAM,0x20,0,0);
 call(f,"nvnCommandBufferBindUniformBuffer",COMMAND,5,1,f->gpu[1]+0x1000,64,SATCHEL_GUEST_DISPATCH_UNSUPPORTED_POOL_PROFILE);
 call(f,"nvnCommandBufferBindUniformBuffer",COMMAND,4,0,f->gpu[1]+0x1000,64,SATCHEL_GUEST_DISPATCH_UNSUPPORTED_POOL_PROFILE);
 call(f,"nvnCommandBufferBindUniformBuffer",COMMAND,5,0,f->gpu[1]+0x1000,0,SATCHEL_GUEST_DISPATCH_UNSUPPORTED_POOL_PROFILE);
 call(f,"nvnCommandBufferBindStorageBuffer",COMMAND,5,1,f->gpu[1]+0x2000,64,SATCHEL_GUEST_DISPATCH_UNSUPPORTED_POOL_PROFILE);
 call(f,"nvnCommandBufferBindStorageBuffer",COMMAND,5,0,f->gpu[0],64,SATCHEL_GUEST_DISPATCH_BAD_OBJECT);
 check(ok(f,"nvnCommandBufferGetCommandMemoryUsed",COMMAND,0,0,0,0)==48);
 ok(f,"nvnCommandBufferBindUniformBuffer",COMMAND,5,0,f->gpu[1]+0x1000,64);
 ok(f,"nvnCommandBufferBindStorageBuffer",COMMAND,5,0,f->gpu[1]+(mode==1?0x1000:(mode==2?0:0x2000)),64);
 if(mode){call(f,"nvnCommandBufferDispatchCompute",COMMAND,1,1,1,0,SATCHEL_GUEST_DISPATCH_UNSUPPORTED_POOL_PROFILE);check(!satchel_vulkan_submission_live_count());return;}
 uint8_t before[192],after[192];check(satchel_cpu_probe_read(f->cpu,STORAGE,before,sizeof(before)));fail_write=1;call(f,"nvnCommandBufferDispatchCompute",COMMAND,1,1,1,0,SATCHEL_GUEST_DISPATCH_BACKEND_FAILURE);check(satchel_cpu_probe_read(f->cpu,STORAGE,after,sizeof(after)) && !memcmp(before,after,sizeof(before)));call(f,"nvnCommandBufferEndRecording",COMMAND,0,0,0,0,SATCHEL_GUEST_DISPATCH_UNSUPPORTED_POOL_PROFILE);call(f,"nvnQueueSubmitCommands",QUEUE,1,HANDLES,0,0,SATCHEL_GUEST_DISPATCH_BAD_OBJECT);check(!satchel_vulkan_submission_live_count());
}
int main(void){collect();Fixture f;program_prepare(&f);ok(&f,"nvnQueueFlush",QUEUE,0,0,0,0);check(!satchel_vulkan_submission_live_count());
 const char *ops[]={"nvnProgramInitialize","nvnProgramFinalize","nvnProgramSetShaders","nvnCommandBufferBindProgram","nvnCommandBufferBindUniformBuffer","nvnCommandBufferBindStorageBuffer","nvnCommandBufferDispatchCompute"};
 for(unsigned i=0;i<7;i++){SatchelCPUProbeResult stop=start(&f,ops[i],PROGRAM,1,SHADERDATA,0,0);check(satchel_cpu_probe_set_x(f.cpu,30,1));handle(&f,stop,SATCHEL_GUEST_DISPATCH_BAD_RETURN,NULL,NULL);check(!satchel_vulkan_submission_live_count());}
 call(&f,"nvnProgramSetShaders",PROGRAM,2,SHADERDATA,0,0,SATCHEL_GUEST_DISPATCH_UNSUPPORTED_POOL_PROFILE);
 check(satchel_cpu_probe_map_alias(f.cpu,0x900000,PROGRAM,4096)==SATCHEL_CPU_MAP_OK);
 call(&f,"nvnMemoryPoolBuilderSetDefaults",0x900000,0,0,0,0,SATCHEL_GUEST_DISPATCH_BAD_OBJECT);
 call(&f,"nvnCommandBufferInitialize",0x900000,DEVICE,0,0,0,SATCHEL_GUEST_DISPATCH_BAD_OBJECT);
 check(satchel_cpu_probe_unmap_alias(f.cpu,0x900000,PROGRAM,4096)==SATCHEL_CPU_MAP_OK);
 // Failed admission preserves the previously accepted immutable generation.
 uint8_t original_code[128],changed[128];check(satchel_cpu_probe_read(f.cpu,STORAGE+65536,original_code,128));
 memcpy(changed,original_code,128);memset(changed+8,0,8);check(satchel_cpu_probe_write(f.cpu,STORAGE+65536,changed,128));ok(&f,"nvnMemoryPoolFlushMappedRange",POOL+256,0,128,0,0);call(&f,"nvnProgramSetShaders",PROGRAM,1,SHADERDATA,0,0,SATCHEL_GUEST_DISPATCH_UNSUPPORTED_POOL_PROFILE);
 memcpy(changed,original_code,128);for(unsigned i=0;i<3;i++){uint64_t word=0;memcpy(&word,changed+8+i*8,8);word^=UINT64_C(1)<<36;memcpy(changed+8+i*8,&word,8);}check(satchel_cpu_probe_write(f.cpu,STORAGE+65536,changed,128));ok(&f,"nvnMemoryPoolFlushMappedRange",POOL+256,0,128,0,0);call(&f,"nvnProgramSetShaders",PROGRAM,1,SHADERDATA,0,0,SATCHEL_GUEST_DISPATCH_UNSUPPORTED_POOL_PROFILE);
 check(satchel_cpu_probe_write(f.cpu,STORAGE+65536,original_code,128));ok(&f,"nvnMemoryPoolFlushMappedRange",POOL+256,0,128,0,0);
 uint64_t h=compute_record(&f,0x1000);ok(&f,"nvnQueueFlush",QUEUE,0,0,0,0);check(!satchel_vulkan_submission_live_count());
 uint8_t multiple[16];put64(multiple,h);put64(multiple+8,h);check(satchel_cpu_probe_write(f.cpu,HANDLES,multiple,16));call(&f,"nvnQueueSubmitCommands",QUEUE,2,HANDLES,0,0,SATCHEL_GUEST_DISPATCH_UNSUPPORTED_POOL_PROFILE);check(!satchel_vulkan_submission_live_count());
 call(&f,"nvnProgramFinalize",PROGRAM,0,0,0,0,SATCHEL_GUEST_DISPATCH_RESOURCE_BUSY);call(&f,"nvnProgramSetShaders",PROGRAM,1,SHADERDATA,0,0,SATCHEL_GUEST_DISPATCH_RESOURCE_BUSY);call(&f,"nvnMemoryPoolFinalize",POOL+256,0,0,0,0,SATCHEL_GUEST_DISPATCH_RESOURCE_BUSY);
 handle(&f,start(&f,"nvnQueueSubmitCommands",QUEUE,1,HANDLES,0,0),SATCHEL_GUEST_DISPATCH_CANCELLED,always,NULL);check(!satchel_vulkan_submission_live_count());
 fail_submit=1;call(&f,"nvnQueueSubmitCommands",QUEUE,1,HANDLES,0,0,SATCHEL_GUEST_DISPATCH_BACKEND_FAILURE);check(!satchel_vulkan_submission_live_count() && !satchel_vulkan_compute_pipeline_live_count());
 fail_submit=2;call(&f,"nvnQueueSubmitCommands",QUEUE,1,HANDLES,0,0,SATCHEL_GUEST_DISPATCH_BACKEND_FAILURE);check(satchel_vulkan_submission_live_count()==1);call(&f,"nvnQueueFlush",QUEUE,0,0,0,0,SATCHEL_GUEST_DISPATCH_BACKEND_FAILURE);check(satchel_vulkan_submission_live_count()==1);handle(&f,start(&f,"nvnQueueFinish",QUEUE,0,0,0,0),SATCHEL_GUEST_DISPATCH_CANCELLED,always,NULL);check(satchel_vulkan_submission_live_count()==1);fail_destroy=1;call(&f,"nvnQueueFinish",QUEUE,0,0,0,0,SATCHEL_GUEST_DISPATCH_BACKEND_FAILURE);call(&f,"nvnDeviceFinalizeCommandHandle",DEVICE,h,0,0,0,SATCHEL_GUEST_DISPATCH_RESOURCE_BUSY);ok(&f,"nvnQueueFinish",QUEUE,0,0,0,0);output_check(&f,12,12);
 // The recording retains immutable state, but each submission snapshots then-current GPU-visible operands.
 operands(&f,100,200,0x1000,0);output_clear(&f);ok(&f,"nvnQueueSubmitCommands",QUEUE,1,HANDLES,0,0);ok(&f,"nvnQueueFlush",QUEUE,0,0,0,0);check(satchel_vulkan_submission_live_count()==1);ok(&f,"nvnQueueFinish",QUEUE,0,0,0,0);output_check(&f,12,12);
 ok(&f,"nvnMemoryPoolFlushMappedRange",POOL+256,0x1000,64,0,0);output_clear(&f);ok(&f,"nvnQueueSubmitCommands",QUEUE,1,HANDLES,0,0);ok(&f,"nvnQueueFlush",QUEUE,0,0,0,0);check(satchel_vulkan_submission_live_count()==1);ok(&f,"nvnQueueFinish",QUEUE,0,0,0,0);output_check(&f,12,300);ok(&f,"nvnQueueFlush",QUEUE,0,0,0,0);check(!satchel_vulkan_submission_live_count());
 ok(&f,"nvnDeviceFinalizeCommandHandle",DEVICE,h,0,0,0);shader(&f,24);operands(&f,UINT32_MAX,2,0x1100,1);output_clear(&f);h=compute_record(&f,0x1100);ok(&f,"nvnQueueSubmitCommands",QUEUE,1,HANDLES,0,0);ok(&f,"nvnQueueFlush",QUEUE,0,0,0,0);check(satchel_vulkan_submission_live_count()==1);ok(&f,"nvnQueueFinish",QUEUE,0,0,0,0);output_check(&f,24,1);ok(&f,"nvnDeviceFinalizeCommandHandle",DEVICE,h,0,0,0);
 ok(&f,"nvnProgramFinalize",PROGRAM,0,0,0,0);ok(&f,"nvnCommandBufferFinalize",COMMAND,0,0,0,0);ok(&f,"nvnQueueFinalize",QUEUE,0,0,0,0);for(unsigned i=0;i<3;i++)ok(&f,"nvnMemoryPoolFinalize",POOL+256*i,0,0,0,0);ok(&f,"nvnDeviceFinalize",DEVICE,0,0,0,0);drop(&f);
 program_prepare(&f);compute_record(&f,0x1000);ok(&f,"nvnQueueSubmitCommands",QUEUE,1,HANDLES,0,0);drop(&f);
 for(unsigned mode=0;mode<3;mode++){program_prepare(&f);negative_record(&f,mode);drop(&f);}
 program_prepare(&f);ok(&f,"nvnCommandBufferBeginRecording",COMMAND,0,0,0,0);ok(&f,"nvnCommandBufferClearBuffer",COMMAND,f.gpu[2],4,1,0);call(&f,"nvnCommandBufferBindProgram",COMMAND,PROGRAM,0x20,0,0,SATCHEL_GUEST_DISPATCH_UNSUPPORTED_POOL_PROFILE);drop(&f);
 program_prepare(&f);ok(&f,"nvnCommandBufferBeginRecording",COMMAND,0,0,0,0);ok(&f,"nvnCommandBufferBindProgram",COMMAND,PROGRAM,0x20,0,0);call(&f,"nvnCommandBufferClearBuffer",COMMAND,f.gpu[2],4,1,0,SATCHEL_GUEST_DISPATCH_UNSUPPORTED_POOL_PROFILE);drop(&f);
 printf("Interpreted NVN scalar compute: %u ASan/UBSan assertions; original guest code/resources, admitted program, recorded bindings, GPU execution, explicit flush/fence/invalidate and CPU-free cleanup.\n",checks);return 0;}
