// Included by VulkanBackend.cpp: uses the same owned device/buffer/job registries
// and serialization gate, with no duplicated Vulkan instance or private tokens.
namespace {
#include "ComputeOriginal.inc"
SatchelVulkanComputeLimits compute_limits(const DeviceSlot &parent){
    VkPhysicalDeviceProperties properties={};vkGetPhysicalDeviceProperties(parent.job->physical,&properties);const auto &l=properties.limits;
    SatchelVulkanComputeLimits result={};result.version=1;result.struct_size=sizeof(result);
    result.max_uniform_range=l.maxUniformBufferRange;result.max_storage_range=l.maxStorageBufferRange;
    result.max_set_uniform_buffers=l.maxDescriptorSetUniformBuffers;result.max_set_storage_buffers=l.maxDescriptorSetStorageBuffers;
    result.max_stage_uniform_buffers=l.maxPerStageDescriptorUniformBuffers;result.max_stage_storage_buffers=l.maxPerStageDescriptorStorageBuffers;
    result.max_stage_resources=l.maxPerStageResources;result.max_bound_sets=l.maxBoundDescriptorSets;
    result.precise_math_requested=1;result.uniform_offset_alignment=l.minUniformBufferOffsetAlignment;result.storage_offset_alignment=l.minStorageBufferOffsetAlignment;return result;
}
bool admitted_original(const std::vector<uint32_t> &code,const SatchelVulkanComputeProfile &profile){
    if(profile.version!=1 || profile.struct_size!=sizeof(profile) || profile.uniform_binding<1 || profile.uniform_binding>7 ||
        profile.storage_binding<1 || profile.storage_binding>7 || profile.uniform_binding==profile.storage_binding || code.size()!=sizeof(original_compute_admission)/4)return false;
    for(size_t i=0;i<code.size();i++){
        const uint32_t expected=i==original_uniform_binding_word?profile.uniform_binding:(i==original_storage_binding_word?profile.storage_binding:original_compute_admission[i]);
        if(code[i]!=expected)return false;
    }return true;
}
bool supported_original(const DeviceSlot &parent,const SatchelVulkanComputeLimits &l){
    return parent.caps.api_version>=VK_API_VERSION_1_1 && (parent.caps.queue_flags&VK_QUEUE_COMPUTE_BIT) && l.max_uniform_range>=SATCHEL_VULKAN_COMPUTE_UNIFORM_BYTES &&
        l.max_storage_range>=16 && l.max_set_uniform_buffers>=1 && l.max_set_storage_buffers>=1 && l.max_stage_uniform_buffers>=1 && l.max_stage_storage_buffers>=1 && l.max_stage_resources>=2 && l.max_bound_sets>=1 &&
        l.uniform_offset_alignment && l.storage_offset_alignment && parent.caps.max_compute_invocations>=1 &&
        parent.caps.max_compute_group_size[0]>=1 && parent.caps.max_compute_group_size[1]>=1 && parent.caps.max_compute_group_size[2]>=1 &&
        parent.caps.max_compute_group_count[0]>=1 && parent.caps.max_compute_group_count[1]>=1 && parent.caps.max_compute_group_count[2]>=1;
}
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_compute_limits(uint64_t device,SatchelVulkanComputeLimits *out){
    if(!out)return SATCHEL_VULKAN_INVALID;*out={};if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;Unlock unlock;
    auto *parent=find_device(device);if(!parent)return SATCHEL_VULKAN_INVALID;*out=compute_limits(*parent);return SATCHEL_VULKAN_OK;
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_compute_pipeline_create_original(uint64_t device,const void *spirv,size_t size,
    const SatchelVulkanComputeProfile *input,uint64_t *output,SatchelVulkanCancelled cancelled,void *context){
    if(!output)return SATCHEL_VULKAN_INVALID;
    if(!spirv || !input || size!=sizeof(original_compute_admission) || size>SATCHEL_VULKAN_COMPUTE_SPIRV_LIMIT){*output=0;return SATCHEL_VULKAN_INVALID;}
    try {
        const SatchelVulkanComputeProfile profile=*input;std::vector<uint32_t> code(size/4);std::memcpy(code.data(),spirv,size);*output=0;
        if(!admitted_original(code,profile))return SATCHEL_VULKAN_UNSUPPORTED;
        if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;Unlock unlock;
        if(cancelled && cancelled(context))return SATCHEL_VULKAN_CANCELLED;
        auto *parent=find_device(device);if(!parent)return SATCHEL_VULKAN_INVALID;
        if(!supported_original(*parent,compute_limits(*parent)))return SATCHEL_VULKAN_UNSUPPORTED;
        ComputePipelineSlot *slot=nullptr;for(auto &candidate:compute_pipelines)if(!candidate.token){slot=&candidate;break;}
        if(!slot || next_compute_pipeline==(UINT64_C(1)<<62))return SATCHEL_VULKAN_BUSY;
        auto pipeline=std::make_unique<ComputePipelineResource>();pipeline->parent=device;pipeline->device=parent->job->device;
        pipeline->uniform_binding=profile.uniform_binding;pipeline->storage_binding=profile.storage_binding;
        VkShaderModuleCreateInfo module={VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};module.codeSize=size;module.pCode=code.data();
        if(vkCreateShaderModule(pipeline->device,&module,nullptr,&pipeline->shader)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
        VkDescriptorSetLayoutBinding bindings[2]={{profile.uniform_binding,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
            {profile.storage_binding,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}};
        VkDescriptorSetLayoutCreateInfo descriptor={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};descriptor.bindingCount=2;descriptor.pBindings=bindings;
        if(vkCreateDescriptorSetLayout(pipeline->device,&descriptor,nullptr,&pipeline->descriptor_layout)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
        VkPipelineLayoutCreateInfo layout={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=1;layout.pSetLayouts=&pipeline->descriptor_layout;
        if(vkCreatePipelineLayout(pipeline->device,&layout,nullptr,&pipeline->layout)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
        if(cancelled && cancelled(context))return SATCHEL_VULKAN_CANCELLED;
        VkComputePipelineCreateInfo create={VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};create.layout=pipeline->layout;
        create.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;create.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;create.stage.module=pipeline->shader;create.stage.pName="main";
        if(vkCreateComputePipelines(pipeline->device,VK_NULL_HANDLE,1,&create,nullptr,&pipeline->pipeline)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
        if(cancelled && cancelled(context))return SATCHEL_VULKAN_CANCELLED;
        slot->token=next_compute_pipeline++;slot->resource=std::move(pipeline);*output=slot->token;return SATCHEL_VULKAN_OK;
    }catch(...){*output=0;return SATCHEL_VULKAN_INTERNAL_ERROR;}
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_compute_pipeline_destroy(uint64_t device,uint64_t token){
    if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;Unlock unlock;auto *pipeline=find_compute_pipeline(device,token);
    if(!pipeline)return SATCHEL_VULKAN_INVALID;if(pipeline->resource->references)return SATCHEL_VULKAN_BUSY;pipeline->resource.reset();pipeline->token=0;return SATCHEL_VULKAN_OK;
}
extern "C" __attribute__((visibility("default"))) uint32_t satchel_vulkan_compute_pipeline_live_count(void){
    if(gate.test_and_set(std::memory_order_acquire))return UINT32_MAX;Unlock unlock;uint32_t count=0;for(const auto &slot:compute_pipelines)count+=slot.token!=0;return count;
}
static SatchelVulkanStatus submit_compute(uint64_t device,uint64_t token,const SatchelVulkanComputeBindings *input,
    uint64_t *output,SatchelVulkanCancelled cancelled,void *context,bool scalar,uint64_t guest_address){
    if(!output)return SATCHEL_VULKAN_INVALID;if(!input){*output=0;return SATCHEL_VULKAN_INVALID;}const SatchelVulkanComputeBindings binding=*input;*output=0;
    if(binding.uniform_size!=SATCHEL_VULKAN_COMPUTE_UNIFORM_BYTES || binding.storage_size<(scalar?4u:16u) || binding.storage_size>(scalar?1048576u:4096u) || (binding.storage_size&3) || binding.uniform_buffer==binding.storage_buffer)return SATCHEL_VULKAN_INVALID;
    if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;Unlock unlock;
    try {
        if(cancelled && cancelled(context))return SATCHEL_VULKAN_CANCELLED;
        auto *parent=find_device(device);auto *pipeline=find_compute_pipeline(device,token);auto *uniform=find_buffer(device,binding.uniform_buffer);auto *storage=find_buffer(device,binding.storage_buffer);
        if(!parent || !pipeline || !uniform || !storage)return SATCHEL_VULKAN_INVALID;
        if(pipeline->resource->scalar!=scalar)return SATCHEL_VULKAN_UNSUPPORTED;
        if(parent->job->persistent_pending || uniform->resource->in_flight || storage->resource->in_flight)return SATCHEL_VULKAN_BUSY;
        if(!(uniform->resource->info.usage&SATCHEL_VULKAN_BUFFER_UNIFORM) || !(storage->resource->info.usage&SATCHEL_VULKAN_BUFFER_STORAGE))return SATCHEL_VULKAN_UNSUPPORTED;
        const auto limits=compute_limits(*parent);
        if(!supported_original(*parent,limits) || binding.uniform_offset%limits.uniform_offset_alignment || binding.storage_offset%limits.storage_offset_alignment || binding.uniform_size>limits.max_uniform_range || binding.storage_size>limits.max_storage_range)return SATCHEL_VULKAN_UNSUPPORTED;
        if(binding.uniform_offset>uniform->resource->info.size || binding.uniform_size>uniform->resource->info.size-binding.uniform_offset || binding.storage_offset>storage->resource->info.size || binding.storage_size>storage->resource->info.size-binding.storage_offset)return SATCHEL_VULKAN_INVALID;
        if(scalar){const auto &r=pipeline->resource->scalar_reflection;
            if(guest_address!=r.expected_storage_address || binding.storage_size<r.storage_required_bytes || guest_address%limits.storage_offset_alignment || guest_address>=(UINT64_C(1)<<40) || binding.storage_size>(UINT64_C(1)<<40)-guest_address)return SATCHEL_VULKAN_UNSUPPORTED;
            const SatchelVulkanStatus synced=sync_buffer(*uniform->resource,true);if(synced!=SATCHEL_VULKAN_OK)return synced;
            uint64_t pointer=0;const auto *bytes=(const uint8_t*)uniform->resource->mapped+binding.uniform_offset+r.pointer_cb_offset;for(unsigned i=0;i<8;i++)pointer|=(uint64_t)bytes[i]<<(i*8);
            if(pointer!=guest_address)return SATCHEL_VULKAN_UNSUPPORTED;
        }
        Submission **slot=nullptr;for(auto &candidate:submissions)if(!candidate){slot=&candidate;break;}
        if(!slot || next_submission==(UINT64_C(1)<<63) || pipeline->resource->references==UINT32_MAX)return SATCHEL_VULKAN_BUSY;
        auto job=std::make_unique<Submission>();job->device=parent->job->device;job->parent=device;job->retained_count=2;job->retained_buffers[0]=binding.uniform_buffer;job->retained_buffers[1]=binding.storage_buffer;job->compute_pipeline=token;
        VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}};
        VkDescriptorPoolCreateInfo descriptors={VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};descriptors.maxSets=1;descriptors.poolSizeCount=2;descriptors.pPoolSizes=sizes;
        if(vkCreateDescriptorPool(job->device,&descriptors,nullptr,&job->descriptors)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
        VkDescriptorSetAllocateInfo allocate_set={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};allocate_set.descriptorPool=job->descriptors;allocate_set.descriptorSetCount=1;allocate_set.pSetLayouts=&pipeline->resource->descriptor_layout;
        VkDescriptorSet set=VK_NULL_HANDLE;if(vkAllocateDescriptorSets(job->device,&allocate_set,&set)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
        VkDescriptorBufferInfo buffers_info[2]={{uniform->resource->buffer,binding.uniform_offset,binding.uniform_size},{storage->resource->buffer,binding.storage_offset,binding.storage_size}};
        VkWriteDescriptorSet writes[2]={};for(unsigned i=0;i<2;i++){auto &w=writes[i];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=set;w.dstBinding=i?pipeline->resource->storage_binding:pipeline->resource->uniform_binding;w.descriptorCount=1;w.descriptorType=i?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;w.pBufferInfo=&buffers_info[i];}
        vkUpdateDescriptorSets(job->device,2,writes,0,nullptr);
        VkCommandPoolCreateInfo pool={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool.queueFamilyIndex=parent->job->queue_family;
        if(vkCreateCommandPool(job->device,&pool,nullptr,&job->pool)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
        VkCommandBufferAllocateInfo allocate={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};allocate.commandPool=job->pool;allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate.commandBufferCount=1;
        VkCommandBuffer command=VK_NULL_HANDLE;if(vkAllocateCommandBuffers(job->device,&allocate,&command)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
        VkCommandBufferBeginInfo begin={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;if(vkBeginCommandBuffer(command,&begin)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
        VkMemoryBarrier barrier={VK_STRUCTURE_TYPE_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_UNIFORM_READ_BIT|VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
        vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline->resource->pipeline);vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline->resource->layout,0,1,&set,0,nullptr);
        vkCmdDispatch(command,1,1,1);
        barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT|VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT|VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,nullptr,0,nullptr);
        if(vkEndCommandBuffer(command)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
        VkFenceCreateInfo fence={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};if(vkCreateFence(job->device,&fence,nullptr,&job->fence)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
        if(cancelled && cancelled(context))return SATCHEL_VULKAN_CANCELLED;
        job->token=next_submission++;job->attempted=true;uniform->resource->in_flight=true;storage->resource->in_flight=true;++pipeline->resource->references;
        parent->job->persistent_pending=true;*slot=job.release();*output=(*slot)->token;
        VkSubmitInfo submit={VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;
        return vkQueueSubmit(parent->job->queue,1,&submit,(*slot)->fence)==VK_SUCCESS?SATCHEL_VULKAN_OK:SATCHEL_VULKAN_API_ERROR;
    }catch(...){return SATCHEL_VULKAN_INTERNAL_ERROR;}
}

extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_compute_submit_original(uint64_t device,uint64_t token,const SatchelVulkanComputeBindings *input,uint64_t *output,SatchelVulkanCancelled cancelled,void *context){return submit_compute(device,token,input,output,cancelled,context,false,0);}
