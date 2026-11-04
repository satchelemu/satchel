// Strict independently checked schema of the bounded scalar emitter, not an
// exact shader hash. All instruction bodies vary with the accepted decoded IR.
namespace {
struct ScalarParser {
 const std::vector<uint32_t> &w;size_t at=5;
 bool take(uint32_t op,std::initializer_list<uint32_t> args){if(at>=w.size() || w[at]!=((uint32_t)(args.size()+1)<<16|op) || args.size()>w.size()-at-1)return false;size_t k=at+1;for(uint32_t x:args)if(w[k++]!=x)return false;at=k;return true;}
 const uint32_t *peek(uint32_t op,size_t count)const{return at<w.size() && count<=w.size()-at && w[at]==((uint32_t)count<<16|op)?w.data()+at:nullptr;}
};
bool scalar_schema(const std::vector<uint32_t> &w,const SatchelScalarReflection &r){
 if(w.size()<64 || w.size()>4096 || r.version!=1 || r.size!=sizeof(r) || r.spirv_words!=w.size() || r.semantic_ops<2 || r.semantic_ops>32 || r.cb_slot>17 || r.uniform_binding>7 || r.storage_binding>7 || r.uniform_binding==r.storage_binding || r.unused_memory_mask!=3 || r.uniform_declared_bytes!=65536 || !r.uniform_required_bytes || r.uniform_required_bytes>65536 || (r.uniform_required_bytes&3) || !r.storage_required_bytes || r.storage_required_bytes>1048576 || (r.storage_required_bytes&3) || r.pointer_cb_offset>65528 || (r.pointer_cb_offset&3) || !r.expected_storage_address || r.expected_storage_address>=(UINT64_C(1)<<40) || !r.store_count || r.store_count>16)return false;
 if(w[0]!=0x07230203 || w[1]!=0x00010300 || w[2] || w[3]<19 || w[3]>512 || w[4])return false;ScalarParser p{w};
 if(!p.take(17,{1}) || !p.take(14,{0,1}) || !p.take(15,{5,15,0x6e69616d,0}) || !p.take(16,{15,17,1,1,1}) ||
 !p.take(71,{5,6,16}) || !p.take(71,{6,2}) || !p.take(72,{6,0,35,0}) || !p.take(71,{9,34,0}) || !p.take(71,{9,33,r.uniform_binding}) ||
 !p.take(71,{10,6,4}) || !p.take(71,{11,2}) || !p.take(72,{11,0,35,0}) || !p.take(71,{14,34,0}) || !p.take(71,{14,33,r.storage_binding}) ||
 !p.take(19,{1}) || !p.take(21,{2,32,0}) || !p.take(23,{3,2,4}) || !p.take(33,{4,1}))return false;
 uint32_t constants[512]={};bool is_constant[512]={},is_value[512]={};uint32_t next=17,constant_count=0;
 while(const uint32_t *i=p.peek(43,4)){if(i[1]!=2 || i[2]!=next || next>=w[3] || constant_count==128)return false;for(uint32_t n=17;n<next;n++)if(constants[n]==i[3])return false;constants[next]=i[3];is_constant[next]=true;++next;++constant_count;p.at+=4;}
 if(constant_count<2 || constants[17]!=0 || constants[18]!=4096)return false;is_value[17]=true;
 if(!p.take(28,{5,3,18}) || !p.take(30,{6,5}) || !p.take(32,{7,2,6}) || !p.take(32,{8,2,2}) || !p.take(59,{7,9,2}) ||
 !p.take(29,{10,2}) || !p.take(30,{11,10}) || !p.take(32,{12,12,11}) || !p.take(32,{13,12,2}) || !p.take(59,{12,14,12}) || !p.take(54,{1,15,0,4}) || !p.take(248,{16}))return false;
 uint32_t uniform_required=0,storage_required=0,stores=0,ops=0;bool pointer_low=false,pointer_high=false;
 auto constant_value=[&](uint32_t id,uint32_t *value){if(id>=next || id>=512 || !is_constant[id])return false;*value=constants[id];return true;};
 while(!p.peek(253,1)){
  if(++ops>64 || next>=w[3])return false;
  if(const uint32_t *i=p.peek(65,7)){
   if(i[1]!=8 || i[2]!=next || i[3]!=9 || i[4]!=17)return false;uint32_t row=0,col=0;if(!constant_value(i[5],&row) || !constant_value(i[6],&col) || row>=4096 || col>=4)return false;const uint32_t offset=row*16+col*4;
   p.at+=7;uint32_t ptr=next++;if(next>=w[3] || !p.take(61,{2,next,ptr}))return false;is_value[next++]=true;uniform_required=std::max(uniform_required,offset+4);pointer_low|=offset==r.pointer_cb_offset;pointer_high|=offset==r.pointer_cb_offset+4;
  }else if(const uint32_t *i=p.peek(128,5)){
   if(i[1]!=2 || i[2]!=next || i[3]>=next || i[4]>=next || !is_value[i[3]] || !is_value[i[4]])return false;is_value[next++]=true;p.at+=5;
  }else if(const uint32_t *i=p.peek(65,6)){
   if(i[1]!=13 || i[2]!=next || i[3]!=14 || i[4]!=17)return false;uint32_t index=0;if(!constant_value(i[5],&index) || index>=1048576/4 || stores>=16)return false;uint32_t offset=index*4;uint32_t ptr=next++;p.at+=6;
   const uint32_t *store=p.peek(62,3);if(!store || store[1]!=ptr || store[2]>=next || !is_value[store[2]])return false;p.at+=3;
   if(stores>=r.store_count || r.stores[stores].offset!=offset || r.stores[stores].size!=4)return false;++stores;storage_required=std::max(storage_required,offset+4);
  }else return false;
 }
 if(!p.take(253,{}) || !p.take(56,{}) || p.at!=w.size() || next!=w[3] || stores!=r.store_count || !pointer_low || !pointer_high || uniform_required!=r.uniform_required_bytes || storage_required!=r.storage_required_bytes)return false;
 for(uint32_t i=stores;i<16;i++)if(r.stores[i].offset || r.stores[i].size)return false;return true;
}
}
extern "C" __attribute__((visibility("default"))) int satchel_vulkan_scalar_validate(const void *source,size_t size,const SatchelScalarReflection *reflection){
 if(!source || !reflection || size<256 || size>16384 || (size&3))return 0;try{SatchelScalarReflection r=*reflection;std::vector<uint32_t> code(size/4);std::memcpy(code.data(),source,size);return scalar_schema(code,r);}catch(...){return 0;}
}
