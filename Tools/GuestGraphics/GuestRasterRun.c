#include "GuestRasterRun.h"
#include "VulkanBuffer.h"
#include "VulkanSubmission.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
typedef struct {uint64_t lease,generation;uint32_t held;} Held;
struct SatchelGuestRasterRun {
 atomic_flag active;SatchelGuestPools *pools;SatchelGuestTextures *textures;SatchelGuestRasterPrograms *programs;
 uint64_t device,texture_ref,program_refs[8],pipelines[8],temporary_leases[8],staging,job;uint32_t program_count,pipeline_count,phase,end_status,cancelled,upload_done,draw_done,published;
 SatchelGuestTextureBinding texture;SatchelVulkanRasterBatch batch;Held held[8];uint32_t held_count;
 uint8_t *tiled,*linear;uint64_t tiled_size,linear_size,host_bytes;
};
static _Atomic uint32_t live_runs;static _Atomic uint64_t host_bytes;
static int reserve_run(void){uint32_t n=atomic_load(&live_runs);do{if(n==SATCHEL_GRUN_LIMIT)return 0;}while(!atomic_compare_exchange_weak(&live_runs,&n,n+1));return 1;}
static int reserve_host(uint64_t size){uint64_t n=atomic_load(&host_bytes);do{if(size>SATCHEL_GRUN_HOST_LIMIT || n>SATCHEL_GRUN_HOST_LIMIT-size)return 0;}while(!atomic_compare_exchange_weak(&host_bytes,&n,n+size));return 1;}
static uint32_t vk(SatchelVulkanStatus s){if(s==SATCHEL_VULKAN_OK)return SATCHEL_GRUN_OK;if(s==SATCHEL_VULKAN_BUSY || s==SATCHEL_VULKAN_TIMEOUT)return SATCHEL_GRUN_BUSY;if(s==SATCHEL_VULKAN_UNSUPPORTED)return SATCHEL_GRUN_UNSUPPORTED;if(s==SATCHEL_VULKAN_CANCELLED)return SATCHEL_GRUN_CANCELLED;return SATCHEL_GRUN_BACKEND;}
static uint32_t pool(uint32_t s){if(!s)return SATCHEL_GRUN_OK;if(s==SATCHEL_GUEST_POOL_BUSY)return SATCHEL_GRUN_BUSY;if(s==SATCHEL_GUEST_POOL_MEMORY)return SATCHEL_GRUN_MEMORY;if(s==SATCHEL_GUEST_POOL_ALLOCATION)return SATCHEL_GRUN_ALLOCATION;return SATCHEL_GRUN_CHANGED;}
static uint32_t program(uint32_t s){if(!s)return SATCHEL_GRUN_OK;if(s==SATCHEL_GRP_BUSY)return SATCHEL_GRUN_BUSY;if(s==SATCHEL_GRP_UNSUPPORTED)return SATCHEL_GRUN_UNSUPPORTED;if(s==SATCHEL_GRP_ALLOCATION)return SATCHEL_GRUN_ALLOCATION;return SATCHEL_GRUN_CHANGED;}
static uint32_t texture(uint32_t s){if(!s)return SATCHEL_GRUN_OK;if(s==SATCHEL_GTEX_BUSY)return SATCHEL_GRUN_BUSY;if(s==SATCHEL_GTEX_UNSUPPORTED)return SATCHEL_GRUN_UNSUPPORTED;return SATCHEL_GRUN_CHANGED;}
static int disjoint(SatchelCPUProbe *cpu,uint64_t a,uint64_t n,uint64_t b,uint64_t m){int overlap=1;return satchel_cpu_probe_physical_overlap(cpu,a,n,b,m,&overlap) && !overlap;}
static uint32_t retain_pool(SatchelGuestRasterRun *r,SatchelCPUProbe *cpu,const SatchelGuestPoolView *view){
 for(unsigned i=0;i<r->held_count;i++)if(r->held[i].generation==view->generation)return SATCHEL_GRUN_OK;
 if(r->held_count==8)return SATCHEL_GRUN_UNSUPPORTED;Held *h=&r->held[r->held_count];SatchelGuestPoolView actual;uint32_t s=pool(satchel_guest_pools_retain_object(r->pools,cpu,r->texture.guest_device,view->object,&h->lease,&actual));if(s)return s;h->generation=actual.generation;++r->held_count;
 return actual.generation==view->generation && actual.cpu_address==view->cpu_address && actual.gpu_address==view->gpu_address && actual.size==view->size?SATCHEL_GRUN_OK:SATCHEL_GRUN_CHANGED;
}
static uint32_t cleanup(SatchelGuestRasterRun *r){
 if(r->job || (r->draw_done && !r->published))return SATCHEL_GRUN_BUSY;
 for(unsigned i=0;i<r->program_count;i++)if(r->pipelines[i]){uint32_t s=vk(satchel_vulkan_raster_destroy(r->device,r->pipelines[i]));if(s)return s;r->pipelines[i]=0;}
 if(r->staging){uint32_t s=vk(satchel_vulkan_buffer_destroy(r->device,r->staging));if(s)return s;r->staging=0;}
 for(unsigned i=0;i<r->held_count;i++){Held *h=&r->held[i];if(h->held){uint32_t s=pool(satchel_guest_pools_gpu_release(r->pools,h->lease));if(s)return s;h->held=0;}if(h->lease){uint32_t s=pool(satchel_guest_pools_release(r->pools,h->lease));if(s)return s;h->lease=0;}}
 for(unsigned i=0;i<r->program_count;i++)if(r->temporary_leases[i]){uint32_t s=pool(satchel_guest_pools_release(r->pools,r->temporary_leases[i]));if(s)return s;r->temporary_leases[i]=0;}
 for(unsigned i=0;i<r->program_count;i++)if(r->program_refs[i]){uint32_t s=program(satchel_guest_raster_programs_release(r->programs,r->program_refs[i]));if(s)return s;r->program_refs[i]=0;}
 if(r->texture_ref){uint32_t s=texture(satchel_guest_textures_release(r->textures,r->texture_ref));if(s)return s;r->texture_ref=0;}
 free(r->tiled);free(r->linear);r->tiled=r->linear=NULL;if(r->host_bytes){atomic_fetch_sub(&host_bytes,r->host_bytes);r->host_bytes=0;}return SATCHEL_GRUN_OK;
}
static uint32_t prepare(SatchelGuestRasterRun *r,SatchelCPUProbe *cpu,const SatchelGuestRasterRequest *input,SatchelVulkanCancelled cancel,void *context){
 const SatchelGuestRasterRequest request=*input;if(cancel && cancel(context))return SATCHEL_GRUN_CANCELLED;
 uint32_t s=texture(satchel_guest_textures_retain(r->textures,cpu,request.texture,&r->texture_ref,&r->texture));if(s)return s;if(r->texture.generation!=request.texture_generation)return SATCHEL_GRUN_CHANGED;r->device=r->texture.backend_device;
 SatchelGuestPoolView target_view;s=pool(satchel_guest_pools_lease_view(r->pools,cpu,r->texture.pool_lease,&target_view));if(s)return s;s=retain_pool(r,cpu,&target_view);if(s)return s;
 r->batch=(SatchelVulkanRasterBatch){.version=1,.size=sizeof(r->batch),.load_operation=request.load_operation,.command_count=request.command_count,.row_pitch=(uint64_t)r->texture.layout.description.width*4};memcpy(r->batch.initial_clear_rgba,request.initial_clear_rgba,16);
 for(unsigned i=0;i<request.command_count;i++){
  if(cancel && cancel(context))return SATCHEL_GRUN_CANCELLED;const SatchelGuestRasterCommand *c=&request.commands[i];SatchelVulkanRasterCommand *draw=&r->batch.commands[i];*draw=c->draw;
  if(c->draw.pipeline || c->draw.vertex_buffer || c->draw.vertex_offset || c->operation!=c->draw.operation)return SATCHEL_GRUN_INVALID;
  if(c->operation==SATCHEL_VULKAN_RASTER_OP_CLEAR){if(c->program || c->vertex_gpu || c->vertex_size)return SATCHEL_GRUN_INVALID;continue;}
  if(c->operation!=SATCHEL_VULKAN_RASTER_OP_DRAW || r->program_count==8 || !c->vertex_size || c->vertex_gpu>UINT64_MAX-c->vertex_size)return SATCHEL_GRUN_UNSUPPORTED;
  unsigned index=r->program_count;SatchelGuestRasterProgramInfo info;s=program(satchel_guest_raster_programs_retain(r->programs,cpu,c->program,&r->program_refs[index],&info));if(s)return s;++r->program_count;
  if(info.program_generation!=c->program_generation || info.capture_generation!=c->capture_generation)return SATCHEL_GRUN_CHANGED;
  s=program(satchel_guest_raster_programs_code_disjoint(r->programs,cpu,r->program_refs[index],r->texture.storage_cpu,r->texture.layout.storage_size));if(s)return s;
  SatchelGuestRasterCodeBinding code[2];s=program(satchel_guest_raster_programs_code_bindings(r->programs,cpu,r->program_refs[index],code));if(s)return s;
  for(unsigned j=0;j<2;j++){s=retain_pool(r,cpu,&code[j].pool);if(s)return s;}
  uint64_t vertex_lease=0;SatchelGuestPoolView vertices;s=pool(satchel_guest_pools_retain_range(r->pools,cpu,r->texture.guest_device,c->vertex_gpu,c->vertex_size,&vertex_lease,&vertices));if(s)return s;
  r->temporary_leases[index]=vertex_lease;SatchelGuestPoolBinding vertex_binding;s=pool(satchel_guest_pools_lease_binding(r->pools,cpu,vertex_lease,c->vertex_gpu,c->vertex_size,&vertex_binding));
  // Keep this temporary lease in the run before any subsequent fallible call.
  unsigned slot=8;for(unsigned j=0;j<r->held_count;j++)if(r->held[j].generation==vertices.generation){slot=j;break;}
  if(slot==8){if(r->held_count==8)return SATCHEL_GRUN_UNSUPPORTED;r->held[r->held_count++]=(Held){vertex_lease,vertices.generation,0};r->temporary_leases[index]=0;}
  if(s)return s;if(!disjoint(cpu,r->texture.storage_cpu,r->texture.layout.storage_size,vertices.cpu_address+vertex_binding.offset,c->vertex_size))return SATCHEL_GRUN_UNSUPPORTED;
  SatchelRasterModule vs,fs;s=program(satchel_guest_raster_programs_compile(r->programs,cpu,r->program_refs[index],&c->pipeline.state,&vs,&fs,&info));if(s)return s;
  SatchelVulkanRasterInfo pipeline;s=vk(satchel_vulkan_raster_create(r->device,&c->pipeline,&vs,&fs,&r->pipelines[index],&pipeline));if(s)return s;++r->pipeline_count;
  // Independent whole-vec4 range proof against the bound guest span, not only
  // the backend's larger pool buffer. Checked first_vertex is currently zero.
  if(c->draw.first_vertex || c->draw.vertex_count<3 || c->draw.vertex_count>4096 || c->draw.vertex_count%3)return SATCHEL_GRUN_UNSUPPORTED;
  uint64_t tail=0;for(unsigned j=0;j<8;j++)if(pipeline.attribute_mask&(1u<<j)){uint64_t end=(uint64_t)c->pipeline.attribute_offsets[j]+16;if(end>tail)tail=end;}
  uint64_t span=(uint64_t)(c->draw.vertex_count-1)*c->pipeline.vertex_stride+tail;if(span>c->vertex_size)return SATCHEL_GRUN_MEMORY;
  draw->pipeline=r->pipelines[index];draw->vertex_buffer=vertex_binding.buffer;draw->vertex_offset=vertex_binding.offset;
 }
 // Revalidate every captured input after the last inter-command cancellation
 // callback; an earlier pipeline must not outlive changed GPU-visible code.
 for(unsigned i=0;i<r->program_count;i++){SatchelGuestRasterCodeBinding code[2];s=program(satchel_guest_raster_programs_code_bindings(r->programs,cpu,r->program_refs[i],code));if(s)return s;}
 SatchelGuestTextureBinding current;s=texture(satchel_guest_textures_binding(r->textures,cpu,r->texture_ref,&current));if(s)return s;
 if(current.generation!=r->texture.generation || current.target!=r->texture.target || current.storage_cpu!=r->texture.storage_cpu || current.storage_gpu!=r->texture.storage_gpu)return SATCHEL_GRUN_CHANGED;
 // No CPU execution/callback may interleave this serialized final validation
 // and hold acquisition. Backend cancellation callbacks must remain pure.
 for(unsigned i=0;i<r->held_count;i++){s=pool(satchel_guest_pools_gpu_acquire(r->pools,cpu,r->held[i].lease));if(s)return s;r->held[i].held=1;}
 r->tiled_size=r->texture.layout.storage_size;r->linear_size=r->batch.row_pitch*r->texture.layout.description.height;
 if(!reserve_host(r->tiled_size+r->linear_size))return SATCHEL_GRUN_BUSY;r->host_bytes=r->tiled_size+r->linear_size;r->tiled=malloc((size_t)r->tiled_size);r->linear=malloc((size_t)r->linear_size);if(!r->tiled || !r->linear)return SATCHEL_GRUN_ALLOCATION;
 s=vk(satchel_vulkan_buffer_read(r->device,r->texture.binding.buffer,r->texture.binding.offset,r->tiled,(size_t)r->tiled_size));if(s)return s;
 if(satchel_guest_texture_decode(&r->texture.layout,r->tiled,r->tiled_size,r->linear,r->linear_size,r->batch.row_pitch)!=SATCHEL_GTL_OK)return SATCHEL_GRUN_INTERNAL;
 SatchelVulkanBufferInfo buffer;s=vk(satchel_vulkan_buffer_create(r->device,r->linear_size,SATCHEL_VULKAN_BUFFER_TRANSFER_SOURCE|SATCHEL_VULKAN_BUFFER_TRANSFER_DESTINATION,&r->staging,&buffer));if(s)return s;r->batch.readback_buffer=r->staging;
 s=vk(satchel_vulkan_buffer_write(r->device,r->staging,0,r->linear,(size_t)r->linear_size));if(s)return s;
 s=vk(satchel_vulkan_target_upload(r->device,r->texture.target,r->staging,0,r->batch.row_pitch,&r->job,cancel,context));if(r->job)r->phase=1;return s;
}
uint32_t satchel_guest_raster_run_begin(SatchelGuestPools *p,SatchelGuestTextures *t,SatchelGuestRasterPrograms *g,SatchelCPUProbe *cpu,const SatchelGuestRasterRequest *request,SatchelGuestRasterRun **out,SatchelVulkanCancelled cancel,void *context){
 if(!out)return SATCHEL_GRUN_INVALID;*out=NULL;if(!p || !t || !g || !cpu || !request || request->version!=1 || request->size!=sizeof(*request) || request->command_count>16 || (request->load_operation!=SATCHEL_VULKAN_RASTER_LOAD && request->load_operation!=SATCHEL_VULKAN_RASTER_CLEAR))return SATCHEL_GRUN_INVALID;
 if(!reserve_run())return SATCHEL_GRUN_BUSY;SatchelGuestRasterRun *r=calloc(1,sizeof(*r));if(!r){atomic_fetch_sub(&live_runs,1);return SATCHEL_GRUN_ALLOCATION;}atomic_flag_clear(&r->active);r->pools=p;r->textures=t;r->programs=g;*out=r;
 atomic_flag_test_and_set(&r->active);uint32_t s=prepare(r,cpu,request,cancel,context);if(s){r->end_status=s;if(s==SATCHEL_GRUN_CANCELLED)r->cancelled=1;}
 if(!r->job){if(!s)s=r->end_status=SATCHEL_GRUN_INTERNAL;uint32_t released=cleanup(r);if(!released){free(r);*out=NULL;atomic_fetch_sub(&live_runs,1);return s;}atomic_flag_clear(&r->active);return released;}
 atomic_flag_clear(&r->active);return s;
}
static void report(SatchelGuestRasterRun *r,uint32_t status,SatchelGuestRasterRunReport *out){*out=(SatchelGuestRasterRunReport){status,r->phase,r->upload_done,r->draw_done,r->published,r->cancelled,r->pipeline_count,1,r->host_bytes};}
uint32_t satchel_guest_raster_run_poll(SatchelGuestRasterRun **pointer,uint32_t timeout,int cancel,SatchelGuestRasterRunReport *out){
 if(!pointer || !*pointer || !out || timeout>5000)return SATCHEL_GRUN_INVALID;SatchelGuestRasterRun *r=*pointer;if(atomic_flag_test_and_set(&r->active))return SATCHEL_GRUN_BUSY;if(cancel)r->cancelled=1;uint32_t s=SATCHEL_GRUN_OK;
 if(r->job){SatchelVulkanSubmissionReport done;s=vk(satchel_vulkan_submission_wait(r->device,r->job,timeout,NULL,NULL,&done));if(s || !done.completed){if(!s)s=SATCHEL_GRUN_BUSY;goto retained;}r->job=0;if(r->phase==1)r->upload_done=1;else if(r->phase==2)r->draw_done=1;else {s=SATCHEL_GRUN_INTERNAL;goto retained;}}
 if(r->phase==1 && r->upload_done && !r->cancelled && !r->end_status){s=vk(satchel_vulkan_raster_batch_readback(r->device,r->texture.target,&r->batch,&r->job,NULL,NULL));if(r->job){r->phase=2;if(s)r->end_status=s;else s=SATCHEL_GRUN_BUSY;goto retained;}r->end_status=s?s:SATCHEL_GRUN_INTERNAL;}
 if(r->draw_done && !r->published){r->phase=3;s=vk(satchel_vulkan_buffer_read(r->device,r->staging,0,r->linear,(size_t)r->linear_size));if(s)goto retained;
  if(satchel_guest_texture_encode(&r->texture.layout,r->linear,r->linear_size,r->batch.row_pitch,r->tiled,r->tiled_size)!=SATCHEL_GTL_OK){s=SATCHEL_GRUN_INTERNAL;goto retained;}
  s=vk(satchel_vulkan_buffer_write(r->device,r->texture.binding.buffer,r->texture.binding.offset,r->tiled,(size_t)r->tiled_size));if(s)goto retained;r->published=1;
 }
 s=cleanup(r);if(s)goto retained;r->phase=4;s=r->end_status?r->end_status:(r->cancelled?SATCHEL_GRUN_CANCELLED:SATCHEL_GRUN_OK);report(r,s,out);out->resources_retained=0;free(r);*pointer=NULL;atomic_fetch_sub(&live_runs,1);return s;
 retained:report(r,s,out);atomic_flag_clear(&r->active);return s;
}
uint32_t satchel_guest_raster_run_stats(SatchelGuestRasterRunStats *out){if(!out)return SATCHEL_GRUN_INVALID;*out=(SatchelGuestRasterRunStats){atomic_load(&live_runs),SATCHEL_GRUN_LIMIT,atomic_load(&host_bytes),SATCHEL_GRUN_HOST_LIMIT};return SATCHEL_GRUN_OK;}

uint32_t satchel_guest_raster_run_peek(SatchelGuestRasterRun *r,SatchelGuestRasterRunReport *out){if(!r||!out)return SATCHEL_GRUN_INVALID;if(atomic_flag_test_and_set(&r->active))return SATCHEL_GRUN_BUSY;report(r,r->end_status,out);atomic_flag_clear(&r->active);return SATCHEL_GRUN_OK;}
