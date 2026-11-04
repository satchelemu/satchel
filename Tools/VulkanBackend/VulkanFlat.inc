#ifndef SATCHEL_FLAT_FORMAT_PROPERTIES
#define SATCHEL_FLAT_FORMAT_PROPERTIES vkGetPhysicalDeviceFormatProperties
#endif
#include "VulkanFlat.h"
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_flat_capabilities(uint64_t device,SatchelVulkanFlatCaps *out){
 if(!out)return SATCHEL_VULKAN_INVALID;
 if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;Unlock unlock;
 auto *parent=find_device(device);if(!parent)return SATCHEL_VULKAN_INVALID;
 VkPhysicalDeviceProperties props={};vkGetPhysicalDeviceProperties(parent->job->physical,&props);
 VkFormatProperties format={};vkGetPhysicalDeviceFormatProperties(parent->job->physical,VK_FORMAT_R32G32B32A32_SFLOAT,&format);
 VkPhysicalDeviceProvokingVertexFeaturesEXT provoking={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_FEATURES_EXT};
 VkPhysicalDeviceFeatures2 features={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};features.pNext=&provoking;
 vkGetPhysicalDeviceFeatures2(parent->job->physical,&features);
 const auto &l=props.limits;SatchelVulkanFlatCaps value={};value.version=1;value.size=sizeof(value);
 value.api_version=props.apiVersion;value.graphics_queue=(parent->caps.queue_flags&VK_QUEUE_GRAPHICS_BIT)?1u:0u;
 value.float4_vertex=(format.bufferFeatures&VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT)?1u:0u;
 value.max_attributes=l.maxVertexInputAttributes;value.max_stride=l.maxVertexInputBindingStride;
 value.max_attribute_offset=l.maxVertexInputAttributeOffset;value.max_vertex_output_components=l.maxVertexOutputComponents;
 value.max_fragment_input_components=l.maxFragmentInputComponents;value.max_fragment_outputs=l.maxFragmentOutputAttachments;
 VkFormatProperties bgra={};SATCHEL_FLAT_FORMAT_PROPERTIES(parent->job->physical,VK_FORMAT_B8G8R8A8_UNORM,&bgra);
 value.physical_bgra8_color_attachment=(bgra.optimalTilingFeatures&VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)?1u:0u;
 value.admitted_color_format=SATCHEL_VULKAN_TARGET_RGBA8_UNORM;
 value.first_enabled=1;value.last_enabled=0;value.last_supported=provoking.provokingVertexLast?1u:0u;
 *out=value;return SATCHEL_VULKAN_OK;
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_flat_preflight(const SatchelVulkanFlatCaps *caps,const SatchelVulkanFlatInterface *input){
 if(!caps || !input)return SATCHEL_VULKAN_INVALID;const auto c=*caps;const auto s=*input;
 if(c.version!=1 || c.size!=sizeof(c) || s.version!=1 || s.size!=sizeof(s))return SATCHEL_VULKAN_INVALID;
 if(c.api_version<VK_API_VERSION_1_1 || c.graphics_queue!=1 || c.float4_vertex!=1 || c.first_enabled!=1 || c.last_enabled>1 || c.last_supported>1 || !c.max_fragment_outputs)return SATCHEL_VULKAN_UNSUPPORTED;
 if(s.provoking_known!=1 || s.provoking_mode!=0 || s.position_components!=15 || s.color_components!=15 || !s.vertex_stride || (s.vertex_stride&3) || s.vertex_stride>c.max_stride)return SATCHEL_VULKAN_UNSUPPORTED;
 /* This profile permits only the generator's explicitly identified inactive
  * support UBO. It is not a blanket exemption for inactive resource claims. */
 if(s.active_resources || s.declared_resources>1 || s.inactive_support_uniform!=s.declared_resources)return SATCHEL_VULKAN_UNSUPPORTED;
 uint32_t input_count=0,varying_count=0,highest_output=0,highest_input=0;
 for(unsigned i=0;i<8;i++){
  const uint32_t vi=s.vertex_inputs[i],vo=s.vertex_outputs[i],fi=s.fragment_inputs[i];
  if((vi|vo|fi|s.flat_outputs[i]|s.flat_inputs[i])&~15u)return SATCHEL_VULKAN_UNSUPPORTED;
  if(vi){++input_count;if(i>=c.max_attributes || s.attribute_offsets[i]>c.max_attribute_offset || (s.attribute_offsets[i]&3) || s.attribute_offsets[i]>s.vertex_stride || s.vertex_stride-s.attribute_offsets[i]<16)return SATCHEL_VULKAN_UNSUPPORTED;}
  else if(s.attribute_offsets[i]!=UINT32_MAX)return SATCHEL_VULKAN_UNSUPPORTED;
  if(vo!=fi || (s.flat_outputs[i]!=0 && s.flat_outputs[i]!=vo) || s.flat_inputs[i]!=fi)return SATCHEL_VULKAN_UNSUPPORTED;
  if(vo){if(vo!=15 || ++varying_count>1)return SATCHEL_VULKAN_UNSUPPORTED;highest_output=(i+1)*4;highest_input=(i+1)*4;}
 }
 if(!input_count || varying_count!=1 || highest_output+14>c.max_vertex_output_components || highest_input>c.max_fragment_input_components)return SATCHEL_VULKAN_UNSUPPORTED;
 return SATCHEL_VULKAN_OK;
}
