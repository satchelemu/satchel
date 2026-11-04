#include "CapturedFlat.h"
#include "VulkanFlat.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstddef>
#include <vector>
static unsigned checks,draws,compiles;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"live captured GPU assertion line%d\n",__LINE__);std::abort();}}while(0)
#include "GPUOriginalPair.h"
static_assert(sizeof(SatchelCFModule)==sizeof(SatchelVulkanFlatModule));
static_assert(offsetof(SatchelCFModule,words)==offsetof(SatchelVulkanFlatModule,words));
static int cancel(void*){return 1;}
static SatchelCFInput input(const SatchelVulkanFlatCaps&caps,const SatchelVulkanComputeLimits&limits){
 SatchelCFInput x={};x.version=1;x.size=sizeof(x);x.vertex={vs,vs+80,80,56};x.fragment={fs,fs+80,80,56};
 // Explicit ORIGINAL test state, not observed guest state. Mirrors draw descriptor below.
 auto&s=x.state;s.known=255;s.single_vertex_program=1;s.viewport_transform_enabled=1;s.depth_zero_to_one=1;s.origin_upper_left=1;s.alpha_test_always=1;s.extra_known=4095;s.topology=3;s.point_size_bits=0x3f800000;s.float_vertex_mask=3;s.float_output_mask=1;
 // Optional translator mechanisms disabled in this bounded resource-free test
 // profile. Workaround/scaled-format facts remain explicit test assumptions;
 // this is not a general captured-game host-state adapter.
 x.host.known=SATCHEL_CF_HOST_ALL;x.host.values=SATCHEL_CF_HOST_QUERYHOSTSUPPORTSBGRAFORMAT;
 CHECK(limits.storage_offset_alignment<=UINT32_MAX);x.host.storage_alignment=(uint32_t)limits.storage_offset_alignment;
 x.host.physical_bgra8_color_attachment=caps.physical_bgra8_color_attachment;
 x.host.admitted_color_format=caps.admitted_color_format;x.host.current_color_format=SATCHEL_VULKAN_TARGET_RGBA8_UNORM;
 x.max_work=65536;x.timeout_ns=1000000000;return x;
}
static SatchelVulkanFlatDescription description(){SatchelVulkanFlatDescription d={};d.version=1;d.size=sizeof(d);d.provoking_known=1;auto&r=d.raster;r.version=1;r.size=sizeof(r);r.state={255,1,1,1,0,1,1,0,0};r.format=1;r.samples=1;r.topology=3;r.color_mask=15;r.sample_mask=1;r.vertex_stride=32;for(auto&o:r.attribute_offsets)o=UINT32_MAX;r.attribute_offsets[0]=0;r.attribute_offsets[1]=16;return d;}
static void refusal(SatchelCFInput x,int expected){SatchelCFPair p,before;std::memset(&p,0xa5,sizeof(p));before=p;CHECK(satchel_captured_flat_compile(&x,&p,sizeof(p))==expected);CHECK(!std::memcmp(&p,&before,sizeof(p)));SatchelVulkanRasterStats stats={};CHECK(satchel_vulkan_raster_stats(&stats)==SATCHEL_VULKAN_OK&&!stats.live_pipelines);}
static void draw(uint64_t device,uint64_t target,uint64_t pipeline,uint64_t vertices,uint64_t output,const uint8_t expected[4]){
 const unsigned w=32,h=24;const size_t pitch=w*4+16,size=64+pitch*h+64;std::vector<uint8_t>bytes(size,0xa5);CHECK(satchel_vulkan_buffer_write(device,output,0,bytes.data(),size)==SATCHEL_VULKAN_OK);
 SatchelVulkanRasterDraw r={};r.version=1;r.size=sizeof(r);r.vertex_count=3;r.vertex_buffer=vertices;r.readback_buffer=output;r.readback_offset=64;r.row_pitch=pitch;r.viewport_width=w;r.viewport_height=h;r.max_depth=1;r.scissor_width=w;r.scissor_height=h;r.clear_rgba[0]=r.clear_rgba[1]=r.clear_rgba[2]=.25f;r.clear_rgba[3]=1;
 uint64_t job=99;CHECK(satchel_vulkan_raster_draw_readback(device,pipeline,target,&r,&job,cancel,nullptr)==SATCHEL_VULKAN_CANCELLED&&!job);
 CHECK(satchel_vulkan_raster_draw_readback(device,pipeline,target,&r,&job,nullptr,nullptr)==SATCHEL_VULKAN_OK&&job);CHECK(satchel_vulkan_raster_destroy(device,pipeline)==SATCHEL_VULKAN_BUSY);CHECK(satchel_vulkan_target_destroy(device,target)==SATCHEL_VULKAN_BUSY);CHECK(satchel_vulkan_buffer_destroy(device,vertices)==SATCHEL_VULKAN_BUSY);
 SatchelVulkanSubmissionReport report={};CHECK(satchel_vulkan_submission_wait(device,job,0,cancel,nullptr,&report)==SATCHEL_VULKAN_CANCELLED&&report.resources_retained);CHECK(satchel_vulkan_submission_wait(device,job,5000,nullptr,nullptr,&report)==SATCHEL_VULKAN_OK&&report.completed&&!report.resources_retained);
 CHECK(satchel_vulkan_buffer_read(device,output,0,bytes.data(),size)==SATCHEL_VULKAN_OK);const uint8_t clear[]={64,64,64,255};unsigned covered=0,uncovered=0;
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){auto*p=bytes.data()+64+y*pitch+x*4;bool a=!std::memcmp(p,expected,4),b=!std::memcmp(p,clear,4);CHECK(a||b);covered+=a;uncovered+=b;}
 CHECK(covered>50&&uncovered>50);CHECK(!std::memcmp(bytes.data()+64,expected,4));CHECK(!std::memcmp(bytes.data()+64+(h-1)*pitch+(w-1)*4,clear,4));
 for(size_t i=0;i<size;i++){bool image=i>=64&&i<64+pitch*h&&((i-64)%pitch)<w*4;if(!image)CHECK(bytes[i]==0xa5);}++draws;
}
int main(){
 uint64_t device=0;SatchelGuestDeviceCaps devicecaps={};CHECK(satchel_vulkan_device_create(0,&device,&devicecaps)==SATCHEL_VULKAN_OK);SatchelVulkanFlatCaps caps={};SatchelVulkanComputeLimits limits={};CHECK(satchel_vulkan_flat_capabilities(device,&caps)==SATCHEL_VULKAN_OK);CHECK(satchel_vulkan_compute_limits(device,&limits)==SATCHEL_VULKAN_OK);CHECK(caps.physical_bgra8_color_attachment&&caps.admitted_color_format==1&&caps.graphics_queue&&caps.first_enabled);
 original(0,0);auto in=input(caps,limits);SatchelCFPair baseline={}; // FIRST call is production, with Vulkan already live.
 CHECK(satchel_captured_flat_compile(&in,&baseline,sizeof(baseline))==SATCHEL_CF_OK);++compiles;CHECK(!baseline.dynamic_code_features);
 auto bad=in;bad.cancelled=cancel;refusal(bad,SATCHEL_CF_CANCELLED);bad=in;bad.timeout_ns=1;refusal(bad,SATCHEL_CF_DEADLINE);bad=in;bad.state.known=247;refusal(bad,SATCHEL_CF_UNSUPPORTED);bad=in;bad.host.physical_bgra8_color_attachment=0;refusal(bad,SATCHEL_CF_UNSUPPORTED);
 vs[0]^=1;refusal(in,SATCHEL_CF_UNSUPPORTED);original(0,0);
 uint64_t target=0,vertices=0,output=0;SatchelVulkanTargetDescription td={1,sizeof(td),1,32,24,0};SatchelVulkanTargetInfo ti={};SatchelVulkanBufferInfo bi={};CHECK(satchel_vulkan_target_create(device,&td,&target,&ti)==SATCHEL_VULKAN_OK);CHECK(satchel_vulkan_buffer_create(device,96,SATCHEL_VULKAN_BUFFER_VERTEX,&vertices,&bi)==SATCHEL_VULKAN_OK);CHECK(satchel_vulkan_buffer_create(device,64+(32*4+16)*24+64,SATCHEL_VULKAN_BUFFER_TRANSFER_DESTINATION,&output,&bi)==SATCHEL_VULKAN_OK);
 for(unsigned loc=0;loc<8;loc++)for(unsigned rotate=0;rotate<4;rotate++){
  original(loc,rotate);in=input(caps,limits);SatchelCFPair compiled={};CHECK(satchel_captured_flat_compile(&in,&compiled,sizeof(compiled))==SATCHEL_CF_OK);++compiles;CHECK(compiled.vertex_facts.varying_mask==(1u<<loc)&&compiled.fragment_facts.varying_mask==(1u<<loc));if(loc||rotate)CHECK(std::memcmp(&compiled.fragment,&baseline.fragment,sizeof(SatchelCFModule)));
  // Copy only freshly returned compiler modules; there is no fixture-file input.
  SatchelVulkanFlatModule v,f;std::memcpy(&v,&compiled.vertex,sizeof(v));std::memcpy(&f,&compiled.fragment,sizeof(f));SatchelVulkanFlatPair facts={};CHECK(satchel_vulkan_flat_validate_pair(&v,&f,&facts)==SATCHEL_VULKAN_OK);CHECK(facts.vertex.outputs[loc]==15&&facts.fragment.flat_inputs[loc]==15&&!facts.vertex.active_uniforms&&!facts.fragment.active_uniforms&&facts.vertex.declared_uniforms==1&&facts.fragment.declared_uniforms==1);
  auto desc=description();uint64_t pipeline=0;SatchelVulkanRasterInfo info={};CHECK(satchel_vulkan_flat_create(device,&desc,&v,&f,&pipeline,&info,&facts)==SATCHEL_VULKAN_OK&&pipeline);
  // Distinct vertex colors establish FIRST provoking semantics. Change the
  // actual first vertex between draws; source rotate independently changes FS.
  for(unsigned variant=0;variant<2;variant++){
   float data[24]={-1,-1,.5f,1, 1,0,0,1, 1,-1,.5f,1, 0,0,1,1, -1,1,.5f,1, 0,0,1,1};const uint8_t colors[2][4]={{255,0,0,255},{0,255,0,255}};for(unsigned c=0;c<4;c++)data[4+c]=colors[variant][c]/255.f;
   CHECK(satchel_vulkan_buffer_write(device,vertices,0,data,sizeof(data))==SATCHEL_VULKAN_OK);uint8_t expected[4];for(unsigned c=0;c<4;c++)expected[c]=colors[variant][(c+rotate)%4];draw(device,target,pipeline,vertices,output,expected);
  }
  CHECK(satchel_vulkan_raster_destroy(device,pipeline)==SATCHEL_VULKAN_OK);
 }
 CHECK(satchel_vulkan_buffer_destroy(device,output)==SATCHEL_VULKAN_OK);CHECK(satchel_vulkan_buffer_destroy(device,vertices)==SATCHEL_VULKAN_OK);CHECK(satchel_vulkan_target_destroy(device,target)==SATCHEL_VULKAN_OK);CHECK(satchel_vulkan_device_destroy(device)==SATCHEL_VULKAN_OK);SatchelVulkanBufferStats bs={};SatchelVulkanTargetStats ts={};SatchelVulkanRasterStats rs={};CHECK(satchel_vulkan_buffer_stats(&bs)==SATCHEL_VULKAN_OK&&!bs.live_buffers&&!bs.allocated_bytes);CHECK(satchel_vulkan_target_stats(&ts)==SATCHEL_VULKAN_OK&&!ts.live_targets&&!ts.allocated_bytes);CHECK(satchel_vulkan_raster_stats(&rs)==SATCHEL_VULKAN_OK&&!rs.live_pipelines);CHECK(!satchel_vulkan_device_live_count()&&!satchel_vulkan_submission_live_count());
 std::printf("Live production captured C facade -> typed Vulkan -> GPU: %u ASan/UBSan assertions; %u compiles,32 original source pairs,64 real draws/fences; locations0..7,4 source swizzles,2 first-vertex colors; actual physicalBGRA=%u/storageAlignment=%llu, all guards/cancellation/cleanup. Remaining host workaround facts are explicit original-test assumptions, not a captured-game capability adapter. No phone/window/display claim.\n",checks,compiles,draws?caps.physical_bgra8_color_attachment:0,(unsigned long long)limits.storage_offset_alignment);
}
