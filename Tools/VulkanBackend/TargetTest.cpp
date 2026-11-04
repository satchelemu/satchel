#include "VulkanRenderTarget.h"
#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <limits>
#include <thread>
#include <chrono>
static unsigned checks,fail_framebuffer,fail_submit,submitted;
static void check_at(bool value,unsigned line){++checks;if(!value){std::fprintf(stderr,"render target assertion line%u check%u\n",line,checks);std::exit(1);}}
#define check(v) check_at((v),__LINE__)
extern "C" VkResult target_test_framebuffer(VkDevice d,const VkFramebufferCreateInfo *i,const VkAllocationCallbacks *a,VkFramebuffer *o){if(fail_framebuffer){--fail_framebuffer;return VK_ERROR_OUT_OF_HOST_MEMORY;}return vkCreateFramebuffer(d,i,a,o);}
extern "C" VkResult target_test_submit(VkQueue q,uint32_t n,const VkSubmitInfo *s,VkFence f){unsigned failed=fail_submit;fail_submit=0;VkResult actual=vkQueueSubmit(q,n,s,f);if(actual==VK_SUCCESS)++submitted;return failed && actual==VK_SUCCESS?VK_ERROR_UNKNOWN:actual;}
static int always(void*){return 1;}
static int countdown(void *p){return --*static_cast<unsigned*>(p)==0;}
static void finish(uint64_t device,uint64_t job){SatchelVulkanSubmissionReport report={};check(satchel_vulkan_submission_wait(device,job,5000,nullptr,nullptr,&report)==SATCHEL_VULKAN_OK && report.completed && !report.resources_retained);}
static SatchelVulkanTargetDescription description(unsigned w,unsigned h){return {1,sizeof(SatchelVulkanTargetDescription),SATCHEL_VULKAN_TARGET_RGBA8_UNORM,w,h,0};}
static void stats(unsigned count){SatchelVulkanTargetStats s={};check(satchel_vulkan_target_stats(&s)==SATCHEL_VULKAN_OK && s.live_targets==count && s.target_limit==SATCHEL_VULKAN_TARGET_LIMIT && s.allocated_bytes<=s.allocation_limit && s.max_target_bytes==SATCHEL_VULKAN_TARGET_BYTE_LIMIT);if(!count)check(!s.allocated_bytes);}
static void readback(uint64_t d,uint64_t b,size_t size,unsigned w,unsigned h,size_t row,size_t offset,const float color[4]){
 std::vector<uint8_t> actual(size),expected(size,0xa5);for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++)for(unsigned c=0;c<4;c++)expected[offset+y*row+x*4+c]=color[c]==1.0f?255:0;
 check(satchel_vulkan_buffer_read(d,b,0,actual.data(),size)==SATCHEL_VULKAN_OK);check(actual==expected);
}
int main(){stats(0);uint64_t device=0,other=0;SatchelGuestDeviceCaps dc={};check(satchel_vulkan_device_create(0,&device,&dc)==SATCHEL_VULKAN_OK);check(satchel_vulkan_device_create(0,&other,&dc)==SATCHEL_VULKAN_OK);
 SatchelVulkanTargetCaps caps={};check(satchel_vulkan_target_capabilities(device,SATCHEL_VULKAN_TARGET_RGBA8_UNORM,&caps)==SATCHEL_VULKAN_OK && caps.supported && caps.queue_graphics && caps.samples==1 && caps.max_width<=2048 && caps.max_height<=2048 && caps.max_logical_bytes==SATCHEL_VULKAN_TARGET_BYTE_LIMIT);check(satchel_vulkan_target_capabilities(0,1,&caps)==SATCHEL_VULKAN_INVALID);check(satchel_vulkan_target_capabilities(device,999,&caps)==SATCHEL_VULKAN_UNSUPPORTED);
 SatchelVulkanTargetInfo info={};uint64_t target=9;auto request=description(13,7);
 for(unsigned bad=0;bad<7;bad++){auto invalid=request;if(bad==0)invalid.width=0;if(bad==1)invalid.height=0;if(bad==2)invalid.width=UINT32_MAX;if(bad==3)invalid.flags=1;if(bad==4)invalid.format=2;if(bad==5){invalid.width=2048;invalid.height=2048;}if(bad==6)invalid.version=2;check(satchel_vulkan_target_create(device,&invalid,&target,&info)!=SATCHEL_VULKAN_OK && !target && !info.allocation_size);stats(0);}
 const unsigned sizes[][2]={{1,1},{13,7},{64,32},{127,19},{320,180}};
 for(const auto &size:sizes){request=description(size[0],size[1]);check(satchel_vulkan_target_create(device,&request,&target,&info)==SATCHEL_VULKAN_OK && target && info.allocation_size>=info.logical_bytes && info.logical_bytes==(uint64_t)size[0]*size[1]*4 && !info.initialized && !info.in_flight);stats(1);
  uint64_t buffer=0;const size_t row=size[0]*4+20,offset=32,bytes=offset+(size[1]-1)*row+size[0]*4+32;SatchelVulkanBufferInfo bi={};check(satchel_vulkan_buffer_create(device,bytes,SATCHEL_VULKAN_BUFFER_TRANSFER_DESTINATION,&buffer,&bi)==SATCHEL_VULKAN_OK);std::vector<uint8_t> sentinel(bytes,0xa5);
  const float colors[][4]={{1,0,0,1},{0,1,0,1},{0,0,1,0},{1,1,1,1}};
  for(const auto &color:colors){check(satchel_vulkan_buffer_write(device,buffer,0,sentinel.data(),bytes)==SATCHEL_VULKAN_OK);uint64_t job=0;check(satchel_vulkan_target_clear_readback(device,target,buffer,offset,row,color,&job,nullptr,nullptr)==SATCHEL_VULKAN_OK && job);
   check(satchel_vulkan_target_info(device,target,&info)==SATCHEL_VULKAN_OK && info.in_flight);check(satchel_vulkan_target_destroy(device,target)==SATCHEL_VULKAN_BUSY);check(satchel_vulkan_device_destroy(device)==SATCHEL_VULKAN_BUSY);check(satchel_vulkan_buffer_destroy(device,buffer)==SATCHEL_VULKAN_BUSY);uint8_t byte=0;check(satchel_vulkan_buffer_read(device,buffer,0,&byte,1)==SATCHEL_VULKAN_BUSY);check(satchel_vulkan_buffer_write(device,buffer,0,&byte,1)==SATCHEL_VULKAN_BUSY);
   uint64_t attempted=99;check(satchel_vulkan_target_clear_readback(device,target,buffer,offset,row,color,&attempted,nullptr,nullptr)==SATCHEL_VULKAN_BUSY && !attempted);
   SatchelVulkanSubmissionReport pending={};check(satchel_vulkan_submission_wait(device,job,0,always,nullptr,&pending)==SATCHEL_VULKAN_CANCELLED && pending.resources_retained && !pending.completed);finish(device,job);
   check(satchel_vulkan_target_info(device,target,&info)==SATCHEL_VULKAN_OK && info.initialized && !info.in_flight);readback(device,buffer,bytes,size[0],size[1],row,offset,color);
  }
  check(satchel_vulkan_target_destroy(other,target)==SATCHEL_VULKAN_INVALID);check(satchel_vulkan_target_info(device,buffer,&info)==SATCHEL_VULKAN_INVALID);check(satchel_vulkan_buffer_info(device,target,&bi)==SATCHEL_VULKAN_INVALID);
  const float color[4]={1,0,1,1};uint64_t job=99;const uint64_t offsets[]={UINT64_MAX-3,bytes,33};for(uint64_t invalid:offsets)check(satchel_vulkan_target_clear_readback(device,target,buffer,invalid,row,color,&job,nullptr,nullptr)==SATCHEL_VULKAN_INVALID && !job);
  check(satchel_vulkan_target_clear_readback(device,target,buffer,offset,size[0]*4-1,color,&job,nullptr,nullptr)==SATCHEL_VULKAN_INVALID && !job);check(satchel_vulkan_target_clear_readback(device,target,buffer,offset,UINT64_MAX-3,color,&job,nullptr,nullptr)==SATCHEL_VULKAN_INVALID && !job);
  for(float bad:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-1.0f,1.1f}){float invalid[4]={bad,0,0,1};check(satchel_vulkan_target_clear_readback(device,target,buffer,offset,row,invalid,&job,nullptr,nullptr)==SATCHEL_VULKAN_UNSUPPORTED && !job);}
  for(unsigned point=1;point<=2;point++){unsigned remaining=point;check(satchel_vulkan_target_clear_readback(device,target,buffer,offset,row,color,&job,countdown,&remaining)==SATCHEL_VULKAN_CANCELLED && !job && !satchel_vulkan_submission_live_count());}
  fail_framebuffer=1;check(satchel_vulkan_target_clear_readback(device,target,buffer,offset,row,color,&job,nullptr,nullptr)==SATCHEL_VULKAN_API_ERROR && !job);check(satchel_vulkan_target_info(device,target,&info)==SATCHEL_VULKAN_OK && !info.in_flight);stats(1);
  check(satchel_vulkan_buffer_write(device,buffer,0,sentinel.data(),bytes)==SATCHEL_VULKAN_OK);fail_submit=1;check(satchel_vulkan_target_clear_readback(device,target,buffer,offset,row,color,&job,nullptr,nullptr)==SATCHEL_VULKAN_API_ERROR && job);check(satchel_vulkan_target_destroy(device,target)==SATCHEL_VULKAN_BUSY);finish(device,job);readback(device,buffer,bytes,size[0],size[1],row,offset,color);
  check(satchel_vulkan_buffer_destroy(device,buffer)==SATCHEL_VULKAN_OK);const uint64_t stale=target;check(satchel_vulkan_target_destroy(device,target)==SATCHEL_VULKAN_OK);check(satchel_vulkan_target_destroy(device,stale)==SATCHEL_VULKAN_INVALID);check(satchel_vulkan_target_info(device,stale,&info)==SATCHEL_VULKAN_INVALID);stats(0);
 }
 // Count cap and padded allocation totals are independent of logical texel size.
 uint64_t tokens[SATCHEL_VULKAN_TARGET_LIMIT];uint64_t sum=0;request=description(1,1);for(auto &token:tokens){check(satchel_vulkan_target_create(device,&request,&token,&info)==SATCHEL_VULKAN_OK);sum+=info.allocation_size;}stats(SATCHEL_VULKAN_TARGET_LIMIT);SatchelVulkanTargetStats total={};check(satchel_vulkan_target_stats(&total)==SATCHEL_VULKAN_OK && total.allocated_bytes==sum);check(satchel_vulkan_target_create(device,&request,&target,&info)==SATCHEL_VULKAN_BUSY && !target);check(satchel_vulkan_device_destroy(device)==SATCHEL_VULKAN_OK);stats(0);for(auto token:tokens)check(satchel_vulkan_target_destroy(device,token)==SATCHEL_VULKAN_INVALID);
 // Aggregate memory cap charges VkMemoryRequirements, not requested texel bytes.
 request=description(2048,1024);std::vector<uint64_t> large;uint64_t allocated=0;
 for(unsigned i=0;i<SATCHEL_VULKAN_TARGET_LIMIT;i++){auto status=satchel_vulkan_target_create(other,&request,&target,&info);if(status==SATCHEL_VULKAN_BUSY)break;check(status==SATCHEL_VULKAN_OK);large.push_back(target);allocated+=info.allocation_size;}
 check(!large.empty() && large.size()<SATCHEL_VULKAN_TARGET_LIMIT);check(satchel_vulkan_target_stats(&total)==SATCHEL_VULKAN_OK && total.allocated_bytes==allocated && allocated<=SATCHEL_VULKAN_TARGET_MEMORY_LIMIT);check(satchel_vulkan_target_create(other,&request,&target,&info)==SATCHEL_VULKAN_BUSY && !target);
 for(auto token:large)check(satchel_vulkan_target_destroy(other,token)==SATCHEL_VULKAN_OK);stats(0);
 // Existing device drain must consume a target job before parent destruction.
 request=description(4,4);check(satchel_vulkan_target_create(other,&request,&target,&info)==SATCHEL_VULKAN_OK);uint64_t buffer=0,job=0;SatchelVulkanBufferInfo bi={};check(satchel_vulkan_buffer_create(other,64,SATCHEL_VULKAN_BUFFER_TRANSFER_DESTINATION,&buffer,&bi)==SATCHEL_VULKAN_OK);const float green[4]={0,1,0,1};check(satchel_vulkan_target_clear_readback(other,target,buffer,0,16,green,&job,nullptr,nullptr)==SATCHEL_VULKAN_OK && job);
 SatchelVulkanStatus drained=SATCHEL_VULKAN_BUSY;for(unsigned i=0;i<5000 && drained==SATCHEL_VULKAN_BUSY;i++){drained=satchel_vulkan_device_drain(other);if(drained==SATCHEL_VULKAN_BUSY)std::this_thread::sleep_for(std::chrono::milliseconds(1));}check(drained==SATCHEL_VULKAN_OK);check(satchel_vulkan_target_info(other,target,&info)==SATCHEL_VULKAN_OK && info.initialized && !info.in_flight);check(satchel_vulkan_device_destroy(other)==SATCHEL_VULKAN_OK);stats(0);check(!satchel_vulkan_submission_live_count() && !satchel_vulkan_device_live_count());SatchelVulkanBufferStats bs={};check(satchel_vulkan_buffer_stats(&bs)==SATCHEL_VULKAN_OK && !bs.live_buffers && !bs.allocated_bytes);
 std::printf("Real Vulkan render targets: %u ASan/UBSan assertions,%u GPU clear/readback submissions, padded rows/guards, repeated layouts, cancellation/error ownership, typed IDs and cleanup. No guest texture/draw/present claim.\n",checks,submitted);return 0;
}
