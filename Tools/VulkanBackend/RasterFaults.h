#include <vulkan/vulkan.h>
extern "C" VkResult raster_test_submit(VkQueue,uint32_t,const VkSubmitInfo *,VkFence);

extern "C" VkResult raster_test_pipelines(VkDevice,VkPipelineCache,uint32_t,const VkGraphicsPipelineCreateInfo *,const VkAllocationCallbacks *,VkPipeline *);
