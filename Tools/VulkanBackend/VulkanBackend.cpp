#include "VulkanScalar.h"
#include "VulkanProvoking.h"
#include "VulkanBackend.h"
#include "VulkanDevice.h"
#include "VulkanTransferBatch.h"
#include "../ShaderBridge/Fixture.h"
#include <vulkan/vulkan.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <memory>
#include <atomic>
#include <vector>

namespace {
struct Job {
    VkInstance instance=VK_NULL_HANDLE;
    VkPhysicalDevice physical=VK_NULL_HANDLE;
    VkDevice device=VK_NULL_HANDLE;
    VkQueue queue=VK_NULL_HANDLE;
    uint32_t queue_family=0;
    VkBuffer buffer=VK_NULL_HANDLE;
    VkDeviceMemory memory=VK_NULL_HANDLE;
    void *mapped=nullptr;
    VkShaderModule shader=VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_layout=VK_NULL_HANDLE;
    VkPipelineLayout pipeline_layout=VK_NULL_HANDLE;
    VkPipeline pipeline=VK_NULL_HANDLE;
    VkDescriptorPool descriptor_pool=VK_NULL_HANDLE;
    VkCommandPool command_pool=VK_NULL_HANDLE;
    VkFence fence=VK_NULL_HANDLE;
    bool submitted=false,completed=false,persistent_pending=false;
    ~Job() {
        if(persistent_pending)return; // Retain native handles through process teardown if completion is unknown.
        if (mapped) vkUnmapMemory(device,memory);
        if (fence) vkDestroyFence(device,fence,nullptr);
        if (command_pool) vkDestroyCommandPool(device,command_pool,nullptr);
        if (descriptor_pool) vkDestroyDescriptorPool(device,descriptor_pool,nullptr);
        if (pipeline) vkDestroyPipeline(device,pipeline,nullptr);
        if (pipeline_layout) vkDestroyPipelineLayout(device,pipeline_layout,nullptr);
        if (descriptor_layout) vkDestroyDescriptorSetLayout(device,descriptor_layout,nullptr);
        if (shader) vkDestroyShaderModule(device,shader,nullptr);
        if (buffer) vkDestroyBuffer(device,buffer,nullptr);
        if (memory) vkFreeMemory(device,memory,nullptr);
        if (device) vkDestroyDevice(device,nullptr);
        if (instance) vkDestroyInstance(instance,nullptr);
    }
};
std::atomic_flag gate=ATOMIC_FLAG_INIT;
struct Unlock { ~Unlock() {gate.clear(std::memory_order_release);} };
// Deliberately retain a single unfinished submission across API calls rather
// than destroy live GPU resources or perform an unbounded vkDeviceWaitIdle.
Job *pending=nullptr;
void dispose(Job *job) {
    if (job->submitted && !job->completed) pending=job;
    else delete job;
}
bool has(const std::vector<VkExtensionProperties> &list,const char *name) {
    return std::any_of(list.begin(),list.end(),[&](const auto &x){return std::strcmp(x.extensionName,name)==0;});
}
SatchelVulkanStatus initialize(Job &j,SatchelVulkanReport &r,SatchelVulkanCancelled cancelled,void *context) {
    const auto stop=[&](){return cancelled && cancelled(context);};
    const auto call=[&](VkResult result){r.vk_result=result;return result==VK_SUCCESS;};
    if (stop()) return SATCHEL_VULKAN_CANCELLED;
    r.stage=1;
    uint32_t count=0;
    if (!call(vkEnumerateInstanceExtensionProperties(nullptr,&count,nullptr))) return SATCHEL_VULKAN_API_ERROR;
    if (!count || count>1024) return SATCHEL_VULKAN_UNSUPPORTED;
    std::vector<VkExtensionProperties> extensions(count);
    if (!call(vkEnumerateInstanceExtensionProperties(nullptr,&count,extensions.data()))) return SATCHEL_VULKAN_API_ERROR;
    extensions.resize(count);
    if (!has(extensions,VK_EXT_LAYER_SETTINGS_EXTENSION_NAME)) return SATCHEL_VULKAN_UNSUPPORTED;
    // Portability enumeration is a loader extension. Direct-linked MoltenVK
    // advertises devices without it; enable the flag only if advertised.
    const bool enumerate_portability=has(extensions,VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
    const char *instance_extensions[]={VK_EXT_LAYER_SETTINGS_EXTENSION_NAME,VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME};
    const uint32_t zero=0; const VkBool32 no=VK_FALSE;
    VkLayerSettingEXT settings[]={
        {"MoltenVK","MVK_CONFIG_LOG_LEVEL",VK_LAYER_SETTING_TYPE_UINT32_EXT,1,&zero},
        {"MoltenVK","MVK_CONFIG_DEBUG",VK_LAYER_SETTING_TYPE_BOOL32_EXT,1,&no},
        {"MoltenVK","MVK_CONFIG_TRACE_VULKAN_CALLS",VK_LAYER_SETTING_TYPE_UINT32_EXT,1,&zero},
        {"MoltenVK","MVK_CONFIG_FAST_MATH_ENABLED",VK_LAYER_SETTING_TYPE_UINT32_EXT,1,&zero},
        {"MoltenVK","MVK_CONFIG_AUTO_GPU_CAPTURE_SCOPE",VK_LAYER_SETTING_TYPE_UINT32_EXT,1,&zero}};
    VkLayerSettingsCreateInfoEXT config={VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT};
    config.settingCount=5; config.pSettings=settings;
    VkApplicationInfo app={VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.pApplicationName="Satchel original Vulkan fixture";app.apiVersion=VK_API_VERSION_1_1;
    VkInstanceCreateInfo instance={VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};instance.pNext=&config;instance.pApplicationInfo=&app;
    instance.flags=enumerate_portability ? VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR : 0;instance.enabledExtensionCount=enumerate_portability ? 2 : 1;instance.ppEnabledExtensionNames=instance_extensions;
    if (!call(vkCreateInstance(&instance,nullptr,&j.instance))) return SATCHEL_VULKAN_API_ERROR;
    if (stop()) return SATCHEL_VULKAN_CANCELLED;
    r.stage=2;
    count=0;if (!call(vkEnumeratePhysicalDevices(j.instance,&count,nullptr))) return SATCHEL_VULKAN_API_ERROR;
    if (!count || count>16) return SATCHEL_VULKAN_UNSUPPORTED;
    std::vector<VkPhysicalDevice> devices(count);
    if (!call(vkEnumeratePhysicalDevices(j.instance,&count,devices.data()))) return SATCHEL_VULKAN_API_ERROR;
    uint32_t family=UINT32_MAX;
    for (uint32_t d=0;d<count && family==UINT32_MAX;++d) {
        uint32_t queues=0;vkGetPhysicalDeviceQueueFamilyProperties(devices[d],&queues,nullptr);
        if (!queues || queues>64) continue;
        std::vector<VkQueueFamilyProperties> props(queues);vkGetPhysicalDeviceQueueFamilyProperties(devices[d],&queues,props.data());
        for (uint32_t i=0;i<queues;++i) if (props[i].queueCount && (props[i].queueFlags&VK_QUEUE_COMPUTE_BIT)) {j.physical=devices[d];family=i;break;}
    }
    if (family==UINT32_MAX) return SATCHEL_VULKAN_UNSUPPORTED;
    VkPhysicalDeviceProperties properties={};vkGetPhysicalDeviceProperties(j.physical,&properties);r.device_api_version=properties.apiVersion;
    count=0;if (!call(vkEnumerateDeviceExtensionProperties(j.physical,nullptr,&count,nullptr))) return SATCHEL_VULKAN_API_ERROR;
    if (count>1024) return SATCHEL_VULKAN_UNSUPPORTED;
    extensions.resize(count);
    if (!call(vkEnumerateDeviceExtensionProperties(j.physical,nullptr,&count,extensions.data()))) return SATCHEL_VULKAN_API_ERROR;
    extensions.resize(count);
    const char *portability="VK_KHR_portability_subset";
    if (!has(extensions,portability)) return SATCHEL_VULKAN_UNSUPPORTED;
    float priority=1;
    VkDeviceQueueCreateInfo queue={VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};queue.queueFamilyIndex=family;queue.queueCount=1;queue.pQueuePriorities=&priority;
    VkDeviceCreateInfo device={VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};device.queueCreateInfoCount=1;device.pQueueCreateInfos=&queue;device.enabledExtensionCount=1;device.ppEnabledExtensionNames=&portability;
    if (!call(vkCreateDevice(j.physical,&device,nullptr,&j.device))) return SATCHEL_VULKAN_API_ERROR;
    r.portability_subset_enabled=1;j.queue_family=family;vkGetDeviceQueue(j.device,family,0,&j.queue);
    if (stop()) return SATCHEL_VULKAN_CANCELLED;
    return SATCHEL_VULKAN_OK;
}
SatchelVulkanStatus execute(SatchelVulkanReport &r,SatchelVulkanCancelled cancelled,void *context) {
    const auto stop=[&](){return cancelled && cancelled(context);};
    std::unique_ptr<Job,decltype(&dispose)> job(new Job,&dispose);auto &j=*job;
    const auto call=[&](VkResult result){r.vk_result=result;return result==VK_SUCCESS;};
    const auto initialized=initialize(j,r,cancelled,context);
    if(initialized!=SATCHEL_VULKAN_OK)return initialized;
    r.stage=3;
    VkBufferCreateInfo buffer={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};buffer.size=16;buffer.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;buffer.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    if (!call(vkCreateBuffer(j.device,&buffer,nullptr,&j.buffer))) return SATCHEL_VULKAN_API_ERROR;
    VkMemoryRequirements requirements={};vkGetBufferMemoryRequirements(j.device,j.buffer,&requirements);
    if (requirements.size<16 || requirements.size>16*1024*1024) return SATCHEL_VULKAN_UNSUPPORTED;
    VkPhysicalDeviceMemoryProperties memory={};vkGetPhysicalDeviceMemoryProperties(j.physical,&memory);
    uint32_t type=UINT32_MAX;
    for (uint32_t i=0;i<memory.memoryTypeCount;++i) if ((requirements.memoryTypeBits&(1u<<i)) && (memory.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
        if (type==UINT32_MAX) type=i;
        if (memory.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) {type=i;break;}
    }
    if (type==UINT32_MAX) return SATCHEL_VULKAN_UNSUPPORTED;
    r.coherent_memory=!!(memory.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VkMemoryAllocateInfo allocate={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocate.allocationSize=requirements.size;allocate.memoryTypeIndex=type;
    if (!call(vkAllocateMemory(j.device,&allocate,nullptr,&j.memory)) || !call(vkBindBufferMemory(j.device,j.buffer,j.memory,0)) ||
        !call(vkMapMemory(j.device,j.memory,0,VK_WHOLE_SIZE,0,&j.mapped))) return SATCHEL_VULKAN_API_ERROR;
    auto *values=static_cast<uint32_t *>(j.mapped);for (unsigned i=0;i<4;++i) values[i]=0xdeadbeef;
    VkMappedMemoryRange range={VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};range.memory=j.memory;range.offset=0;range.size=VK_WHOLE_SIZE;
    if (!r.coherent_memory && !call(vkFlushMappedMemoryRanges(j.device,1,&range))) return SATCHEL_VULKAN_API_ERROR;
    r.stage=4;
    VkShaderModuleCreateInfo shader={VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};shader.codeSize=sizeof(satchel_shader_fixture);shader.pCode=satchel_shader_fixture;
    if (!call(vkCreateShaderModule(j.device,&shader,nullptr,&j.shader))) return SATCHEL_VULKAN_API_ERROR;
    VkDescriptorSetLayoutBinding binding={};binding.binding=0;binding.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;binding.descriptorCount=1;binding.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutCreateInfo descriptor={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};descriptor.bindingCount=1;descriptor.pBindings=&binding;
    if (!call(vkCreateDescriptorSetLayout(j.device,&descriptor,nullptr,&j.descriptor_layout))) return SATCHEL_VULKAN_API_ERROR;
    VkPipelineLayoutCreateInfo layout={VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};layout.setLayoutCount=1;layout.pSetLayouts=&j.descriptor_layout;
    if (!call(vkCreatePipelineLayout(j.device,&layout,nullptr,&j.pipeline_layout))) return SATCHEL_VULKAN_API_ERROR;
    VkComputePipelineCreateInfo pipeline={VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};pipeline.layout=j.pipeline_layout;
    pipeline.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;pipeline.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pipeline.stage.module=j.shader;pipeline.stage.pName="main";
    if (!call(vkCreateComputePipelines(j.device,VK_NULL_HANDLE,1,&pipeline,nullptr,&j.pipeline))) return SATCHEL_VULKAN_API_ERROR;
    if (stop()) return SATCHEL_VULKAN_CANCELLED;
    r.stage=5;
    VkDescriptorPoolSize pool_size={VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1};
    VkDescriptorPoolCreateInfo pool={VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};pool.maxSets=1;pool.poolSizeCount=1;pool.pPoolSizes=&pool_size;
    if (!call(vkCreateDescriptorPool(j.device,&pool,nullptr,&j.descriptor_pool))) return SATCHEL_VULKAN_API_ERROR;
    VkDescriptorSetAllocateInfo set_info={VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};set_info.descriptorPool=j.descriptor_pool;set_info.descriptorSetCount=1;set_info.pSetLayouts=&j.descriptor_layout;
    VkDescriptorSet set=VK_NULL_HANDLE;
    if (!call(vkAllocateDescriptorSets(j.device,&set_info,&set))) return SATCHEL_VULKAN_API_ERROR;
    VkDescriptorBufferInfo buffer_info={j.buffer,0,16};
    VkWriteDescriptorSet write={VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};write.dstSet=set;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.pBufferInfo=&buffer_info;
    vkUpdateDescriptorSets(j.device,1,&write,0,nullptr);
    VkCommandPoolCreateInfo command_pool={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};command_pool.queueFamilyIndex=j.queue_family;
    if (!call(vkCreateCommandPool(j.device,&command_pool,nullptr,&j.command_pool))) return SATCHEL_VULKAN_API_ERROR;
    VkCommandBufferAllocateInfo command_info={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};command_info.commandPool=j.command_pool;command_info.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;command_info.commandBufferCount=1;
    VkCommandBuffer command=VK_NULL_HANDLE;
    if (!call(vkAllocateCommandBuffers(j.device,&command_info,&command))) return SATCHEL_VULKAN_API_ERROR;
    VkCommandBufferBeginInfo begin={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (!call(vkBeginCommandBuffer(command,&begin))) return SATCHEL_VULKAN_API_ERROR;
    vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,j.pipeline);
    vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,j.pipeline_layout,0,1,&set,0,nullptr);
    vkCmdDispatch(command,1,1,1);
    VkBufferMemoryBarrier barrier={VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    barrier.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.buffer=j.buffer;barrier.size=16;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&barrier,0,nullptr);
    if (!call(vkEndCommandBuffer(command))) return SATCHEL_VULKAN_API_ERROR;
    VkFenceCreateInfo fence={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (!call(vkCreateFence(j.device,&fence,nullptr,&j.fence))) return SATCHEL_VULKAN_API_ERROR;
    if (stop()) return SATCHEL_VULKAN_CANCELLED;
    r.stage=6;
    VkSubmitInfo submit={VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;
    if (!call(vkQueueSubmit(j.queue,1,&submit,j.fence))) return SATCHEL_VULKAN_API_ERROR;
    j.submitted=true;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    while (true) {
        const VkResult waited=vkWaitForFences(j.device,1,&j.fence,VK_TRUE,50000000);
        r.vk_result=waited;
        if (waited==VK_SUCCESS) {j.completed=true;break;}
        if (waited==VK_ERROR_DEVICE_LOST) j.completed=true;
        if (waited!=VK_TIMEOUT) return SATCHEL_VULKAN_API_ERROR;
        const bool cancelled_now=stop();
        if (cancelled_now || std::chrono::steady_clock::now()>=deadline) {
            pending=job.release();r.resources_retained=1;
            return cancelled_now ? SATCHEL_VULKAN_CANCELLED : SATCHEL_VULKAN_TIMEOUT;
        }
    }
    r.command_completed=1;r.stage=7;
    if (!r.coherent_memory && !call(vkInvalidateMappedMemoryRanges(j.device,1,&range))) return SATCHEL_VULKAN_API_ERROR;
    r.value_matched=values[0]==0x13579bdf;
    r.guard_words_matched=values[1]==0xdeadbeef && values[2]==0xdeadbeef && values[3]==0xdeadbeef;
    return r.value_matched && r.guard_words_matched ? SATCHEL_VULKAN_OK : SATCHEL_VULKAN_MISMATCH;
}
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_compute_diagnostic(SatchelVulkanReport *report,SatchelVulkanCancelled cancelled,void *context) {
    if (!report) return SATCHEL_VULKAN_INVALID;
    *report={};
    if (gate.test_and_set(std::memory_order_acquire)) {report->status=SATCHEL_VULKAN_BUSY;return SATCHEL_VULKAN_BUSY;}
    Unlock unlock;
    try {
        if (pending) {
            const VkResult status=vkGetFenceStatus(pending->device,pending->fence);
            if (status!=VK_SUCCESS && status!=VK_ERROR_DEVICE_LOST) {
                report->status=SATCHEL_VULKAN_BUSY;report->vk_result=status;report->resources_retained=1;return SATCHEL_VULKAN_BUSY;
            }
            delete pending;pending=nullptr;
        }
        const SatchelVulkanStatus status=execute(*report,cancelled,context);report->status=status;report->resources_retained=pending!=nullptr;return status;
    } catch (...) {report->status=SATCHEL_VULKAN_INTERNAL_ERROR;report->resources_retained=pending!=nullptr;return SATCHEL_VULKAN_INTERNAL_ERROR;}
}

namespace {
struct DeviceSlot {uint64_t token=0;std::unique_ptr<Job> job;SatchelGuestDeviceCaps caps={};};
DeviceSlot devices[SATCHEL_GUEST_DEVICE_REGISTRY_LIMIT];
uint64_t next_device=1;
struct BufferResource {
    VkDevice device=VK_NULL_HANDLE;VkBuffer buffer=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;void *mapped=nullptr;
    SatchelVulkanBufferInfo info={};bool in_flight=false;
    ~BufferResource(){
        if(in_flight)return; // Never destroy uncertain submitted resources, even during static teardown.
        if(mapped)vkUnmapMemory(device,memory);
        if(buffer)vkDestroyBuffer(device,buffer,nullptr);
        if(memory)vkFreeMemory(device,memory,nullptr);
    }
};
struct BufferSlot {uint64_t token=0;std::unique_ptr<BufferResource> resource;};
BufferSlot buffers[SATCHEL_VULKAN_BUFFER_LIMIT];
uint64_t next_buffer=UINT64_C(1)<<63,buffer_allocated=0;
#include "RenderTargetInternal.inc"
#include "RasterInternal.inc"
struct ComputePipelineResource {
    uint64_t parent=0;VkDevice device=VK_NULL_HANDLE;VkShaderModule shader=VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptor_layout=VK_NULL_HANDLE;VkPipelineLayout layout=VK_NULL_HANDLE;VkPipeline pipeline=VK_NULL_HANDLE;
    uint32_t uniform_binding=0,storage_binding=0,references=0;bool scalar=false;SatchelScalarReflection scalar_reflection={};
    ~ComputePipelineResource(){
        if(references)return;
        if(pipeline)vkDestroyPipeline(device,pipeline,nullptr);
        if(layout)vkDestroyPipelineLayout(device,layout,nullptr);
        if(descriptor_layout)vkDestroyDescriptorSetLayout(device,descriptor_layout,nullptr);
        if(shader)vkDestroyShaderModule(device,shader,nullptr);
    }
};
struct ComputePipelineSlot {uint64_t token=0;std::unique_ptr<ComputePipelineResource> resource;};
ComputePipelineSlot compute_pipelines[SATCHEL_VULKAN_COMPUTE_PIPELINE_LIMIT];
uint64_t next_compute_pipeline=UINT64_C(1)<<61;
struct Submission {
    uint64_t token=0,parent=0,source=0,destination=0;
    uint64_t retained_buffers[SATCHEL_VULKAN_BUFFER_LIMIT]={};uint32_t retained_count=0;
    VkDevice device=VK_NULL_HANDLE;VkCommandPool pool=VK_NULL_HANDLE;VkFence fence=VK_NULL_HANDLE;
    uint64_t raster_pipelines[SATCHEL_VULKAN_RASTER_PIPELINE_LIMIT]={};uint32_t raster_count=0;
    uint64_t compute_pipeline=0,target=0,raster_pipeline=0;VkDescriptorPool descriptors=VK_NULL_HANDLE;
    VkRenderPass render_pass=VK_NULL_HANDLE;VkFramebuffer framebuffer=VK_NULL_HANDLE;
    bool attempted=false,completed=false;
    ~Submission(){
        if(attempted && !completed)return;
        if(fence)vkDestroyFence(device,fence,nullptr);
        if(pool)vkDestroyCommandPool(device,pool,nullptr);
        if(descriptors)vkDestroyDescriptorPool(device,descriptors,nullptr);
        if(framebuffer)vkDestroyFramebuffer(device,framebuffer,nullptr);
        if(render_pass)vkDestroyRenderPass(device,render_pass,nullptr);
    }
};
// Raw owning slots intentionally survive static teardown when completion is
// unknown; only an actual fence-success path may delete a submitted job.
Submission *submissions[SATCHEL_VULKAN_SUBMISSION_LIMIT]={};
uint64_t next_submission=UINT64_C(1)<<62;

DeviceSlot *find_device(uint64_t token){for(auto &slot:devices)if(token && slot.token==token)return &slot;return nullptr;}
ComputePipelineSlot *find_compute_pipeline(uint64_t device,uint64_t token){for(auto &slot:compute_pipelines)if(token && slot.token==token && slot.resource->parent==device)return &slot;return nullptr;}
BufferSlot *find_buffer(uint64_t device,uint64_t token){
    for(auto &slot:buffers)if(token && slot.token==token && slot.resource->info.device==device)return &slot;
    return nullptr;
}
SatchelVulkanStatus finish_submission(Submission **slot) {
    auto *job=*slot;auto *parent=find_device(job->parent);BufferSlot *retained[SATCHEL_VULKAN_BUFFER_LIMIT]={};
    if(!parent || !job->retained_count || job->retained_count>SATCHEL_VULKAN_BUFFER_LIMIT)return SATCHEL_VULKAN_INTERNAL_ERROR;
    const uint32_t count=job->retained_count;
    TargetSlot *target=job->target?find_target(job->parent,job->target):nullptr;
    RasterSlot *rasters[SATCHEL_VULKAN_RASTER_PIPELINE_LIMIT]={};const uint32_t raster_count=job->raster_count?job->raster_count:(job->raster_pipeline?1:0);
    if(raster_count>SATCHEL_VULKAN_RASTER_PIPELINE_LIMIT || (job->raster_count && job->raster_pipeline))return SATCHEL_VULKAN_INTERNAL_ERROR;
    for(unsigned i=0;i<raster_count;i++){rasters[i]=find_raster(job->parent,job->raster_count?job->raster_pipelines[i]:job->raster_pipeline);if(!rasters[i] || !rasters[i]->resource->info.references)return SATCHEL_VULKAN_INTERNAL_ERROR;for(unsigned j=0;j<i;j++)if(rasters[i]==rasters[j])return SATCHEL_VULKAN_INTERNAL_ERROR;}
    if(job->target && (!target || !target->resource->info.in_flight))return SATCHEL_VULKAN_INTERNAL_ERROR;
    ComputePipelineSlot *pipeline=job->compute_pipeline?find_compute_pipeline(job->parent,job->compute_pipeline):nullptr;
    if(job->compute_pipeline && (!pipeline || !pipeline->resource->references))return SATCHEL_VULKAN_INTERNAL_ERROR;
    for(uint32_t i=0;i<count;i++){retained[i]=find_buffer(job->parent,job->retained_buffers[i]);if(!retained[i] || !retained[i]->resource->in_flight)return SATCHEL_VULKAN_INTERNAL_ERROR;}
    job->completed=true;delete job;*slot=nullptr;
    for(uint32_t i=0;i<count;i++)retained[i]->resource->in_flight=false;parent->job->persistent_pending=false;
    if(pipeline)--pipeline->resource->references;
    for(unsigned i=0;i<raster_count;i++)--rasters[i]->resource->info.references;
    if(target){target->resource->info.in_flight=0;target->resource->info.initialized=1;target->resource->layout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;}
    return SATCHEL_VULKAN_OK;
}
void release_buffer(BufferSlot &slot){buffer_allocated-=slot.resource->info.allocation_size;slot.resource.reset();slot.token=0;}
SatchelVulkanStatus sync_buffer(BufferResource &resource,bool invalidate){
    if(resource.info.coherent_memory)return SATCHEL_VULKAN_OK;
    VkMappedMemoryRange range={VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE};range.memory=resource.memory;range.offset=0;range.size=VK_WHOLE_SIZE;
    const VkResult result=invalidate ? vkInvalidateMappedMemoryRanges(resource.device,1,&range) : vkFlushMappedMemoryRanges(resource.device,1,&range);
    return result==VK_SUCCESS ? SATCHEL_VULKAN_OK : SATCHEL_VULKAN_API_ERROR;
}

SatchelGuestDeviceCaps snapshot(const Job &j) {
    VkPhysicalDeviceProperties properties={};VkPhysicalDeviceFeatures features={};
    vkGetPhysicalDeviceProperties(j.physical,&properties);vkGetPhysicalDeviceFeatures(j.physical,&features);
    uint32_t count=0;vkGetPhysicalDeviceQueueFamilyProperties(j.physical,&count,nullptr);
    if(!count || count>64)throw std::bad_alloc();
    std::vector<VkQueueFamilyProperties> queues(count);vkGetPhysicalDeviceQueueFamilyProperties(j.physical,&count,queues.data());
    SatchelGuestDeviceCaps caps={};caps.version=1;caps.api_version=properties.apiVersion;
    caps.vendor_id=properties.vendorID;caps.device_id=properties.deviceID;caps.device_type=properties.deviceType;
    caps.queue_family=j.queue_family;caps.queue_flags=queues.at(j.queue_family).queueFlags;caps.portability_subset=1;
    caps.supported_features=(features.samplerAnisotropy?SATCHEL_GUEST_CAP_ANISOTROPY:0)|
        (features.shaderInt64?SATCHEL_GUEST_CAP_SHADER_INT64:0)|(features.shaderFloat64?SATCHEL_GUEST_CAP_SHADER_FLOAT64:0)|
        (features.independentBlend?SATCHEL_GUEST_CAP_INDEPENDENT_BLEND:0);
    const auto &limits=properties.limits;
    caps.max_image_dimension_2d=limits.maxImageDimension2D;caps.max_storage_buffer_range=limits.maxStorageBufferRange;
    caps.max_descriptor_storage_buffers=limits.maxDescriptorSetStorageBuffers;caps.max_compute_invocations=limits.maxComputeWorkGroupInvocations;
    for(unsigned i=0;i<3;i++){caps.max_compute_group_count[i]=limits.maxComputeWorkGroupCount[i];caps.max_compute_group_size[i]=limits.maxComputeWorkGroupSize[i];}
    caps.min_uniform_buffer_offset_alignment=limits.minUniformBufferOffsetAlignment;
    caps.min_storage_buffer_offset_alignment=limits.minStorageBufferOffsetAlignment;caps.noncoherent_atom_size=limits.nonCoherentAtomSize;
    return caps;
}
SatchelGuestBackendStatus backend_status(SatchelVulkanStatus status) {
    return status==SATCHEL_VULKAN_OK?SATCHEL_GUEST_BACKEND_OK:(status==SATCHEL_VULKAN_UNSUPPORTED?SATCHEL_GUEST_BACKEND_UNSUPPORTED:
        (status==SATCHEL_VULKAN_BUSY?SATCHEL_GUEST_BACKEND_LIMIT:SATCHEL_GUEST_BACKEND_FAILURE));
}
SatchelGuestBackendStatus backend_create(void *,uint32_t flags,uint64_t *handle,SatchelGuestDeviceCaps *caps) {return backend_status(satchel_vulkan_device_create(flags,handle,caps));}
SatchelGuestBackendStatus backend_destroy(void *,uint64_t handle) {return backend_status(satchel_vulkan_device_destroy(handle));}
SatchelGuestBackendStatus backend_caps(void *,uint64_t handle,SatchelGuestDeviceCaps *caps) {return backend_status(satchel_vulkan_device_capabilities(handle,caps));}
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_device_create(uint32_t flags,uint64_t *handle,SatchelGuestDeviceCaps *caps) {
    if(!handle || !caps)return SATCHEL_VULKAN_INVALID;*handle=0;*caps={};
    if(flags)return SATCHEL_VULKAN_UNSUPPORTED;
    if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;
    Unlock unlock;
    try {
        DeviceSlot *slot=nullptr;for(auto &candidate:devices)if(!candidate.token){slot=&candidate;break;}
        if(!slot || next_device==(UINT64_C(1)<<59))return SATCHEL_VULKAN_BUSY;
        auto job=std::make_unique<Job>();SatchelVulkanReport report={};
        const auto status=initialize(*job,report,nullptr,nullptr);if(status!=SATCHEL_VULKAN_OK)return status;
        const auto actual=snapshot(*job);
        slot->caps=actual;slot->job=std::move(job);slot->token=next_device++;
        *handle=slot->token;*caps=slot->caps;return SATCHEL_VULKAN_OK;
    } catch(...){return SATCHEL_VULKAN_INTERNAL_ERROR;}
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_device_capabilities(uint64_t handle,SatchelGuestDeviceCaps *caps) {
    if(!caps)return SATCHEL_VULKAN_INVALID;*caps={};
    if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;
    Unlock unlock;
    for(const auto &slot:devices)if(handle && slot.token==handle){*caps=slot.caps;return SATCHEL_VULKAN_OK;}
    return SATCHEL_VULKAN_INVALID;
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_device_destroy(uint64_t handle) {
    if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;
    Unlock unlock;
    for(auto &slot:devices)if(handle && slot.token==handle){
        if(slot.job->persistent_pending)return SATCHEL_VULKAN_BUSY;
        for(const auto &pipeline:compute_pipelines)if(pipeline.token && pipeline.resource->parent==handle && pipeline.resource->references)return SATCHEL_VULKAN_BUSY;
        for(auto &pipeline:compute_pipelines)if(pipeline.token && pipeline.resource->parent==handle){pipeline.resource.reset();pipeline.token=0;}
        for(auto &pipeline:raster_pipelines)if(pipeline.token && pipeline.resource->parent==handle){pipeline.resource.reset();pipeline.token=0;}
        for(auto &target:targets)if(target.token && target.resource->info.device==handle)release_target(target);
        for(auto &buffer:buffers)if(buffer.token && buffer.resource->info.device==handle)release_buffer(buffer);
        slot.job.reset();slot.token=0;slot.caps={};return SATCHEL_VULKAN_OK;
    }
    return SATCHEL_VULKAN_INVALID;
}
extern "C" __attribute__((visibility("default"))) uint32_t satchel_vulkan_device_live_count(void) {
    if(gate.test_and_set(std::memory_order_acquire))return UINT32_MAX;
    Unlock unlock;uint32_t count=0;for(const auto &slot:devices)count+=slot.token!=0;return count;
}
extern "C" __attribute__((visibility("default"))) int satchel_vulkan_guest_device_backend(SatchelGuestDeviceBackend *backend) {
    if(!backend)return 0;*backend={1,0,nullptr,backend_create,backend_destroy,backend_caps};return 1;
}

extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_buffer_create(uint64_t device,uint64_t size,uint32_t usage,uint64_t *buffer,SatchelVulkanBufferInfo *info) {
    if(!buffer || !info)return SATCHEL_VULKAN_INVALID;*buffer=0;*info={};
    if(!size || size>SATCHEL_VULKAN_BUFFER_MAX_SIZE || size>SIZE_MAX || !usage)return SATCHEL_VULKAN_INVALID;
    if(usage&~31u)return SATCHEL_VULKAN_UNSUPPORTED;
    if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;
    Unlock unlock;
    try {
        DeviceSlot *parent=find_device(device);if(!parent)return SATCHEL_VULKAN_INVALID;
        if((usage&SATCHEL_VULKAN_BUFFER_STORAGE) && size>parent->caps.max_storage_buffer_range)return SATCHEL_VULKAN_UNSUPPORTED;
        BufferSlot *slot=nullptr;for(auto &candidate:buffers)if(!candidate.token){slot=&candidate;break;}
        if(!slot || next_buffer==UINT64_MAX || size>SATCHEL_VULKAN_BUFFER_MEMORY_LIMIT-buffer_allocated)return SATCHEL_VULKAN_BUSY;
        auto resource=std::make_unique<BufferResource>();resource->device=parent->job->device;
        VkBufferCreateInfo create={VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};create.size=size;create.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
        if(usage&SATCHEL_VULKAN_BUFFER_STORAGE)create.usage|=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        if(usage&SATCHEL_VULKAN_BUFFER_TRANSFER_SOURCE)create.usage|=VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        if(usage&SATCHEL_VULKAN_BUFFER_TRANSFER_DESTINATION)create.usage|=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if(usage&SATCHEL_VULKAN_BUFFER_UNIFORM)create.usage|=VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        if(usage&SATCHEL_VULKAN_BUFFER_VERTEX)create.usage|=VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        if(vkCreateBuffer(resource->device,&create,nullptr,&resource->buffer)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
        VkMemoryDedicatedRequirements dedicated={VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS};VkMemoryRequirements2 required={VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2};required.pNext=&dedicated;
        VkBufferMemoryRequirementsInfo2 query={VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2};query.buffer=resource->buffer;vkGetBufferMemoryRequirements2(resource->device,&query,&required);const auto &requirements=required.memoryRequirements;
        VkPhysicalDeviceMaintenance3Properties allocation_limits={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES};VkPhysicalDeviceProperties2 properties={VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};properties.pNext=&allocation_limits;vkGetPhysicalDeviceProperties2(parent->job->physical,&properties);
        if(!allocation_limits.maxMemoryAllocationSize || requirements.size>allocation_limits.maxMemoryAllocationSize)return SATCHEL_VULKAN_UNSUPPORTED;
        if(requirements.size<size || !requirements.alignment || !requirements.memoryTypeBits)return SATCHEL_VULKAN_UNSUPPORTED;
        if(requirements.size>SATCHEL_VULKAN_BUFFER_MEMORY_LIMIT-buffer_allocated)return SATCHEL_VULKAN_BUSY;
        VkPhysicalDeviceMemoryProperties memory={};vkGetPhysicalDeviceMemoryProperties(parent->job->physical,&memory);
        if(!memory.memoryTypeCount || memory.memoryTypeCount>VK_MAX_MEMORY_TYPES || !memory.memoryHeapCount || memory.memoryHeapCount>VK_MAX_MEMORY_HEAPS)return SATCHEL_VULKAN_UNSUPPORTED;
        uint32_t type=UINT32_MAX;
        for(uint32_t i=0;i<memory.memoryTypeCount;i++)if((requirements.memoryTypeBits&(1u<<i)) &&
            (memory.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) && memory.memoryTypes[i].heapIndex<memory.memoryHeapCount &&
            requirements.size<=memory.memoryHeaps[memory.memoryTypes[i].heapIndex].size){
            if(type==UINT32_MAX)type=i;
            if(memory.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT){type=i;break;}
        }
        if(type==UINT32_MAX)return SATCHEL_VULKAN_UNSUPPORTED;
        resource->info={device,size,requirements.size,usage,!!(memory.memoryTypes[type].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)};
        VkMemoryDedicatedAllocateInfo dedicated_info={VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};dedicated_info.buffer=resource->buffer;
        VkMemoryAllocateInfo allocate={VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocate.allocationSize=requirements.size;allocate.memoryTypeIndex=type;if(dedicated.requiresDedicatedAllocation || dedicated.prefersDedicatedAllocation)allocate.pNext=&dedicated_info;
        if(vkAllocateMemory(resource->device,&allocate,nullptr,&resource->memory)!=VK_SUCCESS ||
            vkBindBufferMemory(resource->device,resource->buffer,resource->memory,0)!=VK_SUCCESS ||
            vkMapMemory(resource->device,resource->memory,0,VK_WHOLE_SIZE,0,&resource->mapped)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
        std::memset(resource->mapped,0,static_cast<size_t>(requirements.size));
        const auto flushed=sync_buffer(*resource,false);if(flushed!=SATCHEL_VULKAN_OK)return flushed;
        slot->resource=std::move(resource);slot->token=next_buffer++;buffer_allocated+=requirements.size;
        *buffer=slot->token;*info=slot->resource->info;return SATCHEL_VULKAN_OK;
    } catch(...){return SATCHEL_VULKAN_INTERNAL_ERROR;}
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_buffer_info(uint64_t device,uint64_t buffer,SatchelVulkanBufferInfo *info) {
    if(!info)return SATCHEL_VULKAN_INVALID;*info={};
    if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;
    Unlock unlock;const auto *slot=find_buffer(device,buffer);if(!slot)return SATCHEL_VULKAN_INVALID;
    *info=slot->resource->info;return SATCHEL_VULKAN_OK;
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_buffer_write(uint64_t device,uint64_t buffer,uint64_t offset,const void *source,size_t size) {
    if(size && !source)return SATCHEL_VULKAN_INVALID;
    if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;
    Unlock unlock;auto *slot=find_buffer(device,buffer);if(!slot)return SATCHEL_VULKAN_INVALID;
    auto &resource=*slot->resource;
    if(resource.in_flight)return SATCHEL_VULKAN_BUSY;
    if(offset>resource.info.size || size>resource.info.size-offset)return SATCHEL_VULKAN_INVALID;
    if(!size)return SATCHEL_VULKAN_OK;
    std::memcpy(static_cast<uint8_t *>(resource.mapped)+offset,source,size);return sync_buffer(resource,false);
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_buffer_read(uint64_t device,uint64_t buffer,uint64_t offset,void *destination,size_t size) {
    if(size && !destination)return SATCHEL_VULKAN_INVALID;
    if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;
    Unlock unlock;auto *slot=find_buffer(device,buffer);if(!slot)return SATCHEL_VULKAN_INVALID;
    auto &resource=*slot->resource;
    if(resource.in_flight)return SATCHEL_VULKAN_BUSY;
    if(offset>resource.info.size || size>resource.info.size-offset)return SATCHEL_VULKAN_INVALID;
    if(!size)return SATCHEL_VULKAN_OK;
    const auto invalidated=sync_buffer(resource,true);if(invalidated!=SATCHEL_VULKAN_OK)return invalidated;
    std::memcpy(destination,static_cast<const uint8_t *>(resource.mapped)+offset,size);return SATCHEL_VULKAN_OK;
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_buffer_destroy(uint64_t device,uint64_t buffer) {
    if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;
    Unlock unlock;auto *slot=find_buffer(device,buffer);if(!slot)return SATCHEL_VULKAN_INVALID;
    if(slot->resource->in_flight)return SATCHEL_VULKAN_BUSY;
    release_buffer(*slot);return SATCHEL_VULKAN_OK;
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_buffer_stats(SatchelVulkanBufferStats *stats) {
    if(!stats)return SATCHEL_VULKAN_INVALID;*stats={};
    if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;
    Unlock unlock;stats->buffer_limit=SATCHEL_VULKAN_BUFFER_LIMIT;stats->allocation_limit=SATCHEL_VULKAN_BUFFER_MEMORY_LIMIT;
    stats->max_buffer_size=SATCHEL_VULKAN_BUFFER_MAX_SIZE;stats->allocated_bytes=buffer_allocated;
    for(const auto &slot:buffers)stats->live_buffers+=slot.token!=0;
    return SATCHEL_VULKAN_OK;
}

extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_buffer_diagnostic(SatchelVulkanBufferReport *report,SatchelVulkanCancelled cancelled,void *context) {
    if(!report)return SATCHEL_VULKAN_INVALID;*report={};report->stage=1;
    SatchelVulkanStatus status=SATCHEL_VULKAN_INTERNAL_ERROR;uint64_t device=0,first=0,second=0;
    SatchelVulkanBufferStats baseline={},final={};SatchelGuestDeviceCaps caps={};SatchelVulkanBufferInfo a={},b={};
    const auto stop=[&](){return cancelled && cancelled(context);};
    if(satchel_vulkan_buffer_stats(&baseline)!=SATCHEL_VULKAN_OK){report->status=SATCHEL_VULKAN_BUSY;return SATCHEL_VULKAN_BUSY;}
    report->baseline_buffers=baseline.live_buffers;report->baseline_bytes=baseline.allocated_bytes;
    report->baseline_devices=satchel_vulkan_device_live_count();
    if(report->baseline_devices==UINT32_MAX){report->status=SATCHEL_VULKAN_BUSY;return SATCHEL_VULKAN_BUSY;}
    try {
        do {
            if(stop()){status=SATCHEL_VULKAN_CANCELLED;break;}
            status=satchel_vulkan_device_create(0,&device,&caps);if(status!=SATCHEL_VULKAN_OK)break;report->stage=2;
            if(stop()){status=SATCHEL_VULKAN_CANCELLED;break;}
            status=satchel_vulkan_buffer_create(device,65,7,&first,&a);if(status!=SATCHEL_VULKAN_OK)break;++report->created_buffers;report->allocated_bytes=a.allocation_size;
            status=satchel_vulkan_buffer_create(device,31,SATCHEL_VULKAN_BUFFER_STORAGE,&second,&b);if(status!=SATCHEL_VULKAN_OK)break;++report->created_buffers;
            report->allocated_bytes=a.allocation_size+b.allocation_size;report->stage=3;
            if(stop()){status=SATCHEL_VULKAN_CANCELLED;break;}
            uint8_t source[53],destination[65];for(unsigned i=0;i<sizeof(source);i++)source[i]=static_cast<uint8_t>(i*7+3);
            status=satchel_vulkan_buffer_read(device,first,0,destination,sizeof(destination));if(status!=SATCHEL_VULKAN_OK)break;
            report->zero_initialized=1;for(auto byte:destination)if(byte)report->zero_initialized=0;
            if(!report->zero_initialized){status=SATCHEL_VULKAN_MISMATCH;break;}report->stage=4;
            if(stop()){status=SATCHEL_VULKAN_CANCELLED;break;}
            status=satchel_vulkan_buffer_write(device,first,1,source,sizeof(source));if(status!=SATCHEL_VULKAN_OK)break;
            report->write_completed=1;report->stage=5;
            if(stop()){status=SATCHEL_VULKAN_CANCELLED;break;}
            if(satchel_vulkan_buffer_write(device,first,64,source,2)!=SATCHEL_VULKAN_INVALID){status=SATCHEL_VULKAN_MISMATCH;break;}
            report->bounds_rejected=1;
            status=satchel_vulkan_buffer_read(device,first,0,destination,sizeof(destination));if(status!=SATCHEL_VULKAN_OK)break;
            report->read_completed=!std::memcmp(source,destination+1,sizeof(source));report->guards_matched=destination[0]==0;
            for(unsigned i=54;i<sizeof(destination);i++)if(destination[i])report->guards_matched=0;
            if(!report->read_completed || !report->guards_matched){status=SATCHEL_VULKAN_MISMATCH;break;}report->stage=6;
            if(stop()){status=SATCHEL_VULKAN_CANCELLED;break;}
            status=satchel_vulkan_buffer_destroy(device,first);if(status!=SATCHEL_VULKAN_OK)break;
            SatchelVulkanBufferInfo removed={};
            report->explicit_destroy=satchel_vulkan_buffer_info(device,first,&removed)==SATCHEL_VULKAN_INVALID;
            if(!report->explicit_destroy){status=SATCHEL_VULKAN_MISMATCH;break;}report->stage=7;
            if(stop()){status=SATCHEL_VULKAN_CANCELLED;break;}
            const uint64_t removed_device=device;status=satchel_vulkan_device_destroy(device);if(status!=SATCHEL_VULKAN_OK)break;device=0;
            report->parent_cleanup=satchel_vulkan_buffer_info(removed_device,second,&removed)==SATCHEL_VULKAN_INVALID;
            if(!report->parent_cleanup){status=SATCHEL_VULKAN_MISMATCH;break;}report->stage=8;
        }while(false);
    }catch(...){status=SATCHEL_VULKAN_INTERNAL_ERROR;}
    if(device && satchel_vulkan_device_destroy(device)!=SATCHEL_VULKAN_OK)status=SATCHEL_VULKAN_INTERNAL_ERROR;
    const auto queried=satchel_vulkan_buffer_stats(&final);
    if(queried==SATCHEL_VULKAN_OK){report->final_buffers=final.live_buffers;report->final_bytes=final.allocated_bytes;}
    report->final_devices=satchel_vulkan_device_live_count();
    report->cleanup_matched=queried==SATCHEL_VULKAN_OK && report->final_devices==report->baseline_devices && final.live_buffers==baseline.live_buffers && final.allocated_bytes==baseline.allocated_bytes;
    if(!report->cleanup_matched)status=SATCHEL_VULKAN_INTERNAL_ERROR;
    if(status==SATCHEL_VULKAN_OK)report->stage=9;
    report->status=status;return status;
}

namespace {
SatchelVulkanStatus submit_transfer(uint64_t device,uint64_t source,uint64_t source_offset,uint64_t destination,uint64_t destination_offset,
    uint64_t size,uint32_t value,uint64_t *output,SatchelVulkanCancelled cancelled,void *context) {
    if(!output)return SATCHEL_VULKAN_INVALID;*output=0;
    if(!size || size>SATCHEL_VULKAN_BUFFER_MAX_SIZE || (!source && ((destination_offset|size)&3)))return SATCHEL_VULKAN_INVALID;
    if(source && source==destination)return SATCHEL_VULKAN_UNSUPPORTED;
    if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;
    Unlock unlock;
    try {
        if(cancelled && cancelled(context))return SATCHEL_VULKAN_CANCELLED;
        auto *parent=find_device(device);auto *dst=find_buffer(device,destination);auto *src=source?find_buffer(device,source):nullptr;
        if(!parent || !dst || (source && !src))return SATCHEL_VULKAN_INVALID;
        if(parent->job->persistent_pending)return SATCHEL_VULKAN_BUSY;
        if(destination_offset>dst->resource->info.size || size>dst->resource->info.size-destination_offset ||
            (src && (source_offset>src->resource->info.size || size>src->resource->info.size-source_offset)))return SATCHEL_VULKAN_INVALID;
        if(!(dst->resource->info.usage&SATCHEL_VULKAN_BUFFER_TRANSFER_DESTINATION) ||
            (src && !(src->resource->info.usage&SATCHEL_VULKAN_BUFFER_TRANSFER_SOURCE)))return SATCHEL_VULKAN_UNSUPPORTED;
        Submission **slot=nullptr;for(auto &candidate:submissions)if(!candidate){slot=&candidate;break;}
        if(!slot || next_submission==(UINT64_C(1)<<63))return SATCHEL_VULKAN_BUSY;
        auto job=std::make_unique<Submission>();job->device=parent->job->device;job->parent=device;job->source=source;job->destination=destination;
        job->retained_buffers[0]=destination;job->retained_count=1;if(source)job->retained_buffers[job->retained_count++]=source;
        VkCommandPoolCreateInfo pool={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool.queueFamilyIndex=parent->job->queue_family;
        if(vkCreateCommandPool(job->device,&pool,nullptr,&job->pool)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
        VkCommandBufferAllocateInfo allocate={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};allocate.commandPool=job->pool;allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate.commandBufferCount=1;
        VkCommandBuffer command=VK_NULL_HANDLE;if(vkAllocateCommandBuffers(job->device,&allocate,&command)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
        VkCommandBufferBeginInfo begin={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if(vkBeginCommandBuffer(command,&begin)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
        VkBufferMemoryBarrier barriers[2]={};const unsigned barrier_count=src?2:1;
        for(unsigned i=0;i<barrier_count;i++){
            auto &barrier=barriers[i];barrier.sType=VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
            barrier.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask=i?VK_ACCESS_TRANSFER_READ_BIT:VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
            barrier.buffer=i?src->resource->buffer:dst->resource->buffer;barrier.offset=i?source_offset:destination_offset;barrier.size=size;
        }
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,0,nullptr,barrier_count,barriers,0,nullptr);
        if(src){VkBufferCopy region={source_offset,destination_offset,size};vkCmdCopyBuffer(command,src->resource->buffer,dst->resource->buffer,1,&region);}
        else vkCmdFillBuffer(command,dst->resource->buffer,destination_offset,size,value);
        auto &visible=barriers[0];visible.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;visible.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&visible,0,nullptr);
        if(vkEndCommandBuffer(command)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
        VkFenceCreateInfo fence={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};if(vkCreateFence(job->device,&fence,nullptr,&job->fence)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
        if(cancelled && cancelled(context))return SATCHEL_VULKAN_CANCELLED;
        // Publish ownership before attempting submission, so even driver errors
        // cannot lose potentially referenced command/buffer/device resources.
        job->token=next_submission++;job->attempted=true;dst->resource->in_flight=true;if(src)src->resource->in_flight=true;
        parent->job->persistent_pending=true;*slot=job.release();*output=(*slot)->token;
        VkSubmitInfo submit={VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;
        return vkQueueSubmit(parent->job->queue,1,&submit,(*slot)->fence)==VK_SUCCESS?SATCHEL_VULKAN_OK:SATCHEL_VULKAN_API_ERROR;
    }catch(...){return SATCHEL_VULKAN_INTERNAL_ERROR;}
}
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_buffer_fill(uint64_t device,uint64_t buffer,uint64_t offset,uint64_t size,
    uint32_t value,uint64_t *submission,SatchelVulkanCancelled cancelled,void *context) {
    return submit_transfer(device,0,0,buffer,offset,size,value,submission,cancelled,context);
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_buffer_copy(uint64_t device,uint64_t source,uint64_t source_offset,uint64_t destination,
    uint64_t destination_offset,uint64_t size,uint64_t *submission,SatchelVulkanCancelled cancelled,void *context) {
    if(!source){if(submission)*submission=0;return SATCHEL_VULKAN_INVALID;}
    return submit_transfer(device,source,source_offset,destination,destination_offset,size,0,submission,cancelled,context);
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_transfer_batch(uint64_t device,const SatchelVulkanTransferCommand *input,size_t count,
    uint64_t *output,SatchelVulkanCancelled cancelled,void *context) {
    if(!output)return SATCHEL_VULKAN_INVALID;*output=0;
    if(!input || !count || count>SATCHEL_VULKAN_TRANSFER_COMMAND_LIMIT)return SATCHEL_VULKAN_INVALID;
    SatchelVulkanTransferCommand commands[SATCHEL_VULKAN_TRANSFER_COMMAND_LIMIT]={};std::copy_n(input,count,commands);
    if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;Unlock unlock;
    try {
        if(cancelled && cancelled(context))return SATCHEL_VULKAN_CANCELLED;
        auto *parent=find_device(device);if(!parent)return SATCHEL_VULKAN_INVALID;if(parent->job->persistent_pending)return SATCHEL_VULKAN_BUSY;
        BufferSlot *sources[SATCHEL_VULKAN_TRANSFER_COMMAND_LIMIT]={},*destinations[SATCHEL_VULKAN_TRANSFER_COMMAND_LIMIT]={},*retained[SATCHEL_VULKAN_BUFFER_LIMIT]={};
        uint32_t retained_count=0;uint64_t work=0;
        const auto retain=[&](BufferSlot *buffer){for(uint32_t i=0;i<retained_count;i++)if(retained[i]==buffer)return true;
            if(retained_count==SATCHEL_VULKAN_BUFFER_LIMIT)return false;retained[retained_count++]=buffer;return true;};
        for(size_t i=0;i<count;i++){
            const auto &c=commands[i];const bool copy=c.kind==SATCHEL_VULKAN_TRANSFER_COPY;
            if((!copy && c.kind!=SATCHEL_VULKAN_TRANSFER_FILL) || !c.size || c.size>SATCHEL_VULKAN_BUFFER_MAX_SIZE ||
                (copy?(!c.source || c.value):((c.destination_offset|c.size)&3 || c.source || c.source_offset)))return SATCHEL_VULKAN_INVALID;
            const uint64_t factor=copy?2:1;if(c.size>(SATCHEL_VULKAN_TRANSFER_WORK_LIMIT-work)/factor)return SATCHEL_VULKAN_INVALID;work+=c.size*factor;
            auto *dst=find_buffer(device,c.destination);auto *src=copy?find_buffer(device,c.source):nullptr;
            if(!dst || (copy && !src))return SATCHEL_VULKAN_INVALID;
            if(c.destination_offset>dst->resource->info.size || c.size>dst->resource->info.size-c.destination_offset ||
                (src && (c.source_offset>src->resource->info.size || c.size>src->resource->info.size-c.source_offset)))return SATCHEL_VULKAN_INVALID;
            if(!(dst->resource->info.usage&SATCHEL_VULKAN_BUFFER_TRANSFER_DESTINATION) || (src && !(src->resource->info.usage&SATCHEL_VULKAN_BUFFER_TRANSFER_SOURCE)))return SATCHEL_VULKAN_UNSUPPORTED;
            if(src==dst && c.source_offset<c.destination_offset+c.size && c.destination_offset<c.source_offset+c.size)return SATCHEL_VULKAN_UNSUPPORTED;
            if(dst->resource->in_flight || (src && src->resource->in_flight))return SATCHEL_VULKAN_BUSY;
            if(!retain(dst) || (src && !retain(src)))return SATCHEL_VULKAN_BUSY;
            sources[i]=src;destinations[i]=dst;
        }
        Submission **slot=nullptr;for(auto &candidate:submissions)if(!candidate){slot=&candidate;break;}
        if(!slot || next_submission==(UINT64_C(1)<<63))return SATCHEL_VULKAN_BUSY;
        auto job=std::make_unique<Submission>();job->device=parent->job->device;job->parent=device;job->retained_count=retained_count;
        for(uint32_t i=0;i<retained_count;i++)job->retained_buffers[i]=retained[i]->token;
        VkCommandPoolCreateInfo pool={VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pool.queueFamilyIndex=parent->job->queue_family;
        if(vkCreateCommandPool(job->device,&pool,nullptr,&job->pool)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
        VkCommandBufferAllocateInfo allocate={VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};allocate.commandPool=job->pool;allocate.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocate.commandBufferCount=1;
        VkCommandBuffer command=VK_NULL_HANDLE;if(vkAllocateCommandBuffers(job->device,&allocate,&command)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
        VkCommandBufferBeginInfo begin={VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if(vkBeginCommandBuffer(command,&begin)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
        VkMemoryBarrier ordered={VK_STRUCTURE_TYPE_MEMORY_BARRIER};ordered.srcAccessMask=VK_ACCESS_HOST_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;
        ordered.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_HOST_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&ordered,0,nullptr,0,nullptr);
        for(size_t i=0;i<count;i++){
            const auto &c=commands[i];
            if(i){ordered.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;
                vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&ordered,0,nullptr,0,nullptr);}
            if(sources[i]){VkBufferCopy region={c.source_offset,c.destination_offset,c.size};vkCmdCopyBuffer(command,sources[i]->resource->buffer,destinations[i]->resource->buffer,1,&region);}
            else vkCmdFillBuffer(command,destinations[i]->resource->buffer,c.destination_offset,c.size,c.value);
        }
        ordered.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;ordered.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&ordered,0,nullptr,0,nullptr);
        if(vkEndCommandBuffer(command)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
        VkFenceCreateInfo fence={VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};if(vkCreateFence(job->device,&fence,nullptr,&job->fence)!=VK_SUCCESS)return SATCHEL_VULKAN_API_ERROR;
        if(cancelled && cancelled(context))return SATCHEL_VULKAN_CANCELLED;
        job->token=next_submission++;job->attempted=true;
        for(uint32_t i=0;i<retained_count;i++)retained[i]->resource->in_flight=true;
        parent->job->persistent_pending=true;*slot=job.release();*output=(*slot)->token;
        VkSubmitInfo submit={VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;
        return vkQueueSubmit(parent->job->queue,1,&submit,(*slot)->fence)==VK_SUCCESS?SATCHEL_VULKAN_OK:SATCHEL_VULKAN_API_ERROR;
    }catch(...){return SATCHEL_VULKAN_INTERNAL_ERROR;}
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_submission_wait(uint64_t device,uint64_t token,uint32_t timeout_ms,
    SatchelVulkanCancelled cancelled,void *context,SatchelVulkanSubmissionReport *report) {
    if(!report)return SATCHEL_VULKAN_INVALID;*report={};
    const auto result=[&](SatchelVulkanStatus status){report->status=status;return status;};
    if(timeout_ms>SATCHEL_VULKAN_SUBMISSION_WAIT_LIMIT_MS)return result(SATCHEL_VULKAN_INVALID);
    if(gate.test_and_set(std::memory_order_acquire))return result(SATCHEL_VULKAN_BUSY);
    Unlock unlock;
    Submission **slot=nullptr;for(auto &candidate:submissions)if(candidate && candidate->token==token && candidate->parent==device){slot=&candidate;break;}
    if(!slot)return result(SATCHEL_VULKAN_INVALID);
    report->resources_retained=1;
    try {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(timeout_ms);
        while(true){
            if(cancelled && cancelled(context))return result(SATCHEL_VULKAN_CANCELLED);
            const auto remaining=std::chrono::duration_cast<std::chrono::nanoseconds>(deadline-std::chrono::steady_clock::now()).count();
            const uint64_t wait=remaining>0?static_cast<uint64_t>(std::min<int64_t>(remaining,10000000)):0;
            const auto waited=vkWaitForFences((*slot)->device,1,&(*slot)->fence,VK_TRUE,wait);report->vk_result=waited;
            if(waited==VK_SUCCESS){
                const auto finished=finish_submission(slot);if(finished!=SATCHEL_VULKAN_OK)return result(finished);
                report->completed=1;report->resources_retained=0;return result(SATCHEL_VULKAN_OK);
            }
            if(waited!=VK_TIMEOUT)return result(SATCHEL_VULKAN_API_ERROR);
            if(std::chrono::steady_clock::now()>=deadline)return result(SATCHEL_VULKAN_TIMEOUT);
        }
    }catch(...){return result(SATCHEL_VULKAN_INTERNAL_ERROR);}
}
extern "C" __attribute__((visibility("default"))) uint32_t satchel_vulkan_submission_live_count(void) {
    if(gate.test_and_set(std::memory_order_acquire))return UINT32_MAX;
    Unlock unlock;uint32_t count=0;for(const auto *job:submissions)count+=job!=nullptr;return count;
}

namespace {
struct TransferDiagnosticState {uint64_t device=0,source=0,destination=0,job=0;};
TransferDiagnosticState transfer_diagnostic;
std::atomic_flag transfer_diagnostic_gate=ATOMIC_FLAG_INIT;
struct TransferDiagnosticUnlock {~TransferDiagnosticUnlock(){transfer_diagnostic_gate.clear(std::memory_order_release);}};
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_transfer_diagnostic(SatchelVulkanTransferReport *report,
    SatchelVulkanCancelled cancelled,void *context) {
    if(!report)return SATCHEL_VULKAN_INVALID;*report={};report->stage=1;
    const auto result=[&](SatchelVulkanStatus status){report->status=status;return status;};
    if(transfer_diagnostic_gate.test_and_set(std::memory_order_acquire))return result(SATCHEL_VULKAN_BUSY);
    TransferDiagnosticUnlock unlock;auto &state=transfer_diagnostic;
    if(state.device){
        if(state.job){
            SatchelVulkanSubmissionReport previous={};
            const auto status=satchel_vulkan_submission_wait(state.device,state.job,0,nullptr,nullptr,&previous);report->vk_result=previous.vk_result;
            if(status!=SATCHEL_VULKAN_OK || !previous.completed){report->resources_retained=1;return result(SATCHEL_VULKAN_BUSY);}
            state.job=0;
        }
        if(satchel_vulkan_device_destroy(state.device)!=SATCHEL_VULKAN_OK){report->resources_retained=1;return result(SATCHEL_VULKAN_BUSY);}
        state={};report->prior_reaped=1;
    }
    SatchelVulkanBufferStats baseline={},final={};
    if(satchel_vulkan_buffer_stats(&baseline)!=SATCHEL_VULKAN_OK)return result(SATCHEL_VULKAN_BUSY);
    report->baseline_bytes=baseline.allocated_bytes;report->baseline_buffers=baseline.live_buffers;
    report->baseline_devices=satchel_vulkan_device_live_count();report->baseline_jobs=satchel_vulkan_submission_live_count();
    if(report->baseline_devices==UINT32_MAX || report->baseline_jobs==UINT32_MAX)return result(SATCHEL_VULKAN_BUSY);
    SatchelVulkanStatus status=SATCHEL_VULKAN_INTERNAL_ERROR;
    const auto stop=[&](){return cancelled && cancelled(context);};
    try {
        do {
            if(stop()){status=SATCHEL_VULKAN_CANCELLED;break;}
            SatchelGuestDeviceCaps caps={};SatchelVulkanBufferInfo info={};
            status=satchel_vulkan_device_create(0,&state.device,&caps);if(status!=SATCHEL_VULKAN_OK)break;
            status=satchel_vulkan_buffer_create(state.device,16,7,&state.source,&info);if(status!=SATCHEL_VULKAN_OK)break;
            status=satchel_vulkan_buffer_create(state.device,24,7,&state.destination,&info);if(status!=SATCHEL_VULKAN_OK)break;
            report->stage=2;if(stop()){status=SATCHEL_VULKAN_CANCELLED;break;}
            uint32_t guards[6],data[6];for(auto &word:guards)word=0xdeadbeef;
            status=satchel_vulkan_buffer_write(state.device,state.source,0,guards,16);if(status!=SATCHEL_VULKAN_OK)break;
            status=satchel_vulkan_buffer_write(state.device,state.destination,0,guards,24);if(status!=SATCHEL_VULKAN_OK)break;
            report->stage=3;
            status=satchel_vulkan_buffer_fill(state.device,state.source,4,8,0x13579bdf,&state.job,cancelled,context);if(status!=SATCHEL_VULKAN_OK)break;
            report->stage=4;SatchelVulkanSubmissionReport completed={};
            status=satchel_vulkan_submission_wait(state.device,state.job,5000,cancelled,context,&completed);report->vk_result=completed.vk_result;
            if(status!=SATCHEL_VULKAN_OK || !completed.completed)break;state.job=0;report->fill_completed=1;
            status=satchel_vulkan_buffer_read(state.device,state.source,0,data,16);if(status!=SATCHEL_VULKAN_OK)break;
            if(data[0]!=0xdeadbeef || data[1]!=0x13579bdf || data[2]!=0x13579bdf || data[3]!=0xdeadbeef){status=SATCHEL_VULKAN_MISMATCH;break;}
            report->stage=5;if(stop()){status=SATCHEL_VULKAN_CANCELLED;break;}
            status=satchel_vulkan_buffer_copy(state.device,state.source,4,state.destination,8,8,&state.job,cancelled,context);if(status!=SATCHEL_VULKAN_OK)break;
            report->stage=6;
            status=satchel_vulkan_submission_wait(state.device,state.job,5000,cancelled,context,&completed);report->vk_result=completed.vk_result;
            if(status!=SATCHEL_VULKAN_OK || !completed.completed)break;state.job=0;report->copy_completed=1;
            status=satchel_vulkan_buffer_read(state.device,state.destination,0,data,24);if(status!=SATCHEL_VULKAN_OK)break;
            report->values_matched=data[2]==0x13579bdf && data[3]==0x13579bdf;
            report->guards_matched=data[0]==0xdeadbeef && data[1]==0xdeadbeef && data[4]==0xdeadbeef && data[5]==0xdeadbeef;
            if(!report->values_matched || !report->guards_matched){status=SATCHEL_VULKAN_MISMATCH;break;}
            report->stage=7;
        }while(false);
    }catch(...){status=SATCHEL_VULKAN_INTERNAL_ERROR;}
    // Never consume ownership after an unsuccessful submitted wait. A future
    // invocation must observe its real fence before releasing these objects.
    if(!state.job && state.device){
        if(satchel_vulkan_device_destroy(state.device)==SATCHEL_VULKAN_OK)state={};
        else status=SATCHEL_VULKAN_INTERNAL_ERROR;
    }
    report->resources_retained=state.device!=0;
    const auto queried=satchel_vulkan_buffer_stats(&final);report->final_devices=satchel_vulkan_device_live_count();report->final_jobs=satchel_vulkan_submission_live_count();
    if(queried==SATCHEL_VULKAN_OK){report->final_bytes=final.allocated_bytes;report->final_buffers=final.live_buffers;}
    report->cleanup_matched=!state.device && queried==SATCHEL_VULKAN_OK && report->final_devices==report->baseline_devices &&
        report->final_jobs==report->baseline_jobs && final.live_buffers==baseline.live_buffers && final.allocated_bytes==baseline.allocated_bytes;
    if(status==SATCHEL_VULKAN_OK && !report->cleanup_matched)status=SATCHEL_VULKAN_INTERNAL_ERROR;
    if(status==SATCHEL_VULKAN_OK)report->stage=9;
    return result(status);
}

extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_device_drain(uint64_t device) {
    if(gate.test_and_set(std::memory_order_acquire))return SATCHEL_VULKAN_BUSY;
    Unlock unlock;
    try {
        const auto *parent=find_device(device);if(!parent)return SATCHEL_VULKAN_INVALID;
        if(!parent->job->persistent_pending)return SATCHEL_VULKAN_OK;
        for(auto &job:submissions)if(job && job->parent==device){
            const auto result=vkWaitForFences(job->device,1,&job->fence,VK_TRUE,0);
            if(result==VK_SUCCESS)return finish_submission(&job);
            return result==VK_TIMEOUT?SATCHEL_VULKAN_BUSY:SATCHEL_VULKAN_API_ERROR;
        }
        return SATCHEL_VULKAN_INTERNAL_ERROR;
    }catch(...){return SATCHEL_VULKAN_INTERNAL_ERROR;}
}
namespace {SatchelGuestBackendStatus backend_drain(void *,uint64_t device){return backend_status(satchel_vulkan_device_drain(device));}}
extern "C" __attribute__((visibility("default"))) int satchel_vulkan_guest_device_backend_v2(SatchelGuestDeviceBackendV2 *backend) {
    if(!backend)return 0;*backend={{2,0,nullptr,backend_create,backend_destroy,backend_caps},backend_drain};return 1;
}

#include "ScalarSchema.inc"
#include "VulkanCompute.inc"
#include "ScalarBackend.inc"

#include "VulkanRenderTarget.inc"

#include "RasterSchema.inc"
#include "VulkanRaster.inc"

#include "VulkanRasterBatch.inc"

#include "VulkanFlatSchema.inc"
#include "VulkanFlat.inc"
#include "VulkanProvoking.inc"
#include "VulkanFlatCreate.inc"
#include "VulkanCapturedHost.inc"
