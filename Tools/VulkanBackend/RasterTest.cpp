#include "VulkanRaster.h"
#include "Fixtures.h"
#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <vector>
#include <limits>
#include <thread>
#include <chrono>
static unsigned checks,draws,fail_submit,fail_pipeline;
static void ck(bool good,unsigned line){++checks;if(!good){std::fprintf(stderr,"raster assertion line %u check %u\n",line,checks);std::exit(1);}}
#define CHECK(x) ck((x),__LINE__)
extern "C" VkResult raster_test_submit(VkQueue q,uint32_t n,const VkSubmitInfo *s,VkFence f){unsigned fail=fail_submit;fail_submit=0;auto result=vkQueueSubmit(q,n,s,f);if(result==VK_SUCCESS)++draws;return fail && result==VK_SUCCESS?VK_ERROR_UNKNOWN:result;}
extern "C" VkResult raster_test_pipelines(VkDevice d,VkPipelineCache c,uint32_t n,const VkGraphicsPipelineCreateInfo *p,const VkAllocationCallbacks *a,VkPipeline *o){auto result=vkCreateGraphicsPipelines(d,c,n,p,a,o);unsigned fail=fail_pipeline;fail_pipeline=0;return fail && result==VK_SUCCESS?VK_ERROR_UNKNOWN:result;}
static int always(void*){return 1;}
static int countdown(void *p){return --*static_cast<unsigned*>(p)==0;}
static uint32_t bits(float x){uint32_t y;std::memcpy(&y,&x,4);return y;}
static void modules(unsigned location,unsigned rotation,const float color[4],SatchelRasterModule &vs,SatchelRasterModule &fs,bool partial=false){
 RasterFixture v,f;fixture_init(&v,1);fixture_ald(&v,4,location,0,4);
 for(unsigned c=0;c<4;c++){fixture_mov(&v,20+c,4+(c+rotation)%4);fixture_ast(&v,20+c,c,1);}if(partial){fixture_imm(&v,20,0x3f800000);fixture_ast(&v,20,3,1);}fixture_exit(&v);
 fixture_init(&f,5);for(unsigned i=0;i<4;i++){fixture_imm(&f,20+i,bits(color[i]));fixture_mov(&f,i,20+i);}fixture_exit(&f);
 auto vi=fixture_input(&v,1),fi=fixture_input(&f,5);CHECK(satchel_raster_compile_pair(&vi,&fi,&vs,&fs)==SATCHEL_RASTER_OK);CHECK(satchel_vulkan_raster_validate(&vs,1)==SATCHEL_VULKAN_OK);CHECK(satchel_vulkan_raster_validate(&fs,5)==SATCHEL_VULKAN_OK);
}
static SatchelVulkanRasterDescription desc(unsigned location){SatchelVulkanRasterDescription d={};d.version=1;d.size=sizeof(d);d.state={255,1,1,1,0,1,1,0,0};d.format=1;d.samples=1;d.topology=3;d.color_mask=15;d.sample_mask=1;d.vertex_stride=16;for(auto &offset:d.attribute_offsets)offset=UINT32_MAX;d.attribute_offsets[location]=0;return d;}
static void schema(const SatchelRasterModule &m,unsigned stage){
 for(unsigned length=0;length<m.reflection.spirv_words;length++){auto bad=m;bad.reflection.spirv_words=length;CHECK(satchel_vulkan_raster_validate(&bad,stage)==SATCHEL_VULKAN_UNSUPPORTED);}
 for(unsigned at=5;at<m.reflection.spirv_words;){auto bad=m;bad.words[at]=(bad.words[at]&0xffff0000u)|65535;CHECK(satchel_vulkan_raster_validate(&bad,stage)==SATCHEL_VULKAN_UNSUPPORTED);at+=m.words[at]>>16;}
 for(unsigned i=0;i<5;i++){auto bad=m;bad.words[i]^=1;CHECK(satchel_vulkan_raster_validate(&bad,stage)==SATCHEL_VULKAN_UNSUPPORTED);}
 auto bad=m;bad.reflection.descriptor_count=1;CHECK(satchel_vulkan_raster_validate(&bad,stage)==SATCHEL_VULKAN_UNSUPPORTED);bad=m;bad.reflection.required_capabilities=3;CHECK(satchel_vulkan_raster_validate(&bad,stage)==SATCHEL_VULKAN_UNSUPPORTED);bad=m;bad.reflection.interstage_components=15;CHECK(satchel_vulkan_raster_validate(&bad,stage)==SATCHEL_VULKAN_UNSUPPORTED);
}
static void clean(){SatchelVulkanTargetStats t={};SatchelVulkanRasterStats r={};SatchelVulkanBufferStats b={};CHECK(satchel_vulkan_target_stats(&t)==SATCHEL_VULKAN_OK && !t.live_targets && !t.allocated_bytes);CHECK(satchel_vulkan_raster_stats(&r)==SATCHEL_VULKAN_OK && !r.live_pipelines);CHECK(satchel_vulkan_buffer_stats(&b)==SATCHEL_VULKAN_OK && !b.live_buffers && !b.allocated_bytes);CHECK(!satchel_vulkan_submission_live_count() && !satchel_vulkan_device_live_count());}
static void wait(uint64_t d,uint64_t job){SatchelVulkanSubmissionReport report={};CHECK(satchel_vulkan_submission_wait(d,job,5000,nullptr,nullptr,&report)==SATCHEL_VULKAN_OK && report.completed && !report.resources_retained);}
static void pixels(uint64_t d,uint64_t b,const SatchelVulkanRasterDraw &call,const float positions[3][4],const float color[4],unsigned width,unsigned height,size_t size){
 std::vector<uint8_t> data(size);CHECK(satchel_vulkan_buffer_read(d,b,0,data.data(),size)==SATCHEL_VULKAN_OK);
 float p[3][2];for(unsigned i=0;i<3;i++){p[i][0]=call.viewport_x+(positions[i][0]/positions[i][3]+1)*call.viewport_width/2;p[i][1]=call.viewport_y+(positions[i][1]/positions[i][3]+1)*call.viewport_height/2;}
 unsigned interior=0,exterior=0;std::vector<uint8_t> touched(size);
 for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++){
  float e[3];for(unsigned i=0;i<3;i++){auto j=(i+1)%3;e[i]=(p[j][0]-p[i][0])*(y+.5f-p[i][1])-(p[j][1]-p[i][1])*(x+.5f-p[i][0]);}
  bool edge=false;for(float value:e)edge|=std::fabs(value)<.001f;
  bool inside=(e[0]>0 && e[1]>0 && e[2]>0)||(e[0]<0 && e[1]<0 && e[2]<0);
  bool scissor=x>=call.scissor_x && x<call.scissor_x+call.scissor_width && y>=call.scissor_y && y<call.scissor_y+call.scissor_height;
  bool match_clear=true,match_color=true;for(unsigned c=0;c<4;c++){size_t at=call.readback_offset+y*call.row_pitch+x*4+c;touched[at]=1;match_clear&=data[at]==uint8_t(call.clear_rgba[c]*255);match_color&=data[at]==uint8_t(color[c]*255);}
  if(edge && scissor)CHECK(match_clear || match_color);else if(inside && scissor){CHECK(match_color);++interior;}else{CHECK(match_clear);++exterior;}
 }
 CHECK(interior>10 && exterior>10);for(size_t i=0;i<size;i++)if(!touched[i])CHECK(data[i]==0xa5);
}
int main(){clean();uint64_t device=0,other=0;SatchelGuestDeviceCaps caps={};CHECK(satchel_vulkan_device_create(0,&device,&caps)==SATCHEL_VULKAN_OK && (caps.queue_flags&VK_QUEUE_GRAPHICS_BIT));CHECK(satchel_vulkan_device_create(0,&other,&caps)==SATCHEL_VULKAN_OK);
 const unsigned width=64,height=48;SatchelVulkanTargetDescription td={1,sizeof(td),1,width,height,0};SatchelVulkanTargetInfo ti={};uint64_t target=0;CHECK(satchel_vulkan_target_create(device,&td,&target,&ti)==SATCHEL_VULKAN_OK);
 uint64_t vertex=0,output=0;SatchelVulkanBufferInfo bi={};CHECK(satchel_vulkan_buffer_create(device,128,SATCHEL_VULKAN_BUFFER_VERTEX|SATCHEL_VULKAN_BUFFER_TRANSFER_DESTINATION,&vertex,&bi)==SATCHEL_VULKAN_OK);
 const uint64_t row=width*4+20,offset=32,size=offset+(height-1)*row+width*4+32;CHECK(satchel_vulkan_buffer_create(device,size,SATCHEL_VULKAN_BUFFER_TRANSFER_DESTINATION,&output,&bi)==SATCHEL_VULKAN_OK);std::vector<uint8_t> sentinel(size,0xa5);
 SatchelRasterModule vs,fs;uint64_t pipeline=0;SatchelVulkanRasterInfo pi={};const float colors[][4]={{1,0,0,1},{0,1,0,1},{0,0,1,1},{1,1,0,0},{0,1,1,1},{1,0,1,0},{1,1,1,1},{0,0,0,0}};
 for(unsigned trial=0;trial<12;trial++){
  unsigned location=trial%8,rotation=trial%4;const auto &color=colors[trial%8];modules(location,rotation,color,vs,fs,trial==11);if(trial<2){schema(vs,1);schema(fs,5);}
  auto description=desc(location);CHECK(satchel_vulkan_raster_create(device,&description,&vs,&fs,&pipeline,&pi)==SATCHEL_VULKAN_OK && pipeline && !pi.references && pi.attribute_mask==(1u<<location));
  // Actual uploaded vertex values compensate the original shader's swizzle.
  float positions[3][4]={{-.75f,-.75f,.5f,1},{.75f,-.75f,.5f,1},{-.75f,.75f,.5f,1}};if(trial&1)positions[1][0]=.25f;
  float uploaded[12];for(unsigned i=0;i<3;i++)for(unsigned c=0;c<4;c++)uploaded[i*4+(c+rotation)%4]=positions[i][c];CHECK(satchel_vulkan_buffer_write(device,vertex,16,uploaded,sizeof(uploaded))==SATCHEL_VULKAN_OK);CHECK(satchel_vulkan_buffer_write(device,output,0,sentinel.data(),size)==SATCHEL_VULKAN_OK);
  SatchelVulkanRasterDraw call={};call.version=1;call.size=sizeof(call);call.vertex_count=3;call.vertex_buffer=vertex;call.vertex_offset=16;call.readback_buffer=output;call.readback_offset=offset;call.row_pitch=row;call.viewport_width=width;call.viewport_height=height;call.max_depth=1;call.scissor_width=width;call.scissor_height=height;call.clear_rgba[0]=1;call.clear_rgba[2]=1;call.clear_rgba[3]=1;
  if(trial==10){call.viewport_x=4;call.viewport_y=4;call.viewport_width=48;call.viewport_height=32;call.scissor_x=12;call.scissor_y=8;call.scissor_width=36;call.scissor_height=28;}
  uint64_t job=0;for(unsigned point=1;point<=2;point++){unsigned remaining=point;CHECK(satchel_vulkan_raster_draw_readback(device,pipeline,target,&call,&job,countdown,&remaining)==SATCHEL_VULKAN_CANCELLED && !job);}
  if(trial==2)fail_submit=1;auto submitted=satchel_vulkan_raster_draw_readback(device,pipeline,target,&call,&job,nullptr,nullptr);CHECK(submitted==(trial==2?SATCHEL_VULKAN_API_ERROR:SATCHEL_VULKAN_OK) && job);
  CHECK(satchel_vulkan_raster_info(device,pipeline,&pi)==SATCHEL_VULKAN_OK && pi.references==1);CHECK(satchel_vulkan_raster_destroy(device,pipeline)==SATCHEL_VULKAN_BUSY);CHECK(satchel_vulkan_target_destroy(device,target)==SATCHEL_VULKAN_BUSY);CHECK(satchel_vulkan_device_destroy(device)==SATCHEL_VULKAN_BUSY);CHECK(satchel_vulkan_buffer_destroy(device,vertex)==SATCHEL_VULKAN_BUSY);CHECK(satchel_vulkan_buffer_write(device,vertex,16,uploaded,sizeof(uploaded))==SATCHEL_VULKAN_BUSY);CHECK(satchel_vulkan_buffer_read(device,output,0,sentinel.data(),4)==SATCHEL_VULKAN_BUSY);
  SatchelVulkanSubmissionReport pending={};CHECK(satchel_vulkan_submission_wait(device,job,0,always,nullptr,&pending)==SATCHEL_VULKAN_CANCELLED && pending.resources_retained);wait(device,job);pixels(device,output,call,positions,color,width,height,size);
  CHECK(satchel_vulkan_raster_info(device,pipeline,&pi)==SATCHEL_VULKAN_OK && !pi.references);CHECK(satchel_vulkan_raster_destroy(other,pipeline)==SATCHEL_VULKAN_INVALID);
  if(trial==0){
   for(unsigned bad=0;bad<10;bad++){auto invalid=call;if(bad==0)invalid.vertex_offset=120;if(bad==1)invalid.vertex_count=4;if(bad==2)invalid.row_pitch=width*4-4;if(bad==3)invalid.readback_offset=UINT64_MAX-3;if(bad==4)invalid.viewport_width=std::numeric_limits<float>::quiet_NaN();if(bad==5)invalid.scissor_x=UINT32_MAX;if(bad==6)invalid.min_depth=.5f;if(bad==7)invalid.viewport_height=height+1;if(bad==8)invalid.vertex_buffer=target;if(bad==9)invalid.first_vertex=1;job=99;CHECK(satchel_vulkan_raster_draw_readback(device,pipeline,target,&invalid,&job,nullptr,nullptr)!=SATCHEL_VULKAN_OK && !job);}
   for(unsigned bad=0;bad<16;bad++){auto invalid=description;if(bad==0)invalid.state.y_negate=1;if(bad==1)invalid.depth_test=1;if(bad==2)invalid.vertex_stride=12;if(bad==3)invalid.attribute_offsets[location]=4;if(bad==4)invalid.blend_enable=1;if(bad==5)invalid.samples=4;if(bad==6)invalid.topology=1;if(bad==7)invalid.attribute_offsets[7]=0;if(bad==8)invalid.polygon_mode=1;if(bad==9)invalid.rasterizer_discard=1;if(bad==10)invalid.depth_clamp=1;if(bad==11)invalid.depth_bias=1;if(bad==12)invalid.logic_op_enable=1;if(bad==13)invalid.sample_mask=0;if(bad==14)invalid.sample_shading=1;if(bad==15)invalid.alpha_to_one=1;uint64_t rejected=99;CHECK(satchel_vulkan_raster_create(device,&invalid,&vs,&fs,&rejected,&pi)==SATCHEL_VULKAN_UNSUPPORTED && !rejected);}
  }
  uint64_t stale=pipeline;CHECK(satchel_vulkan_raster_destroy(device,pipeline)==SATCHEL_VULKAN_OK);CHECK(satchel_vulkan_raster_destroy(device,stale)==SATCHEL_VULKAN_INVALID);
 }
 // Two distinct attributes: reflection reads only XY at0 and X at7, but the
 // emitted shader loads both whole vec4s. A last-component-short buffer rejects.
 {
  RasterFixture v,f;fixture_init(&v,1);fixture_ald(&v,0,0,0,2);fixture_imm(&v,2,bits(.5f));fixture_ald(&v,3,7,0,1);fixture_ast(&v,0,0,4);fixture_exit(&v);fixture_init(&f,5);const float cyan[]={0,1,1,1};for(unsigned i=0;i<4;i++)fixture_imm(&f,i,bits(cyan[i]));fixture_exit(&f);auto vi=fixture_input(&v,1),fi=fixture_input(&f,5);CHECK(!satchel_raster_compile_pair(&vi,&fi,&vs,&fs));CHECK(vs.reflection.input_components[0]==3 && vs.reflection.input_components[7]==1);CHECK(satchel_vulkan_raster_validate(&vs,1)==SATCHEL_VULKAN_OK);
  auto description=desc(0);description.vertex_stride=32;description.attribute_offsets[7]=16;
  fail_pipeline=1;CHECK(satchel_vulkan_raster_create(device,&description,&vs,&fs,&pipeline,&pi)==SATCHEL_VULKAN_API_ERROR && !pipeline);SatchelVulkanRasterStats rs={};CHECK(satchel_vulkan_raster_stats(&rs)==SATCHEL_VULKAN_OK && !rs.live_pipelines);
  CHECK(satchel_vulkan_raster_create(device,&description,&vs,&fs,&pipeline,&pi)==SATCHEL_VULKAN_OK && pi.attribute_mask==129);
  const float positions[3][4]={{-.75f,-.75f,.5f,1},{.75f,-.75f,.5f,1},{-.75f,.75f,.5f,1}};float data[24]={};for(unsigned i=0;i<3;i++){data[i*8]=positions[i][0];data[i*8+1]=positions[i][1];data[i*8+4]=1;}uint64_t upload=0,upload_job=0;CHECK(satchel_vulkan_buffer_create(device,sizeof(data),SATCHEL_VULKAN_BUFFER_TRANSFER_SOURCE,&upload,&bi)==SATCHEL_VULKAN_OK);CHECK(satchel_vulkan_buffer_write(device,upload,0,data,sizeof(data))==SATCHEL_VULKAN_OK);CHECK(satchel_vulkan_buffer_copy(device,upload,0,vertex,16,sizeof(data),&upload_job,nullptr,nullptr)==SATCHEL_VULKAN_OK && upload_job);wait(device,upload_job);CHECK(satchel_vulkan_buffer_destroy(device,upload)==SATCHEL_VULKAN_OK);CHECK(satchel_vulkan_buffer_write(device,output,0,sentinel.data(),size)==SATCHEL_VULKAN_OK);
  SatchelVulkanRasterDraw call={};call.version=1;call.size=sizeof(call);call.vertex_count=3;call.vertex_buffer=vertex;call.vertex_offset=16;call.readback_buffer=output;call.readback_offset=offset;call.row_pitch=row;call.viewport_width=width;call.viewport_height=height;call.max_depth=1;call.scissor_width=width;call.scissor_height=height;call.clear_rgba[3]=1;
  uint64_t short_buffer=0;CHECK(satchel_vulkan_buffer_create(device,100,SATCHEL_VULKAN_BUFFER_VERTEX,&short_buffer,&bi)==SATCHEL_VULKAN_OK);auto bad=call;bad.vertex_buffer=short_buffer;uint64_t job=99;CHECK(satchel_vulkan_raster_draw_readback(device,pipeline,target,&bad,&job,nullptr,nullptr)==SATCHEL_VULKAN_INVALID && !job);CHECK(satchel_vulkan_buffer_destroy(device,short_buffer)==SATCHEL_VULKAN_OK);
  CHECK(satchel_vulkan_raster_draw_readback(device,pipeline,target,&call,&job,nullptr,nullptr)==SATCHEL_VULKAN_OK && job);SatchelVulkanStatus drained=SATCHEL_VULKAN_BUSY;for(unsigned i=0;i<5000 && drained==SATCHEL_VULKAN_BUSY;i++){drained=satchel_vulkan_device_drain(device);if(drained==SATCHEL_VULKAN_BUSY)std::this_thread::sleep_for(std::chrono::milliseconds(1));}CHECK(drained==SATCHEL_VULKAN_OK);pixels(device,output,call,positions,cyan,width,height,size);CHECK(satchel_vulkan_raster_destroy(device,pipeline)==SATCHEL_VULKAN_OK);
 }
 // Count cap and parent cleanup include persistent pipelines, not only jobs.
 const float red[]={1,0,0,1};modules(0,0,red,vs,fs);auto d=desc(0);uint64_t pipelines[8];for(auto &p:pipelines)CHECK(satchel_vulkan_raster_create(device,&d,&vs,&fs,&p,&pi)==SATCHEL_VULKAN_OK);CHECK(satchel_vulkan_raster_create(device,&d,&vs,&fs,&pipeline,&pi)==SATCHEL_VULKAN_BUSY && !pipeline);CHECK(satchel_vulkan_device_destroy(device)==SATCHEL_VULKAN_OK);for(auto p:pipelines)CHECK(satchel_vulkan_raster_destroy(device,p)==SATCHEL_VULKAN_INVALID);CHECK(satchel_vulkan_device_destroy(other)==SATCHEL_VULKAN_OK);clean();
 std::printf("Raster backend: %u sanitizer assertions, %u actual dynamic Maxwell->SPIR-V->Vulkan triangle draws, pixel/row guards, state and cleanup. No game frame or NVN draw admission.\n",checks,draws);return 0;
}
