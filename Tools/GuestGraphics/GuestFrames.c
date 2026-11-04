#include "GuestFrames.h"
#include "VulkanRasterBatch.h"
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#ifndef SATCHEL_FRAME_BYTES_ALLOC
#define SATCHEL_FRAME_BYTES_ALLOC malloc
#endif
#ifndef SATCHEL_FRAME_BUFFER_READ
#define SATCHEL_FRAME_BUFFER_READ satchel_vulkan_buffer_read
#endif
#ifndef SATCHEL_FRAME_BUFFER_DESTROY
#define SATCHEL_FRAME_BUFFER_DESTROY satchel_vulkan_buffer_destroy
#endif
#ifndef SATCHEL_FRAME_SUBMIT
#define SATCHEL_FRAME_SUBMIT satchel_vulkan_raster_batch_readback
#endif

typedef struct {SatchelFrameInfo info;uint64_t device,buffer,job;uint8_t *bytes;unsigned discard,copied;} Frame;
struct SatchelGuestFrames {uint64_t epoch;unsigned attached,retiring;Frame frames[SATCHEL_FRAME_CAPACITY];};
static atomic_flag active=ATOMIC_FLAG_INIT;
static SatchelGuestFrames *owners[SATCHEL_FRAME_OWNER_LIMIT];
static uint32_t frame_count;
static uint64_t frame_bytes,next_token=1;
static int enter(void){return !atomic_flag_test_and_set(&active);}
static SatchelFrameStatus leave(SatchelFrameStatus s){atomic_flag_clear(&active);return s;}
static int valid(SatchelGuestFrames *p){if(!p)return 0;for(unsigned i=0;i<SATCHEL_FRAME_OWNER_LIMIT;i++)if(owners[i]==p)return 1;return 0;}
static Frame *find(SatchelGuestFrames *p,uint64_t token){if(!token)return NULL;for(unsigned i=0;i<SATCHEL_FRAME_CAPACITY;i++)if(p->frames[i].info.token==token)return p->frames+i;return NULL;}
static void release_bytes(Frame *f){if(!f->info.token)return;free(f->bytes);frame_bytes-=f->info.byte_size;--frame_count;memset(f,0,sizeof(*f));}
static SatchelFrameStatus backend(SatchelVulkanStatus s){return s==SATCHEL_VULKAN_OK?SATCHEL_FRAME_OK:s==SATCHEL_VULKAN_BUSY?SATCHEL_FRAME_BUSY:s==SATCHEL_VULKAN_TIMEOUT?SATCHEL_FRAME_PENDING:s==SATCHEL_VULKAN_CANCELLED?SATCHEL_FRAME_CANCELLED:SATCHEL_FRAME_BACKEND;}
static void obsolete(SatchelGuestFrames *p){for(unsigned i=0;i<SATCHEL_FRAME_CAPACITY;i++){Frame *f=p->frames+i;if(!f->info.token || f->info.state==SATCHEL_FRAME_LEASED)continue;f->discard=1;if(!f->job&&!f->buffer)release_bytes(f);else f->info.state=SATCHEL_FRAME_RETIRING;}}
static SatchelFrameStatus advance(Frame *f,uint32_t timeout,SatchelFrameCancelled cancel,void *context){
 if(cancel&&cancel(context))return SATCHEL_FRAME_CANCELLED;
 if(f->job){SatchelVulkanSubmissionReport r={0};SatchelVulkanStatus s=satchel_vulkan_submission_wait(f->device,f->job,timeout,cancel,context,&r);if(s!=SATCHEL_VULKAN_OK)return backend(s);if(!r.completed||r.resources_retained)return SATCHEL_FRAME_BACKEND;f->job=0;}
 if(!f->discard&&!f->copied){SatchelVulkanStatus s=SATCHEL_FRAME_BUFFER_READ(f->device,f->buffer,0,f->bytes,(size_t)f->info.byte_size);if(s!=SATCHEL_VULKAN_OK)return backend(s);f->copied=1;}
 if(f->buffer){SatchelVulkanStatus s=SATCHEL_FRAME_BUFFER_DESTROY(f->device,f->buffer);if(s!=SATCHEL_VULKAN_OK)return backend(s);f->buffer=0;}
 if(f->discard)release_bytes(f);else f->info.state=SATCHEL_FRAME_READY;
 return SATCHEL_FRAME_OK;
}
SatchelFrameStatus satchel_guest_frames_create(uint64_t epoch,SatchelGuestFrames **out){
 if(!out)return SATCHEL_FRAME_INVALID;*out=NULL;if(!epoch)return SATCHEL_FRAME_INVALID;if(!enter())return SATCHEL_FRAME_BUSY;
 unsigned slot=SATCHEL_FRAME_OWNER_LIMIT;for(unsigned i=0;i<SATCHEL_FRAME_OWNER_LIMIT;i++)if(!owners[i]){slot=i;break;}if(slot==SATCHEL_FRAME_OWNER_LIMIT)return leave(SATCHEL_FRAME_LIMIT);
 SatchelGuestFrames *p=calloc(1,sizeof(*p));if(!p)return leave(SATCHEL_FRAME_ALLOCATION);p->epoch=epoch;p->attached=1;owners[slot]=p;*out=p;return leave(SATCHEL_FRAME_OK);
}
SatchelFrameStatus satchel_guest_frames_attach(SatchelGuestFrames *p,uint64_t epoch){if(!enter())return SATCHEL_FRAME_BUSY;if(!valid(p)||p->retiring||!epoch||epoch<=p->epoch)return leave(SATCHEL_FRAME_INVALID);obsolete(p);p->epoch=epoch;p->attached=1;return leave(SATCHEL_FRAME_OK);}
SatchelFrameStatus satchel_guest_frames_detach(SatchelGuestFrames *p,uint64_t epoch){if(!enter())return SATCHEL_FRAME_BUSY;if(!valid(p)||epoch!=p->epoch)return leave(SATCHEL_FRAME_INVALID);p->attached=0;obsolete(p);return leave(SATCHEL_FRAME_OK);}
SatchelFrameStatus satchel_guest_frames_begin(SatchelGuestFrames *p,const SatchelFrameSource *input,uint64_t *ticket,SatchelFrameCancelled cancel,void *context){
 if(!ticket)return SATCHEL_FRAME_INVALID;SatchelFrameSource source={0};if(input)source=*input;*ticket=0;if(!input)return SATCHEL_FRAME_INVALID;if(!enter())return SATCHEL_FRAME_BUSY;
 if(!valid(p)||source.version!=1||source.size!=sizeof(source))return leave(SATCHEL_FRAME_INVALID);
 if(p->retiring||!p->attached||source.epoch!=p->epoch)return leave(SATCHEL_FRAME_NO_SURFACE);
 if(source.format!=SATCHEL_FRAME_RGBA8_UNORM||source.color_space!=SATCHEL_FRAME_LINEAR_SRGB||source.origin!=SATCHEL_FRAME_TOP_LEFT)return leave(SATCHEL_FRAME_UNSUPPORTED);
 if(cancel&&cancel(context))return leave(SATCHEL_FRAME_CANCELLED);
 if(frame_count==SATCHEL_FRAME_CAPACITY||next_token==UINT64_MAX)return leave(SATCHEL_FRAME_LIMIT);
 SatchelVulkanTargetInfo target={0};SatchelVulkanStatus s=satchel_vulkan_target_info(source.device,source.target,&target);if(s!=SATCHEL_VULKAN_OK)return leave(backend(s));
 if(target.in_flight)return leave(SATCHEL_FRAME_BUSY);if(!target.initialized||target.format!=source.format||!target.width||!target.height||target.width>2048||target.height>2048)return leave(SATCHEL_FRAME_UNSUPPORTED);
 const uint64_t size=(uint64_t)target.width*target.height*4;if(!size||size>SATCHEL_FRAME_MAX_BYTES||size>SATCHEL_FRAME_TOTAL_BYTES-frame_bytes)return leave(SATCHEL_FRAME_LIMIT);
 Frame *f=NULL;for(unsigned i=0;i<SATCHEL_FRAME_CAPACITY;i++)if(!p->frames[i].info.token){f=p->frames+i;break;}if(!f)return leave(SATCHEL_FRAME_LIMIT);
 f->info=(SatchelFrameInfo){1,sizeof(SatchelFrameInfo),SATCHEL_FRAME_SNAPSHOTTING,target.width,target.height,target.width*4,source.format,source.color_space,source.origin,source.epoch,next_token++,size};f->device=source.device;frame_bytes+=size;++frame_count;
 f->bytes=SATCHEL_FRAME_BYTES_ALLOC((size_t)size);if(!f->bytes){release_bytes(f);return leave(SATCHEL_FRAME_ALLOCATION);}
 SatchelVulkanBufferInfo info={0};s=satchel_vulkan_buffer_create(source.device,size,SATCHEL_VULKAN_BUFFER_TRANSFER_DESTINATION,&f->buffer,&info);if(s!=SATCHEL_VULKAN_OK){release_bytes(f);return leave(backend(s));}
 SatchelVulkanRasterBatch batch={0};batch.version=1;batch.size=sizeof(batch);batch.load_operation=SATCHEL_VULKAN_RASTER_LOAD;batch.readback_buffer=f->buffer;batch.row_pitch=f->info.row_stride;
 s=SATCHEL_FRAME_SUBMIT(source.device,source.target,&batch,&f->job,cancel,context);
 if(s==SATCHEL_VULKAN_OK&&f->job){*ticket=f->info.token;return leave(SATCHEL_FRAME_OK);}
 f->discard=1;f->info.state=SATCHEL_FRAME_RETIRING;
 if(!f->job){SatchelFrameStatus cleanup=advance(f,0,NULL,NULL);if(cleanup==SATCHEL_FRAME_OK)return leave(s==SATCHEL_VULKAN_OK?SATCHEL_FRAME_BACKEND:backend(s));}
 *ticket=f->info.token;return leave(s==SATCHEL_VULKAN_OK?SATCHEL_FRAME_BACKEND:backend(s));
}
SatchelFrameStatus satchel_guest_frames_poll(SatchelGuestFrames *p,uint64_t token,uint32_t timeout,SatchelFrameCancelled cancel,void *context){
 if(timeout>SATCHEL_FRAME_POLL_LIMIT_MS)return SATCHEL_FRAME_INVALID;if(!enter())return SATCHEL_FRAME_BUSY;if(!valid(p))return leave(SATCHEL_FRAME_INVALID);Frame *f=find(p,token);if(!f)return leave(SATCHEL_FRAME_INVALID);
 if(f->info.state==SATCHEL_FRAME_READY||f->info.state==SATCHEL_FRAME_LEASED)return leave(SATCHEL_FRAME_OK);return leave(advance(f,timeout,cancel,context));
}
SatchelFrameStatus satchel_guest_frames_take(SatchelGuestFrames *p,uint64_t epoch,SatchelFrameInfo *out){
 if(!out)return SATCHEL_FRAME_INVALID;if(!enter())return SATCHEL_FRAME_BUSY;if(!valid(p))return leave(SATCHEL_FRAME_INVALID);if(!p->attached||p->retiring||p->epoch!=epoch)return leave(SATCHEL_FRAME_NO_SURFACE);
 Frame *chosen=NULL;for(unsigned i=0;i<SATCHEL_FRAME_CAPACITY;i++){Frame *f=p->frames+i;if(f->info.token&&!f->discard&&f->info.state!=SATCHEL_FRAME_LEASED&&f->info.epoch==epoch&&(!chosen||f->info.token<chosen->info.token))chosen=f;}
 if(!chosen||chosen->info.state!=SATCHEL_FRAME_READY)return leave(SATCHEL_FRAME_NOT_READY);chosen->info.state=SATCHEL_FRAME_LEASED;*out=chosen->info;return leave(SATCHEL_FRAME_OK);
}
SatchelFrameStatus satchel_guest_frames_copy(SatchelGuestFrames *p,uint64_t token,uint64_t offset,void *out,size_t size){
 if(!out||!size)return SATCHEL_FRAME_INVALID;if(!enter())return SATCHEL_FRAME_BUSY;if(!valid(p))return leave(SATCHEL_FRAME_INVALID);Frame *f=find(p,token);if(!f)return leave(SATCHEL_FRAME_INVALID);if(f->info.state!=SATCHEL_FRAME_LEASED)return leave(SATCHEL_FRAME_NOT_READY);
 if(offset>f->info.byte_size||size>f->info.byte_size-offset)return leave(SATCHEL_FRAME_INVALID);memcpy(out,f->bytes+(size_t)offset,size);return leave(SATCHEL_FRAME_OK);
}
SatchelFrameStatus satchel_guest_frames_release(SatchelGuestFrames *p,uint64_t token){if(!enter())return SATCHEL_FRAME_BUSY;if(!valid(p))return leave(SATCHEL_FRAME_INVALID);Frame *f=find(p,token);if(!f||f->info.state!=SATCHEL_FRAME_LEASED)return leave(SATCHEL_FRAME_INVALID);release_bytes(f);return leave(SATCHEL_FRAME_OK);}
SatchelFrameStatus satchel_guest_frames_collect(SatchelGuestFrames *p){
 if(!enter())return SATCHEL_FRAME_BUSY;if(!valid(p))return leave(SATCHEL_FRAME_INVALID);SatchelFrameStatus status=SATCHEL_FRAME_OK;
 for(unsigned i=0;i<SATCHEL_FRAME_CAPACITY;i++){Frame *f=p->frames+i;if(!f->info.token||!f->discard)continue;SatchelFrameStatus s=advance(f,0,NULL,NULL);if(s!=SATCHEL_FRAME_OK)status=s;}return leave(status);
}
SatchelFrameStatus satchel_guest_frames_try_destroy(SatchelGuestFrames **owner){
 if(!owner||!*owner)return SATCHEL_FRAME_INVALID;if(!enter())return SATCHEL_FRAME_BUSY;SatchelGuestFrames *p=*owner;if(!valid(p))return leave(SATCHEL_FRAME_INVALID);p->retiring=1;p->attached=0;obsolete(p);
 for(unsigned i=0;i<SATCHEL_FRAME_CAPACITY;i++)if(p->frames[i].info.token)return leave(SATCHEL_FRAME_BUSY);
 for(unsigned i=0;i<SATCHEL_FRAME_OWNER_LIMIT;i++)if(owners[i]==p)owners[i]=NULL;free(p);*owner=NULL;return leave(SATCHEL_FRAME_OK);
}
SatchelFrameStatus satchel_guest_frames_stats(SatchelFrameStats *out){
 if(!out)return SATCHEL_FRAME_INVALID;if(!enter())return SATCHEL_FRAME_BUSY;SatchelFrameStats s={0};s.frames=frame_count;s.bytes=frame_bytes;s.byte_limit=SATCHEL_FRAME_TOTAL_BYTES;
 for(unsigned i=0;i<SATCHEL_FRAME_OWNER_LIMIT;i++)if(owners[i]){++s.owners;for(unsigned j=0;j<SATCHEL_FRAME_CAPACITY;j++){Frame *f=owners[i]->frames+j;if(f->info.state==SATCHEL_FRAME_SNAPSHOTTING)++s.snapshotting;if(f->info.state==SATCHEL_FRAME_READY)++s.ready;if(f->info.state==SATCHEL_FRAME_LEASED)++s.leased;if(f->info.state==SATCHEL_FRAME_RETIRING)++s.retiring;}}
 *out=s;return leave(SATCHEL_FRAME_OK);
}
