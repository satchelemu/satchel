#include <vulkan/vulkan.h>
extern "C" VkResult target_test_framebuffer(VkDevice,const VkFramebufferCreateInfo *,const VkAllocationCallbacks *,VkFramebuffer *);
extern "C" VkResult target_test_submit(VkQueue,uint32_t,const VkSubmitInfo *,VkFence);
