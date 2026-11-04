#include "GuestVulkanCompute.h"
#include "GuestTransferServices.h"
#include "VulkanDevice.h"
#include <stdlib.h>
typedef struct {SatchelGuestPools *pools;SatchelGuestTransfers *transfers;SatchelGuestCompute *compute;SatchelGuestPoolDeviceValidator validate;SatchelGuestPoolCheckedRangeGuard guard;void *context;} Owner;
static int validate(void *p,SatchelCPUProbe *cpu,uint64_t device,uint64_t *backend,uint64_t *generation){Owner *o=p;return o->validate(o->context,cpu,device,backend,generation);}
static int pool_guard(void *p,SatchelCPUProbe *cpu,uint64_t address,uint64_t size){Owner *o=p;
    if(o->guard && !o->guard(o->context,cpu,address,size))return 0;
    return (!o->transfers || satchel_guest_transfers_check_pool_range_checked(o->transfers,cpu,address,size)==SATCHEL_GUEST_POOL_OK) && (!o->compute || satchel_guest_compute_pool_guard(o->compute,cpu,address,size));
}
static int device_guard(void *p,SatchelCPUProbe *cpu,uint64_t address,uint64_t size){Owner *o=p;return (!o->guard || o->guard(o->context,cpu,address,size)) && (!o->compute || satchel_guest_compute_pool_guard(o->compute,cpu,address,size));}
static int program_guard(void *p,SatchelCPUProbe *cpu,uint64_t address,uint64_t size){Owner *o=p;return (!o->guard || o->guard(o->context,cpu,address,size)) && (!o->transfers || satchel_guest_transfers_guard(o->transfers,cpu,address,size)==SATCHEL_GUEST_POOL_OK);}
static void *create_checked(SatchelGuestPoolDeviceValidator v,SatchelGuestPoolCheckedRangeGuard g,void *c){Owner *o=calloc(1,sizeof(*o));if(!o)return NULL;o->validate=v;o->guard=g;o->context=c;
    o->pools=satchel_guest_pools_create_checked(validate,pool_guard,o);if(!o->pools){free(o);return NULL;}
    o->transfers=satchel_guest_transfers_create_checked(o->pools,validate,device_guard,o);if(!o->transfers){if(satchel_guest_pools_try_destroy(&o->pools)!=SATCHEL_GUEST_POOL_OK)abort();free(o);return NULL;}o->compute=satchel_guest_compute_create(o->pools,validate,program_guard,o);if(!o->compute){if(satchel_guest_transfers_try_destroy(&o->transfers) || satchel_guest_pools_try_destroy(&o->pools))abort();free(o);return NULL;}
    if(!satchel_guest_transfers_attach_compute(o->transfers,o->compute))abort();return o;
}
static void *create_legacy(SatchelGuestPoolDeviceValidator v,SatchelGuestPoolRangeGuard g,void *c){(void)v;(void)g;(void)c;return NULL;}
static uint32_t prepare_retirement(void *p){Owner *o=p;uint32_t status=satchel_guest_transfers_try_destroy(&o->transfers);if(status)return status;status=satchel_guest_compute_try_destroy(&o->compute);return status==SATCHEL_GC_OK?SATCHEL_GUEST_POOL_OK:(status==SATCHEL_GC_BUSY?SATCHEL_GUEST_POOL_BUSY:SATCHEL_GUEST_POOL_BACKEND);}
static SatchelGuestPoolStatus destroy(void **pointer){Owner *o=*pointer;if(!o)return SATCHEL_GUEST_POOL_OK;if(o->transfers || o->compute)return SATCHEL_GUEST_POOL_BUSY;
    SatchelGuestPoolStatus status=satchel_guest_pools_try_destroy(&o->pools);if(status==SATCHEL_GUEST_POOL_OK){free(o);*pointer=NULL;}return status;
}
static SatchelGuestPoolStatus execute_pool(void *p,SatchelCPUProbe *cpu,const SatchelGuestPoolRequest *request,SatchelGuestPoolResult *result){Owner *o=p;return satchel_guest_pools_execute(o->pools,cpu,request,result);}
static uint32_t execute_transfer(void *p,SatchelCPUProbe *cpu,const SatchelGuestTransferRequest *request,SatchelGuestTransferResult *result,SatchelCPUProbeCancelled cancel,void *context){Owner *o=p;return satchel_guest_transfers_execute(o->transfers,cpu,request,result,cancel,context);}
static SatchelGuestPoolStatus resolve(void *p,SatchelCPUProbe *cpu,uint64_t device,uint64_t gpu,uint64_t size,SatchelGuestPoolBinding *binding){Owner *o=p;return satchel_guest_pools_resolve(o->pools,cpu,device,gpu,size,binding);}
static SatchelGuestPoolStatus children(void *p,uint64_t device,uint32_t *count){Owner *o=p;uint32_t pools=0,transfers=0;SatchelGuestPoolStatus status=satchel_guest_pools_device_children(o->pools,device,&pools);
    if(status)return status;status=(SatchelGuestPoolStatus)satchel_guest_transfers_device_children(o->transfers,device,&transfers);if(status)return status;uint32_t programs=0;if(satchel_guest_compute_device_children(o->compute,device,&programs)!=SATCHEL_GC_OK)return SATCHEL_GUEST_POOL_BUSY;*count=pools+transfers+programs;return SATCHEL_GUEST_POOL_OK;
}
static SatchelGuestPoolStatus unowned_range(void *p,uint64_t address,uint64_t size){(void)p;(void)address;(void)size;return SATCHEL_GUEST_POOL_UNSUPPORTED;}
static SatchelGuestPoolStatus unowned_range_checked(void *p,SatchelCPUProbe *cpu,uint64_t address,uint64_t size){Owner *o=p;SatchelGuestPoolStatus status=satchel_guest_pools_check_unowned_range_checked(o->pools,cpu,address,size);if(status)return status;status=(SatchelGuestPoolStatus)satchel_guest_transfers_check_unowned_range_checked(o->transfers,cpu,address,size);if(status)return status;return satchel_guest_compute_pool_guard(o->compute,cpu,address,size)?SATCHEL_GUEST_POOL_OK:SATCHEL_GUEST_POOL_BAD_OBJECT;}
int satchel_guest_dispatch_enable_vulkan_compute(SatchelGuestDispatch *dispatch){SatchelGuestDeviceBackendV2 actual={0};if(!satchel_vulkan_guest_device_backend_v2(&actual))return 0;
    const SatchelGuestTransferServices services={{{1,create_legacy,destroy,execute_pool,resolve,children,unowned_range},create_checked,unowned_range_checked},prepare_retirement,execute_transfer};return satchel_guest_dispatch_attach_transfer_services(dispatch,&actual,&services);
}
