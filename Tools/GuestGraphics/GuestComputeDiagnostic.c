#include "GuestComputeDiagnostic.h"
#include "VulkanCompute.h"
#include "SatchelShaderFixture.h"
#include "SatchelShaderProbe.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static atomic_flag active=ATOMIC_FLAG_INIT;
static struct {uint64_t device,pipeline,uniform,storage,job;} state;
typedef struct {SatchelGuestComputeCancelled callback;void *context;uint64_t deadline;int timeout;} Cancellation;
static uint64_t now(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return UINT64_MAX;return (uint64_t)t.tv_sec*1000000000+(uint64_t)t.tv_nsec;}
static int cancelled(void *pointer){Cancellation *c=pointer;if(now()>=c->deadline){c->timeout=1;return 1;}return c->callback && c->callback(c->context);}
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t ldc(unsigned rd,unsigned bytes,unsigned offset){return UINT64_C(0xef90000000000000)|((uint64_t)(bytes==8?5:4)<<48)|(UINT64_C(3)<<36)|((uint64_t)offset<<20)|(UINT64_C(7)<<16)|(UINT64_C(255)<<8)|rd;}
static int snapshot(SatchelGuestComputeDiagnostic *r,int final){SatchelVulkanBufferStats s={0};SatchelVulkanStatus status=satchel_vulkan_buffer_stats(&s);if(status!=SATCHEL_VULKAN_OK){r->backend_status=status;return 0;}
    uint32_t devices=satchel_vulkan_device_live_count(),jobs=satchel_vulkan_submission_live_count(),pipelines=satchel_vulkan_compute_pipeline_live_count();if(devices==UINT32_MAX || jobs==UINT32_MAX || pipelines==UINT32_MAX)return 0;
    if(final){r->final_devices=devices;r->final_buffers=s.live_buffers;r->final_jobs=jobs;r->final_pipelines=pipelines;r->final_bytes=s.allocated_bytes;}
    else{r->baseline_devices=devices;r->baseline_buffers=s.live_buffers;r->baseline_jobs=jobs;r->baseline_pipelines=pipelines;r->baseline_bytes=s.allocated_bytes;}return 1;
}
static SatchelVulkanStatus cleanup(void){
    if(state.job){SatchelVulkanSubmissionReport report={0};SatchelVulkanStatus status=satchel_vulkan_submission_wait(state.device,state.job,0,NULL,NULL,&report);if(status!=SATCHEL_VULKAN_OK)return status;if(!report.completed || report.resources_retained)return SATCHEL_VULKAN_INTERNAL_ERROR;state.job=0;}
    if(state.device){SatchelVulkanStatus status=satchel_vulkan_device_destroy(state.device);if(status!=SATCHEL_VULKAN_OK)return status;}memset(&state,0,sizeof(state));return SATCHEL_VULKAN_OK;
}
static int okay(SatchelGuestComputeDiagnostic *r,SatchelVulkanStatus status,Cancellation *cancel){r->backend_status=status;if(status==SATCHEL_VULKAN_OK)return 1;
    if(status==SATCHEL_VULKAN_CANCELLED)r->status=cancel->timeout?SATCHEL_GUEST_COMPUTE_BUSY:SATCHEL_GUEST_COMPUTE_CANCELLED;
    else if(status==SATCHEL_VULKAN_BUSY || status==SATCHEL_VULKAN_TIMEOUT)r->status=SATCHEL_GUEST_COMPUTE_BUSY;return 0;
}
int satchel_guest_compute_diagnostic(SatchelGuestComputeDiagnostic *r,SatchelGuestComputeCancelled callback,void *context){
    if(!r)return 0;memset(r,0,sizeof(*r));r->status=SATCHEL_GUEST_COMPUTE_FAILED;r->stage=1;
    if(atomic_flag_test_and_set(&active)){r->status=SATCHEL_GUEST_COMPUTE_BUSY;r->resources_retained=1;return 0;}
    const uint64_t start=now();Cancellation cancel={callback,context,start>UINT64_MAX-UINT64_C(10000000000)?UINT64_MAX:start+UINT64_C(10000000000),0};
    uint8_t *ubo=NULL,*result=NULL,*expected=NULL;uint8_t code[64],cb[32]={0},spirv[16384];SatchelShaderFixtureReflection reflection={0};SatchelVulkanComputeLimits limits={0};SatchelGuestDeviceCaps caps={0};SatchelVulkanBufferInfo info={0};int skip_cleanup=0;
    uint64_t uoff=0,soff=0;size_t usize=0,ssize=0;SatchelVulkanComputeBindings bindings={0};SatchelVulkanComputeProfile profile={1,sizeof(profile),1,2};
    if(!snapshot(r,0))goto done;if(state.device){SatchelVulkanStatus status=cleanup();if(!okay(r,status,&cancel)){skip_cleanup=1;goto done;}r->prior_reaped=1;if(!snapshot(r,0))goto done;}
    if(cancelled(&cancel)){r->status=cancel.timeout?SATCHEL_GUEST_COMPUTE_BUSY:SATCHEL_GUEST_COMPUTE_CANCELLED;goto done;}
    r->stage=2;if(!okay(r,satchel_vulkan_device_create(0,&state.device,&caps),&cancel) || !okay(r,satchel_vulkan_compute_limits(state.device,&limits),&cancel))goto done;
    if(limits.version!=1 || limits.struct_size!=sizeof(limits) || limits.max_uniform_range<65536 || !limits.uniform_offset_alignment || limits.uniform_offset_alignment>65536 || !limits.storage_offset_alignment || limits.storage_offset_alignment>65536)goto done;
    r->precise_math_requested=limits.precise_math_requested;uoff=limits.uniform_offset_alignment;soff=limits.storage_offset_alignment;usize=65536+(size_t)uoff*2;ssize=(size_t)soff+64;
    r->stage=3;if(cancelled(&cancel)){r->status=cancel.timeout?SATCHEL_GUEST_COMPUTE_BUSY:SATCHEL_GUEST_COMPUTE_CANCELLED;goto done;}
    const uint64_t original[8]={0,ldc(0,8,0),ldc(2,4,12),ldc(3,4,16),0,UINT64_C(0x5c10000000000000)|(UINT64_C(3)<<20)|(UINT64_C(7)<<16)|(UINT64_C(2)<<8)|4,
        UINT64_C(0xeed8000000000000)|(UINT64_C(4)<<48)|(UINT64_C(1)<<45)|(UINT64_C(7)<<16)|4,UINT64_C(0xe300000000000000)|(UINT64_C(7)<<16)|15};
    memcpy(code,original,64);put32(cb,0x10000);put32(cb+8,16);put32(cb+12,5);put32(cb+16,7);
    SatchelShaderFixtureInput input={.version=1,.struct_size=sizeof(input),.code=code,.constant_buffer=cb,.code_size=64,.constant_buffer_size=32,
        .local_size_x=1,.local_size_y=1,.local_size_z=1,.constant_buffer_slot=3,.uniform_binding=1,.storage_binding=2,.storage_size=16,
        .storage_offset_alignment=(uint32_t)limits.storage_offset_alignment,.storage_gpu_address=0x10000};
    r->compiler_status=satchel_shader_fixture_compile(&input,spirv,sizeof(spirv),&reflection);if(r->compiler_status!=SATCHEL_SHADER_FIXTURE_OK)goto done;
    r->runtime_features=(uint32_t)satchel_shader_runtime_features();if(r->runtime_features || reflection.version!=1 || reflection.struct_size!=sizeof(reflection) || reflection.spirv_size>sizeof(spirv) || reflection.active_buffer_count!=2 || reflection.buffer_count!=3)goto done;
    r->compiled=1;r->spirv_bytes=reflection.spirv_size;r->active_bindings=reflection.active_buffer_count;r->declared_bindings=reflection.buffer_count;
    r->stage=4;if(!okay(r,satchel_vulkan_compute_pipeline_create_original(state.device,spirv,reflection.spirv_size,&profile,&state.pipeline,cancelled,&cancel),&cancel))goto done;r->pipeline_created=1;
    r->stage=5;ubo=malloc(usize);result=malloc(ssize);expected=malloc(ssize);if(!ubo || !result || !expected)goto done;
    if(!okay(r,satchel_vulkan_buffer_create(state.device,usize,SATCHEL_VULKAN_BUFFER_UNIFORM,&state.uniform,&info),&cancel) || !okay(r,satchel_vulkan_buffer_create(state.device,ssize,SATCHEL_VULKAN_BUFFER_STORAGE,&state.storage,&info),&cancel))goto done;
    bindings=(SatchelVulkanComputeBindings){state.uniform,uoff,65536,state.storage,soff,16};r->values_matched=1;r->guards_matched=1;
    const uint32_t pairs[][2]={{0,0},{5,7},{UINT32_MAX,1},{0x80000000,0x80000000},{0x7fc00001,0},{0x7f800001,0},{0xff800001,0},{0x7fffffff,0},{0x7f800000,0},{0xff800000,0},{1,0},{0x007fffff,1},{0x00800000,UINT32_MAX},{0xdeadbeef,0x11223344}};
    for(unsigned n=0;n<sizeof(pairs)/sizeof(pairs[0]);n++){
        r->stage=6;if(cancelled(&cancel)){r->status=cancel.timeout?SATCHEL_GUEST_COMPUTE_BUSY:SATCHEL_GUEST_COMPUTE_CANCELLED;goto done;}
        memset(ubo,0x3c,usize);memset(ubo+uoff,0,65536);put32(cb+12,pairs[n][0]);put32(cb+16,pairs[n][1]);memcpy(ubo+uoff,cb,32);memset(expected,0xa5,ssize);
        if(!okay(r,satchel_vulkan_buffer_write(state.device,state.uniform,0,ubo,usize),&cancel) || !okay(r,satchel_vulkan_buffer_write(state.device,state.storage,0,expected,ssize),&cancel) || !okay(r,satchel_vulkan_compute_submit_original(state.device,state.pipeline,&bindings,&state.job,cancelled,&cancel),&cancel))goto done;
        r->stage=7;SatchelVulkanSubmissionReport completed={0};if(!okay(r,satchel_vulkan_submission_wait(state.device,state.job,5000,cancelled,&cancel,&completed),&cancel))goto done;
        if(!completed.completed || completed.resources_retained)goto done;state.job=0;++r->fences_completed;
        r->stage=8;if(!okay(r,satchel_vulkan_buffer_read(state.device,state.storage,0,result,ssize),&cancel))goto done;
        r->values_matched=r->values_matched && get32(result+soff)==(uint32_t)(pairs[n][0]+pairs[n][1]);
        r->guards_matched=r->guards_matched && !memcmp(result,expected,(size_t)soff) && !memcmp(result+soff+4,expected+soff+4,ssize-(size_t)soff-4);
        uint8_t *check=malloc(usize);if(!check)goto done;SatchelVulkanStatus read=satchel_vulkan_buffer_read(state.device,state.uniform,0,check,usize);int unchanged=read==SATCHEL_VULKAN_OK && !memcmp(check,ubo,usize);free(check);
        if(!okay(r,read,&cancel))goto done;r->guards_matched=r->guards_matched && unchanged;if(!r->values_matched || !r->guards_matched)goto done;++r->cases_completed;
    }
    r->stage=9;if(cancelled(&cancel)){r->status=cancel.timeout?SATCHEL_GUEST_COMPUTE_BUSY:SATCHEL_GUEST_COMPUTE_CANCELLED;goto done;}
    if(!okay(r,satchel_vulkan_compute_pipeline_destroy(state.device,state.pipeline),&cancel))goto done;state.pipeline=0;r->pipeline_finalized=1;
    if(!okay(r,satchel_vulkan_buffer_destroy(state.device,state.uniform),&cancel))goto done;state.uniform=0;
    if(!okay(r,satchel_vulkan_buffer_destroy(state.device,state.storage),&cancel))goto done;state.storage=0;r->buffers_finalized=1;
    if(!okay(r,satchel_vulkan_device_destroy(state.device),&cancel))goto done;state.device=0;r->device_finalized=1;r->status=SATCHEL_GUEST_COMPUTE_OK;
 done:
    free(ubo);free(result);free(expected);
    if(!skip_cleanup && !state.job && state.device){SatchelVulkanStatus status=cleanup();if(status!=SATCHEL_VULKAN_OK && r->status==SATCHEL_GUEST_COMPUTE_OK)r->status=SATCHEL_GUEST_COMPUTE_BUSY;}
    r->resources_retained=state.device!=0;if(!snapshot(r,1))r->status=SATCHEL_GUEST_COMPUTE_FAILED;
    r->cleanup_matched=!r->resources_retained && r->baseline_devices==r->final_devices && r->baseline_buffers==r->final_buffers && r->baseline_jobs==r->final_jobs && r->baseline_pipelines==r->final_pipelines && r->baseline_bytes==r->final_bytes;
    if(r->status==SATCHEL_GUEST_COMPUTE_OK){if(!r->cleanup_matched)r->status=SATCHEL_GUEST_COMPUTE_FAILED;else r->stage=10;}
    int success=r->status==SATCHEL_GUEST_COMPUTE_OK;atomic_flag_clear(&active);return success;
}
