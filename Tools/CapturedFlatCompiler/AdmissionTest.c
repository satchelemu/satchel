#include "CapturedFlatAdmission.h"
#include "CapturedFlatV2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdalign.h>
static unsigned checks;
#define CHECK(v) do{++checks;if(!(v)){fprintf(stderr,"admission assertion line%d\n",__LINE__);abort();}}while(0)
#include "GPUOriginalPair.h"
static SatchelCFAdmissionInput active;
static SatchelCFAdmissionInput request(void){return(SatchelCFAdmissionInput){1,sizeof(SatchelCFAdmissionInput),{vs,vs+80,80,56},{fs,fs+80,80,56},65536,1000000000,NULL,NULL};}
static void refused(SatchelCFAdmissionInput *in,int rc){SatchelCFAdmission out,before;memset(&out,0xa5,sizeof(out));before=out;CHECK(satchel_captured_flat_admit(in,&out,sizeof(out))==rc);CHECK(!memcmp(&out,&before,sizeof(out)));}
static int cancel(void *c){(void)c;return 1;}
static int entered;
static int reenter(void *c){(void)c;if(!entered++){refused(&active,SATCHEL_CF_BUSY);SatchelCFInput x={0};SatchelCFPair p,b;memset(&p,0xa5,sizeof(p));b=p;CHECK(satchel_captured_flat_compile(&x,&p,sizeof(p))==SATCHEL_CF_BUSY);CHECK(!memcmp(&p,&b,sizeof(p)));SatchelCFInputV2 v={0};v.version=2;v.size=sizeof(v);v.device_known=8;v.policy_known=16777201;v.requested_source_unused=6;v.input.host.known=v.device_known|v.policy_known;v.input.vertex=active.vertex;v.input.fragment=active.fragment;SatchelCFPairV2 p2,b2;memset(&p2,0xa5,sizeof(p2));b2=p2;CHECK(satchel_captured_flat_compile_v2(&v,&p2,sizeof(p2))==SATCHEL_CF_BUSY);CHECK(!memcmp(&p2,&b2,sizeof(p2)));}return 0;}
static int modified;static int mutate(void*c){(void)c;if(!modified++){vs[0]^=1;fs[88]^=7;}return 0;}
int main(int argc,char **argv){original(0,0);active=request();SatchelCFAdmissionInput bad=active;
 if(argc>1&&!strcmp(argv[1],"deadline")){bad.timeout_ns=1;refused(&bad,SATCHEL_CF_DEADLINE);}else if(argc>1&&!strcmp(argv[1],"cancel")){bad.cancelled=cancel;refused(&bad,SATCHEL_CF_CANCELLED);}else if(argc>1&&!strcmp(argv[1],"invalid")){bad.version=9;refused(&bad,SATCHEL_CF_INVALID);}
 SatchelCFAdmission out={0};CHECK(satchel_captured_flat_admit(&active,&out,sizeof(out))==SATCHEL_CF_OK);CHECK(out.version==1&&out.size==sizeof(out)&&out.profile==1&&out.nvn_stage_mask==3&&out.source_unused_host==6&&out.source_unused_state==8);CHECK(out.vertex.attribute_mask==3&&!out.fragment.attribute_mask&&out.vertex.varying_mask==1&&out.fragment.varying_mask==1);SatchelCFAdmission baseline=out;
 for(unsigned location=0;location<8;location++)for(unsigned rotate=0;rotate<4;rotate++){original(location,rotate);CHECK(satchel_captured_flat_admit(&active,&out,sizeof(out))==SATCHEL_CF_OK);CHECK(out.vertex.varying_mask==(1u<<location)&&out.fragment.varying_mask==(1u<<location)&&out.vertex.attribute_mask==3&&out.source_unused_state==8);}
 original(0,0);bad=active;bad.max_work=1;refused(&bad,SATCHEL_CF_LIMIT);bad=active;bad.timeout_ns=1;refused(&bad,SATCHEL_CF_DEADLINE);bad=active;bad.cancelled=cancel;refused(&bad,SATCHEL_CF_CANCELLED);bad=active;bad.max_work=65537;refused(&bad,SATCHEL_CF_INVALID);
 active.cancelled=reenter;CHECK(satchel_captured_flat_admit(&active,&out,sizeof(out))==SATCHEL_CF_OK&&entered);active.cancelled=mutate;CHECK(satchel_captured_flat_admit(&active,&out,sizeof(out))==SATCHEL_CF_OK&&modified&&!memcmp(&out,&baseline,sizeof(out)));original(0,0);active=request();
 // Valid original resource instruction is forbidden, independently of malformed forms.
 w64(vs+88,0xef9400300007ff00ULL);refused(&active,SATCHEL_CF_UNSUPPORTED);original(0,0);w64(vs+88,0xef90000000000000ULL);refused(&active,SATCHEL_CF_UNSUPPORTED);original(0,0);
 // All non-user attribute offsets, including PositionY/FragCoord and FrontFacing,
 // cannot acquire the source-unused YNegate/driver proof.
 const unsigned offsets[]={0x64,0x68,0x6c,0x70,0x74,0x78,0x7c,0x3fc};
 for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++){w64(fs+88,(r64(fs+88)&~(0x3ffULL<<28))|((uint64_t)offsets[i]<<28));refused(&active,SATCHEL_CF_UNSUPPORTED);original(0,0);}
 for(unsigned bit=0;bit<32;bit++){w32(fs,0x61462u^(1u<<bit));refused(&active,SATCHEL_CF_UNSUPPORTED);original(0,0);}
 for(unsigned n=0;n<=1104;n++){bad=active;bad.vertex.code_size=n;if(n==56)continue;SatchelCFAdmission result,before;memset(&result,0xa5,sizeof(result));before=result;int rc=satchel_captured_flat_admit(&bad,&result,sizeof(result));if(rc!=SATCHEL_CF_OK)CHECK(!memcmp(&result,&before,sizeof(result)));else CHECK(n>56&&n<=1024&&!(n&7)&&result.source_unused_state==8);}
 alignas(8) unsigned char rawIn[sizeof(active)+8],rawOut[sizeof(out)+8],marker[sizeof(rawOut)];memcpy(rawIn+4,&active,sizeof(active));memset(rawOut,0xa5,sizeof(rawOut));memcpy(marker,rawOut,sizeof(rawOut));out=baseline;CHECK(satchel_captured_flat_admit((SatchelCFAdmissionInput*)(rawIn+4),&out,sizeof(out))==SATCHEL_CF_INVALID&&!memcmp(&out,&baseline,sizeof(out)));CHECK(satchel_captured_flat_admit(&active,(SatchelCFAdmission*)(rawOut+4),sizeof(out))==SATCHEL_CF_INVALID&&!memcmp(rawOut,marker,sizeof(rawOut)));CHECK(satchel_captured_flat_admit(NULL,&out,sizeof(out))==SATCHEL_CF_INVALID&&!memcmp(&out,&baseline,sizeof(out)));CHECK(satchel_captured_flat_admit(&active,&out,sizeof(out)-1)==SATCHEL_CF_INVALID&&!memcmp(&out,&baseline,sizeof(out)));
 CHECK(satchel_captured_flat_admit(&active,&out,sizeof(out))==SATCHEL_CF_OK);printf("PASS %u source-only admission assertions; immutable original source, no observed state or compilation claim\n",checks);return 0;}
