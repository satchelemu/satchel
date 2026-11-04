#include "VulkanCompute.h"
#include "../MaxwellShaderCompiler/SatchelShaderFixture.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <chrono>
#include <thread>
static unsigned checks;
static void check_at(bool v,unsigned line){++checks;if(!v){std::fprintf(stderr,"Vulkan compute assertion failed line%u check%u\n",line,checks);std::exit(1);}}
#define check(v) check_at((v),__LINE__)
static uint64_t ldc(unsigned rd,unsigned bytes,unsigned offset){return UINT64_C(0xef90000000000000)|((uint64_t)(bytes==8?5:4)<<48)|(UINT64_C(3)<<36)|((uint64_t)offset<<20)|(UINT64_C(7)<<16)|(UINT64_C(255)<<8)|rd;}
static std::vector<uint8_t> compile(unsigned ub=1,unsigned sb=2){
    const uint64_t words[8]={0,ldc(0,8,0),ldc(2,4,12),ldc(3,4,16),0,
        UINT64_C(0x5c10000000000000)|(UINT64_C(3)<<20)|(UINT64_C(7)<<16)|(UINT64_C(2)<<8)|4,
        UINT64_C(0xeed8000000000000)|(UINT64_C(4)<<48)|(UINT64_C(1)<<45)|(UINT64_C(7)<<16)|4,
        UINT64_C(0xe300000000000000)|(UINT64_C(7)<<16)|15};
    const uint32_t cb[8]={0x10000,0,16,5,7,0,0,0};SatchelShaderFixtureInput input={};input.version=1;input.struct_size=sizeof(input);input.code=(const uint8_t*)words;input.constant_buffer=(const uint8_t*)cb;input.code_size=64;input.constant_buffer_size=32;input.local_size_x=input.local_size_y=input.local_size_z=1;input.constant_buffer_slot=3;input.uniform_binding=ub;input.storage_binding=sb;input.storage_size=16;input.storage_offset_alignment=16;input.storage_gpu_address=0x10000;
    std::vector<uint8_t> code(16384);SatchelShaderFixtureReflection reflection={};check(satchel_shader_fixture_compile(&input,code.data(),(uint32_t)code.size(),&reflection)==SATCHEL_SHADER_FIXTURE_OK);check(reflection.spirv_size==1208 && reflection.active_buffer_count==2 && reflection.buffers[0].flags==0 && reflection.buffers[1].binding==ub && reflection.buffers[2].binding==sb);code.resize(reflection.spirv_size);return code;
}
static int always(void*){return 1;}
static int countdown(void *p){unsigned *n=(unsigned*)p;return --*n==0;}
struct Mutation {std::vector<uint8_t> *code;SatchelVulkanComputeBindings *bindings;unsigned calls;};
static int mutate(void *p){auto *m=(Mutation*)p;if(!m->calls++){if(m->code)std::memset(m->code->data(),0,m->code->size());if(m->bindings)*m->bindings={};}return 0;}
static void finish(uint64_t device,uint64_t job){SatchelVulkanSubmissionReport report={};check(satchel_vulkan_submission_wait(device,job,5000,nullptr,nullptr,&report)==SATCHEL_VULKAN_OK && report.completed && !report.resources_retained);}
static void baseline(){SatchelVulkanBufferStats s={};check(satchel_vulkan_buffer_stats(&s)==SATCHEL_VULKAN_OK && !s.live_buffers && !s.allocated_bytes && !satchel_vulkan_device_live_count() && !satchel_vulkan_submission_live_count() && !satchel_vulkan_compute_pipeline_live_count());}
int main(){auto code=compile();baseline();uint64_t device=0,other=0;SatchelGuestDeviceCaps caps={};check(satchel_vulkan_device_create(0,&device,&caps)==SATCHEL_VULKAN_OK);check(satchel_vulkan_device_create(0,&other,&caps)==SATCHEL_VULKAN_OK);SatchelVulkanComputeLimits limits={};check(satchel_vulkan_compute_limits(device,&limits)==SATCHEL_VULKAN_OK && limits.version==1 && limits.struct_size==sizeof(limits) && limits.precise_math_requested && limits.max_uniform_range>=65536 && limits.max_stage_resources>=2);
    check(satchel_vulkan_compute_limits(0,&limits)==SATCHEL_VULKAN_INVALID && !limits.version);check(satchel_vulkan_compute_limits(device,&limits)==SATCHEL_VULKAN_OK);
    const auto original=code;SatchelVulkanComputeProfile profile={1,sizeof(profile),1,2};uint64_t pipeline=0;
    check(satchel_vulkan_compute_pipeline_create_original(device,code.data(),code.size(),&profile,&pipeline,nullptr,nullptr)==SATCHEL_VULKAN_OK && pipeline && satchel_vulkan_compute_pipeline_live_count()==1);
    // Every nonbinding bit/structural mutation fails before the driver. With an
    // unchanged profile, mutating either Binding also fails exact admission.
    for(size_t i=0;i<code.size()*8;i++){code[i/8]^=(uint8_t)(1u<<(i%8));uint64_t rejected=99;check(satchel_vulkan_compute_pipeline_create_original(device,code.data(),code.size(),&profile,&rejected,nullptr,nullptr)==SATCHEL_VULKAN_UNSUPPORTED && !rejected);code[i/8]^=(uint8_t)(1u<<(i%8));}
    for(size_t i=0;i<code.size();i++){uint64_t rejected=99;check(satchel_vulkan_compute_pipeline_create_original(device,code.data(),i,&profile,&rejected,nullptr,nullptr)==SATCHEL_VULKAN_INVALID && !rejected);}
    check(satchel_vulkan_compute_pipeline_destroy(other,pipeline)==SATCHEL_VULKAN_INVALID);
    const uint64_t uoff=limits.uniform_offset_alignment,soff=limits.storage_offset_alignment;
    check(uoff && soff && uoff<65536 && soff<65536);uint64_t uniform=0,storage=0,wrong=0;SatchelVulkanBufferInfo info={};
    check(satchel_vulkan_buffer_create(device,65536+uoff*2,SATCHEL_VULKAN_BUFFER_UNIFORM,&uniform,&info)==SATCHEL_VULKAN_OK && info.usage==8);
    check(satchel_vulkan_buffer_create(device,soff+64,SATCHEL_VULKAN_BUFFER_STORAGE,&storage,&info)==SATCHEL_VULKAN_OK);
    check(satchel_vulkan_buffer_create(device,65536+uoff,SATCHEL_VULKAN_BUFFER_STORAGE,&wrong,&info)==SATCHEL_VULKAN_OK);
    SatchelVulkanComputeBindings binding={uniform,uoff,65536,storage,soff,16};
    const uint32_t cases[][2]={{0,0},{5,7},{UINT32_MAX,1},{0x80000000,0x80000000},{0x7fc00001,0},{0x7f800001,0},{0xff800001,0},{0x7fffffff,0},{0x7f800000,0},{0xff800000,0},{1,0},{0x007fffff,1},{0x00800000,0xffffffff},{0xdeadbeef,0x11223344}};
    std::vector<uint8_t> result(soff+64),expected(result.size(),0xa5),ubo(65536+uoff*2,0x3c);
    for(const auto &pair:cases){std::fill(ubo.begin()+uoff,ubo.begin()+uoff+65536,0);uint32_t cb[8]={0x10000,0,16,pair[0],pair[1],0,0,0};std::memcpy(ubo.data()+uoff,cb,32);
        check(satchel_vulkan_buffer_write(device,uniform,0,ubo.data(),ubo.size())==SATCHEL_VULKAN_OK);check(satchel_vulkan_buffer_write(device,storage,0,expected.data(),expected.size())==SATCHEL_VULKAN_OK);uint64_t job=0;
        check(satchel_vulkan_compute_submit_original(device,pipeline,&binding,&job,nullptr,nullptr)==SATCHEL_VULKAN_OK && job);
        check(satchel_vulkan_compute_pipeline_destroy(device,pipeline)==SATCHEL_VULKAN_BUSY);check(satchel_vulkan_device_destroy(device)==SATCHEL_VULKAN_BUSY);check(satchel_vulkan_buffer_destroy(device,uniform)==SATCHEL_VULKAN_BUSY);
        check(satchel_vulkan_buffer_read(device,storage,0,result.data(),result.size())==SATCHEL_VULKAN_BUSY);check(satchel_vulkan_buffer_write(device,uniform,0,ubo.data(),ubo.size())==SATCHEL_VULKAN_BUSY);
        SatchelVulkanSubmissionReport report={};check(satchel_vulkan_submission_wait(device,job,5000,always,nullptr,&report)==SATCHEL_VULKAN_CANCELLED && report.resources_retained && !report.completed);
        finish(device,job);check(satchel_vulkan_buffer_read(device,storage,0,result.data(),result.size())==SATCHEL_VULKAN_OK);uint32_t actual=0;std::memcpy(&actual,result.data()+soff,4);check(actual==uint32_t(pair[0]+pair[1]));
        check(!std::memcmp(result.data(),expected.data(),soff) && !std::memcmp(result.data()+soff+4,expected.data()+soff+4,result.size()-soff-4));
        std::vector<uint8_t> uniform_after(ubo.size());check(satchel_vulkan_buffer_read(device,uniform,0,uniform_after.data(),uniform_after.size())==SATCHEL_VULKAN_OK && uniform_after==ubo);
    }
    // Explicit offsets/ranges/usages and parent IDs, immutable descriptor snapshot.
    for(unsigned i=0;i<10;i++){auto bad=binding;// populated below without data-dependent driver access
        if(i==0)bad.uniform_buffer=wrong;if(i==1)bad.uniform_offset=1;if(i==2)bad.storage_offset=1;if(i==3)bad.uniform_size=32;if(i==4)bad.storage_size=4;if(i==5)bad.storage_size=4097;if(i==6)bad.uniform_offset=UINT64_MAX;if(i==7)bad.storage_offset=UINT64_MAX;if(i==8)bad.uniform_buffer=storage;if(i==9)bad.storage_buffer=uniform;
        uint64_t job=99;check(satchel_vulkan_compute_submit_original(device,pipeline,&bad,&job,nullptr,nullptr)!=SATCHEL_VULKAN_OK && !job);
    }
    uint64_t job=99;check(satchel_vulkan_compute_submit_original(other,pipeline,&binding,&job,nullptr,nullptr)==SATCHEL_VULKAN_INVALID && !job);
    for(unsigned point=1;point<=2;point++){unsigned n=point;check(satchel_vulkan_compute_submit_original(device,pipeline,&binding,&job,countdown,&n)==SATCHEL_VULKAN_CANCELLED && !job && !satchel_vulkan_submission_live_count());}
    auto copied=binding;Mutation mutation={nullptr,&copied,0};check(satchel_vulkan_compute_submit_original(device,pipeline,&copied,&job,mutate,&mutation)==SATCHEL_VULKAN_OK && job && !copied.uniform_buffer);finish(device,job);
    // All42 binding pairs consume actual fresh compiler output and real pipelines.
    for(unsigned ub=1;ub<=7;ub++)for(unsigned sb=1;sb<=7;sb++)if(ub!=sb){auto variant=compile(ub,sb);SatchelVulkanComputeProfile p={1,sizeof(p),ub,sb};uint64_t t=0;
        check(satchel_vulkan_compute_pipeline_create_original(device,variant.data(),variant.size(),&p,&t,nullptr,nullptr)==SATCHEL_VULKAN_OK);check(satchel_vulkan_compute_submit_original(device,t,&binding,&job,nullptr,nullptr)==SATCHEL_VULKAN_OK);finish(device,job);check(satchel_vulkan_compute_pipeline_destroy(device,t)==SATCHEL_VULKAN_OK);}
    for(unsigned point=1;point<=3;point++){unsigned n=point;uint64_t t=99;check(satchel_vulkan_compute_pipeline_create_original(device,code.data(),code.size(),&profile,&t,countdown,&n)==SATCHEL_VULKAN_CANCELLED && !t && satchel_vulkan_compute_pipeline_live_count()==1);}
    Mutation creation={&code,nullptr,0};uint64_t t=0;check(satchel_vulkan_compute_pipeline_create_original(device,code.data(),code.size(),&profile,&t,mutate,&creation)==SATCHEL_VULKAN_OK);check(code!=original);code=original;check(satchel_vulkan_compute_pipeline_destroy(device,t)==SATCHEL_VULKAN_OK);
    uint64_t slots[7]={};for(auto &slot:slots)check(satchel_vulkan_compute_pipeline_create_original(device,code.data(),code.size(),&profile,&slot,nullptr,nullptr)==SATCHEL_VULKAN_OK);check(satchel_vulkan_compute_pipeline_live_count()==8);check(satchel_vulkan_compute_pipeline_create_original(device,code.data(),code.size(),&profile,&t,nullptr,nullptr)==SATCHEL_VULKAN_BUSY && !t);
    check(satchel_vulkan_compute_submit_original(device,pipeline,&binding,&job,nullptr,nullptr)==SATCHEL_VULKAN_OK);SatchelVulkanStatus drained=SATCHEL_VULKAN_BUSY;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(drained==SATCHEL_VULKAN_BUSY && std::chrono::steady_clock::now()<deadline){drained=satchel_vulkan_device_drain(device);if(drained==SATCHEL_VULKAN_BUSY)std::this_thread::sleep_for(std::chrono::milliseconds(1));}check(drained==SATCHEL_VULKAN_OK && !satchel_vulkan_submission_live_count());
    check(satchel_vulkan_device_destroy(device)==SATCHEL_VULKAN_OK && !satchel_vulkan_compute_pipeline_live_count());check(satchel_vulkan_compute_pipeline_destroy(device,pipeline)==SATCHEL_VULKAN_INVALID);check(satchel_vulkan_device_destroy(other)==SATCHEL_VULKAN_OK);baseline();
    std::printf("Original compiled Maxwell -> Vulkan compute: %u ASan/UBSan assertions;14 integer/NaN-bit operand pairs,42 binding pairs, actual GPU guards/fences and retained pipeline cleanup. No game shader support.\n",checks);
}
