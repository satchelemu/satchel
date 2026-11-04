#include "VulkanFlat.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>
#include <string>
#include <vulkan/vulkan.h>
static unsigned checks,fail_pipeline,fail_submit;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"flat assertion line%d check%u\n",__LINE__,checks);std::abort();}}while(0)
extern "C" VkResult flat_test_pipelines(VkDevice d,VkPipelineCache c,uint32_t n,const VkGraphicsPipelineCreateInfo*p,const VkAllocationCallbacks*a,VkPipeline*out){if(fail_pipeline){--fail_pipeline;return VK_ERROR_OUT_OF_HOST_MEMORY;}return vkCreateGraphicsPipelines(d,c,n,p,a,out);}
extern "C" VkResult flat_test_submit(VkQueue q,uint32_t n,const VkSubmitInfo*p,VkFence f){auto r=vkQueueSubmit(q,n,p,f);if(fail_submit&&r==VK_SUCCESS){--fail_submit;return VK_ERROR_DEVICE_LOST;}return r;}
static SatchelVulkanFlatModule load(const std::string &path){SatchelVulkanFlatModule m={};m.version=1;m.size=sizeof(m);std::ifstream f(path,std::ios::binary|std::ios::ate);CHECK(f.good());auto size=f.tellg();CHECK(size>=20&&size<=static_cast<std::streamoff>(sizeof(m.words))&&size%4==0);m.word_count=(uint32_t)(size/4);f.seekg(0);CHECK((bool)f.read((char*)m.words,size));return m;}
static std::vector<unsigned> ops(const SatchelVulkanFlatModule&m,unsigned op){std::vector<unsigned> result;for(unsigned i=5;i<m.word_count;i+=m.words[i]>>16)if((m.words[i]&65535)==op)result.push_back(i);return result;}
static void reject(const SatchelVulkanFlatModule&v,const SatchelVulkanFlatModule&f){SatchelVulkanFlatPair p;std::memset(&p,0xa5,sizeof(p));auto before=p;CHECK(satchel_vulkan_flat_validate_pair(&v,&f,&p)==SATCHEL_VULKAN_UNSUPPORTED);CHECK(!std::memcmp(&p,&before,sizeof(p)));}
static SatchelVulkanFlatDescription description(){SatchelVulkanFlatDescription d={};d.version=1;d.size=sizeof(d);d.provoking_known=1;auto &r=d.raster;r.version=1;r.size=sizeof(r);r.state={255,1,1,1,0,1,1,0,0};r.format=1;r.samples=1;r.topology=3;r.color_mask=15;r.sample_mask=1;r.vertex_stride=32;for(auto &o:r.attribute_offsets)o=UINT32_MAX;r.attribute_offsets[0]=0;r.attribute_offsets[1]=16;return d;}
static int cancel(void*){return 1;}
static void wait(uint64_t d,uint64_t j){SatchelVulkanSubmissionReport r={};CHECK(satchel_vulkan_submission_wait(d,j,5000,nullptr,nullptr,&r)==SATCHEL_VULKAN_OK&&r.completed&&!r.resources_retained);}
static unsigned pipelines(){SatchelVulkanRasterStats s={};CHECK(satchel_vulkan_raster_stats(&s)==SATCHEL_VULKAN_OK);return s.live_pipelines;}
static void malformed(const SatchelVulkanFlatModule&v,const SatchelVulkanFlatModule&f){
 for(unsigned index:{0u,1u,3u,4u}){auto m=v;m.words[index]^=0xffffffff;reject(m,f);}
 for(unsigned n:{0u,4u,4097u}){auto m=v;m.word_count=n;reject(m,f);}reject(f,v);
 {auto m=v;m.words[5]=17;reject(m,f);m=v;m.words[5]=0xffff0011;reject(m,f);m=v;m.words[6]=10;reject(m,f);}
 for(unsigned at:ops(v,65)){auto m=v;m.words[at+1]=1;reject(m,f);m=v;m.words[at+3]=13;reject(m,f);}
 for(unsigned at:ops(v,61)){auto m=v;m.words[at+1]=1;reject(m,f);m=v;m.words[at+3]=0;reject(m,f);}
 for(unsigned at:ops(v,62)){auto m=v;m.words[at+2]=1;reject(m,f);}
 for(unsigned at:ops(v,43))if(v.words[at+1]==6){auto m=v;m.words[at+3]=4;reject(m,f);m=v;m.words[at+3]=UINT32_MAX;reject(m,f);}
 for(unsigned at:ops(v,72)){auto m=v;m.words[at+2]=8;reject(m,f);m=v;m.words[at+4]^=1;reject(m,f);}
 for(unsigned at:ops(v,32)){auto m=v;m.words[at+2]=7;reject(m,f);}
 for(unsigned at:ops(f,71)){auto m=f;if(f.words[at+2]==14){m.words[at+2]=13;reject(v,m);}if(f.words[at+2]==30&&f.words[at+1]==16){m.words[at+3]=7;reject(v,m);}}
 {auto m=v;unsigned at=ops(m,15)[0];m.words[at+5]=13;reject(m,f);m=v;at=ops(m,62)[0];m.words[at+2]=39;reject(m,f);}
 // SPIR-V forbids duplicate non-aggregate types, including unused ones.
 for(unsigned op:{19u,21u,22u,23u,32u,33u})for(unsigned at:ops(v,op)){auto m=v;unsigned count=m.words[at]>>16,where=ops(m,54)[0];std::memmove(m.words+where+count,m.words+where,(m.word_count-where)*4);std::memcpy(m.words+where,v.words+at,count*4);m.words[where+1]=m.words[3]++;m.word_count+=count;reject(m,f);}
 {auto m=v;unsigned at=ops(m,5)[0];m.words[at+2]=0xffffffff;reject(m,f);}
 // Every shorter complete-prefix or partial-instruction module must reject.
 for(unsigned n=0;n<v.word_count;n++){auto m=v;m.word_count=n;reject(m,f);}
 for(unsigned n=0;n<f.word_count;n++){auto m=f;m.word_count=n;reject(v,m);}
 // No partial pair output even when the first stage independently validates.
 auto bad=f;bad.words[6]=11;reject(v,bad);
}
static unsigned mutation_audit(const SatchelVulkanFlatModule&v,const SatchelVulkanFlatModule&f,const std::string &folder){
 unsigned accepted=0;
 for(unsigned stage=0;stage<2;stage++){const auto &original=stage?f:v;for(unsigned word=0;word<original.word_count;word++)for(unsigned bit=0;bit<32;bit++){
  auto changed=original;changed.words[word]^=1u<<bit;SatchelVulkanFlatPair result;std::memset(&result,0xa5,sizeof(result));auto old=result;
  auto status=satchel_vulkan_flat_validate_pair(stage?&v:&changed,stage?&changed:&f,&result);CHECK(status==SATCHEL_VULKAN_OK||status==SATCHEL_VULKAN_UNSUPPORTED);
  if(status==SATCHEL_VULKAN_OK){std::ofstream out(folder+"/accepted-"+std::to_string(accepted++)+".spv",std::ios::binary);CHECK((bool)out.write((char*)changed.words,changed.word_count*4));}
  else CHECK(!std::memcmp(&old,&result,sizeof(old)));
 }}return accepted;
}
static void draw(uint64_t device,uint64_t target,uint64_t pipeline,uint64_t vertices,uint64_t output,unsigned w,unsigned h,const uint8_t expected[4],bool fault=false){
 const size_t pitch=w*4+16,size=64+pitch*h+64;std::vector<uint8_t> bytes(size,0xa5);CHECK(satchel_vulkan_buffer_write(device,output,0,bytes.data(),size)==SATCHEL_VULKAN_OK);
 SatchelVulkanRasterDraw r={};r.version=1;r.size=sizeof(r);r.vertex_count=3;r.vertex_buffer=vertices;r.readback_buffer=output;r.readback_offset=64;r.row_pitch=pitch;r.viewport_width=w;r.viewport_height=h;r.max_depth=1;r.scissor_width=w;r.scissor_height=h;r.clear_rgba[0]=r.clear_rgba[1]=r.clear_rgba[2]=.25f;r.clear_rgba[3]=1;
 uint64_t job=99;CHECK(satchel_vulkan_raster_draw_readback(device,pipeline,target,&r,&job,cancel,nullptr)==SATCHEL_VULKAN_CANCELLED&&!job);
 fail_submit=fault;CHECK(satchel_vulkan_raster_draw_readback(device,pipeline,target,&r,&job,nullptr,nullptr)==(fault?SATCHEL_VULKAN_API_ERROR:SATCHEL_VULKAN_OK)&&job);
 CHECK(satchel_vulkan_raster_destroy(device,pipeline)==SATCHEL_VULKAN_BUSY);CHECK(satchel_vulkan_buffer_destroy(device,vertices)==SATCHEL_VULKAN_BUSY);CHECK(satchel_vulkan_target_destroy(device,target)==SATCHEL_VULKAN_BUSY);
 SatchelVulkanSubmissionReport pending={};CHECK(satchel_vulkan_submission_wait(device,job,0,cancel,nullptr,&pending)==SATCHEL_VULKAN_CANCELLED&&pending.resources_retained);wait(device,job);
 CHECK(satchel_vulkan_buffer_read(device,output,0,bytes.data(),size)==SATCHEL_VULKAN_OK);const uint8_t clear[]={64,64,64,255};unsigned covered=0,uncovered=0;
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){const auto *p=bytes.data()+64+y*pitch+x*4;bool color=!std::memcmp(p,expected,4),background=!std::memcmp(p,clear,4);CHECK(color||background);covered+=color;uncovered+=background;}
 CHECK(covered>50&&uncovered>50);CHECK(!std::memcmp(bytes.data()+64,expected,4));CHECK(!std::memcmp(bytes.data()+64+(h-1)*pitch+(w-1)*4,clear,4));
 for(size_t i=0;i<size;i++){bool image=i>=64&&i<64+pitch*h&&((i-64)%pitch)<w*4;if(!image)CHECK(bytes[i]==0xa5);}
}
int main(int argc,char**argv){CHECK(argc==3);std::string folder=argv[1];auto v=load(folder+"/flat-vertex.spv"),f=load(folder+"/flat-fragment.spv"),v3=load(folder+"/location3-vertex-raw.spv"),f3=load(folder+"/location3-fragment-raw.spv");
 SatchelVulkanFlatPair facts={};CHECK(satchel_vulkan_flat_validate_pair(&v,&f,&facts)==SATCHEL_VULKAN_OK);CHECK(facts.vertex.inputs[0]==15&&facts.vertex.inputs[1]==15&&facts.vertex.outputs[0]==15&&!facts.vertex.flat_outputs[0]&&facts.fragment.inputs[0]==15&&facts.fragment.flat_inputs[0]==15);CHECK(facts.vertex.position_components==15&&facts.vertex.point_size_one==1&&facts.fragment.color_components==15);CHECK(facts.vertex.declared_uniforms==1&&facts.fragment.declared_uniforms==1&&!facts.vertex.active_uniforms&&!facts.fragment.active_uniforms&&facts.vertex.support_member_extent==1380&&facts.fragment.support_member_extent==1380);CHECK(facts.vertex.declared_clip_components==8&&facts.vertex.declared_cull_components==1&&!facts.vertex.active_clip_components&&!facts.vertex.active_cull_components);CHECK(facts.vertex.required_capabilities==1&&facts.fragment.required_capabilities==1);
 CHECK(satchel_vulkan_flat_validate_pair(&v3,&f3,&facts)==SATCHEL_VULKAN_OK&&facts.vertex.outputs[3]==15&&facts.fragment.inputs[3]==15);reject(v,f3);reject(v3,f);malformed(v,f);unsigned accepted=mutation_audit(v,f,argv[2]);
 uint64_t device=0;SatchelGuestDeviceCaps caps={};CHECK(satchel_vulkan_device_create(0,&device,&caps)==SATCHEL_VULKAN_OK);SatchelVulkanFlatCaps flat={};CHECK(satchel_vulkan_flat_capabilities(device,&flat)==SATCHEL_VULKAN_OK&&flat.graphics_queue&&flat.first_enabled&&!flat.last_enabled);
 auto desc=description();SatchelVulkanRasterInfo info={};uint64_t pipeline=99;auto invalid=desc;invalid.provoking_mode=1;CHECK(satchel_vulkan_flat_create(device,&invalid,&v,&f,&pipeline,&info,&facts)==SATCHEL_VULKAN_UNSUPPORTED&&!pipeline);invalid=desc;invalid.provoking_known=0;CHECK(satchel_vulkan_flat_create(device,&invalid,&v,&f,&pipeline,&info,&facts)==SATCHEL_VULKAN_UNSUPPORTED&&!pipeline);
 invalid=desc;invalid.raster.state.known=247;CHECK(satchel_vulkan_flat_create(device,&invalid,&v,&f,&pipeline,&info,&facts)==SATCHEL_VULKAN_UNSUPPORTED&&!pipeline);invalid=desc;invalid.raster.state.y_negate=1;CHECK(satchel_vulkan_flat_create(device,&invalid,&v,&f,&pipeline,&info,&facts)==SATCHEL_VULKAN_UNSUPPORTED&&!pipeline);
 fail_pipeline=1;CHECK(satchel_vulkan_flat_create(device,&desc,&v,&f,&pipeline,&info,&facts)==SATCHEL_VULKAN_API_ERROR&&!pipeline&&!pipelines());CHECK(satchel_vulkan_flat_create(device,&desc,&v,&f,&pipeline,&info,&facts)==SATCHEL_VULKAN_OK&&pipeline&&info.attribute_mask==3);
 uint64_t target=0,vertices=0,output=0;SatchelVulkanTargetDescription td={1,sizeof(td),1,32,24,0};SatchelVulkanTargetInfo ti={};SatchelVulkanBufferInfo bi={};CHECK(satchel_vulkan_target_create(device,&td,&target,&ti)==SATCHEL_VULKAN_OK);CHECK(satchel_vulkan_buffer_create(device,96,SATCHEL_VULKAN_BUFFER_VERTEX,&vertices,&bi)==SATCHEL_VULKAN_OK);CHECK(satchel_vulkan_buffer_create(device,64+(32*4+16)*24+64,SATCHEL_VULKAN_BUFFER_TRANSFER_DESTINATION,&output,&bi)==SATCHEL_VULKAN_OK);
 {uint64_t short_buffer=0;CHECK(satchel_vulkan_buffer_create(device,95,SATCHEL_VULKAN_BUFFER_VERTEX,&short_buffer,&bi)==SATCHEL_VULKAN_OK);SatchelVulkanRasterDraw short_draw={};short_draw.version=1;short_draw.size=sizeof(short_draw);short_draw.vertex_count=3;short_draw.vertex_buffer=short_buffer;short_draw.readback_buffer=output;short_draw.row_pitch=128;short_draw.viewport_width=32;short_draw.viewport_height=24;short_draw.max_depth=1;short_draw.scissor_width=32;short_draw.scissor_height=24;uint64_t job=99;CHECK(satchel_vulkan_raster_draw_readback(device,pipeline,target,&short_draw,&job,nullptr,nullptr)==SATCHEL_VULKAN_INVALID&&!job);CHECK(satchel_vulkan_buffer_destroy(device,short_buffer)==SATCHEL_VULKAN_OK);}
 float data[24]={-1,-1,.5f,1, 1,0,0,1, 1,-1,.5f,1, 0,1,0,1, -1,1,.5f,1, 0,0,1,1};CHECK(satchel_vulkan_buffer_write(device,vertices,0,data,sizeof(data))==SATCHEL_VULKAN_OK);const uint8_t red[]={255,0,0,255},green[]={0,255,0,255};draw(device,target,pipeline,vertices,output,32,24,red);
 data[4]=0;data[5]=1;data[12]=1;data[13]=0;CHECK(satchel_vulkan_buffer_write(device,vertices,0,data,sizeof(data))==SATCHEL_VULKAN_OK);draw(device,target,pipeline,vertices,output,32,24,green,true);CHECK(satchel_vulkan_raster_destroy(device,pipeline)==SATCHEL_VULKAN_OK);
 CHECK(satchel_vulkan_flat_create(device,&desc,&v3,&f3,&pipeline,&info,&facts)==SATCHEL_VULKAN_OK);draw(device,target,pipeline,vertices,output,32,24,green);CHECK(satchel_vulkan_raster_destroy(device,pipeline)==SATCHEL_VULKAN_OK);
 uint64_t all[SATCHEL_VULKAN_RASTER_PIPELINE_LIMIT]={};for(auto &p:all)CHECK(satchel_vulkan_flat_create(device,&desc,&v,&f,&p,&info,&facts)==SATCHEL_VULKAN_OK);CHECK(satchel_vulkan_flat_create(device,&desc,&v,&f,&pipeline,&info,&facts)==SATCHEL_VULKAN_BUSY&&!pipeline);for(auto p:all)CHECK(satchel_vulkan_raster_destroy(device,p)==SATCHEL_VULKAN_OK);CHECK(!pipelines());
 CHECK(satchel_vulkan_buffer_destroy(device,output)==SATCHEL_VULKAN_OK);CHECK(satchel_vulkan_buffer_destroy(device,vertices)==SATCHEL_VULKAN_OK);CHECK(satchel_vulkan_target_destroy(device,target)==SATCHEL_VULKAN_OK);CHECK(satchel_vulkan_device_destroy(device)==SATCHEL_VULKAN_OK);SatchelVulkanBufferStats bs={};SatchelVulkanTargetStats ts={};CHECK(satchel_vulkan_buffer_stats(&bs)==SATCHEL_VULKAN_OK&&!bs.live_buffers&&!bs.allocated_bytes);CHECK(satchel_vulkan_target_stats(&ts)==SATCHEL_VULKAN_OK&&!ts.live_targets&&!ts.allocated_bytes);CHECK(!satchel_vulkan_submission_live_count()&&!satchel_vulkan_device_live_count());
 std::printf("Typed strict-Ryujinx flat pair: %u ASan/UBSan assertions, %u accepted original single-bit mutations for independent spirv-val; real generated modules, first-provoking per-vertex colors, location0/3, changed vertex data, fence/error/cancellation and cleanup. No captured game shader/window/presentation claim.\n",checks,accepted);
return 0;
}
