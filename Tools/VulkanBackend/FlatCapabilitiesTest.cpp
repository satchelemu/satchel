#include "VulkanFlat.h"
#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
static unsigned checks,calls,mode;static VkPhysicalDevice physical;static uint64_t live;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"flat capability assertion%d\n",__LINE__);std::abort();}}while(0)
extern "C" void flat_caps_properties(VkPhysicalDevice p,VkFormat f,VkFormatProperties *out){
 ++calls;CHECK(f==VK_FORMAT_B8G8R8A8_UNORM);physical=p;vkGetPhysicalDeviceFormatProperties(p,f,out);
 if(mode==1)out->optimalTilingFeatures&=~VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
 if(mode==2){out->optimalTilingFeatures=VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;out->linearTilingFeatures=0;out->bufferFeatures=0;}
 if(mode==3){SatchelVulkanFlatCaps sentinel;std::memset(&sentinel,0xa5,sizeof(sentinel));auto before=sentinel;CHECK(satchel_vulkan_flat_capabilities(live,&sentinel)==SATCHEL_VULKAN_BUSY);CHECK(!std::memcmp(&before,&sentinel,sizeof(before)));}
}
int main(){SatchelGuestDeviceCaps device={};CHECK(satchel_vulkan_device_create(0,&live,&device)==SATCHEL_VULKAN_OK);SatchelVulkanFlatCaps caps={};CHECK(satchel_vulkan_flat_capabilities(live,&caps)==SATCHEL_VULKAN_OK&&calls==1);VkFormatProperties raw={};vkGetPhysicalDeviceFormatProperties(physical,VK_FORMAT_B8G8R8A8_UNORM,&raw);CHECK(caps.physical_bgra8_color_attachment==((raw.optimalTilingFeatures&VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)?1u:0u));CHECK(caps.admitted_color_format==SATCHEL_VULKAN_TARGET_RGBA8_UNORM);const auto actual_physical=caps.physical_bgra8_color_attachment;
 SatchelVulkanFlatInterface interface={};interface.version=1;interface.size=sizeof(interface);interface.vertex_inputs[0]=interface.vertex_inputs[1]=15;interface.vertex_outputs[0]=interface.fragment_inputs[0]=interface.flat_inputs[0]=15;interface.position_components=interface.color_components=15;interface.provoking_known=1;interface.vertex_stride=32;for(auto &offset:interface.attribute_offsets)offset=UINT32_MAX;interface.attribute_offsets[0]=0;interface.attribute_offsets[1]=16;CHECK(satchel_vulkan_flat_preflight(&caps,&interface)==SATCHEL_VULKAN_OK);
 mode=1;CHECK(satchel_vulkan_flat_capabilities(live,&caps)==SATCHEL_VULKAN_OK&&!caps.physical_bgra8_color_attachment&&caps.admitted_color_format==1);CHECK(satchel_vulkan_flat_preflight(&caps,&interface)==SATCHEL_VULKAN_OK);mode=2;CHECK(satchel_vulkan_flat_capabilities(live,&caps)==SATCHEL_VULKAN_OK&&caps.physical_bgra8_color_attachment&&caps.admitted_color_format==1);mode=3;CHECK(satchel_vulkan_flat_capabilities(live,&caps)==SATCHEL_VULKAN_OK);mode=0;
 // Physical BGRA support never changes the admitted target format contract.
 SatchelVulkanTargetCaps tc={};CHECK(satchel_vulkan_target_capabilities(live,2,&tc)==SATCHEL_VULKAN_UNSUPPORTED);SatchelVulkanTargetDescription request={1,sizeof(request),2,16,16,0};SatchelVulkanTargetInfo ti={};uint64_t token=99;CHECK(satchel_vulkan_target_create(live,&request,&token,&ti)==SATCHEL_VULKAN_UNSUPPORTED&&!token);SatchelVulkanTargetStats stats={};CHECK(satchel_vulkan_target_stats(&stats)==SATCHEL_VULKAN_OK&&!stats.live_targets&&!stats.allocated_bytes);
 SatchelVulkanFlatCaps sentinel;std::memset(&sentinel,0xa5,sizeof(sentinel));auto before=sentinel;unsigned previous=calls;CHECK(satchel_vulkan_flat_capabilities(live+1,&sentinel)==SATCHEL_VULKAN_INVALID&&calls==previous);CHECK(!std::memcmp(&before,&sentinel,sizeof(before)));CHECK(satchel_vulkan_device_destroy(live)==SATCHEL_VULKAN_OK);CHECK(satchel_vulkan_flat_capabilities(live,&sentinel)==SATCHEL_VULKAN_INVALID&&calls==previous);CHECK(!std::memcmp(&before,&sentinel,sizeof(before)));CHECK(!satchel_vulkan_device_live_count()&&!satchel_vulkan_submission_live_count());
 std::printf("Physical BGRA/native RGBA capability delta: %u ASan/UBSan assertions, actual physicalBGRA8=%u; actual independent format query, injected feature-bit absence/presence, reentrant/stale guards and unsupported BGRA target creation. No general BGRA renderer support claim.\n",checks,actual_physical);
}
