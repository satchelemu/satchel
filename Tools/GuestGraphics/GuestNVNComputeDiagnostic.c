#include "GuestNVNComputeDiagnostic.h"
#include "GuestVulkanCompute.h"
#include "GuestVulkanPools.h"
#include "GuestCompute.h"
#include "VulkanScalar.h"
#include "VulkanDevice.h"
#include "VulkanBuffer.h"
#include "VulkanSubmission.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
enum {CALLER=0x100000,BOOTSTRAP=0x100100,STUBS=0x200000,DATA=0x300000,DEVICE_BUILDER=0x300100,
    DEVICE=0x301000,BUILDER=0x304100,POOL=0x305000,QUEUE=0x308000,QBUILDER=0x30a000,COMMAND=0x30b000,CONTROL=0x30c000,HANDLES=0x30d000,PROGRAM=0x30e000,SHADERDATA=0x30f000,SHADERCONTROL=0x310000,STORAGE=0x800000,QMEM=0xa00000};
static atomic_flag active=ATOMIC_FLAG_INIT;
static SatchelGuestDispatch *deferred_handoff;
static int pending_retirement;
static void put32(uint8_t *p,uint32_t value){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(value>>(8*i));}
static int snapshot(SatchelGuestPoolDiagnostic *report,int final){
    SatchelVulkanBufferStats stats={0};SatchelVulkanStatus status=satchel_vulkan_buffer_stats(&stats);if(status!=SATCHEL_VULKAN_OK){report->backend_status=status;return 0;}
    if(final){report->final_devices=satchel_vulkan_device_live_count();report->final_buffers=stats.live_buffers;report->final_jobs=satchel_vulkan_submission_live_count();report->final_bytes=stats.allocated_bytes;}
    else {report->baseline_devices=satchel_vulkan_device_live_count();report->baseline_buffers=stats.live_buffers;report->baseline_jobs=satchel_vulkan_submission_live_count();report->baseline_bytes=stats.allocated_bytes;}
    return 1;
}
static int reap(SatchelGuestPoolDiagnostic *report){
    if(deferred_handoff && !satchel_guest_dispatch_handoff(&deferred_handoff)){report->status=SATCHEL_GUEST_POOL_DIAG_BUSY;return 0;}
    if(pending_retirement){SatchelGuestRetirementReport retired={0};SatchelGuestRetirementStatus status=satchel_guest_dispatch_retirement_poll(NULL,NULL,&retired);
        if(status!=SATCHEL_GUEST_RETIRE_IDLE){report->status=(status==SATCHEL_GUEST_RETIRE_PENDING || status==SATCHEL_GUEST_RETIRE_BUSY)?SATCHEL_GUEST_POOL_DIAG_BUSY:SATCHEL_GUEST_POOL_DIAG_FAILED;return 0;}
        pending_retirement=0;
    }
    return 1;
}
static int cancelled(SatchelGuestPoolDiagnostic *report,SatchelGuestPoolCancelled callback,void *context){if(callback && callback(context)){report->status=SATCHEL_GUEST_POOL_DIAG_CANCELLED;return 1;}return 0;}
static int invoke(SatchelCPUProbe *cpu,SatchelGuestDispatch *dispatch,SatchelGuestNVNComputeDiagnostic *report,uint64_t target,
    uint64_t x0,uint64_t x1,uint64_t x2,uint64_t x3,uint64_t x4,SatchelGuestPoolCancelled cancel,void *context,uint64_t *value){
    if(!satchel_cpu_probe_set_pc(cpu,CALLER) || !satchel_cpu_probe_set_x(cpu,9,target) || !satchel_cpu_probe_set_x(cpu,0,x0) || !satchel_cpu_probe_set_x(cpu,1,x1) || !satchel_cpu_probe_set_x(cpu,2,x2) || !satchel_cpu_probe_set_x(cpu,3,x3) || !satchel_cpu_probe_set_x(cpu,4,x4))return 0;
    SatchelCPUProbeResult stop=satchel_cpu_probe_run(cpu,32,cancel,context);report->instructions+=stop.steps;report->cpu_stop_reason=stop.reason;
    if(stop.reason!=SATCHEL_CPU_BREAKPOINT){if(stop.reason==SATCHEL_CPU_CANCELLED)report->memory.status=SATCHEL_GUEST_POOL_DIAG_CANCELLED;return 0;}
    SatchelGuestDispatchReport dispatched={0};SatchelGuestDispatchStatus status=satchel_guest_dispatch_handle(dispatch,cpu,&stop,cancel,context,&dispatched);
    report->dispatch_status=status;report->dispatch_events=dispatched.events;
    if(status!=SATCHEL_GUEST_DISPATCH_RETURNED){if(status==SATCHEL_GUEST_DISPATCH_CANCELLED)report->memory.status=SATCHEL_GUEST_POOL_DIAG_CANCELLED;return 0;}
    stop=satchel_cpu_probe_run(cpu,8,cancel,context);report->instructions+=stop.steps;report->cpu_stop_reason=stop.reason;
    if(stop.reason==SATCHEL_CPU_CANCELLED)report->memory.status=SATCHEL_GUEST_POOL_DIAG_CANCELLED;
    return stop.reason==SATCHEL_CPU_SVC && stop.svc==0x42 && satchel_cpu_probe_get_x(cpu,0,value);
}
static int call(SatchelCPUProbe *cpu,SatchelGuestDispatch *dispatch,SatchelGuestNVNComputeDiagnostic *report,const char *name,
    uint64_t x0,uint64_t x1,uint64_t x2,uint64_t x3,uint64_t x4,SatchelGuestPoolCancelled cancel,void *context,uint64_t *value){
    uint64_t target=0;
    return satchel_cpu_probe_write(cpu,DATA,name,strlen(name)+1) && invoke(cpu,dispatch,report,BOOTSTRAP,DATA,0,0,0,0,cancel,context,&target) &&
        invoke(cpu,dispatch,report,target,x0,x1,x2,x3,x4,cancel,context,value);
}
static void put64(uint8_t *p,uint64_t value){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(value>>(8*i));}
#define CALL(name,a,b,c,d,e) call(cpu,dispatch,report,name,a,b,c,d,e,cancel,context,&value)

static uint64_t ldc(unsigned d,unsigned bytes,unsigned offset){return UINT64_C(0xef90000000000000)|((uint64_t)(bytes==8?5:4)<<48)|(UINT64_C(3)<<36)|((uint64_t)offset<<20)|(UINT64_C(7)<<16)|(UINT64_C(255)<<8)|d;}
int satchel_guest_nvn_compute_diagnostic(SatchelGuestNVNComputeDiagnostic *report,SatchelGuestPoolCancelled cancel,void *context){
 if(!report)return 0;memset(report,0,sizeof(*report));SatchelGuestPoolDiagnostic *r=&report->memory;r->status=SATCHEL_GUEST_POOL_DIAG_FAILED;r->stage=1;
 if(atomic_flag_test_and_set(&active)){r->status=SATCHEL_GUEST_POOL_DIAG_BUSY;r->resources_retained=1;return 0;}
 SatchelCPUProbe *cpu=NULL;SatchelGuestDispatch *dispatch=NULL;uint8_t *stubs=NULL,*data=NULL;uint8_t code[8],ctl[0x880]={0},shader[128]={0},sd[16],cb[64]={0},expected[96],actual[96],vector[16],read_vector[16],handle_bytes[8];
 SatchelGuestDispatchLayout layout={0};SatchelGuestDeviceBackendV2 backend={0};uint64_t gpu[2]={0},value=0,handle=0;char error[160]={0};int skip_reap=0;
 if(!snapshot(r,0))goto done;if(deferred_handoff || pending_retirement){if(!reap(r)){skip_reap=1;goto done;}r->prior_reaped=1;if(!snapshot(r,0))goto done;}report->baseline_pipelines=satchel_vulkan_compute_pipeline_live_count();if(cancelled(r,cancel,context))goto done;
 if(!satchel_guest_dispatch_build_stubs(NULL,0,&layout))goto done;stubs=malloc(layout.region_size);data=malloc(131072);if(!stubs || !data || !satchel_guest_dispatch_build_stubs(stubs,layout.region_size,&layout))goto done;
 memset(data,0x7a,131072);put32(code,0xd63f0120);put32(code+4,0xd4000841);
 SatchelCPUProbeRegion regions[]={{CALLER,code,8,4096,5},{STUBS,stubs,layout.region_size,layout.region_size,5},{DATA,data,131072,131072,3},{STORAGE,NULL,0,2*65536,3},{QMEM,NULL,0,12288,3}};
 cpu=satchel_cpu_probe_create(regions,5,CALLER,error,sizeof(error));free(stubs);stubs=NULL;free(data);data=NULL;if(!cpu)goto done;
 dispatch=satchel_guest_dispatch_create(BOOTSTRAP,STUBS,1024,30000);if(!dispatch || !satchel_vulkan_guest_device_backend_v2(&backend) || !satchel_guest_dispatch_set_device_backend_v2(dispatch,&backend) || !satchel_guest_dispatch_enable_vulkan_compute(dispatch))goto done;
 uint64_t points[2];if(!satchel_guest_dispatch_breakpoints(dispatch,points) || !satchel_cpu_probe_set_breakpoints(cpu,points,2))goto done;
 for(unsigned i=0;i<16;i++)vector[i]=(uint8_t)(i*7);if(!satchel_cpu_probe_set_x(cpu,19,0x23456789abcdef01ULL) || !satchel_cpu_probe_set_q(cpu,8,vector))goto done;
 r->stage=2;if(cancelled(r,cancel,context))goto done;
 if(!CALL("nvnDeviceBuilderSetDefaults",DEVICE_BUILDER,0,0,0,0) || !CALL("nvnDeviceInitialize",DEVICE,DEVICE_BUILDER,0,0,0) || value!=1)goto done;r->device_created=1;
 for(unsigned i=0;i<2;i++){if(!CALL("nvnMemoryPoolBuilderSetDefaults",BUILDER,0,0,0,0) || !CALL("nvnMemoryPoolBuilderSetDevice",BUILDER,DEVICE,0,0,0) || !CALL("nvnMemoryPoolBuilderSetStorage",BUILDER,STORAGE+i*65536,65536,0,0) || !CALL("nvnMemoryPoolBuilderSetFlags",BUILDER,0x24,0,0,0) || !CALL("nvnMemoryPoolInitialize",POOL+i*256,BUILDER,0,0,0) || value!=1 || !CALL("nvnMemoryPoolGetBufferAddress",POOL+i*256,0,0,0,0))goto done;gpu[i]=value;}r->pool_created=1;
 r->stage=3;if(cancelled(r,cancel,context))goto done;
 if(!CALL("nvnQueueBuilderSetDefaults",QBUILDER,0,0,0,0) || !CALL("nvnQueueBuilderSetDevice",QBUILDER,DEVICE,0,0,0) || !CALL("nvnQueueBuilderSetCommandMemorySize",QBUILDER,4096,0,0,0) || !CALL("nvnQueueBuilderSetComputeMemorySize",QBUILDER,4096,0,0,0) || !CALL("nvnQueueBuilderSetControlMemorySize",QBUILDER,4096,0,0,0) || !CALL("nvnQueueBuilderGetQueueMemorySize",QBUILDER,0,0,0,0) || value!=12288 || !CALL("nvnQueueBuilderSetQueueMemory",QBUILDER,QMEM,12288,0,0) || !CALL("nvnQueueInitialize",QUEUE,QBUILDER,0,0,0) || value!=1)goto done;
 if(!CALL("nvnCommandBufferInitialize",COMMAND,DEVICE,0,0,0) || value!=1 || !CALL("nvnCommandBufferAddCommandMemory",COMMAND,POOL,0,4096,0) || !CALL("nvnCommandBufferAddControlMemory",COMMAND,CONTROL,128,0,0) || !CALL("nvnProgramInitialize",PROGRAM,DEVICE,0,0,0) || value!=1)goto done;report->program_initialized=1;
 put32(ctl,0x98761234);put32(ctl+0x20,sizeof(ctl));put32(ctl+0x700,64);put32(ctl+0x704,128);put32(ctl+0x748,1);put32(ctl+0x74c,1);put32(ctl+0x750,1);put64(sd,gpu[1]);put64(sd+8,SHADERCONTROL);
 if(!satchel_cpu_probe_write(cpu,SHADERCONTROL,ctl,sizeof(ctl)) || !satchel_cpu_probe_write(cpu,SHADERDATA,sd,sizeof(sd)))goto done;
 r->stage=4;if(cancelled(r,cancel,context))goto done;
 for(unsigned test=0;test<4;test++){
  const unsigned cb_offset=test==3?0x1100:0x1000,store_offset=test==3?24:12;const uint32_t a=test==3?UINT32_MAX:(test?100:5),b=test==3?2:(test?200:7),expected_value=test==3?1:(test<2?12:300);
  if(test==0 || test==3){
   uint64_t words[8]={0,ldc(10,8,8),ldc(20,4,24),ldc(21,4,28),0,UINT64_C(0x5c10000000070000)|(UINT64_C(21)<<20)|(UINT64_C(20)<<8)|22,UINT64_C(0xeedc200000070000)|((uint64_t)store_offset<<20)|(UINT64_C(10)<<8)|22,UINT64_C(0xe30000000007000f)};memcpy(shader,words,64);
   if(!satchel_cpu_probe_write(cpu,STORAGE+65536,shader,sizeof(shader)) || !CALL("nvnMemoryPoolFlushMappedRange",POOL+256,0,sizeof(shader),0,0) || !CALL("nvnProgramSetShaders",PROGRAM,1,SHADERDATA,0,0) || value!=1 || satchel_vulkan_compute_pipeline_live_count()!=report->baseline_pipelines)goto done;++report->program_admissions;
  }
  memset(cb,0,sizeof(cb));put64(cb+8,gpu[1]+0x2000);put32(cb+24,a);put32(cb+28,b);if(!satchel_cpu_probe_write(cpu,STORAGE+65536+cb_offset,cb,sizeof(cb)))goto done;
  if(test!=1 && !CALL("nvnMemoryPoolFlushMappedRange",POOL+256,cb_offset,sizeof(cb),0,0))goto done;
  memset(expected,0xa5,sizeof(expected));if(!satchel_cpu_probe_write(cpu,STORAGE+65536+0x1ff0,expected,sizeof(expected)) || !CALL("nvnMemoryPoolFlushMappedRange",POOL+256,0x1ff0,sizeof(expected),0,0))goto done;
  r->stage=5;if(cancelled(r,cancel,context))goto done;
  if(test==0 || test==3){if(!CALL("nvnCommandBufferBeginRecording",COMMAND,0,0,0,0) || !CALL("nvnCommandBufferBindProgram",COMMAND,PROGRAM,0x20,0,0) || !CALL("nvnCommandBufferBindUniformBuffer",COMMAND,5,0,gpu[1]+cb_offset,64) || !CALL("nvnCommandBufferBindStorageBuffer",COMMAND,5,0,gpu[1]+0x2000,64) || !CALL("nvnCommandBufferDispatchCompute",COMMAND,1,1,1,0) || !CALL("nvnCommandBufferEndRecording",COMMAND,0,0,0,0) || !value)goto done;handle=value;put64(handle_bytes,handle);if(!satchel_cpu_probe_write(cpu,HANDLES,handle_bytes,8))goto done;report->commands_recorded+=4;}
  r->stage=6;if(cancelled(r,cancel,context))goto done;
  if(!CALL("nvnQueueSubmitCommands",QUEUE,1,HANDLES,0,0) || satchel_vulkan_compute_pipeline_live_count()!=report->baseline_pipelines+1 || satchel_vulkan_submission_live_count()!=r->baseline_jobs+1)goto done;++report->pipelines_compiled;
  if(!CALL("nvnQueueFlush",QUEUE,0,0,0,0) || satchel_vulkan_submission_live_count()!=r->baseline_jobs+1)goto done;
  r->stage=7;if(cancelled(r,cancel,context))goto done;
  if(!CALL("nvnQueueFinish",QUEUE,0,0,0,0) || satchel_vulkan_compute_pipeline_live_count()!=report->baseline_pipelines || satchel_vulkan_submission_live_count()!=r->baseline_jobs)goto done;++report->fences_completed;
  if(!satchel_cpu_probe_read(cpu,STORAGE+65536+0x1ff0,actual,sizeof(actual)) || memcmp(actual,expected,sizeof(actual)) || !CALL("nvnMemoryPoolInvalidateMappedRange",POOL+256,0x1ff0,sizeof(actual),0,0))goto done;
  put32(expected+16+store_offset,expected_value);if(!satchel_cpu_probe_read(cpu,STORAGE+65536+0x1ff0,actual,sizeof(actual)) || memcmp(actual,expected,sizeof(actual)))goto done;++report->cases_completed;
  if(test==1)report->unflushed_preserved=1;if(test==2)report->flushed_changed=1;if(test==3)report->changed_code_rebound=1;
  if(test>=2){if(!CALL("nvnDeviceFinalizeCommandHandle",DEVICE,handle,0,0,0))goto done;++report->handle_finalized;}
 }
 r->values_matched=r->guards_matched=r->gpu_completed=r->flush_completed=r->invalidate_completed=1;
 r->state_preserved=satchel_cpu_probe_get_x(cpu,19,&value) && value==0x23456789abcdef01ULL && satchel_cpu_probe_get_q(cpu,8,read_vector) && !memcmp(vector,read_vector,16);if(!r->state_preserved)goto done;
 r->stage=8;if(cancelled(r,cancel,context))goto done;
 if(!CALL("nvnProgramFinalize",PROGRAM,0,0,0,0))goto done;report->program_finalized=1;
 if(!CALL("nvnCommandBufferFinalize",COMMAND,0,0,0,0))goto done;report->command_finalized=1;
 if(!CALL("nvnQueueFinalize",QUEUE,0,0,0,0))goto done;report->queue_finalized=1;
 for(unsigned i=0;i<2;i++)if(!CALL("nvnMemoryPoolFinalize",POOL+256*i,0,0,0,0))goto done;r->pool_finalized=1;
 if(!CALL("nvnDeviceFinalize",DEVICE,0,0,0,0))goto done;report->device_finalized=satchel_guest_dispatch_device_count(dispatch)==0;if(!report->device_finalized)goto done;
 r->stage=9;if(cancelled(r,cancel,context))goto done;r->status=SATCHEL_GUEST_POOL_DIAG_OK;
 done:
 free(stubs);free(data);
 if(satchel_vulkan_submission_live_count()>r->baseline_jobs)skip_reap=1;
 if(dispatch){if(!satchel_guest_dispatch_handoff(&dispatch))deferred_handoff=dispatch;pending_retirement=1;}
 if(cpu)satchel_cpu_probe_destroy(cpu);
 if(!skip_reap && (deferred_handoff || pending_retirement) && !reap(r) && r->status==SATCHEL_GUEST_POOL_DIAG_OK)r->status=SATCHEL_GUEST_POOL_DIAG_BUSY;
 r->resources_retained=deferred_handoff || pending_retirement;if(!snapshot(r,1))r->status=SATCHEL_GUEST_POOL_DIAG_FAILED;report->final_pipelines=satchel_vulkan_compute_pipeline_live_count();
 r->cleanup_matched=!r->resources_retained && r->baseline_devices==r->final_devices && r->baseline_buffers==r->final_buffers && r->baseline_jobs==r->final_jobs && r->baseline_bytes==r->final_bytes && report->baseline_pipelines==report->final_pipelines;
 if(r->status==SATCHEL_GUEST_POOL_DIAG_OK){if(!r->cleanup_matched)r->status=SATCHEL_GUEST_POOL_DIAG_FAILED;else r->stage=10;}
 int success=r->status==SATCHEL_GUEST_POOL_DIAG_OK;atomic_flag_clear(&active);return success;
}
