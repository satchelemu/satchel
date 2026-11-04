#include "ScalarCompiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(v) do{++checks;if(!(v)){fprintf(stderr,"admission assertion %u line%d\n",checks,__LINE__);exit(1);}}while(0)
static uint64_t ldc(unsigned rd,unsigned bytes,unsigned slot,unsigned off){return UINT64_C(0xef90000000000000)|((uint64_t)(bytes==8?5:4)<<48)|((uint64_t)slot<<36)|((uint64_t)off<<20)|(UINT64_C(7)<<16)|(UINT64_C(255)<<8)|rd;}
static uint64_t add(unsigned d,unsigned a,unsigned b){return UINT64_C(0x5c10000000000000)|((uint64_t)b<<20)|(UINT64_C(7)<<16)|((uint64_t)a<<8)|d;}
static uint64_t store(unsigned v,unsigned p,unsigned off){return UINT64_C(0xeed8000000000000)|(UINT64_C(4)<<48)|(UINT64_C(1)<<45)|((uint64_t)off<<20)|(UINT64_C(7)<<16)|((uint64_t)p<<8)|v;}
static size_t pack(uint64_t *out,const uint64_t *ops,size_t count){size_t n=0;memset(out,0,512);for(size_t i=0;i<count;i++){if(n%4==0)out[n++]=UINT64_C(0x12345678);out[n++]=ops[i];}while(n%4)out[n++]=0;return n*8;}
static SatchelScalarProgramInput admission(uint64_t *code,size_t n){return (SatchelScalarProgramInput){.version=1,.size=sizeof(SatchelScalarProgramInput),.known=SATCHEL_SCALAR_KNOWN_WORKGROUP,.local_x=1,.local_y=1,.local_z=1,.local_bytes=UINT32_MAX,.shared_bytes=UINT32_MAX,.code=(uint8_t*)code,.code_size=n};}
static SatchelScalarInput dispatch(uint32_t *cb){return (SatchelScalarInput){.version=1,.size=sizeof(SatchelScalarInput),.known=SATCHEL_SCALAR_KNOWN_BINDINGS|SATCHEL_SCALAR_KNOWN_CAPS,.group_x=1,.group_y=1,.group_z=1,.shader_enabled=1,.max_uniform_range=65536,.max_storage_range=1048576,.max_uniform_descriptors=1,.max_storage_descriptors=1,.max_stage_resources=2,.uniform_alignment=256,.storage_alignment=256,.cb_slot=3,.uniform_binding=1,.storage_binding=2,.storage_cb_disjoint=1,.code=(void*)(uintptr_t)1,.code_size=SIZE_MAX,.constant_buffer=(uint8_t*)cb,.constant_buffer_size=64,.storage_address=0x10000,.storage_size=64};}
static void reject_admission(const SatchelScalarProgramInput *in){SatchelScalarProgram out;memset(&out,0xa5,sizeof(out));CHECK(satchel_scalar_admit(in,&out)!=SATCHEL_SCALAR_OK);for(size_t i=0;i<sizeof(out);i++)CHECK(((uint8_t*)&out)[i]==0xa5);}
static void reject_compile(const SatchelScalarProgram *p,const SatchelScalarInput *in){uint32_t out[4096];SatchelScalarReflection r;memset(out,0xa5,sizeof(out));memset(&r,0xa5,sizeof(r));CHECK(satchel_scalar_compile_program(p,in,out,4096,&r)!=SATCHEL_SCALAR_OK);for(size_t i=0;i<sizeof(out);i++)CHECK(((uint8_t*)out)[i]==0xa5);for(size_t i=0;i<sizeof(r);i++)CHECK(((uint8_t*)&r)[i]==0xa5);}
int main(void){uint64_t code[64];const uint64_t exitop=UINT64_C(0xe30000000007000f);uint64_t ops[]={ldc(10,8,3,8),ldc(20,4,3,24),ldc(21,4,3,28),add(22,20,21),store(22,10,12),exitop};
 SatchelScalarProgramInput in=admission(code,pack(code,ops,6));SatchelScalarProgram p;CHECK(satchel_scalar_admit(&in,&p)==SATCHEL_SCALAR_OK);
 CHECK(p.version==1 && p.size==sizeof(p) && p.semantic_ops==6 && p.op_count==6 && p.known==1 && p.local_bytes==UINT32_MAX && p.shared_bytes==UINT32_MAX);
 CHECK(p.requirements.cb_slot==3 && p.requirements.uniform_declared_bytes==65536 && p.requirements.uniform_required_bytes==32 && p.requirements.storage_required_bytes==16 && p.requirements.pointer_cb_offset==8 && p.requirements.store_count==1 && p.requirements.stores[0].offset==12 && p.requirements.stores[0].size==4 && p.requirements.unused_memory_mask==3);
 /* No CB exists at admission. Real dispatch supplied later: zeros cannot fake a
  * bound pointer and reject, while the immutable admitted program remains valid. */
 uint32_t cb[16]={0};SatchelScalarInput d=dispatch(cb);reject_compile(&p,&d);cb[2]=0x10000;cb[6]=UINT32_MAX;cb[7]=7;
 uint32_t out[4096],direct[4096];SatchelScalarReflection r,dr;CHECK(satchel_scalar_compile_program(&p,&d,out,4096,&r)==0);
 SatchelScalarInput real=d;real.known=7;real.code=p.code;real.code_size=p.code_size;real.local_x=real.local_y=real.local_z=1;
 CHECK(satchel_scalar_compile(&real,direct,4096,&dr)==0 && !memcmp(out,direct,r.spirv_words*4) && !memcmp(&r,&dr,sizeof(r)));
 /* Caller input/control can disappear after acceptance; program owns a copy. */
 memset(code,0,sizeof(code));CHECK(satchel_scalar_compile_program(&p,&d,direct,4096,&dr)==0 && !memcmp(out,direct,r.spirv_words*4));
 cb[6]=0x7f800001;cb[7]=1;CHECK(satchel_scalar_compile_program(&p,&d,direct,4096,&dr)==0 && !memcmp(out,direct,r.spirv_words*4));
 SatchelScalarInput saved=d;
 #define BAD(field,value) do{d=saved;d.field=value;reject_compile(&p,&d);}while(0)
 BAD(known,0);BAD(known,2);BAD(known,39);BAD(constant_buffer,NULL);BAD(constant_buffer_size,28);BAD(cb_slot,4);BAD(storage_size,12);BAD(storage_address,0x20000);BAD(storage_cb_disjoint,0);BAD(group_x,2);BAD(shader_enabled,0);BAD(max_uniform_range,65535);BAD(max_storage_descriptors,0);BAD(storage_alignment,3);d=saved;
 /* All derived IR/requirements bytes are checked, including unused slots. */
 const size_t begin=offsetof(SatchelScalarProgram,semantic_ops),end=offsetof(SatchelScalarProgram,code);
 for(size_t i=begin;i<end;i++){if(i>=offsetof(SatchelScalarProgram,code_size) && i<offsetof(SatchelScalarProgram,code_size)+sizeof(p.code_size))continue;SatchelScalarProgram bad=p;((uint8_t*)&bad)[i]^=1;reject_compile(&bad,&d);}
 SatchelScalarProgram bad=p;bad.size--;reject_compile(&bad,&d);bad=p;bad.version++;reject_compile(&bad,&d);bad=p;bad.code[8]^=8;reject_compile(&bad,&d);
 bad=p;bad.code_size=513;reject_compile(&bad,&d);bad=p;bad.code_size=63;reject_compile(&bad,&d);
 /* Source length is not a derived IR field: extra all-zero trailing words may
  * form another valid canonical program with identical semantics. Check that
  * distinction instead of incorrectly demanding every length mutation fail. */
 bad=p;bad.code_size+=8;CHECK(satchel_scalar_compile_program(&bad,&d,direct,4096,&dr)==0 && !memcmp(out,direct,r.spirv_words*4));
 /* Deferred CB slot is derived from code, not an input/default. */
 for(unsigned slot=0;slot<32;slot++){ops[0]=ldc(10,8,slot,8);ops[1]=ldc(20,4,slot,24);ops[2]=ldc(21,4,slot,28);in=admission(code,pack(code,ops,6));if(slot<=17){CHECK(satchel_scalar_admit(&in,&bad)==0 && bad.requirements.cb_slot==slot);}else reject_admission(&in);}
 ops[0]=ldc(10,8,3,8);ops[1]=ldc(20,4,3,24);ops[2]=ldc(21,4,4,28);in=admission(code,pack(code,ops,6));reject_admission(&in);ops[2]=ldc(21,4,3,28);
 in=admission(code,pack(code,ops,6));SatchelScalarProgramInput good=in;
 #define BAD_PROGRAM(field,value) do{in=good;in.field=value;reject_admission(&in);}while(0)
 BAD_PROGRAM(version,2);BAD_PROGRAM(size,0);BAD_PROGRAM(known,0);BAD_PROGRAM(known,7);BAD_PROGRAM(known,33);BAD_PROGRAM(local_x,2);BAD_PROGRAM(local_y,0);BAD_PROGRAM(code,NULL);BAD_PROGRAM(code_size,0);BAD_PROGRAM(code_size,513);BAD_PROGRAM(code_size,63);
 in=good;in.known=25;CHECK(satchel_scalar_admit(&in,&bad)==0 && bad.known==25 && bad.local_bytes==UINT32_MAX && bad.requirements.unused_memory_mask==3);
 /* Admission knows extents, even when no resource could yet be read. */
 uint64_t edge[]={ldc(10,8,3,32764),ldc(20,4,3,0),store(20,10,1048572),exitop};in=admission(code,pack(code,edge,4));CHECK(satchel_scalar_admit(&in,&bad)==0 && bad.requirements.uniform_required_bytes==32772 && bad.requirements.storage_required_bytes==1048576 && bad.requirements.pointer_cb_offset==32764);
 edge[2]=store(20,10,1048576);in.code_size=pack(code,edge,4);reject_admission(&in);edge[2]=store(20,10,2);in.code_size=pack(code,edge,4);reject_admission(&in);edge[2]=store(20,10,0);edge[0]=ldc(10,8,3,32768);in.code_size=pack(code,edge,4);reject_admission(&in);
 /* Unsupported modifier mutations fail admission without touching output. */
 in=admission(code,pack(code,ops,6));const uint64_t fields[]={UINT64_C(0xff)|(UINT64_C(0xffff)<<20)|(UINT64_C(31)<<36)|(UINT64_C(1)<<48),UINT64_C(0xffff)|(UINT64_C(255)<<20),UINT64_C(0xffff)|(UINT64_C(0xffffff)<<20),0};const unsigned pos[]={1,5,6,7};
 for(unsigned family=0;family<4;family++)for(unsigned bit=0;bit<64;bit++)if(!(fields[family]&(UINT64_C(1)<<bit))){uint64_t original=code[pos[family]];code[pos[family]]^=UINT64_C(1)<<bit;reject_admission(&in);code[pos[family]]=original;}
 /* Multiple pointer pairs cannot be hidden until dispatch; register clobbers
  * and absent definitions also fail before any binding exists. */
 uint64_t pairs[]={ldc(10,8,3,8),store(255,10,0),ldc(20,8,3,16),store(255,20,4),exitop};in=admission(code,pack(code,pairs,5));reject_admission(&in);
 uint64_t clobber[]={ldc(10,8,3,8),add(11,11,255),store(255,10,0),exitop};in=admission(code,pack(code,clobber,4));reject_admission(&in);
 clobber[1]=add(21,40,255);in.code_size=pack(code,clobber,4);reject_admission(&in);
 /* Admission ceilings apply before resources exist, independently of compile. */
 uint64_t many[40];unsigned n=0;many[n++]=ldc(10,8,3,8);many[n++]=ldc(20,4,3,24);
 for(unsigned i=0;i<28;i++)many[n++]=add(20,20,255);many[n++]=store(20,10,0);many[n++]=exitop;
 in=admission(code,pack(code,many,n));CHECK(satchel_scalar_admit(&in,&bad)==0 && bad.semantic_ops==32);
 many[n-1]=add(20,20,255);many[n++]=exitop;in.code_size=pack(code,many,n);reject_admission(&in);
 n=0;many[n++]=ldc(10,8,3,8);for(unsigned i=0;i<16;i++)many[n++]=store(255,10,i*4);many[n++]=exitop;
 in=admission(code,pack(code,many,n));CHECK(satchel_scalar_admit(&in,&bad)==0 && bad.requirements.store_count==16);
 many[n-1]=store(255,10,0);many[n++]=exitop;in.code_size=pack(code,many,n);reject_admission(&in);
 CHECK(satchel_scalar_admit(NULL,&bad)==SATCHEL_SCALAR_INVALID && satchel_scalar_admit(&in,NULL)==SATCHEL_SCALAR_INVALID);
 printf("Scalar code admission: %u ASan/UBSan assertions; no CB or default resources at admission; dispatch obligations remain checked.\n",checks);return 0;}
