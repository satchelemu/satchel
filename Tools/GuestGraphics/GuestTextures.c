#include "GuestTextures.h"
#include "VulkanRenderTarget.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
typedef struct {uint64_t device,device_generation,pool,offset;SatchelGuestTextureDescription description;} Builder;
typedef struct {uint32_t state,references;SatchelGuestTextureBinding b;} Texture;
typedef struct {uint64_t token,generation;} Reference;
struct SatchelGuestTextures {SatchelGuestPools *pools;SatchelGuestPoolDeviceValidator validate;SatchelGuestPoolCheckedRangeGuard guard;void *context;atomic_flag active;uint32_t retiring;Texture textures[SATCHEL_GTEX_LIMIT];Reference refs[SATCHEL_GTEX_REFS];};
static _Atomic uint64_t next_generation=1,next_ref=1;
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t u64(const uint8_t *p){return u32(p)|(uint64_t)u32(p+4)<<32;}
static void p32(uint8_t *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(i*8));}
static void p64(uint8_t *p,uint64_t n){p32(p,(uint32_t)n);p32(p+4,(uint32_t)(n>>32));}
static int identity(_Atomic uint64_t *counter,uint64_t *out){uint64_t n=atomic_load(counter);do{if(n==UINT64_MAX)return 0;}while(!atomic_compare_exchange_weak(counter,&n,n+1));*out=n;return 1;}
static uint32_t pool_status(SatchelGuestPoolStatus s){switch(s){case SATCHEL_GUEST_POOL_OK:return SATCHEL_GTEX_OK;case SATCHEL_GUEST_POOL_BUSY:return SATCHEL_GTEX_BUSY;case SATCHEL_GUEST_POOL_MEMORY:return SATCHEL_GTEX_MEMORY;case SATCHEL_GUEST_POOL_UNSUPPORTED:return SATCHEL_GTEX_UNSUPPORTED;case SATCHEL_GUEST_POOL_LIMIT_REACHED:return SATCHEL_GTEX_LIMIT_REACHED;case SATCHEL_GUEST_POOL_ALLOCATION:return SATCHEL_GTEX_ALLOCATION;case SATCHEL_GUEST_POOL_BACKEND:return SATCHEL_GTEX_BACKEND;default:return SATCHEL_GTEX_BAD_OBJECT;}}
static uint32_t vk_status(SatchelVulkanStatus s){switch(s){case SATCHEL_VULKAN_OK:return SATCHEL_GTEX_OK;case SATCHEL_VULKAN_BUSY:return SATCHEL_GTEX_BUSY;case SATCHEL_VULKAN_UNSUPPORTED:return SATCHEL_GTEX_UNSUPPORTED;default:return SATCHEL_GTEX_BACKEND;}}
static uint32_t enter(SatchelGuestTextures *o,SatchelCPUProbe *cpu){if(!o || !cpu)return SATCHEL_GTEX_INVALID;if(atomic_flag_test_and_set(&o->active))return SATCHEL_GTEX_BUSY;if(o->retiring){atomic_flag_clear(&o->active);return SATCHEL_GTEX_INVALID;}return SATCHEL_GTEX_OK;}
static uint32_t leave(SatchelGuestTextures *o,uint32_t s){atomic_flag_clear(&o->active);return s;}
static int disjoint(SatchelCPUProbe *cpu,uint64_t a,uint64_t n,uint64_t b,uint64_t m){int overlap=1;return satchel_cpu_probe_physical_overlap(cpu,a,n,b,m,&overlap) && !overlap;}
static uint32_t write(SatchelCPUProbe *cpu,uint64_t address,const uint8_t *data,size_t size){SatchelCPUProbeWrite w={address,data,size};switch(satchel_cpu_probe_writev(cpu,&w,1)){case SATCHEL_CPU_WRITE_OK:return SATCHEL_GTEX_OK;case SATCHEL_CPU_WRITE_BUSY:return SATCHEL_GTEX_BUSY;case SATCHEL_CPU_WRITE_ALLOCATION:return SATCHEL_GTEX_ALLOCATION;default:return SATCHEL_GTEX_MEMORY;}}
static Texture *find(SatchelGuestTextures *o,uint64_t object){for(unsigned i=0;i<SATCHEL_GTEX_LIMIT;i++)if(o->textures[i].state && o->textures[i].b.object==object)return &o->textures[i];return NULL;}
static uint32_t private_range(SatchelGuestTextures *o,SatchelCPUProbe *cpu,uint64_t a,uint64_t n,const Texture *self){
 if(!satchel_cpu_probe_check_range(cpu,a,n,SATCHEL_CPU_READ|SATCHEL_CPU_WRITE))return SATCHEL_GTEX_MEMORY;if(o->guard && !o->guard(o->context,cpu,a,n))return SATCHEL_GTEX_BAD_OBJECT;
 uint32_t s=pool_status(satchel_guest_pools_check_unowned_range_checked(o->pools,cpu,a,n));if(s)return s;
 for(unsigned i=0;i<SATCHEL_GTEX_LIMIT;i++){const Texture *t=&o->textures[i];if(t->state && t!=self && !disjoint(cpu,a,n,t->b.object,SATCHEL_GTEX_OBJECT_SIZE))return SATCHEL_GTEX_BAD_OBJECT;}return SATCHEL_GTEX_OK;
}
static uint32_t device(SatchelGuestTextures *o,SatchelCPUProbe *cpu,uint64_t a,uint64_t *token,uint64_t *generation){*token=*generation=0;return o->validate(o->context,cpu,a,token,generation) && *token && *generation?SATCHEL_GTEX_OK:SATCHEL_GTEX_BAD_OBJECT;}
static void decode_builder(const uint8_t bytes[128],Builder *b){memset(b,0,sizeof(*b));b->device=u64(bytes+8);b->device_generation=u64(bytes+16);b->pool=u64(bytes+24);b->offset=u64(bytes+32);SatchelGuestTextureDescription *d=&b->description;d->version=1;d->size=sizeof(*d);d->target=u32(bytes+40);d->format=u32(bytes+44);d->flags=u32(bytes+48);d->width=u32(bytes+52);d->height=u32(bytes+56);d->depth=u32(bytes+60);d->levels=u32(bytes+64);d->samples=u32(bytes+68);d->stride=u64(bytes+72);}
static void encode_builder(const Builder *b,uint8_t bytes[128]){memset(bytes,0,128);p32(bytes,0x504c5442);p32(bytes+4,1);p64(bytes+8,b->device);p64(bytes+16,b->device_generation);p64(bytes+24,b->pool);p64(bytes+32,b->offset);const SatchelGuestTextureDescription *d=&b->description;p32(bytes+40,d->target);p32(bytes+44,d->format);p32(bytes+48,d->flags);p32(bytes+52,d->width);p32(bytes+56,d->height);p32(bytes+60,d->depth);p32(bytes+64,d->levels);p32(bytes+68,d->samples);p64(bytes+72,d->stride);}
static uint32_t builder(SatchelGuestTextures *o,SatchelCPUProbe *cpu,uint64_t a,Builder *out,int defaults){
 uint32_t s=private_range(o,cpu,a,128,NULL);if(s)return s;uint8_t bytes[128];if(!satchel_cpu_probe_read(cpu,a,bytes,128))return SATCHEL_GTEX_MEMORY;
 if(u32(bytes)!=0x504c5442 || u32(bytes+4)!=1){if(!defaults)return SATCHEL_GTEX_BAD_OBJECT;memset(out,0,sizeof(*out));return SATCHEL_GTEX_OK;}decode_builder(bytes,out);
 if(out->device){uint64_t token=0,gen=0;s=device(o,cpu,out->device,&token,&gen);if(s || gen!=out->device_generation || !disjoint(cpu,a,128,out->device,0x3000))return SATCHEL_GTEX_BAD_OBJECT;}return SATCHEL_GTEX_OK;
}
static uint32_t cleanup(SatchelGuestTextures *o,Texture *t){
 if(t->references)return SATCHEL_GTEX_BUSY;if(t->b.target){uint32_t s=vk_status(satchel_vulkan_target_destroy(t->b.backend_device,t->b.target));if(s)return s;t->b.target=0;}
 if(t->b.pool_lease){uint32_t s=pool_status(satchel_guest_pools_release(o->pools,t->b.pool_lease));if(s)return s;t->b.pool_lease=0;}memset(t,0,sizeof(*t));return SATCHEL_GTEX_OK;
}
static uint32_t valid(SatchelGuestTextures *o,SatchelCPUProbe *cpu,Texture *t){
 if(!t || t->state!=1)return SATCHEL_GTEX_BAD_OBJECT;uint32_t s=private_range(o,cpu,t->b.object,SATCHEL_GTEX_OBJECT_SIZE,t);if(s)return s;uint8_t bytes[16];if(!satchel_cpu_probe_read(cpu,t->b.object,bytes,16))return SATCHEL_GTEX_MEMORY;
 if(u32(bytes)!=0x504c5458 || u32(bytes+4)!=1 || u64(bytes+8)!=t->b.generation)return SATCHEL_GTEX_BAD_OBJECT;
 uint64_t token=0,gen=0;s=device(o,cpu,t->b.guest_device,&token,&gen);if(s || token!=t->b.backend_device || gen!=t->b.device_generation || !disjoint(cpu,t->b.object,SATCHEL_GTEX_OBJECT_SIZE,t->b.guest_device,0x3000))return SATCHEL_GTEX_BAD_OBJECT;
 SatchelGuestPoolView view;SatchelGuestPoolBinding binding;s=pool_status(satchel_guest_pools_lease_view(o->pools,cpu,t->b.pool_lease,&view));if(s)return s;s=pool_status(satchel_guest_pools_lease_binding(o->pools,cpu,t->b.pool_lease,t->b.storage_gpu,t->b.layout.storage_size,&binding));if(s)return s;
 if(view.generation!=t->b.pool_generation || view.object!=t->b.pool_object || view.guest_device!=t->b.guest_device || binding.buffer!=t->b.binding.buffer || binding.offset!=t->b.binding.offset || binding.size!=t->b.binding.size || t->b.storage_cpu!=view.cpu_address+binding.offset)return SATCHEL_GTEX_BAD_OBJECT;
 for(unsigned i=0;i<SATCHEL_GTEX_LIMIT;i++){const Texture *other=&o->textures[i];if(other->state && other!=t && !disjoint(cpu,t->b.storage_cpu,t->b.layout.storage_size,other->b.storage_cpu,other->b.layout.storage_size))return SATCHEL_GTEX_UNSUPPORTED;}
 SatchelVulkanTargetInfo info;s=vk_status(satchel_vulkan_target_info(t->b.backend_device,t->b.target,&info));if(s)return s;
 return info.width==t->b.layout.description.width && info.height==t->b.layout.description.height && info.format==1?SATCHEL_GTEX_OK:SATCHEL_GTEX_INTERNAL;
}
SatchelGuestTextures *satchel_guest_textures_create(SatchelGuestPools *p,SatchelGuestPoolDeviceValidator v,SatchelGuestPoolCheckedRangeGuard g,void *c){if(!p || !v)return NULL;SatchelGuestTextures *o=calloc(1,sizeof(*o));if(o){o->pools=p;o->validate=v;o->guard=g;o->context=c;atomic_flag_clear(&o->active);}return o;}
static uint32_t initialize(SatchelGuestTextures *o,SatchelCPUProbe *cpu,uint64_t address,uint64_t ba){
 if(find(o,address))return SATCHEL_GTEX_BAD_OBJECT;Builder b;uint32_t s=builder(o,cpu,ba,&b,0);if(s)return s;s=private_range(o,cpu,address,SATCHEL_GTEX_OBJECT_SIZE,NULL);if(s)return s;
 uint64_t token=0,gen=0;s=device(o,cpu,b.device,&token,&gen);if(s || gen!=b.device_generation || !disjoint(cpu,address,SATCHEL_GTEX_OBJECT_SIZE,b.device,0x3000) || !disjoint(cpu,address,SATCHEL_GTEX_OBJECT_SIZE,ba,128))return SATCHEL_GTEX_BAD_OBJECT;
 SatchelGuestTextureLayout layout;int ls=satchel_guest_texture_layout(&b.description,&layout);if(ls)return ls==SATCHEL_GTL_INVALID?SATCHEL_GTEX_INVALID:SATCHEL_GTEX_UNSUPPORTED;
 if(b.offset>INT64_MAX || (b.offset&(layout.alignment-1)))return SATCHEL_GTEX_INVALID;Texture *slot=NULL;for(unsigned i=0;i<SATCHEL_GTEX_LIMIT;i++)if(!o->textures[i].state){slot=&o->textures[i];break;}if(!slot)return SATCHEL_GTEX_LIMIT_REACHED;
 uint64_t id=0;if(!identity(&next_generation,&id))return SATCHEL_GTEX_LIMIT_REACHED;
 SatchelGuestPoolView view;uint64_t lease=0;s=pool_status(satchel_guest_pools_retain_object(o->pools,cpu,b.device,b.pool,&lease,&view));if(s)return s;
 slot->state=2;slot->b=(SatchelGuestTextureBinding){.object=address,.guest_device=b.device,.backend_device=token,.device_generation=gen,.generation=id,.pool_lease=lease,.pool_generation=view.generation,.pool_object=b.pool,.layout=layout};
 if(b.offset>view.size || layout.storage_size>view.size-b.offset){s=SATCHEL_GTEX_INVALID;goto fail;}
 slot->b.storage_cpu=view.cpu_address+b.offset;slot->b.storage_gpu=view.gpu_address+b.offset;
 for(unsigned i=0;i<SATCHEL_GTEX_LIMIT;i++){const Texture *other=&o->textures[i];if(other->state && other!=slot && !disjoint(cpu,slot->b.storage_cpu,layout.storage_size,other->b.storage_cpu,other->b.layout.storage_size)){s=SATCHEL_GTEX_UNSUPPORTED;goto fail;}}
 s=pool_status(satchel_guest_pools_lease_binding(o->pools,cpu,lease,slot->b.storage_gpu,layout.storage_size,&slot->b.binding));if(s)goto fail;
 SatchelVulkanTargetDescription d={1,sizeof(d),1,b.description.width,b.description.height,0};SatchelVulkanTargetInfo info;s=vk_status(satchel_vulkan_target_create(token,&d,&slot->b.target,&info));if(s)goto fail;
 uint8_t tag[SATCHEL_GTEX_OBJECT_SIZE]={0};p32(tag,0x504c5458);p32(tag+4,1);p64(tag+8,id);s=write(cpu,address,tag,sizeof(tag));if(s)goto fail;slot->state=1;return SATCHEL_GTEX_OK;
 fail: {uint32_t released=cleanup(o,slot);return released?released:s;}
}
static uint32_t execute(SatchelGuestTextures *o,SatchelCPUProbe *cpu,const SatchelGuestTextureRequest *r,uint64_t *value){
 if(r->operation>=SATCHEL_GTEX_DEFAULTS && r->operation<=SATCHEL_GTEX_STORAGE_ALIGNMENT){
  Builder b;uint32_t s=builder(o,cpu,r->object,&b,r->operation==SATCHEL_GTEX_DEFAULTS);if(s)return s;SatchelGuestTextureDescription *d=&b.description;
  switch(r->operation){
   case SATCHEL_GTEX_DEFAULTS:{uint64_t address=b.device,gen=b.device_generation;memset(&b,0,sizeof(b));b.device=address;b.device_generation=gen;*d=(SatchelGuestTextureDescription){1,sizeof(*d),1,0,0,1,1,1,1,0,0};break;}
   case SATCHEL_GTEX_DEVICE:{uint64_t token=0,gen=0;s=device(o,cpu,r->arg0,&token,&gen);if(s || !disjoint(cpu,r->object,128,r->arg0,0x3000))return SATCHEL_GTEX_BAD_OBJECT;b.device=r->arg0;b.device_generation=gen;break;}
   case SATCHEL_GTEX_FLAGS:d->flags=(uint32_t)r->arg0;break;
   case SATCHEL_GTEX_TARGET:d->target=(uint32_t)r->arg0;break;
   case SATCHEL_GTEX_SIZE2D:if((int32_t)r->arg0<=0 || (int32_t)r->arg1<=0)return SATCHEL_GTEX_INVALID;d->width=(uint32_t)r->arg0;d->height=(uint32_t)r->arg1;d->depth=1;break;
   case SATCHEL_GTEX_LEVELS:d->levels=(uint32_t)r->arg0;break;
   case SATCHEL_GTEX_FORMAT:d->format=(uint32_t)r->arg0;break;
   case SATCHEL_GTEX_SAMPLES:d->samples=(uint32_t)r->arg0;break;
   case SATCHEL_GTEX_STRIDE:d->stride=r->arg0;break;
   case SATCHEL_GTEX_STORAGE:b.pool=r->arg0;b.offset=r->arg1;break;
   case SATCHEL_GTEX_STORAGE_SIZE:case SATCHEL_GTEX_STORAGE_ALIGNMENT:{SatchelGuestTextureLayout l;int status=satchel_guest_texture_layout(d,&l);if(status)return status==SATCHEL_GTL_INVALID?SATCHEL_GTEX_INVALID:SATCHEL_GTEX_UNSUPPORTED;*value=r->operation==SATCHEL_GTEX_STORAGE_SIZE?l.storage_size:l.alignment;return SATCHEL_GTEX_OK;}
   default:return SATCHEL_GTEX_UNSUPPORTED;
  }
  uint8_t bytes[128];encode_builder(&b,bytes);return write(cpu,r->object,bytes,sizeof(bytes));
 }
 if(r->operation==SATCHEL_GTEX_INITIALIZE){uint32_t s=initialize(o,cpu,r->object,r->arg0);if(!s)*value=1;return s;}
 Texture *t=find(o,r->object);if(t && t->state==2 && r->operation==SATCHEL_GTEX_FINALIZE)return cleanup(o,t);uint32_t s=valid(o,cpu,t);if(s)return s;
 switch(r->operation){
  case SATCHEL_GTEX_FINALIZE:{if(t->references)return SATCHEL_GTEX_BUSY;uint8_t empty[SATCHEL_GTEX_OBJECT_SIZE]={0};s=write(cpu,t->b.object,empty,sizeof(empty));if(s)return s;t->state=2;return cleanup(o,t);}
  case SATCHEL_GTEX_WIDTH:*value=t->b.layout.description.width;return SATCHEL_GTEX_OK;
  case SATCHEL_GTEX_HEIGHT:*value=t->b.layout.description.height;return SATCHEL_GTEX_OK;
  case SATCHEL_GTEX_POOL:*value=t->b.pool_object;return SATCHEL_GTEX_OK;
  case SATCHEL_GTEX_OFFSET:*value=t->b.binding.offset;return SATCHEL_GTEX_OK;
  case SATCHEL_GTEX_GPU_ADDRESS:*value=t->b.storage_gpu;return SATCHEL_GTEX_OK;
  default:return SATCHEL_GTEX_UNSUPPORTED;
 }
}
uint32_t satchel_guest_textures_execute(SatchelGuestTextures *o,SatchelCPUProbe *cpu,const SatchelGuestTextureRequest *request,SatchelGuestTextureResult *out){if(!out)return SATCHEL_GTEX_INVALID;*out=(SatchelGuestTextureResult){SATCHEL_GTEX_INVALID,0};if(!request)return SATCHEL_GTEX_INVALID;uint32_t s=enter(o,cpu);if(s){out->status=s;return s;}SatchelGuestTextureRequest copy=*request;uint64_t value=0;s=execute(o,cpu,&copy,&value);out->status=s;if(!s)out->value=value;return leave(o,s);}
static Texture *referenced(SatchelGuestTextures *o,uint64_t token,Reference **ref){for(unsigned i=0;i<SATCHEL_GTEX_REFS;i++)if(token && o->refs[i].token==token){if(ref)*ref=&o->refs[i];for(unsigned j=0;j<SATCHEL_GTEX_LIMIT;j++)if(o->textures[j].state==1 && o->textures[j].b.generation==o->refs[i].generation)return &o->textures[j];}return NULL;}
uint32_t satchel_guest_textures_retain(SatchelGuestTextures *o,SatchelCPUProbe *cpu,uint64_t object,uint64_t *ref,SatchelGuestTextureBinding *out){if(!ref || !out)return SATCHEL_GTEX_INVALID;*ref=0;uint32_t s=enter(o,cpu);if(s)return s;Texture *t=find(o,object);s=valid(o,cpu,t);if(s)return leave(o,s);Reference *slot=NULL;for(unsigned i=0;i<SATCHEL_GTEX_REFS;i++)if(!o->refs[i].token){slot=&o->refs[i];break;}if(!slot || t->references==UINT32_MAX)return leave(o,SATCHEL_GTEX_LIMIT_REACHED);uint64_t id=0;if(!identity(&next_ref,&id))return leave(o,SATCHEL_GTEX_LIMIT_REACHED);*slot=(Reference){id,t->b.generation};++t->references;*ref=id;*out=t->b;return leave(o,SATCHEL_GTEX_OK);}
uint32_t satchel_guest_textures_binding(SatchelGuestTextures *o,SatchelCPUProbe *cpu,uint64_t ref,SatchelGuestTextureBinding *out){if(!out)return SATCHEL_GTEX_INVALID;uint32_t s=enter(o,cpu);if(s)return s;Texture *t=referenced(o,ref,NULL);s=valid(o,cpu,t);if(!s)*out=t->b;return leave(o,s);}
uint32_t satchel_guest_textures_release(SatchelGuestTextures *o,uint64_t token){if(!o || !token)return SATCHEL_GTEX_INVALID;if(atomic_flag_test_and_set(&o->active))return SATCHEL_GTEX_BUSY;Reference *ref=NULL;Texture *t=referenced(o,token,&ref);uint32_t s=SATCHEL_GTEX_BAD_OBJECT;if(t && t->references){--t->references;memset(ref,0,sizeof(*ref));s=SATCHEL_GTEX_OK;}return leave(o,s);}
uint32_t satchel_guest_textures_pool_guard(const SatchelGuestTextures *o,SatchelCPUProbe *cpu,uint64_t a,uint64_t n){if(!o || !cpu || !n || a>UINT64_MAX-n)return SATCHEL_GTEX_INVALID;for(unsigned i=0;i<SATCHEL_GTEX_LIMIT;i++)if(o->textures[i].state && !disjoint(cpu,a,n,o->textures[i].b.object,SATCHEL_GTEX_OBJECT_SIZE))return SATCHEL_GTEX_BAD_OBJECT;return SATCHEL_GTEX_OK;}
uint32_t satchel_guest_textures_device_children(SatchelGuestTextures *o,uint64_t device,uint32_t *out){if(!o || !out)return SATCHEL_GTEX_INVALID;if(atomic_flag_test_and_set(&o->active))return SATCHEL_GTEX_BUSY;*out=0;for(unsigned i=0;i<SATCHEL_GTEX_LIMIT;i++)*out+=o->textures[i].state && o->textures[i].b.guest_device==device;return leave(o,SATCHEL_GTEX_OK);}
uint32_t satchel_guest_textures_try_destroy(SatchelGuestTextures **pointer){if(!pointer)return SATCHEL_GTEX_INVALID;if(!*pointer)return SATCHEL_GTEX_OK;SatchelGuestTextures *o=*pointer;if(atomic_flag_test_and_set(&o->active))return SATCHEL_GTEX_BUSY;o->retiring=1;for(unsigned i=0;i<SATCHEL_GTEX_REFS;i++)if(o->refs[i].token)return leave(o,SATCHEL_GTEX_BUSY);for(unsigned i=0;i<SATCHEL_GTEX_LIMIT;i++)if(o->textures[i].state){uint32_t s=cleanup(o,&o->textures[i]);if(s)return leave(o,s);}free(o);*pointer=NULL;return SATCHEL_GTEX_OK;}
