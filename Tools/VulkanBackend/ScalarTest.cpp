#include "VulkanScalar.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include "ComputeOriginal.inc"
static unsigned checks;
static void ck(bool v,unsigned line){++checks;if(!v){std::fprintf(stderr,"scalar GPU assertion line%u check%u\n",line,checks);std::exit(1);}}
#define check(v) ck((v),__LINE__)
static uint64_t load(unsigned d,unsigned bytes,unsigned offset){return UINT64_C(0xef90000000000000)|((uint64_t)(bytes==8?5:4)<<48)|(UINT64_C(3)<<36)|((uint64_t)offset<<20)|(UINT64_C(7)<<16)|(UINT64_C(255)<<8)|d;}
static uint64_t add(unsigned d,unsigned a,unsigned b){return UINT64_C(0x5c10000000000000)|((uint64_t)b<<20)|(UINT64_C(7)<<16)|((uint64_t)a<<8)|d;}
static uint64_t store(unsigned v,unsigned p,unsigned off){return UINT64_C(0xeed8000000000000)|(UINT64_C(4)<<48)|(UINT64_C(1)<<45)|((uint64_t)off<<20)|(UINT64_C(7)<<16)|((uint64_t)p<<8)|v;}
static std::vector<uint64_t> code(unsigned p){const uint64_t end=UINT64_C(0xe30000000007000f);const std::vector<std::vector<uint64_t>> programs={
 {load(10,8,8),load(20,4,24),load(21,4,28),add(22,20,21),store(22,10,12),end},
 {load(4,8,8),load(1,4,24),load(2,4,28),load(3,4,32),add(6,1,2),add(7,6,3),store(7,4,0),store(1,4,4),store(6,4,16),end},
 {load(100,8,8),load(8,4,24),add(9,8,255),store(9,100,60),store(255,100,0),store(8,100,0),end},
 {load(0,8,8),load(42,4,32),store(42,0,28),end}};
 std::vector<uint64_t> result;for(auto op:programs.at(p)){if(result.size()%4==0)result.push_back(0);result.push_back(op);}while(result.size()%4)result.push_back(0);return result;}
static int always(void*){return 1;}
static int countdown(void *p){return --*(unsigned*)p==0;}
static void finish(uint64_t d,uint64_t j){SatchelVulkanSubmissionReport r={};check(satchel_vulkan_submission_wait(d,j,5000,nullptr,nullptr,&r)==SATCHEL_VULKAN_OK && r.completed && !r.resources_retained);}
static uint32_t get32(const uint8_t *p){uint32_t x;std::memcpy(&x,p,4);return x;}
static void put32(uint8_t *p,uint32_t x){std::memcpy(p,&x,4);}
int main(int argc,char **argv){check(argc==2);uint64_t device=0,other=0;SatchelGuestDeviceCaps caps={};check(satchel_vulkan_device_create(0,&device,&caps)==SATCHEL_VULKAN_OK);check(satchel_vulkan_device_create(0,&other,&caps)==SATCHEL_VULKAN_OK);SatchelVulkanComputeLimits limits={};check(satchel_vulkan_compute_limits(device,&limits)==SATCHEL_VULKAN_OK && limits.max_uniform_range>=65536);
 const size_t uoff=(size_t)limits.uniform_offset_alignment,soff=(size_t)limits.storage_offset_alignment;check(uoff && uoff<=65536 && soff && soff<=65536);uint64_t uniform=0,storage=0;SatchelVulkanBufferInfo info={};check(satchel_vulkan_buffer_create(device,65536+uoff*2,SATCHEL_VULKAN_BUFFER_UNIFORM,&uniform,&info)==SATCHEL_VULKAN_OK);check(satchel_vulkan_buffer_create(device,soff+128,SATCHEL_VULKAN_BUFFER_STORAGE,&storage,&info)==SATCHEL_VULKAN_OK);
 SatchelVulkanScalarBindings bindings={{uniform,uoff,65536,storage,soff,64},0x10000,1,0};std::vector<uint8_t> ubo(65536+uoff*2,0x3c),expected(soff+128,0xa5),actual(expected.size());
 uint32_t first[4096]={};size_t first_size=0;SatchelScalarReflection first_ref={};
 for(unsigned p=0;p<4;p++){auto maxwell=code(p);uint32_t cb[16]={};cb[2]=0x10000;cb[6]=UINT32_MAX;cb[7]=7;cb[8]=0x80000001;
  SatchelScalarInput input={};input.version=1;input.size=sizeof(input);input.known=7;input.local_x=input.local_y=input.local_z=input.group_x=input.group_y=input.group_z=1;input.shader_enabled=1;input.max_uniform_range=limits.max_uniform_range;input.max_storage_range=limits.max_storage_range;input.max_uniform_descriptors=limits.max_stage_uniform_buffers;input.max_storage_descriptors=limits.max_stage_storage_buffers;input.max_stage_resources=limits.max_stage_resources;input.uniform_alignment=(uint32_t)uoff;input.storage_alignment=(uint32_t)soff;input.cb_slot=3;input.uniform_binding=1;input.storage_binding=2;input.code=(const uint8_t*)maxwell.data();input.code_size=maxwell.size()*8;input.constant_buffer=(const uint8_t*)cb;input.constant_buffer_size=64;input.storage_address=0x10000;input.storage_size=64;input.storage_cb_disjoint=1;
  uint32_t spirv[4096];SatchelScalarReflection reflection={};check(satchel_scalar_compile(&input,spirv,4096,&reflection)==0);size_t size=reflection.spirv_words*4;check(satchel_vulkan_scalar_validate(spirv,size,&reflection));
  if(!p){first_size=size;std::memcpy(first,spirv,size);first_ref=reflection;}else check(first_size!=size || std::memcmp(first,spirv,size));
  uint64_t pipeline=0;check(satchel_vulkan_scalar_pipeline_create(device,spirv,size,&reflection,&pipeline,nullptr,nullptr)==SATCHEL_VULKAN_OK && pipeline);
  uint64_t job=99;check(satchel_vulkan_compute_submit_original(device,pipeline,&bindings.buffers,&job,nullptr,nullptr)==SATCHEL_VULKAN_UNSUPPORTED && !job);
  const uint32_t values[][3]={{UINT32_MAX,7,0x80000001},{5,7,9},{0x7f800001,0x7fc00001,1},{0xdeadbeef,0x11223344,0xff800001},{0x80000000,0x80000000,UINT32_MAX},{1,0x7fffff,0x800000}};
  for(const auto &v:values){cb[6]=v[0];cb[7]=v[1];cb[8]=v[2];std::fill(ubo.begin()+uoff,ubo.begin()+uoff+65536,0);std::memcpy(ubo.data()+uoff,cb,64);std::fill(expected.begin(),expected.end(),0xa5);
   check(satchel_vulkan_buffer_write(device,uniform,0,ubo.data(),ubo.size())==SATCHEL_VULKAN_OK);check(satchel_vulkan_buffer_write(device,storage,0,expected.data(),expected.size())==SATCHEL_VULKAN_OK);
   SatchelVulkanStatus submit=satchel_vulkan_scalar_submit(device,pipeline,&bindings,&job,nullptr,nullptr);if(submit!=SATCHEL_VULKAN_OK)std::fprintf(stderr,"submit status%u\n",(unsigned)submit);check(submit==SATCHEL_VULKAN_OK && job);check(satchel_vulkan_compute_pipeline_destroy(device,pipeline)==SATCHEL_VULKAN_BUSY);check(satchel_vulkan_device_destroy(device)==SATCHEL_VULKAN_BUSY);check(satchel_vulkan_buffer_write(device,uniform,0,ubo.data(),ubo.size())==SATCHEL_VULKAN_BUSY);
   SatchelVulkanSubmissionReport cancelled={};check(satchel_vulkan_submission_wait(device,job,5000,always,nullptr,&cancelled)==SATCHEL_VULKAN_CANCELLED && cancelled.resources_retained);finish(device,job);
   // Independent C oracle evaluates the authored source intent, not emitted IR.
   switch(p){case 0:put32(expected.data()+soff+12,v[0]+v[1]);break;case 1:put32(expected.data()+soff,(v[0]+v[1])+v[2]);put32(expected.data()+soff+4,v[0]);put32(expected.data()+soff+16,v[0]+v[1]);break;case 2:put32(expected.data()+soff+60,v[0]);put32(expected.data()+soff,v[0]);break;case 3:put32(expected.data()+soff+28,v[2]);break;}
   check(satchel_vulkan_buffer_read(device,storage,0,actual.data(),actual.size())==SATCHEL_VULKAN_OK && actual==expected);std::vector<uint8_t> ubocopy(ubo.size());check(satchel_vulkan_buffer_read(device,uniform,0,ubocopy.data(),ubocopy.size())==SATCHEL_VULKAN_OK && ubocopy==ubo);
  }
  // Same captured pointer pair must still identify the retained storage range.
  put32(ubo.data()+uoff+8,get32(ubo.data()+uoff+8)+256);check(satchel_vulkan_buffer_write(device,uniform,0,ubo.data(),ubo.size())==SATCHEL_VULKAN_OK);check(satchel_vulkan_scalar_submit(device,pipeline,&bindings,&job,nullptr,nullptr)==SATCHEL_VULKAN_UNSUPPORTED && !job);put32(ubo.data()+uoff+8,0x10000);check(satchel_vulkan_buffer_write(device,uniform,0,ubo.data(),ubo.size())==SATCHEL_VULKAN_OK);
  for(unsigned point=1;point<=2;point++){unsigned left=point;check(satchel_vulkan_scalar_submit(device,pipeline,&bindings,&job,countdown,&left)==SATCHEL_VULKAN_CANCELLED && !job && !satchel_vulkan_submission_live_count());}
  auto bad=bindings;bad.guest_cb_storage_disjoint=0;check(satchel_vulkan_scalar_submit(device,pipeline,&bad,&job,nullptr,nullptr)==SATCHEL_VULKAN_UNSUPPORTED && !job);bad=bindings;bad.reserved=1;check(satchel_vulkan_scalar_submit(device,pipeline,&bad,&job,nullptr,nullptr)==SATCHEL_VULKAN_UNSUPPORTED && !job);bad=bindings;bad.storage_guest_address+=256;check(satchel_vulkan_scalar_submit(device,pipeline,&bad,&job,nullptr,nullptr)==SATCHEL_VULKAN_UNSUPPORTED && !job);bad=bindings;bad.buffers.storage_size=reflection.storage_required_bytes-4;check(satchel_vulkan_scalar_submit(device,pipeline,&bad,&job,nullptr,nullptr)!=SATCHEL_VULKAN_OK && !job);bad=bindings;bad.buffers.uniform_buffer=storage;check(satchel_vulkan_scalar_submit(device,pipeline,&bad,&job,nullptr,nullptr)==SATCHEL_VULKAN_INVALID && !job);
  check(satchel_vulkan_scalar_submit(other,pipeline,&bindings,&job,nullptr,nullptr)==SATCHEL_VULKAN_INVALID && !job);check(satchel_vulkan_compute_pipeline_destroy(device,pipeline)==SATCHEL_VULKAN_OK);
 }
 // Parse every one-bit mutation and truncated prefix without invoking a driver.
 // Valid parameter/dataflow mutations may be admitted; a general schema is not
 // an exact shader hash. Structural forbidden-op mutations must all reject.
 unsigned admitted_mutations=0;for(size_t bit=0;bit<first_size*8;bit++){first[bit/32]^=1u<<(bit%32);if(satchel_vulkan_scalar_validate(first,first_size,&first_ref)){char path[512];check(std::snprintf(path,sizeof(path),"%s/mutation%u.spv",argv[1],admitted_mutations)>0);FILE *file=std::fopen(path,"wb");check(file && std::fwrite(first,1,first_size,file)==first_size && std::fclose(file)==0);++admitted_mutations;}first[bit/32]^=1u<<(bit%32);++checks;}
 for(size_t n=0;n<first_size;n++){check(!satchel_vulkan_scalar_validate(first,n,&first_ref));}
 for(size_t at=5;at<first_size/4;){uint32_t word=first[at];first[at]=(word&0xffff0000)|249;check(!satchel_vulkan_scalar_validate(first,first_size,&first_ref));first[at]=word;at+=word>>16;}
 SatchelScalarReflection bad=first_ref;bad.unused_memory_mask=0;check(!satchel_vulkan_scalar_validate(first,first_size,&bad));bad=first_ref;bad.store_count=17;check(!satchel_vulkan_scalar_validate(first,first_size,&bad));bad=first_ref;bad.stores[0].offset+=4;check(!satchel_vulkan_scalar_validate(first,first_size,&bad));bad=first_ref;bad.uniform_required_bytes+=4;check(!satchel_vulkan_scalar_validate(first,first_size,&bad));bad=first_ref;bad.pointer_cb_offset=65532;check(!satchel_vulkan_scalar_validate(first,first_size,&bad));
 uint64_t cancelled_pipeline=99;for(unsigned point=1;point<=3;point++){unsigned left=point;check(satchel_vulkan_scalar_pipeline_create(device,first,first_size,&first_ref,&cancelled_pipeline,countdown,&left)==SATCHEL_VULKAN_CANCELLED && !cancelled_pipeline && !satchel_vulkan_compute_pipeline_live_count());}
 uint64_t original_pipeline=0;SatchelVulkanComputeProfile original_profile={1,sizeof(original_profile),1,2};check(satchel_vulkan_compute_pipeline_create_original(device,original_compute_admission,sizeof(original_compute_admission),&original_profile,&original_pipeline,nullptr,nullptr)==SATCHEL_VULKAN_OK);uint64_t wrong_job=99;check(satchel_vulkan_scalar_submit(device,original_pipeline,&bindings,&wrong_job,nullptr,nullptr)==SATCHEL_VULKAN_UNSUPPORTED && !wrong_job);check(satchel_vulkan_compute_pipeline_destroy(device,original_pipeline)==SATCHEL_VULKAN_OK);
 check(satchel_vulkan_device_destroy(device)==SATCHEL_VULKAN_OK);check(satchel_vulkan_device_destroy(other)==SATCHEL_VULKAN_OK);SatchelVulkanBufferStats stats={};check(satchel_vulkan_buffer_stats(&stats)==SATCHEL_VULKAN_OK && !stats.live_buffers && !stats.allocated_bytes && !satchel_vulkan_compute_pipeline_live_count() && !satchel_vulkan_submission_live_count() && !satchel_vulkan_device_live_count());
 std::printf("Scalar schema/GPU: %u ASan/UBSan assertions;4 dynamic programs/24 GPU runs, guards/pointer snapshots/fences/cleanup; %u safe-schema single-bit variants accepted. No game shader admission.\n",checks,admitted_mutations);
}
