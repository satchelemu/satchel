#include "VulkanFlat.h"
namespace {
/* Independent typed module admission. No compiler reflection is trusted and no
 * supplied word is replaced. IDs, decoration targets and access paths are all
 * bounded; the one entry is a single straight-line block with no function calls. */
struct FlatNode {
 uint32_t kind=0,type=0,element=0,count=0,storage=0,literal=0,root=0;
 uint32_t members[8]={},decor=0,location=0,binding=0,set=0,stride=0;
 uint32_t member_decor[8]={},member_builtin[8]={},member_offset[8]={};
 uint32_t path=0,component=0,write_mask=0,read_mask=0;
 bool named=false;uint32_t named_members=0;
};
enum {FD_BLOCK=1,FD_STRIDE=2,FD_FLAT=4,FD_LOCATION=8,FD_BINDING=16,FD_SET=32};
struct FlatParser {
 FlatNode ids[SATCHEL_VULKAN_FLAT_ID_LIMIT]={};uint32_t bound=0,stage=0,phase=0;
 uint32_t entry=0,interfaces[16]={},interface_count=0,cap=0,memory=0,mode=0;
 uint32_t uniform=0,block=0,varying=0,color=0;bool function=false,label=false,returned=false,closed=false;
 bool valid(uint32_t n)const{return n&&n<bound;}
 bool defined(uint32_t n)const{return valid(n)&&ids[n].kind;}
 bool define(uint32_t n,uint32_t kind){if(!valid(n)||ids[n].kind)return false;ids[n].kind=kind;return true;}
 bool scalar(uint32_t n,uint32_t kind)const{return defined(n)&&ids[n].kind==kind;}
 bool integer(uint32_t n,uint32_t sign)const{return scalar(n,21)&&ids[n].literal==sign;}
 bool vector(uint32_t n,uint32_t element_kind,uint32_t sign=0)const{return scalar(n,23)&&ids[n].count==4&&(element_kind==21?integer(ids[n].element,sign):scalar(ids[n].element,element_kind));}
 bool array(uint32_t n,uint32_t kind,uint32_t count)const{return scalar(n,28)&&ids[n].count==count&&(kind==21?integer(ids[n].element,0):scalar(ids[n].element,kind));}
 bool string(const uint32_t *w,uint32_t start,uint32_t count)const{
  if(start>=count)return false;bool ended=false;for(uint32_t i=start;i<count;i++)for(unsigned b=0;b<4;b++){uint8_t c=(uint8_t)(w[i]>>(b*8));if(ended&&c)return false;if(c&&(c<32||c>126))return false;if(!c)ended=true;}return ended;
 }
 bool support(uint32_t n)const{
  if(!scalar(n,30))return false;const auto &s=ids[n];if(s.count!=8||s.decor!=FD_BLOCK)return false;
  const uint32_t offsets[]={0,16,144,160,176,192,1360,1376};
  for(unsigned i=0;i<8;i++)if(s.member_decor[i]!=2||s.member_offset[i]!=offsets[i])return false;
  const auto *m=s.members;
  return integer(m[0],0)&&array(m[1],21,8)&&ids[m[1]].decor==FD_STRIDE&&ids[m[1]].stride==16&&vector(m[2],22)&&vector(m[3],22)&&integer(m[4],1)&&array(m[5],22,73)&&ids[m[5]].decor==FD_STRIDE&&ids[m[5]].stride==16&&vector(m[6],21,1)&&integer(m[7],1);
 }
 bool per_vertex(uint32_t n)const{
  if(!scalar(n,30))return false;const auto &s=ids[n];if(s.count!=4||s.decor!=FD_BLOCK)return false;
  const uint32_t builtins[]={0,1,3,4};for(unsigned i=0;i<4;i++)if(s.member_decor[i]!=1||s.member_builtin[i]!=builtins[i])return false;
  return vector(s.members[0],22)&&scalar(s.members[1],22)&&array(s.members[2],22,8)&&!ids[s.members[2]].decor&&array(s.members[3],22,1)&&!ids[s.members[3]].decor;
 }
 bool globals(SatchelVulkanFlatStage &facts){
  facts={};facts.stage=stage;facts.required_capabilities=1;uint32_t ins=0,outs=0,builtins=0,uniforms=0;bool listed[SATCHEL_VULKAN_FLAT_ID_LIMIT]={};
  for(unsigned i=0;i<interface_count;i++){uint32_t n=interfaces[i];if(!scalar(n,59)||listed[n]||(ids[n].storage!=1&&ids[n].storage!=3))return false;listed[n]=true;}
  for(uint32_t i=1;i<bound;i++){
   auto &n=ids[i];if(!n.kind&&(n.decor||n.named||n.named_members))return false;
   for(unsigned j=0;j<8;j++)if(n.member_decor[j]||((n.named_members>>j)&1u)){if(n.kind!=30||j>=n.count)return false;}
   if(n.kind==59){const uint32_t t=ids[n.type].element;
    if(n.storage==2){if(listed[i]||++uniforms!=1||n.decor!=(FD_SET|FD_BINDING)||n.set||n.binding||!support(t))return false;uniform=i;facts.declared_uniforms=1;facts.support_member_extent=1380;}
    else if(!listed[i])return false;
    else if(n.storage==3&&stage==1&&per_vertex(t)){if(n.decor||++builtins!=1)return false;block=i;facts.declared_clip_components=8;facts.declared_cull_components=1;}
    else {
     if(!vector(t,22)||!(n.decor&FD_LOCATION)||(n.decor&~(FD_LOCATION|FD_FLAT))||n.location>=8)return false;
     uint32_t bit=1u<<n.location;
     if(n.storage==1){if(ins&bit)return false;ins|=bit;facts.inputs[n.location]=15;facts.flat_inputs[n.location]=(n.decor&FD_FLAT)?15:0;if(stage==1&&(n.decor&FD_FLAT))return false;if(stage==5){if(varying||!(n.decor&FD_FLAT))return false;varying=i;}}
     else {if(outs&bit)return false;outs|=bit;facts.outputs[n.location]=15;facts.flat_outputs[n.location]=(n.decor&FD_FLAT)?15:0;if(stage==1){if(varying)return false;varying=i;}else {if(n.location||color||(n.decor&FD_FLAT))return false;color=i;}}
    }
   }else if(n.decor){
    if(n.kind==28){if(n.decor!=FD_STRIDE||n.stride!=16)return false;}
    else if(n.kind==30){if(n.decor!=FD_BLOCK||(!support(i)&&!(stage==1&&per_vertex(i))))return false;}
    else return false;
   }
  }
  return uniforms==1&&ins&&outs&&(stage==1?(builtins==1&&varying):(builtins==0&&varying&&color));
 }
 bool parse(const SatchelVulkanFlatModule &m,uint32_t expected,SatchelVulkanFlatStage &facts){
  if(m.version!=1||m.size!=sizeof(m)||m.word_count<5||m.word_count>SATCHEL_VULKAN_FLAT_WORD_LIMIT)return false;
  const auto *words=m.words;if(words[0]!=0x07230203||words[1]!=0x00010300||!words[3]||words[3]>SATCHEL_VULKAN_FLAT_ID_LIMIT||words[4])return false;bound=words[3];stage=expected;
  for(uint32_t at=5;at<m.word_count;){const auto *w=words+at;uint32_t count=w[0]>>16,op=w[0]&65535;if(!count||count>m.word_count-at||closed)return false;
   if(function&&(op!=65&&op!=61&&op!=62&&op!=248&&op!=253&&op!=56))return false;
   if(!function&&(op==65||op==61||op==62||op==248||op==253||op==56))return false;
   if(function&&((!label&&op!=248)||(returned&&op!=56)))return false;
   switch(op){
    case 17:if(count!=2||phase||w[1]!=1||cap++)return false;break;
    case 14:if(count!=3||phase||cap!=1||w[1]||w[2]!=1||memory++)return false;phase=2;break;
    case 15:if(count<5||count>21||phase!=2||entry||w[1]!=(stage==1?0u:4u)||!valid(w[2])||w[3]!=0x6e69616d||w[4])return false;entry=w[2];interface_count=count-5;for(unsigned i=0;i<interface_count;i++){if(!valid(w[i+5]))return false;interfaces[i]=w[i+5];}phase=3;break;
    case 16:if(count!=3||phase!=3||stage!=5||w[1]!=entry||w[2]!=7||mode++)return false;break;
    case 5:case 6:{unsigned start=op==5?2:3;if(phase<3||phase>4||count<start+1||!valid(w[1])||!string(w,start,count))return false;phase=4;if(op==5)ids[w[1]].named=true;else {if(w[2]>=8)return false;ids[w[1]].named_members|=1u<<w[2];}break;}
    case 71:{if(phase<3||phase>5||count<3||!valid(w[1]))return false;phase=5;auto &n=ids[w[1]];uint32_t flag=0;switch(w[2]){
      case 2:if(count!=3)return false;flag=FD_BLOCK;break;case 14:if(count!=3)return false;flag=FD_FLAT;break;
      case 6:if(count!=4||w[3]!=16)return false;flag=FD_STRIDE;n.stride=w[3];break;
      case 30:if(count!=4||w[3]>=8)return false;flag=FD_LOCATION;n.location=w[3];break;
      case 33:if(count!=4||w[3])return false;flag=FD_BINDING;n.binding=w[3];break;
      case 34:if(count!=4||w[3])return false;flag=FD_SET;n.set=w[3];break;default:return false;}
     if(n.decor&flag)return false;n.decor|=flag;break;}
    case 72:{if(phase<3||phase>5||count!=5||!valid(w[1])||w[2]>=8)return false;phase=5;auto &n=ids[w[1]];uint32_t flag=0;if(w[3]==11){if(w[4]>4||w[4]==2)return false;flag=1;n.member_builtin[w[2]]=w[4];}else if(w[3]==35){flag=2;n.member_offset[w[2]]=w[4];}else return false;if(n.member_decor[w[2]]&flag)return false;n.member_decor[w[2]]|=flag;break;}
    case 19:case 21:case 22:case 23:case 28:case 30:case 32:case 33:case 43:case 59:{
     if(phase<3||phase>6||count<2)return false;phase=6;uint32_t id=(op==43||op==59)?(count>2?w[2]:0):w[1];if(!define(id,op))return false;auto &n=ids[id];
     if(op==19){if(count!=2)return false;}
     else if(op==21){if(count!=4||w[2]!=32||w[3]>1)return false;n.literal=w[3];}
     else if(op==22){if(count!=3||w[2]!=32)return false;}
     else if(op==23){if(count!=4||w[3]!=4||(!scalar(w[2],21)&&!scalar(w[2],22)))return false;n.element=w[2];n.count=4;}
     else if(op==28){if(count!=4||(!scalar(w[2],21)&&!scalar(w[2],22))||!scalar(w[3],43)||!integer(ids[w[3]].type,0)||!ids[w[3]].literal||ids[w[3]].literal>128)return false;n.element=w[2];n.count=ids[w[3]].literal;}
     else if(op==30){if(count<3||count>10)return false;n.count=count-2;for(unsigned i=0;i<n.count;i++){if(!defined(w[i+2])||(ids[w[i+2]].kind!=21&&ids[w[i+2]].kind!=22&&ids[w[i+2]].kind!=23&&ids[w[i+2]].kind!=28))return false;n.members[i]=w[i+2];}}
     else if(op==32){if(count!=4||(w[2]!=1&&w[2]!=2&&w[2]!=3)||!defined(w[3])||(ids[w[3]].kind!=22&&ids[w[3]].kind!=23&&ids[w[3]].kind!=30))return false;n.storage=w[2];n.element=w[3];}
     else if(op==33){if(count!=3||!scalar(w[2],19))return false;n.element=w[2];}
     else if(op==43){if(count!=4||(!scalar(w[1],21)&&!scalar(w[1],22)))return false;n.type=w[1];n.literal=w[3];}
     else if(op==59){if(count!=4||!scalar(w[1],32)||ids[w[1]].storage!=w[3])return false;n.type=w[1];n.storage=w[3];n.root=id;}
     if(op==19||op==21||op==22||op==23||op==32||op==33)for(uint32_t previous=1;previous<bound;previous++){if(previous==id||ids[previous].kind!=op)continue;const auto &other=ids[previous];if(n.literal==other.literal&&n.element==other.element&&n.count==other.count&&n.storage==other.storage)return false;}
     break;}
    case 54:if(count!=5||phase!=6||!entry||!scalar(w[1],19)||w[2]!=entry||w[3]||!scalar(w[4],33)||ids[w[4]].element!=w[1]||!define(w[2],54)||(stage==5?mode!=1:mode!=0)||!globals(facts))return false;function=true;phase=7;break;
    case 248:if(count!=2||label||!define(w[1],248))return false;label=true;break;
    case 65:{if(count<5||count>6||!scalar(w[1],32)||!scalar(w[3],59)||!define(w[2],65))return false;auto &n=ids[w[2]];const auto &base=ids[w[3]];if(base.storage==2||ids[w[1]].storage!=base.storage)return false;uint32_t t=ids[base.type].element,path=0,component=0;
     for(unsigned i=4;i<count;i++){if(!scalar(w[i],43)||!scalar(ids[w[i]].type,21))return false;uint32_t index=ids[w[i]].literal;
      if(ids[t].kind==30){if(w[3]!=block||i!=4||index>1)return false;path=index+1;t=ids[t].members[index];}
      else if(ids[t].kind==23){if(index>=4)return false;component=index;t=ids[t].element;}
      else return false;
     }
     if(!scalar(t,22)||ids[w[1]].element!=t||(w[3]==block&&((path==1&&count!=6)||(path==2&&count!=5))))return false;
     n.type=w[1];n.root=w[3];n.storage=base.storage;n.path=path;n.component=component;break;}
    case 61:{if(count!=4||!scalar(w[1],22)||!scalar(w[3],65)||ids[w[3]].storage!=1||ids[ids[w[3]].type].element!=w[1]||!define(w[2],61))return false;ids[w[2]].type=w[1];auto &pointer=ids[w[3]];ids[pointer.root].read_mask|=1u<<pointer.component;break;}
    case 62:{if(count!=3||!scalar(w[1],65)||ids[w[1]].storage!=3||!defined(w[2])||(ids[w[2]].kind!=43&&ids[w[2]].kind!=61)||ids[w[2]].type!=ids[ids[w[1]].type].element)return false;auto &p=ids[w[1]];if(p.root==block){if(p.path==2){if(ids[w[2]].kind!=43||ids[w[2]].literal!=0x3f800000)return false;facts.point_size_one=1;}else facts.position_components|=1u<<p.component;}else ids[p.root].write_mask|=1u<<p.component;break;}
    case 253:if(count!=1||returned)return false;returned=true;break;
    case 56:if(count!=1||!returned)return false;closed=true;function=false;break;
    default:return false;
   }
   at+=count;
  }
  if(!closed||cap!=1||memory!=1)return false;
  for(uint32_t i=1;i<bound;i++)if(ids[i].kind==59){auto &n=ids[i];if(n.decor&FD_LOCATION){if(n.storage==1)facts.read_components[n.location]=n.read_mask;else facts.written_components[n.location]=n.write_mask;}}
  if(stage==1)return facts.position_components==15&&facts.point_size_one==1&&ids[varying].write_mask==15;
  facts.color_components=ids[color].write_mask;return facts.color_components==15;
 }
};
bool flat_schema_pair(const SatchelVulkanFlatModule &vertex,const SatchelVulkanFlatModule &fragment,SatchelVulkanFlatPair &out){
 FlatParser vp,fp;SatchelVulkanFlatPair result={};result.version=1;result.size=sizeof(result);
 if(!vp.parse(vertex,1,result.vertex)||!fp.parse(fragment,5,result.fragment))return false;
 unsigned count=0;for(unsigned i=0;i<8;i++){
  if(result.vertex.outputs[i]!=result.fragment.inputs[i]||result.fragment.flat_inputs[i]!=result.fragment.inputs[i])return false;
  if(result.vertex.outputs[i]&&++count>1)return false;
 }
 if(count!=1)return false;out=result;return true;
}
}
extern "C" __attribute__((visibility("default"))) SatchelVulkanStatus satchel_vulkan_flat_validate_pair(const SatchelVulkanFlatModule *vs,const SatchelVulkanFlatModule *fs,SatchelVulkanFlatPair *out){
 if(!vs||!fs||!out)return SATCHEL_VULKAN_INVALID;const auto vertex=*vs,fragment=*fs;SatchelVulkanFlatPair result={};if(!flat_schema_pair(vertex,fragment,result))return SATCHEL_VULKAN_UNSUPPORTED;*out=result;return SATCHEL_VULKAN_OK;
}
