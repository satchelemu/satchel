#ifndef SATCHEL_RASTER_CREATE_PIPELINES
#define SATCHEL_RASTER_CREATE_PIPELINES vkCreateGraphicsPipelines
#endif
#ifndef SATCHEL_RASTER_QUEUE_SUBMIT
#define SATCHEL_RASTER_QUEUE_SUBMIT vkQueueSubmit
#endif
namespace {
VkResult raster_pass(VkDevice device,VkImageLayout initial,VkRenderPass *out,bool clear=true){
 VkAttachmentDescription attachment={};attachment.format=target_format;attachment.samples=VK_SAMPLE_COUNT_1_BIT;attachment.loadOp=clear?VK_ATTACHMENT_LOAD_OP_CLEAR:VK_ATTACHMENT_LOAD_OP_LOAD;attachment.storeOp=VK_ATTACHMENT_STORE_OP_STORE;attachment.stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachment.stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;attachment.initialLayout=initial;attachment.finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
 VkAttachmentReference color={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};VkSubpassDescription subpass={};subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=1;subpass.pColorAttachments=&color;
 VkSubpassDependency dependencies[2]={};dependencies[0].srcSubpass=VK_SUBPASS_EXTERNAL;dependencies[0].dstSubpass=0;dependencies[0].srcStageMask=initial==VK_IMAGE_LAYOUT_UNDEFINED?VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT:(VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);dependencies[0].srcAccessMask=initial==VK_IMAGE_LAYOUT_UNDEFINED?0:(VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);dependencies[0].dstStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dependencies[0].dstAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT|(clear?0:VK_ACCESS_COLOR_ATTACHMENT_READ_BIT);
 dependencies[1].srcSubpass=0;dependencies[1].dstSubpass=VK_SUBPASS_EXTERNAL;dependencies[1].srcStageMask=VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;dependencies[1].srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;dependencies[1].dstStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;dependencies[1].dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
 VkRenderPassCreateInfo create={VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};create.attachmentCount=1;create.pAttachments=&attachment;create.subpassCount=1;create.pSubpasses=&subpass;create.dependencyCount=2;create.pDependencies=dependencies;return vkCreateRenderPass(device,&create,nullptr,out);
}
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_raster_create(uint64_t device,const SatchelVulkanRasterDescription *input,const SatchelRasterModule *vs,const SatchelRasterModule *fs,uint64_t *token,SatchelVulkanRasterInfo *out){
 if(!token || !out)return SATCHEL_VULKAN_INVALID;*token=0;*out={};if(!input || !vs || !fs)return SATCHEL_VULKAN_INVALID;
 const auto request=*input;const auto vertex=*vs,fragment=*fs;
 if(request.version!=1 || request.size!=sizeof(request))return SATCHEL_VULKAN_INVALID;
 if(!raster_state(request.state) || request.format!=1 || request.samples!=1 || request.topology!=SATCHEL_VULKAN_RASTER_TRIANGLE_LIST || request.primitive_restart || request.cull_mode || request.front_face>1 || request.depth_test || request.depth_write || request.stencil_test || request.blend_enable || request.color_mask!=15 || request.polygon_mode || request.rasterizer_discard || request.depth_clamp || request.depth_bias || request.logic_op_enable || request.sample_mask!=1 || request.sample_shading || request.alpha_to_one || !request.vertex_stride || (request.vertex_stride&3) || !raster_schema(vertex,1) || !raster_schema(fragment,5))return SATCHEL_VULKAN_UNSUPPORTED;
 if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;Unlock unlock;
 try{
  auto *parent=find_device(device);if(!parent)return SATCHEL_VULKAN_INVALID;if(!(parent->caps.queue_flags&VK_QUEUE_GRAPHICS_BIT))return SATCHEL_VULKAN_UNSUPPORTED;
  VkPhysicalDeviceProperties props={};vkGetPhysicalDeviceProperties(parent->job->physical,&props);const auto &limits=props.limits;
  if(props.apiVersion<VK_API_VERSION_1_1 || !limits.maxVertexInputBindings || request.vertex_stride>limits.maxVertexInputBindingStride || limits.maxVertexOutputComponents<4 || limits.maxFragmentOutputAttachments<1)return SATCHEL_VULKAN_UNSUPPORTED;
  VkFormatProperties format={};vkGetPhysicalDeviceFormatProperties(parent->job->physical,VK_FORMAT_R32G32B32A32_SFLOAT,&format);if(!(format.bufferFeatures&VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT))return SATCHEL_VULKAN_UNSUPPORTED;
  SatchelVulkanTargetCaps caps={};auto status=target_caps(*parent,request.format,caps);if(status!=SATCHEL_VULKAN_OK)return status;if(!caps.supported)return SATCHEL_VULKAN_UNSUPPORTED;
  VkVertexInputAttributeDescription attrs[8]={};uint32_t count=0,mask=0;
  for(unsigned i=0;i<8;i++){
   const uint32_t offset=request.attribute_offsets[i];if(!vertex.reflection.input_components[i]){if(offset!=UINT32_MAX)return SATCHEL_VULKAN_UNSUPPORTED;continue;}
   if(i>=limits.maxVertexInputAttributes || offset>limits.maxVertexInputAttributeOffset || (offset&3) || offset>request.vertex_stride || request.vertex_stride-offset<16)return SATCHEL_VULKAN_UNSUPPORTED;
   attrs[count++]={i,0,VK_FORMAT_R32G32B32A32_SFLOAT,offset};mask|=1u<<i;
  }
  if(!count || count>limits.maxVertexInputAttributes)return SATCHEL_VULKAN_UNSUPPORTED;
  RasterSlot *slot=nullptr;for(auto &candidate:raster_pipelines)if(!candidate.token){slot=&candidate;break;}if(!slot || next_raster==(UINT64_C(1)<<60))return SATCHEL_VULKAN_BUSY;
  auto resource=std::make_unique<RasterResource>();resource->parent=device;resource->device=parent->job->device;resource->description=request;resource->info={device,0,mask,request.vertex_stride};
  VkShaderModuleCreateInfo shader={VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};shader.codeSize=vertex.reflection.spirv_words*4;shader.pCode=vertex.words;if(vkCreateShaderModule(resource->device,&shader,nullptr,&resource->vertex)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
  shader.codeSize=fragment.reflection.spirv_words*4;shader.pCode=fragment.words;if(vkCreateShaderModule(resource->device,&shader,nullptr,&resource->fragment)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
  VkPipelineLayoutCreateInfo layout={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};if(vkCreatePipelineLayout(resource->device,&layout,nullptr,&resource->layout)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
  if(raster_pass(resource->device,VK_IMAGE_LAYOUT_UNDEFINED,&resource->pass)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
  VkPipelineShaderStageCreateInfo stages[2]={{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO},{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};stages[0].stage=VK_SHADER_STAGE_VERTEX_BIT;stages[0].module=resource->vertex;stages[0].pName="main";stages[1].stage=VK_SHADER_STAGE_FRAGMENT_BIT;stages[1].module=resource->fragment;stages[1].pName="main";
  VkVertexInputBindingDescription binding={0,request.vertex_stride,VK_VERTEX_INPUT_RATE_VERTEX};VkPipelineVertexInputStateCreateInfo vertex_input={VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};vertex_input.vertexBindingDescriptionCount=1;vertex_input.pVertexBindingDescriptions=&binding;vertex_input.vertexAttributeDescriptionCount=count;vertex_input.pVertexAttributeDescriptions=attrs;
  VkPipelineInputAssemblyStateCreateInfo assembly={VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
  VkPipelineViewportStateCreateInfo viewport={VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};viewport.viewportCount=1;viewport.scissorCount=1;
  VkPipelineRasterizationStateCreateInfo raster={VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=VK_CULL_MODE_NONE;raster.frontFace=request.front_face?VK_FRONT_FACE_CLOCKWISE:VK_FRONT_FACE_COUNTER_CLOCKWISE;raster.lineWidth=1;
  VkPipelineMultisampleStateCreateInfo sample={VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};sample.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;sample.pSampleMask=&request.sample_mask;
  VkPipelineColorBlendAttachmentState blend={};blend.colorWriteMask=VK_COLOR_COMPONENT_R_BIT|VK_COLOR_COMPONENT_G_BIT|VK_COLOR_COMPONENT_B_BIT|VK_COLOR_COMPONENT_A_BIT;VkPipelineColorBlendStateCreateInfo blending={VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};blending.attachmentCount=1;blending.pAttachments=&blend;
  const VkDynamicState dynamic_values[]={VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};VkPipelineDynamicStateCreateInfo dynamic={VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};dynamic.dynamicStateCount=2;dynamic.pDynamicStates=dynamic_values;
  VkGraphicsPipelineCreateInfo create={VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};create.stageCount=2;create.pStages=stages;create.pVertexInputState=&vertex_input;create.pInputAssemblyState=&assembly;create.pViewportState=&viewport;create.pRasterizationState=&raster;create.pMultisampleState=&sample;create.pColorBlendState=&blending;create.pDynamicState=&dynamic;create.layout=resource->layout;create.renderPass=resource->pass;
  if(SATCHEL_RASTER_CREATE_PIPELINES(resource->device,VK_NULL_HANDLE,1,&create,nullptr,&resource->pipeline)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
  slot->resource=std::move(resource);slot->token=next_raster++;*token=slot->token;*out=slot->resource->info;return SATCHEL_VULKAN_OK;
 }catch(...){return SATCHEL_VULKAN_INTERNAL_ERROR;}
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_raster_info(uint64_t device,uint64_t token,SatchelVulkanRasterInfo *out){if(!out)return SATCHEL_VULKAN_INVALID;*out={};if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;Unlock unlock;auto *slot=find_raster(device,token);if(!slot)return SATCHEL_VULKAN_INVALID;*out=slot->resource->info;return SATCHEL_VULKAN_OK;}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_raster_destroy(uint64_t device,uint64_t token){if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;Unlock unlock;auto *slot=find_raster(device,token);if(!slot)return SATCHEL_VULKAN_INVALID;if(slot->resource->info.references)return SATCHEL_VULKAN_BUSY;slot->resource.reset();slot->token=0;return SATCHEL_VULKAN_OK;}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_raster_stats(SatchelVulkanRasterStats *out){if(!out)return SATCHEL_VULKAN_INVALID;*out={};if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;Unlock unlock;out->pipeline_limit=SATCHEL_VULKAN_RASTER_PIPELINE_LIMIT;for(auto &slot:raster_pipelines)out->live_pipelines+=slot.token!=0;return SATCHEL_VULKAN_OK;}

extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_raster_draw_readback(uint64_t device,uint64_t pipeline_token,uint64_t token,const SatchelVulkanRasterDraw *input,uint64_t *output,SatchelVulkanCancelled cancelled,void *context){
 if(!output)return SATCHEL_VULKAN_INVALID;*output=0;if(!input)return SATCHEL_VULKAN_INVALID;const auto request=*input;
 if(request.version!=1 || request.size!=sizeof(request) || request.first_vertex || request.vertex_count<3 || request.vertex_count>SATCHEL_VULKAN_RASTER_VERTEX_LIMIT || request.vertex_count%3 || (request.vertex_offset&3))return SATCHEL_VULKAN_INVALID;
 const uint64_t buffer=request.readback_buffer,offset=request.readback_offset,row_pitch=request.row_pitch;
 if((offset&3) || !row_pitch || (row_pitch&3))return SATCHEL_VULKAN_INVALID;
 float copied[4];std::copy_n(request.clear_rgba,4,copied);for(float value:copied)if(!std::isfinite(value) || value<0 || value>1)return SATCHEL_VULKAN_UNSUPPORTED;
 const float viewport_values[]={request.viewport_x,request.viewport_y,request.viewport_width,request.viewport_height,request.min_depth,request.max_depth};for(float value:viewport_values)if(!std::isfinite(value))return SATCHEL_VULKAN_UNSUPPORTED;
 if(request.viewport_x<0 || request.viewport_y<0 || request.viewport_width<=0 || request.viewport_height<=0 || request.min_depth!=0 || request.max_depth!=1 || !request.scissor_width || !request.scissor_height)return SATCHEL_VULKAN_UNSUPPORTED;
 if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;Unlock unlock;
 try{
  if(cancelled && cancelled(context))return SATCHEL_VULKAN_CANCELLED;
  auto *parent=find_device(device);auto *target=find_target(device,token);auto *destination=find_buffer(device,buffer);auto *pipeline=find_raster(device,pipeline_token);auto *vertices=find_buffer(device,request.vertex_buffer);if(!parent || !target || !destination || !pipeline || !vertices || vertices==destination)return SATCHEL_VULKAN_INVALID;
  if(!(parent->caps.queue_flags&VK_QUEUE_GRAPHICS_BIT))return SATCHEL_VULKAN_UNSUPPORTED;
  auto &image=*target->resource;auto &dst=*destination->resource;auto &vertex=*vertices->resource;auto &raster=*pipeline->resource;
  if(raster.description.format!=image.info.format)return SATCHEL_VULKAN_UNSUPPORTED;
  if(request.viewport_x+request.viewport_width>image.info.width || request.viewport_y+request.viewport_height>image.info.height || request.scissor_x>image.info.width || request.scissor_width>image.info.width-request.scissor_x || request.scissor_y>image.info.height || request.scissor_height>image.info.height-request.scissor_y)return SATCHEL_VULKAN_INVALID;
  VkPhysicalDeviceProperties properties={};vkGetPhysicalDeviceProperties(parent->job->physical,&properties);const auto &limits=properties.limits;
  if(request.viewport_width>limits.maxViewportDimensions[0] || request.viewport_height>limits.maxViewportDimensions[1] || request.viewport_x<limits.viewportBoundsRange[0] || request.viewport_y<limits.viewportBoundsRange[0] || request.viewport_x+request.viewport_width>limits.viewportBoundsRange[1] || request.viewport_y+request.viewport_height>limits.viewportBoundsRange[1])return SATCHEL_VULKAN_UNSUPPORTED;
  if(!(vertex.info.usage&SATCHEL_VULKAN_BUFFER_VERTEX))return SATCHEL_VULKAN_UNSUPPORTED;
  uint64_t end=0;for(unsigned i=0;i<8;i++)if(raster.info.attribute_mask&(1u<<i))end=std::max<uint64_t>(end,raster.description.attribute_offsets[i]+UINT64_C(16));
  const uint64_t vertex_span=(uint64_t)(request.vertex_count-1)*raster.description.vertex_stride+end;
  if(request.vertex_offset>vertex.info.size || vertex_span>vertex.info.size-request.vertex_offset)return SATCHEL_VULKAN_INVALID;
  if(parent->job->persistent_pending || image.info.in_flight || dst.in_flight || vertex.in_flight || raster.info.references)return SATCHEL_VULKAN_BUSY;
  if(!(dst.info.usage&SATCHEL_VULKAN_BUFFER_TRANSFER_DESTINATION))return SATCHEL_VULKAN_UNSUPPORTED;
  const uint64_t row_bytes=(uint64_t)image.info.width*4;
  if(row_pitch<row_bytes || row_pitch>UINT32_MAX*UINT64_C(4) || offset>dst.info.size)return SATCHEL_VULKAN_INVALID;
  const uint64_t available=dst.info.size-offset,rows=image.info.height-1;
  if(row_bytes>available || (rows && row_pitch>(available-row_bytes)/rows))return SATCHEL_VULKAN_INVALID;
  const uint64_t span=rows*row_pitch+row_bytes;
  Submission **slot=nullptr;for(auto &candidate:submissions)if(!candidate){slot=&candidate;break;}if(!slot || next_submission==(UINT64_C(1)<<63))return SATCHEL_VULKAN_BUSY;
  auto job=std::make_unique<Submission>();job->device=parent->job->device;job->parent=device;job->target=token;job->retained_buffers[0]=buffer;job->retained_count=2;job->retained_buffers[1]=request.vertex_buffer;job->raster_pipeline=pipeline_token;
  if(raster_pass(job->device,image.layout,&job->render_pass)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
  VkFramebufferCreateInfo framebuffer={VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};framebuffer.renderPass=job->render_pass;framebuffer.attachmentCount=1;framebuffer.pAttachments=&image.view;framebuffer.width=image.info.width;framebuffer.height=image.info.height;framebuffer.layers=1;
  if(vkCreateFramebuffer(job->device,&framebuffer,nullptr,&job->framebuffer)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
  VkCommandPoolCreateInfo pool={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool.queueFamilyIndex=parent->job->queue_family;if(vkCreateCommandPool(job->device,&pool,nullptr,&job->pool)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
  VkCommandBufferAllocateInfo allocate={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};allocate.commandPool=job->pool;allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate.commandBufferCount=1;VkCommandBuffer command=VK_NULL_HANDLE;if(vkAllocateCommandBuffers(job->device,&allocate,&command)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
  VkCommandBufferBeginInfo begin={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;if(vkBeginCommandBuffer(command,&begin)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
  VkBufferMemoryBarrier barrier={VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.buffer=dst.buffer;barrier.offset=offset;barrier.size=span;
  vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,1,&barrier,0,nullptr);
  VkBufferMemoryBarrier vertex_barrier={VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};vertex_barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_MEMORY_WRITE_BIT;vertex_barrier.dstAccessMask=VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;vertex_barrier.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;vertex_barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;vertex_barrier.buffer=vertex.buffer;vertex_barrier.offset=request.vertex_offset;vertex_barrier.size=vertex_span;
  vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,0,0,nullptr,1,&vertex_barrier,0,nullptr);
  raster_index_barrier(command,raster);
  VkClearValue clear={};std::copy_n(copied,4,clear.color.float32);VkRenderPassBeginInfo render={VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};render.renderPass=job->render_pass;render.framebuffer=job->framebuffer;render.renderArea.extent={image.info.width,image.info.height};render.clearValueCount=1;render.pClearValues=&clear;
  vkCmdBeginRenderPass(command,&render,VK_SUBPASS_CONTENTS_INLINE);
  vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_GRAPHICS,raster.pipeline);
  VkViewport viewport={request.viewport_x,request.viewport_y,request.viewport_width,request.viewport_height,request.min_depth,request.max_depth};VkRect2D scissor={{(int32_t)request.scissor_x,(int32_t)request.scissor_y},{request.scissor_width,request.scissor_height}};
  vkCmdSetViewport(command,0,1,&viewport);vkCmdSetScissor(command,0,1,&scissor);VkDeviceSize vertex_offset=request.vertex_offset;vkCmdBindVertexBuffers(command,0,1,&vertex.buffer,&vertex_offset);raster_draw_vertices(command,raster,request.vertex_count);vkCmdEndRenderPass(command);
  VkBufferImageCopy copy={};copy.bufferOffset=offset;copy.bufferRowLength=(uint32_t)(row_pitch/4);copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={image.info.width,image.info.height,1};vkCmdCopyImageToBuffer(command,image.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,dst.buffer,1,&copy);
  barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&barrier,0,nullptr);
  if(vkEndCommandBuffer(command)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;VkFenceCreateInfo fence={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};if(vkCreateFence(job->device,&fence,nullptr,&job->fence)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
  if(cancelled && cancelled(context))return SATCHEL_VULKAN_CANCELLED;
  job->token=next_submission++;job->attempted=true;image.info.in_flight=1;dst.in_flight=true;vertex.in_flight=true;++raster.info.references;parent->job->persistent_pending=true;*slot=job.release();*output=(*slot)->token;
  VkSubmitInfo submit={VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;return SATCHEL_RASTER_QUEUE_SUBMIT(parent->job->queue,1,&submit,(*slot)->fence)==VK_SUCCESS?SATCHEL_VULKAN_OK:SATCHEL_VULKAN_API_ERROR;
 }catch(...){return SATCHEL_VULKAN_INTERNAL_ERROR;}
}

extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_raster_capabilities(uint64_t device,SatchelVulkanRasterCaps *out){
 if(!out)return SATCHEL_VULKAN_INVALID;if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;Unlock unlock;
 auto *parent=find_device(device);if(!parent)return SATCHEL_VULKAN_INVALID;VkPhysicalDeviceProperties props={};vkGetPhysicalDeviceProperties(parent->job->physical,&props);VkFormatProperties format={};vkGetPhysicalDeviceFormatProperties(parent->job->physical,VK_FORMAT_R32G32B32A32_SFLOAT,&format);SatchelVulkanTargetCaps target={};auto s=target_caps(*parent,1,target);if(s!=SATCHEL_VULKAN_OK)return s;
 SatchelVulkanRasterCaps value={1,sizeof(value),(parent->caps.queue_flags&VK_QUEUE_GRAPHICS_BIT)?1u:0u,target.supported,(format.bufferFeatures&VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT)?1u:0u,props.limits.maxVertexInputAttributes,props.limits.maxVertexInputBindingStride,props.limits.maxVertexInputAttributeOffset};
 if(props.apiVersion<VK_API_VERSION_1_1 || !props.limits.maxVertexInputBindings || props.limits.maxVertexOutputComponents<4 || !props.limits.maxFragmentOutputAttachments)value.graphics_queue=0;*out=value;return SATCHEL_VULKAN_OK;
}
