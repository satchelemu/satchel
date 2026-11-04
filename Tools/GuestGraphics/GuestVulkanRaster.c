#include "GuestVulkanRaster.h"
#include "GuestTransferServices.h"
#include "GuestRasterOperations.h"
#include "VulkanDevice.h"
#include <stdlib.h>
#include <string.h>
typedef struct {uint64_t object,generation,device;uint32_t family,retiring;} Family;
typedef struct {uint64_t object,backend,generation;uint32_t origin,depth;} Mode;
typedef struct {SatchelGuestPools *pools;SatchelGuestTransfers *transfers;SatchelGuestCompute *compute;SatchelGuestTextures *textures;SatchelGuestRasterPrograms *programs;
 SatchelGuestPoolDeviceValidator validate;SatchelGuestPoolCheckedRangeGuard guard;void *context;Family families[8];Mode modes[8];} Owner;
static uint32_t compute_status(uint32_t s){switch(s){case SATCHEL_GC_OK:return 0;case SATCHEL_GC_MEMORY:return SATCHEL_GUEST_POOL_MEMORY;case SATCHEL_GC_UNSUPPORTED:return SATCHEL_GUEST_POOL_UNSUPPORTED;case SATCHEL_GC_BUSY:return SATCHEL_GUEST_POOL_BUSY;case SATCHEL_GC_ALLOCATION:return SATCHEL_GUEST_POOL_ALLOCATION;case SATCHEL_GC_CANCELLED:return SATCHEL_GUEST_TRANSFER_CANCELLED;case SATCHEL_GC_BACKEND:return SATCHEL_GUEST_POOL_BACKEND;default:return SATCHEL_GUEST_POOL_BAD_OBJECT;}}
static uint32_t pair_status(uint32_t s){switch(s){case SATCHEL_GRP_OK:return 0;case SATCHEL_GRP_MEMORY:return SATCHEL_GUEST_POOL_MEMORY;case SATCHEL_GRP_UNSUPPORTED:return SATCHEL_GUEST_POOL_UNSUPPORTED;case SATCHEL_GRP_BUSY:return SATCHEL_GUEST_POOL_BUSY;case SATCHEL_GRP_LIMIT:return SATCHEL_GUEST_POOL_LIMIT_REACHED;case SATCHEL_GRP_ALLOCATION:return SATCHEL_GUEST_POOL_ALLOCATION;case SATCHEL_GRP_BACKEND:return SATCHEL_GUEST_POOL_BACKEND;default:return SATCHEL_GUEST_POOL_BAD_OBJECT;}}
static uint32_t texture_status(uint32_t s){switch(s){case SATCHEL_GTEX_OK:return 0;case SATCHEL_GTEX_MEMORY:return SATCHEL_GUEST_POOL_MEMORY;case SATCHEL_GTEX_UNSUPPORTED:return SATCHEL_GUEST_POOL_UNSUPPORTED;case SATCHEL_GTEX_BUSY:return SATCHEL_GUEST_POOL_BUSY;case SATCHEL_GTEX_LIMIT_REACHED:return SATCHEL_GUEST_POOL_LIMIT_REACHED;case SATCHEL_GTEX_ALLOCATION:return SATCHEL_GUEST_POOL_ALLOCATION;case SATCHEL_GTEX_BACKEND:return SATCHEL_GUEST_POOL_BACKEND;default:return SATCHEL_GUEST_POOL_BAD_OBJECT;}}
static int validate(void *p,SatchelCPUProbe *cpu,uint64_t d,uint64_t *b,uint64_t *g){Owner *o=p;return o->validate(o->context,cpu,d,b,g);}
static int external(Owner *o,SatchelCPUProbe *cpu,uint64_t a,uint64_t n){return !o->guard || o->guard(o->context,cpu,a,n);}
static int pool_guard(void *p,SatchelCPUProbe *cpu,uint64_t a,uint64_t n){Owner *o=p;return external(o,cpu,a,n)&&(!o->transfers || !satchel_guest_transfers_check_pool_range_checked(o->transfers,cpu,a,n))&&(!o->compute || satchel_guest_compute_pool_guard(o->compute,cpu,a,n))&&(!o->textures || !satchel_guest_textures_pool_guard(o->textures,cpu,a,n));}
static int command_guard(void *p,SatchelCPUProbe *cpu,uint64_t a,uint64_t n){Owner *o=p;return external(o,cpu,a,n)&&(!o->compute || satchel_guest_compute_pool_guard(o->compute,cpu,a,n))&&(!o->textures || !satchel_guest_textures_pool_guard(o->textures,cpu,a,n));}
static int program_guard(void *p,SatchelCPUProbe *cpu,uint64_t a,uint64_t n){Owner *o=p;return external(o,cpu,a,n)&&(!o->transfers || !satchel_guest_transfers_guard(o->transfers,cpu,a,n))&&(!o->textures || !satchel_guest_textures_pool_guard(o->textures,cpu,a,n));}
static int texture_guard(void *p,SatchelCPUProbe *cpu,uint64_t a,uint64_t n){Owner *o=p;return external(o,cpu,a,n)&&(!o->transfers || !satchel_guest_transfers_guard(o->transfers,cpu,a,n))&&(!o->compute || satchel_guest_compute_pool_guard(o->compute,cpu,a,n));}
static Family *family(Owner *o,uint64_t object){for(unsigned i=0;i<8;i++)if(o->families[i].object==object)return &o->families[i];return NULL;}
static int identity(void *p,SatchelCPUProbe *cpu,uint64_t a,SatchelGuestRasterIdentity *out,uint32_t *f){Owner *o=p;Family *slot=family(o,a);SatchelGuestProgramIdentity id;if(!slot || slot->retiring || satchel_guest_compute_program_identity(o->compute,cpu,a,&id)!=SATCHEL_GC_OK || id.program_generation!=slot->generation || id.guest_device!=slot->device)return 0;
 *out=(SatchelGuestRasterIdentity){id.guest_device,id.backend_device,id.device_generation,id.program_generation};*f=slot->family;return 1;}
static uint32_t mode(Owner *o,SatchelCPUProbe *cpu,uint64_t d,Mode **out){uint64_t b=0,g=0;if(!validate(o,cpu,d,&b,&g))return SATCHEL_GUEST_POOL_BAD_OBJECT;Mode *empty=NULL;
 for(unsigned i=0;i<8;i++){Mode *m=&o->modes[i];if(m->object && m->object==d && m->backend==b && m->generation==g){*out=m;return 0;}if(m->object){SatchelGuestDeviceCaps caps;SatchelVulkanStatus s=satchel_vulkan_device_capabilities(m->backend,&caps);if(s==SATCHEL_VULKAN_INVALID)memset(m,0,sizeof(*m));}if(!m->object && !empty)empty=m;}
 if(!empty)return SATCHEL_GUEST_POOL_LIMIT_REACHED;*empty=(Mode){d,b,g,0,0};*out=empty;return 0;
}
static uint32_t queue_state(void *p,SatchelCPUProbe *cpu,uint64_t d,SatchelGuestRasterQueueState *out){Owner *o=p;Mode *m;uint32_t s=mode(o,cpu,d,&m);if(s)return s;
 /* Ordinary owned NVN viewport API uses an enabled transform; no raw Maxwell
  * command interface is admitted. No transform-feedback begin is implemented.
  * Shader YNegate is unobserved and proved unused by this compiler profile. */
 *out=(SatchelGuestRasterQueueState){1,sizeof(*out),23,m->origin,m->depth,1,0,0,d,m->backend,m->generation};return 0;
}
static uint32_t program_execute(Owner *o,SatchelCPUProbe *cpu,const SatchelGuestTransferRequest *r,SatchelGuestTransferResult *out){uint64_t object=r->x[0];Family *f=family(o,object);uint32_t s;
 if(r->operation==SATCHEL_GT_PROGRAM_INITIALIZE){if(f)return SATCHEL_GUEST_POOL_BAD_OBJECT;for(unsigned i=0;i<8;i++)if(!o->families[i].object){f=&o->families[i];break;}if(!f)return SATCHEL_GUEST_POOL_LIMIT_REACHED;
  s=compute_status(satchel_guest_compute_program_initialize(o->compute,cpu,object,r->x[1]));if(s)return s;SatchelGuestProgramIdentity id;
  /* Identity is read after the checked common commit. Preserve ownership even
   * if a later consistency check fails, so retirement never drops the object. */
  *f=(Family){.object=object,.device=r->x[1]};s=compute_status(satchel_guest_compute_program_identity(o->compute,cpu,object,&id));if(s)return s;f->generation=id.program_generation;out->set_value=1;out->value=1;return 0;
 }
 if(!f)return SATCHEL_GUEST_POOL_BAD_OBJECT;
 if(r->operation==SATCHEL_GT_PROGRAM_FINALIZE){if(!f->retiring){uint32_t refs=0;s=pair_status(satchel_guest_raster_programs_references(o->programs,object,f->generation,&refs));if(s)return s;if(refs)return SATCHEL_GUEST_POOL_BUSY;
   s=compute_status(satchel_guest_compute_program_finalize(o->compute,cpu,object));if(s)return s;f->retiring=1;}
  s=pair_status(satchel_guest_raster_programs_forget(o->programs,object,f->generation));if(s)return s;memset(f,0,sizeof(*f));return 0;
 }
 if(f->retiring)return SATCHEL_GUEST_POOL_BUSY;
 if((int32_t)r->x[1]==1){if(f->family==SATCHEL_GRP_FAMILY_RASTER)return SATCHEL_GUEST_POOL_UNSUPPORTED;s=compute_status(satchel_guest_compute_program_admit(o->compute,cpu,object,1,r->x[2]));if(s)return s;f->family=SATCHEL_GRP_FAMILY_COMPUTE;}
 else if((int32_t)r->x[1]==2){if(f->family==SATCHEL_GRP_FAMILY_COMPUTE)return SATCHEL_GUEST_POOL_UNSUPPORTED;SatchelGuestProgramIdentity id;s=compute_status(satchel_guest_compute_program_identity(o->compute,cpu,object,&id));if(s)return s;SatchelVulkanRasterCaps caps;
  SatchelVulkanStatus status=satchel_vulkan_raster_capabilities(id.backend_device,&caps);if(status!=SATCHEL_VULKAN_OK)return status==SATCHEL_VULKAN_BUSY?SATCHEL_GUEST_POOL_BUSY:SATCHEL_GUEST_POOL_BACKEND;
  if(!caps.graphics_queue || !caps.rgba8_target || !caps.float4_vertex || caps.max_attributes<8 || caps.max_stride<16)return SATCHEL_GUEST_POOL_UNSUPPORTED;
  SatchelGuestRasterProgramInfo info;s=pair_status(satchel_guest_raster_programs_capture(o->programs,cpu,object,2,r->x[2],&info));if(s)return s;f->family=SATCHEL_GRP_FAMILY_RASTER;
 }else return SATCHEL_GUEST_POOL_UNSUPPORTED;
 out->set_value=1;out->value=1;return 0;
}
static uint32_t prepare_retirement(void *p){Owner *o=p;uint32_t s=satchel_guest_transfers_try_destroy(&o->transfers);if(s)return s;s=pair_status(satchel_guest_raster_programs_try_destroy(&o->programs));if(s)return s;s=compute_status(satchel_guest_compute_try_destroy(&o->compute));if(s)return s;return texture_status(satchel_guest_textures_try_destroy(&o->textures));}
static SatchelGuestPoolStatus destroy(void **p){Owner *o=*p;if(!o)return SATCHEL_GUEST_POOL_OK;if(o->transfers||o->programs||o->compute||o->textures)return SATCHEL_GUEST_POOL_BUSY;SatchelGuestPoolStatus s=satchel_guest_pools_try_destroy(&o->pools);if(!s){free(o);*p=NULL;}return s;}
static void *create_checked(SatchelGuestPoolDeviceValidator v,SatchelGuestPoolCheckedRangeGuard g,void *c){if(!v||!g)return NULL;Owner *o=calloc(1,sizeof(*o));if(!o)return NULL;o->validate=v;o->guard=g;o->context=c;
 o->pools=satchel_guest_pools_create_checked(validate,pool_guard,o);if(o->pools)o->transfers=satchel_guest_transfers_create_checked(o->pools,validate,command_guard,o);if(o->transfers)o->compute=satchel_guest_compute_create(o->pools,validate,program_guard,o);if(o->compute)o->textures=satchel_guest_textures_create(o->pools,validate,texture_guard,o);if(o->textures)o->programs=satchel_guest_raster_programs_create(o->pools,identity,o);
 if(!o->programs){if(prepare_retirement(o) || (o->pools && satchel_guest_pools_try_destroy(&o->pools)))abort();free(o);return NULL;}
 if(!satchel_guest_transfers_attach_compute(o->transfers,o->compute) || !satchel_guest_transfers_attach_raster(o->transfers,o->textures,o->programs,queue_state,o))abort();return o;
}
static void *create_legacy(SatchelGuestPoolDeviceValidator v,SatchelGuestPoolRangeGuard g,void *c){(void)v;(void)g;(void)c;return NULL;}
static SatchelGuestPoolStatus execute_pool(void *p,SatchelCPUProbe *cpu,const SatchelGuestPoolRequest *r,SatchelGuestPoolResult *out){Owner *o=p;return satchel_guest_pools_execute(o->pools,cpu,r,out);}
static int state_guard(void *p,SatchelCPUProbe *cpu,uint64_t a,uint64_t n){Owner *o=p;return pool_guard(o,cpu,a,n)&&!satchel_guest_transfers_guard(o->transfers,cpu,a,n)&&!satchel_guest_pools_check_unowned_range_checked(o->pools,cpu,a,n);}
static uint32_t execute_transfer(void *p,SatchelCPUProbe *cpu,const SatchelGuestTransferRequest *r,SatchelGuestTransferResult *out,SatchelCPUProbeCancelled cancel,void *context){Owner *o=p;*out=(SatchelGuestTransferResult){0};if(cancel&&cancel(context))return out->status=SATCHEL_GUEST_TRANSFER_CANCELLED;uint32_t s;
 if(r->operation>=SATCHEL_GT_PROGRAM_INITIALIZE && r->operation<=SATCHEL_GT_PROGRAM_SHADERS){s=program_execute(o,cpu,r,out);goto done;}
 if(r->operation>SATCHEL_GRO_TEXTURE_BASE && r->operation<=SATCHEL_GRO_TEXTURE_BASE+SATCHEL_GTEX_GPU_ADDRESS){SatchelGuestTextureRequest q={r->operation-SATCHEL_GRO_TEXTURE_BASE,r->x[0],r->x[1],r->x[2]};SatchelGuestTextureResult result;s=texture_status(satchel_guest_textures_execute(o->textures,cpu,&q,&result));if(!s && (q.operation>=SATCHEL_GTEX_WIDTH || q.operation==SATCHEL_GTEX_INITIALIZE || q.operation==SATCHEL_GTEX_STORAGE_SIZE || q.operation==SATCHEL_GTEX_STORAGE_ALIGNMENT)){out->set_value=1;out->value=result.value;}goto done;}
 if(r->operation>SATCHEL_GRO_STATE_BASE && r->operation<=SATCHEL_GRO_STATE_BASE+SATCHEL_GNS_STREAM_DIVISOR){SatchelGuestNVNStateRequest q={.operation=r->operation-SATCHEL_GRO_STATE_BASE};memcpy(q.x,r->x,sizeof(q.x));SatchelGuestNVNStateResult result=satchel_guest_nvn_state_apply(cpu,&q,state_guard,o);s=result.status==SATCHEL_GNS_OK?0:result.status==SATCHEL_GNS_MEMORY?SATCHEL_GUEST_POOL_MEMORY:result.status==SATCHEL_GNS_BUSY?SATCHEL_GUEST_POOL_BUSY:result.status==SATCHEL_GNS_ALLOCATION?SATCHEL_GUEST_POOL_ALLOCATION:SATCHEL_GUEST_POOL_UNSUPPORTED;goto done;}
 if(r->operation>=SATCHEL_GRO_ORIGIN_SET && r->operation<=SATCHEL_GRO_DEPTH_GET){Mode *m;s=mode(o,cpu,r->x[0],&m);if(s)goto done;
  if(r->operation==SATCHEL_GRO_ORIGIN_GET || r->operation==SATCHEL_GRO_DEPTH_GET){out->set_value=1;out->value=r->operation==SATCHEL_GRO_ORIGIN_GET?m->origin:m->depth;s=0;goto done;}
  if((uint32_t)r->x[1]>1){s=SATCHEL_GUEST_POOL_UNSUPPORTED;goto done;}uint32_t children=0;s=satchel_guest_transfers_device_children(o->transfers,r->x[0],&children);if(s)goto done;if(children){s=SATCHEL_GUEST_POOL_BUSY;goto done;}if(r->operation==SATCHEL_GRO_ORIGIN_SET)m->origin=(uint32_t)r->x[1];else m->depth=(uint32_t)r->x[1];s=0;goto done;
 }
 s=satchel_guest_transfers_execute(o->transfers,cpu,r,out,cancel,context);
 done:out->status=s;if(s){out->value=0;out->set_value=0;}return s;
}
static SatchelGuestPoolStatus resolve(void *p,SatchelCPUProbe *cpu,uint64_t d,uint64_t a,uint64_t n,SatchelGuestPoolBinding *b){Owner *o=p;return satchel_guest_pools_resolve(o->pools,cpu,d,a,n,b);}
static SatchelGuestPoolStatus children(void *p,uint64_t d,uint32_t *count){Owner *o=p;uint32_t n=0,total=0,s;s=satchel_guest_pools_device_children(o->pools,d,&n);if(s)return (SatchelGuestPoolStatus)s;total+=n;s=satchel_guest_transfers_device_children(o->transfers,d,&n);if(s)return (SatchelGuestPoolStatus)s;total+=n;s=compute_status(satchel_guest_compute_device_children(o->compute,d,&n));if(s)return (SatchelGuestPoolStatus)s;total+=n;s=pair_status(satchel_guest_raster_programs_device_children(o->programs,d,&n));if(s)return (SatchelGuestPoolStatus)s;total+=n;s=texture_status(satchel_guest_textures_device_children(o->textures,d,&n));if(s)return (SatchelGuestPoolStatus)s;*count=total+n;return SATCHEL_GUEST_POOL_OK;}
static SatchelGuestPoolStatus unowned(void *p,uint64_t a,uint64_t n){(void)p;(void)a;(void)n;return SATCHEL_GUEST_POOL_UNSUPPORTED;}
static SatchelGuestPoolStatus unowned_checked(void *p,SatchelCPUProbe *cpu,uint64_t a,uint64_t n){Owner *o=p;return state_guard(o,cpu,a,n)?SATCHEL_GUEST_POOL_OK:SATCHEL_GUEST_POOL_BAD_OBJECT;}
int satchel_guest_dispatch_enable_vulkan_raster(SatchelGuestDispatch *d){SatchelGuestDeviceBackendV2 actual={0};if(!satchel_vulkan_guest_device_backend_v2(&actual))return 0;const SatchelGuestTransferServices services={{{1,create_legacy,destroy,execute_pool,resolve,children,unowned},create_checked,unowned_checked},prepare_retirement,execute_transfer};return satchel_guest_dispatch_attach_transfer_services(d,&actual,&services);}
