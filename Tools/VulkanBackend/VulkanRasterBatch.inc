#ifndef SATCHEL_RASTER_BATCH_SUBMIT
#define SATCHEL_RASTER_BATCH_SUBMIT vkQueueSubmit
#endif
namespace {
bool rgba_valid(const float rgba[4]){for(unsigned i=0;i<4;i++)if(!std::isfinite(rgba[i]) || rgba[i]<0 || rgba[i]>1)return false;return true;}
bool target_buffer_span(const TargetResource &target,const BufferResource &buffer,uint64_t offset,uint64_t pitch,uint64_t &span){
 const uint64_t bytes=(uint64_t)target.info.width*4,rows=target.info.height-1;if((offset&3) || (pitch&3) || pitch<bytes || pitch>UINT32_MAX*UINT64_C(4) || offset>buffer.info.size)return false;
 const uint64_t remaining=buffer.info.size-offset;if(bytes>remaining || (rows && pitch>(remaining-bytes)/rows))return false;span=rows*pitch+bytes;return true;
}
VkResult make_commands(Submission &job,const Job &parent,VkCommandBuffer &command){
 VkCommandPoolCreateInfo pool={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool.queueFamilyIndex=parent.queue_family;auto status=vkCreateCommandPool(job.device,&pool,nullptr,&job.pool);if(status!=VK_SUCCESS)return status;
 VkCommandBufferAllocateInfo allocate={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};allocate.commandPool=job.pool;allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate.commandBufferCount=1;status=vkAllocateCommandBuffers(job.device,&allocate,&command);if(status!=VK_SUCCESS)return status;
 VkCommandBufferBeginInfo begin={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;return vkBeginCommandBuffer(command,&begin);
}
VkResult end_commands(Submission &job,VkCommandBuffer command){auto result=vkEndCommandBuffer(command);if(result!=VK_SUCCESS)return result;VkFenceCreateInfo fence={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};return vkCreateFence(job.device,&fence,nullptr,&job.fence);}
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_target_upload(uint64_t device,uint64_t token,uint64_t source,uint64_t offset,uint64_t pitch,uint64_t *output,SatchelVulkanCancelled cancel,void *context){
 if(!output)return SATCHEL_VULKAN_INVALID;*output=0;if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;Unlock unlock;
 try{
  if(cancel && cancel(context))return SATCHEL_VULKAN_CANCELLED;
  auto *parent=find_device(device);auto *target=find_target(device,token);auto *buffer=find_buffer(device,source);if(!parent || !target || !buffer)return SATCHEL_VULKAN_INVALID;
  auto &image=*target->resource;auto &src=*buffer->resource;if(parent->job->persistent_pending || image.info.in_flight || src.in_flight)return SATCHEL_VULKAN_BUSY;
  if(!(src.info.usage&SATCHEL_VULKAN_BUFFER_TRANSFER_SOURCE))return SATCHEL_VULKAN_UNSUPPORTED;uint64_t span=0;if(!target_buffer_span(image,src,offset,pitch,span))return SATCHEL_VULKAN_INVALID;
  Submission **slot=nullptr;for(auto &candidate:submissions)if(!candidate){slot=&candidate;break;}if(!slot || next_submission==(UINT64_C(1)<<63))return SATCHEL_VULKAN_BUSY;
  auto job=std::make_unique<Submission>();job->parent=device;job->device=parent->job->device;job->target=token;job->retained_buffers[0]=source;job->retained_count=1;
  VkCommandBuffer command=VK_NULL_HANDLE;if(make_commands(*job,*parent->job,command)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
  VkBufferMemoryBarrier input={VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};input.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_MEMORY_WRITE_BIT;input.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;input.srcQueueFamilyIndex=input.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;input.buffer=src.buffer;input.offset=offset;input.size=span;
  VkImageMemoryBarrier before={VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};before.srcAccessMask=image.layout==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;before.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;before.oldLayout=image.layout;before.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;before.srcQueueFamilyIndex=before.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;before.image=image.image;before.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,1,&input,1,&before);
  VkBufferImageCopy copy={};copy.bufferOffset=offset;copy.bufferRowLength=(uint32_t)(pitch/4);copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={image.info.width,image.info.height,1};vkCmdCopyBufferToImage(command,src.buffer,image.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
  VkImageMemoryBarrier after=before;after.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;after.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;after.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;after.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&after);
  if(end_commands(*job,command)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;if(cancel && cancel(context))return SATCHEL_VULKAN_CANCELLED;
  job->token=next_submission++;job->attempted=true;image.info.in_flight=1;src.in_flight=true;parent->job->persistent_pending=true;*slot=job.release();*output=(*slot)->token;
  VkSubmitInfo submit={VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;return SATCHEL_RASTER_BATCH_SUBMIT(parent->job->queue,1,&submit,(*slot)->fence)==VK_SUCCESS?SATCHEL_VULKAN_OK:SATCHEL_VULKAN_API_ERROR;
 }catch(...){return SATCHEL_VULKAN_INTERNAL_ERROR;}
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_raster_batch_readback(uint64_t device,uint64_t target_token,const SatchelVulkanRasterBatch *input,uint64_t *output,SatchelVulkanCancelled cancel,void *context){
 if(!output)return SATCHEL_VULKAN_INVALID;*output=0;if(!input)return SATCHEL_VULKAN_INVALID;const auto batch=*input;
 if(batch.version!=1 || batch.size!=sizeof(batch) || batch.command_count>SATCHEL_VULKAN_RASTER_BATCH_LIMIT)return SATCHEL_VULKAN_INVALID;
 if(batch.load_operation!=SATCHEL_VULKAN_RASTER_LOAD && batch.load_operation!=SATCHEL_VULKAN_RASTER_CLEAR)return SATCHEL_VULKAN_UNSUPPORTED;
 if(batch.load_operation==SATCHEL_VULKAN_RASTER_CLEAR && !rgba_valid(batch.initial_clear_rgba))return SATCHEL_VULKAN_UNSUPPORTED;
 if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;Unlock unlock;
 try{
  if(cancel && cancel(context))return SATCHEL_VULKAN_CANCELLED;auto *parent=find_device(device);auto *target=find_target(device,target_token);auto *output_buffer=find_buffer(device,batch.readback_buffer);if(!parent || !target || !output_buffer)return SATCHEL_VULKAN_INVALID;
  auto &image=*target->resource;auto &dst=*output_buffer->resource;if(parent->job->persistent_pending || image.info.in_flight || dst.in_flight)return SATCHEL_VULKAN_BUSY;
  if(!(parent->caps.queue_flags&VK_QUEUE_GRAPHICS_BIT) || !(dst.info.usage&SATCHEL_VULKAN_BUFFER_TRANSFER_DESTINATION))return SATCHEL_VULKAN_UNSUPPORTED;
  if(batch.load_operation==SATCHEL_VULKAN_RASTER_LOAD && !image.info.initialized)return SATCHEL_VULKAN_UNSUPPORTED;
  uint64_t span=0;if(!target_buffer_span(image,dst,batch.readback_offset,batch.row_pitch,span))return SATCHEL_VULKAN_INVALID;
  VkPhysicalDeviceProperties props={};vkGetPhysicalDeviceProperties(parent->job->physical,&props);const auto &limits=props.limits;
  RasterSlot *pipelines[SATCHEL_VULKAN_RASTER_PIPELINE_LIMIT]={};BufferSlot *buffers[SATCHEL_VULKAN_BUFFER_LIMIT]={output_buffer};uint64_t vertex_spans[SATCHEL_VULKAN_RASTER_BATCH_LIMIT]={};unsigned np=0,nb=1;
  for(unsigned i=0;i<batch.command_count;i++){
   const auto &c=batch.commands[i];if(c.operation==SATCHEL_VULKAN_RASTER_OP_CLEAR){if(!rgba_valid(c.clear_rgba))return SATCHEL_VULKAN_UNSUPPORTED;continue;}if(c.operation!=SATCHEL_VULKAN_RASTER_OP_DRAW)return SATCHEL_VULKAN_UNSUPPORTED;
   auto *pipeline=find_raster(device,c.pipeline);auto *buffer=find_buffer(device,c.vertex_buffer);if(!pipeline || !buffer || buffer==output_buffer)return SATCHEL_VULKAN_INVALID;
   const auto &raster=*pipeline->resource;const auto &vertex=*buffer->resource;if(raster.info.references || vertex.in_flight)return SATCHEL_VULKAN_BUSY;if(raster.description.format!=image.info.format || !(vertex.info.usage&SATCHEL_VULKAN_BUFFER_VERTEX))return SATCHEL_VULKAN_UNSUPPORTED;
   if(c.first_vertex || c.vertex_count<3 || c.vertex_count>SATCHEL_VULKAN_RASTER_VERTEX_LIMIT || c.vertex_count%3 || (c.vertex_offset&3))return SATCHEL_VULKAN_INVALID;
   const float values[]={c.viewport_x,c.viewport_y,c.viewport_width,c.viewport_height,c.min_depth,c.max_depth};for(float v:values)if(!std::isfinite(v))return SATCHEL_VULKAN_UNSUPPORTED;
   if(c.viewport_x<0 || c.viewport_y<0 || c.viewport_width<=0 || c.viewport_height<=0 || c.min_depth!=0 || c.max_depth!=1 || !c.scissor_width || !c.scissor_height)return SATCHEL_VULKAN_UNSUPPORTED;
   if(c.viewport_x+c.viewport_width>image.info.width || c.viewport_y+c.viewport_height>image.info.height || c.scissor_x>image.info.width || c.scissor_width>image.info.width-c.scissor_x || c.scissor_y>image.info.height || c.scissor_height>image.info.height-c.scissor_y)return SATCHEL_VULKAN_INVALID;
   if(c.viewport_width>limits.maxViewportDimensions[0] || c.viewport_height>limits.maxViewportDimensions[1] || c.viewport_x<limits.viewportBoundsRange[0] || c.viewport_y<limits.viewportBoundsRange[0] || c.viewport_x+c.viewport_width>limits.viewportBoundsRange[1] || c.viewport_y+c.viewport_height>limits.viewportBoundsRange[1])return SATCHEL_VULKAN_UNSUPPORTED;
   uint64_t last=0;for(unsigned j=0;j<8;j++)if(raster.info.attribute_mask&(1u<<j))last=std::max<uint64_t>(last,raster.description.attribute_offsets[j]+UINT64_C(16));vertex_spans[i]=(uint64_t)(c.vertex_count-1)*raster.description.vertex_stride+last;
   if(c.vertex_offset>vertex.info.size || vertex_spans[i]>vertex.info.size-c.vertex_offset)return SATCHEL_VULKAN_INVALID;
   unsigned k=0;for(;k<np;k++)if(pipelines[k]==pipeline)break;if(k==np){if(np==SATCHEL_VULKAN_RASTER_PIPELINE_LIMIT)return SATCHEL_VULKAN_UNSUPPORTED;pipelines[np++]=pipeline;}
   k=0;for(;k<nb;k++)if(buffers[k]==buffer)break;if(k==nb){if(nb==SATCHEL_VULKAN_BUFFER_LIMIT)return SATCHEL_VULKAN_UNSUPPORTED;buffers[nb++]=buffer;}
  }
  Submission **slot=nullptr;for(auto &candidate:submissions)if(!candidate){slot=&candidate;break;}if(!slot || next_submission==(UINT64_C(1)<<63))return SATCHEL_VULKAN_BUSY;
  auto job=std::make_unique<Submission>();job->parent=device;job->device=parent->job->device;job->target=target_token;job->retained_count=nb;for(unsigned i=0;i<nb;i++)job->retained_buffers[i]=buffers[i]->token;job->raster_count=np;for(unsigned i=0;i<np;i++)job->raster_pipelines[i]=pipelines[i]->token;
  if(raster_pass(job->device,image.layout,&job->render_pass,batch.load_operation==SATCHEL_VULKAN_RASTER_CLEAR)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
  VkFramebufferCreateInfo framebuffer={VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};framebuffer.renderPass=job->render_pass;framebuffer.attachmentCount=1;framebuffer.pAttachments=&image.view;framebuffer.width=image.info.width;framebuffer.height=image.info.height;framebuffer.layers=1;if(vkCreateFramebuffer(job->device,&framebuffer,nullptr,&job->framebuffer)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
  VkCommandBuffer command=VK_NULL_HANDLE;if(make_commands(*job,*parent->job,command)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
  VkMemoryBarrier memory={VK_STRUCTURE_TYPE_MEMORY_BARRIER};memory.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_MEMORY_WRITE_BIT;memory.dstAccessMask=VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT|VK_ACCESS_INDEX_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_VERTEX_INPUT_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&memory,0,nullptr,0,nullptr);
  VkClearValue clear={};std::copy_n(batch.initial_clear_rgba,4,clear.color.float32);VkRenderPassBeginInfo begin={VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=job->render_pass;begin.framebuffer=job->framebuffer;begin.renderArea.extent={image.info.width,image.info.height};begin.clearValueCount=1;begin.pClearValues=&clear;vkCmdBeginRenderPass(command,&begin,VK_SUBPASS_CONTENTS_INLINE);
  for(unsigned i=0;i<batch.command_count;i++){
   const auto &c=batch.commands[i];if(c.operation==SATCHEL_VULKAN_RASTER_OP_CLEAR){VkClearAttachment attachment={};attachment.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;std::copy_n(c.clear_rgba,4,attachment.clearValue.color.float32);VkClearRect rect={{{0,0},{image.info.width,image.info.height}},0,1};vkCmdClearAttachments(command,1,&attachment,1,&rect);continue;}
   auto *pipeline=find_raster(device,c.pipeline);auto *buffer=find_buffer(device,c.vertex_buffer);VkViewport viewport={c.viewport_x,c.viewport_y,c.viewport_width,c.viewport_height,c.min_depth,c.max_depth};VkRect2D scissor={{(int32_t)c.scissor_x,(int32_t)c.scissor_y},{c.scissor_width,c.scissor_height}};VkDeviceSize offset=c.vertex_offset;
   vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline->resource->pipeline);vkCmdSetViewport(command,0,1,&viewport);vkCmdSetScissor(command,0,1,&scissor);vkCmdBindVertexBuffers(command,0,1,&buffer->resource->buffer,&offset);raster_draw_vertices(command,*pipeline->resource,c.vertex_count);
  }
  vkCmdEndRenderPass(command);VkBufferImageCopy copy={};copy.bufferOffset=batch.readback_offset;copy.bufferRowLength=(uint32_t)(batch.row_pitch/4);copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={image.info.width,image.info.height,1};vkCmdCopyImageToBuffer(command,image.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,dst.buffer,1,&copy);
  VkBufferMemoryBarrier readback={VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};readback.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;readback.dstAccessMask=VK_ACCESS_HOST_READ_BIT;readback.srcQueueFamilyIndex=readback.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;readback.buffer=dst.buffer;readback.offset=batch.readback_offset;readback.size=span;vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&readback,0,nullptr);
  if(end_commands(*job,command)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;if(cancel && cancel(context))return SATCHEL_VULKAN_CANCELLED;
  job->token=next_submission++;job->attempted=true;image.info.in_flight=1;for(unsigned i=0;i<nb;i++)buffers[i]->resource->in_flight=true;for(unsigned i=0;i<np;i++)++pipelines[i]->resource->info.references;parent->job->persistent_pending=true;*slot=job.release();*output=(*slot)->token;
  VkSubmitInfo submit={VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;return SATCHEL_RASTER_BATCH_SUBMIT(parent->job->queue,1,&submit,(*slot)->fence)==VK_SUCCESS?SATCHEL_VULKAN_OK:SATCHEL_VULKAN_API_ERROR;
 }catch(...){return SATCHEL_VULKAN_INTERNAL_ERROR;}
}
