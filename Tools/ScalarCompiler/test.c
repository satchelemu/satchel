#include "ScalarCompiler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
static unsigned checks;
#define CHECK(v) do{++checks;if(!(v)){fprintf(stderr,"scalar assertion %u line%d\n",checks,__LINE__);exit(1);}}while(0)
static uint64_t load(unsigned rd,unsigned bytes,unsigned off){return UINT64_C(0xef90000000000000)|((uint64_t)(bytes==8?5:4)<<48)|(UINT64_C(3)<<36)|((uint64_t)off<<20)|(UINT64_C(7)<<16)|(UINT64_C(255)<<8)|rd;}
static uint64_t add(unsigned d,unsigned a,unsigned b){return UINT64_C(0x5c10000000000000)|((uint64_t)b<<20)|(UINT64_C(7)<<16)|((uint64_t)a<<8)|d;}
static uint64_t store(unsigned v,unsigned p,unsigned off){return UINT64_C(0xeed8000000000000)|(UINT64_C(4)<<48)|(UINT64_C(1)<<45)|((uint64_t)off<<20)|(UINT64_C(7)<<16)|((uint64_t)p<<8)|v;}
static size_t pack(uint64_t *out,const uint64_t *ops,size_t count){size_t n=0;for(size_t i=0;i<count;i++){if(n%4==0)out[n++]=0;out[n++]=ops[i];}while(n%4)out[n++]=0;return n*8;}
static SatchelScalarInput input(uint64_t *code,size_t size,uint32_t *cb){return (SatchelScalarInput){.version=1,.size=sizeof(SatchelScalarInput),.known=7,.local_x=1,.local_y=1,.local_z=1,.group_x=1,.group_y=1,.group_z=1,.shader_enabled=1,.max_uniform_range=65536,.max_storage_range=1048576,.max_uniform_descriptors=1,.max_storage_descriptors=1,.max_stage_resources=2,.uniform_alignment=256,.storage_alignment=256,.cb_slot=3,.uniform_binding=1,.storage_binding=2,.storage_cb_disjoint=1,.code=(const uint8_t*)code,.code_size=size,.constant_buffer=(const uint8_t*)cb,.constant_buffer_size=64,.storage_address=0x10000,.storage_size=64};}
static void reject(SatchelScalarInput *in){uint32_t out[4096];SatchelScalarReflection r;memset(out,0xa5,sizeof(out));memset(&r,0xa5,sizeof(r));CHECK(satchel_scalar_compile(in,out,4096,&r)!=SATCHEL_SCALAR_OK);for(size_t i=0;i<4096;i++)CHECK(out[i]==0xa5a5a5a5);for(size_t i=0;i<sizeof(r);i++)CHECK(((uint8_t*)&r)[i]==0xa5);}
static void writefile(const char *dir,unsigned n,const char *ext,const void *p,size_t bytes){char path[512];CHECK(snprintf(path,sizeof(path),"%s/program%u.%s",dir,n,ext)>0);FILE*f=fopen(path,"wb");CHECK(f);CHECK(fwrite(p,1,bytes,f)==bytes);CHECK(fclose(f)==0);}
int main(int argc,char**argv){CHECK(argc==2);uint64_t code[64]={0};uint32_t cb[16]={0};cb[2]=0x10000;cb[6]=UINT32_MAX;cb[7]=7;cb[8]=0x80000001;
 const uint64_t exitop=UINT64_C(0xe30000000007000f);
 uint64_t programs[4][16]={
  {load(10,8,8),load(20,4,24),load(21,4,28),add(22,20,21),store(22,10,12),exitop},
  {load(4,8,8),load(1,4,24),load(2,4,28),load(3,4,32),add(6,1,2),add(7,6,3),store(7,4,0),store(1,4,4),store(6,4,16),exitop},
  {load(100,8,8),load(8,4,24),add(9,8,255),store(9,100,60),store(255,100,0),store(8,100,0),exitop},
  {load(0,8,8),load(42,4,32),store(42,0,28),exitop}
 };
 const unsigned counts[]={6,10,7,4};uint32_t baseline[4096];SatchelScalarReflection br={0};
 for(unsigned p=0;p<4;p++){size_t bytes=pack(code,programs[p],counts[p]);SatchelScalarInput in=input(code,bytes,cb);uint32_t out[4096];SatchelScalarReflection r;
  CHECK(satchel_scalar_compile(&in,out,4096,&r)==0);CHECK(r.store_count==(p==1 || p==2?3:1));CHECK(r.pointer_cb_offset==8 && r.expected_storage_address==0x10000 && r.uniform_declared_bytes==65536);CHECK(r.spirv_words>64 && r.spirv_words<4096);
  uint32_t expected[16];for(unsigned i=0;i<16;i++)expected[i]=0xa5a5a5a5;
  /* Independent native C arithmetic/assignment oracle for each original program. */
  switch(p){case 0:expected[3]=cb[6]+cb[7];break;case 1:expected[0]=(cb[6]+cb[7])+cb[8];expected[1]=cb[6];expected[4]=cb[6]+cb[7];break;case 2:expected[15]=cb[6]+0u;expected[0]=0;expected[0]=cb[6];break;case 3:expected[7]=cb[8];break;}
  writefile(argv[1],p,"spv",out,r.spirv_words*4);writefile(argv[1],p,"cb",cb,64);writefile(argv[1],p,"expected",expected,64);writefile(argv[1],p,"maxwell",code,bytes);
  if(!p){br=r;memcpy(baseline,out,r.spirv_words*4);}
 }
 size_t bytes=pack(code,programs[0],counts[0]);SatchelScalarInput in=input(code,bytes,cb),saved=in;
 uint32_t out[4096];SatchelScalarReflection r;cb[6]=0x7f800001;cb[7]=0x00000001;CHECK(satchel_scalar_compile(&in,out,4096,&r)==0);CHECK(r.spirv_words==br.spirv_words && !memcmp(out,baseline,r.spirv_words*4));
 #define BAD(field,value) do{in=saved;in.field=value;reject(&in);}while(0)
 BAD(known,0);BAD(known,3);BAD(known,39);BAD(storage_cb_disjoint,0);BAD(local_x,2);BAD(group_x,2);BAD(shader_enabled,0);BAD(max_uniform_range,65535);BAD(max_storage_range,63);BAD(max_stage_resources,1);BAD(uniform_alignment,3);BAD(storage_alignment,0);BAD(storage_address,0x10004);BAD(storage_size,3);BAD(storage_size,1048580);BAD(constant_buffer_size,12);BAD(code_size,0);BAD(code_size,513);BAD(code_size,63);BAD(cb_slot,4);BAD(storage_binding,1);in=saved;
 cb[2]^=256;reject(&in);cb[2]^=256;
 uint64_t old=code[1];code[1]^=UINT64_C(1)<<19;reject(&in);code[1]=old; /* inverted predicate */
 old=code[5];code[5]^=UINT64_C(1)<<47;reject(&in);code[5]=old; /* IADD CC */
 old=code[6];code[6]=store(22,10,64);reject(&in);code[6]=store(22,10,2);reject(&in);code[6]=store(22,11,0);reject(&in);code[6]=old;
 old=code[7];code[7]=0;reject(&in);code[7]=old;
 old=code[3];code[3]=load(21,4,63);reject(&in);code[3]=old;
 memset(out,0xa5,sizeof(out));memset(&r,0xa5,sizeof(r));CHECK(satchel_scalar_compile(&in,out,1,&r)==SATCHEL_SCALAR_LIMIT);CHECK(out[0]==0xa5a5a5a5 && r.version==0xa5a5a5a5);
 /* output/reflection overlap and invalid register sources reject atomically. */
 CHECK(satchel_scalar_compile(&in,out,4096,(SatchelScalarReflection*)out)==SATCHEL_SCALAR_INVALID);CHECK(out[0]==0xa5a5a5a5);
 old=code[5];code[5]=add(22,99,21);reject(&in);code[5]=old;
 /* Different accepted registers/offsets produce genuinely different modules. */
 code[6]=store(22,10,8);CHECK(satchel_scalar_compile(&in,out,4096,&r)==0);CHECK(r.stores[0].offset==8 && memcmp(out,baseline,r.spirv_words*4));
 /* Unknown-but-proven-unused sizes do not fabricate known values. */
 in=saved;in.local_bytes=UINT32_MAX;in.shared_bytes=UINT32_MAX;
 CHECK(satchel_scalar_compile(&in,out,4096,&r)==0 && r.unused_memory_mask==3);
 in.known=31;CHECK(satchel_scalar_compile(&in,out,4096,&r)==0 && r.unused_memory_mask==3);
 in=saved;code[6]=old; /* restore original store after prior varying-offset test */
 /* Semantic and store ceilings are actual hard bounds. */
 uint64_t longops[40];unsigned n=0;longops[n++]=load(10,8,8);longops[n++]=load(20,4,24);
 for(unsigned i=0;i<28;i++)longops[n++]=add(20,20,255);
 longops[n++]=store(20,10,0);longops[n++]=exitop;CHECK(n==32);
 in.code_size=pack(code,longops,n);CHECK(satchel_scalar_compile(&in,out,4096,&r)==0 && r.semantic_ops==32);
 longops[n-1]=add(20,20,255);longops[n++]=exitop;in.code_size=pack(code,longops,n);reject(&in);
 n=0;longops[n++]=load(10,8,8);longops[n++]=load(20,4,24);for(unsigned i=0;i<16;i++)longops[n++]=store(20,10,i*4);longops[n++]=exitop;
 in.code_size=pack(code,longops,n);CHECK(satchel_scalar_compile(&in,out,4096,&r)==0 && r.store_count==16 && r.storage_required_bytes==64);
 longops[n-1]=store(20,10,0);longops[n++]=exitop;in.code_size=pack(code,longops,n);reject(&in);
 /* Pointer provenance must survive BOTH registers; same-pair reload restores it. */
 uint64_t clobber[]={load(10,8,8),load(20,4,24),add(11,11,255),store(20,10,0),exitop};
 in=saved;in.code_size=pack(code,clobber,5);reject(&in);
 clobber[2]=load(10,4,8);in.code_size=pack(code,clobber,5);reject(&in);
 uint64_t reload[]={load(10,8,8),load(20,4,24),add(11,11,255),load(10,8,8),store(20,10,0),exitop};
 in.code_size=pack(code,reload,6);CHECK(satchel_scalar_compile(&in,out,4096,&r)==0);
 /* Signed16 immediate's maximum aligned start may span into the next word. */
 uint32_t largecb[16384]={0};largecb[8191]=0x10000;largecb[0]=0x12345678;
 uint64_t endpoint[]={load(10,8,32764),load(20,4,0),store(20,10,0),exitop};
 in.code_size=pack(code,endpoint,4);in.constant_buffer=(uint8_t*)largecb;in.constant_buffer_size=32772;
 CHECK(satchel_scalar_compile(&in,out,4096,&r)==0 && r.uniform_required_bytes==32772 && r.pointer_cb_offset==32764);
 in.constant_buffer_size=32768;reject(&in);in.constant_buffer_size=65536;code[1]=load(10,8,32768);reject(&in);
 /* End at40-bit limit is valid; any byte past is rejected without output. */
 in=saved;in.code_size=pack(code,programs[0],counts[0]);in.storage_address=(UINT64_C(1)<<40)-256;in.storage_size=256;
 cb[2]=(uint32_t)in.storage_address;cb[3]=(uint32_t)(in.storage_address>>32);
 CHECK(satchel_scalar_compile(&in,out,4096,&r)==0);in.storage_size=260;reject(&in);cb[2]=0x10000;cb[3]=0;
 in=saved;CHECK(satchel_scalar_compile(&in,out,4096,&r)==0);size_t exact=r.spirv_words;CHECK(satchel_scalar_compile(&in,out,exact,&r)==0);
 memset(out,0xa5,sizeof(out));memset(&r,0xa5,sizeof(r));CHECK(satchel_scalar_compile(&in,out,exact-1,&r)==SATCHEL_SCALAR_LIMIT && r.version==0xa5a5a5a5 && out[0]==0xa5a5a5a5);
 /* Every excluded encoding bit rejects; allowed register/immediate bits are separately tested above. */
 const uint64_t variable[]={UINT64_C(0xff)|(UINT64_C(0xffff)<<20)|(UINT64_C(31)<<36)|(UINT64_C(1)<<48),UINT64_C(0xffff)|(UINT64_C(255)<<20),UINT64_C(0xffff)|(UINT64_C(0xffffff)<<20),0};
 const unsigned pos[]={1,5,6,7};
 for(unsigned family=0;family<4;family++)for(unsigned bit=0;bit<64;bit++)if(!(variable[family]&(UINT64_C(1)<<bit))){uint64_t originalword=code[pos[family]];code[pos[family]]^=UINT64_C(1)<<bit;reject(&in);code[pos[family]]=originalword;}
 printf("Bounded scalar compiler: %u ASan/UBSan assertions;4 independently specified original programs. GPU validation pending.\n",checks);return 0;
}
