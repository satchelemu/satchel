#ifndef SATCHEL_TARGET_CREATE_FRAMEBUFFER
#define SATCHEL_TARGET_CREATE_FRAMEBUFFER vkCreateFramebuffer
#endif
#ifndef SATCHEL_TARGET_QUEUE_SUBMIT
#define SATCHEL_TARGET_QUEUE_SUBMIT vkQueueSubmit
#endif
#include <cmath>
namespace {
constexpr VkFormat target_format=VK_FORMAT_R8G8B8A8_UNORM;
constexpr VkImageUsageFlags target_usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
SatchelVulkanStatus target_caps(const DeviceSlot &parent,uint32_t format,SatchelVulkanTargetCaps &out){
 out={1,sizeof(out),format,0,!!(parent.caps.queue_flags&VK_QUEUE_GRAPHICS_BIT),0,0,0,SATCHEL_VULKAN_TARGET_BYTE_LIMIT,0};
 if(format!=SATCHEL_VULKAN_TARGET_RGBA8_UNORM)return SATCHEL_VULKAN_UNSUPPORTED;
 VkFormatProperties2 properties={VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2};vkGetPhysicalDeviceFormatProperties2(parent.job->physical,target_format,&properties);
 const auto required=VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT|VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
 VkPhysicalDeviceImageFormatInfo2 request={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};request.format=target_format;request.type=VK_IMAGE_TYPE_2D;request.tiling=VK_IMAGE_TILING_OPTIMAL;request.usage=target_usage;
 VkImageFormatProperties2 answer={VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};const auto result=vkGetPhysicalDeviceImageFormatProperties2(parent.job->physical,&request,&answer);
 if(result==VK_ERROR_FORMAT_NOT_SUPPORTED)return SATCHEL_VULKAN_OK;if(result!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
 VkPhysicalDeviceProperties device_properties={};vkGetPhysicalDeviceProperties(parent.job->physical,&device_properties);const auto &limits=device_properties.limits;
 const auto &image=answer.imageFormatProperties;out.max_width=std::min<uint32_t>(image.maxExtent.width,SATCHEL_VULKAN_TARGET_DIMENSION_LIMIT);out.max_height=std::min<uint32_t>(image.maxExtent.height,SATCHEL_VULKAN_TARGET_DIMENSION_LIMIT);out.max_resource_bytes=image.maxResourceSize;out.max_width=std::min(out.max_width,limits.maxFramebufferWidth);out.max_height=std::min(out.max_height,limits.maxFramebufferHeight);
 out.samples=(image.sampleCounts&VK_SAMPLE_COUNT_1_BIT)?1:0;
 out.supported=out.queue_graphics && limits.maxColorAttachments && (limits.framebufferColorSampleCounts&VK_SAMPLE_COUNT_1_BIT) && (properties.formatProperties.optimalTilingFeatures&required)==required && out.max_width && out.max_height && image.maxExtent.depth && image.maxMipLevels && image.maxArrayLayers && out.samples;
 return SATCHEL_VULKAN_OK;
}
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_target_capabilities(uint64_t device,uint32_t format,SatchelVulkanTargetCaps *out){
 if(!out)return SATCHEL_VULKAN_INVALID;*out={};if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;Unlock unlock;
 auto *parent=find_device(device);if(!parent)return SATCHEL_VULKAN_INVALID;return target_caps(*parent,format,*out);
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_target_create(uint64_t device,const SatchelVulkanTargetDescription *input,uint64_t *token,SatchelVulkanTargetInfo *out){
 if(!token || !out)return SATCHEL_VULKAN_INVALID;*token=0;*out={};if(!input)return SATCHEL_VULKAN_INVALID;const auto request=*input;
 if(request.version!=1 || request.size!=sizeof(request) || !request.width || !request.height)return SATCHEL_VULKAN_INVALID;
 if(request.flags || request.format!=SATCHEL_VULKAN_TARGET_RGBA8_UNORM || request.width>SATCHEL_VULKAN_TARGET_DIMENSION_LIMIT || request.height>SATCHEL_VULKAN_TARGET_DIMENSION_LIMIT)return SATCHEL_VULKAN_UNSUPPORTED;
 const uint64_t logical=(uint64_t)request.width*request.height*4;if(logical>SATCHEL_VULKAN_TARGET_BYTE_LIMIT)return SATCHEL_VULKAN_UNSUPPORTED;
 if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;Unlock unlock;
 try{
  auto *parent=find_device(device);if(!parent)return SATCHEL_VULKAN_INVALID;SatchelVulkanTargetCaps caps={};auto status=target_caps(*parent,request.format,caps);if(status!=SATCHEL_VULKAN_OK)return status;
  if(!caps.supported || request.width>caps.max_width || request.height>caps.max_height || logical>caps.max_resource_bytes)return SATCHEL_VULKAN_UNSUPPORTED;
  TargetSlot *slot=nullptr;for(auto &candidate:targets)if(!candidate.token){slot=&candidate;break;}
  if(!slot || next_target==(UINT64_C(1)<<61) || logical>SATCHEL_VULKAN_TARGET_MEMORY_LIMIT-target_allocated)return SATCHEL_VULKAN_BUSY;
  auto resource=std::make_unique<TargetResource>();resource->device=parent->job->device;
  VkImageCreateInfo create={VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};create.imageType=VK_IMAGE_TYPE_2D;create.format=target_format;create.extent={request.width,request.height,1};create.mipLevels=1;create.arrayLayers=1;create.samples=VK_SAMPLE_COUNT_1_BIT;create.tiling=VK_IMAGE_TILING_OPTIMAL;create.usage=target_usage;create.sharingMode=VK_SHARING_MODE_EXCLUSIVE;create.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
  if(vkCreateImage(resource->device,&create,nullptr,&resource->image)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
  VkMemoryDedicatedRequirements dedicated={VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};VkMemoryRequirements2 requirements={VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};requirements.pNext=&dedicated;
  VkImageMemoryRequirementsInfo2 query={VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2};query.image=resource->image;vkGetImageMemoryRequirements2(resource->device,&query,&requirements);
  const auto &memory_required=requirements.memoryRequirements;
  if(memory_required.size<logical || !memory_required.alignment || !memory_required.memoryTypeBits || memory_required.size>caps.max_resource_bytes)return SATCHEL_VULKAN_UNSUPPORTED;
  if(memory_required.size>SATCHEL_VULKAN_TARGET_MEMORY_LIMIT-target_allocated)return SATCHEL_VULKAN_BUSY;
  VkPhysicalDeviceMemoryProperties memory={};vkGetPhysicalDeviceMemoryProperties(parent->job->physical,&memory);
  if(!memory.memoryTypeCount || memory.memoryTypeCount>VK_MAX_MEMORY_TYPES || !memory.memoryHeapCount || memory.memoryHeapCount>VK_MAX_MEMORY_HEAPS)return SATCHEL_VULKAN_UNSUPPORTED;
  uint32_t type=UINT32_MAX;
  for(uint32_t i=0;i<memory.memoryTypeCount;i++)if((memory_required.memoryTypeBits&(1u<<i)) && memory.memoryTypes[i].heapIndex<memory.memoryHeapCount && memory_required.size<=memory.memoryHeaps[memory.memoryTypes[i].heapIndex].size){if(type==UINT32_MAX)type=i;if(memory.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT){type=i;break;}}
  if(type==UINT32_MAX)return SATCHEL_VULKAN_UNSUPPORTED;
  VkMemoryDedicatedAllocateInfo dedicated_allocation={VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};dedicated_allocation.image=resource->image;
  VkMemoryAllocateInfo allocation={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=memory_required.size;allocation.memoryTypeIndex=type;if(dedicated.requiresDedicatedAllocation || dedicated.prefersDedicatedAllocation)allocation.pNext=&dedicated_allocation;
  if(vkAllocateMemory(resource->device,&allocation,nullptr,&resource->memory)!=VK_SUCCESS || vkBindImageMemory(resource->device,resource->image,resource->memory,0)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
  VkImageViewCreateInfo view={VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=resource->image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=target_format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  if(vkCreateImageView(resource->device,&view,nullptr,&resource->view)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
  resource->info={device,logical,memory_required.size,request.format,request.width,request.height,0,0};slot->resource=std::move(resource);slot->token=next_target++;target_allocated+=memory_required.size;*token=slot->token;*out=slot->resource->info;return SATCHEL_VULKAN_OK;
 }catch(...){return SATCHEL_VULKAN_INTERNAL_ERROR;}
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_target_info(uint64_t device,uint64_t token,SatchelVulkanTargetInfo *out){
 if(!out)return SATCHEL_VULKAN_INVALID;*out={};if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;Unlock unlock;auto *slot=find_target(device,token);if(!slot)return SATCHEL_VULKAN_INVALID;*out=slot->resource->info;return SATCHEL_VULKAN_OK;
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_target_destroy(uint64_t device,uint64_t token){
 if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;Unlock unlock;auto *slot=find_target(device,token);if(!slot)return SATCHEL_VULKAN_INVALID;if(slot->resource->info.in_flight)return SATCHEL_VULKAN_BUSY;release_target(*slot);return SATCHEL_VULKAN_OK;
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_target_stats(SatchelVulkanTargetStats *out){
 if(!out)return SATCHEL_VULKAN_INVALID;*out={};if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;Unlock unlock;out->target_limit=SATCHEL_VULKAN_TARGET_LIMIT;out->allocation_limit=SATCHEL_VULKAN_TARGET_MEMORY_LIMIT;out->max_target_bytes=SATCHEL_VULKAN_TARGET_BYTE_LIMIT;out->allocated_bytes=target_allocated;for(const auto &slot:targets)out->live_targets+=slot.token!=0;return SATCHEL_VULKAN_OK;
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_target_clear_readback(uint64_t device,uint64_t token,uint64_t buffer,uint64_t offset,uint64_t row_pitch,const float rgba[4],uint64_t *output,SatchelVulkanCancelled cancelled,void *context){
 if(!output)return SATCHEL_VULKAN_INVALID;*output=0;if(!rgba || (offset&3) || !row_pitch || (row_pitch&3))return SATCHEL_VULKAN_INVALID;float copied[4];std::copy_n(rgba,4,copied);for(float value:copied)if(!std::isfinite(value) || value<0 || value>1)return SATCHEL_VULKAN_UNSUPPORTED;
 if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;Unlock unlock;
 try{
  if(cancelled && cancelled(context))return SATCHEL_VULKAN_CANCELLED;
  auto *parent=find_device(device);auto *target=find_target(device,token);auto *destination=find_buffer(device,buffer);if(!parent || !target || !destination)return SATCHEL_VULKAN_INVALID;
  if(!(parent->caps.queue_flags&VK_QUEUE_GRAPHICS_BIT))return SATCHEL_VULKAN_UNSUPPORTED;
  auto &image=*target->resource;auto &dst=*destination->resource;
  if(parent->job->persistent_pending || image.info.in_flight || dst.in_flight)return SATCHEL_VULKAN_BUSY;
  if(!(dst.info.usage&SATCHEL_VULKAN_BUFFER_TRANSFER_DESTINATION))return SATCHEL_VULKAN_UNSUPPORTED;
  const uint64_t row_bytes=(uint64_t)image.info.width*4;
  if(row_pitch<row_bytes || row_pitch>UINT32_MAX*UINT64_C(4) || offset>dst.info.size)return SATCHEL_VULKAN_INVALID;
  const uint64_t available=dst.info.size-offset,rows=image.info.height-1;
  if(row_bytes>available || (rows && row_pitch>(available-row_bytes)/rows))return SATCHEL_VULKAN_INVALID;
  const uint64_t span=rows*row_pitch+row_bytes;
  Submission **slot=nullptr;for(auto &candidate:submissions)if(!candidate){slot=&candidate;break;}if(!slot || next_submission==(UINT64_C(1)<<63))return SATCHEL_VULKAN_BUSY;
  auto job=std::make_unique<Submission>();job->device=parent->job->device;job->parent=device;job->target=token;job->retained_buffers[0]=buffer;job->retained_count=1;
  VkAttachmentDescription attachment={};attachment.format=target_format;attachment.samples=VK_SAMPLE_COUNT_1_BIT;attachment.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;attachment.initialLayout=image.layout;attachment.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
  VkAttachmentReference color={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription subpass={};subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=1;subpass.pColorAttachments=&color;
  VkSubpassDependency dependencies[2]={};dependencies[0].srcSubpass=VK_SUBPASS_EXTERNAL;dependencies[0].dstSubpass=0;dependencies[0].srcStageMask=image.layout==VK_IMAGE_LAYOUT_UNDEFINED?VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:VK_PIPELINE_STAGE_TRANSFER_BIT;dependencies[0].srcAccessMask=image.layout==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_TRANSFER_READ_BIT;dependencies[0].dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dependencies[0].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
  dependencies[1].srcSubpass=0;dependencies[1].dstSubpass=VK_SUBPASS_EXTERNAL;dependencies[1].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dependencies[1].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;dependencies[1].dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;dependencies[1].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
  VkRenderPassCreateInfo render_pass={VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};render_pass.attachmentCount=1;render_pass.pAttachments=&attachment;render_pass.subpassCount=1;render_pass.pSubpasses=&subpass;render_pass.dependencyCount=2;render_pass.pDependencies=dependencies;
  if(vkCreateRenderPass(job->device,&render_pass,nullptr,&job->render_pass)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
  VkFramebufferCreateInfo framebuffer={VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};framebuffer.renderPass=job->render_pass;framebuffer.attachmentCount=1;framebuffer.pAttachments=&image.view;framebuffer.width=image.info.width;framebuffer.height=image.info.height;framebuffer.layers=1;
  if(SATCHEL_TARGET_CREATE_FRAMEBUFFER(job->device,&framebuffer,nullptr,&job->framebuffer)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
  VkCommandPoolCreateInfo pool={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool.queueFamilyIndex=parent->job->queue_family;if(vkCreateCommandPool(job->device,&pool,nullptr,&job->pool)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
  VkCommandBufferAllocateInfo allocate={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};allocate.commandPool=job->pool;allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate.commandBufferCount=1;VkCommandBuffer command=VK_NULL_HANDLE;if(vkAllocateCommandBuffers(job->device,&allocate,&command)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
  VkCommandBufferBeginInfo begin={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;if(vkBeginCommandBuffer(command,&begin)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
  VkBufferMemoryBarrier barrier={VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.buffer=dst.buffer;barrier.offset=offset;barrier.size=span;
  vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,1,&barrier,0,nullptr);
  VkClearValue clear={};std::copy_n(copied,4,clear.color.float32);VkRenderPassBeginInfo render={VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};render.renderPass=job->render_pass;render.framebuffer=job->framebuffer;render.renderArea.extent={image.info.width,image.info.height};render.clearValueCount=1;render.pClearValues=&clear;
  vkCmdBeginRenderPass(command,&render,VK_SUBPASS_CONTENTS_INLINE);vkCmdEndRenderPass(command);
  VkBufferImageCopy copy={};copy.bufferOffset=offset;copy.bufferRowLength=(uint32_t)(row_pitch/4);copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={image.info.width,image.info.height,1};vkCmdCopyImageToBuffer(command,image.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,dst.buffer,1,&copy);
  barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&barrier,0,nullptr);
  if(vkEndCommandBuffer(command)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;VkFenceCreateInfo fence={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};if(vkCreateFence(job->device,&fence,nullptr,&job->fence)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
  if(cancelled && cancelled(context))return SATCHEL_VULKAN_CANCELLED;
  job->token=next_submission++;job->attempted=true;image.info.in_flight=1;dst.in_flight=true;parent->job->persistent_pending=true;*slot=job.release();*output=(*slot)->token;
  VkSubmitInfo submit={VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;return SATCHEL_TARGET_QUEUE_SUBMIT(parent->job->queue,1,&submit,(*slot)->fence)==VK_SUCCESS?SATCHEL_VULKAN_OK:SATCHEL_VULKAN_API_ERROR;
 }catch(...){return SATCHEL_VULKAN_INTERNAL_ERROR;}
}
