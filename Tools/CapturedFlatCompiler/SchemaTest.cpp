/* Independent renderer schema applied to actual compiler outputs. No Vulkan
 * device/pipeline/draw is created by this test. */
#include "CapturedFlat.h"
#include "VulkanFlatSchema.inc"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <cstring>
static_assert(sizeof(SatchelCFModule)==sizeof(SatchelVulkanFlatModule));
static_assert(offsetof(SatchelCFModule,words)==offsetof(SatchelVulkanFlatModule,words));
static SatchelVulkanFlatModule read(const std::filesystem::path&p){SatchelVulkanFlatModule m={};m.version=1;m.size=sizeof m;auto size=std::filesystem::file_size(p);if(size%4||size<20||size>sizeof m.words)throw 1;m.word_count=(uint32_t)(size/4);std::ifstream f(p,std::ios::binary);if(!f.read((char*)m.words,size))throw 2;return m;}
int main(int argc,char**argv){try{if(argc<2)return 1;unsigned count=0;for(int i=1;i<argc;i++)for(const auto&entry:std::filesystem::directory_iterator(argv[i])){auto p=entry.path();auto name=p.filename().string();auto at=name.find("-vertex.spv");if(at==std::string::npos)continue;name.replace(at,11,"-fragment.spv");auto vertex=read(p),fragment=read(p.parent_path()/name);SatchelVulkanFlatPair pair={};if(satchel_vulkan_flat_validate_pair(&vertex,&fragment,&pair)!=SATCHEL_VULKAN_OK){std::cerr<<"Pair schema rejected original output "<<p.filename()<<"\n";return 2;}if(pair.vertex.declared_uniforms!=1||pair.fragment.declared_uniforms!=1||pair.vertex.active_uniforms||pair.fragment.active_uniforms)return 3;count++;}std::cout<<count<<" actual original compiler pairs passed independent typed renderer schema (no pipeline/draw claim).\n";return 0;}catch(...){return 4;}}
