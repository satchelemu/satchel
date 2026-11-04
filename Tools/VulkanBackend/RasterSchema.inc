// Independent structural verifier of the bounded dynamic raster compiler schema.
// It admits varying locations, component selections and literal IEEE754 bits;
// no fixture hashes, reference shader substitution or reflection-only trust.
namespace {
struct RasterReader {
 const uint32_t *words;size_t count,cursor=5;
 bool match(uint32_t opcode,std::initializer_list<uint32_t> args){
  if(cursor>=count || words[cursor]!=(uint32_t(args.size()+1)<<16|opcode) || args.size()+1>count-cursor)return false;
  size_t at=cursor+1;for(auto value:args)if(words[at++]!=value)return false;cursor=at;return true;
 }
 bool opcode(uint32_t code,uint32_t length)const{return cursor<count && words[cursor]==(length<<16|code) && length<=count-cursor;}
};
bool raster_schema(const SatchelRasterModule &module,uint32_t stage){
 const auto &r=module.reflection;const auto *w=module.words;
 if(r.version!=1 || r.size!=sizeof(r) || r.stage!=stage || (stage!=1 && stage!=5) || r.spirv_words<5 || r.spirv_words>SATCHEL_RASTER_SPIRV_WORDS || !r.semantic_ops || r.semantic_ops>SATCHEL_RASTER_MAX_OPS || r.descriptor_count || r.required_capabilities!=1 || r.unused_memory_mask!=7 || r.unused_state_mask!=8 || r.interstage_components || r.position_components!=(stage==1?15u:0u) || r.color_components!=(stage==5?15u:0u))return false;
 if(w[0]!=0x07230203 || w[1]!=0x10300 || w[2] || w[4])return false;
 uint32_t vars[8]={},loads[8]={},masks[8]={},next=10,attributes=0;
 for(unsigned i=0;i<8;i++){if(r.input_components[i]&~15u)return false;if(r.input_components[i]){if(stage!=1)return false;vars[i]=next++;++attributes;}}
 RasterReader q{w,r.spirv_words};if(!q.match(17,{1}) || !q.match(14,{0,1}) || !q.opcode(15,6+attributes))return false;
 const uint32_t entry[]={stage==1?0u:4u,7,0x6e69616d,0,9};for(unsigned i=0;i<5;i++)if(w[q.cursor+1+i]!=entry[i])return false;
 size_t ei=q.cursor+6;for(auto var:vars)if(var && w[ei++]!=var)return false;q.cursor=ei;
 if(stage==5 && !q.match(16,{7,7}))return false;
 if(!q.match(71,{9,stage==1?11u:30u,0}))return false;
 for(unsigned i=0;i<8;i++)if(vars[i] && !q.match(71,{vars[i],30,i}))return false;
 if(!q.match(19,{1}) || !q.match(22,{2,32}) || !q.match(23,{3,2,4}) || !q.match(33,{4,1}) || !q.match(32,{5,1,3}) || !q.match(32,{6,3,3}))return false;
 uint32_t constant_ids[4]={},constant_count=0;
 while(q.opcode(43,4)){if(constant_count==4 || w[q.cursor+1]!=2 || w[q.cursor+2]!=next)return false;constant_ids[constant_count++]=next++;q.cursor+=4;}
 for(auto var:vars)if(var && !q.match(59,{5,var,1}))return false;
 if(!q.match(59,{6,9,3}) || !q.match(54,{1,7,0,4}) || !q.match(248,{8}))return false;
 for(unsigned i=0;i<8;i++)if(vars[i]){loads[i]=next++;if(!q.match(61,{3,loads[i],vars[i]}))return false;}
 uint32_t extracts[4]={},extract_count=0;
 while(q.opcode(81,5)){
  if(extract_count==4 || w[q.cursor+1]!=2 || w[q.cursor+2]!=next || w[q.cursor+4]>=4)return false;
  unsigned location=8;for(unsigned i=0;i<8;i++)if(loads[i] && loads[i]==w[q.cursor+3])location=i;
  if(location==8)return false;masks[location]|=1u<<w[q.cursor+4];extracts[extract_count++]=next++;q.cursor+=5;
 }
 if(constant_count+extract_count!=4 || !q.opcode(80,7) || w[q.cursor+1]!=3 || w[q.cursor+2]!=next)return false;
 unsigned ci=0,xi=0;for(unsigned i=0;i<4;i++){uint32_t source=w[q.cursor+3+i];if(ci<constant_count && source==constant_ids[ci])++ci;else if(xi<extract_count && source==extracts[xi])++xi;else return false;}
 if(ci!=constant_count || xi!=extract_count)return false;q.cursor+=7;const uint32_t output=next++;
 if(!q.match(62,{9,output}) || !q.match(253,{}) || !q.match(56,{}) || q.cursor!=q.count || w[3]!=next)return false;
 for(unsigned i=0;i<8;i++)if(masks[i]&~r.input_components[i])return false;
 return true;
}
bool raster_state(const SatchelRasterState &s){return (s.known==247 || s.known==255) && s.single_vertex_program==1 && s.viewport_transform_enabled==1 && s.depth_zero_to_one==1 && (!(s.known&8) || s.y_negate<=1) && s.origin_upper_left==1 && s.alpha_test_always==1 && !s.alpha_to_coverage && !s.transform_feedback;}
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_raster_validate(const SatchelRasterModule *m,uint32_t stage){return m && raster_schema(*m,stage)?SATCHEL_VULKAN_OK:SATCHEL_VULKAN_UNSUPPORTED;}
