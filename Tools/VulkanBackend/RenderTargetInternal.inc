// Included inside the backend's private namespace after DeviceSlot/BufferSlot.
struct TargetResource {
 VkDevice device=VK_NULL_HANDLE;VkImage image=VK_NULL_HANDLE;VkImageView view=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;
 VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;SatchelVulkanTargetInfo info={};
 ~TargetResource(){if(info.in_flight)return;if(view)vkDestroyImageView(device,view,nullptr);if(image)vkDestroyImage(device,image,nullptr);if(memory)vkFreeMemory(device,memory,nullptr);}
};
struct TargetSlot {uint64_t token=0;std::unique_ptr<TargetResource> resource;};
TargetSlot targets[SATCHEL_VULKAN_TARGET_LIMIT];uint64_t next_target=UINT64_C(1)<<60,target_allocated=0;
TargetSlot *find_target(uint64_t parent,uint64_t token){for(auto &slot:targets)if(token && slot.token==token && slot.resource->info.device==parent)return &slot;return nullptr;}
void release_target(TargetSlot &slot){target_allocated-=slot.resource->info.allocation_size;slot.resource.reset();slot.token=0;}
