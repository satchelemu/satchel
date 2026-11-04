#ifndef SATCHEL_PROVOKING_CREATE_BUFFER
#define SATCHEL_PROVOKING_CREATE_BUFFER vkCreateBuffer
#endif
#ifndef SATCHEL_PROVOKING_REQUIREMENTS
#define SATCHEL_PROVOKING_REQUIREMENTS vkGetBufferMemoryRequirements2
#endif
#ifndef SATCHEL_PROVOKING_ALLOCATE
#define SATCHEL_PROVOKING_ALLOCATE vkAllocateMemory
#endif
namespace {
SatchelVulkanStatus provoking_index_create(const DeviceSlot &parent,RasterResource &resource){
 const uint64_t bytes=SATCHEL_VULKAN_PROVOKING_INDEX_COUNT*sizeof(uint32_t);
 if(provoking_buffers>=SATCHEL_VULKAN_RASTER_PIPELINE_LIMIT || bytes>SATCHEL_VULKAN_PROVOKING_MEMORY_LIMIT-provoking_allocated)return SATCHEL_VULKAN_BUSY;
 VkBufferCreateInfo create={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};create.size=bytes;create.usage=VK_BUFFER_USAGE_INDEX_BUFFER_BIT;create.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
 if(SATCHEL_PROVOKING_CREATE_BUFFER(resource.device,&create,nullptr,&resource.provoking_indices)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
 VkMemoryDedicatedRequirements dedicated={VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};VkMemoryRequirements2 required={VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};required.pNext=&dedicated;VkBufferMemoryRequirementsInfo2 query={VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2};query.buffer=resource.provoking_indices;SATCHEL_PROVOKING_REQUIREMENTS(resource.device,&query,&required);const auto &req=required.memoryRequirements;
 VkPhysicalDeviceMaintenance3Properties allocation_limits={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES};VkPhysicalDeviceProperties2 props={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};props.pNext=&allocation_limits;vkGetPhysicalDeviceProperties2(parent.job->physical,&props);
 if(req.size<bytes || !req.alignment || (req.alignment&(req.alignment-1)) || !req.memoryTypeBits || !allocation_limits.maxMemoryAllocationSize || req.size>allocation_limits.maxMemoryAllocationSize || req.size>SATCHEL_VULKAN_PROVOKING_ALLOCATION_MAX)return SATCHEL_VULKAN_UNSUPPORTED;
 if(req.size>SATCHEL_VULKAN_PROVOKING_MEMORY_LIMIT-provoking_allocated)return SATCHEL_VULKAN_BUSY;
 VkPhysicalDeviceMemoryProperties memory={};vkGetPhysicalDeviceMemoryProperties(parent.job->physical,&memory);if(!memory.memoryTypeCount || memory.memoryTypeCount>VK_MAX_MEMORY_TYPES || !memory.memoryHeapCount || memory.memoryHeapCount>VK_MAX_MEMORY_HEAPS)return SATCHEL_VULKAN_UNSUPPORTED;uint32_t type=UINT32_MAX;
 for(uint32_t i=0;i<memory.memoryTypeCount;i++)if((req.memoryTypeBits&(1u<<i)) && (memory.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) && memory.memoryTypes[i].heapIndex<memory.memoryHeapCount && req.size<=memory.memoryHeaps[memory.memoryTypes[i].heapIndex].size){if(type==UINT32_MAX)type=i;if(memory.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT){type=i;break;}}
 if(type==UINT32_MAX)return SATCHEL_VULKAN_UNSUPPORTED;
 VkMemoryDedicatedAllocateInfo dedicated_info={VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};dedicated_info.buffer=resource.provoking_indices;VkMemoryAllocateInfo allocate={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocate.allocationSize=req.size;allocate.memoryTypeIndex=type;if(dedicated.requiresDedicatedAllocation || dedicated.prefersDedicatedAllocation)allocate.pNext=&dedicated_info;
 if(SATCHEL_PROVOKING_ALLOCATE(resource.device,&allocate,nullptr,&resource.provoking_memory)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
 resource.provoking_allocation=req.size;provoking_allocated+=req.size;++provoking_buffers;
 if(vkBindBufferMemory(resource.device,resource.provoking_indices,resource.provoking_memory,0)!=VK_SUCCESS || vkMapMemory(resource.device,resource.provoking_memory,0,VK_WHOLE_SIZE,0,&resource.provoking_mapped)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
 std::memset(resource.provoking_mapped,0,static_cast<size_t>(req.size));auto *indices=static_cast<uint32_t*>(resource.provoking_mapped);for(uint32_t i=0;i<SATCHEL_VULKAN_PROVOKING_INDEX_COUNT;i+=3){indices[i]=i+2;indices[i+1]=i;indices[i+2]=i+1;}
 if(!(memory.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){VkMappedMemoryRange range={VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};range.memory=resource.provoking_memory;range.offset=0;range.size=VK_WHOLE_SIZE;if(vkFlushMappedMemoryRanges(resource.device,1,&range)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;}
 vkUnmapMemory(resource.device,resource.provoking_memory);resource.provoking_mapped=nullptr;return SATCHEL_VULKAN_OK;
}
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_provoking_capabilities(uint64_t device,SatchelVulkanProvokingCaps *out){
 if(!out)return SATCHEL_VULKAN_INVALID;if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;Unlock unlock;auto *parent=find_device(device);if(!parent)return SATCHEL_VULKAN_INVALID;
 try{uint32_t count=0;if(vkEnumerateDeviceExtensionProperties(parent->job->physical,nullptr,&count,nullptr)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;if(count>1024)return SATCHEL_VULKAN_UNSUPPORTED;std::vector<VkExtensionProperties> extensions(count);if(vkEnumerateDeviceExtensionProperties(parent->job->physical,nullptr,&count,extensions.data())!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;extensions.resize(count);
  const bool advertised=has(extensions,VK_EXT_PROVOKING_VERTEX_EXTENSION_NAME);VkPhysicalDeviceProvokingVertexFeaturesEXT provoking={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROVOKING_VERTEX_FEATURES_EXT};if(advertised){VkPhysicalDeviceFeatures2 features={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};features.pNext=&provoking;vkGetPhysicalDeviceFeatures2(parent->job->physical,&features);}
  SatchelVulkanProvokingCaps caps={};caps.version=1;caps.size=sizeof(caps);caps.graphics_queue=(parent->caps.queue_flags&VK_QUEUE_GRAPHICS_BIT)?1u:0u;caps.native_extension=advertised;caps.native_last_supported=advertised&&provoking.provokingVertexLast;caps.native_last_enabled=0;caps.first_supported=caps.graphics_queue;VkPhysicalDeviceProperties props={};vkGetPhysicalDeviceProperties(parent->job->physical,&props);caps.rotated_last_supported=caps.graphics_queue && props.limits.maxDrawIndexedIndexValue>=SATCHEL_VULKAN_PROVOKING_INDEX_COUNT-1;caps.max_vertex_count=SATCHEL_VULKAN_PROVOKING_INDEX_COUNT;caps.per_pipeline_allocation_limit=SATCHEL_VULKAN_PROVOKING_ALLOCATION_MAX;caps.aggregate_allocation_limit=SATCHEL_VULKAN_PROVOKING_MEMORY_LIMIT;*out=caps;return SATCHEL_VULKAN_OK;
 }catch(...){return SATCHEL_VULKAN_INTERNAL_ERROR;}
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_provoking_stats(SatchelVulkanProvokingStats *out){if(!out)return SATCHEL_VULKAN_INVALID;if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;Unlock unlock;*out={1,sizeof(*out),provoking_buffers,SATCHEL_VULKAN_RASTER_PIPELINE_LIMIT,provoking_allocated,SATCHEL_VULKAN_PROVOKING_MEMORY_LIMIT};return SATCHEL_VULKAN_OK;}
