#include <vulkan/vulkan.h>
extern "C" VkResult flat_test_pipelines(VkDevice,VkPipelineCache,uint32_t,const VkGraphicsPipelineCreateInfo*,const VkAllocationCallbacks*,VkPipeline*);
extern "C" VkResult flat_test_submit(VkQueue,uint32_t,const VkSubmitInfo*,VkFence);
