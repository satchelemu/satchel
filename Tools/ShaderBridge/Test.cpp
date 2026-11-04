#include "ShaderBridge.h"
#include "Fixture.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
static unsigned checks;
static void check(bool value) { ++checks; if (!value) { std::fprintf(stderr,"shader bridge check %u failed\n", checks); std::exit(1); } }
int main(int argc, char **argv) {
    std::vector<char> source(SATCHEL_SHADER_MAX_MSL_BYTES);
    SatchelShaderInfo info = {};
    auto run = [&](const void *data, size_t length) {
        return satchel_shader_spirv_to_msl(data,length,source.data(),source.size(),&info);
    };
    check(run(satchel_shader_fixture,sizeof(satchel_shader_fixture)) == SATCHEL_SHADER_OK);
    check(info.stage == SATCHEL_SHADER_COMPUTE && info.binding_count == 1);
    check(info.bindings[0].kind == SATCHEL_SHADER_BUFFER && info.bindings[0].binding == 0);
    check(info.bindings[0].active == 1);
    check(info.local_size[0] == 1 && info.local_size[1] == 1 && info.local_size[2] == 1);
    check(std::strcmp(info.entry_point,"satchel_main") == 0);
    check(info.source_bytes == std::strlen(source.data()));
    check(std::strstr(source.data(),"[[buffer(0)]]") != nullptr);
    char small[4] = {'x','x','x',0};
    check(satchel_shader_spirv_to_msl(satchel_shader_fixture,sizeof(satchel_shader_fixture),small,sizeof(small),&info)==SATCHEL_SHADER_CAPACITY);
    check(small[0]==0 && info.source_bytes==0);
    // Valid accessible output buffers are cleared on malformed modules.
    for (size_t length=20; length<sizeof(satchel_shader_fixture); length+=4) {
        check(run(satchel_shader_fixture,length)!=SATCHEL_SHADER_OK);
        check(source[0]==0 && info.source_bytes==0);
    }
    auto fixture=std::vector<uint32_t>(std::begin(satchel_shader_fixture),std::end(satchel_shader_fixture));
    for (size_t i=5;i<fixture.size();) {
        const uint32_t old=fixture[i]; const size_t length=old>>16;
        fixture[i]=old&0xffff; check(run(fixture.data(),fixture.size()*4)==SATCHEL_SHADER_INVALID);
        fixture[i]=(0xffffu<<16)|(old&0xffff); check(run(fixture.data(),fixture.size()*4)==SATCHEL_SHADER_INVALID);
        fixture[i]=old; i+=length;
    }
    for (uint32_t value: {0u,65537u,0xffffffffu}) {
        fixture[3]=value; check(run(fixture.data(),fixture.size()*4)==SATCHEL_SHADER_INVALID);
    }
    fixture[3]=13;
    for (uint32_t value: {0u,0x03022307u,0xffffffffu}) {
        fixture[0]=value; check(run(fixture.data(),fixture.size()*4)==SATCHEL_SHADER_INVALID);
    }
    fixture[0]=0x07230203;
    // Semantic mutations that the bridge must reject without exporting errors.
    for (size_t i=5;i<fixture.size();) {
        const size_t length=fixture[i]>>16; const uint32_t opcode=fixture[i]&0xffff;
        if (opcode==71 && fixture[i+2]==34) {
            fixture[i+3]=1; check(run(fixture.data(),fixture.size()*4)==SATCHEL_SHADER_UNSUPPORTED); fixture[i+3]=0;
        }
        if (opcode==71 && fixture[i+2]==33) {
            fixture[i+3]=31; check(run(fixture.data(),fixture.size()*4)==SATCHEL_SHADER_UNSUPPORTED); fixture[i+3]=0;
        }
        if (opcode==16) {
            fixture[i+3]=1025; check(run(fixture.data(),fixture.size()*4)==SATCHEL_SHADER_UNSUPPORTED); fixture[i+3]=1;
        }
        i+=length;
    }
    // Deterministic malformed framing sweep: bounded before compiler entry.
    uint32_t random=0xa551;
    for (unsigned i=0;i<1000;++i) {
        random=random*1664525u+1013904223u;
        std::vector<uint32_t> bad(6+(random%64),0);
        bad[0]=0x07230203; bad[1]=0x10000; bad[3]=16;
        bad[5]=(0xffffu<<16)|(random&0xffff);
        check(run(bad.data(),bad.size()*4)==SATCHEL_SHADER_INVALID);
    }
    check(run(nullptr,20)==SATCHEL_SHADER_INVALID);
    check(run(satchel_shader_fixture,SATCHEL_SHADER_MAX_SPIRV_BYTES+4)==SATCHEL_SHADER_INVALID);
    check(satchel_shader_spirv_to_msl(satchel_shader_fixture,sizeof(satchel_shader_fixture),source.data(),source.size(),nullptr)==SATCHEL_SHADER_INVALID);
    check(run(satchel_shader_fixture,sizeof(satchel_shader_fixture))==SATCHEL_SHADER_OK);
    std::vector<unsigned char> unaligned(sizeof(satchel_shader_fixture)+1);
    std::memcpy(unaligned.data()+1,satchel_shader_fixture,sizeof(satchel_shader_fixture));
    check(run(unaligned.data()+1,sizeof(satchel_shader_fixture))==SATCHEL_SHADER_OK);
    check(satchel_shader_spirv_to_msl(unaligned.data()+1,sizeof(satchel_shader_fixture),
        reinterpret_cast<char *>(unaligned.data()+1),sizeof(satchel_shader_fixture),&info)==SATCHEL_SHADER_INVALID);
    check(std::memcmp(unaligned.data()+1,satchel_shader_fixture,sizeof(satchel_shader_fixture))==0);
    check(satchel_shader_spirv_to_msl(satchel_shader_fixture,sizeof(satchel_shader_fixture),nullptr,1,&info)==SATCHEL_SHADER_INVALID);
    check(satchel_shader_spirv_to_msl(satchel_shader_fixture,sizeof(satchel_shader_fixture),source.data(),0,&info)==SATCHEL_SHADER_INVALID);
    check(satchel_shader_spirv_to_msl(satchel_shader_fixture,sizeof(satchel_shader_fixture),source.data(),SATCHEL_SHADER_MAX_MSL_BYTES+1,&info)==SATCHEL_SHADER_INVALID);
    // Optional original Maxwell EXIT output from the separately tested translator.
    if (argc==2) {
        std::ifstream file(argv[1],std::ios::binary);
        std::vector<char> exitSpv((std::istreambuf_iterator<char>(file)),{});
        check(!exitSpv.empty() && exitSpv.size()<=SATCHEL_SHADER_MAX_SPIRV_BYTES);
        check(run(exitSpv.data(),exitSpv.size())==SATCHEL_SHADER_OK);
        check(info.stage==SATCHEL_SHADER_COMPUTE && info.local_size[0]==1);
        check(info.binding_count==1 && info.bindings[0].kind==SATCHEL_SHADER_BUFFER && info.bindings[0].binding==0);
        check(info.bindings[0].active==0);
        std::puts("Original Maxwell EXIT SPIR-V translated to iOS MSL (no pixel-output claim).");
    }
    std::printf("Shader bridge: %u checks passed.\n",checks);
}
