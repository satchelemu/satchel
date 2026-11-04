#include "CapturedHostAdapter.h"
#include "VulkanCapturedHost.h"
#include <stdint.h>
#include <string.h>
static int range(const void *p,size_t n){return p&&(uintptr_t)p<=UINTPTR_MAX-n;}
static int overlap(const void*a,size_t an,const void*b,size_t bn){return (uintptr_t)a<(uintptr_t)b+bn&&(uintptr_t)b<(uintptr_t)a+an;}
int satchel_captured_compile_for_target(uint64_t device,uint64_t target,const SatchelCapturedRequest *request,SatchelCFPairV2 *out,uint32_t size){
 if(size!=sizeof(*out)||!range(request,sizeof(*request))||!range(out,sizeof(*out))||((uintptr_t)request%_Alignof(SatchelCapturedRequest))||((uintptr_t)out%_Alignof(SatchelCFPairV2))||overlap(request,sizeof(*request),out,sizeof(*out)))return SATCHEL_CF_INVALID;
 SatchelCapturedRequest r=*request;if(r.version!=1||r.size!=sizeof(r))return SATCHEL_CF_INVALID;
 SatchelVulkanCapturedHost facts={0};SatchelVulkanStatus status=satchel_vulkan_captured_host(device,target,&facts);
 if(status!=SATCHEL_VULKAN_OK)return status==SATCHEL_VULKAN_BUSY?SATCHEL_CF_BUSY:status==SATCHEL_VULKAN_UNSUPPORTED?SATCHEL_CF_UNSUPPORTED:status==SATCHEL_VULKAN_INVALID?SATCHEL_CF_INVALID:SATCHEL_CF_INTERNAL;
 if(facts.version!=1||facts.size!=sizeof(facts)||facts.profile!=1)return SATCHEL_CF_UNSUPPORTED;
 SatchelCFInputV2 input={0};input.version=2;input.size=sizeof(input);input.device_known=facts.device_known;input.policy_known=facts.policy_known;input.requested_source_unused=facts.unknown;
 input.input.version=1;input.input.size=sizeof(input.input);input.input.vertex=r.vertex;input.input.fragment=r.fragment;input.input.state=r.state;input.input.max_work=r.max_work;input.input.timeout_ns=r.timeout_ns;input.input.cancelled=r.cancelled;input.input.context=r.context;
 input.input.host.known=facts.device_known|facts.policy_known;input.input.host.values=facts.values;input.input.host.storage_alignment=facts.storage_alignment;input.input.host.physical_bgra8_color_attachment=facts.physical_bgra8_color_attachment;input.input.host.admitted_color_format=facts.admitted_color_format;input.input.host.current_color_format=facts.current_color_format;
 return satchel_captured_flat_compile_v2(&input,out,size);
}
