#include "GuestPrograms.h"
#include "VulkanBuffer.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
typedef struct {uint64_t lease,gpu,guest_device;SatchelGuestProgramMetadata meta;uint8_t *code,*control;} Capture;
typedef struct {uint32_t state;uint64_t object,device,backend,device_generation,generation;Capture *capture;} Program;
struct SatchelGuestPrograms {SatchelGuestPools *pools;SatchelGuestPoolDeviceValidator validate;SatchelGuestPoolCheckedRangeGuard guard;void *context;atomic_flag active;uint32_t retiring;Program programs[SATCHEL_GP_LIMIT];Capture *pending;};
static _Atomic uint64_t next_generation=1;
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t u64(const uint8_t *p){return u32(p)|(uint64_t)u32(p+4)<<32;}
static void p32(uint8_t *p,uint32_t x){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(x>>(i*8));}
static void p64(uint8_t *p,uint64_t x){p32(p,(uint32_t)x);p32(p+4,(uint32_t)(x>>32));}
static int identity(uint64_t *out){uint64_t n=atomic_load(&next_generation);do{if(n==UINT64_MAX)return 0;}while(!atomic_compare_exchange_weak(&next_generation,&n,n+1));*out=n;return 1;}
static SatchelGuestProgramStatus pool_status(SatchelGuestPoolStatus s){switch(s){case SATCHEL_GUEST_POOL_OK:return SATCHEL_GP_OK;case SATCHEL_GUEST_POOL_BUSY:return SATCHEL_GP_BUSY;case SATCHEL_GUEST_POOL_MEMORY:return SATCHEL_GP_MEMORY;case SATCHEL_GUEST_POOL_ALLOCATION:return SATCHEL_GP_ALLOCATION;case SATCHEL_GUEST_POOL_LIMIT_REACHED:return SATCHEL_GP_LIMIT_REACHED;case SATCHEL_GUEST_POOL_BACKEND:return SATCHEL_GP_BACKEND;default:return SATCHEL_GP_BAD_OBJECT;}}
static SatchelGuestProgramStatus vk_status(SatchelVulkanStatus s){return s==SATCHEL_VULKAN_OK?SATCHEL_GP_OK:(s==SATCHEL_VULKAN_BUSY?SATCHEL_GP_BUSY:SATCHEL_GP_BACKEND);}
static SatchelGuestProgramStatus write(SatchelCPUProbe *cpu,uint64_t a,const void *p,size_t n){SatchelCPUProbeWrite w={a,p,n};switch(satchel_cpu_probe_writev(cpu,&w,1)){case SATCHEL_CPU_WRITE_OK:return SATCHEL_GP_OK;case SATCHEL_CPU_WRITE_BUSY:return SATCHEL_GP_BUSY;case SATCHEL_CPU_WRITE_ALLOCATION:return SATCHEL_GP_ALLOCATION;default:return SATCHEL_GP_MEMORY;}}
static int disjoint(SatchelCPUProbe *cpu,uint64_t a,uint64_t n,uint64_t b,uint64_t m){int overlap=1;return satchel_cpu_probe_physical_overlap(cpu,a,n,b,m,&overlap) && !overlap;}
static Program *find(SatchelGuestPrograms *o,uint64_t a){for(unsigned i=0;i<SATCHEL_GP_LIMIT;i++)if(o->programs[i].state && o->programs[i].object==a)return &o->programs[i];return NULL;}
static SatchelGuestProgramStatus release(SatchelGuestPrograms *o,Capture **c){if(!*c)return SATCHEL_GP_OK;if((*c)->lease){SatchelGuestProgramStatus s=pool_status(satchel_guest_pools_release(o->pools,(*c)->lease));if(s!=SATCHEL_GP_OK)return s;(*c)->lease=0;}free((*c)->code);free((*c)->control);free(*c);*c=NULL;return SATCHEL_GP_OK;}
static SatchelGuestProgramStatus object_range(SatchelGuestPrograms *o,SatchelCPUProbe *cpu,uint64_t a,Program *self){
 if(!satchel_cpu_probe_check_range(cpu,a,SATCHEL_GP_OBJECT_SIZE,SATCHEL_CPU_READ|SATCHEL_CPU_WRITE))return SATCHEL_GP_MEMORY;
 if(o->guard && !o->guard(o->context,cpu,a,SATCHEL_GP_OBJECT_SIZE))return SATCHEL_GP_BAD_OBJECT;
 SatchelGuestProgramStatus s=pool_status(satchel_guest_pools_check_unowned_range_checked(o->pools,cpu,a,SATCHEL_GP_OBJECT_SIZE));if(s!=SATCHEL_GP_OK)return s;
 for(unsigned i=0;i<SATCHEL_GP_LIMIT;i++){Program *p=&o->programs[i];if(p->state && p!=self && !disjoint(cpu,a,SATCHEL_GP_OBJECT_SIZE,p->object,SATCHEL_GP_OBJECT_SIZE))return SATCHEL_GP_BAD_OBJECT;}
 return SATCHEL_GP_OK;
}
static SatchelGuestProgramStatus valid(SatchelGuestPrograms *o,SatchelCPUProbe *cpu,Program *p){
 if(!p || p->state!=1)return SATCHEL_GP_BAD_OBJECT;uint64_t token=0,generation=0;
 if(!o->validate(o->context,cpu,p->device,&token,&generation) || token!=p->backend || generation!=p->device_generation)return SATCHEL_GP_BAD_OBJECT;
 SatchelGuestProgramStatus s=object_range(o,cpu,p->object,p);if(s!=SATCHEL_GP_OK)return s;
 if(!disjoint(cpu,p->object,SATCHEL_GP_OBJECT_SIZE,p->device,0x3000))return SATCHEL_GP_BAD_OBJECT;
 uint8_t bytes[16];if(!satchel_cpu_probe_read(cpu,p->object,bytes,16))return SATCHEL_GP_MEMORY;
 return u32(bytes)==0x504c5052 && u32(bytes+4)==1 && u64(bytes+8)==p->generation?SATCHEL_GP_OK:SATCHEL_GP_BAD_OBJECT;
}
static SatchelGuestProgramStatus enter(SatchelGuestPrograms *o,SatchelCPUProbe *cpu){if(!o || !cpu)return SATCHEL_GP_INVALID;if(atomic_flag_test_and_set(&o->active))return SATCHEL_GP_BUSY;if(o->retiring){atomic_flag_clear(&o->active);return SATCHEL_GP_INVALID;}return SATCHEL_GP_OK;}
static SatchelGuestProgramStatus leave(SatchelGuestPrograms *o,SatchelGuestProgramStatus s){atomic_flag_clear(&o->active);return s;}
SatchelGuestPrograms *satchel_guest_programs_create(SatchelGuestPools *pools,SatchelGuestPoolDeviceValidator validate,SatchelGuestPoolCheckedRangeGuard guard,void *context){if(!pools || !validate)return NULL;SatchelGuestPrograms *o=calloc(1,sizeof(*o));if(o){o->pools=pools;o->validate=validate;o->guard=guard;o->context=context;atomic_flag_clear(&o->active);}return o;}
SatchelGuestProgramStatus satchel_guest_programs_identity(SatchelGuestPrograms *o,SatchelCPUProbe *cpu,uint64_t address,SatchelGuestProgramIdentity *out){
 if(!out)return SATCHEL_GP_INVALID;SatchelGuestProgramStatus s=enter(o,cpu);if(s!=SATCHEL_GP_OK)return s;
 Program *p=find(o,address);s=valid(o,cpu,p);if(s==SATCHEL_GP_OK)*out=(SatchelGuestProgramIdentity){p->device,p->backend,p->device_generation,p->generation};return leave(o,s);
}
SatchelGuestProgramStatus satchel_guest_programs_initialize(SatchelGuestPrograms *o,SatchelCPUProbe *cpu,uint64_t a,uint64_t d){
 SatchelGuestProgramStatus s=enter(o,cpu);if(s!=SATCHEL_GP_OK)return s;
 if(find(o,a))return leave(o,SATCHEL_GP_BAD_OBJECT);s=object_range(o,cpu,a,NULL);if(s!=SATCHEL_GP_OK)return leave(o,s);
 uint64_t token=0,generation=0;if(!o->validate(o->context,cpu,d,&token,&generation) || !token || !generation || !disjoint(cpu,a,SATCHEL_GP_OBJECT_SIZE,d,0x3000))return leave(o,SATCHEL_GP_BAD_OBJECT);
 Program *p=NULL;for(unsigned i=0;i<SATCHEL_GP_LIMIT;i++)if(!o->programs[i].state){p=&o->programs[i];break;}if(!p)return leave(o,SATCHEL_GP_LIMIT_REACHED);
 uint64_t id;if(!identity(&id))return leave(o,SATCHEL_GP_LIMIT_REACHED);uint8_t tag[SATCHEL_GP_OBJECT_SIZE]={0};p32(tag,0x504c5052);p32(tag+4,1);p64(tag+8,id);
 s=write(cpu,a,tag,sizeof(tag));if(s==SATCHEL_GP_OK)*p=(Program){1,a,d,token,generation,id,NULL};return leave(o,s);
}
static SatchelGuestProgramStatus parse(Capture *c){
 const uint8_t *ctl=c->control;SatchelGuestProgramMetadata *m=&c->meta;
 if(u32(ctl)!=0x98761234 || u32(ctl+0x20)!=m->control_size || u32(ctl+0x704)!=m->code_size)return SATCHEL_GP_INVALID;
 const uint32_t c1=u32(ctl+0x700);if(!c1 || c1>m->code_size || (c1&7))return SATCHEL_GP_INVALID;
 m->c1_offset=c1;m->c1_size=m->code_size-c1;m->local_x=u32(ctl+0x748);m->local_y=u32(ctl+0x74c);m->local_z=u32(ctl+0x750);m->shared_memory_size=u32(ctl+0x754);
 if(!m->local_x || !m->local_y || !m->local_z || m->local_x>1024 || m->local_y>1024 || m->local_z>1024 || (uint64_t)m->local_x*m->local_y*m->local_z>1024 || m->shared_memory_size>65536)return SATCHEL_GP_UNSUPPORTED;
 if(u32(c->code)==0x12345678)return SATCHEL_GP_UNSUPPORTED;
 if(u32(c->code)==0x12345679){if(c1<=0x100)return SATCHEL_GP_INVALID;for(unsigned i=0x30;i<0x100;i++)if(c->code[i])return SATCHEL_GP_UNSUPPORTED;m->instruction_offset=0x100;}
 m->instruction_size=c1-m->instruction_offset;if(!m->instruction_size || (m->instruction_size&7))return SATCHEL_GP_INVALID;
 m->stage=5;m->capture_only=1;m->compiler_ready=0;m->local_memory_known=0;m->local_memory_size=0;return SATCHEL_GP_OK;
}
SatchelGuestProgramStatus satchel_guest_programs_set_shaders_admitted(SatchelGuestPrograms *o,SatchelCPUProbe *cpu,uint64_t a,int32_t count,uint64_t data,SatchelGuestProgramAdmission admit,void *context,uint64_t *generation){
 SatchelGuestProgramStatus s=enter(o,cpu);if(s!=SATCHEL_GP_OK)return s;s=release(o,&o->pending);if(s!=SATCHEL_GP_OK)return leave(o,s);
 Program *p=find(o,a);s=valid(o,cpu,p);if(s!=SATCHEL_GP_OK)return leave(o,s);if(count!=1)return leave(o,count<0?SATCHEL_GP_INVALID:SATCHEL_GP_UNSUPPORTED);
 uint8_t sd[16],head[0x24];if(!satchel_cpu_probe_read(cpu,data,sd,16))return leave(o,SATCHEL_GP_MEMORY);const uint64_t gpu=u64(sd),control=u64(sd+8);
 if(!satchel_cpu_probe_read(cpu,control,head,sizeof(head)))return leave(o,SATCHEL_GP_MEMORY);uint32_t nctl=u32(head+0x20);if(u32(head)!=0x98761234)return leave(o,SATCHEL_GP_INVALID);if(nctl<0x758 || nctl>SATCHEL_GP_CONTROL_LIMIT)return leave(o,SATCHEL_GP_UNSUPPORTED);
 Capture *c=calloc(1,sizeof(*c));if(!c)return leave(o,SATCHEL_GP_ALLOCATION);c->gpu=gpu;c->guest_device=p->device;c->meta=(SatchelGuestProgramMetadata){.version=1,.struct_size=sizeof(c->meta),.control_size=nctl};o->pending=c;
 c->control=malloc(nctl);if(!c->control){s=SATCHEL_GP_ALLOCATION;goto fail;}if(!satchel_cpu_probe_read(cpu,control,c->control,nctl)){s=SATCHEL_GP_MEMORY;goto fail;}
 uint32_t ncode=u32(c->control+0x704);if(ncode<8 || ncode>SATCHEL_GP_CODE_LIMIT || (ncode&7)){s=SATCHEL_GP_UNSUPPORTED;goto fail;}c->meta.code_size=ncode;
 SatchelGuestPoolView view={0};s=pool_status(satchel_guest_pools_retain_range(o->pools,cpu,p->device,gpu,ncode,&c->lease,&view));if(s!=SATCHEL_GP_OK)goto fail;c->meta.pool_generation=view.generation;
 SatchelGuestPoolBinding binding={0};s=pool_status(satchel_guest_pools_lease_binding(o->pools,cpu,c->lease,gpu,ncode,&binding));if(s!=SATCHEL_GP_OK)goto fail;
 c->code=malloc(ncode);if(!c->code){s=SATCHEL_GP_ALLOCATION;goto fail;}s=vk_status(satchel_vulkan_buffer_read(p->backend,binding.buffer,binding.offset,c->code,ncode));if(s!=SATCHEL_GP_OK)goto fail;
 s=parse(c);if(s!=SATCHEL_GP_OK)goto fail;if(admit){s=admit(context,c->code,&c->meta);if(s!=SATCHEL_GP_OK)goto fail;}if(!identity(&c->meta.generation)){s=SATCHEL_GP_LIMIT_REACHED;goto fail;}
 s=release(o,&p->capture);if(s!=SATCHEL_GP_OK)goto fail;p->capture=c;o->pending=NULL;if(generation)*generation=c->meta.generation;return leave(o,SATCHEL_GP_OK);
 fail:{SatchelGuestProgramStatus cleanup=release(o,&o->pending);return leave(o,cleanup==SATCHEL_GP_OK?s:cleanup);}
}
static SatchelGuestProgramStatus current(SatchelGuestPrograms *o,SatchelCPUProbe *cpu,Program *p){
 SatchelGuestProgramStatus s=valid(o,cpu,p);if(s!=SATCHEL_GP_OK)return s;Capture *c=p->capture;if(!c)return SATCHEL_GP_BAD_OBJECT;
 SatchelGuestPoolBinding binding={0};s=pool_status(satchel_guest_pools_lease_binding(o->pools,cpu,c->lease,c->gpu,c->meta.code_size,&binding));if(s!=SATCHEL_GP_OK)return s;if(binding.pool_generation!=c->meta.pool_generation)return SATCHEL_GP_BAD_OBJECT;
 uint8_t *copy=malloc(c->meta.code_size);if(!copy)return SATCHEL_GP_ALLOCATION;s=vk_status(satchel_vulkan_buffer_read(p->backend,binding.buffer,binding.offset,copy,c->meta.code_size));if(s==SATCHEL_GP_OK && memcmp(copy,c->code,c->meta.code_size))s=SATCHEL_GP_CHANGED;free(copy);return s;
}
SatchelGuestProgramStatus satchel_guest_programs_metadata(SatchelGuestPrograms *o,SatchelCPUProbe *cpu,uint64_t a,SatchelGuestProgramMetadata *out){if(!out)return SATCHEL_GP_INVALID;SatchelGuestProgramStatus s=enter(o,cpu);if(s!=SATCHEL_GP_OK)return s;Program *p=find(o,a);s=current(o,cpu,p);if(s==SATCHEL_GP_OK)*out=p->capture->meta;return leave(o,s);}
static int host_disjoint(const void *a,size_t n,const void *b,size_t m){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=UINTPTR_MAX-n && y<=UINTPTR_MAX-m && (x+n<=y || y+m<=x);}
SatchelGuestProgramStatus satchel_guest_programs_copy(SatchelGuestPrograms *o,SatchelCPUProbe *cpu,uint64_t a,void *code,size_t nc,void *control,size_t nt,SatchelGuestProgramMetadata *out){
 if(!code || !control || !out)return SATCHEL_GP_INVALID;SatchelGuestProgramStatus s=enter(o,cpu);if(s!=SATCHEL_GP_OK)return s;Program *p=find(o,a);s=current(o,cpu,p);if(s==SATCHEL_GP_OK){Capture *c=p->capture;
 if(nc<c->meta.code_size || nt<c->meta.control_size || !host_disjoint(code,c->meta.code_size,control,c->meta.control_size) || !host_disjoint(code,c->meta.code_size,out,sizeof(*out)) || !host_disjoint(control,c->meta.control_size,out,sizeof(*out)))s=SATCHEL_GP_INVALID;
 else {memcpy(code,c->code,c->meta.code_size);memcpy(control,c->control,c->meta.control_size);*out=c->meta;}}
 return leave(o,s);
}
SatchelGuestProgramStatus satchel_guest_programs_finalize(SatchelGuestPrograms *o,SatchelCPUProbe *cpu,uint64_t a){
 SatchelGuestProgramStatus s=enter(o,cpu);if(s!=SATCHEL_GP_OK)return s;Program *p=find(o,a);if(!p)return leave(o,SATCHEL_GP_BAD_OBJECT);
 if(p->state==1){s=valid(o,cpu,p);if(s!=SATCHEL_GP_OK)return leave(o,s);uint8_t empty[SATCHEL_GP_OBJECT_SIZE]={0};s=write(cpu,a,empty,sizeof(empty));if(s!=SATCHEL_GP_OK)return leave(o,s);p->state=2;}
 s=release(o,&p->capture);if(s==SATCHEL_GP_OK)memset(p,0,sizeof(*p));return leave(o,s);
}
SatchelGuestProgramStatus satchel_guest_programs_device_children(SatchelGuestPrograms *o,uint64_t device,uint32_t *count){if(!o || !count)return SATCHEL_GP_INVALID;if(atomic_flag_test_and_set(&o->active))return SATCHEL_GP_BUSY;uint32_t n=0;for(unsigned i=0;i<SATCHEL_GP_LIMIT;i++)n+=o->programs[i].state && o->programs[i].device==device;if(o->pending && o->pending->guest_device==device)++n;*count=n;return leave(o,SATCHEL_GP_OK);}
SatchelGuestProgramStatus satchel_guest_programs_check_unowned_range(SatchelGuestPrograms *o,SatchelCPUProbe *cpu,uint64_t a,uint64_t n){SatchelGuestProgramStatus s=enter(o,cpu);if(s!=SATCHEL_GP_OK)return s;if(!n || a>UINT64_MAX-n)return leave(o,SATCHEL_GP_INVALID);for(unsigned i=0;i<SATCHEL_GP_LIMIT;i++)if(o->programs[i].state && !disjoint(cpu,a,n,o->programs[i].object,SATCHEL_GP_OBJECT_SIZE))return leave(o,SATCHEL_GP_BAD_OBJECT);return leave(o,SATCHEL_GP_OK);}
SatchelGuestProgramStatus satchel_guest_programs_try_destroy(SatchelGuestPrograms **pointer){if(!pointer)return SATCHEL_GP_INVALID;SatchelGuestPrograms *o=*pointer;if(!o)return SATCHEL_GP_OK;if(atomic_flag_test_and_set(&o->active))return SATCHEL_GP_BUSY;o->retiring=1;SatchelGuestProgramStatus s=release(o,&o->pending);if(s!=SATCHEL_GP_OK)return leave(o,s);
 for(unsigned i=0;i<SATCHEL_GP_LIMIT;i++){s=release(o,&o->programs[i].capture);if(s!=SATCHEL_GP_OK)return leave(o,s);memset(&o->programs[i],0,sizeof(Program));}free(o);*pointer=NULL;return SATCHEL_GP_OK;
}

SatchelGuestProgramStatus satchel_guest_programs_pool_guard(const SatchelGuestPrograms *o,SatchelCPUProbe *cpu,uint64_t a,uint64_t n){
 if(!o || !cpu || !n || a>UINT64_MAX-n)return SATCHEL_GP_INVALID;
 for(unsigned i=0;i<SATCHEL_GP_LIMIT;i++)if(o->programs[i].state && !disjoint(cpu,a,n,o->programs[i].object,SATCHEL_GP_OBJECT_SIZE))return SATCHEL_GP_BAD_OBJECT;
 return SATCHEL_GP_OK;
}

SatchelGuestProgramStatus satchel_guest_programs_code_disjoint(SatchelGuestPrograms *o,SatchelCPUProbe *cpu,uint64_t program,uint64_t address,uint64_t size){
 SatchelGuestProgramStatus s=enter(o,cpu);if(s!=SATCHEL_GP_OK)return s;if(!size || address>UINT64_MAX-size)return leave(o,SATCHEL_GP_INVALID);
 Program *p=find(o,program);s=current(o,cpu,p);if(s!=SATCHEL_GP_OK)return leave(o,s);Capture *c=p->capture;SatchelGuestPoolView view={0};
 s=pool_status(satchel_guest_pools_lease_view(o->pools,cpu,c->lease,&view));if(s!=SATCHEL_GP_OK)return leave(o,s);
 if(c->gpu<view.gpu_address || c->gpu-view.gpu_address>=view.size || c->meta.code_size>view.size-(c->gpu-view.gpu_address))return leave(o,SATCHEL_GP_BAD_OBJECT);
 uint64_t code_address=view.cpu_address+(c->gpu-view.gpu_address);
 return leave(o,disjoint(cpu,address,size,code_address,c->meta.code_size)?SATCHEL_GP_OK:SATCHEL_GP_UNSUPPORTED);
}

SatchelGuestProgramStatus satchel_guest_programs_set_shaders(SatchelGuestPrograms *o,SatchelCPUProbe *cpu,uint64_t a,int32_t n,uint64_t data){return satchel_guest_programs_set_shaders_admitted(o,cpu,a,n,data,NULL,NULL,NULL);}
