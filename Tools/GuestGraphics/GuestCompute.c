#include "GuestCompute.h"
#include "VulkanScalar.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
typedef struct {uint64_t object,device,generation;uint32_t references,admitted;SatchelScalarProgram program;} Link;
typedef struct {uint32_t state;SatchelGuestComputeBinding input;uint64_t cb_lease,storage_lease,program_generation,backend,device_generation;} Binding;
typedef struct {uint64_t device,pipeline,uniform,job;uint32_t compiled,submitted,completed,semantic_ops,stores,unused;} Run;
struct SatchelGuestCompute {SatchelGuestPools *pools;SatchelGuestPrograms *programs;SatchelGuestPoolDeviceValidator validate;void *context;atomic_flag active;uint32_t retiring;Link links[8];Binding binding,pending;Run run;};
static uint32_t gp(SatchelGuestProgramStatus s){switch(s){case SATCHEL_GP_OK:return SATCHEL_GC_OK;case SATCHEL_GP_MEMORY:return SATCHEL_GC_MEMORY;case SATCHEL_GP_UNSUPPORTED:return SATCHEL_GC_UNSUPPORTED;case SATCHEL_GP_BUSY:return SATCHEL_GC_BUSY;case SATCHEL_GP_ALLOCATION:return SATCHEL_GC_ALLOCATION;case SATCHEL_GP_CHANGED:return SATCHEL_GC_CHANGED;case SATCHEL_GP_BACKEND:return SATCHEL_GC_BACKEND;default:return SATCHEL_GC_INVALID;}}
static uint32_t pool(SatchelGuestPoolStatus s){switch(s){case SATCHEL_GUEST_POOL_OK:return SATCHEL_GC_OK;case SATCHEL_GUEST_POOL_MEMORY:return SATCHEL_GC_MEMORY;case SATCHEL_GUEST_POOL_BUSY:return SATCHEL_GC_BUSY;case SATCHEL_GUEST_POOL_ALLOCATION:return SATCHEL_GC_ALLOCATION;case SATCHEL_GUEST_POOL_BACKEND:return SATCHEL_GC_BACKEND;default:return SATCHEL_GC_INVALID;}}
static uint32_t vk(SatchelVulkanStatus s){switch(s){case SATCHEL_VULKAN_OK:return SATCHEL_GC_OK;case SATCHEL_VULKAN_BUSY:case SATCHEL_VULKAN_TIMEOUT:return SATCHEL_GC_BUSY;case SATCHEL_VULKAN_CANCELLED:return SATCHEL_GC_CANCELLED;case SATCHEL_VULKAN_UNSUPPORTED:return SATCHEL_GC_UNSUPPORTED;case SATCHEL_VULKAN_INVALID:return SATCHEL_GC_INVALID;default:return SATCHEL_GC_BACKEND;}}
static uint32_t enter(SatchelGuestCompute *o,int retiring){if(!o)return SATCHEL_GC_INVALID;if(atomic_flag_test_and_set(&o->active))return SATCHEL_GC_BUSY;if(o->retiring && !retiring){atomic_flag_clear(&o->active);return SATCHEL_GC_INVALID;}return SATCHEL_GC_OK;}
static uint32_t leave(SatchelGuestCompute *o,uint32_t s){atomic_flag_clear(&o->active);return s;}
static Link *link_for(SatchelGuestCompute *o,uint64_t p){for(unsigned i=0;i<8;i++)if(o->links[i].object==p && p)return &o->links[i];return NULL;}
static int held(SatchelGuestCompute *o,uint64_t p){Link *l=link_for(o,p);return (l && l->references) || (o->binding.state && o->binding.input.program==p) || (o->pending.state && o->pending.input.program==p);}
static uint32_t release_binding(SatchelGuestCompute *o,Binding *b){if(!b->state)return SATCHEL_GC_OK;b->state=2;uint32_t s;
 if(b->cb_lease){s=pool(satchel_guest_pools_release(o->pools,b->cb_lease));if(s)return s;b->cb_lease=0;}
 if(b->storage_lease){s=pool(satchel_guest_pools_release(o->pools,b->storage_lease));if(s)return s;b->storage_lease=0;}
 memset(b,0,sizeof(*b));return SATCHEL_GC_OK;}
static uint32_t clean_run(SatchelGuestCompute *o,uint32_t ms,SatchelCPUProbeCancelled cancel,void *ctx,int *completed){Run *r=&o->run;uint32_t s;
 if(r->job){SatchelVulkanSubmissionReport report={0};s=vk(satchel_vulkan_submission_wait(r->device,r->job,ms,cancel,ctx,&report));if(s)return s;if(!report.completed || report.resources_retained)return SATCHEL_GC_INTERNAL;r->job=0;r->completed=1;if(completed)*completed=1;}
 if(r->pipeline){s=vk(satchel_vulkan_compute_pipeline_destroy(r->device,r->pipeline));if(s)return s;r->pipeline=0;}
 if(r->uniform){s=vk(satchel_vulkan_buffer_destroy(r->device,r->uniform));if(s)return s;r->uniform=0;}
 memset(r,0,sizeof(*r));return SATCHEL_GC_OK;}
static int resources(const SatchelGuestCompute *o){return o->run.job || o->run.pipeline || o->run.uniform;}
static uint32_t spans(SatchelGuestCompute *o,SatchelCPUProbe *cpu,const Binding *b,SatchelGuestPoolBinding *cb,SatchelGuestPoolBinding *storage){
 if(b->state!=1)return SATCHEL_GC_BUSY;Link *l=link_for(o,b->input.program);uint64_t backend=0,generation=0;if(!l || !o->validate(o->context,cpu,l->device,&backend,&generation) || backend!=b->backend || generation!=b->device_generation)return SATCHEL_GC_CHANGED;
 SatchelGuestPoolView cv={0},sv={0};uint32_t s=pool(satchel_guest_pools_lease_view(o->pools,cpu,b->cb_lease,&cv));if(s)return s;s=pool(satchel_guest_pools_lease_view(o->pools,cpu,b->storage_lease,&sv));if(s)return s;
 if(cv.guest_device!=l->device || sv.guest_device!=l->device)return SATCHEL_GC_CHANGED;
 s=pool(satchel_guest_pools_lease_binding(o->pools,cpu,b->cb_lease,b->input.cb_gpu,b->input.cb_size,cb));if(s)return s;s=pool(satchel_guest_pools_lease_binding(o->pools,cpu,b->storage_lease,b->input.storage_gpu,b->input.storage_size,storage));if(s)return s;
 uint64_t ca=cv.cpu_address+(b->input.cb_gpu-cv.gpu_address),sa=sv.cpu_address+(b->input.storage_gpu-sv.gpu_address);int overlap=1;
 if(!satchel_cpu_probe_check_range(cpu,ca,b->input.cb_size,SATCHEL_CPU_READ) || !satchel_cpu_probe_check_range(cpu,sa,b->input.storage_size,SATCHEL_CPU_READ|SATCHEL_CPU_WRITE) || !satchel_cpu_probe_physical_overlap(cpu,ca,b->input.cb_size,sa,b->input.storage_size,&overlap))return SATCHEL_GC_MEMORY;
 if(overlap)return SATCHEL_GC_UNSUPPORTED;
 return gp(satchel_guest_programs_code_disjoint(o->programs,cpu,b->input.program,sa,b->input.storage_size));
}
SatchelGuestCompute *satchel_guest_compute_create(SatchelGuestPools *pools,SatchelGuestPoolDeviceValidator validate,SatchelGuestPoolCheckedRangeGuard guard,void *context){if(!pools || !validate)return NULL;SatchelGuestCompute *o=calloc(1,sizeof(*o));if(!o)return NULL;o->pools=pools;o->validate=validate;o->context=context;atomic_flag_clear(&o->active);o->programs=satchel_guest_programs_create(pools,validate,guard,context);if(!o->programs){free(o);return NULL;}return o;}
uint32_t satchel_guest_compute_program_identity(SatchelGuestCompute *o,SatchelCPUProbe *cpu,uint64_t p,SatchelGuestProgramIdentity *out){if(!out || !cpu)return SATCHEL_GC_INVALID;uint32_t s=enter(o,0);if(s)return s;s=gp(satchel_guest_programs_identity(o->programs,cpu,p,out));return leave(o,s);}
uint32_t satchel_guest_compute_program_initialize(SatchelGuestCompute *o,SatchelCPUProbe *cpu,uint64_t p,uint64_t d){uint32_t s=enter(o,0);if(s)return s;if(!cpu || !p || link_for(o,p))return leave(o,SATCHEL_GC_INVALID);Link *slot=NULL;for(unsigned i=0;i<8;i++)if(!o->links[i].object){slot=&o->links[i];break;}if(!slot)return leave(o,SATCHEL_GC_BUSY);s=gp(satchel_guest_programs_initialize(o->programs,cpu,p,d));if(!s)*slot=(Link){.object=p,.device=d};return leave(o,s);}
uint32_t satchel_guest_compute_program_capture(SatchelGuestCompute *o,SatchelCPUProbe *cpu,uint64_t p,int32_t count,uint64_t data){uint32_t s=enter(o,0);if(s)return s;if(held(o,p))return leave(o,SATCHEL_GC_BUSY);s=gp(satchel_guest_programs_set_shaders(o->programs,cpu,p,count,data));return leave(o,s);}
uint32_t satchel_guest_compute_program_finalize(SatchelGuestCompute *o,SatchelCPUProbe *cpu,uint64_t p){uint32_t s=enter(o,0);if(s)return s;if(held(o,p))return leave(o,SATCHEL_GC_BUSY);Link *l=link_for(o,p);if(!l)return leave(o,SATCHEL_GC_INVALID);s=gp(satchel_guest_programs_finalize(o->programs,cpu,p));if(!s)*l=(Link){0};return leave(o,s);}
uint32_t satchel_guest_compute_bind(SatchelGuestCompute *o,SatchelCPUProbe *cpu,const SatchelGuestComputeBinding *input){if(!input || !cpu)return SATCHEL_GC_INVALID;const SatchelGuestComputeBinding request=*input;uint32_t s=enter(o,0);if(s)return s;if(resources(o))return leave(o,SATCHEL_GC_BUSY);s=release_binding(o,&o->pending);if(s)return leave(o,s);
 if(request.cb_size<8 || request.cb_size>65536 || (request.cb_size&3) || !request.storage_size || request.storage_size>1048576 || (request.storage_size&3) || request.uniform_slot>14 || !request.groups_x || !request.groups_y || !request.groups_z)return leave(o,SATCHEL_GC_UNSUPPORTED);
 Link *l=link_for(o,request.program);if(!l)return leave(o,SATCHEL_GC_INVALID);SatchelGuestProgramMetadata meta={0};s=gp(satchel_guest_programs_metadata(o->programs,cpu,request.program,&meta));if(s)return leave(o,s);
 uint64_t backend=0,generation=0;if(!o->validate(o->context,cpu,l->device,&backend,&generation) || !backend || !generation)return leave(o,SATCHEL_GC_INVALID);
 Binding *b=&o->pending;*b=(Binding){.state=1,.input=request,.program_generation=meta.generation,.backend=backend,.device_generation=generation};SatchelGuestPoolView view={0};
 s=pool(satchel_guest_pools_retain_range(o->pools,cpu,l->device,request.cb_gpu,request.cb_size,&b->cb_lease,&view));if(s)goto fail;s=pool(satchel_guest_pools_retain_range(o->pools,cpu,l->device,request.storage_gpu,request.storage_size,&b->storage_lease,&view));if(s)goto fail;
 SatchelGuestPoolBinding cb={0},storage={0};s=spans(o,cpu,b,&cb,&storage);if(s)goto fail;s=release_binding(o,&o->binding);if(s)goto fail;o->binding=*b;memset(b,0,sizeof(*b));return leave(o,SATCHEL_GC_OK);
 fail:{uint32_t cleanup=release_binding(o,b);return leave(o,cleanup?cleanup:s);}
}
uint32_t satchel_guest_compute_unbind(SatchelGuestCompute *o){uint32_t s=enter(o,0);if(s)return s;if(resources(o))return leave(o,SATCHEL_GC_BUSY);s=release_binding(o,&o->pending);if(!s)s=release_binding(o,&o->binding);return leave(o,s);}
static int cancelled(SatchelCPUProbeCancelled callback,void *context){return callback && callback(context);}
static void report_run(SatchelGuestComputeReport *r,const Run *run){r->compiled=run->compiled;r->submitted=run->submitted;r->completed=run->completed;r->semantic_ops=run->semantic_ops;r->stores=run->stores;r->unused_memory_mask=run->unused;}
uint32_t satchel_guest_compute_submit(SatchelGuestCompute *o,SatchelCPUProbe *cpu,SatchelGuestComputeReport *report,SatchelCPUProbeCancelled cancel,void *ctx){
 if(!report)return SATCHEL_GC_INVALID;memset(report,0,sizeof(*report));uint32_t s=enter(o,0);if(s){report->status=s;return s;}report->stage=1;uint8_t *code=NULL,*control=NULL,*cbbytes=NULL,*verify=NULL,*ubo=NULL;uint32_t spirv[4096];SatchelScalarReflection reflection={0};SatchelGuestProgramMetadata meta={0};SatchelGuestPoolBinding cb={0},storage={0};SatchelVulkanComputeLimits limits={0};SatchelGuestDeviceCaps caps={0};SatchelVulkanBufferInfo info={0};Binding *b=&o->binding;Run *run=&o->run;
 if(!cpu || b->state!=1 || o->pending.state){s=SATCHEL_GC_INVALID;goto done;}if(resources(o)){s=SATCHEL_GC_BUSY;goto done;}if(cancelled(cancel,ctx)){s=SATCHEL_GC_CANCELLED;goto done;}
 s=spans(o,cpu,b,&cb,&storage);if(s)goto done;s=gp(satchel_guest_programs_metadata(o->programs,cpu,b->input.program,&meta));if(s)goto done;if(meta.generation!=b->program_generation){s=SATCHEL_GC_CHANGED;goto done;}if(meta.instruction_size>512){s=SATCHEL_GC_UNSUPPORTED;goto done;}
 code=malloc(65536);control=malloc(65536);cbbytes=malloc((size_t)b->input.cb_size);verify=malloc((size_t)b->input.cb_size);ubo=calloc(1,65536);if(!code || !control || !cbbytes || !verify || !ubo){s=SATCHEL_GC_ALLOCATION;goto done;}
 s=gp(satchel_guest_programs_copy(o->programs,cpu,b->input.program,code,65536,control,65536,&meta));if(s)goto done;s=vk(satchel_vulkan_buffer_read(b->backend,cb.buffer,cb.offset,cbbytes,(size_t)b->input.cb_size));if(s)goto done;
 s=vk(satchel_vulkan_compute_limits(b->backend,&limits));if(s)goto done;s=vk(satchel_vulkan_device_capabilities(b->backend,&caps));if(s)goto done;if(caps.api_version<((1u<<22)|(1u<<12))){s=SATCHEL_GC_UNSUPPORTED;goto done;}
 report->stage=2;SatchelScalarInput input={.version=1,.size=sizeof(input),.known=SATCHEL_SCALAR_KNOWN_WORKGROUP|SATCHEL_SCALAR_KNOWN_BINDINGS|SATCHEL_SCALAR_KNOWN_CAPS|SATCHEL_SCALAR_KNOWN_SHARED,
 .local_x=meta.local_x,.local_y=meta.local_y,.local_z=meta.local_z,.shared_bytes=meta.shared_memory_size,.group_x=b->input.groups_x,.group_y=b->input.groups_y,.group_z=b->input.groups_z,
 .shader_enabled=1,.max_uniform_range=limits.max_uniform_range,.max_storage_range=limits.max_storage_range,.max_uniform_descriptors=limits.max_stage_uniform_buffers<limits.max_set_uniform_buffers?limits.max_stage_uniform_buffers:limits.max_set_uniform_buffers,
 .max_storage_descriptors=limits.max_stage_storage_buffers<limits.max_set_storage_buffers?limits.max_stage_storage_buffers:limits.max_set_storage_buffers,.max_stage_resources=limits.max_stage_resources,
 .uniform_alignment=(uint32_t)limits.uniform_offset_alignment,.storage_alignment=(uint32_t)limits.storage_offset_alignment,.cb_slot=b->input.uniform_slot+3,.uniform_binding=1,.storage_binding=2,.storage_cb_disjoint=1,
 .code=code+meta.instruction_offset,.code_size=meta.instruction_size,.constant_buffer=cbbytes,.constant_buffer_size=(size_t)b->input.cb_size,.storage_address=b->input.storage_gpu,.storage_size=(uint32_t)b->input.storage_size};
 Link *admitted=link_for(o,b->input.program);if(!admitted || !admitted->admitted || admitted->generation!=meta.generation){s=SATCHEL_GC_UNSUPPORTED;goto done;}
 report->compiler_status=(uint32_t)satchel_scalar_compile_program(&admitted->program,&input,spirv,4096,&reflection);if(report->compiler_status){s=SATCHEL_GC_UNSUPPORTED;goto done;}if(cancelled(cancel,ctx)){s=SATCHEL_GC_CANCELLED;goto done;}
 run->device=b->backend;report->stage=3;s=vk(satchel_vulkan_scalar_pipeline_create(run->device,spirv,(size_t)reflection.spirv_words*4,&reflection,&run->pipeline,cancel,ctx));if(s)goto done;run->compiled=1;run->semantic_ops=reflection.semantic_ops;run->stores=reflection.store_count;run->unused=reflection.unused_memory_mask;
 report->stage=4;s=vk(satchel_vulkan_buffer_create(run->device,65536,SATCHEL_VULKAN_BUFFER_UNIFORM,&run->uniform,&info));if(s)goto done;memcpy(ubo,cbbytes,(size_t)b->input.cb_size);s=vk(satchel_vulkan_buffer_write(run->device,run->uniform,0,ubo,65536));if(s)goto done;
 // Final snapshot validation follows all compiler/driver callbacks. The composite
 // CPU/pool worker is serialized; no other guest execution occurs during submit.
 report->stage=5;if(cancelled(cancel,ctx)){s=SATCHEL_GC_CANCELLED;goto done;}s=spans(o,cpu,b,&cb,&storage);if(s)goto done;SatchelGuestProgramMetadata now={0};s=gp(satchel_guest_programs_metadata(o->programs,cpu,b->input.program,&now));if(s)goto done;if(now.generation!=meta.generation){s=SATCHEL_GC_CHANGED;goto done;}
 s=vk(satchel_vulkan_buffer_read(b->backend,cb.buffer,cb.offset,verify,(size_t)b->input.cb_size));if(s)goto done;if(memcmp(cbbytes,verify,(size_t)b->input.cb_size)){s=SATCHEL_GC_CHANGED;goto done;}
 SatchelVulkanScalarBindings bind={.buffers={run->uniform,0,65536,storage.buffer,storage.offset,b->input.storage_size},.storage_guest_address=b->input.storage_gpu,.guest_cb_storage_disjoint=1};
 report->stage=6;s=vk(satchel_vulkan_scalar_submit(run->device,run->pipeline,&bind,&run->job,cancel,ctx));if(run->job)run->submitted=1;
 done:report_run(report,run);free(code);free(control);free(cbbytes);free(verify);free(ubo);if(!run->job && resources(o)){uint32_t cleanup=clean_run(o,0,NULL,NULL,NULL);if(cleanup)s=cleanup;}report->status=s;report->resources_retained=resources(o);report->bindings_retained=o->binding.state || o->pending.state;return leave(o,s);
}
uint32_t satchel_guest_compute_wait(SatchelGuestCompute *o,uint32_t ms,SatchelGuestComputeReport *report,SatchelCPUProbeCancelled cancel,void *ctx){if(!report)return SATCHEL_GC_INVALID;memset(report,0,sizeof(*report));uint32_t s=enter(o,1);if(s){report->status=s;return s;}report->stage=7;report_run(report,&o->run);int completed=(int)o->run.completed;s=clean_run(o,ms,cancel,ctx,&completed);report->completed=(uint32_t)completed;report->status=s;report->resources_retained=resources(o);report->bindings_retained=o->binding.state || o->pending.state;return leave(o,s);}
int satchel_guest_compute_pool_guard(const SatchelGuestCompute *o,SatchelCPUProbe *cpu,uint64_t a,uint64_t n){return o && satchel_guest_programs_pool_guard(o->programs,cpu,a,n)==SATCHEL_GP_OK;}
uint32_t satchel_guest_compute_try_destroy(SatchelGuestCompute **pointer){if(!pointer)return SATCHEL_GC_INVALID;SatchelGuestCompute *o=*pointer;if(!o)return SATCHEL_GC_OK;uint32_t s=enter(o,1);if(s)return s;o->retiring=1;s=clean_run(o,0,NULL,NULL,NULL);if(!s)s=release_binding(o,&o->pending);if(!s)s=release_binding(o,&o->binding);if(!s)s=gp(satchel_guest_programs_try_destroy(&o->programs));if(s)return leave(o,s);free(o);*pointer=NULL;return SATCHEL_GC_OK;}

uint32_t satchel_guest_compute_device_children(SatchelGuestCompute *o,uint64_t device,uint32_t *count){if(!count)return SATCHEL_GC_INVALID;uint32_t s=enter(o,1);if(s)return s;s=gp(satchel_guest_programs_device_children(o->programs,device,count));return leave(o,s);}

typedef struct {SatchelScalarProgram program;SatchelVulkanComputeLimits limits;} Admission;
static SatchelGuestProgramStatus admit(void *context,const uint8_t *code,const SatchelGuestProgramMetadata *m){
 Admission *a=context;SatchelScalarProgramInput input={.version=1,.size=sizeof(input),.known=SATCHEL_SCALAR_KNOWN_WORKGROUP|SATCHEL_SCALAR_KNOWN_SHARED,
 .local_x=m->local_x,.local_y=m->local_y,.local_z=m->local_z,.shared_bytes=m->shared_memory_size,
 .code=code+m->instruction_offset,.code_size=m->instruction_size};
 if(satchel_scalar_admit(&input,&a->program)!=SATCHEL_SCALAR_OK)return SATCHEL_GP_UNSUPPORTED;
 const SatchelScalarRequirements *r=&a->program.requirements;const SatchelVulkanComputeLimits *l=&a->limits;
 if(r->cb_slot<3 || r->cb_slot>17 || r->uniform_declared_bytes>l->max_uniform_range || r->storage_required_bytes>l->max_storage_range || !l->max_stage_uniform_buffers || !l->max_set_uniform_buffers || !l->max_stage_storage_buffers || !l->max_set_storage_buffers || l->max_stage_resources<2)return SATCHEL_GP_UNSUPPORTED;
 return SATCHEL_GP_OK;
}
uint32_t satchel_guest_compute_program_admit(SatchelGuestCompute *o,SatchelCPUProbe *cpu,uint64_t p,int32_t count,uint64_t data){
 uint32_t s=enter(o,0);if(s)return s;Link *l=link_for(o,p);if(!l)return leave(o,SATCHEL_GC_INVALID);if(held(o,p))return leave(o,SATCHEL_GC_BUSY);
 uint64_t backend=0,device_generation=0;if(!o->validate(o->context,cpu,l->device,&backend,&device_generation))return leave(o,SATCHEL_GC_CHANGED);
 Admission a={0};SatchelGuestDeviceCaps caps={0};s=vk(satchel_vulkan_compute_limits(backend,&a.limits));if(s)return leave(o,s);s=vk(satchel_vulkan_device_capabilities(backend,&caps));if(s)return leave(o,s);if(caps.api_version<((1u<<22)|(1u<<12)))return leave(o,SATCHEL_GC_UNSUPPORTED);
 uint64_t generation=0;s=gp(satchel_guest_programs_set_shaders_admitted(o->programs,cpu,p,count,data,admit,&a,&generation));
 if(!s){l->program=a.program;l->generation=generation;l->admitted=1;}return leave(o,s);
}
uint32_t satchel_guest_compute_program_retain(SatchelGuestCompute *o,SatchelCPUProbe *cpu,uint64_t p,uint64_t device,SatchelScalarRequirements *requirements){
 if(!requirements)return SATCHEL_GC_INVALID;uint32_t s=enter(o,0);if(s)return s;Link *l=link_for(o,p);if(!l || !l->admitted || l->device!=device)return leave(o,SATCHEL_GC_UNSUPPORTED);
 SatchelGuestProgramMetadata m={0};s=gp(satchel_guest_programs_metadata(o->programs,cpu,p,&m));if(!s && m.generation!=l->generation)s=SATCHEL_GC_CHANGED;if(!s && l->references==UINT32_MAX)s=SATCHEL_GC_BUSY;
 if(!s){++l->references;*requirements=l->program.requirements;}return leave(o,s);
}
uint32_t satchel_guest_compute_program_release(SatchelGuestCompute *o,uint64_t p){uint32_t s=enter(o,1);if(s)return s;Link *l=link_for(o,p);if(!l || !l->references)return leave(o,SATCHEL_GC_INVALID);--l->references;return leave(o,SATCHEL_GC_OK);}
