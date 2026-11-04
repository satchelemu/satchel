#include "GuestCapturedPrograms.h"
#include "VulkanBuffer.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct {uint64_t lease,gpu;uint8_t *code,*control;SatchelGuestCapturedStage metadata;} Stage;
typedef struct Capture {
    uint64_t program,generation;SatchelGuestRasterIdentity identity;uint32_t references;
    SatchelCFAdmission admission;
    Stage stages[2]; /* canonical vertex,fragment order after admission */
} Capture;
typedef struct {uint64_t token;Capture *capture;} Reference;
struct SatchelGuestCapturedPrograms {
    SatchelGuestPools *pools;SatchelGuestRasterIdentityValidator validate;void *context;
    atomic_flag active;uint32_t retiring;
    Capture *captures[SATCHEL_GCAP_PROGRAM_LIMIT],*pending,*retired;
    Reference references[SATCHEL_GCAP_REFERENCE_LIMIT];
};
static _Atomic uint64_t sequence=1;
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t u64(const uint8_t *p){return u32(p)|(uint64_t)u32(p+4)<<32;}
static int identity_equal(const SatchelGuestRasterIdentity *a,const SatchelGuestRasterIdentity *b){return a->guest_device==b->guest_device && a->backend_device==b->backend_device && a->device_generation==b->device_generation && a->program_generation==b->program_generation;}
static int identity_next(uint64_t *out){uint64_t v=atomic_load(&sequence);do{if(v==UINT64_MAX)return 0;}while(!atomic_compare_exchange_weak(&sequence,&v,v+1));*out=v;return 1;}
static int host_overlap(const void *a,size_t n,const void *b,size_t m){uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x>UINTPTR_MAX-n || y>UINTPTR_MAX-m || (x<y+m && y<x+n);}
static uint32_t pool_status(SatchelGuestPoolStatus s){switch(s){case SATCHEL_GUEST_POOL_OK:return SATCHEL_GCAP_OK;case SATCHEL_GUEST_POOL_BUSY:return SATCHEL_GCAP_BUSY;case SATCHEL_GUEST_POOL_MEMORY:return SATCHEL_GCAP_MEMORY;case SATCHEL_GUEST_POOL_ALLOCATION:return SATCHEL_GCAP_ALLOCATION;case SATCHEL_GUEST_POOL_LIMIT_REACHED:return SATCHEL_GCAP_LIMIT;case SATCHEL_GUEST_POOL_BACKEND:return SATCHEL_GCAP_BACKEND;case SATCHEL_GUEST_POOL_UNSUPPORTED:return SATCHEL_GCAP_UNSUPPORTED;default:return SATCHEL_GCAP_BAD_OBJECT;}}
static uint32_t backend_status(SatchelVulkanStatus s){return s==SATCHEL_VULKAN_OK?SATCHEL_GCAP_OK:(s==SATCHEL_VULKAN_BUSY?SATCHEL_GCAP_BUSY:SATCHEL_GCAP_BACKEND);}
static uint32_t compiler_status(int s){return s==SATCHEL_CF_OK?SATCHEL_GCAP_OK:s==SATCHEL_CF_LIMIT?SATCHEL_GCAP_LIMIT:s==SATCHEL_CF_BUSY?SATCHEL_GCAP_BUSY:(s==SATCHEL_CF_CANCELLED||s==SATCHEL_CF_DEADLINE)?SATCHEL_GCAP_CANCELLED:SATCHEL_GCAP_UNSUPPORTED;}
static uint32_t enter(SatchelGuestCapturedPrograms *o,int retirement){if(!o)return SATCHEL_GCAP_INVALID;if(atomic_flag_test_and_set(&o->active))return SATCHEL_GCAP_BUSY;if(o->retiring && !retirement){atomic_flag_clear(&o->active);return SATCHEL_GCAP_INVALID;}return SATCHEL_GCAP_OK;}
static uint32_t leave(SatchelGuestCapturedPrograms *o,uint32_t s){atomic_flag_clear(&o->active);return s;}
static Capture **find(SatchelGuestCapturedPrograms *o,uint64_t program){for(unsigned i=0;i<SATCHEL_GCAP_PROGRAM_LIMIT;i++)if(o->captures[i] && o->captures[i]->program==program)return o->captures+i;return NULL;}
static Reference *reference(SatchelGuestCapturedPrograms *o,uint64_t token){for(unsigned i=0;i<SATCHEL_GCAP_REFERENCE_LIMIT;i++)if(token && o->references[i].token==token)return o->references+i;return NULL;}
static uint32_t release_capture(SatchelGuestCapturedPrograms *o,Capture **ptr){
    Capture *c=*ptr;if(!c)return SATCHEL_GCAP_OK;if(c->references)return SATCHEL_GCAP_BUSY;
    for(unsigned i=0;i<2;i++)if(c->stages[i].lease){uint32_t s=pool_status(satchel_guest_pools_release(o->pools,c->stages[i].lease));if(s)return s;c->stages[i].lease=0;}
    for(unsigned i=0;i<2;i++){free(c->stages[i].code);free(c->stages[i].control);}free(c);*ptr=NULL;return SATCHEL_GCAP_OK;
}
static uint32_t collect(SatchelGuestCapturedPrograms *o){uint32_t s=release_capture(o,&o->pending);return s?s:release_capture(o,&o->retired);}
static uint32_t checked_identity(SatchelGuestCapturedPrograms *o,SatchelCPUProbe *cpu,uint64_t program,SatchelGuestRasterIdentity *id,uint32_t *family){
    if(!cpu)return SATCHEL_GCAP_INVALID;SatchelGuestRasterIdentity value={0};uint32_t kind=UINT32_MAX;
    if(!o->validate(o->context,cpu,program,&value,&kind) || !value.guest_device || !value.backend_device || !value.device_generation || !value.program_generation)return SATCHEL_GCAP_BAD_OBJECT;
    if(kind>SATCHEL_GCAP_FAMILY_COMPUTE)return SATCHEL_GCAP_BAD_OBJECT;if(kind==SATCHEL_GCAP_FAMILY_COMPUTE)return SATCHEL_GCAP_UNSUPPORTED;
    *id=value;*family=kind;return SATCHEL_GCAP_OK;
}
static SatchelGuestCapturedProgramInfo info(const Capture *c,uint32_t compiled){
    return (SatchelGuestCapturedProgramInfo){.version=1,.size=sizeof(SatchelGuestCapturedProgramInfo),.nvn_stage_mask=SATCHEL_GCAP_NVN_VERTEX_BIT|SATCHEL_GCAP_NVN_FRAGMENT_BIT,.admitted=1,.compiled=compiled,.program_generation=c->identity.program_generation,.capture_generation=c->generation,.vertex=c->stages[0].metadata,.fragment=c->stages[1].metadata,.source_requirements=c->admission};
}
static uint32_t current(SatchelGuestCapturedPrograms *o,SatchelCPUProbe *cpu,Capture *c){
    if(!c)return SATCHEL_GCAP_BAD_OBJECT;SatchelGuestRasterIdentity id;uint32_t family=0;uint32_t status=checked_identity(o,cpu,c->program,&id,&family);if(status)return status;
    if(family!=SATCHEL_GCAP_FAMILY_RASTER || !identity_equal(&id,&c->identity))return SATCHEL_GCAP_BAD_OBJECT;
    for(unsigned i=0;i<2;i++){
        Stage *s=c->stages+i;SatchelGuestPoolBinding b={0};status=pool_status(satchel_guest_pools_lease_binding(o->pools,cpu,s->lease,s->gpu,s->metadata.code_size,&b));if(status)return status;
        if(b.pool_generation!=s->metadata.pool_generation)return SATCHEL_GCAP_BAD_OBJECT;
        uint8_t *copy=malloc(s->metadata.code_size);if(!copy)return SATCHEL_GCAP_ALLOCATION;
        status=backend_status(satchel_vulkan_buffer_read(id.backend_device,b.buffer,b.offset,copy,s->metadata.code_size));
        if(!status && memcmp(copy,s->code,s->metadata.code_size))status=SATCHEL_GCAP_CHANGED;free(copy);if(status)return status;
    }return SATCHEL_GCAP_OK;
}
static uint32_t capture_stage(SatchelGuestCapturedPrograms *o,SatchelCPUProbe *cpu,Capture *c,Stage *s,const uint8_t sd[16]){
    s->gpu=u64(sd);const uint64_t control=u64(sd+8);uint8_t prefix[0x24];
    if(!satchel_cpu_probe_read(cpu,control,prefix,sizeof(prefix)))return SATCHEL_GCAP_MEMORY;
    uint32_t nctl=u32(prefix+0x20);if(u32(prefix)!=0x98761234)return SATCHEL_GCAP_UNSUPPORTED;
    if(nctl<0x760 || nctl>SATCHEL_GCAP_CONTROL_LIMIT)return SATCHEL_GCAP_UNSUPPORTED;
    s->control=malloc(nctl);if(!s->control)return SATCHEL_GCAP_ALLOCATION;
    if(!satchel_cpu_probe_read(cpu,control,s->control,nctl))return SATCHEL_GCAP_MEMORY;
    if(u32(s->control)!=0x98761234 || u32(s->control+0x20)!=nctl)return SATCHEL_GCAP_CHANGED;
    uint32_t ncode=u32(s->control+0x704),c1=u32(s->control+0x700);
    if(ncode<0x88 || ncode>SATCHEL_GCAP_CODE_LIMIT || (ncode&7) || c1<=0x80 || c1>ncode || (c1&7) || c1-0x80>SATCHEL_CF_CODE_BYTES)return SATCHEL_GCAP_UNSUPPORTED;
    s->metadata.control_size=nctl;s->metadata.code_size=ncode;s->metadata.sph_offset=0x30;s->metadata.instruction_offset=0x80;s->metadata.instruction_size=c1-0x80;s->metadata.c1_offset=c1;s->metadata.c1_size=ncode-c1;
    SatchelGuestPoolView view={0};uint32_t status=pool_status(satchel_guest_pools_retain_range(o->pools,cpu,c->identity.guest_device,s->gpu,ncode,&s->lease,&view));if(status)return status;s->metadata.pool_generation=view.generation;
    SatchelGuestPoolBinding binding={0};status=pool_status(satchel_guest_pools_lease_binding(o->pools,cpu,s->lease,s->gpu,ncode,&binding));if(status)return status;
    s->code=malloc(ncode);if(!s->code)return SATCHEL_GCAP_ALLOCATION;
    status=backend_status(satchel_vulkan_buffer_read(c->identity.backend_device,binding.buffer,binding.offset,s->code,ncode));if(status)return status;
    if(u32(s->code)!=0x12345678)return SATCHEL_GCAP_UNSUPPORTED;
    const uint32_t stage=(u32(s->code+0x30)>>10)&15;if(stage!=SATCHEL_GCAP_SPH_VERTEX && stage!=SATCHEL_GCAP_SPH_FRAGMENT)return SATCHEL_GCAP_UNSUPPORTED;
    /* Pinned control parser identifies early-fragment-test and neighboring
     * fragment flags here. Their behavior is not represented by this first
     * pipeline profile; preserve explicit rejection rather than defaulting. */
    if(stage==SATCHEL_GCAP_SPH_FRAGMENT)for(unsigned i=0x718;i<=0x71d;i++)if(s->control[i])return SATCHEL_GCAP_UNSUPPORTED;
    s->metadata.sph_stage=stage;s->metadata.nvn_stage_bit=stage==SATCHEL_GCAP_SPH_VERTEX?SATCHEL_GCAP_NVN_VERTEX_BIT:SATCHEL_GCAP_NVN_FRAGMENT_BIT;return SATCHEL_GCAP_OK;
}
SatchelGuestCapturedPrograms *satchel_guest_captured_programs_create(SatchelGuestPools *pools,SatchelGuestRasterIdentityValidator validate,void *context){if(!pools || !validate)return NULL;SatchelGuestCapturedPrograms *o=calloc(1,sizeof(*o));if(o){o->pools=pools;o->validate=validate;o->context=context;atomic_flag_clear(&o->active);}return o;}
uint32_t satchel_guest_captured_programs_capture(SatchelGuestCapturedPrograms *o,SatchelCPUProbe *cpu,uint64_t program,int32_t count,uint64_t data,SatchelGuestCapturedProgramInfo *out){
    if(!cpu || !out)return SATCHEL_GCAP_INVALID;uint32_t status=enter(o,0);if(status)return status;
    if(count!=2)return leave(o,count<0?SATCHEL_GCAP_INVALID:SATCHEL_GCAP_UNSUPPORTED);
    SatchelGuestRasterIdentity id;uint32_t family=0;status=checked_identity(o,cpu,program,&id,&family);if(status)return leave(o,status);
    Capture **slot=find(o,program);if(slot && (!identity_equal(&id,&(*slot)->identity) || family!=SATCHEL_GCAP_FAMILY_RASTER))return leave(o,SATCHEL_GCAP_BAD_OBJECT);
    if(slot && (*slot)->references)return leave(o,SATCHEL_GCAP_BUSY);
    if(!slot){for(unsigned i=0;i<SATCHEL_GCAP_PROGRAM_LIMIT;i++)if(!o->captures[i]){slot=o->captures+i;break;}if(!slot)return leave(o,SATCHEL_GCAP_LIMIT);}
    status=collect(o);if(status)return leave(o,status);uint8_t entries[32];if(!satchel_cpu_probe_read(cpu,data,entries,sizeof(entries)))return leave(o,SATCHEL_GCAP_MEMORY);
    Capture *candidate=calloc(1,sizeof(*candidate));if(!candidate)return leave(o,SATCHEL_GCAP_ALLOCATION);o->pending=candidate;candidate->identity=id;candidate->program=program;
    status=capture_stage(o,cpu,candidate,candidate->stages,entries);if(!status)status=capture_stage(o,cpu,candidate,candidate->stages+1,entries+16);
    if(!status && candidate->stages[0].metadata.sph_stage==candidate->stages[1].metadata.sph_stage)status=SATCHEL_GCAP_UNSUPPORTED;
    if(!status && candidate->stages[0].metadata.sph_stage==SATCHEL_GCAP_SPH_FRAGMENT){Stage temp=candidate->stages[0];candidate->stages[0]=candidate->stages[1];candidate->stages[1]=temp;}
    if(!status){Stage *v=&candidate->stages[0],*f=&candidate->stages[1];SatchelCFAdmissionInput input={.version=1,.size=sizeof(input),.vertex={v->code+0x30,v->code+0x80,80,v->metadata.instruction_size},.fragment={f->code+0x30,f->code+0x80,80,f->metadata.instruction_size},.max_work=65536,.timeout_ns=1000000000};
        status=compiler_status(satchel_captured_flat_admit(&input,&candidate->admission,sizeof(candidate->admission)));
        if(!status){v->metadata.admission=candidate->admission.vertex;f->metadata.admission=candidate->admission.fragment;}}
    if(!status && !identity_next(&candidate->generation))status=SATCHEL_GCAP_LIMIT;
    if(status){uint32_t cleanup=release_capture(o,&o->pending);return leave(o,cleanup?cleanup:status);}
    if(candidate->stages[0].metadata.sph_stage==SATCHEL_GCAP_SPH_FRAGMENT){Stage temp=candidate->stages[0];candidate->stages[0]=candidate->stages[1];candidate->stages[1]=temp;}
    /* No fallible call after this point. Existing capture remains intact if
     * anything above fails. Old leases retire separately after successful swap. */
    o->retired=*slot;*slot=candidate;o->pending=NULL;*out=info(candidate,0);return leave(o,SATCHEL_GCAP_OK);
}
uint32_t satchel_guest_captured_programs_info(SatchelGuestCapturedPrograms *o,SatchelCPUProbe *cpu,uint64_t program,SatchelGuestCapturedProgramInfo *out){if(!out)return SATCHEL_GCAP_INVALID;uint32_t status=enter(o,0);if(status)return status;Capture **slot=find(o,program);status=current(o,cpu,slot?*slot:NULL);if(!status)*out=info(*slot,0);return leave(o,status);}
uint32_t satchel_guest_captured_programs_retain(SatchelGuestCapturedPrograms *o,SatchelCPUProbe *cpu,uint64_t program,uint64_t *token,SatchelGuestCapturedProgramInfo *out){
    if(!token || !out || host_overlap(token,sizeof(*token),out,sizeof(*out)))return SATCHEL_GCAP_INVALID;uint32_t status=enter(o,0);if(status)return status;Capture **slot=find(o,program);status=current(o,cpu,slot?*slot:NULL);if(status)return leave(o,status);
    Reference *ref=NULL;for(unsigned i=0;i<SATCHEL_GCAP_REFERENCE_LIMIT;i++)if(!o->references[i].token){ref=o->references+i;break;}if(!ref)return leave(o,SATCHEL_GCAP_LIMIT);
    uint64_t id;if(!identity_next(&id))return leave(o,SATCHEL_GCAP_LIMIT);++(*slot)->references;*ref=(Reference){id,*slot};*out=info(*slot,0);*token=id;return leave(o,SATCHEL_GCAP_OK);
}
uint32_t satchel_guest_captured_programs_release(SatchelGuestCapturedPrograms *o,uint64_t token){uint32_t status=enter(o,1);if(status)return status;Reference *ref=reference(o,token);if(!ref || !ref->capture->references)return leave(o,SATCHEL_GCAP_BAD_OBJECT);--ref->capture->references;memset(ref,0,sizeof(*ref));return leave(o,SATCHEL_GCAP_OK);}
uint32_t satchel_guest_captured_programs_compile(SatchelGuestCapturedPrograms *o,SatchelCPUProbe *cpu,uint64_t token,const SatchelCFState *state,uint64_t target,SatchelCFPairV2 *output,SatchelGuestCapturedProgramInfo *out,SatchelVulkanCancelled cancel,void *context){
 if(!state||!output||!out||((uintptr_t)state%_Alignof(SatchelCFState))||((uintptr_t)output%_Alignof(SatchelCFPairV2))||((uintptr_t)out%_Alignof(SatchelGuestCapturedProgramInfo))||host_overlap(output,sizeof(*output),out,sizeof(*out)))return SATCHEL_GCAP_INVALID;
 const SatchelCFState actual=*state;
 // No defaults for missing queue facts. This stage deliberately cannot draw a
 // guest recording whose provoking or YNegate facts remain unobserved.
 if(actual.known!=255 || actual.extra_known!=4095)return SATCHEL_GCAP_STATE_UNAVAILABLE;
 uint32_t status=enter(o,0);if(status)return status;Reference *ref=reference(o,token);Capture *c=ref?ref->capture:NULL;status=current(o,cpu,c);if(status)return leave(o,status);
 Stage *v=&c->stages[0],*f=&c->stages[1];SatchelCapturedRequest request={.version=1,.size=sizeof(request),.vertex={v->code+v->metadata.sph_offset,v->code+v->metadata.instruction_offset,80,v->metadata.instruction_size},.fragment={f->code+f->metadata.sph_offset,f->code+f->metadata.instruction_offset,80,f->metadata.instruction_size},.state=actual,.max_work=65536,.timeout_ns=1000000000,.cancelled=cancel,.context=context};
 SatchelCFPairV2 result;status=compiler_status(satchel_captured_compile_for_target(c->identity.backend_device,target,&request,&result,sizeof(result)));
 // Cancellation callbacks are pure under the owner gate. Still revalidate the
 // live GPU-visible code after the synchronous compiler before publishing it.
 if(!status)status=current(o,cpu,c);
 if(!status){*output=result;*out=info(c,1);}return leave(o,status);
}
uint32_t satchel_guest_captured_programs_code_disjoint(SatchelGuestCapturedPrograms *o,SatchelCPUProbe *cpu,uint64_t token,uint64_t address,uint64_t size){
    if(!size || address>UINT64_MAX-size)return SATCHEL_GCAP_INVALID;uint32_t status=enter(o,0);if(status)return status;Reference *ref=reference(o,token);Capture *c=ref?ref->capture:NULL;status=current(o,cpu,c);if(status)return leave(o,status);
    for(unsigned i=0;i<2;i++){Stage *s=c->stages+i;SatchelGuestPoolView view={0};status=pool_status(satchel_guest_pools_lease_view(o->pools,cpu,s->lease,&view));if(status)return leave(o,status);
        if(s->gpu<view.gpu_address || s->gpu-view.gpu_address>=view.size || s->metadata.code_size>view.size-(s->gpu-view.gpu_address))return leave(o,SATCHEL_GCAP_BAD_OBJECT);
        const uint64_t offset=s->gpu-view.gpu_address;if(view.cpu_address>UINT64_MAX-offset)return leave(o,SATCHEL_GCAP_BAD_OBJECT);int overlap=1;
        if(!satchel_cpu_probe_physical_overlap(cpu,address,size,view.cpu_address+offset,s->metadata.code_size,&overlap))return leave(o,SATCHEL_GCAP_MEMORY);if(overlap)return leave(o,SATCHEL_GCAP_UNSUPPORTED);
    }return leave(o,SATCHEL_GCAP_OK);
}
uint32_t satchel_guest_captured_programs_code_bindings(SatchelGuestCapturedPrograms *o,SatchelCPUProbe *cpu,uint64_t token,SatchelGuestCapturedCodeBinding out[2]){
    if(!out)return SATCHEL_GCAP_INVALID;uint32_t status=enter(o,0);if(status)return status;
    Reference *ref=reference(o,token);Capture *c=ref?ref->capture:NULL;status=current(o,cpu,c);if(status)return leave(o,status);
    SatchelGuestCapturedCodeBinding candidate[2]={0};
    for(unsigned i=0;i<2;i++){
        const Stage *s=c->stages+i;SatchelGuestCapturedCodeBinding *b=candidate+i;
        status=pool_status(satchel_guest_pools_lease_view(o->pools,cpu,s->lease,&b->pool));if(status)return leave(o,status);
        status=pool_status(satchel_guest_pools_lease_binding(o->pools,cpu,s->lease,s->gpu,s->metadata.code_size,&b->binding));if(status)return leave(o,status);
        if(b->pool.generation!=s->metadata.pool_generation || b->binding.pool_generation!=b->pool.generation || b->pool.guest_device!=c->identity.guest_device || s->gpu<b->pool.gpu_address)return leave(o,SATCHEL_GCAP_BAD_OBJECT);
        const uint64_t offset=s->gpu-b->pool.gpu_address;
        if(offset>=b->pool.size || s->metadata.code_size>b->pool.size-offset || b->pool.cpu_address>UINT64_MAX-offset || b->binding.offset!=offset || b->binding.size!=s->metadata.code_size)return leave(o,SATCHEL_GCAP_BAD_OBJECT);
        b->sph_stage=s->metadata.sph_stage;b->nvn_stage_bit=s->metadata.nvn_stage_bit;b->capture_generation=c->generation;
        b->code_gpu=s->gpu;b->code_cpu=b->pool.cpu_address+offset;b->code_size=s->metadata.code_size;
    }
    memcpy(out,candidate,sizeof(candidate));return leave(o,SATCHEL_GCAP_OK);
}
uint32_t satchel_guest_captured_programs_references(SatchelGuestCapturedPrograms *o,uint64_t program,uint64_t generation,uint32_t *out){if(!out)return SATCHEL_GCAP_INVALID;uint32_t status=enter(o,1);if(status)return status;Capture **slot=find(o,program);if(slot && (*slot)->identity.program_generation!=generation)return leave(o,SATCHEL_GCAP_BAD_OBJECT);*out=slot?(*slot)->references:0;return leave(o,SATCHEL_GCAP_OK);}
uint32_t satchel_guest_captured_programs_forget(SatchelGuestCapturedPrograms *o,uint64_t program,uint64_t generation){
    uint32_t status=enter(o,1);if(status)return status;Capture **slot=find(o,program);if(!slot){status=collect(o);return leave(o,status);}if((*slot)->identity.program_generation!=generation)return leave(o,SATCHEL_GCAP_BAD_OBJECT);if((*slot)->references)return leave(o,SATCHEL_GCAP_BUSY);
    status=collect(o);if(status)return leave(o,status);o->retired=*slot;*slot=NULL;status=release_capture(o,&o->retired);return leave(o,status);
}
uint32_t satchel_guest_captured_programs_device_children(SatchelGuestCapturedPrograms *o,uint64_t device,uint32_t *out){if(!out)return SATCHEL_GCAP_INVALID;uint32_t status=enter(o,1);if(status)return status;uint32_t n=0;for(unsigned i=0;i<SATCHEL_GCAP_PROGRAM_LIMIT;i++)if(o->captures[i] && o->captures[i]->identity.guest_device==device)n++;if(o->pending && o->pending->identity.guest_device==device)n++;if(o->retired && o->retired->identity.guest_device==device)n++;*out=n;return leave(o,SATCHEL_GCAP_OK);}
uint32_t satchel_guest_captured_programs_collect(SatchelGuestCapturedPrograms *o){uint32_t status=enter(o,1);if(status)return status;return leave(o,collect(o));}
uint32_t satchel_guest_captured_programs_try_destroy(SatchelGuestCapturedPrograms **ptr){
    if(!ptr)return SATCHEL_GCAP_INVALID;SatchelGuestCapturedPrograms *o=*ptr;if(!o)return SATCHEL_GCAP_OK;uint32_t status=enter(o,1);if(status)return status;
    for(unsigned i=0;i<SATCHEL_GCAP_REFERENCE_LIMIT;i++)if(o->references[i].token)return leave(o,SATCHEL_GCAP_BUSY);
    o->retiring=1;status=collect(o);if(status)return leave(o,status);for(unsigned i=0;i<SATCHEL_GCAP_PROGRAM_LIMIT;i++){status=release_capture(o,o->captures+i);if(status)return leave(o,status);}free(o);*ptr=NULL;return SATCHEL_GCAP_OK;
}
