#include <vulkan/vulkan.h>
extern "C" VkResult provoking_test_create(VkDevice,const VkBufferCreateInfo*,const VkAllocationCallbacks*,VkBuffer*);
extern "C" void provoking_test_requirements(VkDevice,const VkBufferMemoryRequirementsInfo2*,VkMemoryRequirements2*);
extern "C" VkResult provoking_test_allocate(VkDevice,const VkMemoryAllocateInfo*,const VkAllocationCallbacks*,VkDeviceMemory*);
extern "C" VkResult flat_test_pipelines(VkDevice,VkPipelineCache,uint32_t,const VkGraphicsPipelineCreateInfo*,const VkAllocationCallbacks*,VkPipeline*);
extern "C" VkResult flat_test_submit(VkQueue,uint32_t,const VkSubmitInfo*,VkFence);
