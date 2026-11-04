uint64_t provoking_allocated=0;uint32_t provoking_buffers=0;
struct RasterResource {
 uint64_t parent=0;VkDevice device=VK_NULL_HANDLE;
 VkShaderModule vertex=VK_NULL_HANDLE,fragment=VK_NULL_HANDLE;VkPipelineLayout layout=VK_NULL_HANDLE;
 VkRenderPass pass=VK_NULL_HANDLE;VkPipeline pipeline=VK_NULL_HANDLE;
 VkBuffer provoking_indices=VK_NULL_HANDLE;VkDeviceMemory provoking_memory=VK_NULL_HANDLE;void *provoking_mapped=nullptr;uint64_t provoking_allocation=0;
 SatchelVulkanRasterDescription description={};SatchelVulkanRasterInfo info={};
 ~RasterResource(){if(info.references)return;if(provoking_mapped)vkUnmapMemory(device,provoking_memory);if(provoking_indices)vkDestroyBuffer(device,provoking_indices,nullptr);if(provoking_memory)vkFreeMemory(device,provoking_memory,nullptr);if(provoking_allocation){provoking_allocated-=provoking_allocation;--provoking_buffers;}if(pipeline)vkDestroyPipeline(device,pipeline,nullptr);if(pass)vkDestroyRenderPass(device,pass,nullptr);if(layout)vkDestroyPipelineLayout(device,layout,nullptr);if(vertex)vkDestroyShaderModule(device,vertex,nullptr);if(fragment)vkDestroyShaderModule(device,fragment,nullptr);}
};
struct RasterSlot {uint64_t token=0;std::unique_ptr<RasterResource> resource;};
RasterSlot raster_pipelines[SATCHEL_VULKAN_RASTER_PIPELINE_LIMIT];uint64_t next_raster=UINT64_C(1)<<59;
RasterSlot *find_raster(uint64_t parent,uint64_t token){for(auto &slot:raster_pipelines)if(token && slot.token==token && slot.resource->parent==parent)return &slot;return nullptr;}

/* Cyclic triangle order preserves orientation, source indices and primitive
 * sequence. The typed profile excludes observable invocation-order effects. */
void raster_draw_vertices(VkCommandBuffer command,const RasterResource &r,uint32_t count){
 if(r.provoking_indices){vkCmdBindIndexBuffer(command,r.provoking_indices,0,VK_INDEX_TYPE_UINT32);vkCmdDrawIndexed(command,count,1,0,0,0);}
 else vkCmdDraw(command,count,1,0,0);
}
void raster_index_barrier(VkCommandBuffer command,const RasterResource &r){if(!r.provoking_indices)return;
 VkBufferMemoryBarrier barrier={VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_INDEX_READ_BIT;barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.buffer=r.provoking_indices;barrier.offset=0;barrier.size=SATCHEL_VULKAN_PROVOKING_INDEX_COUNT*sizeof(uint32_t);
 vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,0,0,nullptr,1,&barrier,0,nullptr);
}
