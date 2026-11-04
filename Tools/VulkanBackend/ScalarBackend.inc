extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_scalar_pipeline_create(uint64_t device,const void *spirv,size_t size,
    const SatchelScalarReflection *input,uint64_t *output,SatchelVulkanCancelled cancelled,void *context){
    if(!output)return SATCHEL_VULKAN_INVALID;
    if(!spirv || !input || size<256 || (size&3) || size>SATCHEL_VULKAN_COMPUTE_SPIRV_LIMIT){*output=0;return SATCHEL_VULKAN_INVALID;}
    try {
        const SatchelScalarReflection profile=*input;std::vector<uint32_t> code(size/4);std::memcpy(code.data(),spirv,size);*output=0;
        if(!scalar_schema(code,profile))return SATCHEL_VULKAN_UNSUPPORTED;
        if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;Unlock unlock;
        if(cancelled && cancelled(context))return SATCHEL_VULKAN_CANCELLED;
        auto *parent=find_device(device);if(!parent)return SATCHEL_VULKAN_INVALID;
        const auto limits=compute_limits(*parent);if(!supported_original(*parent,limits) || profile.storage_required_bytes>limits.max_storage_range || profile.expected_storage_address%limits.storage_offset_alignment)return SATCHEL_VULKAN_UNSUPPORTED;
        ComputePipelineSlot *slot=nullptr;for(auto &candidate:compute_pipelines)if(!candidate.token){slot=&candidate;break;}
        if(!slot || next_compute_pipeline==(UINT64_C(1)<<62))return SATCHEL_VULKAN_BUSY;
        auto pipeline=std::make_unique<ComputePipelineResource>();pipeline->parent=device;pipeline->device=parent->job->device;
        pipeline->scalar=true;pipeline->scalar_reflection=profile;pipeline->uniform_binding=profile.uniform_binding;pipeline->storage_binding=profile.storage_binding;
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
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_scalar_submit(uint64_t device,uint64_t token,const SatchelVulkanScalarBindings *input,uint64_t *output,SatchelVulkanCancelled cancelled,void *context){
 if(!output)return SATCHEL_VULKAN_INVALID;if(!input){*output=0;return SATCHEL_VULKAN_INVALID;}const SatchelVulkanScalarBindings copy=*input;if(copy.guest_cb_storage_disjoint!=1 || copy.reserved){*output=0;return SATCHEL_VULKAN_UNSUPPORTED;}return submit_compute(device,token,&copy.buffers,output,cancelled,context,true,copy.storage_guest_address);
}
