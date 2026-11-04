#include "GuestTransfers.h"
#include "VulkanTransferBatch.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
enum {QUEUE_BYTES=8192,COMMAND_BYTES=160,BUILDER_BYTES=64,RECORD_BYTES=48,END_BYTES=16,CONTROL_BYTES=64};
typedef struct {uint64_t tokens[SATCHEL_GUEST_POOL_LIMIT],generations[SATCHEL_GUEST_POOL_LIMIT];uint32_t count;} References;
typedef struct {SatchelVulkanTransferCommand gpu;uint64_t source_lease,destination_lease;} Recorded;
typedef struct {
    uint64_t object,id,device,device_generation,backend,command_lease,command_pool_generation,command_base,command_size,control_base,control_size,sealed;
    SatchelGuestRasterRecord *raster;uint32_t recording,count,compute_bound;SatchelGuestComputeBinding compute;SatchelScalarRequirements requirements;References refs;Recorded records[SATCHEL_GUEST_TRANSFER_COMMAND_LIMIT];
} Command;
typedef struct {
    uint64_t id,device,device_generation,command_owner,command_generation,command_lease,command_pool_generation,command_base,control_base;
    SatchelGuestRasterRecord *raster;uint32_t count,in_flight,compute_bound;SatchelGuestComputeBinding compute;References refs;Recorded records[SATCHEL_GUEST_TRANSFER_COMMAND_LIMIT];
} Recording;
typedef struct {
    uint64_t object,id,device,device_generation,backend,memory,memory_size,job,submitted,finished;
    uint64_t present_lease,present_sequence;uint32_t present_cancelled,present_drained,present_dirty,present_error;
    SatchelGuestRasterQueueState raster_state;SatchelGuestRasterRun *raster_run;uint32_t raster_mode,raster_attempted,raster_draw_submitted,raster_completed,raster_error;
    uint32_t command_size,compute_size,control_size,pending_count,compute_job,submit_error;uint64_t handles[SATCHEL_GUEST_TRANSFER_SUBMIT_HANDLE_LIMIT];
} Queue;
struct SatchelGuestTransfers {SatchelGuestTextures *textures;SatchelGuestRasterPrograms *raster_programs;SatchelGuestRasterQueueQuery raster_query;void *raster_context;SatchelGuestCompute *compute;uint64_t compute_command;SatchelGuestPools *pools;SatchelGuestPoolDeviceValidator validate;SatchelGuestPoolRangeGuard guard;SatchelGuestPoolCheckedRangeGuard checked_guard;void *context;atomic_flag active;uint32_t retiring;
    Queue queues[SATCHEL_GUEST_TRANSFER_QUEUE_LIMIT];Command commands[SATCHEL_GUEST_TRANSFER_COMMAND_BUFFER_LIMIT];Recording recordings[SATCHEL_GUEST_TRANSFER_HANDLE_LIMIT];};
static _Atomic uint64_t next_object=1,next_handle=UINT64_C(0x2000000000000000),next_present_lease=1;
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put64(uint8_t *p,uint64_t v){put32(p,(uint32_t)v);put32(p+4,(uint32_t)(v>>32));}
static uint64_t get64(const uint8_t *p){return get32(p)|(uint64_t)get32(p+4)<<32;}
static int identity(_Atomic uint64_t *counter,uint64_t *id){uint64_t old=atomic_load(counter);do{if(old==UINT64_MAX)return 0;}while(!atomic_compare_exchange_weak(counter,&old,old+1));*id=old;return 1;}
static int overlap(uint64_t a,uint64_t n,uint64_t b,uint64_t m){return n && m && a<b+m && b<a+n;}
static int physical_overlap(SatchelCPUProbe *cpu,uint64_t a,uint64_t n,uint64_t b,uint64_t m){
    if(!n || !m)return 0;if(!cpu)return overlap(a,n,b,m);int result=1;
    return !satchel_cpu_probe_physical_overlap(cpu,a,n,b,m,&result) || result;
}
static int guarded(SatchelGuestTransfers *s,SatchelCPUProbe *cpu,uint64_t address,uint64_t size){
    if(s->checked_guard)return s->checked_guard(s->context,cpu,address,size);
    return !s->guard || s->guard(s->context,address,size);
}
static uint32_t compute_status(uint32_t s){switch(s){case SATCHEL_GC_OK:return SATCHEL_GUEST_POOL_OK;case SATCHEL_GC_MEMORY:return SATCHEL_GUEST_POOL_MEMORY;case SATCHEL_GC_UNSUPPORTED:return SATCHEL_GUEST_POOL_UNSUPPORTED;case SATCHEL_GC_BUSY:return SATCHEL_GUEST_POOL_BUSY;case SATCHEL_GC_CANCELLED:return SATCHEL_GUEST_TRANSFER_CANCELLED;case SATCHEL_GC_ALLOCATION:return SATCHEL_GUEST_POOL_ALLOCATION;case SATCHEL_GC_BACKEND:return SATCHEL_GUEST_POOL_BACKEND;case SATCHEL_GC_INTERNAL:return SATCHEL_GUEST_POOL_INTERNAL;default:return SATCHEL_GUEST_POOL_BAD_OBJECT;}}
static uint32_t raster_record_status(uint32_t v){switch(v){case SATCHEL_GRR_OK:return SATCHEL_GUEST_POOL_OK;case SATCHEL_GRR_BUSY:return SATCHEL_GUEST_POOL_BUSY;case SATCHEL_GRR_MEMORY:return SATCHEL_GUEST_POOL_MEMORY;case SATCHEL_GRR_UNSUPPORTED:return SATCHEL_GUEST_POOL_UNSUPPORTED;case SATCHEL_GRR_LIMIT:return SATCHEL_GUEST_POOL_LIMIT_REACHED;case SATCHEL_GRR_ALLOCATION:return SATCHEL_GUEST_POOL_ALLOCATION;case SATCHEL_GRR_BACKEND:return SATCHEL_GUEST_POOL_BACKEND;case SATCHEL_GRR_INTERNAL:return SATCHEL_GUEST_POOL_INTERNAL;default:return SATCHEL_GUEST_POOL_BAD_OBJECT;}}
static uint32_t raster_run_status(uint32_t v){switch(v){case SATCHEL_GRUN_OK:return SATCHEL_GUEST_POOL_OK;case SATCHEL_GRUN_BUSY:return SATCHEL_GUEST_POOL_BUSY;case SATCHEL_GRUN_MEMORY:return SATCHEL_GUEST_POOL_MEMORY;case SATCHEL_GRUN_UNSUPPORTED:return SATCHEL_GUEST_POOL_UNSUPPORTED;case SATCHEL_GRUN_ALLOCATION:return SATCHEL_GUEST_POOL_ALLOCATION;case SATCHEL_GRUN_CANCELLED:return SATCHEL_GUEST_TRANSFER_CANCELLED;case SATCHEL_GRUN_BACKEND:return SATCHEL_GUEST_POOL_BACKEND;case SATCHEL_GRUN_INTERNAL:return SATCHEL_GUEST_POOL_INTERNAL;default:return SATCHEL_GUEST_POOL_BAD_OBJECT;}}
static uint32_t gpu_status(SatchelVulkanStatus s){return s==SATCHEL_VULKAN_OK?SATCHEL_GUEST_POOL_OK:(s==SATCHEL_VULKAN_CANCELLED?SATCHEL_GUEST_TRANSFER_CANCELLED:((s==SATCHEL_VULKAN_BUSY || s==SATCHEL_VULKAN_TIMEOUT)?SATCHEL_GUEST_POOL_BUSY:(s==SATCHEL_VULKAN_UNSUPPORTED?SATCHEL_GUEST_POOL_UNSUPPORTED:SATCHEL_GUEST_POOL_BACKEND)));}
static uint32_t writev(SatchelCPUProbe *cpu,const SatchelCPUProbeWrite *writes,size_t count){SatchelCPUWriteResult s=satchel_cpu_probe_writev(cpu,writes,count);return s==SATCHEL_CPU_WRITE_OK?SATCHEL_GUEST_POOL_OK:(s==SATCHEL_CPU_WRITE_BUSY?SATCHEL_GUEST_POOL_BUSY:(s==SATCHEL_CPU_WRITE_ALLOCATION?SATCHEL_GUEST_POOL_ALLOCATION:SATCHEL_GUEST_POOL_MEMORY));}
static uint32_t write(SatchelCPUProbe *cpu,uint64_t address,const void *bytes,size_t size){SatchelCPUProbeWrite w={address,bytes,size};return writev(cpu,&w,1);}
static uint32_t raw_range(SatchelCPUProbe *cpu,uint64_t address,uint64_t size){return satchel_cpu_probe_check_range(cpu,address,size,SATCHEL_CPU_READ|SATCHEL_CPU_WRITE)?SATCHEL_GUEST_POOL_OK:SATCHEL_GUEST_POOL_MEMORY;}
static uint32_t unowned(SatchelGuestTransfers *s,SatchelCPUProbe *cpu,uint64_t address,uint64_t size){
    if(!size || address>UINT64_MAX-size)return SATCHEL_GUEST_POOL_INVALID;
    for(unsigned i=0;i<SATCHEL_GUEST_TRANSFER_QUEUE_LIMIT;i++){Queue *q=&s->queues[i];if(q->id && (physical_overlap(cpu,address,size,q->object,QUEUE_BYTES) || physical_overlap(cpu,address,size,q->memory,q->memory_size)))return SATCHEL_GUEST_POOL_BAD_OBJECT;}
    for(unsigned i=0;i<SATCHEL_GUEST_TRANSFER_COMMAND_BUFFER_LIMIT;i++){Command *c=&s->commands[i];if(c->id && (physical_overlap(cpu,address,size,c->object,COMMAND_BYTES) || physical_overlap(cpu,address,size,c->command_base,c->command_size) || physical_overlap(cpu,address,size,c->control_base,c->control_size)))return SATCHEL_GUEST_POOL_BAD_OBJECT;}
    for(unsigned i=0;i<SATCHEL_GUEST_TRANSFER_HANDLE_LIMIT;i++){Recording *r=&s->recordings[i];if(r->id && (physical_overlap(cpu,address,size,r->command_base,(uint64_t)r->count*RECORD_BYTES+END_BYTES) || physical_overlap(cpu,address,size,r->control_base,CONTROL_BYTES)))return SATCHEL_GUEST_POOL_BAD_OBJECT;}
    return SATCHEL_GUEST_POOL_OK;
}
static uint32_t protected_range(SatchelGuestTransfers *s,SatchelCPUProbe *cpu,uint64_t address,uint64_t size,int allow_pool){
    uint32_t status=raw_range(cpu,address,size);if(status)return status;
    if(!guarded(s,cpu,address,size))return SATCHEL_GUEST_POOL_BAD_OBJECT;
    if(!allow_pool){status=satchel_guest_pools_check_unowned_range_checked(s->pools,cpu,address,size);if(status)return status;}
    return unowned(s,cpu,address,size);
}
static uint32_t device(SatchelGuestTransfers *s,SatchelCPUProbe *cpu,uint64_t address,uint64_t *backend,uint64_t *generation){
    *backend=0;*generation=0;return s->validate(s->context,cpu,address,backend,generation) && *backend && *generation?SATCHEL_GUEST_POOL_OK:SATCHEL_GUEST_POOL_BAD_OBJECT;
}
static void tag(uint8_t *bytes,size_t size,uint32_t magic,uint64_t id){memset(bytes,0,size);put32(bytes,magic);put32(bytes+4,1);put64(bytes+8,id);}
static uint32_t check_tag(SatchelCPUProbe *cpu,uint64_t address,size_t size,uint32_t magic,uint64_t id){uint8_t bytes[16];if(raw_range(cpu,address,size) || !satchel_cpu_probe_read(cpu,address,bytes,16))return SATCHEL_GUEST_POOL_MEMORY;
    return get32(bytes)==magic && get32(bytes+4)==1 && get64(bytes+8)==id?SATCHEL_GUEST_POOL_OK:SATCHEL_GUEST_POOL_BAD_OBJECT;}
static Command *command(SatchelGuestTransfers *s,uint64_t object){for(unsigned i=0;i<SATCHEL_GUEST_TRANSFER_COMMAND_BUFFER_LIMIT;i++)if(s->commands[i].id && s->commands[i].object==object)return &s->commands[i];return NULL;}
static Queue *queue(SatchelGuestTransfers *s,uint64_t object){for(unsigned i=0;i<SATCHEL_GUEST_TRANSFER_QUEUE_LIMIT;i++)if(s->queues[i].id && s->queues[i].object==object)return &s->queues[i];return NULL;}
static Recording *recording(SatchelGuestTransfers *s,uint64_t id){for(unsigned i=0;i<SATCHEL_GUEST_TRANSFER_HANDLE_LIMIT;i++)if(id && s->recordings[i].id==id)return &s->recordings[i];return NULL;}
static uint32_t check_command(SatchelGuestTransfers *s,SatchelCPUProbe *cpu,Command *c){
    if(!c)return SATCHEL_GUEST_POOL_BAD_OBJECT;uint32_t status=check_tag(cpu,c->object,COMMAND_BYTES,0x504c4342,c->id);if(status)return status;
    uint64_t backend=0,generation=0;status=device(s,cpu,c->device,&backend,&generation);return status || backend!=c->backend || generation!=c->device_generation?SATCHEL_GUEST_POOL_BAD_OBJECT:SATCHEL_GUEST_POOL_OK;
}
static uint32_t check_queue(SatchelGuestTransfers *s,SatchelCPUProbe *cpu,Queue *q){
    if(!q)return SATCHEL_GUEST_POOL_BAD_OBJECT;uint32_t status=check_tag(cpu,q->object,QUEUE_BYTES,0x504c5155,q->id);if(status)return status;
    status=check_tag(cpu,q->memory,(size_t)q->memory_size,0x504c514d,q->id);if(status)return status;
    uint64_t backend=0,generation=0;status=device(s,cpu,q->device,&backend,&generation);return status || backend!=q->backend || generation!=q->device_generation?SATCHEL_GUEST_POOL_BAD_OBJECT:SATCHEL_GUEST_POOL_OK;
}
static uint32_t release_refs(SatchelGuestTransfers *s,References *refs){while(refs->count){uint32_t index=refs->count-1;uint32_t status=satchel_guest_pools_release(s->pools,refs->tokens[index]);if(status)return status;refs->tokens[index]=0;refs->generations[index]=0;--refs->count;}return SATCHEL_GUEST_POOL_OK;}
static uint32_t retain_range(SatchelGuestTransfers *s,SatchelCPUProbe *cpu,Command *c,uint64_t gpu,uint64_t size,uint64_t *lease){
    uint64_t token=0;SatchelGuestPoolView view={0};uint32_t status=satchel_guest_pools_retain_range(s->pools,cpu,c->device,gpu,size,&token,&view);if(status)return status;
    /* First transfer profile uses dedicated command-storage pools. This avoids
     * data writes or later grants aliasing live recording representations. */
    for(unsigned i=0;i<SATCHEL_GUEST_TRANSFER_COMMAND_BUFFER_LIMIT;i++)if(s->commands[i].id && s->commands[i].command_pool_generation==view.generation)status=SATCHEL_GUEST_POOL_BAD_OBJECT;
    for(unsigned i=0;i<SATCHEL_GUEST_TRANSFER_HANDLE_LIMIT;i++)if(s->recordings[i].id && s->recordings[i].command_pool_generation==view.generation)status=SATCHEL_GUEST_POOL_BAD_OBJECT;
    if(status){uint32_t released=satchel_guest_pools_release(s->pools,token);return released?released:status;}
    status=unowned(s,cpu,view.cpu_address+(gpu-view.gpu_address),size);if(status){uint32_t released=satchel_guest_pools_release(s->pools,token);return released?released:status;}
    for(unsigned i=0;i<c->refs.count;i++)if(c->refs.generations[i]==view.generation){status=satchel_guest_pools_release(s->pools,token);if(status)return status;*lease=c->refs.tokens[i];return SATCHEL_GUEST_POOL_OK;}
    if(c->refs.count==SATCHEL_GUEST_POOL_LIMIT){status=satchel_guest_pools_release(s->pools,token);return status?status:SATCHEL_GUEST_POOL_LIMIT_REACHED;}
    const uint32_t index=c->refs.count++;c->refs.tokens[index]=token;c->refs.generations[index]=view.generation;*lease=token;return SATCHEL_GUEST_POOL_OK;
}
static void encode_record(uint8_t bytes[RECORD_BYTES],const SatchelVulkanTransferCommand *c){put32(bytes,c->kind);put32(bytes+4,c->value);put64(bytes+8,c->source);put64(bytes+16,c->source_offset);put64(bytes+24,c->destination);put64(bytes+32,c->destination_offset);put64(bytes+40,c->size);}
static void command_tag(uint8_t bytes[COMMAND_BYTES],const Command *c,uint32_t recording_state,uint32_t count){tag(bytes,COMMAND_BYTES,0x504c4342,c->id);put32(bytes+16,recording_state);put32(bytes+20,count);}
static void control_tag(uint8_t bytes[CONTROL_BYTES],const Recording *r){tag(bytes,CONTROL_BYTES,0x504c5243,r->id);put64(bytes+16,r->device);put64(bytes+24,r->command_generation);put32(bytes+32,r->count);put32(bytes+36,r->count*RECORD_BYTES+END_BYTES);}
static uint32_t builder(SatchelGuestTransfers *s,SatchelCPUProbe *cpu,const SatchelGuestTransferRequest *r,SatchelGuestTransferResult *out){
    const uint64_t address=r->x[0];uint8_t bytes[BUILDER_BYTES];uint32_t status=protected_range(s,cpu,address,BUILDER_BYTES,0);if(status)return status;
    if(!satchel_cpu_probe_read(cpu,address,bytes,sizeof(bytes)))return SATCHEL_GUEST_POOL_MEMORY;
    int valid=get32(bytes)==0x504c5142 && get32(bytes+4)==1;
    if(r->operation==SATCHEL_GT_QUEUE_DEFAULTS){uint64_t parent=valid?get64(bytes+8):0,generation=valid?get64(bytes+16):0;memset(bytes,0,sizeof(bytes));put32(bytes,0x504c5142);put32(bytes+4,1);put64(bytes+8,parent);put64(bytes+16,generation);put32(bytes+40,65536);put32(bytes+44,262144);put32(bytes+48,16384);put32(bytes+56,4096);}
    else if(r->operation==SATCHEL_GT_QUEUE_DEVICE){uint64_t backend=0,generation=0;status=device(s,cpu,r->x[1],&backend,&generation);if(status)return status;if(!valid){memset(bytes,0,sizeof(bytes));put32(bytes,0x504c5142);put32(bytes+4,1);}put64(bytes+8,r->x[1]);put64(bytes+16,generation);}
    else {
        if(!valid)return SATCHEL_GUEST_POOL_BAD_OBJECT;
        if(r->operation==SATCHEL_GT_QUEUE_FLAGS){if((uint32_t)r->x[1])return SATCHEL_GUEST_POOL_UNSUPPORTED;put32(bytes+52,0);}
        else if(r->operation>=SATCHEL_GT_QUEUE_COMMAND_SIZE && r->operation<=SATCHEL_GT_QUEUE_CONTROL_SIZE){if(r->x[1]>SATCHEL_GUEST_TRANSFER_QUEUE_MEMORY_LIMIT || (r->x[1]&4095))return SATCHEL_GUEST_POOL_UNSUPPORTED;put32(bytes+40+(r->operation-SATCHEL_GT_QUEUE_COMMAND_SIZE)*4,(uint32_t)r->x[1]);}
        else if(r->operation==SATCHEL_GT_QUEUE_MEMORY){if(!r->x[2] || r->x[2]>SATCHEL_GUEST_TRANSFER_QUEUE_MEMORY_LIMIT || ((r->x[1]|r->x[2])&4095))return SATCHEL_GUEST_POOL_UNSUPPORTED;
            status=protected_range(s,cpu,r->x[1],r->x[2],0);if(status)return status;if(physical_overlap(cpu,address,64,r->x[1],r->x[2]))return SATCHEL_GUEST_POOL_BAD_OBJECT;put64(bytes+24,r->x[1]);put64(bytes+32,r->x[2]);}
        else if(r->operation==SATCHEL_GT_QUEUE_MEMORY_SIZE){uint64_t size=(uint64_t)get32(bytes+40)+get32(bytes+44)+get32(bytes+48);if(!get32(bytes+40) || !get32(bytes+48) || size>SATCHEL_GUEST_TRANSFER_QUEUE_MEMORY_LIMIT)return SATCHEL_GUEST_POOL_UNSUPPORTED;out->value=(size+4095)&~UINT64_C(4095);out->set_value=1;return SATCHEL_GUEST_POOL_OK;}
        else return SATCHEL_GUEST_POOL_UNSUPPORTED;
    }
    return write(cpu,address,bytes,sizeof(bytes));
}
static uint32_t queue_initialize(SatchelGuestTransfers *s,SatchelCPUProbe *cpu,uint64_t object,uint64_t builder_address){
    uint8_t b[64];uint32_t status=protected_range(s,cpu,object,QUEUE_BYTES,0);if(status)return status;
    status=protected_range(s,cpu,builder_address,64,0);if(status)return status;if(!satchel_cpu_probe_read(cpu,builder_address,b,sizeof(b)))return SATCHEL_GUEST_POOL_MEMORY;
    if(get32(b)!=0x504c5142 || get32(b+4)!=1)return SATCHEL_GUEST_POOL_BAD_OBJECT;
    const uint64_t parent=get64(b+8),memory=get64(b+24),memory_size=get64(b+32);uint64_t backend=0,generation=0;
    status=device(s,cpu,parent,&backend,&generation);if(status || generation!=get64(b+16))return SATCHEL_GUEST_POOL_BAD_OBJECT;
    const uint32_t command_size=get32(b+40),compute_size=get32(b+44),control_size=get32(b+48);
    const uint64_t required=(uint64_t)command_size+compute_size+control_size;
    if(get32(b+52) || command_size<4096 || control_size<4096 || ((command_size|compute_size|control_size)&4095) ||
        required>SATCHEL_GUEST_TRANSFER_QUEUE_MEMORY_LIMIT || memory_size<required || memory_size>SATCHEL_GUEST_TRANSFER_QUEUE_MEMORY_LIMIT || ((memory|memory_size)&4095))return SATCHEL_GUEST_POOL_UNSUPPORTED;
    status=protected_range(s,cpu,memory,memory_size,0);if(status)return status;
    if(physical_overlap(cpu,object,QUEUE_BYTES,memory,memory_size) || physical_overlap(cpu,object,QUEUE_BYTES,builder_address,64) || physical_overlap(cpu,builder_address,64,memory,memory_size))return SATCHEL_GUEST_POOL_BAD_OBJECT;
    Queue *slot=NULL;for(unsigned i=0;i<SATCHEL_GUEST_TRANSFER_QUEUE_LIMIT;i++){if(s->queues[i].id && s->queues[i].device==parent)return SATCHEL_GUEST_POOL_LIMIT_REACHED;if(!s->queues[i].id && !slot)slot=&s->queues[i];}
    if(!slot)return SATCHEL_GUEST_POOL_LIMIT_REACHED;uint64_t id=0;if(!identity(&next_object,&id))return SATCHEL_GUEST_POOL_LIMIT_REACHED;
    SatchelGuestRasterQueueState raster_state={0};if(s->raster_query){status=s->raster_query(s->raster_context,cpu,parent,&raster_state);if(status)return status;if(raster_state.device!=parent || raster_state.backend!=backend || raster_state.device_generation!=generation)return SATCHEL_GUEST_POOL_BAD_OBJECT;}
    uint8_t object_bytes[QUEUE_BYTES],*backing=calloc(1,(size_t)memory_size);if(!backing)return SATCHEL_GUEST_POOL_ALLOCATION;
    tag(object_bytes,sizeof(object_bytes),0x504c5155,id);tag(backing,(size_t)memory_size,0x504c514d,id);
    SatchelCPUProbeWrite writes[]={{object,object_bytes,sizeof(object_bytes)},{memory,backing,(size_t)memory_size}};status=writev(cpu,writes,2);free(backing);if(status)return status;
    *slot=(Queue){.object=object,.id=id,.device=parent,.device_generation=generation,.backend=backend,.memory=memory,.memory_size=memory_size,
        .command_size=command_size,.compute_size=compute_size,.control_size=control_size,.raster_state=raster_state};return SATCHEL_GUEST_POOL_OK;
}
static uint32_t command_initialize(SatchelGuestTransfers *s,SatchelCPUProbe *cpu,uint64_t object,uint64_t parent){
    uint32_t status=protected_range(s,cpu,object,COMMAND_BYTES,0);if(status)return status;uint64_t backend=0,generation=0;status=device(s,cpu,parent,&backend,&generation);if(status)return status;
    Command *slot=NULL;for(unsigned i=0;i<SATCHEL_GUEST_TRANSFER_COMMAND_BUFFER_LIMIT;i++)if(!s->commands[i].id){slot=&s->commands[i];break;}
    if(!slot)return SATCHEL_GUEST_POOL_LIMIT_REACHED;uint64_t id=0;if(!identity(&next_object,&id))return SATCHEL_GUEST_POOL_LIMIT_REACHED;
    Command next={.object=object,.id=id,.device=parent,.device_generation=generation,.backend=backend};uint8_t bytes[COMMAND_BYTES];command_tag(bytes,&next,0,0);
    status=write(cpu,object,bytes,sizeof(bytes));if(status)return status;*slot=next;return SATCHEL_GUEST_POOL_OK;
}
static uint32_t add_memory(SatchelGuestTransfers *s,SatchelCPUProbe *cpu,Command *c,const SatchelGuestTransferRequest *r){
    if(c->recording || c->sealed)return SATCHEL_GUEST_POOL_BUSY;
    if(r->operation==SATCHEL_GT_COMMAND_MEMORY){
        if(c->command_lease)return SATCHEL_GUEST_POOL_UNSUPPORTED;
        if(r->x[2]>INT64_MAX || !r->x[3] || (r->x[2]&7) || (r->x[3]&7) || r->x[3]<RECORD_BYTES+END_BYTES)return SATCHEL_GUEST_POOL_UNSUPPORTED;
        uint64_t lease=0;SatchelGuestPoolView view={0};uint32_t status=satchel_guest_pools_retain_object(s->pools,cpu,c->device,r->x[1],&lease,&view);if(status)return status;
        if(r->x[2]>view.size || r->x[3]>view.size-r->x[2])status=SATCHEL_GUEST_POOL_INVALID;
        else status=protected_range(s,cpu,view.cpu_address+r->x[2],r->x[3],1);
        for(unsigned i=0;i<SATCHEL_GUEST_TRANSFER_COMMAND_BUFFER_LIMIT;i++)for(unsigned j=0;j<s->commands[i].refs.count;j++)if(s->commands[i].refs.generations[j]==view.generation)status=SATCHEL_GUEST_POOL_BAD_OBJECT;
        for(unsigned i=0;i<SATCHEL_GUEST_TRANSFER_HANDLE_LIMIT;i++)for(unsigned j=0;j<s->recordings[i].refs.count;j++)if(s->recordings[i].refs.generations[j]==view.generation)status=SATCHEL_GUEST_POOL_BAD_OBJECT;
        if(status){uint32_t released=satchel_guest_pools_release(s->pools,lease);return released?released:status;}
        c->command_lease=lease;c->command_pool_generation=view.generation;c->command_base=view.cpu_address+r->x[2];c->command_size=r->x[3];return SATCHEL_GUEST_POOL_OK;
    }
    if(c->control_size)return SATCHEL_GUEST_POOL_UNSUPPORTED;
    if(r->x[2]<CONTROL_BYTES || r->x[2]>65536 || (r->x[1]&7) || (r->x[2]&7))return SATCHEL_GUEST_POOL_UNSUPPORTED;
    uint32_t status=protected_range(s,cpu,r->x[1],r->x[2],0);if(status)return status;
    c->control_base=r->x[1];c->control_size=r->x[2];return SATCHEL_GUEST_POOL_OK;
}
static uint32_t command_memory(SatchelGuestTransfers *s,SatchelCPUProbe *cpu,Command *c){
    if(!c->command_lease || !c->control_size)return SATCHEL_GUEST_POOL_BAD_OBJECT;
    SatchelGuestPoolView view={0};uint32_t status=satchel_guest_pools_lease_view(s->pools,cpu,c->command_lease,&view);if(status)return status;
    if(c->command_base<view.cpu_address || c->command_base-view.cpu_address>view.size || c->command_size>view.size-(c->command_base-view.cpu_address))return SATCHEL_GUEST_POOL_BAD_OBJECT;
    return raw_range(cpu,c->control_base,c->control_size);
}
static uint32_t rollback_refs(SatchelGuestTransfers *s,Command *c,uint32_t count,uint32_t status){while(c->refs.count>count){unsigned index=c->refs.count-1;uint32_t released=satchel_guest_pools_release(s->pools,c->refs.tokens[index]);if(released)return SATCHEL_GUEST_POOL_INTERNAL;--c->refs.count;c->refs.tokens[index]=0;c->refs.generations[index]=0;}return status;}
static uint32_t record_transfer(SatchelGuestTransfers *s,SatchelCPUProbe *cpu,Command *c,const SatchelGuestTransferRequest *r){
    if(!c->recording)return SATCHEL_GUEST_POOL_BAD_OBJECT;if(c->compute.program || c->raster)return SATCHEL_GUEST_POOL_UNSUPPORTED;uint32_t status=command_memory(s,cpu,c);if(status)return status;
    if(c->count==SATCHEL_GUEST_TRANSFER_COMMAND_LIMIT || ((uint64_t)c->count+1)*RECORD_BYTES+END_BYTES>c->command_size)return SATCHEL_GUEST_POOL_LIMIT_REACHED;
    Recorded next={0};const uint32_t old_refs=c->refs.count;
    if(r->operation==SATCHEL_GT_CLEAR){
        if(!r->x[2] || ((r->x[1]|r->x[2])&3))return SATCHEL_GUEST_POOL_UNSUPPORTED;
        next.gpu=(SatchelVulkanTransferCommand){SATCHEL_VULKAN_TRANSFER_FILL,(uint32_t)r->x[3],0,0,r->x[1],0,r->x[2]};
    }else {
        if((uint32_t)r->x[4])return SATCHEL_GUEST_POOL_UNSUPPORTED;if(!r->x[3])return SATCHEL_GUEST_POOL_INVALID;
        next.gpu=(SatchelVulkanTransferCommand){SATCHEL_VULKAN_TRANSFER_COPY,0,r->x[1],0,r->x[2],0,r->x[3]};
        status=retain_range(s,cpu,c,next.gpu.source,next.gpu.size,&next.source_lease);if(status)return status;
    }
    status=retain_range(s,cpu,c,next.gpu.destination,next.gpu.size,&next.destination_lease);if(status)return rollback_refs(s,c,old_refs,status);
    if(next.gpu.source && overlap(next.gpu.source,next.gpu.size,next.gpu.destination,next.gpu.size))return rollback_refs(s,c,old_refs,SATCHEL_GUEST_POOL_UNSUPPORTED);
    uint8_t encoded[RECORD_BYTES],object[COMMAND_BYTES];encode_record(encoded,&next.gpu);command_tag(object,c,1,c->count+1);
    SatchelCPUProbeWrite writes[]={{c->command_base+(uint64_t)c->count*RECORD_BYTES,encoded,sizeof(encoded)},{c->object,object,sizeof(object)}};
    status=writev(cpu,writes,2);if(status)return rollback_refs(s,c,old_refs,status);
    c->records[c->count++]=next;return SATCHEL_GUEST_POOL_OK;
}
/* Initial compute state is intentionally bounded: one program bind and one
 * dispatch per recording, one live compute recording per owner. All bindings
 * are recorded into caller memory; later mutation is rejected, never replayed. */
static uint32_t record_compute(SatchelGuestTransfers *s,SatchelCPUProbe *cpu,Command *c,const SatchelGuestTransferRequest *r){
 if(!s->compute || c->raster)return SATCHEL_GUEST_POOL_UNSUPPORTED;
 if(!c->recording || c->compute_bound)return SATCHEL_GUEST_POOL_BUSY;
 uint32_t status=command_memory(s,cpu,c);if(status)return status;
 if(c->count==SATCHEL_GUEST_TRANSFER_COMMAND_LIMIT || ((uint64_t)c->count+1)*RECORD_BYTES+END_BYTES>c->command_size)return SATCHEL_GUEST_POOL_LIMIT_REACHED;
 if(c->count && !c->compute.program)return SATCHEL_GUEST_POOL_UNSUPPORTED;
 if(s->compute_command && s->compute_command!=c->id)return SATCHEL_GUEST_POOL_BUSY;
 const uint32_t old_refs=c->refs.count;SatchelGuestComputeBinding state=c->compute;SatchelScalarRequirements requirements=c->requirements;int retained_program=0;
 Recorded next={.gpu={.kind=0x100+r->operation,.source=r->x[1],.source_offset=r->x[2],.destination=r->x[3],.size=r->x[4]}};
 if(r->operation==SATCHEL_GT_BIND_PROGRAM){
  if(c->compute.program || (uint32_t)r->x[2]!=0x20)return SATCHEL_GUEST_POOL_UNSUPPORTED;
  status=compute_status(satchel_guest_compute_program_retain(s->compute,cpu,r->x[1],c->device,&requirements));if(status)return status;retained_program=1;state.program=r->x[1];
 }else if(!state.program)return SATCHEL_GUEST_POOL_BAD_OBJECT;
 else if(r->operation==SATCHEL_GT_BIND_UNIFORM || r->operation==SATCHEL_GT_BIND_STORAGE){
  if((uint32_t)r->x[1]!=5 || !r->x[4] || (r->x[4]&3))return SATCHEL_GUEST_POOL_UNSUPPORTED;
  if(r->operation==SATCHEL_GT_BIND_UNIFORM){if((uint32_t)r->x[2]>14 || (uint32_t)r->x[2]+3!=requirements.cb_slot || r->x[4]>65536 || r->x[4]<requirements.uniform_required_bytes || state.cb_gpu)return SATCHEL_GUEST_POOL_UNSUPPORTED;state.cb_gpu=r->x[3];state.cb_size=r->x[4];state.uniform_slot=(uint32_t)r->x[2];}
  else {if((uint32_t)r->x[2] || r->x[4]>1048576 || r->x[4]<requirements.storage_required_bytes || state.storage_gpu)return SATCHEL_GUEST_POOL_UNSUPPORTED;state.storage_gpu=r->x[3];state.storage_size=r->x[4];}
  status=retain_range(s,cpu,c,r->x[3],r->x[4],&next.destination_lease);if(status)return status;
 }else if(r->operation==SATCHEL_GT_DISPATCH){
  if(!state.cb_gpu || !state.storage_gpu || (uint32_t)r->x[1]!=1 || (uint32_t)r->x[2]!=1 || (uint32_t)r->x[3]!=1)return SATCHEL_GUEST_POOL_UNSUPPORTED;
  state.groups_x=1;state.groups_y=1;state.groups_z=1;
  status=compute_status(satchel_guest_compute_bind(s->compute,cpu,&state));if(status)return status;
  /* Own even if the following atomic guest write fails. Cleanup remains CPU-free. */
  c->compute_bound=1;s->compute_command=c->id;
 }else return SATCHEL_GUEST_POOL_UNSUPPORTED;
 uint8_t encoded[RECORD_BYTES],object[COMMAND_BYTES];encode_record(encoded,&next.gpu);command_tag(object,c,1,c->count+1);
 SatchelCPUProbeWrite writes[]={{c->command_base+(uint64_t)c->count*RECORD_BYTES,encoded,sizeof(encoded)},{c->object,object,sizeof(object)}};
 status=writev(cpu,writes,2);if(status){
  if(retained_program){uint32_t released=compute_status(satchel_guest_compute_program_release(s->compute,state.program));if(released){c->compute.program=state.program;return released;}}
  return rollback_refs(s,c,old_refs,status);
 }
 c->compute=state;c->requirements=requirements;c->records[c->count++]=next;return SATCHEL_GUEST_POOL_OK;
}
/* Shared command owner keeps raster state off guest RAM. Only committed draw/
 * clear snapshots occupy the caller-owned bounded command representation. */
static int raster_guard(void *context,SatchelCPUProbe *cpu,uint64_t a,uint64_t n){SatchelGuestTransfers *s=context;return guarded(s,cpu,a,n) && !unowned(s,cpu,a,n);}
typedef struct {SatchelGuestTransfers *owner;Command *command;uint32_t status;} RasterCommit;
static uint32_t commit_raster(void *context,SatchelCPUProbe *cpu,uint32_t count,const SatchelGuestRasterCommand *snapshot){
 RasterCommit *x=context;Command *c=x->command;if(count!=c->count || c->count>=SATCHEL_GUEST_TRANSFER_COMMAND_LIMIT){x->status=SATCHEL_GUEST_POOL_INTERNAL;return SATCHEL_GRR_INTERNAL;}
 Recorded next={.gpu={.kind=0x200+snapshot->operation,.value=count,.source=snapshot->program,.source_offset=snapshot->program_generation,.destination=snapshot->capture_generation,.destination_offset=snapshot->vertex_gpu,.size=snapshot->vertex_size}};
 if(snapshot->operation==SATCHEL_VULKAN_RASTER_OP_CLEAR){uint32_t bits[4];memcpy(bits,snapshot->draw.clear_rgba,sizeof(bits));next.gpu.source=(uint64_t)bits[0]|(uint64_t)bits[1]<<32;next.gpu.source_offset=(uint64_t)bits[2]|(uint64_t)bits[3]<<32;}
 uint8_t encoded[RECORD_BYTES],object[COMMAND_BYTES];encode_record(encoded,&next.gpu);command_tag(object,c,1,c->count+1);
 SatchelCPUProbeWrite writes[]={{c->command_base+(uint64_t)c->count*RECORD_BYTES,encoded,sizeof(encoded)},{c->object,object,sizeof(object)}};
 x->status=writev(cpu,writes,2);if(x->status)return SATCHEL_GRR_MEMORY;c->records[c->count++]=next;return SATCHEL_GRR_OK;
}
static uint32_t record_raster(SatchelGuestTransfers *s,SatchelCPUProbe *cpu,Command *c,const SatchelGuestTransferRequest *request){
 if(!s->textures || !s->raster_programs || !s->raster_query)return SATCHEL_GUEST_POOL_UNSUPPORTED;
 if(!c->recording || c->sealed)return SATCHEL_GUEST_POOL_BAD_OBJECT;if(c->compute.program || c->compute_bound || (c->count && !c->raster))return SATCHEL_GUEST_POOL_UNSUPPORTED;
 uint32_t status=command_memory(s,cpu,c);if(status)return status;
 if(c->count>=SATCHEL_GUEST_TRANSFER_COMMAND_LIMIT || ((uint64_t)c->count+1)*RECORD_BYTES+END_BYTES>c->command_size)return SATCHEL_GUEST_POOL_LIMIT_REACHED;
 if(!c->raster){Queue *q=NULL;for(unsigned i=0;i<SATCHEL_GUEST_TRANSFER_QUEUE_LIMIT;i++)if(s->queues[i].id && s->queues[i].device==c->device && s->queues[i].device_generation==c->device_generation){q=&s->queues[i];break;}if(!q)return SATCHEL_GUEST_POOL_UNSUPPORTED;
  c->raster=satchel_guest_raster_record_create(s->pools,s->textures,s->raster_programs,&q->raster_state,raster_guard,s);if(!c->raster)return SATCHEL_GUEST_POOL_ALLOCATION;
 }
 SatchelGuestRasterStateRequest r={0};memcpy(r.x,request->x,sizeof(r.x));memcpy(r.f,request->f,sizeof(r.f));
 switch(request->operation){
 case SATCHEL_GT_BIND_PROGRAM:r.operation=SATCHEL_GRR_PROGRAM;break;case SATCHEL_GT_RENDER_TARGETS:r.operation=SATCHEL_GRR_TARGET;break;
 case SATCHEL_GT_CLEAR_COLOR:r.operation=SATCHEL_GRR_CLEAR;break;case SATCHEL_GT_DRAW_ARRAYS:r.operation=SATCHEL_GRR_DRAW;break;
 case SATCHEL_GT_VERTEX_BUFFER:r.operation=SATCHEL_GRR_VERTEX_BUFFER;break;case SATCHEL_GT_VERTEX_ATTRIBUTES:r.operation=SATCHEL_GRR_VERTEX_ATTRIBUTES;break;case SATCHEL_GT_VERTEX_STREAM:r.operation=SATCHEL_GRR_VERTEX_STREAM;break;
 case SATCHEL_GT_VIEWPORT:r.operation=SATCHEL_GRR_VIEWPORT;break;case SATCHEL_GT_SCISSOR:r.operation=SATCHEL_GRR_SCISSOR;break;case SATCHEL_GT_DEPTH_RANGE:r.operation=SATCHEL_GRR_DEPTH_RANGE;break;
 case SATCHEL_GT_COLOR:r.operation=SATCHEL_GRR_COLOR;break;case SATCHEL_GT_CHANNEL:r.operation=SATCHEL_GRR_CHANNEL;break;case SATCHEL_GT_POLYGON:r.operation=SATCHEL_GRR_POLYGON;break;case SATCHEL_GT_DEPTH_STENCIL:r.operation=SATCHEL_GRR_DEPTH_STENCIL;break;case SATCHEL_GT_MULTISAMPLE:r.operation=SATCHEL_GRR_MULTISAMPLE;break;
 case SATCHEL_GT_DEPTH_CLAMP:r.operation=SATCHEL_GRR_DEPTH_CLAMP;break;case SATCHEL_GT_RASTER_DISCARD:r.operation=SATCHEL_GRR_RASTER_DISCARD;break;case SATCHEL_GT_SAMPLE_MASK:r.operation=SATCHEL_GRR_SAMPLE_MASK;break;case SATCHEL_GT_RENDER_ENABLE:r.operation=SATCHEL_GRR_RENDER_ENABLE;break;case SATCHEL_GT_SWIZZLES:r.operation=SATCHEL_GRR_SWIZZLES;break;
 default:return SATCHEL_GUEST_POOL_UNSUPPORTED;}
 RasterCommit commit={s,c,0};status=raster_record_status(satchel_guest_raster_record_apply(c->raster,cpu,&r,commit_raster,&commit));return commit.status?commit.status:status;
}
static uint32_t release_compute(SatchelGuestTransfers *s,uint64_t command_id,SatchelGuestComputeBinding *state,uint32_t *bound){
 uint32_t status;if(*bound){status=compute_status(satchel_guest_compute_unbind(s->compute));if(status)return status;*bound=0;if(s->compute_command==command_id)s->compute_command=0;}
 if(state->program){status=compute_status(satchel_guest_compute_program_release(s->compute,state->program));if(status)return status;memset(state,0,sizeof(*state));}return SATCHEL_GUEST_POOL_OK;
}
static uint32_t end_recording(SatchelGuestTransfers *s,SatchelCPUProbe *cpu,Command *c,uint64_t *result){
    if(!c->recording || c->sealed)return SATCHEL_GUEST_POOL_BAD_OBJECT;if(!c->count || (c->compute.program && (!c->compute_bound || c->records[c->count-1].gpu.kind!=0x100+SATCHEL_GT_DISPATCH)))return SATCHEL_GUEST_POOL_UNSUPPORTED;
    if(c->raster && satchel_guest_raster_record_count(c->raster)!=c->count)return SATCHEL_GUEST_POOL_INTERNAL;
    uint32_t status=command_memory(s,cpu,c);if(status)return status;
    Recording *slot=NULL;for(unsigned i=0;i<SATCHEL_GUEST_TRANSFER_HANDLE_LIMIT;i++)if(!s->recordings[i].id){slot=&s->recordings[i];break;}
    if(!slot)return SATCHEL_GUEST_POOL_LIMIT_REACHED;uint64_t id=0;if(!identity(&next_handle,&id))return SATCHEL_GUEST_POOL_LIMIT_REACHED;
    SatchelGuestPoolView view={0};status=satchel_guest_pools_lease_view(s->pools,cpu,c->command_lease,&view);if(status)return status;
    uint64_t command_lease=0;status=satchel_guest_pools_retain_object(s->pools,cpu,c->device,view.object,&command_lease,&view);if(status)return status;
    Recording next={.id=id,.device=c->device,.device_generation=c->device_generation,.command_owner=c->object,.command_generation=c->id,
        .command_lease=command_lease,.command_pool_generation=c->command_pool_generation,.command_base=c->command_base,.control_base=c->control_base,.count=c->count,.compute_bound=c->compute_bound,.compute=c->compute,.refs=c->refs,.raster=c->raster};
    memcpy(next.records,c->records,sizeof(next.records));uint8_t end[END_BYTES],control[CONTROL_BYTES],object[COMMAND_BYTES];tag(end,sizeof(end),0x504c454e,id);control_tag(control,&next);command_tag(object,c,0,c->count);
    SatchelCPUProbeWrite writes[]={{c->command_base+(uint64_t)c->count*RECORD_BYTES,end,sizeof(end)},{c->control_base,control,sizeof(control)},{c->object,object,sizeof(object)}};
    status=writev(cpu,writes,3);if(status){uint32_t released=satchel_guest_pools_release(s->pools,command_lease);return released?SATCHEL_GUEST_POOL_INTERNAL:status;}
    *slot=next;c->raster=NULL;memset(&c->refs,0,sizeof(c->refs));memset(&c->compute,0,sizeof(c->compute));c->compute_bound=0;c->recording=0;c->sealed=id;*result=id;return SATCHEL_GUEST_POOL_OK;
}
static uint32_t release_recording(SatchelGuestTransfers *s,Recording *r){
    if(r->in_flight)return SATCHEL_GUEST_POOL_BUSY;uint32_t status=raster_record_status(satchel_guest_raster_record_destroy(&r->raster));if(status)return status;status=release_compute(s,r->command_generation,&r->compute,&r->compute_bound);if(status)return status;status=release_refs(s,&r->refs);if(status)return status;
    if(r->command_lease){status=satchel_guest_pools_release(s->pools,r->command_lease);if(status)return status;r->command_lease=0;}
    Command *c=command(s,r->command_owner);if(c && c->id==r->command_generation && c->sealed==r->id){c->sealed=0;c->count=0;}
    memset(r,0,sizeof(*r));return SATCHEL_GUEST_POOL_OK;
}
static uint32_t validate_recording(SatchelGuestTransfers *s,SatchelCPUProbe *cpu,Queue *q,Recording *r,SatchelVulkanTransferCommand *output){
    if(!r || r->in_flight || r->device!=q->device || r->device_generation!=q->device_generation)return SATCHEL_GUEST_POOL_BAD_OBJECT;
    SatchelGuestPoolView view={0};uint32_t status=satchel_guest_pools_lease_view(s->pools,cpu,r->command_lease,&view);if(status)return status;
    const uint64_t size=(uint64_t)r->count*RECORD_BYTES+END_BYTES;
    if(r->command_base<view.cpu_address || r->command_base-view.cpu_address>view.size || size>view.size-(r->command_base-view.cpu_address))return SATCHEL_GUEST_POOL_BAD_OBJECT;
    uint8_t expected[SATCHEL_GUEST_TRANSFER_COMMAND_LIMIT*RECORD_BYTES+END_BYTES],actual[sizeof(expected)],control[CONTROL_BYTES],read_control[CONTROL_BYTES];
    for(unsigned i=0;i<r->count;i++)encode_record(expected+i*RECORD_BYTES,&r->records[i].gpu);
    tag(expected+r->count*RECORD_BYTES,END_BYTES,0x504c454e,r->id);control_tag(control,r);
    if(!satchel_cpu_probe_read(cpu,r->command_base,actual,(size_t)size) || raw_range(cpu,r->control_base,CONTROL_BYTES) || !satchel_cpu_probe_read(cpu,r->control_base,read_control,sizeof(read_control)))return SATCHEL_GUEST_POOL_MEMORY;
    if(memcmp(actual,expected,(size_t)size) || memcmp(control,read_control,sizeof(control)))return SATCHEL_GUEST_POOL_BAD_OBJECT;
    for(unsigned i=0;i<r->count;i++){
        const Recorded *record=&r->records[i];SatchelGuestPoolBinding destination={0},source={0};
        if(r->compute.program || r->raster){output[i]=record->gpu;continue;}
        status=satchel_guest_pools_lease_binding(s->pools,cpu,record->destination_lease,record->gpu.destination,record->gpu.size,&destination);if(status)return status;
        output[i]=record->gpu;output[i].destination=destination.buffer;output[i].destination_offset=destination.offset;
        if(record->gpu.kind==SATCHEL_VULKAN_TRANSFER_COPY){status=satchel_guest_pools_lease_binding(s->pools,cpu,record->source_lease,record->gpu.source,record->gpu.size,&source);if(status)return status;output[i].source=source.buffer;output[i].source_offset=source.offset;}
    }
    return SATCHEL_GUEST_POOL_OK;
}
static void clear_pending(SatchelGuestTransfers *s,Queue *q){for(unsigned i=0;i<q->pending_count;i++){Recording *r=recording(s,q->handles[i]);if(r && r->in_flight)--r->in_flight;q->handles[i]=0;}q->pending_count=0;}
static void queue_tag(uint8_t bytes[64],Queue *q,int pending){tag(bytes,64,0x504c5155,q->id);put64(bytes+16,q->submitted);put64(bytes+24,q->finished);put32(bytes+32,pending?1:0);}
static uint32_t submit(SatchelGuestTransfers *s,SatchelCPUProbe *cpu,Queue *q,const SatchelGuestTransferRequest *request,SatchelCPUProbeCancelled cancel,void *context){
    if(satchel_guest_transfers_raster_pending(s) || q->job || q->compute_job || q->pending_count)return SATCHEL_GUEST_POOL_BUSY;
    const uint32_t n=(uint32_t)request->x[1];if(!n || n>SATCHEL_GUEST_TRANSFER_SUBMIT_HANDLE_LIMIT)return SATCHEL_GUEST_POOL_INVALID;
    uint8_t handles_bytes[SATCHEL_GUEST_TRANSFER_SUBMIT_HANDLE_LIMIT*8];if(!satchel_cpu_probe_read(cpu,request->x[2],handles_bytes,n*8))return SATCHEL_GUEST_POOL_MEMORY;
    SatchelVulkanTransferCommand commands[SATCHEL_GUEST_TRANSFER_COMMAND_LIMIT];Recording *handles[SATCHEL_GUEST_TRANSFER_SUBMIT_HANDLE_LIMIT];uint32_t count=0,unique=0;uint64_t work=0;Recording *compute=NULL,*raster=NULL;
    uint8_t encoded[SATCHEL_GUEST_TRANSFER_COMMAND_LIMIT*RECORD_BYTES],control[16+SATCHEL_GUEST_TRANSFER_SUBMIT_HANDLE_LIMIT*8]={0};
    for(unsigned i=0;i<n;i++){
        Recording *r=recording(s,get64(handles_bytes+i*8));if(!r || !r->count || r->count>SATCHEL_GUEST_TRANSFER_COMMAND_LIMIT-count)return SATCHEL_GUEST_POOL_BAD_OBJECT;
        if(r->raster){if(n!=1)return SATCHEL_GUEST_POOL_UNSUPPORTED;raster=r;}
        if(r->compute.program){if(n!=1 || !r->compute_bound || !q->compute_size || s->compute_command!=r->command_generation)return SATCHEL_GUEST_POOL_UNSUPPORTED;compute=r;}
        uint32_t status=validate_recording(s,cpu,q,r,commands+count);if(status)return status;
        int known=0;for(unsigned j=0;j<unique;j++)known|=handles[j]==r;if(!known)handles[unique++]=r;
        for(unsigned j=0;j<r->count;j++){const Recorded *record=&r->records[j];const uint64_t factor=record->gpu.kind==SATCHEL_VULKAN_TRANSFER_COPY?2:1;
            if(!compute && !raster && record->gpu.size>(SATCHEL_VULKAN_TRANSFER_WORK_LIMIT-work)/factor)return SATCHEL_GUEST_POOL_LIMIT_REACHED;if(!compute && !raster)work+=record->gpu.size*factor;
            encode_record(encoded+(count+j)*RECORD_BYTES,&record->gpu);}
        count+=r->count;put64(control+16+i*8,r->id);
    }
    const size_t command_bytes=count*RECORD_BYTES,control_bytes=16+n*8;
    if(q->command_size<64 || command_bytes>q->command_size-64 || control_bytes>q->control_size || q->submitted==UINT64_MAX)return SATCHEL_GUEST_POOL_LIMIT_REACHED;
    put32(control,0x504c5355);put32(control+4,n);put64(control+8,q->submitted+1);
    const uint64_t command_address=q->memory+64,control_address=q->memory+q->command_size+q->compute_size;
    /* Queue memory is explicit preparation storage. A backend refusal before
     * ownership may leave this complete staged snapshot, but never advances
     * submitted state or retains handles. Do not attempt an allocating rollback
     * across CPU writes and a driver call: those are not one transaction. The
     * opaque queue object is updated by Finish after actual fence completion. */
    SatchelCPUProbeWrite writes[]={{command_address,encoded,command_bytes},{control_address,control,control_bytes}};
    uint32_t status=writev(cpu,writes,2);if(status)return status;
    if(raster){
      SatchelGuestRasterRequest snapshot;status=raster_record_status(satchel_guest_raster_record_snapshot(raster->raster,cpu,&snapshot));if(status)return status;
      SatchelGuestRasterRun *run=NULL;uint32_t started=satchel_guest_raster_run_begin(s->pools,s->textures,s->raster_programs,cpu,&snapshot,&run,cancel,context);
      if(!run)return started==SATCHEL_GRUN_OK?SATCHEL_GUEST_POOL_INTERNAL:raster_run_status(started);
      /* Own every nonnull run before any fallible wait/report. A cleanup-only
       * retained admission error holds its handle but isn't a GPU submission. */
      q->raster_run=run;q->raster_mode=1;q->raster_error=raster_run_status(started);q->pending_count=1;q->handles[0]=raster->id;++raster->in_flight;
      SatchelGuestRasterRunReport report={0};uint32_t inspected=satchel_guest_raster_run_peek(run,&report);if(inspected)return raster_run_status(inspected);
      if(report.phase==1 || report.phase==2){q->raster_attempted=1;++q->submitted;}else return q->raster_error?q->raster_error:SATCHEL_GUEST_POOL_INTERNAL;
      if(started)return raster_run_status(started);
      /* QueueSubmit success means the draw batch, not merely its prerequisite
       * upload, has actually reached vkQueueSubmit. This wait is bounded. */
      uint32_t progressed=satchel_guest_raster_run_poll(&q->raster_run,5000,cancel && cancel(context),&report);
      if(report.phase==2){q->raster_draw_submitted=1;if(progressed==SATCHEL_GRUN_BUSY && report.status==SATCHEL_GRUN_BUSY)return SATCHEL_GUEST_POOL_OK;}
      if(!q->raster_run){q->raster_completed=report.published && report.draw_completed;q->raster_error=raster_run_status(progressed);if(!q->raster_completed && !q->raster_error)q->raster_error=SATCHEL_GUEST_POOL_INTERNAL;}
      else if(progressed!=SATCHEL_GRUN_BUSY)q->raster_error=raster_run_status(progressed);
      return q->raster_error?q->raster_error:raster_run_status(progressed);
    }
    if(compute){SatchelGuestComputeReport report={0};uint32_t submitted=satchel_guest_compute_submit(s->compute,cpu,&report,cancel,context);
      if(report.submitted){q->compute_job=1;q->submit_error=compute_status(submitted);++q->submitted;q->pending_count=1;q->handles[0]=compute->id;++compute->in_flight;}
      return compute_status(submitted);
    }
    uint64_t job=0;SatchelVulkanStatus submitted=satchel_vulkan_transfer_batch(q->backend,commands,count,&job,cancel,context);
    if(job){q->job=job;q->submit_error=gpu_status(submitted);++q->submitted;q->pending_count=unique;for(unsigned i=0;i<unique;i++){q->handles[i]=handles[i]->id;++handles[i]->in_flight;}return gpu_status(submitted);}
    return submitted==SATCHEL_VULKAN_OK?SATCHEL_GUEST_POOL_INTERNAL:gpu_status(submitted);
}
/* Submit contacts vkQueueSubmit immediately. Flush validates publication and
 * ownership but neither waits/reaps a fence nor changes pool cache visibility.
 * Retained jobs from failed submission cannot be reported as successful flush. */
static uint32_t flush(SatchelGuestTransfers *s,const Queue *q){
 if(q->raster_mode){if(q->job || q->compute_job || q->pending_count!=1)return SATCHEL_GUEST_POOL_INTERNAL;Recording *r=recording(s,q->handles[0]);if(!r || !r->raster || r->in_flight!=1)return SATCHEL_GUEST_POOL_INTERNAL;if(q->raster_error)return q->raster_error;return q->raster_draw_submitted?SATCHEL_GUEST_POOL_OK:SATCHEL_GUEST_POOL_BUSY;}
 if((q->job && q->compute_job) || q->pending_count>SATCHEL_GUEST_TRANSFER_SUBMIT_HANDLE_LIMIT || q->finished>q->submitted)return SATCHEL_GUEST_POOL_INTERNAL;
 if(!q->pending_count)return (q->job || q->compute_job || q->submitted!=q->finished || q->submit_error)?SATCHEL_GUEST_POOL_INTERNAL:SATCHEL_GUEST_POOL_OK;
 if(q->submitted-q->finished!=1)return SATCHEL_GUEST_POOL_INTERNAL;
 for(unsigned i=0;i<q->pending_count;i++){Recording *r=recording(s,q->handles[i]);if(!r || r->in_flight!=1 || r->device!=q->device || r->device_generation!=q->device_generation)return SATCHEL_GUEST_POOL_INTERNAL;for(unsigned j=0;j<i;j++)if(q->handles[j]==q->handles[i])return SATCHEL_GUEST_POOL_INTERNAL;}
 return q->submit_error;
}
static uint32_t complete_work(SatchelGuestTransfers *s,Queue *q,uint32_t timeout,SatchelCPUProbeCancelled cancel,void *context){
    if(q->raster_mode){
      if(q->raster_run){SatchelGuestRasterRunReport report={0};uint32_t progressed=satchel_guest_raster_run_poll(&q->raster_run,timeout,cancel && cancel(context),&report);if(report.phase==2)q->raster_draw_submitted=1;
        if(q->raster_run)return raster_run_status(progressed);q->raster_completed=report.draw_completed && report.published;q->raster_error=raster_run_status(progressed);if(!q->raster_completed && !q->raster_error)q->raster_error=SATCHEL_GUEST_POOL_INTERNAL;
      }
      if(q->raster_error)return q->raster_error;if(!q->raster_completed || !q->raster_attempted)return SATCHEL_GUEST_POOL_INTERNAL;
      return SATCHEL_GUEST_POOL_OK;
    }
    if(q->compute_job){SatchelGuestComputeReport report={0};uint32_t status=compute_status(satchel_guest_compute_wait(s->compute,timeout,&report,cancel,context));if(status)return status;if(!report.completed || report.resources_retained)return SATCHEL_GUEST_POOL_INTERNAL;q->compute_job=0;}
    if(q->job){SatchelVulkanSubmissionReport report={0};SatchelVulkanStatus status=satchel_vulkan_submission_wait(q->backend,q->job,timeout,cancel,context,&report);
        if(status!=SATCHEL_VULKAN_OK)return gpu_status(status);if(!report.completed || report.resources_retained)return SATCHEL_GUEST_POOL_INTERNAL;q->job=0;}
    return SATCHEL_GUEST_POOL_OK;
}
static void commit_completion(SatchelGuestTransfers *s,Queue *q){q->finished=q->submitted;q->submit_error=0;clear_pending(s,q);q->raster_mode=q->raster_attempted=q->raster_draw_submitted=q->raster_completed=q->raster_error=0;}
static uint32_t finish(SatchelGuestTransfers *s,SatchelCPUProbe *cpu,Queue *q,SatchelCPUProbeCancelled cancel,void *context){
    uint32_t status=complete_work(s,q,5000,cancel,context);if(status)return status;
    if(q->raster_mode || q->pending_count || q->present_dirty){Queue next=*q;next.finished=q->submitted;uint8_t object[64];queue_tag(object,&next,0);status=write(cpu,q->object,object,sizeof(object));if(status)return status;commit_completion(s,q);q->present_dirty=0;}
    return SATCHEL_GUEST_POOL_OK;
}
static uint32_t execute(SatchelGuestTransfers *s,SatchelCPUProbe *cpu,const SatchelGuestTransferRequest *r,SatchelGuestTransferResult *out,SatchelCPUProbeCancelled cancel,void *context){
    if(s->retiring)return SATCHEL_GUEST_POOL_INVALID;if(cancel && cancel(context))return SATCHEL_GUEST_TRANSFER_CANCELLED;
    if(r->operation>=SATCHEL_GT_PROGRAM_INITIALIZE && r->operation<=SATCHEL_GT_PROGRAM_SHADERS){
      if(!s->compute)return SATCHEL_GUEST_POOL_UNSUPPORTED;uint32_t status;
      if(r->operation==SATCHEL_GT_PROGRAM_INITIALIZE)status=satchel_guest_compute_program_initialize(s->compute,cpu,r->x[0],r->x[1]);
      else if(r->operation==SATCHEL_GT_PROGRAM_FINALIZE)status=satchel_guest_compute_program_finalize(s->compute,cpu,r->x[0]);
      else status=satchel_guest_compute_program_admit(s->compute,cpu,r->x[0],(int32_t)r->x[1],r->x[2]);
      if(!status && r->operation!=SATCHEL_GT_PROGRAM_FINALIZE){out->value=1;out->set_value=1;}return compute_status(status);
    }
    if(r->operation>=SATCHEL_GT_QUEUE_DEFAULTS && r->operation<=SATCHEL_GT_QUEUE_MEMORY)return builder(s,cpu,r,out);
    if(r->operation==SATCHEL_GT_QUEUE_INITIALIZE){uint32_t status=queue_initialize(s,cpu,r->x[0],r->x[1]);if(!status){out->set_value=1;out->value=1;}return status;}
    if((r->operation>=SATCHEL_GT_QUEUE_FINALIZE && r->operation<=SATCHEL_GT_QUEUE_FINISH) || r->operation==SATCHEL_GT_QUEUE_FLUSH){Queue *q=queue(s,r->x[0]);uint32_t status=check_queue(s,cpu,q);if(status)return status;
        if(q->present_lease)return SATCHEL_GUEST_POOL_BUSY;
        if(q->present_dirty){uint8_t object[64];queue_tag(object,q,0);status=write(cpu,q->object,object,sizeof(object));if(status)return status;q->present_dirty=0;}
        if(r->operation==SATCHEL_GT_QUEUE_FLUSH)return flush(s,q);
        if(r->operation==SATCHEL_GT_QUEUE_SUBMIT)return submit(s,cpu,q,r,cancel,context);
        if(r->operation==SATCHEL_GT_QUEUE_FINISH)return finish(s,cpu,q,cancel,context);
        if(q->job || q->compute_job || q->raster_mode || q->pending_count)return SATCHEL_GUEST_POOL_BUSY;uint8_t empty[QUEUE_BYTES]={0};status=write(cpu,q->object,empty,sizeof(empty));if(!status)memset(q,0,sizeof(*q));return status;}
    if(r->operation==SATCHEL_GT_COMMAND_INITIALIZE){uint32_t status=command_initialize(s,cpu,r->x[0],r->x[1]);if(!status){out->set_value=1;out->value=1;}return status;}
    if(r->operation==SATCHEL_GT_HANDLE_FINALIZE){Recording *h=recording(s,r->x[1]);uint64_t backend=0,generation=0;uint32_t status=device(s,cpu,r->x[0],&backend,&generation);
        if(status || !h || h->device!=r->x[0] || h->device_generation!=generation)return SATCHEL_GUEST_POOL_BAD_OBJECT;return release_recording(s,h);}
    Command *c=command(s,r->x[0]);uint32_t status=check_command(s,cpu,c);if(status)return status;
    if(r->operation==SATCHEL_GT_COMMAND_FINALIZE){if(c->recording)return SATCHEL_GUEST_POOL_BUSY;uint8_t empty[COMMAND_BYTES]={0};status=write(cpu,c->object,empty,sizeof(empty));if(status)return status;
        status=raster_record_status(satchel_guest_raster_record_destroy(&c->raster));if(status)return status;status=release_compute(s,c->id,&c->compute,&c->compute_bound);if(status)return status;status=release_refs(s,&c->refs);if(status)return status;if(c->command_lease){status=satchel_guest_pools_release(s->pools,c->command_lease);if(status)return status;}memset(c,0,sizeof(*c));return SATCHEL_GUEST_POOL_OK;}
    if(r->operation==SATCHEL_GT_COMMAND_MEMORY || r->operation==SATCHEL_GT_CONTROL_MEMORY)return add_memory(s,cpu,c,r);
    if(r->operation==SATCHEL_GT_BEGIN){if(c->recording || c->sealed)return SATCHEL_GUEST_POOL_BUSY;status=command_memory(s,cpu,c);if(status)return status;
        uint8_t bytes[COMMAND_BYTES];command_tag(bytes,c,1,0);status=write(cpu,c->object,bytes,sizeof(bytes));if(!status){c->recording=1;c->count=0;}return status;}
    if(r->operation==SATCHEL_GT_END){status=end_recording(s,cpu,c,&out->value);if(!status)out->set_value=1;return status;}
    if(r->operation==SATCHEL_GT_CLEAR || r->operation==SATCHEL_GT_COPY)return record_transfer(s,cpu,c,r);
    if((r->operation>=SATCHEL_GT_RENDER_TARGETS && r->operation<=SATCHEL_GT_SWIZZLES) || (r->operation==SATCHEL_GT_BIND_PROGRAM && (uint32_t)r->x[2]==3))return record_raster(s,cpu,c,r);
    if(r->operation>=SATCHEL_GT_BIND_PROGRAM && r->operation<=SATCHEL_GT_DISPATCH)return record_compute(s,cpu,c,r);
    const uint64_t used=c->count*RECORD_BYTES+(c->sealed?END_BYTES:0),control_used=c->sealed?CONTROL_BYTES:0;
    switch(r->operation){
        case SATCHEL_GT_COMMAND_SIZE:out->value=c->command_size;break;
        case SATCHEL_GT_COMMAND_USED:out->value=used;break;
        case SATCHEL_GT_COMMAND_FREE:out->value=c->command_size-used;break;
        case SATCHEL_GT_CONTROL_SIZE:out->value=c->control_size;break;
        case SATCHEL_GT_CONTROL_USED:out->value=control_used;break;
        case SATCHEL_GT_CONTROL_FREE:out->value=c->control_size-control_used;break;
        case SATCHEL_GT_IS_RECORDING:out->value=c->recording;break;
        default:return SATCHEL_GUEST_POOL_UNSUPPORTED;
    }out->set_value=1;return SATCHEL_GUEST_POOL_OK;
}
SatchelGuestTransfers *satchel_guest_transfers_create(SatchelGuestPools *pools,SatchelGuestPoolDeviceValidator validate,SatchelGuestPoolRangeGuard guard,void *context){
    if(!pools || !validate)return NULL;SatchelGuestTransfers *s=calloc(1,sizeof(*s));if(!s)return NULL;s->pools=pools;s->validate=validate;s->guard=guard;s->context=context;atomic_flag_clear(&s->active);return s;
}
SatchelGuestTransfers *satchel_guest_transfers_create_checked(SatchelGuestPools *pools,SatchelGuestPoolDeviceValidator validate,SatchelGuestPoolCheckedRangeGuard guard,void *context){
    SatchelGuestTransfers *s=satchel_guest_transfers_create(pools,validate,NULL,context);if(s)s->checked_guard=guard;return s;
}
uint32_t satchel_guest_transfers_execute(SatchelGuestTransfers *s,SatchelCPUProbe *cpu,const SatchelGuestTransferRequest *request,SatchelGuestTransferResult *result,SatchelCPUProbeCancelled cancel,void *context){
    if(!result)return SATCHEL_GUEST_POOL_INVALID;*result=(SatchelGuestTransferResult){.status=SATCHEL_GUEST_POOL_INVALID};if(!s || !cpu || !request)return SATCHEL_GUEST_POOL_INVALID;
    if(atomic_flag_test_and_set(&s->active)){result->status=SATCHEL_GUEST_POOL_BUSY;return result->status;}
    const SatchelGuestTransferRequest copied=*request;result->status=execute(s,cpu,&copied,result,cancel,context);if(result->status){result->set_value=0;result->value=0;}
    atomic_flag_clear(&s->active);return result->status;
}
uint32_t satchel_guest_transfers_device_children(SatchelGuestTransfers *s,uint64_t device_address,uint32_t *count){
    if(count)*count=0;if(!s || !count)return SATCHEL_GUEST_POOL_INVALID;if(atomic_flag_test_and_set(&s->active))return SATCHEL_GUEST_POOL_BUSY;
    for(unsigned i=0;i<SATCHEL_GUEST_TRANSFER_QUEUE_LIMIT;i++)*count+=s->queues[i].id && s->queues[i].device==device_address;
    for(unsigned i=0;i<SATCHEL_GUEST_TRANSFER_COMMAND_BUFFER_LIMIT;i++)*count+=s->commands[i].id && s->commands[i].device==device_address;
    for(unsigned i=0;i<SATCHEL_GUEST_TRANSFER_HANDLE_LIMIT;i++)*count+=s->recordings[i].id && s->recordings[i].device==device_address;
    atomic_flag_clear(&s->active);return SATCHEL_GUEST_POOL_OK;
}
static uint32_t check_unowned_range(SatchelGuestTransfers *s,SatchelCPUProbe *cpu,uint64_t address,uint64_t size){
    if(!s)return SATCHEL_GUEST_POOL_INVALID;if(atomic_flag_test_and_set(&s->active))return SATCHEL_GUEST_POOL_BUSY;uint32_t status=unowned(s,cpu,address,size);atomic_flag_clear(&s->active);return status;
}
uint32_t satchel_guest_transfers_check_pool_range_checked(const SatchelGuestTransfers *s,SatchelCPUProbe *cpu,uint64_t address,uint64_t size){
    if(!s || !size || address>UINT64_MAX-size)return SATCHEL_GUEST_POOL_INVALID;
    for(unsigned i=0;i<SATCHEL_GUEST_TRANSFER_QUEUE_LIMIT;i++){const Queue *q=&s->queues[i];if(q->id && (physical_overlap(cpu,address,size,q->object,QUEUE_BYTES) || physical_overlap(cpu,address,size,q->memory,q->memory_size)))return SATCHEL_GUEST_POOL_BAD_OBJECT;}
    for(unsigned i=0;i<SATCHEL_GUEST_TRANSFER_COMMAND_BUFFER_LIMIT;i++){const Command *c=&s->commands[i];if(c->id && (physical_overlap(cpu,address,size,c->object,COMMAND_BYTES) || physical_overlap(cpu,address,size,c->control_base,c->control_size)))return SATCHEL_GUEST_POOL_BAD_OBJECT;}
    for(unsigned i=0;i<SATCHEL_GUEST_TRANSFER_HANDLE_LIMIT;i++){const Recording *r=&s->recordings[i];if(r->id && physical_overlap(cpu,address,size,r->control_base,CONTROL_BYTES))return SATCHEL_GUEST_POOL_BAD_OBJECT;}
    return SATCHEL_GUEST_POOL_OK;
}
uint32_t satchel_guest_transfers_check_unowned_range(SatchelGuestTransfers *s,uint64_t address,uint64_t size){return check_unowned_range(s,NULL,address,size);}
uint32_t satchel_guest_transfers_check_unowned_range_checked(SatchelGuestTransfers *s,SatchelCPUProbe *cpu,uint64_t address,uint64_t size){if(!cpu)return SATCHEL_GUEST_POOL_INVALID;return check_unowned_range(s,cpu,address,size);}
uint32_t satchel_guest_transfers_try_destroy(SatchelGuestTransfers **pointer){
    if(!pointer)return SATCHEL_GUEST_POOL_INVALID;if(!*pointer)return SATCHEL_GUEST_POOL_OK;SatchelGuestTransfers *s=*pointer;
    if(atomic_flag_test_and_set(&s->active))return SATCHEL_GUEST_POOL_BUSY;s->retiring=1;uint32_t status=SATCHEL_GUEST_POOL_OK;
    for(unsigned i=0;i<SATCHEL_GUEST_TRANSFER_QUEUE_LIMIT;i++)if(s->queues[i].present_lease){status=SATCHEL_GUEST_POOL_BUSY;goto done;}
    if(s->compute){SatchelGuestComputeReport report={0};status=compute_status(satchel_guest_compute_wait(s->compute,0,&report,NULL,NULL));if(status)goto done;}
    for(unsigned i=0;i<SATCHEL_GUEST_TRANSFER_QUEUE_LIMIT;i++)if(s->queues[i].id){Queue *q=&s->queues[i];q->compute_job=0;
        if(q->raster_run){SatchelGuestRasterRunReport report={0};uint32_t waited=satchel_guest_raster_run_poll(&q->raster_run,0,1,&report);if(q->raster_run){status=raster_run_status(waited);if(!status)status=SATCHEL_GUEST_POOL_BUSY;goto done;}}
        if(q->job){SatchelVulkanSubmissionReport report={0};SatchelVulkanStatus waited=satchel_vulkan_submission_wait(q->backend,q->job,0,NULL,NULL,&report);if(waited!=SATCHEL_VULKAN_OK){status=gpu_status(waited);goto done;}if(!report.completed || report.resources_retained){status=SATCHEL_GUEST_POOL_INTERNAL;goto done;}q->job=0;}
        clear_pending(s,q);memset(q,0,sizeof(*q));}
    for(unsigned i=0;i<SATCHEL_GUEST_TRANSFER_HANDLE_LIMIT;i++)if(s->recordings[i].id){status=release_recording(s,&s->recordings[i]);if(status)goto done;}
    for(unsigned i=0;i<SATCHEL_GUEST_TRANSFER_COMMAND_BUFFER_LIMIT;i++)if(s->commands[i].id){Command *c=&s->commands[i];status=raster_record_status(satchel_guest_raster_record_destroy(&c->raster));if(status)goto done;status=release_compute(s,c->id,&c->compute,&c->compute_bound);if(status)goto done;status=release_refs(s,&c->refs);if(status)goto done;
        if(c->command_lease){status=satchel_guest_pools_release(s->pools,c->command_lease);if(status)goto done;c->command_lease=0;}memset(c,0,sizeof(*c));}
 done:
    if(!status){*pointer=NULL;free(s);}else atomic_flag_clear(&s->active);return status;
}

int satchel_guest_transfers_attach_compute(SatchelGuestTransfers *s,SatchelGuestCompute *compute){if(!s || !compute || s->compute || s->retiring)return 0;s->compute=compute;return 1;}

uint32_t satchel_guest_transfers_guard(const SatchelGuestTransfers *s,SatchelCPUProbe *cpu,uint64_t a,uint64_t n){if(!s || !cpu || !n || a>UINT64_MAX-n)return SATCHEL_GUEST_POOL_INVALID;return unowned((SatchelGuestTransfers *)s,cpu,a,n);}

int satchel_guest_transfers_attach_raster(SatchelGuestTransfers *s,SatchelGuestTextures *t,SatchelGuestRasterPrograms *p,SatchelGuestRasterQueueQuery query,void *ctx){if(!s||!t||!p||!query||s->textures||s->retiring)return 0;for(unsigned i=0;i<SATCHEL_GUEST_TRANSFER_QUEUE_LIMIT;i++)if(s->queues[i].id)return 0;s->textures=t;s->raster_programs=p;s->raster_query=query;s->raster_context=ctx;return 1;}
int satchel_guest_transfers_raster_pending(const SatchelGuestTransfers *s){if(!s)return 0;for(unsigned i=0;i<SATCHEL_GUEST_TRANSFER_QUEUE_LIMIT;i++)if(s->queues[i].raster_mode)return 1;return 0;}

static Queue *present_queue(SatchelGuestTransfers *s,uint64_t lease){if(!lease)return NULL;for(unsigned i=0;i<SATCHEL_GUEST_TRANSFER_QUEUE_LIMIT;i++)if(s->queues[i].present_lease==lease)return &s->queues[i];return NULL;}
uint32_t satchel_guest_transfers_present_prepare(SatchelGuestTransfers *s,SatchelCPUProbe *cpu,uint64_t address,uint64_t expected,uint64_t *lease,SatchelGuestPresentQueueInfo *out){
 if(!s||!cpu||!lease||!out||((uintptr_t)lease%_Alignof(uint64_t))||((uintptr_t)out%_Alignof(SatchelGuestPresentQueueInfo)))return SATCHEL_GUEST_POOL_INVALID;
 uintptr_t lp=(uintptr_t)lease,op=(uintptr_t)out;if(lp>UINTPTR_MAX-sizeof(*lease)||op>UINTPTR_MAX-sizeof(*out)||(lp<op+sizeof(*out)&&op<lp+sizeof(*lease)))return SATCHEL_GUEST_POOL_INVALID;
 if(atomic_flag_test_and_set(&s->active))return SATCHEL_GUEST_POOL_BUSY;uint32_t status=SATCHEL_GUEST_POOL_BAD_OBJECT;Queue *q=queue(s,address);
 if(s->retiring){status=SATCHEL_GUEST_POOL_BUSY;goto done;}status=check_queue(s,cpu,q);if(status)goto done;if(q->device!=expected){status=SATCHEL_GUEST_POOL_BAD_OBJECT;goto done;}if(q->present_lease){status=SATCHEL_GUEST_POOL_BUSY;goto done;}
 if(q->submit_error||q->raster_error){status=q->raster_error?q->raster_error:q->submit_error;goto done;}
 uint64_t id=0;if(!identity(&next_present_lease,&id)){status=SATCHEL_GUEST_POOL_LIMIT_REACHED;goto done;}
 SatchelGuestPresentQueueInfo info={1,sizeof(info),q->object,q->id,q->device,q->backend,q->device_generation,q->submitted};
 q->present_lease=id;q->present_sequence=q->submitted;q->present_cancelled=q->present_drained=q->present_error=0;*lease=id;*out=info;status=SATCHEL_GUEST_POOL_OK;
 done:atomic_flag_clear(&s->active);return status;
}
uint32_t satchel_guest_transfers_present_poll(SatchelGuestTransfers *s,uint64_t lease,uint32_t timeout,int cancel,SatchelGuestPresentQueueProgress *out){
 if(!s||!out||((uintptr_t)out%_Alignof(SatchelGuestPresentQueueProgress))||timeout>10||(cancel!=0&&cancel!=1))return SATCHEL_GUEST_POOL_INVALID;
 if(atomic_flag_test_and_set(&s->active))return SATCHEL_GUEST_POOL_BUSY;Queue *q=present_queue(s,lease);if(!q){atomic_flag_clear(&s->active);return SATCHEL_GUEST_POOL_BAD_OBJECT;}
 if(cancel)q->present_cancelled=1;uint32_t status=SATCHEL_GUEST_POOL_OK;if(q->submitted!=q->present_sequence)status=SATCHEL_GUEST_POOL_INTERNAL;
 /* A cancelled preparation still owes every preceding real queue operation. */
 if(!status&&!q->present_drained){status=complete_work(s,q,timeout,NULL,NULL);if(!status)q->present_drained=1;
  else if(status!=SATCHEL_GUEST_POOL_BUSY){q->present_error=status;
   /* A terminal failed prior operation is never advertised READY. Cancellation
    * may release only the ordering lease after every actual GPU owner is gone;
    * original errored queue/recording ownership remains for ordinary retirement. */
   if(q->present_cancelled&&!q->raster_run&&!q->job&&!q->compute_job)q->present_drained=1;
  }}
 if(q->present_error)status=q->present_error;else if(!status&&q->present_cancelled)status=SATCHEL_GUEST_TRANSFER_CANCELLED;
 SatchelGuestPresentQueueProgress progress={status,q->present_drained&&!q->present_cancelled&&!q->present_error,q->present_drained,q->present_cancelled,q->present_sequence};*out=progress;atomic_flag_clear(&s->active);return status;
}
uint32_t satchel_guest_transfers_present_release(SatchelGuestTransfers *s,uint64_t lease){
 if(!s)return SATCHEL_GUEST_POOL_INVALID;if(atomic_flag_test_and_set(&s->active))return SATCHEL_GUEST_POOL_BUSY;Queue *q=present_queue(s,lease);uint32_t status=SATCHEL_GUEST_POOL_BAD_OBJECT;
 if(q){if(!q->present_drained)status=SATCHEL_GUEST_POOL_BUSY;else{if(!q->present_error){commit_completion(s,q);q->present_dirty=1;}q->present_lease=q->present_sequence=0;q->present_cancelled=q->present_drained=q->present_error=0;status=SATCHEL_GUEST_POOL_OK;}}
 atomic_flag_clear(&s->active);return status;
}
