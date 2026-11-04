#include "VulkanCapturedHost.h"
#ifndef SATCHEL_CAPTURED_HOST_FORMAT_PROPERTIES
#define SATCHEL_CAPTURED_HOST_FORMAT_PROPERTIES vkGetPhysicalDeviceFormatProperties
#endif
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_captured_host(uint64_t device,uint64_t target,SatchelVulkanCapturedHost *out){
 if(!out || (reinterpret_cast<uintptr_t>(out)%alignof(SatchelVulkanCapturedHost)))return SATCHEL_VULKAN_INVALID;
 if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;Unlock unlock;
 auto *parent=find_device(device);auto *color=find_target(device,target);
 if(!parent || !color)return SATCHEL_VULKAN_INVALID;
 const auto &caps=parent->caps;const auto &info=color->resource->info;
 // This factory enables only portability_subset, no optional feature structs.
 // Supports* below means implemented by this fixed profile, never every
 // operation physically supported by an underlying Vulkan implementation.
 if(caps.api_version<VK_API_VERSION_1_1 || !(caps.queue_flags&VK_QUEUE_GRAPHICS_BIT) || caps.enabled_features || !caps.portability_subset || info.format!=SATCHEL_VULKAN_TARGET_RGBA8_UNORM)return SATCHEL_VULKAN_UNSUPPORTED;
 const auto alignment=caps.min_storage_buffer_offset_alignment;
 if(alignment<16 || alignment>65536 || (alignment&(alignment-1)))return SATCHEL_VULKAN_UNSUPPORTED;
 VkFormatProperties format={};SATCHEL_CAPTURED_HOST_FORMAT_PROPERTIES(parent->job->physical,VK_FORMAT_B8G8R8A8_UNORM,&format);
 if(!(format.optimalTilingFeatures&VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT))return SATCHEL_VULKAN_UNSUPPORTED;
 SatchelVulkanCapturedHost value={};value.version=1;value.size=sizeof(value);value.profile=1;
 value.device_known=UINT64_C(1)<<3;value.unknown=(UINT64_C(1)<<1)|(UINT64_C(1)<<2);
 value.policy_known=UINT64_C(16777215)&~(value.device_known|value.unknown);value.values=UINT64_C(1)<<3;
 value.storage_alignment=(uint32_t)alignment;value.physical_bgra8_color_attachment=1;
 value.admitted_color_format=SATCHEL_VULKAN_TARGET_RGBA8_UNORM;value.current_color_format=info.format;
 *out=value;return SATCHEL_VULKAN_OK;
}
