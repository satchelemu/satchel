#include "GuestTextureFrames.h"
#include "VulkanBuffer.h"
#include "VulkanRenderTarget.h"
#include "VulkanSubmission.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
struct SatchelGuestTextureFrames {
 SatchelGuestPools *pools;SatchelGuestTextures *textures;SatchelGuestFrames *frames;
 SatchelGuestTextureBinding texture;SatchelGuestPoolBinding binding;
 uint64_t texture_ref,lease,staging,job,ticket,epoch,host_bytes,tiled_size,linear_size;
 uint8_t *tiled,*linear;uint32_t held,phase,end_status,retiring,detached,upload_done,snapshot_done,snapshot_drained,leased,format,color_space,origin;
};
static atomic_flag active=ATOMIC_FLAG_INIT;
static SatchelGuestTextureFrames *owners[SATCHEL_GTF_LIMIT];
static uint64_t host_bytes;
static int enter(void){return !atomic_flag_test_and_set(&active);}
static SatchelGuestTextureFrameStatus leave(SatchelGuestTextureFrameStatus s){atomic_flag_clear(&active);return s;}
static int valid(SatchelGuestTextureFrames*r){for(unsigned i=0;i<SATCHEL_GTF_LIMIT;i++)if(owners[i]&&owners[i]==r)return 1;return 0;}
static SatchelGuestTextureFrameStatus vk(uint32_t s){return s==SATCHEL_VULKAN_OK?SATCHEL_GTF_OK:s==SATCHEL_VULKAN_BUSY||s==SATCHEL_VULKAN_TIMEOUT?SATCHEL_GTF_BUSY:s==SATCHEL_VULKAN_CANCELLED?SATCHEL_GTF_CANCELLED:s==SATCHEL_VULKAN_UNSUPPORTED?SATCHEL_GTF_UNSUPPORTED:SATCHEL_GTF_BACKEND;}
static SatchelGuestTextureFrameStatus pool(uint32_t s){return s==SATCHEL_GUEST_POOL_OK?SATCHEL_GTF_OK:s==SATCHEL_GUEST_POOL_BUSY?SATCHEL_GTF_BUSY:s==SATCHEL_GUEST_POOL_MEMORY?SATCHEL_GTF_MEMORY:s==SATCHEL_GUEST_POOL_ALLOCATION?SATCHEL_GTF_ALLOCATION:SATCHEL_GTF_CHANGED;}
static SatchelGuestTextureFrameStatus texture(uint32_t s){return s==SATCHEL_GTEX_OK?SATCHEL_GTF_OK:s==SATCHEL_GTEX_BUSY?SATCHEL_GTF_BUSY:s==SATCHEL_GTEX_MEMORY?SATCHEL_GTF_MEMORY:s==SATCHEL_GTEX_UNSUPPORTED?SATCHEL_GTF_UNSUPPORTED:SATCHEL_GTF_CHANGED;}
static SatchelGuestTextureFrameStatus frame(uint32_t s){return s==SATCHEL_FRAME_OK?SATCHEL_GTF_OK:s==SATCHEL_FRAME_BUSY||s==SATCHEL_FRAME_PENDING?SATCHEL_GTF_BUSY:s==SATCHEL_FRAME_CANCELLED?SATCHEL_GTF_CANCELLED:s==SATCHEL_FRAME_NOT_READY?SATCHEL_GTF_NOT_READY:s==SATCHEL_FRAME_ALLOCATION?SATCHEL_GTF_ALLOCATION:s==SATCHEL_FRAME_LIMIT?SATCHEL_GTF_LIMIT_REACHED:s==SATCHEL_FRAME_UNSUPPORTED?SATCHEL_GTF_UNSUPPORTED:s==SATCHEL_FRAME_INVALID||s==SATCHEL_FRAME_NO_SURFACE?SATCHEL_GTF_INVALID:SATCHEL_GTF_BACKEND;}
static int gpu_owned(const SatchelGuestTextureFrames*r){return r->texture_ref||r->lease||r->held||r->staging||r->job||(r->ticket&&!r->snapshot_drained);}
static SatchelGuestTextureFrameStatus detach(SatchelGuestTextureFrames*r){r->retiring=1;if(!r->end_status)r->end_status=SATCHEL_GTF_CANCELLED;r->phase=SATCHEL_GTF_RETIRING;if(r->frames&&!r->detached){SatchelGuestTextureFrameStatus s=frame(satchel_guest_frames_detach(r->frames,r->epoch));if(s)return s;r->detached=1;}return SATCHEL_GTF_OK;}
static SatchelGuestTextureFrameStatus cleanup(SatchelGuestTextureFrames*r){
 if(r->job||(r->ticket&&!r->snapshot_drained))return SATCHEL_GTF_BUSY;
 if(r->staging){SatchelGuestTextureFrameStatus s=vk(satchel_vulkan_buffer_destroy(r->texture.backend_device,r->staging));if(s)return s;r->staging=0;}
 if(r->held){SatchelGuestTextureFrameStatus s=pool(satchel_guest_pools_gpu_release(r->pools,r->lease));if(s)return s;r->held=0;}
 if(r->lease){SatchelGuestTextureFrameStatus s=pool(satchel_guest_pools_release(r->pools,r->lease));if(s)return s;r->lease=0;}
 if(r->texture_ref){SatchelGuestTextureFrameStatus s=texture(satchel_guest_textures_release(r->textures,r->texture_ref));if(s)return s;r->texture_ref=0;}
 free(r->tiled);free(r->linear);r->tiled=r->linear=NULL;if(r->host_bytes){host_bytes-=r->host_bytes;r->host_bytes=0;}return SATCHEL_GTF_OK;
}
static SatchelGuestTextureFrameStatus prepare(SatchelGuestTextureFrames*r,SatchelCPUProbe*cpu,const SatchelGuestTextureFrameRequest*q,SatchelFrameCancelled cancel,void*ctx){
 if(cancel&&cancel(ctx))return SATCHEL_GTF_CANCELLED;
 SatchelGuestTextureFrameStatus s=texture(satchel_guest_textures_retain(r->textures,cpu,q->texture,&r->texture_ref,&r->texture));if(s)return s;
 if(r->texture.generation!=q->texture_generation)return SATCHEL_GTF_CHANGED;
 r->tiled_size=r->texture.layout.storage_size;uint64_t width=r->texture.layout.description.width,height=r->texture.layout.description.height;
 if(!width||!height||width>2048||height>2048||r->texture.layout.description.format!=SATCHEL_GTL_RGBA8||width>UINT64_MAX/4||height>UINT64_MAX/(width*4))return SATCHEL_GTF_UNSUPPORTED;r->linear_size=width*4*height;
 if(!r->tiled_size||r->tiled_size>SATCHEL_GTL_MAX_STORAGE||r->linear_size>SATCHEL_FRAME_MAX_BYTES||r->tiled_size>UINT64_MAX-r->linear_size)return SATCHEL_GTF_LIMIT_REACHED;
 uint64_t charge=r->tiled_size+r->linear_size;if(charge>SATCHEL_GTF_STAGING_PER_OWNER||host_bytes>SATCHEL_GTF_STAGING_TOTAL||charge>SATCHEL_GTF_STAGING_TOTAL-host_bytes)return SATCHEL_GTF_LIMIT_REACHED;
 SatchelGuestPoolView view={0};s=pool(satchel_guest_pools_retain_object(r->pools,cpu,r->texture.guest_device,r->texture.pool_object,&r->lease,&view));if(s)return s;
 if(view.generation!=r->texture.pool_generation)return SATCHEL_GTF_CHANGED;s=pool(satchel_guest_pools_lease_binding(r->pools,cpu,r->lease,r->texture.storage_gpu,r->tiled_size,&r->binding));if(s)return s;
 if(r->binding.buffer!=r->texture.binding.buffer||r->binding.offset!=r->texture.binding.offset||r->binding.size!=r->tiled_size||r->binding.pool_generation!=r->texture.pool_generation)return SATCHEL_GTF_CHANGED;
 s=frame(satchel_guest_frames_create(r->epoch,&r->frames));if(s)return s;if(cancel&&cancel(ctx))return SATCHEL_GTF_CANCELLED;
 SatchelGuestTextureBinding current={0};s=texture(satchel_guest_textures_binding(r->textures,cpu,r->texture_ref,&current));if(s)return s;
 if(current.generation!=r->texture.generation||current.device_generation!=r->texture.device_generation||current.backend_device!=r->texture.backend_device||current.target!=r->texture.target||current.storage_cpu!=r->texture.storage_cpu||current.storage_gpu!=r->texture.storage_gpu||current.pool_generation!=r->texture.pool_generation||current.binding.buffer!=r->binding.buffer||current.binding.offset!=r->binding.offset||current.layout.storage_size!=r->tiled_size)return SATCHEL_GTF_CHANGED;
 // No callback or guest operation interleaves final validation and hold.
 s=pool(satchel_guest_pools_gpu_acquire(r->pools,cpu,r->lease));if(s)return s;r->held=1;host_bytes+=charge;r->host_bytes=charge;
 r->tiled=malloc((size_t)r->tiled_size);r->linear=malloc((size_t)r->linear_size);if(!r->tiled||!r->linear)return SATCHEL_GTF_ALLOCATION;
 s=vk(satchel_vulkan_buffer_read(r->texture.backend_device,r->binding.buffer,r->binding.offset,r->tiled,(size_t)r->tiled_size));if(s)return s;
 if(satchel_guest_texture_decode(&r->texture.layout,r->tiled,(size_t)r->tiled_size,r->linear,(size_t)r->linear_size,width*4)!=SATCHEL_GTL_OK)return SATCHEL_GTF_UNSUPPORTED;
 SatchelVulkanBufferInfo info={0};s=vk(satchel_vulkan_buffer_create(r->texture.backend_device,r->linear_size,SATCHEL_VULKAN_BUFFER_TRANSFER_SOURCE,&r->staging,&info));if(s)return s;
 s=vk(satchel_vulkan_buffer_write(r->texture.backend_device,r->staging,0,r->linear,(size_t)r->linear_size));if(s)return s;
 s=vk(satchel_vulkan_target_upload(r->texture.backend_device,r->texture.target,r->staging,0,width*4,&r->job,cancel,ctx));if(r->job)r->phase=SATCHEL_GTF_UPLOADING;else if(!s)s=SATCHEL_GTF_BACKEND;return s;
}
static SatchelGuestTextureFrameStatus progress(SatchelGuestTextureFrames*r,uint32_t timeout,int cancel){
 SatchelGuestTextureFrameStatus s=SATCHEL_GTF_OK;if(cancel){if(!r->end_status)r->end_status=SATCHEL_GTF_CANCELLED;s=detach(r);if(s)return s;}
 if(r->job){SatchelVulkanSubmissionReport done={0};s=vk(satchel_vulkan_submission_wait(r->texture.backend_device,r->job,timeout,NULL,NULL,&done));if(s)return s;if(!done.completed||done.resources_retained)return SATCHEL_GTF_BACKEND;r->job=0;r->upload_done=1;}
 if(!r->retiring&&!r->end_status&&r->upload_done&&!r->ticket){SatchelFrameSource source={1,sizeof(source),r->epoch,r->texture.backend_device,r->texture.target,r->format,r->color_space,r->origin};s=frame(satchel_guest_frames_begin(r->frames,&source,&r->ticket,NULL,NULL));if(r->ticket)r->phase=SATCHEL_GTF_SNAPSHOTTING;if(s){r->end_status=s;SatchelGuestTextureFrameStatus d=detach(r);if(d)return d;}else if(!r->ticket){r->end_status=SATCHEL_GTF_BACKEND;s=detach(r);if(s)return s;}if(r->ticket)return s?s:SATCHEL_GTF_BUSY;}
 if(r->ticket&&!r->snapshot_drained){s=frame(satchel_guest_frames_poll(r->frames,r->ticket,timeout,NULL,NULL));if(s)return s;r->snapshot_drained=1;if(!r->retiring&&!r->end_status)r->snapshot_done=1;}
 s=cleanup(r);if(s)return s;if(!r->retiring&&!r->end_status&&r->snapshot_done){r->phase=SATCHEL_GTF_READY;return SATCHEL_GTF_OK;}return r->end_status?(SatchelGuestTextureFrameStatus)r->end_status:SATCHEL_GTF_OK;
}
static void report(const SatchelGuestTextureFrames*r,uint32_t status,SatchelGuestTextureFrameReport*out){*out=(SatchelGuestTextureFrameReport){status,r->phase,r->upload_done,r->snapshot_done,gpu_owned(r),r->leased,r->retiring,r->epoch,r->host_bytes};}
SatchelGuestTextureFrameStatus satchel_guest_texture_frames_begin(SatchelGuestPools*p,SatchelGuestTextures*t,SatchelCPUProbe*cpu,const SatchelGuestTextureFrameRequest*input,SatchelGuestTextureFrames**out,SatchelFrameCancelled cancel,void*ctx){
 if(!out||((uintptr_t)out%_Alignof(SatchelGuestTextureFrames*)))return SATCHEL_GTF_INVALID;SatchelGuestTextureFrameRequest q={0};int aligned=input&&!((uintptr_t)input%_Alignof(SatchelGuestTextureFrameRequest));if(aligned)q=*input;*out=NULL;if(!aligned||!p||!t||!cpu||q.version!=1||q.size!=sizeof(q)||!q.texture||!q.texture_generation||!q.epoch)return SATCHEL_GTF_INVALID;
 if(q.format!=SATCHEL_FRAME_RGBA8_UNORM||q.color_space!=SATCHEL_FRAME_LINEAR_SRGB||q.origin!=SATCHEL_FRAME_TOP_LEFT)return SATCHEL_GTF_UNSUPPORTED;if(!enter())return SATCHEL_GTF_BUSY;
 unsigned slot=SATCHEL_GTF_LIMIT;for(unsigned i=0;i<SATCHEL_GTF_LIMIT;i++)if(!owners[i]){slot=i;break;}if(slot==SATCHEL_GTF_LIMIT)return leave(SATCHEL_GTF_LIMIT_REACHED);
 SatchelGuestTextureFrames*r=calloc(1,sizeof(*r));if(!r)return leave(SATCHEL_GTF_ALLOCATION);r->pools=p;r->textures=t;r->epoch=q.epoch;r->format=q.format;r->color_space=q.color_space;r->origin=q.origin;r->phase=SATCHEL_GTF_PREPARING;owners[slot]=r;*out=r;
 SatchelGuestTextureFrameStatus s=prepare(r,cpu,&q,cancel,ctx);if(s){r->end_status=s;SatchelGuestTextureFrameStatus d=detach(r);if(!d&&!r->job){d=progress(r,0,0);if(!gpu_owned(r)&&r->frames){SatchelGuestTextureFrameStatus f=frame(satchel_guest_frames_try_destroy(&r->frames));if(f)d=f;}if(!gpu_owned(r)&&!r->frames){owners[slot]=NULL;free(r);*out=NULL;return leave(s);}}if(d)return leave(d);}return leave(s);
}
SatchelGuestTextureFrameStatus satchel_guest_texture_frames_poll(SatchelGuestTextureFrames*r,uint32_t timeout,int cancel,SatchelGuestTextureFrameReport*out){if(!out||((uintptr_t)out%_Alignof(SatchelGuestTextureFrameReport))||timeout>SATCHEL_FRAME_POLL_LIMIT_MS||cancel<0||cancel>1)return SATCHEL_GTF_INVALID;if(!enter())return SATCHEL_GTF_BUSY;if(!valid(r))return leave(SATCHEL_GTF_INVALID);SatchelGuestTextureFrameStatus s=progress(r,timeout,cancel);report(r,s,out);return leave(s);}
SatchelGuestTextureFrameStatus satchel_guest_texture_frames_peek(SatchelGuestTextureFrames*r,SatchelGuestTextureFrameReport*out){if(!out||((uintptr_t)out%_Alignof(SatchelGuestTextureFrameReport)))return SATCHEL_GTF_INVALID;if(!enter())return SATCHEL_GTF_BUSY;if(!valid(r))return leave(SATCHEL_GTF_INVALID);report(r,r->end_status,out);return leave(SATCHEL_GTF_OK);}
SatchelGuestTextureFrameStatus satchel_guest_texture_frames_take(SatchelGuestTextureFrames*r,uint64_t epoch,SatchelFrameInfo*out){if(!out||((uintptr_t)out%_Alignof(SatchelFrameInfo)))return SATCHEL_GTF_INVALID;if(!enter())return SATCHEL_GTF_BUSY;if(!valid(r)||r->retiring||epoch!=r->epoch)return leave(SATCHEL_GTF_INVALID);if(r->phase!=SATCHEL_GTF_READY||gpu_owned(r))return leave(SATCHEL_GTF_NOT_READY);SatchelGuestTextureFrameStatus s=frame(satchel_guest_frames_take(r->frames,epoch,out));if(!s)r->leased=1;return leave(s);}
SatchelGuestTextureFrameStatus satchel_guest_texture_frames_copy(SatchelGuestTextureFrames*r,uint64_t token,uint64_t offset,void*out,size_t size){if(!enter())return SATCHEL_GTF_BUSY;if(!valid(r)||!r->frames)return leave(SATCHEL_GTF_INVALID);return leave(frame(satchel_guest_frames_copy(r->frames,token,offset,out,size)));}
SatchelGuestTextureFrameStatus satchel_guest_texture_frames_release(SatchelGuestTextureFrames*r,uint64_t token){if(!enter())return SATCHEL_GTF_BUSY;if(!valid(r)||!r->frames)return leave(SATCHEL_GTF_INVALID);SatchelGuestTextureFrameStatus s=frame(satchel_guest_frames_release(r->frames,token));if(!s)r->leased=0;return leave(s);}
SatchelGuestTextureFrameStatus satchel_guest_texture_frames_detach(SatchelGuestTextureFrames*r,uint64_t epoch){if(!enter())return SATCHEL_GTF_BUSY;if(!valid(r)||epoch!=r->epoch)return leave(SATCHEL_GTF_INVALID);return leave(detach(r));}
SatchelGuestTextureFrameStatus satchel_guest_texture_frames_try_destroy(SatchelGuestTextureFrames**out){if(!out||((uintptr_t)out%_Alignof(SatchelGuestTextureFrames*)))return SATCHEL_GTF_INVALID;if(!enter())return SATCHEL_GTF_BUSY;SatchelGuestTextureFrames*r=*out;if(!valid(r))return leave(SATCHEL_GTF_INVALID);SatchelGuestTextureFrameStatus s=detach(r);if(s)return leave(s);s=progress(r,0,0);if(gpu_owned(r))return leave(s?s:SATCHEL_GTF_BUSY);if(r->frames){s=frame(satchel_guest_frames_try_destroy(&r->frames));if(s)return leave(s);}for(unsigned i=0;i<SATCHEL_GTF_LIMIT;i++)if(owners[i]==r)owners[i]=NULL;free(r);*out=NULL;return leave(SATCHEL_GTF_OK);}
SatchelGuestTextureFrameStatus satchel_guest_texture_frames_device_children(uint64_t guest,uint64_t backend,uint32_t*out){if(!out||((uintptr_t)out%_Alignof(uint32_t))||!guest||!backend)return SATCHEL_GTF_INVALID;if(!enter())return SATCHEL_GTF_BUSY;uint32_t n=0;for(unsigned i=0;i<SATCHEL_GTF_LIMIT;i++)if(owners[i]&&owners[i]->texture.guest_device==guest&&owners[i]->texture.backend_device==backend&&gpu_owned(owners[i]))n++;*out=n;return leave(SATCHEL_GTF_OK);}
SatchelGuestTextureFrameStatus satchel_guest_texture_frames_check_submit(uint64_t backend){if(!backend)return SATCHEL_GTF_INVALID;if(!enter())return SATCHEL_GTF_BUSY;for(unsigned i=0;i<SATCHEL_GTF_LIMIT;i++)if(owners[i]&&owners[i]->texture.backend_device==backend&&gpu_owned(owners[i]))return leave(SATCHEL_GTF_BUSY);return leave(SATCHEL_GTF_OK);}
SatchelGuestTextureFrameStatus satchel_guest_texture_frames_stats(SatchelGuestTextureFrameStats*out){if(!out||((uintptr_t)out%_Alignof(SatchelGuestTextureFrameStats)))return SATCHEL_GTF_INVALID;if(!enter())return SATCHEL_GTF_BUSY;SatchelGuestTextureFrameStats s={.owner_limit=SATCHEL_GTF_LIMIT,.staging_limit=SATCHEL_GTF_STAGING_TOTAL,.staging_bytes=host_bytes};for(unsigned i=0;i<SATCHEL_GTF_LIMIT;i++)if(owners[i]){s.owners++;s.gpu_owners+=gpu_owned(owners[i]);}*out=s;return leave(SATCHEL_GTF_OK);}
