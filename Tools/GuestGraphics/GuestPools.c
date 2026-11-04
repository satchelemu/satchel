#include "GuestPools.h"
#include "GuestDispatch.h"
#include "VulkanBuffer.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
typedef struct {
    uint32_t state;uint64_t object,generation,guest_device,device_generation,device,buffer,storage,size,gpu;uint32_t references;uint64_t allocation_size;uint32_t gpu_held;
} Pool;
typedef struct {uint64_t token,generation;uint32_t gpu_held;} Lease;
struct SatchelGuestPools {SatchelGuestPoolDeviceValidator validate;SatchelGuestPoolRangeGuard guard;SatchelGuestPoolCheckedRangeGuard checked_guard;void *context;atomic_flag active;uint32_t retiring;Pool pools[SATCHEL_GUEST_POOL_LIMIT];Lease leases[SATCHEL_GUEST_POOL_LEASE_LIMIT];};
static _Atomic uint64_t next_generation=1,next_gpu=UINT64_C(0x100000000),next_lease=1;
enum {TRANSIENT_LIMIT=64*1024*1024};
static _Atomic uint64_t transient_bytes;
static void *copy_allocate(size_t n){
 uint64_t before=atomic_load(&transient_bytes);do{if(n>TRANSIENT_LIMIT-before)return NULL;}while(!atomic_compare_exchange_weak(&transient_bytes,&before,before+n));
 void *p=malloc(n);if(!p)atomic_fetch_sub(&transient_bytes,n);return p;
}
static void copy_free(void *p,size_t n){if(p){free(p);atomic_fetch_sub(&transient_bytes,n);}}
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put64(uint8_t *p,uint64_t v){put32(p,(uint32_t)v);put32(p+4,(uint32_t)(v>>32));}
static uint64_t get64(const uint8_t *p){return get32(p)|(uint64_t)get32(p+4)<<32;}
static int take(_Atomic uint64_t *counter,uint64_t size,uint64_t *value){
    uint64_t current=atomic_load(counter);
    do {if(!size || current>UINT64_MAX-size)return 0;}while(!atomic_compare_exchange_weak(counter,&current,current+size));
    *value=current;return 1;
}
static int overlap(SatchelCPUProbe *cpu,uint64_t a,uint64_t n,uint64_t b,uint64_t m){
    int result=1;if(!cpu)return a<b+m && b<a+n;
    return !satchel_cpu_probe_physical_overlap(cpu,a,n,b,m,&result) || result;
}
static int guarded(SatchelGuestPools *owner,SatchelCPUProbe *cpu,uint64_t address,uint64_t size){
    if(owner->checked_guard)return owner->checked_guard(owner->context,cpu,address,size);
    return !owner->guard || owner->guard(owner->context,address,size);
}
static SatchelGuestPoolStatus backend(SatchelVulkanStatus status){return status==SATCHEL_VULKAN_OK?SATCHEL_GUEST_POOL_OK:(status==SATCHEL_VULKAN_BUSY?SATCHEL_GUEST_POOL_BUSY:(status==SATCHEL_VULKAN_UNSUPPORTED?SATCHEL_GUEST_POOL_UNSUPPORTED:SATCHEL_GUEST_POOL_BACKEND));}
static SatchelGuestPoolStatus write_guest(SatchelCPUProbe *cpu,uint64_t address,const void *bytes,size_t size){
    SatchelCPUProbeWrite write={address,bytes,size};
    const SatchelCPUWriteResult status=satchel_cpu_probe_writev(cpu,&write,1);
    return status==SATCHEL_CPU_WRITE_OK?SATCHEL_GUEST_POOL_OK:(status==SATCHEL_CPU_WRITE_BUSY?SATCHEL_GUEST_POOL_BUSY:(status==SATCHEL_CPU_WRITE_ALLOCATION?SATCHEL_GUEST_POOL_ALLOCATION:SATCHEL_GUEST_POOL_MEMORY));
}
static SatchelGuestPoolStatus device(SatchelGuestPools *owner,SatchelCPUProbe *cpu,uint64_t address,uint64_t *token,uint64_t *generation){
    *token=0;*generation=0;
    return owner->validate(owner->context,cpu,address,token,generation) && *token && *generation ? SATCHEL_GUEST_POOL_OK:SATCHEL_GUEST_POOL_BAD_OBJECT;
}
static SatchelGuestPoolStatus storage(SatchelGuestPools *owner,SatchelCPUProbe *cpu,uint64_t address,uint64_t size){
    if(!size || size>SATCHEL_GUEST_POOL_MAX_SIZE || (address&(SATCHEL_GUEST_POOL_ALIGNMENT-1)) || (size&(SATCHEL_GUEST_POOL_ALIGNMENT-1)))return SATCHEL_GUEST_POOL_UNSUPPORTED;
    if(!satchel_cpu_probe_check_range(cpu,address,size,SATCHEL_CPU_READ|SATCHEL_CPU_WRITE))return SATCHEL_GUEST_POOL_MEMORY;
    return !guarded(owner,cpu,address,size)?SATCHEL_GUEST_POOL_BAD_OBJECT:SATCHEL_GUEST_POOL_OK;
}
static SatchelGuestPoolStatus read_builder(SatchelGuestPools *owner,SatchelCPUProbe *cpu,uint64_t address,uint8_t bytes[64],int existing){
    if(!satchel_cpu_probe_check_range(cpu,address,64,SATCHEL_CPU_READ|SATCHEL_CPU_WRITE) || !satchel_cpu_probe_read(cpu,address,bytes,64))return SATCHEL_GUEST_POOL_MEMORY;
    if(!guarded(owner,cpu,address,64))return SATCHEL_GUEST_POOL_BAD_OBJECT;
    for(unsigned i=0;i<SATCHEL_GUEST_POOL_LIMIT;i++)if(owner->pools[i].state){
        const Pool *p=&owner->pools[i];if(overlap(cpu,address,64,p->object,256) || overlap(cpu,address,64,p->storage,p->size))return SATCHEL_GUEST_POOL_BAD_OBJECT;
    }
    if(existing && (get32(bytes)!=0x504c5042 || get32(bytes+4)!=1))return SATCHEL_GUEST_POOL_BAD_OBJECT;
    return SATCHEL_GUEST_POOL_OK;
}
static Pool *find(SatchelGuestPools *owner,uint64_t address){for(unsigned i=0;i<SATCHEL_GUEST_POOL_LIMIT;i++)if(owner->pools[i].state==1 && owner->pools[i].object==address)return &owner->pools[i];return NULL;}
static SatchelGuestPoolStatus validate_pool(SatchelGuestPools *owner,SatchelCPUProbe *cpu,Pool *pool){
    if(!pool)return SATCHEL_GUEST_POOL_BAD_OBJECT;
    uint8_t object[16];uint64_t token=0,generation=0;
    if(!satchel_cpu_probe_check_range(cpu,pool->object,256,SATCHEL_CPU_READ) || !satchel_cpu_probe_read(cpu,pool->object,object,sizeof(object)))return SATCHEL_GUEST_POOL_MEMORY;
    if(get32(object)!=0x504c504f || get32(object+4)!=1 || get64(object+8)!=pool->generation)return SATCHEL_GUEST_POOL_BAD_OBJECT;
    SatchelGuestPoolStatus status=device(owner,cpu,pool->guest_device,&token,&generation);
    if(status!=SATCHEL_GUEST_POOL_OK || token!=pool->device || generation!=pool->device_generation)return SATCHEL_GUEST_POOL_BAD_OBJECT;
    status=storage(owner,cpu,pool->storage,pool->size);if(status!=SATCHEL_GUEST_POOL_OK)return status;
    if(overlap(cpu,pool->storage,pool->size,pool->object,256) || overlap(cpu,pool->storage,pool->size,pool->guest_device,SATCHEL_GUEST_DISPATCH_DEVICE_SIZE) || overlap(cpu,pool->object,256,pool->guest_device,SATCHEL_GUEST_DISPATCH_DEVICE_SIZE))return SATCHEL_GUEST_POOL_BAD_OBJECT;
    for(unsigned i=0;i<SATCHEL_GUEST_POOL_LIMIT;i++){const Pool *other=&owner->pools[i];if(other==pool || !other->state)continue;
        if(overlap(cpu,pool->storage,pool->size,other->storage,other->size) || overlap(cpu,pool->storage,pool->size,other->object,256) || overlap(cpu,pool->object,256,other->storage,other->size) || overlap(cpu,pool->object,256,other->object,256))return SATCHEL_GUEST_POOL_BAD_OBJECT;}
    SatchelVulkanBufferInfo info={0};status=backend(satchel_vulkan_buffer_info(pool->device,pool->buffer,&info));
    if(status!=SATCHEL_GUEST_POOL_OK)return status;
    return info.device==pool->device && info.size==pool->size && info.usage==23 && info.allocation_size==pool->allocation_size ? SATCHEL_GUEST_POOL_OK:SATCHEL_GUEST_POOL_INTERNAL;
}
static SatchelGuestPoolStatus initialize(SatchelGuestPools *owner,SatchelCPUProbe *cpu,uint64_t address,uint64_t builder){
    uint8_t bytes[64],object[256]={0};
    SatchelGuestPoolStatus status=read_builder(owner,cpu,builder,bytes,1);if(status!=SATCHEL_GUEST_POOL_OK)return status;
    if(!satchel_cpu_probe_check_range(cpu,address,256,SATCHEL_CPU_READ|SATCHEL_CPU_WRITE))return SATCHEL_GUEST_POOL_MEMORY;
    if(!guarded(owner,cpu,address,256))return SATCHEL_GUEST_POOL_BAD_OBJECT;
    const uint64_t guest_device=get64(bytes+8),expected_generation=get64(bytes+16),base=get64(bytes+24),size=get64(bytes+32);
    if(get32(bytes+40)!=SATCHEL_GUEST_POOL_CACHED_FLAGS)return SATCHEL_GUEST_POOL_UNSUPPORTED;
    uint64_t token=0,device_generation=0;status=device(owner,cpu,guest_device,&token,&device_generation);
    if(status!=SATCHEL_GUEST_POOL_OK || device_generation!=expected_generation)return SATCHEL_GUEST_POOL_BAD_OBJECT;
    if(guest_device>UINT64_MAX-SATCHEL_GUEST_DISPATCH_DEVICE_SIZE)return SATCHEL_GUEST_POOL_BAD_OBJECT;
    status=storage(owner,cpu,base,size);if(status!=SATCHEL_GUEST_POOL_OK)return status;
    if(overlap(cpu,address,256,builder,64) || overlap(cpu,address,256,base,size) || overlap(cpu,builder,64,base,size) ||
        overlap(cpu,base,size,guest_device,SATCHEL_GUEST_DISPATCH_DEVICE_SIZE) || overlap(cpu,address,256,guest_device,SATCHEL_GUEST_DISPATCH_DEVICE_SIZE) ||
        overlap(cpu,builder,64,guest_device,SATCHEL_GUEST_DISPATCH_DEVICE_SIZE))return SATCHEL_GUEST_POOL_BAD_OBJECT;
    Pool *slot=NULL;
    for(unsigned i=0;i<SATCHEL_GUEST_POOL_LIMIT;i++){
        const Pool *p=&owner->pools[i];if(!p->state){if(!slot)slot=&owner->pools[i];continue;}
        if(overlap(cpu,address,256,p->object,256) || overlap(cpu,address,256,p->storage,p->size) ||
            overlap(cpu,base,size,p->object,256) || overlap(cpu,base,size,p->storage,p->size))return SATCHEL_GUEST_POOL_BAD_OBJECT;
    }
    if(!slot)return SATCHEL_GUEST_POOL_LIMIT_REACHED;
    uint64_t identity=0,gpu=0;
    if(!take(&next_generation,1,&identity) || !take(&next_gpu,size+SATCHEL_GUEST_POOL_ALIGNMENT,&gpu))return SATCHEL_GUEST_POOL_LIMIT_REACHED;
    uint8_t *snapshot=copy_allocate((size_t)size);if(!snapshot)return SATCHEL_GUEST_POOL_ALLOCATION;
    if(!satchel_cpu_probe_read(cpu,base,snapshot,(size_t)size)){copy_free(snapshot,(size_t)size);return SATCHEL_GUEST_POOL_MEMORY;}
    uint64_t buffer=0;SatchelVulkanBufferInfo info={0};status=backend(satchel_vulkan_buffer_create(token,size,23,&buffer,&info));
    if(status!=SATCHEL_GUEST_POOL_OK){copy_free(snapshot,(size_t)size);return status;}
    *slot=(Pool){2,address,identity,guest_device,device_generation,token,buffer,base,size,gpu,0,info.allocation_size,0};
    status=backend(satchel_vulkan_buffer_write(token,buffer,0,snapshot,(size_t)size));copy_free(snapshot,(size_t)size);
    put32(object,0x504c504f);put32(object+4,1);put64(object+8,identity);
    if(status==SATCHEL_GUEST_POOL_OK)status=write_guest(cpu,address,object,sizeof(object));
    if(status!=SATCHEL_GUEST_POOL_OK){
        if(satchel_vulkan_buffer_destroy(token,buffer)!=SATCHEL_VULKAN_OK)return SATCHEL_GUEST_POOL_INTERNAL;
        memset(slot,0,sizeof(*slot));return status;
    }
    slot->state=1;return SATCHEL_GUEST_POOL_OK;
}
static SatchelGuestPoolStatus execute(SatchelGuestPools *owner,SatchelCPUProbe *cpu,const SatchelGuestPoolRequest *request,uint64_t *result){
    if(owner->retiring)return SATCHEL_GUEST_POOL_INVALID;
    const uint32_t operation=request->operation;const uint64_t address=request->object;
    if(operation>=SATCHEL_GUEST_POOL_BUILDER_DEFAULTS && operation<=SATCHEL_GUEST_POOL_BUILDER_FLAGS){
        uint8_t bytes[64];SatchelGuestPoolStatus status=read_builder(owner,cpu,address,bytes,operation!=SATCHEL_GUEST_POOL_BUILDER_DEFAULTS);if(status!=SATCHEL_GUEST_POOL_OK)return status;
        if(operation==SATCHEL_GUEST_POOL_BUILDER_DEFAULTS){memset(bytes,0,sizeof(bytes));put32(bytes,0x504c5042);put32(bytes+4,1);}
        else if(operation==SATCHEL_GUEST_POOL_BUILDER_DEVICE){
            uint64_t token=0,generation=0;status=device(owner,cpu,request->argument0,&token,&generation);if(status!=SATCHEL_GUEST_POOL_OK)return status;
            if(request->argument0>UINT64_MAX-SATCHEL_GUEST_DISPATCH_DEVICE_SIZE || overlap(cpu,address,64,request->argument0,SATCHEL_GUEST_DISPATCH_DEVICE_SIZE))return SATCHEL_GUEST_POOL_BAD_OBJECT;
            put64(bytes+8,request->argument0);put64(bytes+16,generation);
        }else if(operation==SATCHEL_GUEST_POOL_BUILDER_STORAGE){
            status=storage(owner,cpu,request->argument0,request->argument1);if(status!=SATCHEL_GUEST_POOL_OK)return status;
            if(overlap(cpu,address,64,request->argument0,request->argument1))return SATCHEL_GUEST_POOL_BAD_OBJECT;
            put64(bytes+24,request->argument0);put64(bytes+32,request->argument1);
        }else {
            if((uint32_t)request->argument0!=SATCHEL_GUEST_POOL_CACHED_FLAGS)return SATCHEL_GUEST_POOL_UNSUPPORTED;
            put32(bytes+40,(uint32_t)request->argument0);
        }
        return write_guest(cpu,address,bytes,sizeof(bytes));
    }
    if(operation==SATCHEL_GUEST_POOL_INITIALIZE){const SatchelGuestPoolStatus status=initialize(owner,cpu,address,request->argument0);if(status==SATCHEL_GUEST_POOL_OK)*result=1;return status;}
    Pool *pool=find(owner,address);SatchelGuestPoolStatus status=validate_pool(owner,cpu,pool);if(status!=SATCHEL_GUEST_POOL_OK)return status;
    if(operation==SATCHEL_GUEST_POOL_MAP){*result=pool->storage;return SATCHEL_GUEST_POOL_OK;}
    if(operation==SATCHEL_GUEST_POOL_GPU_ADDRESS){*result=pool->gpu;return SATCHEL_GUEST_POOL_OK;}
    if(operation==SATCHEL_GUEST_POOL_FINALIZE){
        if(pool->references)return SATCHEL_GUEST_POOL_BUSY;
        uint8_t old[256],empty[256]={0};
        if(!satchel_cpu_probe_check_range(cpu,address,256,SATCHEL_CPU_READ|SATCHEL_CPU_WRITE) || !satchel_cpu_probe_read(cpu,address,old,sizeof(old)))return SATCHEL_GUEST_POOL_MEMORY;
        // Non-mutating backend ownership check, not a guest memory write probe.
        status=backend(satchel_vulkan_buffer_read(pool->device,pool->buffer,0,NULL,0));if(status!=SATCHEL_GUEST_POOL_OK)return status;
        status=write_guest(cpu,address,empty,sizeof(empty));if(status!=SATCHEL_GUEST_POOL_OK)return status;
        status=backend(satchel_vulkan_buffer_destroy(pool->device,pool->buffer));
        if(status!=SATCHEL_GUEST_POOL_OK){return write_guest(cpu,address,old,sizeof(old))==SATCHEL_GUEST_POOL_OK?status:SATCHEL_GUEST_POOL_INTERNAL;}
        memset(pool,0,sizeof(*pool));return SATCHEL_GUEST_POOL_OK;
    }
    if(operation!=SATCHEL_GUEST_POOL_FLUSH && operation!=SATCHEL_GUEST_POOL_INVALIDATE)return SATCHEL_GUEST_POOL_UNSUPPORTED;
    if(pool->gpu_held)return SATCHEL_GUEST_POOL_BUSY;
    const uint64_t offset=request->argument0,size=request->argument1;
    if(offset>INT64_MAX || offset>pool->size || size>pool->size-offset)return SATCHEL_GUEST_POOL_INVALID;
    if(!size)return SATCHEL_GUEST_POOL_OK;
    const uint64_t guest=pool->storage+offset;
    if(!satchel_cpu_probe_check_range(cpu,guest,size,SATCHEL_CPU_READ|SATCHEL_CPU_WRITE))return SATCHEL_GUEST_POOL_MEMORY;
    uint8_t *copy=copy_allocate((size_t)size);if(!copy)return SATCHEL_GUEST_POOL_ALLOCATION;
    if(operation==SATCHEL_GUEST_POOL_FLUSH){
        if(!satchel_cpu_probe_read(cpu,guest,copy,(size_t)size))status=SATCHEL_GUEST_POOL_MEMORY;
        else status=backend(satchel_vulkan_buffer_write(pool->device,pool->buffer,offset,copy,(size_t)size));
    }else {
        status=backend(satchel_vulkan_buffer_read(pool->device,pool->buffer,offset,copy,(size_t)size));
        if(status==SATCHEL_GUEST_POOL_OK)status=write_guest(cpu,guest,copy,(size_t)size);
    }
    copy_free(copy,(size_t)size);return status;
}
SatchelGuestPools *satchel_guest_pools_create(SatchelGuestPoolDeviceValidator validate,void *context){
    return satchel_guest_pools_create_protected(validate,NULL,context);
}
SatchelGuestPools *satchel_guest_pools_create_protected(SatchelGuestPoolDeviceValidator validate,SatchelGuestPoolRangeGuard guard,void *context){
    if(!validate)return NULL;SatchelGuestPools *owner=calloc(1,sizeof(*owner));if(!owner)return NULL;
    owner->validate=validate;owner->guard=guard;owner->context=context;atomic_flag_clear(&owner->active);return owner;
}
SatchelGuestPools *satchel_guest_pools_create_checked(SatchelGuestPoolDeviceValidator validate,SatchelGuestPoolCheckedRangeGuard guard,void *context){
    SatchelGuestPools *owner=satchel_guest_pools_create_protected(validate,NULL,context);if(owner)owner->checked_guard=guard;return owner;
}
SatchelGuestPoolStatus satchel_guest_pools_device_children(SatchelGuestPools *owner,uint64_t guest_device,uint32_t *count){
    if(!count)return SATCHEL_GUEST_POOL_INVALID;*count=0;if(!owner)return SATCHEL_GUEST_POOL_INVALID;
    if(atomic_flag_test_and_set(&owner->active))return SATCHEL_GUEST_POOL_BUSY;
    for(unsigned i=0;i<SATCHEL_GUEST_POOL_LIMIT;i++)if(owner->pools[i].state && owner->pools[i].guest_device==guest_device)++*count;
    atomic_flag_clear(&owner->active);return SATCHEL_GUEST_POOL_OK;
}
static SatchelGuestPoolStatus check_unowned_range(SatchelGuestPools *owner,SatchelCPUProbe *cpu,uint64_t address,uint64_t size){
    if(!owner || !size || address>UINT64_MAX-size)return SATCHEL_GUEST_POOL_INVALID;
    if(atomic_flag_test_and_set(&owner->active))return SATCHEL_GUEST_POOL_BUSY;
    SatchelGuestPoolStatus status=owner->retiring?SATCHEL_GUEST_POOL_INVALID:SATCHEL_GUEST_POOL_OK;
    for(unsigned i=0;i<SATCHEL_GUEST_POOL_LIMIT && status==SATCHEL_GUEST_POOL_OK;i++)if(owner->pools[i].state){
        const Pool *p=&owner->pools[i];if(overlap(cpu,address,size,p->object,256) || overlap(cpu,address,size,p->storage,p->size))status=SATCHEL_GUEST_POOL_BAD_OBJECT;
    }
    atomic_flag_clear(&owner->active);return status;
}
SatchelGuestPoolStatus satchel_guest_pools_check_unowned_range(SatchelGuestPools *owner,uint64_t address,uint64_t size){return check_unowned_range(owner,NULL,address,size);}
SatchelGuestPoolStatus satchel_guest_pools_check_unowned_range_checked(SatchelGuestPools *owner,SatchelCPUProbe *cpu,uint64_t address,uint64_t size){if(!cpu)return SATCHEL_GUEST_POOL_INVALID;return check_unowned_range(owner,cpu,address,size);}
SatchelGuestPoolStatus satchel_guest_pools_try_destroy(SatchelGuestPools **pointer){
    if(!pointer)return SATCHEL_GUEST_POOL_INVALID;if(!*pointer)return SATCHEL_GUEST_POOL_OK;
    SatchelGuestPools *owner=*pointer;if(atomic_flag_test_and_set(&owner->active))return SATCHEL_GUEST_POOL_BUSY;owner->retiring=1;
    for(unsigned i=0;i<SATCHEL_GUEST_POOL_LEASE_LIMIT;i++)if(owner->leases[i].token){atomic_flag_clear(&owner->active);return SATCHEL_GUEST_POOL_BUSY;}
    SatchelGuestPoolStatus status=SATCHEL_GUEST_POOL_OK;
    for(unsigned i=0;i<SATCHEL_GUEST_POOL_LIMIT;i++)if(owner->pools[i].state){
        Pool *pool=&owner->pools[i];status=backend(satchel_vulkan_buffer_destroy(pool->device,pool->buffer));
        if(status!=SATCHEL_GUEST_POOL_OK)break;memset(pool,0,sizeof(*pool));
    }
    if(status==SATCHEL_GUEST_POOL_OK){*pointer=NULL;free(owner);}else atomic_flag_clear(&owner->active);
    return status;
}
SatchelGuestPoolStatus satchel_guest_pools_execute(SatchelGuestPools *owner,SatchelCPUProbe *cpu,const SatchelGuestPoolRequest *request,SatchelGuestPoolResult *result){
    if(!result)return SATCHEL_GUEST_POOL_INVALID;*result=(SatchelGuestPoolResult){0};result->status=SATCHEL_GUEST_POOL_INVALID;
    if(!owner || !cpu || !request)return SATCHEL_GUEST_POOL_INVALID;
    if(atomic_flag_test_and_set(&owner->active)){result->status=SATCHEL_GUEST_POOL_BUSY;return SATCHEL_GUEST_POOL_BUSY;}
    const SatchelGuestPoolRequest copied=*request;uint64_t value=0;
    const SatchelGuestPoolStatus status=execute(owner,cpu,&copied,&value);result->status=status;if(status==SATCHEL_GUEST_POOL_OK)result->value=value;
    atomic_flag_clear(&owner->active);return status;
}
SatchelGuestPoolStatus satchel_guest_pools_resolve(SatchelGuestPools *owner,SatchelCPUProbe *cpu,uint64_t guest_device,uint64_t gpu,uint64_t size,SatchelGuestPoolBinding *binding){
    if(!binding)return SATCHEL_GUEST_POOL_INVALID;*binding=(SatchelGuestPoolBinding){0};
    if(!owner || !cpu || !size || gpu>UINT64_MAX-size)return SATCHEL_GUEST_POOL_INVALID;
    if(atomic_flag_test_and_set(&owner->active))return SATCHEL_GUEST_POOL_BUSY;
    SatchelGuestPoolStatus status=SATCHEL_GUEST_POOL_BAD_OBJECT;
    if(owner->retiring)status=SATCHEL_GUEST_POOL_INVALID;
    else for(unsigned i=0;i<SATCHEL_GUEST_POOL_LIMIT;i++){
        Pool *pool=&owner->pools[i];if(pool->state!=1 || pool->guest_device!=guest_device || gpu<pool->gpu || gpu-pool->gpu>pool->size || size>pool->size-(gpu-pool->gpu))continue;
        status=validate_pool(owner,cpu,pool);if(status==SATCHEL_GUEST_POOL_OK && pool->gpu_held)status=SATCHEL_GUEST_POOL_BUSY;if(status==SATCHEL_GUEST_POOL_OK)*binding=(SatchelGuestPoolBinding){pool->buffer,gpu-pool->gpu,size,pool->generation};break;
    }
    atomic_flag_clear(&owner->active);return status;
}
static SatchelGuestPoolView pool_view(const Pool *pool){return (SatchelGuestPoolView){pool->object,pool->guest_device,pool->storage,pool->gpu,pool->size,pool->generation};}
static Pool *leased_pool(SatchelGuestPools *owner,uint64_t token,Lease **lease){
    if(!token)return NULL;
    for(unsigned i=0;i<SATCHEL_GUEST_POOL_LEASE_LIMIT;i++)if(owner->leases[i].token==token){
        if(lease)*lease=&owner->leases[i];for(unsigned j=0;j<SATCHEL_GUEST_POOL_LIMIT;j++)if(owner->pools[j].state==1 && owner->pools[j].generation==owner->leases[i].generation)return &owner->pools[j];break;
    }
    return NULL;
}
static SatchelGuestPoolStatus retain_pool(SatchelGuestPools *owner,SatchelCPUProbe *cpu,Pool *pool,uint64_t *token,SatchelGuestPoolView *view){
    SatchelGuestPoolStatus status=validate_pool(owner,cpu,pool);if(status!=SATCHEL_GUEST_POOL_OK)return status;if(pool->gpu_held)return SATCHEL_GUEST_POOL_BUSY;
    Lease *slot=NULL;for(unsigned i=0;i<SATCHEL_GUEST_POOL_LEASE_LIMIT;i++)if(!owner->leases[i].token){slot=&owner->leases[i];break;}
    if(!slot || pool->references==UINT32_MAX)return SATCHEL_GUEST_POOL_LIMIT_REACHED;
    uint64_t id=0;if(!take(&next_lease,1,&id))return SATCHEL_GUEST_POOL_LIMIT_REACHED;
    *slot=(Lease){id,pool->generation,0};++pool->references;*token=id;*view=pool_view(pool);return SATCHEL_GUEST_POOL_OK;
}
SatchelGuestPoolStatus satchel_guest_pools_retain_object(SatchelGuestPools *owner,SatchelCPUProbe *cpu,uint64_t device,uint64_t object,uint64_t *token,SatchelGuestPoolView *view){
    if(token)*token=0;if(view)*view=(SatchelGuestPoolView){0};if(!owner || !cpu || !token || !view)return SATCHEL_GUEST_POOL_INVALID;
    if(atomic_flag_test_and_set(&owner->active))return SATCHEL_GUEST_POOL_BUSY;
    Pool *pool=find(owner,object);SatchelGuestPoolStatus status=owner->retiring?SATCHEL_GUEST_POOL_INVALID:(!pool || pool->guest_device!=device?SATCHEL_GUEST_POOL_BAD_OBJECT:retain_pool(owner,cpu,pool,token,view));
    atomic_flag_clear(&owner->active);return status;
}
SatchelGuestPoolStatus satchel_guest_pools_retain_range(SatchelGuestPools *owner,SatchelCPUProbe *cpu,uint64_t device,uint64_t gpu,uint64_t size,uint64_t *token,SatchelGuestPoolView *view){
    if(token)*token=0;if(view)*view=(SatchelGuestPoolView){0};if(!owner || !cpu || !token || !view || !size || gpu>UINT64_MAX-size)return SATCHEL_GUEST_POOL_INVALID;
    if(atomic_flag_test_and_set(&owner->active))return SATCHEL_GUEST_POOL_BUSY;SatchelGuestPoolStatus status=owner->retiring?SATCHEL_GUEST_POOL_INVALID:SATCHEL_GUEST_POOL_BAD_OBJECT;
    if(!owner->retiring)for(unsigned i=0;i<SATCHEL_GUEST_POOL_LIMIT;i++){Pool *pool=&owner->pools[i];if(pool->state==1 && pool->guest_device==device && gpu>=pool->gpu && gpu-pool->gpu<pool->size && size<=pool->size-(gpu-pool->gpu)){status=retain_pool(owner,cpu,pool,token,view);break;}}
    atomic_flag_clear(&owner->active);return status;
}
SatchelGuestPoolStatus satchel_guest_pools_lease_view(SatchelGuestPools *owner,SatchelCPUProbe *cpu,uint64_t token,SatchelGuestPoolView *view){
    if(view)*view=(SatchelGuestPoolView){0};if(!owner || !cpu || !view)return SATCHEL_GUEST_POOL_INVALID;
    if(atomic_flag_test_and_set(&owner->active))return SATCHEL_GUEST_POOL_BUSY;
    Pool *pool=leased_pool(owner,token,NULL);SatchelGuestPoolStatus status=owner->retiring?SATCHEL_GUEST_POOL_INVALID:validate_pool(owner,cpu,pool);
    if(status==SATCHEL_GUEST_POOL_OK)*view=pool_view(pool);atomic_flag_clear(&owner->active);return status;
}
SatchelGuestPoolStatus satchel_guest_pools_lease_binding(SatchelGuestPools *owner,SatchelCPUProbe *cpu,uint64_t token,uint64_t gpu,uint64_t size,SatchelGuestPoolBinding *binding){
    if(binding)*binding=(SatchelGuestPoolBinding){0};if(!owner || !cpu || !binding || !size || gpu>UINT64_MAX-size)return SATCHEL_GUEST_POOL_INVALID;
    if(atomic_flag_test_and_set(&owner->active))return SATCHEL_GUEST_POOL_BUSY;
    Lease *lease=NULL;Pool *pool=leased_pool(owner,token,&lease);SatchelGuestPoolStatus status=owner->retiring?SATCHEL_GUEST_POOL_INVALID:validate_pool(owner,cpu,pool);
    if(status==SATCHEL_GUEST_POOL_OK && pool->gpu_held && !lease->gpu_held)status=SATCHEL_GUEST_POOL_BUSY;
    if(status==SATCHEL_GUEST_POOL_OK){if(gpu<pool->gpu || gpu-pool->gpu>=pool->size || size>pool->size-(gpu-pool->gpu))status=SATCHEL_GUEST_POOL_INVALID;
        else *binding=(SatchelGuestPoolBinding){pool->buffer,gpu-pool->gpu,size,pool->generation};}
    atomic_flag_clear(&owner->active);return status;
}
SatchelGuestPoolStatus satchel_guest_pools_release(SatchelGuestPools *owner,uint64_t token){
    if(!owner || !token)return SATCHEL_GUEST_POOL_INVALID;if(atomic_flag_test_and_set(&owner->active))return SATCHEL_GUEST_POOL_BUSY;
    Lease *lease=NULL;Pool *pool=leased_pool(owner,token,&lease);SatchelGuestPoolStatus status=SATCHEL_GUEST_POOL_BAD_OBJECT;
    if(pool && lease && lease->gpu_held)status=SATCHEL_GUEST_POOL_BUSY;
    else if(pool && lease && pool->references){--pool->references;memset(lease,0,sizeof(*lease));status=SATCHEL_GUEST_POOL_OK;}
    atomic_flag_clear(&owner->active);return status;
}

SatchelGuestPoolStatus satchel_guest_pools_budget(SatchelGuestPools *owner,SatchelGuestPoolBudget *out){
 if(!owner || !out)return SATCHEL_GUEST_POOL_INVALID;if(atomic_flag_test_and_set(&owner->active))return SATCHEL_GUEST_POOL_BUSY;
 SatchelGuestPoolBudget answer={0};answer.transient_bytes=atomic_load(&transient_bytes);answer.transient_limit=TRANSIENT_LIMIT;for(unsigned i=0;i<SATCHEL_GUEST_POOL_LIMIT;i++){const Pool *p=&owner->pools[i];if(p->state){answer.live_pools+=p->state==1;answer.provisional_pools+=p->state!=1;answer.logical_bytes+=p->size;answer.backing_bytes+=p->allocation_size;}}
 SatchelVulkanBufferStats stats={0};SatchelGuestPoolStatus status=backend(satchel_vulkan_buffer_stats(&stats));if(status==SATCHEL_GUEST_POOL_OK){answer.global_backing_bytes=stats.allocated_bytes;answer.global_backing_limit=stats.allocation_limit;if(answer.backing_bytes>answer.global_backing_bytes || answer.global_backing_limit!=SATCHEL_GUEST_POOL_BACKING_LIMIT)status=SATCHEL_GUEST_POOL_INTERNAL;else *out=answer;}
 atomic_flag_clear(&owner->active);return status;
}

SatchelGuestPoolStatus satchel_guest_pools_gpu_acquire(SatchelGuestPools *o,SatchelCPUProbe *cpu,uint64_t token){
 if(!o || !cpu || !token)return SATCHEL_GUEST_POOL_INVALID;if(atomic_flag_test_and_set(&o->active))return SATCHEL_GUEST_POOL_BUSY;
 Lease *lease=NULL;Pool *p=leased_pool(o,token,&lease);SatchelGuestPoolStatus s=o->retiring?SATCHEL_GUEST_POOL_INVALID:validate_pool(o,cpu,p);
 if(s==SATCHEL_GUEST_POOL_OK){if(p->gpu_held || lease->gpu_held)s=SATCHEL_GUEST_POOL_BUSY;else {s=backend(satchel_vulkan_buffer_read(p->device,p->buffer,0,NULL,0));if(s==SATCHEL_GUEST_POOL_OK)p->gpu_held=lease->gpu_held=1;}}
 atomic_flag_clear(&o->active);return s;
}
SatchelGuestPoolStatus satchel_guest_pools_gpu_release(SatchelGuestPools *o,uint64_t token){
 if(!o || !token)return SATCHEL_GUEST_POOL_INVALID;if(atomic_flag_test_and_set(&o->active))return SATCHEL_GUEST_POOL_BUSY;
 Lease *lease=NULL;Pool *p=leased_pool(o,token,&lease);SatchelGuestPoolStatus s=SATCHEL_GUEST_POOL_BAD_OBJECT;
 if(p && lease && p->gpu_held && lease->gpu_held){p->gpu_held=lease->gpu_held=0;s=SATCHEL_GUEST_POOL_OK;}
 atomic_flag_clear(&o->active);return s;
}
