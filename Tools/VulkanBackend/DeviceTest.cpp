#include "VulkanDevice.h"
#include <cstdio>
#include <cstdlib>
static unsigned checks;
static void check(bool value){++checks;if(!value){std::fprintf(stderr,"Vulkan lifecycle check%u failed\n",checks);std::exit(1);}}
int main(){
    check(satchel_vulkan_device_live_count()==0);
    SatchelGuestDeviceCaps caps={};uint64_t handle=0;
    check(satchel_vulkan_device_create(0,nullptr,&caps)==SATCHEL_VULKAN_INVALID);
    check(satchel_vulkan_device_create(0,&handle,nullptr)==SATCHEL_VULKAN_INVALID);
    for(unsigned bit=0;bit<32;++bit){handle=77;check(satchel_vulkan_device_create(1u<<bit,&handle,&caps)==SATCHEL_VULKAN_UNSUPPORTED && handle==0 && caps.version==0);}
    check(satchel_vulkan_device_destroy(0)==SATCHEL_VULKAN_INVALID);
    check(satchel_vulkan_device_capabilities(1,&caps)==SATCHEL_VULKAN_INVALID && caps.version==0);
    uint64_t handles[SATCHEL_GUEST_DEVICE_REGISTRY_LIMIT]={};
    for(unsigned i=0;i<SATCHEL_GUEST_DEVICE_REGISTRY_LIMIT;++i){
        check(satchel_vulkan_device_create(0,&handles[i],&caps)==SATCHEL_VULKAN_OK && handles[i]!=0);
        check(caps.version==1 && caps.api_version && caps.portability_subset==1 && (caps.queue_flags&2));
        check(caps.enabled_features==0 && !(caps.supported_features&~15u));
        check(caps.max_compute_invocations && caps.max_image_dimension_2d && caps.max_storage_buffer_range && caps.noncoherent_atom_size);
        SatchelGuestDeviceCaps again={};check(satchel_vulkan_device_capabilities(handles[i],&again)==SATCHEL_VULKAN_OK && again.device_id==caps.device_id && again.max_compute_invocations==caps.max_compute_invocations);
        check(satchel_vulkan_device_live_count()==i+1);
    }
    check(satchel_vulkan_device_create(0,&handle,&caps)==SATCHEL_VULKAN_BUSY && handle==0);
    for(auto token:handles){check(satchel_vulkan_device_destroy(token)==SATCHEL_VULKAN_OK);check(satchel_vulkan_device_destroy(token)==SATCHEL_VULKAN_INVALID);check(satchel_vulkan_device_capabilities(token,&caps)==SATCHEL_VULKAN_INVALID && caps.version==0);}
    check(satchel_vulkan_device_live_count()==0);
    SatchelGuestDeviceBackend backend={};check(satchel_vulkan_guest_device_backend(&backend) && backend.version==1 && backend.supported_flags==0);
    check(backend.create(backend.context,0,&handle,&caps)==SATCHEL_GUEST_BACKEND_OK && handle>handles[7]);
    check(backend.capabilities(backend.context,handle,&caps)==SATCHEL_GUEST_BACKEND_OK && caps.max_compute_invocations);
    check(backend.destroy(backend.context,handle)==SATCHEL_GUEST_BACKEND_OK && satchel_vulkan_device_live_count()==0);
    std::printf("Persistent real Vulkan lifecycle: %u wrapper sanitizer checks passed; actual capabilities, eight-device limit, stale tokens and cleanup verified.\n",checks);
}
