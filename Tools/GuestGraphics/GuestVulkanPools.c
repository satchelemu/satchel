#include "GuestVulkanPools.h"
#include "GuestCheckedPoolServices.h"
#include "VulkanDevice.h"
static void *create(SatchelGuestPoolDeviceValidator validate,SatchelGuestPoolRangeGuard guard,void *context){return satchel_guest_pools_create_protected(validate,guard,context);}
static void *create_checked(SatchelGuestPoolDeviceValidator v,SatchelGuestPoolCheckedRangeGuard g,void *c){return satchel_guest_pools_create_checked(v,g,c);}
static SatchelGuestPoolStatus destroy(void **pointer){SatchelGuestPools *owner=*pointer;SatchelGuestPoolStatus status=satchel_guest_pools_try_destroy(&owner);*pointer=owner;return status;}
static SatchelGuestPoolStatus execute(void *owner,SatchelCPUProbe *cpu,const SatchelGuestPoolRequest *request,SatchelGuestPoolResult *result){return satchel_guest_pools_execute(owner,cpu,request,result);}
static SatchelGuestPoolStatus resolve(void *owner,SatchelCPUProbe *cpu,uint64_t device,uint64_t gpu,uint64_t size,SatchelGuestPoolBinding *binding){return satchel_guest_pools_resolve(owner,cpu,device,gpu,size,binding);}
static SatchelGuestPoolStatus children(void *owner,uint64_t device,uint32_t *count){return satchel_guest_pools_device_children(owner,device,count);}
static SatchelGuestPoolStatus unowned_range(void *owner,uint64_t address,uint64_t size){return satchel_guest_pools_check_unowned_range(owner,address,size);}
static SatchelGuestPoolStatus unowned_range_checked(void *owner,SatchelCPUProbe *cpu,uint64_t address,uint64_t size){return satchel_guest_pools_check_unowned_range_checked(owner,cpu,address,size);}
int satchel_guest_dispatch_enable_vulkan_pools(SatchelGuestDispatch *dispatch){
    SatchelGuestDeviceBackendV2 actual={0};if(!satchel_vulkan_guest_device_backend_v2(&actual))return 0;
    const SatchelGuestCheckedPoolServices services={{1,create,destroy,execute,resolve,children,unowned_range},create_checked,unowned_range_checked};
    return satchel_guest_dispatch_attach_checked_pool_services(dispatch,&actual,&services);
}
SatchelGuestDispatchStatus satchel_guest_dispatch_vulkan_pool_binding(SatchelGuestDispatch *dispatch,SatchelCPUProbe *cpu,uint64_t guest_device,
    uint64_t gpu,uint64_t size,SatchelGuestPoolBinding *binding,uint64_t *backend_device){
    return satchel_guest_dispatch_resolve_pool(dispatch,cpu,guest_device,gpu,size,binding,backend_device);
}
