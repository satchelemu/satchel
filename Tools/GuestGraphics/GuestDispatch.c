#include "WindowDispatchOperations.h"
#include "GuestTransfers.h"
#include "GuestDispatch.h"
#include "GuestPoolServices.h"
#include "GuestTransferServices.h"
#include "GuestRasterOperations.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
static const char *const names[]={
#include "NVNNames.inc"
};
enum { NAME_COUNT=sizeof(names)/sizeof(names[0]), STUB_FIRST=16, STUB_STRIDE=8,
       REGION_SIZE=(STUB_FIRST+NAME_COUNT*STUB_STRIDE+4095)&~4095 };
_Static_assert(NAME_COUNT<=8190,"NVN stub registry exceeds the64KiB mapping limit");
_Static_assert(NAME_COUNT<=UINT16_MAX,"MOVZ stub ID exceeds16bits");
_Static_assert(STUB_FIRST+(uint64_t)NAME_COUNT*STUB_STRIDE<(UINT64_C(1)<<27),"Stub branch exceeds signed26-bit displacement");
typedef struct {uint64_t address,guest_token,backend_token;uint32_t state;SatchelGuestDeviceCaps caps;} DeviceSlot;
struct SatchelGuestDispatch {uint64_t bootstrap,base,deadline,events,next_device;uint32_t limit,owner_slot,retiring;SatchelGuestDeviceBackend backend;
    SatchelGuestBackendStatus (*drain)(void *,uint64_t);DeviceSlot devices[SATCHEL_GUEST_DEVICE_REGISTRY_LIMIT];
    SatchelGuestPoolServices pool_services;void *pool_owner;
    SatchelGuestPoolStatus (*unowned_range_checked)(void *,SatchelCPUProbe *,uint64_t,uint64_t);
    uint32_t (*prepare_retirement)(void *);
    uint32_t (*execute_transfer)(void *,SatchelCPUProbe *,const SatchelGuestTransferRequest *,SatchelGuestTransferResult *,SatchelCPUProbeCancelled,void *);};
static SatchelGuestDispatch *retirement_owners[SATCHEL_GUEST_RETIREMENT_LIMIT];
static int retirement_poll_active,dispatch_handle_active;
// Installed Darwin pthread headers provide the static recursive initializer.
// Hold this process-wide owner lock across handlers/callbacks. Reentry is still
// rejected by the explicit guards, while another worker can safely wait to
// transfer ownership without racing a retiring context's free.
static pthread_mutex_t owner_lock=PTHREAD_RECURSIVE_MUTEX_INITIALIZER;
static int pool_device(void *,SatchelCPUProbe *,uint64_t,uint64_t *,uint64_t *);
static int pool_protected_range(void *context,uint64_t address,uint64_t size){
    SatchelGuestDispatch *d=context;if(!size || address>UINT64_MAX-size)return 0;
    for(unsigned i=0;i<SATCHEL_GUEST_DEVICE_REGISTRY_LIMIT;i++)if(d->devices[i].state &&
        address<d->devices[i].address+SATCHEL_GUEST_DISPATCH_DEVICE_SIZE && d->devices[i].address<address+size)return 0;
    return 1;
}
static int pool_protected_range_checked(void *context,SatchelCPUProbe *cpu,uint64_t address,uint64_t size){
    SatchelGuestDispatch *d=context;if(!cpu || !size || address>UINT64_MAX-size)return 0;
    for(unsigned i=0;i<SATCHEL_GUEST_DEVICE_REGISTRY_LIMIT;i++)if(d->devices[i].state){int overlap=1;
        if(!satchel_cpu_probe_physical_overlap(cpu,address,size,d->devices[i].address,SATCHEL_GUEST_DISPATCH_DEVICE_SIZE,&overlap) || overlap)return 0;}
    return 1;
}
static SatchelGuestPoolStatus pool_unowned(SatchelGuestDispatch *d,SatchelCPUProbe *cpu,uint64_t address,uint64_t size){
    return d->unowned_range_checked?d->unowned_range_checked(d->pool_owner,cpu,address,size):d->pool_services.unowned_range(d->pool_owner,address,size);
}
static SatchelGuestDispatchStatus pool_status(SatchelGuestPoolStatus status){
    switch(status){
    case SATCHEL_GUEST_POOL_OK:return SATCHEL_GUEST_DISPATCH_RETURNED;
    case SATCHEL_GUEST_POOL_MEMORY:return SATCHEL_GUEST_DISPATCH_MEMORY_FAULT;
    case SATCHEL_GUEST_POOL_BAD_OBJECT:return SATCHEL_GUEST_DISPATCH_BAD_OBJECT;
    case SATCHEL_GUEST_POOL_UNSUPPORTED:return SATCHEL_GUEST_DISPATCH_UNSUPPORTED_POOL_PROFILE;
    case SATCHEL_GUEST_POOL_LIMIT_REACHED:return SATCHEL_GUEST_DISPATCH_RESOURCE_LIMIT;
    case SATCHEL_GUEST_POOL_BUSY:return SATCHEL_GUEST_DISPATCH_RESOURCE_BUSY;
    case SATCHEL_GUEST_POOL_BACKEND:case SATCHEL_GUEST_POOL_ALLOCATION:return SATCHEL_GUEST_DISPATCH_BACKEND_FAILURE;
    case SATCHEL_GUEST_POOL_INTERNAL:return SATCHEL_GUEST_DISPATCH_INTERNAL;
    default:return SATCHEL_GUEST_DISPATCH_INVALID;
    }
}

static uint64_t now(void) {struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return UINT64_MAX;return (uint64_t)t.tv_sec*1000000000+(uint64_t)t.tv_nsec;}
static void put32(uint8_t *p,uint32_t value) {for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(value>>(8*i));}
static uint32_t get32(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int satchel_guest_dispatch_build_stubs(void *output,size_t capacity,SatchelGuestDispatchLayout *layout) {
    if (!layout || (!output && capacity) || (output && capacity<REGION_SIZE)) return 0;
    *layout=(SatchelGuestDispatchLayout){REGION_SIZE,0,STUB_FIRST,STUB_STRIDE,NAME_COUNT};
    if (!output) return 1;
    uint8_t *bytes=output;
    for(size_t offset=0;offset<REGION_SIZE;offset+=4)put32(bytes+offset,0xd4200000); /* BRK #0 fail-closed guards */
    for(uint32_t i=0;i<NAME_COUNT;i++) {
        const uint32_t offset=STUB_FIRST+i*STUB_STRIDE;
        put32(bytes+offset,0xd2800010|((i+1)<<5)); /* original MOVZ X16,#id */
        const int32_t displacement=-(int32_t)(offset+4)/4;
        put32(bytes+offset+4,0x14000000|((uint32_t)displacement&0x03ffffff));
    }
    return 1;
}
SatchelGuestDispatch *satchel_guest_dispatch_create(uint64_t bootstrap,uint64_t base,uint32_t events,uint32_t timeout_ms) {
    if (bootstrap<4096 || (bootstrap&3) || base<4096 || (base&4095) || base>UINT64_MAX-REGION_SIZE ||
        (bootstrap>=base && bootstrap<base+REGION_SIZE) || !events || events>SATCHEL_GUEST_DISPATCH_EVENT_LIMIT ||
        !timeout_ms || timeout_ms>SATCHEL_GUEST_DISPATCH_TIMEOUT_LIMIT_MS) return NULL;
    const uint64_t start=now(), duration=(uint64_t)timeout_ms*1000000;
    if(start>UINT64_MAX-duration)return NULL;
    SatchelGuestDispatch *d=calloc(1,sizeof(*d));if(!d)return NULL;
    d->bootstrap=bootstrap;d->base=base;d->deadline=start+duration;d->limit=events;d->next_device=1;d->owner_slot=SATCHEL_GUEST_RETIREMENT_LIMIT;return d;
}
static int satchel_guest_dispatch_retire_locked(SatchelGuestDispatch **owner) {
    if(!owner || !*owner || retirement_poll_active || dispatch_handle_active)return 0;
    SatchelGuestDispatch *d=*owner;if(d->retiring)return 0;
    if(d->owner_slot==SATCHEL_GUEST_RETIREMENT_LIMIT){if(d->backend.version)return 0;*owner=NULL;free(d);return 1;}
    if(d->owner_slot>=SATCHEL_GUEST_RETIREMENT_LIMIT || retirement_owners[d->owner_slot]!=d)return 0;
    d->retiring=1;*owner=NULL;return 1;
}
static int satchel_guest_dispatch_retirement_stats_locked(SatchelGuestRetirementReport *report) {
    if(!report)return 0;*report=(SatchelGuestRetirementReport){0};
    for(unsigned i=0;i<SATCHEL_GUEST_RETIREMENT_LIMIT;i++)if(retirement_owners[i]){
        const SatchelGuestDispatch *d=retirement_owners[i];++report->reserved_contexts;
        if(!d->retiring){++report->active_contexts;continue;}++report->retiring_contexts;
        for(unsigned j=0;j<SATCHEL_GUEST_DEVICE_REGISTRY_LIMIT;j++)report->pending_devices+=d->devices[j].state!=0;
    }
    return 1;
}
static SatchelGuestRetirementStatus satchel_guest_dispatch_retirement_poll_locked(SatchelCPUProbeCancelled cancelled,void *context,SatchelGuestRetirementReport *report) {
    if(!report || retirement_poll_active || dispatch_handle_active)return SATCHEL_GUEST_RETIRE_INVALID;
    *report=(SatchelGuestRetirementReport){0};retirement_poll_active=1;
    uint32_t released_contexts=0,released_devices=0,failures=0,last_status=0;int stopped=0;
    for(unsigned i=0;i<SATCHEL_GUEST_RETIREMENT_LIMIT && !stopped;i++){
        SatchelGuestDispatch *d=retirement_owners[i];if(!d || !d->retiring)continue;
        if(d->pool_owner){
            if(d->prepare_retirement){
                if(cancelled && cancelled(context)){stopped=1;break;}
                const uint32_t prepared=d->prepare_retirement(d->pool_owner);
                if(prepared!=SATCHEL_GUEST_POOL_OK){++failures;last_status=prepared==SATCHEL_GUEST_POOL_BUSY?SATCHEL_GUEST_BACKEND_LIMIT:SATCHEL_GUEST_BACKEND_FAILURE;continue;}
            }
            /* Drain completion before pool backing, then parent devices. The
             * pool owner borrows no CPU and retains every failed/BUSY release. */
            for(unsigned j=0;j<SATCHEL_GUEST_DEVICE_REGISTRY_LIMIT;j++)if(d->devices[j].state){
                if(cancelled && cancelled(context)){stopped=1;break;}
                SatchelGuestBackendStatus status=d->drain(d->backend.context,d->devices[j].backend_token);
                if(status!=SATCHEL_GUEST_BACKEND_OK){++failures;last_status=status;}
            }
            if(stopped)break;
            const SatchelGuestPoolStatus released=d->pool_services.destroy(&d->pool_owner);
            if(released!=SATCHEL_GUEST_POOL_OK){++failures;last_status=released==SATCHEL_GUEST_POOL_BUSY?SATCHEL_GUEST_BACKEND_LIMIT:SATCHEL_GUEST_BACKEND_FAILURE;continue;}
            if(d->pool_owner){++failures;last_status=SATCHEL_GUEST_BACKEND_FAILURE;continue;}
        }
        for(unsigned j=0;j<SATCHEL_GUEST_DEVICE_REGISTRY_LIMIT;j++){
            if(cancelled && cancelled(context)){stopped=1;break;}
            DeviceSlot *slot=&d->devices[j];if(!slot->state)continue;
            SatchelGuestBackendStatus status=d->drain?d->drain(d->backend.context,slot->backend_token):SATCHEL_GUEST_BACKEND_OK;
            if(status==SATCHEL_GUEST_BACKEND_OK)status=d->backend.destroy(d->backend.context,slot->backend_token);
            if(status==SATCHEL_GUEST_BACKEND_OK){memset(slot,0,sizeof(*slot));++released_devices;}
            else {++failures;last_status=status;}
        }
        if(stopped)break;
        unsigned owned=0;for(unsigned j=0;j<SATCHEL_GUEST_DEVICE_REGISTRY_LIMIT;j++)owned+=d->devices[j].state!=0;
        if(!owned && !d->pool_owner){retirement_owners[i]=NULL;free(d);++released_contexts;}
    }
    retirement_poll_active=0;satchel_guest_dispatch_retirement_stats(report);
    report->released_contexts=released_contexts;report->released_devices=released_devices;report->backend_failures=failures;report->last_backend_status=last_status;
    return stopped?SATCHEL_GUEST_RETIRE_CANCELLED:(report->retiring_contexts?SATCHEL_GUEST_RETIRE_PENDING:SATCHEL_GUEST_RETIRE_IDLE);
}
static void satchel_guest_dispatch_destroy_locked(SatchelGuestDispatch *d) {
    if(!d)return;
    if(satchel_guest_dispatch_retire(&d)){SatchelGuestRetirementReport report;satchel_guest_dispatch_retirement_poll(NULL,NULL,&report);}
}
static int attach_backend(SatchelGuestDispatch *d,const SatchelGuestDeviceBackend *backend,SatchelGuestBackendStatus (*drain)(void *,uint64_t)) {
    if(!d || !backend || d->retiring || retirement_poll_active || dispatch_handle_active || d->events || d->backend.version || backend->supported_flags ||
        !backend->create || !backend->destroy || !backend->capabilities)return 0;
    unsigned index=0;for(;index<SATCHEL_GUEST_RETIREMENT_LIMIT;index++)if(!retirement_owners[index])break;
    if(index==SATCHEL_GUEST_RETIREMENT_LIMIT)return 0;
    d->backend=*backend;d->drain=drain;d->owner_slot=index;retirement_owners[index]=d;return 1;
}
static int satchel_guest_dispatch_set_device_backend_locked(SatchelGuestDispatch *d,const SatchelGuestDeviceBackend *backend) {
    return backend && backend->version==1 && attach_backend(d,backend,NULL);
}
static int satchel_guest_dispatch_set_device_backend_v2_locked(SatchelGuestDispatch *d,const SatchelGuestDeviceBackendV2 *backend) {
    return backend && backend->device.version==2 && backend->drain && attach_backend(d,&backend->device,backend->drain);
}
static uint32_t satchel_guest_dispatch_device_count_locked(const SatchelGuestDispatch *d) {
    uint32_t count=0;if(d)for(unsigned i=0;i<SATCHEL_GUEST_DEVICE_REGISTRY_LIMIT;i++)count+=d->devices[i].state==1;return count;
}
static int satchel_guest_dispatch_device_capabilities_locked(const SatchelGuestDispatch *d,uint64_t address,SatchelGuestDeviceCaps *caps) {
    if(!caps)return 0;memset(caps,0,sizeof(*caps));if(!d)return 0;
    for(unsigned i=0;i<SATCHEL_GUEST_DEVICE_REGISTRY_LIMIT;i++)if(d->devices[i].state==1 && d->devices[i].address==address){*caps=d->devices[i].caps;return 1;}
    return 0;
}
static int satchel_guest_dispatch_breakpoints_locked(const SatchelGuestDispatch *d,uint64_t addresses[2]) {
    if(!d || !addresses)return 0;addresses[0]=d->bootstrap;addresses[1]=d->base;return 1;
}
const char *satchel_guest_dispatch_name(uint32_t id) {return id==0 ? "nvnBootstrapLoader" : (id<=NAME_COUNT ? names[id-1] : NULL);}
static SatchelGuestDispatchStatus lookup(SatchelCPUProbe *cpu,uint64_t address,uint32_t *id) {
    char name[SATCHEL_GUEST_DISPATCH_NAME_LIMIT+1];
    if(!address)return SATCHEL_GUEST_DISPATCH_MEMORY_FAULT;
    for(size_t i=0;i<sizeof(name);i++) {
        if(address>UINT64_MAX-i || !satchel_cpu_probe_read(cpu,address+i,name+i,1))return SATCHEL_GUEST_DISPATCH_MEMORY_FAULT;
        if(!name[i]) {
            for(uint32_t j=0;j<NAME_COUNT;j++)if(!strcmp(name,names[j])) {*id=j+1;return SATCHEL_GUEST_DISPATCH_RETURNED;}
            return SATCHEL_GUEST_DISPATCH_UNKNOWN_NAME;
        }
    }
    return SATCHEL_GUEST_DISPATCH_UNKNOWN_NAME;
}
static void put64(uint8_t *p,uint64_t value){put32(p,(uint32_t)value);put32(p+4,(uint32_t)(value>>32));}
static uint64_t get64(const uint8_t *p){return get32(p)|(uint64_t)get32(p+4)<<32;}
static int pool_device(void *context,SatchelCPUProbe *cpu,uint64_t address,uint64_t *backend_token,uint64_t *generation){
    SatchelGuestDispatch *d=context;if(!d || d->retiring || !cpu || !backend_token || !generation)return 0;
    const DeviceSlot *slot=NULL;for(unsigned i=0;i<SATCHEL_GUEST_DEVICE_REGISTRY_LIMIT;i++)if(d->devices[i].state==1 && d->devices[i].address==address){slot=&d->devices[i];break;}
    uint8_t tag[16];SatchelGuestDeviceCaps caps={0};
    if(!slot || !satchel_cpu_probe_check_range(cpu,address,SATCHEL_GUEST_DISPATCH_DEVICE_SIZE,SATCHEL_CPU_READ) || !satchel_cpu_probe_read(cpu,address,tag,sizeof(tag)) ||
       get32(tag)!=0x504c4456 || get32(tag+4)!=1 || get64(tag+8)!=slot->guest_token ||
       d->backend.capabilities(d->backend.context,slot->backend_token,&caps)!=SATCHEL_GUEST_BACKEND_OK || caps.version!=1)return 0;
    *backend_token=slot->backend_token;*generation=slot->guest_token;return 1;
}
static SatchelGuestDispatchStatus restore_pc(SatchelCPUProbe *cpu,const SatchelCPUProbeResult *stop,SatchelGuestDispatchStatus status){return satchel_cpu_probe_set_pc(cpu,stop->pc)?status:SATCHEL_GUEST_DISPATCH_INTERNAL;}
static int release_device(SatchelGuestDispatch *d,DeviceSlot *slot){
    if(d->backend.destroy(d->backend.context,slot->backend_token)!=SATCHEL_GUEST_BACKEND_OK)return 0;
    memset(slot,0,sizeof(*slot));return 1;
}
static SatchelGuestDispatchStatus device_initialize(SatchelGuestDispatch *d,SatchelCPUProbe *cpu,const SatchelCPUProbeResult *stop,
    uint64_t address,uint64_t builder,uint64_t lr,SatchelCPUProbeCancelled cancelled,void *context) {
    if(!d->backend.version)return SATCHEL_GUEST_DISPATCH_BACKEND_UNAVAILABLE;
    if(address>UINT64_MAX-SATCHEL_GUEST_DISPATCH_DEVICE_SIZE)return SATCHEL_GUEST_DISPATCH_MEMORY_FAULT;
    if(d->unowned_range_checked){
        if(!satchel_cpu_probe_check_range(cpu,address,SATCHEL_GUEST_DISPATCH_DEVICE_SIZE,SATCHEL_CPU_READ|SATCHEL_CPU_WRITE))return SATCHEL_GUEST_DISPATCH_MEMORY_FAULT;
        if(!pool_protected_range_checked(d,cpu,address,SATCHEL_GUEST_DISPATCH_DEVICE_SIZE))return SATCHEL_GUEST_DISPATCH_BAD_OBJECT;
    }
    if(d->pool_owner){SatchelGuestDispatchStatus status=pool_status(pool_unowned(d,cpu,address,SATCHEL_GUEST_DISPATCH_DEVICE_SIZE));if(status!=SATCHEL_GUEST_DISPATCH_RETURNED)return status;}
    DeviceSlot *slot=NULL;
    for(unsigned i=0;i<SATCHEL_GUEST_DEVICE_REGISTRY_LIMIT;i++){
        if(d->devices[i].state && address<d->devices[i].address+SATCHEL_GUEST_DISPATCH_DEVICE_SIZE && d->devices[i].address<address+SATCHEL_GUEST_DISPATCH_DEVICE_SIZE)return SATCHEL_GUEST_DISPATCH_BAD_OBJECT;
        if(!d->devices[i].state && !slot)slot=&d->devices[i];
    }
    if(!slot || d->next_device==UINT64_MAX)return SATCHEL_GUEST_DISPATCH_DEVICE_LIMIT;
    uint8_t build[SATCHEL_GUEST_DISPATCH_BUILDER_SIZE],old[SATCHEL_GUEST_DISPATCH_DEVICE_SIZE],object[SATCHEL_GUEST_DISPATCH_DEVICE_SIZE]={0};
    if(!satchel_cpu_probe_read(cpu,builder,build,sizeof(build)) || !satchel_cpu_probe_read(cpu,address,old,sizeof(old)))return SATCHEL_GUEST_DISPATCH_MEMORY_FAULT;
    if(get32(build)!=0x504c4442 || get32(build+8)!=1)return SATCHEL_GUEST_DISPATCH_BAD_OBJECT;
    if(get32(build+4))return SATCHEL_GUEST_DISPATCH_UNSUPPORTED_FLAGS;
    if(address<builder+sizeof(build) && builder<address+sizeof(old))return SATCHEL_GUEST_DISPATCH_BAD_OBJECT;
    if(d->unowned_range_checked){int overlap=1;if(!satchel_cpu_probe_physical_overlap(cpu,address,sizeof(old),builder,sizeof(build),&overlap) || overlap)return SATCHEL_GUEST_DISPATCH_BAD_OBJECT;}
    if(cancelled && cancelled(context))return SATCHEL_GUEST_DISPATCH_CANCELLED;
    if(now()>=d->deadline)return SATCHEL_GUEST_DISPATCH_LIMIT;
    if(!satchel_cpu_probe_set_pc(cpu,lr))return SATCHEL_GUEST_DISPATCH_BAD_RETURN;
    uint64_t backend_token=0;SatchelGuestDeviceCaps caps={0};
    const SatchelGuestBackendStatus created=d->backend.create(d->backend.context,0,&backend_token,&caps);
    if(created!=SATCHEL_GUEST_BACKEND_OK)return restore_pc(cpu,stop,created==SATCHEL_GUEST_BACKEND_LIMIT?SATCHEL_GUEST_DISPATCH_DEVICE_LIMIT:SATCHEL_GUEST_DISPATCH_BACKEND_FAILURE);
    // Provisional ownership is retained until committed, or until destroy succeeds.
    slot->state=2;slot->backend_token=backend_token;slot->address=address;
    SatchelGuestDispatchStatus failed=SATCHEL_GUEST_DISPATCH_RETURNED;
    if(!backend_token || caps.version!=1)failed=SATCHEL_GUEST_DISPATCH_BACKEND_FAILURE;
    else if(cancelled && cancelled(context))failed=SATCHEL_GUEST_DISPATCH_CANCELLED;
    else if(now()>=d->deadline)failed=SATCHEL_GUEST_DISPATCH_LIMIT;
    put32(object,0x504c4456);put32(object+4,1);put64(object+8,d->next_device);
    if(failed==SATCHEL_GUEST_DISPATCH_RETURNED && !satchel_cpu_probe_write(cpu,address,object,sizeof(object)))failed=SATCHEL_GUEST_DISPATCH_MEMORY_FAULT;
    if(failed!=SATCHEL_GUEST_DISPATCH_RETURNED){if(!release_device(d,slot))failed=SATCHEL_GUEST_DISPATCH_INTERNAL;return restore_pc(cpu,stop,failed);}
    if(!satchel_cpu_probe_set_x(cpu,0,1)){
        int restored=satchel_cpu_probe_write(cpu,address,old,sizeof(old));int released=release_device(d,slot);
        (void)restored;(void)released;return restore_pc(cpu,stop,SATCHEL_GUEST_DISPATCH_INTERNAL);
    }
    slot->state=1;slot->guest_token=d->next_device++;slot->caps=caps;return SATCHEL_GUEST_DISPATCH_RETURNED;
}
static SatchelGuestDispatchStatus device_finalize(SatchelGuestDispatch *d,SatchelCPUProbe *cpu,const SatchelCPUProbeResult *stop,uint64_t address,uint64_t lr,SatchelCPUProbeCancelled cancelled,void *context) {
    if(!d->backend.version)return SATCHEL_GUEST_DISPATCH_BACKEND_UNAVAILABLE;
    DeviceSlot *slot=NULL;for(unsigned i=0;i<SATCHEL_GUEST_DEVICE_REGISTRY_LIMIT;i++)if(d->devices[i].state==1 && d->devices[i].address==address){slot=&d->devices[i];break;}
    if(!slot)return SATCHEL_GUEST_DISPATCH_BAD_OBJECT;
    uint8_t old[SATCHEL_GUEST_DISPATCH_DEVICE_SIZE],empty[SATCHEL_GUEST_DISPATCH_DEVICE_SIZE]={0};
    if(!satchel_cpu_probe_read(cpu,address,old,sizeof(old)))return SATCHEL_GUEST_DISPATCH_MEMORY_FAULT;
    if(get32(old)!=0x504c4456 || get32(old+4)!=1 || get64(old+8)!=slot->guest_token)return SATCHEL_GUEST_DISPATCH_BAD_OBJECT;
    if(d->pool_owner){uint32_t children=0;SatchelGuestDispatchStatus status=pool_status(d->pool_services.children(d->pool_owner,address,&children));if(status!=SATCHEL_GUEST_DISPATCH_RETURNED)return status;if(children)return SATCHEL_GUEST_DISPATCH_RESOURCE_BUSY;}
    if(cancelled && cancelled(context))return SATCHEL_GUEST_DISPATCH_CANCELLED;
    if(now()>=d->deadline)return SATCHEL_GUEST_DISPATCH_LIMIT;
    if(!satchel_cpu_probe_set_pc(cpu,lr))return SATCHEL_GUEST_DISPATCH_BAD_RETURN;
    if(!satchel_cpu_probe_write(cpu,address,empty,sizeof(empty)))return restore_pc(cpu,stop,SATCHEL_GUEST_DISPATCH_MEMORY_FAULT);
    if(!release_device(d,slot)){
        if(!satchel_cpu_probe_write(cpu,address,old,sizeof(old)))return SATCHEL_GUEST_DISPATCH_INTERNAL;
        return restore_pc(cpu,stop,SATCHEL_GUEST_DISPATCH_BACKEND_FAILURE);
    }
    return SATCHEL_GUEST_DISPATCH_RETURNED;
}
/* Verified NVNdeviceInfo 0x1c..0x22 (pinned reconstructed nvn.h), mapped
 * to the corresponding real VkPhysicalDeviceLimits compute fields. No API
 * version, compiler version, resource layout or optional-feature claims. */
static SatchelGuestDispatchStatus device_get_integer(SatchelGuestDispatch *d,SatchelCPUProbe *cpu,const SatchelCPUProbeResult *stop,
    uint64_t address,uint32_t selector,uint64_t output,uint64_t lr,SatchelCPUProbeCancelled cancelled,void *context) {
    if(selector<0x1c || selector>0x22)return SATCHEL_GUEST_DISPATCH_UNSUPPORTED_QUERY;
    if(!d->backend.version)return SATCHEL_GUEST_DISPATCH_BACKEND_UNAVAILABLE;
    const DeviceSlot *slot=NULL;
    for(unsigned i=0;i<SATCHEL_GUEST_DEVICE_REGISTRY_LIMIT;i++)if(d->devices[i].state==1 && d->devices[i].address==address){slot=&d->devices[i];break;}
    if(!slot)return SATCHEL_GUEST_DISPATCH_BAD_OBJECT;
    uint8_t object[16],bytes[4];
    if(!satchel_cpu_probe_read(cpu,address,object,sizeof(object)))return SATCHEL_GUEST_DISPATCH_MEMORY_FAULT;
    if(get32(object)!=0x504c4456 || get32(object+4)!=1 || get64(object+8)!=slot->guest_token)return SATCHEL_GUEST_DISPATCH_BAD_OBJECT;
    if(output>UINT64_MAX-sizeof(bytes))return SATCHEL_GUEST_DISPATCH_MEMORY_FAULT;
    if(d->unowned_range_checked){
        if(!satchel_cpu_probe_check_range(cpu,output,sizeof(bytes),SATCHEL_CPU_WRITE))return SATCHEL_GUEST_DISPATCH_MEMORY_FAULT;
        if(!pool_protected_range_checked(d,cpu,output,sizeof(bytes)))return SATCHEL_GUEST_DISPATCH_BAD_OBJECT;
    }
    if(d->pool_owner){SatchelGuestDispatchStatus status=pool_status(pool_unowned(d,cpu,output,sizeof(bytes)));if(status!=SATCHEL_GUEST_DISPATCH_RETURNED)return status;}
    // An output cannot overwrite any live private device representation.
    for(unsigned i=0;i<SATCHEL_GUEST_DEVICE_REGISTRY_LIMIT;i++)if(d->devices[i].state && output<d->devices[i].address+SATCHEL_GUEST_DISPATCH_DEVICE_SIZE &&
        d->devices[i].address<output+sizeof(bytes))return SATCHEL_GUEST_DISPATCH_BAD_OBJECT;
    const uint32_t value=selector<0x1f ? slot->caps.max_compute_group_size[selector-0x1c] :
        (selector==0x1f ? slot->caps.max_compute_invocations : slot->caps.max_compute_group_count[selector-0x20]);
    if(!value || value>INT32_MAX)return SATCHEL_GUEST_DISPATCH_BACKEND_FAILURE;
    put32(bytes,value);
    if(cancelled && cancelled(context))return SATCHEL_GUEST_DISPATCH_CANCELLED;
    if(now()>=d->deadline)return SATCHEL_GUEST_DISPATCH_LIMIT;
    if(!satchel_cpu_probe_set_pc(cpu,lr))return SATCHEL_GUEST_DISPATCH_BAD_RETURN;
    if(!satchel_cpu_probe_write(cpu,output,bytes,sizeof(bytes)))return restore_pc(cpu,stop,SATCHEL_GUEST_DISPATCH_MEMORY_FAULT);
    return SATCHEL_GUEST_DISPATCH_RETURNED; /* Void ABI: X0 and all other registers preserved. */
}
static uint32_t pool_operation(const char *name){
    static const char *const operations[]={"nvnMemoryPoolBuilderSetDefaults","nvnMemoryPoolBuilderSetDevice","nvnMemoryPoolBuilderSetStorage",
        "nvnMemoryPoolBuilderSetFlags","nvnMemoryPoolInitialize","nvnMemoryPoolFinalize","nvnMemoryPoolMap","nvnMemoryPoolGetBufferAddress",
        "nvnMemoryPoolFlushMappedRange","nvnMemoryPoolInvalidateMappedRange"};
    for(unsigned i=0;i<sizeof(operations)/sizeof(operations[0]);i++)if(!strcmp(name,operations[i]))return i+1;
    return 0;
}
static SatchelGuestDispatchStatus dispatch_pool(SatchelGuestDispatch *d,SatchelCPUProbe *cpu,const SatchelCPUProbeResult *stop,uint32_t operation,
    uint64_t object,uint64_t a,uint64_t lr,SatchelCPUProbeCancelled cancelled,void *context){
    if(!d->pool_owner)return SATCHEL_GUEST_DISPATCH_UNIMPLEMENTED;
    uint64_t b=0;if(!satchel_cpu_probe_get_x(cpu,2,&b))return SATCHEL_GUEST_DISPATCH_INTERNAL;
    if(cancelled && cancelled(context))return SATCHEL_GUEST_DISPATCH_CANCELLED;
    if(now()>=d->deadline)return SATCHEL_GUEST_DISPATCH_LIMIT;
    if(!satchel_cpu_probe_set_pc(cpu,lr))return SATCHEL_GUEST_DISPATCH_BAD_RETURN;
    const SatchelGuestPoolRequest request={operation,object,a,b};SatchelGuestPoolResult result={0};
    SatchelGuestDispatchStatus status=pool_status(d->pool_services.execute(d->pool_owner,cpu,&request,&result));
    if(status!=SATCHEL_GUEST_DISPATCH_RETURNED)return restore_pc(cpu,stop,status);
    if(operation==SATCHEL_GUEST_POOL_INITIALIZE || operation==SATCHEL_GUEST_POOL_MAP || operation==SATCHEL_GUEST_POOL_GPU_ADDRESS){
        if(!satchel_cpu_probe_set_x(cpu,0,result.value))return restore_pc(cpu,stop,SATCHEL_GUEST_DISPATCH_INTERNAL);
    }
    return SATCHEL_GUEST_DISPATCH_RETURNED;
}
static uint32_t transfer_operation(const char *name){
    static const char *const windows[]={"nvnWindowBuilderSetDefaults","nvnWindowBuilderSetDevice","nvnWindowBuilderSetNativeWindow","nvnWindowBuilderSetTextures","nvnWindowBuilderSetPresentInterval","nvnWindowBuilderGetNativeWindow","nvnWindowBuilderGetPresentInterval","nvnWindowInitialize","nvnWindowFinalize","nvnWindowGetNativeWindow","nvnWindowGetPresentInterval","nvnWindowSetPresentInterval","nvnWindowSetCrop","nvnWindowGetCrop","nvnQueueAcquireTexture","nvnQueuePresentTexture","nvnWindowAcquireTexture"};
    _Static_assert(sizeof(windows)/sizeof(windows[0])==SATCHEL_GWO_OPERATION_COUNT,"Window operation catalog mismatch");
    for(unsigned i=0;i<sizeof(windows)/sizeof(windows[0]);i++)if(!strcmp(name,windows[i]))return SATCHEL_GWO_BASE+i+1;
    static const char *const operations[]={"nvnQueueBuilderSetDefaults","nvnQueueBuilderSetDevice","nvnQueueBuilderSetFlags",
        "nvnQueueBuilderSetCommandMemorySize","nvnQueueBuilderSetComputeMemorySize","nvnQueueBuilderSetControlMemorySize",
        "nvnQueueBuilderGetQueueMemorySize","nvnQueueBuilderSetQueueMemory","nvnQueueInitialize","nvnQueueFinalize","nvnQueueSubmitCommands","nvnQueueFinish",
        "nvnCommandBufferInitialize","nvnCommandBufferFinalize","nvnCommandBufferAddCommandMemory","nvnCommandBufferAddControlMemory",
        "nvnCommandBufferBeginRecording","nvnCommandBufferEndRecording","nvnCommandBufferClearBuffer","nvnCommandBufferCopyBufferToBuffer","nvnDeviceFinalizeCommandHandle",
        "nvnCommandBufferGetCommandMemorySize","nvnCommandBufferGetCommandMemoryUsed","nvnCommandBufferGetCommandMemoryFree",
        "nvnCommandBufferGetControlMemorySize","nvnCommandBufferGetControlMemoryUsed","nvnCommandBufferGetControlMemoryFree","nvnCommandBufferIsRecording",
        "nvnProgramInitialize","nvnProgramFinalize","nvnProgramSetShaders","nvnCommandBufferBindProgram","nvnCommandBufferBindUniformBuffer","nvnCommandBufferBindStorageBuffer","nvnCommandBufferDispatchCompute","nvnQueueFlush","nvnCommandBufferSetRenderTargets","nvnCommandBufferClearColor","nvnCommandBufferDrawArrays","nvnCommandBufferBindVertexBuffer","nvnCommandBufferBindVertexAttribState","nvnCommandBufferBindVertexStreamState","nvnCommandBufferSetViewport","nvnCommandBufferSetScissor","nvnCommandBufferSetDepthRange","nvnCommandBufferBindColorState","nvnCommandBufferBindChannelMaskState","nvnCommandBufferBindPolygonState","nvnCommandBufferBindDepthStencilState","nvnCommandBufferBindMultisampleState","nvnCommandBufferSetDepthClamp","nvnCommandBufferSetRasterizerDiscard","nvnCommandBufferSetSampleMask","nvnCommandBufferSetRenderEnable","nvnCommandBufferSetViewportSwizzles"};
    _Static_assert(sizeof(operations)/sizeof(operations[0])==SATCHEL_GT_SWIZZLES,"Transfer operation catalog mismatch");
    for(unsigned i=0;i<sizeof(operations)/sizeof(operations[0]);i++)if(!strcmp(name,operations[i]))return i+1;
    static const char *const textures[]={"nvnTextureBuilderSetDefaults","nvnTextureBuilderSetDevice","nvnTextureBuilderSetFlags","nvnTextureBuilderSetTarget","nvnTextureBuilderSetSize2D","nvnTextureBuilderSetLevels","nvnTextureBuilderSetFormat","nvnTextureBuilderSetSamples","nvnTextureBuilderSetStride","nvnTextureBuilderSetStorage","nvnTextureBuilderGetStorageSize","nvnTextureBuilderGetStorageAlignment","nvnTextureInitialize","nvnTextureFinalize","nvnTextureGetWidth","nvnTextureGetHeight","nvnTextureGetMemoryPool","nvnTextureGetMemoryOffset","nvnTextureGetTextureAddress"};
    for(unsigned i=0;i<sizeof(textures)/sizeof(textures[0]);i++)if(!strcmp(name,textures[i]))return SATCHEL_GRO_TEXTURE_BASE+i+1;
    static const char *const states[]={"nvnColorStateSetDefaults","nvnColorStateSetBlendEnable","nvnColorStateSetLogicOp","nvnColorStateSetAlphaTest","nvnChannelMaskStateSetDefaults","nvnChannelMaskStateSetChannelMask","nvnPolygonStateSetDefaults","nvnPolygonStateSetCullFace","nvnPolygonStateSetFrontFace","nvnPolygonStateSetPolygonMode","nvnPolygonStateSetPolygonOffsetEnables","nvnDepthStencilStateSetDefaults","nvnDepthStencilStateSetDepthTestEnable","nvnDepthStencilStateSetDepthWriteEnable","nvnDepthStencilStateSetDepthFunc","nvnDepthStencilStateSetStencilTestEnable","nvnDepthStencilStateSetStencilFunc","nvnDepthStencilStateSetStencilOp","nvnMultisampleStateSetDefaults","nvnMultisampleStateSetMultisampleEnable","nvnMultisampleStateSetSamples","nvnMultisampleStateSetAlphaToCoverageEnable","nvnMultisampleStateSetAlphaToCoverageDither","nvnMultisampleStateSetRasterSamples","nvnMultisampleStateSetCoverageModulationMode","nvnMultisampleStateSetCoverageToColorEnable","nvnMultisampleStateSetCoverageToColorOutput","nvnMultisampleStateSetSampleLocationsEnable","nvnMultisampleStateSetSampleLocationsGridEnable","nvnVertexAttribStateSetDefaults","nvnVertexAttribStateSetFormat","nvnVertexAttribStateSetStreamIndex","nvnVertexStreamStateSetDefaults","nvnVertexStreamStateSetStride","nvnVertexStreamStateSetDivisor"};
    for(unsigned i=0;i<sizeof(states)/sizeof(states[0]);i++)if(!strcmp(name,states[i]))return SATCHEL_GRO_STATE_BASE+i+1;
    static const char *const modes[]={"nvnDeviceSetWindowOriginMode","nvnDeviceGetWindowOriginMode","nvnDeviceSetDepthMode","nvnDeviceGetDepthMode"};
    for(unsigned i=0;i<sizeof(modes)/sizeof(modes[0]);i++)if(!strcmp(name,modes[i]))return SATCHEL_GRO_ORIGIN_SET-1+i+1;
return 0;
}
typedef struct {SatchelGuestDispatch *dispatch;SatchelCPUProbeCancelled cancel;void *context;int expired;} TransferCancellation;
static int transfer_cancel(void *pointer){TransferCancellation *c=pointer;if(now()>=c->dispatch->deadline){c->expired=1;return 1;}return c->cancel && c->cancel(c->context);}
static SatchelGuestDispatchStatus dispatch_transfer(SatchelGuestDispatch *d,SatchelCPUProbe *cpu,const SatchelCPUProbeResult *stop,uint32_t operation,
    uint64_t lr,SatchelCPUProbeCancelled cancelled,void *context){
    if(!d->pool_owner || !d->execute_transfer)return SATCHEL_GUEST_DISPATCH_UNIMPLEMENTED;
    SatchelGuestTransferRequest request={.operation=operation};for(unsigned i=0;i<6;i++)if(!satchel_cpu_probe_get_x(cpu,i,&request.x[i]))return SATCHEL_GUEST_DISPATCH_INTERNAL;
    if(operation==SATCHEL_GT_DEPTH_RANGE){uint8_t q[16];for(unsigned i=0;i<2;i++){if(!satchel_cpu_probe_get_q(cpu,i,q))return SATCHEL_GUEST_DISPATCH_INTERNAL;request.f[i]=(uint32_t)q[0]|(uint32_t)q[1]<<8|(uint32_t)q[2]<<16|(uint32_t)q[3]<<24;}}
    if(cancelled && cancelled(context))return SATCHEL_GUEST_DISPATCH_CANCELLED;
    if(now()>=d->deadline)return SATCHEL_GUEST_DISPATCH_LIMIT;
    if(!satchel_cpu_probe_set_pc(cpu,lr))return SATCHEL_GUEST_DISPATCH_BAD_RETURN;
    TransferCancellation cancellation={d,cancelled,context,0};SatchelGuestTransferResult result={0};
    const uint32_t actual=d->execute_transfer(d->pool_owner,cpu,&request,&result,transfer_cancel,&cancellation);
    const SatchelGuestDispatchStatus status=actual==SATCHEL_GUEST_WINDOW_WAIT?SATCHEL_GUEST_DISPATCH_WINDOW_WAIT:actual==SATCHEL_GUEST_WINDOW_NO_SURFACE?SATCHEL_GUEST_DISPATCH_SURFACE_UNAVAILABLE:actual==SATCHEL_GUEST_WINDOW_TIMING_UNAVAILABLE?SATCHEL_GUEST_DISPATCH_PRESENT_TIMING_UNAVAILABLE:actual==SATCHEL_GUEST_TRANSFER_CANCELLED?(cancellation.expired?SATCHEL_GUEST_DISPATCH_LIMIT:SATCHEL_GUEST_DISPATCH_CANCELLED):actual==SATCHEL_GUEST_TRANSFER_SHADER_STATE_UNAVAILABLE?SATCHEL_GUEST_DISPATCH_SHADER_STATE_UNAVAILABLE:pool_status((SatchelGuestPoolStatus)actual);
    if(status!=SATCHEL_GUEST_DISPATCH_RETURNED)return restore_pc(cpu,stop,status);
    if(result.set_value && !satchel_cpu_probe_set_x(cpu,0,result.value))return restore_pc(cpu,stop,SATCHEL_GUEST_DISPATCH_INTERNAL);
    return SATCHEL_GUEST_DISPATCH_RETURNED;
}
static SatchelGuestDispatchStatus process(SatchelGuestDispatch *d,SatchelCPUProbe *cpu,const SatchelCPUProbeResult *stop,
    SatchelCPUProbeCancelled cancelled,void *context,SatchelGuestDispatchReport *report) {
    if(!d || d->retiring || !cpu || !stop || stop->reason!=SATCHEL_CPU_BREAKPOINT)return SATCHEL_GUEST_DISPATCH_INVALID;
    if(stop->pc!=d->bootstrap && stop->pc!=d->base)return SATCHEL_GUEST_DISPATCH_NOT_OURS;
    const SatchelCPUProbeResult current=satchel_cpu_probe_run(cpu,0,NULL,NULL);
    if(current.reason!=SATCHEL_CPU_BUDGET || current.pc!=stop->pc)return SATCHEL_GUEST_DISPATCH_INVALID;
    if(cancelled && cancelled(context))return SATCHEL_GUEST_DISPATCH_CANCELLED;
    if(d->events>=d->limit || now()>=d->deadline)return SATCHEL_GUEST_DISPATCH_LIMIT;
    ++d->events;report->events=d->events;
    uint64_t x0,x1,lr,id=0;
    if(!satchel_cpu_probe_get_x(cpu,0,&x0) || !satchel_cpu_probe_get_x(cpu,1,&x1) || !satchel_cpu_probe_get_x(cpu,30,&lr))return SATCHEL_GUEST_DISPATCH_INTERNAL;
    if(stop->pc==d->base && (!satchel_cpu_probe_get_x(cpu,16,&id) || id==0 || id>NAME_COUNT))return SATCHEL_GUEST_DISPATCH_INVALID;
    report->operation_id=(uint32_t)id;
    const char *name=satchel_guest_dispatch_name((uint32_t)id);
    const uint32_t pool_op=pool_operation(name);if(pool_op)return dispatch_pool(d,cpu,stop,pool_op,x0,x1,lr,cancelled,context);
    const uint32_t transfer_op=transfer_operation(name);if(transfer_op)return dispatch_transfer(d,cpu,stop,transfer_op,lr,cancelled,context);
    if(!strcmp(name,"nvnDeviceInitialize"))return device_initialize(d,cpu,stop,x0,x1,lr,cancelled,context);
    if(!strcmp(name,"nvnDeviceFinalize"))return device_finalize(d,cpu,stop,x0,lr,cancelled,context);
    if(!strcmp(name,"nvnDeviceGetInteger")) {
        uint64_t output=0;if(!satchel_cpu_probe_get_x(cpu,2,&output))return SATCHEL_GUEST_DISPATCH_INTERNAL;
        return device_get_integer(d,cpu,stop,x0,(uint32_t)x1,output,lr,cancelled,context);
    }
    uint64_t result=0;int set_result=0,write_object=0;
    uint8_t object[SATCHEL_GUEST_DISPATCH_BUILDER_SIZE];
    if(!id || !strcmp(name,"nvnDeviceGetProcAddress")) {
        uint32_t resolved=0;
        const SatchelGuestDispatchStatus status=lookup(cpu,id ? x1 : x0,&resolved);
        if(status!=SATCHEL_GUEST_DISPATCH_RETURNED)return status;
        result=d->base+STUB_FIRST+(uint64_t)(resolved-1)*STUB_STRIDE;set_result=1;
    } else if(!strcmp(name,"nvnDeviceBuilderSetDefaults")) {
        if(!satchel_cpu_probe_read(cpu,x0,object,sizeof(object)))return SATCHEL_GUEST_DISPATCH_MEMORY_FAULT;
        memset(object,0,sizeof(object));put32(object,0x504c4442);put32(object+8,1);write_object=1;
    } else if(!strcmp(name,"nvnDeviceBuilderSetFlags")) {
        if(!satchel_cpu_probe_read(cpu,x0,object,sizeof(object)))return SATCHEL_GUEST_DISPATCH_MEMORY_FAULT;
        if(get32(object)!=0x504c4442 || get32(object+8)!=1)return SATCHEL_GUEST_DISPATCH_BAD_OBJECT;
        put32(object+4,(uint32_t)x1);write_object=1;
    } else return SATCHEL_GUEST_DISPATCH_UNIMPLEMENTED;
    if(write_object && d->pool_owner){
        if(!(d->unowned_range_checked?pool_protected_range_checked(d,cpu,x0,sizeof(object)):pool_protected_range(d,x0,sizeof(object))))return SATCHEL_GUEST_DISPATCH_BAD_OBJECT;
        SatchelGuestDispatchStatus status=pool_status(pool_unowned(d,cpu,x0,sizeof(object)));if(status!=SATCHEL_GUEST_DISPATCH_RETURNED)return status;
    }
    if(cancelled && cancelled(context))return SATCHEL_GUEST_DISPATCH_CANCELLED;
    if(now()>=d->deadline)return SATCHEL_GUEST_DISPATCH_LIMIT;
    // A checked set_pc is the first commit mutation. No outputs change for an
    // invalid LR. CPU write checks the complete span before its single memcpy;
    // if it fails, restore the known valid original breakpoint PC.
    if(!satchel_cpu_probe_set_pc(cpu,lr))return SATCHEL_GUEST_DISPATCH_BAD_RETURN;
    if(write_object && !satchel_cpu_probe_write(cpu,x0,object,sizeof(object))) {
        if(!satchel_cpu_probe_set_pc(cpu,stop->pc))return SATCHEL_GUEST_DISPATCH_INTERNAL;
        return SATCHEL_GUEST_DISPATCH_MEMORY_FAULT;
    }
    if(set_result && !satchel_cpu_probe_set_x(cpu,0,result)) {
        if(!satchel_cpu_probe_set_pc(cpu,stop->pc))return SATCHEL_GUEST_DISPATCH_INTERNAL;
        return SATCHEL_GUEST_DISPATCH_INTERNAL;
    }
    return SATCHEL_GUEST_DISPATCH_RETURNED;
}
static SatchelGuestDispatchStatus satchel_guest_dispatch_handle_locked(SatchelGuestDispatch *d,SatchelCPUProbe *cpu,const SatchelCPUProbeResult *stop,
    SatchelCPUProbeCancelled cancelled,void *context,SatchelGuestDispatchReport *report) {
    if(!report)return SATCHEL_GUEST_DISPATCH_INVALID;
    *report=(SatchelGuestDispatchReport){0};
    if(dispatch_handle_active || retirement_poll_active){report->status=SATCHEL_GUEST_DISPATCH_INVALID;return SATCHEL_GUEST_DISPATCH_INVALID;}
    dispatch_handle_active=1;
    const SatchelGuestDispatchStatus status=process(d,cpu,stop,cancelled,context,report);
    report->status=status;if(d)report->events=d->events;dispatch_handle_active=0;return status;
}

/* Public owner boundaries: handlers and cleanup never race across workers. */
int satchel_guest_dispatch_retire(SatchelGuestDispatch **owner) {
    if(pthread_mutex_trylock(&owner_lock))return 0;
    const int result=satchel_guest_dispatch_retire_locked(owner);pthread_mutex_unlock(&owner_lock);return result;
}
int satchel_guest_dispatch_retirement_stats(SatchelGuestRetirementReport *report) {
    if(pthread_mutex_trylock(&owner_lock)){if(report)*report=(SatchelGuestRetirementReport){0};return 0;}
    const int result=satchel_guest_dispatch_retirement_stats_locked(report);pthread_mutex_unlock(&owner_lock);return result;
}
SatchelGuestRetirementStatus satchel_guest_dispatch_retirement_poll(SatchelCPUProbeCancelled cancelled,void *context,SatchelGuestRetirementReport *report) {
    if(pthread_mutex_trylock(&owner_lock)){if(report)*report=(SatchelGuestRetirementReport){0};return SATCHEL_GUEST_RETIRE_BUSY;}
    const SatchelGuestRetirementStatus result=satchel_guest_dispatch_retirement_poll_locked(cancelled,context,report);pthread_mutex_unlock(&owner_lock);return result;
}
void satchel_guest_dispatch_destroy(SatchelGuestDispatch *d) {
    if(pthread_mutex_lock(&owner_lock))return;
    satchel_guest_dispatch_destroy_locked(d);pthread_mutex_unlock(&owner_lock);
}
int satchel_guest_dispatch_set_device_backend(SatchelGuestDispatch *d,const SatchelGuestDeviceBackend *backend) {
    if(pthread_mutex_lock(&owner_lock))return 0;
    const int result=satchel_guest_dispatch_set_device_backend_locked(d,backend);pthread_mutex_unlock(&owner_lock);return result;
}
int satchel_guest_dispatch_set_device_backend_v2(SatchelGuestDispatch *d,const SatchelGuestDeviceBackendV2 *backend) {
    if(pthread_mutex_lock(&owner_lock))return 0;
    const int result=satchel_guest_dispatch_set_device_backend_v2_locked(d,backend);pthread_mutex_unlock(&owner_lock);return result;
}
uint32_t satchel_guest_dispatch_device_count(const SatchelGuestDispatch *d) {
    if(pthread_mutex_lock(&owner_lock))return UINT32_MAX;
    const uint32_t result=satchel_guest_dispatch_device_count_locked(d);pthread_mutex_unlock(&owner_lock);return result;
}
int satchel_guest_dispatch_device_capabilities(const SatchelGuestDispatch *d,uint64_t address,SatchelGuestDeviceCaps *caps) {
    if(pthread_mutex_lock(&owner_lock))return 0;
    const int result=satchel_guest_dispatch_device_capabilities_locked(d,address,caps);pthread_mutex_unlock(&owner_lock);return result;
}
int satchel_guest_dispatch_breakpoints(const SatchelGuestDispatch *d,uint64_t addresses[2]) {
    if(pthread_mutex_lock(&owner_lock))return 0;
    const int result=satchel_guest_dispatch_breakpoints_locked(d,addresses);pthread_mutex_unlock(&owner_lock);return result;
}
SatchelGuestDispatchStatus satchel_guest_dispatch_handle(SatchelGuestDispatch *d,SatchelCPUProbe *cpu,const SatchelCPUProbeResult *stop,SatchelCPUProbeCancelled cancelled,void *context,SatchelGuestDispatchReport *report) {
    if(pthread_mutex_lock(&owner_lock))return SATCHEL_GUEST_DISPATCH_INVALID;
    const SatchelGuestDispatchStatus result=satchel_guest_dispatch_handle_locked(d,cpu,stop,cancelled,context,report);pthread_mutex_unlock(&owner_lock);return result;
}
int satchel_guest_dispatch_check_output_range(SatchelGuestDispatch *d,SatchelCPUProbe *cpu,uint64_t address,size_t size) {
    if(pthread_mutex_trylock(&owner_lock))return 0;
    int allowed=0;
    if(d && cpu && size && address<=UINT64_MAX-size && !dispatch_handle_active && !retirement_poll_active && !d->retiring &&
       (!d->pool_owner || d->unowned_range_checked)) {
        dispatch_handle_active=1;
        if(satchel_cpu_probe_check_range(cpu,address,size,SATCHEL_CPU_WRITE) && pool_protected_range_checked(d,cpu,address,size))
            allowed=!d->pool_owner || d->unowned_range_checked(d->pool_owner,cpu,address,size)==SATCHEL_GUEST_POOL_OK;
        dispatch_handle_active=0;
    }
    pthread_mutex_unlock(&owner_lock);return allowed;
}
int satchel_guest_dispatch_handoff(SatchelGuestDispatch **owner) {
    if(pthread_mutex_lock(&owner_lock))return 0;
    const int result=satchel_guest_dispatch_retire_locked(owner);
    pthread_mutex_unlock(&owner_lock);return result;
}
static int attach_pool_services(SatchelGuestDispatch *d,const SatchelGuestDeviceBackendV2 *expected,const SatchelGuestPoolServices *services,const SatchelGuestCheckedPoolServices *checked){
    if(pthread_mutex_lock(&owner_lock))return 0;int success=0;
    if(d && expected && services && !d->retiring && !d->events && !d->pool_owner && !retirement_poll_active && !dispatch_handle_active &&
       d->backend.version==2 && expected->device.version==2 && d->backend.supported_flags==expected->device.supported_flags &&
       d->backend.context==expected->device.context && d->backend.create==expected->device.create && d->backend.destroy==expected->device.destroy &&
       d->backend.capabilities==expected->device.capabilities && d->drain==expected->drain && d->drain && services->version==1 &&
       services->create && services->destroy && services->execute && services->resolve && services->children && services->unowned_range){
        dispatch_handle_active=1;void *pool_owner=checked?checked->create_checked(pool_device,pool_protected_range_checked,d):services->create(pool_device,pool_protected_range,d);dispatch_handle_active=0;
        if(pool_owner){d->pool_owner=pool_owner;d->pool_services=*services;d->unowned_range_checked=checked?checked->unowned_range_checked:NULL;success=1;}
    }
    pthread_mutex_unlock(&owner_lock);return success;
}
int satchel_guest_dispatch_attach_pool_services(SatchelGuestDispatch *d,const SatchelGuestDeviceBackendV2 *expected,const SatchelGuestPoolServices *services){return attach_pool_services(d,expected,services,NULL);}
int satchel_guest_dispatch_attach_checked_pool_services(SatchelGuestDispatch *d,const SatchelGuestDeviceBackendV2 *expected,const SatchelGuestCheckedPoolServices *services){
    if(!services || !services->create_checked || !services->unowned_range_checked)return 0;return attach_pool_services(d,expected,&services->pools,services);
}
int satchel_guest_dispatch_attach_transfer_services(SatchelGuestDispatch *d,const SatchelGuestDeviceBackendV2 *expected,const SatchelGuestTransferServices *services){
    if(!services || !services->prepare_retirement || !services->execute_transfer)return 0;
    if(pthread_mutex_lock(&owner_lock))return 0;
    const int success=satchel_guest_dispatch_attach_checked_pool_services(d,expected,&services->checked);
    if(success){d->prepare_retirement=services->prepare_retirement;d->execute_transfer=services->execute_transfer;}
    pthread_mutex_unlock(&owner_lock);return success;
}
SatchelGuestDispatchStatus satchel_guest_dispatch_resolve_pool(SatchelGuestDispatch *d,SatchelCPUProbe *cpu,uint64_t device,uint64_t gpu,uint64_t size,
    SatchelGuestPoolBinding *binding,uint64_t *backend_device){
    if(binding)*binding=(SatchelGuestPoolBinding){0};if(backend_device)*backend_device=0;
    if(!binding || !backend_device || !cpu)return SATCHEL_GUEST_DISPATCH_INVALID;
    if(pthread_mutex_lock(&owner_lock))return SATCHEL_GUEST_DISPATCH_INTERNAL;
    SatchelGuestDispatchStatus status=SATCHEL_GUEST_DISPATCH_INVALID;
    if(d && !d->retiring && d->pool_owner && !dispatch_handle_active && !retirement_poll_active){
        dispatch_handle_active=1;uint64_t token=0,generation=0;
        if(!pool_device(d,cpu,device,&token,&generation))status=SATCHEL_GUEST_DISPATCH_BAD_OBJECT;
        else {status=pool_status(d->pool_services.resolve(d->pool_owner,cpu,device,gpu,size,binding));if(status==SATCHEL_GUEST_DISPATCH_RETURNED)*backend_device=token;}
        dispatch_handle_active=0;
    }
    pthread_mutex_unlock(&owner_lock);return status;
}
