/* Original Maxwell programs composed from pinned public decoder bitfields.
 * No captured game content. This client calls the production C export first. */
#include "CapturedFlat.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdatomic.h>
static unsigned checks;
#define CHECK(c) do{++checks;if(!(c)){fprintf(stderr,"check failed line %d: %s\n",__LINE__,#c);exit(2);}}while(0)
static uint8_t vs[1104],fs[1104];
static void w32(uint8_t*p,uint32_t x){memcpy(p,&x,4);}
static void w64(uint8_t*p,uint64_t x){memcpy(p,&x,8);}
static uint64_t r64(const uint8_t*p){uint64_t x;memcpy(&x,p,8);return x;}
static void original(unsigned location,unsigned rotate){
 memset(vs,0,sizeof vs);memset(fs,0,sizeof fs);
 w32(vs,0x60461);w32(fs,0x61462);w32(vs+16,0xff000);w32(fs+16,0xff000);
 w32(vs+24,255);w32(vs+52,0xf0000000);w32(vs+56,15u<<(location*4));
 w32(fs+24+(location/4)*4,0x55u<<((location%4)*8));w32(fs+72,15);
 const uint64_t v[]={0,0xefd9ff800807ff00ULL,0xeff1ff800707ff00ULL,0xefd9ff800907ff04ULL,0,0xeff1ff800807ff04ULL,0xe30000000007000fULL};
 for(unsigned i=0;i<7;i++)w64(vs+80+i*8,v[i]);
 w64(vs+120,r64(vs+120)+((uint64_t)location*16<<20));
 for(unsigned i=0;i<4;i++){unsigned slot=i<3?i+1:5;uint64_t off=0x80+location*16+((i+rotate)%4)*4;w64(fs+80+slot*8,0xe003ff800ff7ff00ULL|(off<<28)|i);}
 w64(fs+128,0xe30000000007000fULL);
}
static SatchelCFInput input(void){
 SatchelCFInput x={0};x.version=1;x.size=sizeof x;x.vertex=(SatchelCFSource){vs,vs+80,80,56};x.fragment=(SatchelCFSource){fs,fs+80,80,56};
 x.state=(SatchelCFState){.known=255,.single_vertex_program=1,.viewport_transform_enabled=1,.depth_zero_to_one=1,.origin_upper_left=1,.alpha_test_always=1,.extra_known=4095,.topology=3,.point_size_bits=0x3f800000,.float_vertex_mask=3,.float_output_mask=1};
 x.host=(SatchelCFHost){.known=SATCHEL_CF_HOST_ALL,.values=SATCHEL_CF_HOST_QUERYHOSTSUPPORTSBGRAFORMAT,.storage_alignment=16,.physical_bgra8_color_attachment=1,.admitted_color_format=1,.current_color_format=1};x.max_work=65536;x.timeout_ns=1000000000;return x;
}
static int calls,cancel_at;
static int cancel(void*p){(void)p;return ++calls>=cancel_at;}
static void failure(SatchelCFInput *x,int expected){SatchelCFPair out,before;memset(&out,0xa5,sizeof out);before=out;int rc=satchel_captured_flat_compile(x,&out,sizeof out);if(rc!=expected)fprintf(stderr,"status %d expected %d\n",rc,expected);CHECK(rc==expected);CHECK(!memcmp(&out,&before,sizeof out));}
static SatchelCFPair baseline;
static void success(SatchelCFInput*x,SatchelCFPair*out){memset(out,0xa5,sizeof *out);int rc=satchel_captured_flat_compile(x,out,sizeof *out);if(rc)fprintf(stderr,"compile status %d\n",rc);CHECK(rc==0);CHECK(out->version==1&&out->size==sizeof *out&&out->dynamic_code_features==0);CHECK(out->vertex.size==sizeof(SatchelCFModule)&&out->fragment.size==sizeof(SatchelCFModule));CHECK(out->vertex.word_count>5&&out->vertex.word_count<=4096&&out->fragment.word_count<=4096);CHECK(out->vertex.words[0]==0x07230203&&out->fragment.words[0]==0x07230203);CHECK(out->vertex.words[3]<=256&&out->fragment.words[3]<=256);CHECK(out->vertex_facts.declared_uniforms==1&&out->fragment_facts.declared_uniforms==1);CHECK(!out->vertex_facts.active_uniforms&&!out->fragment_facts.active_uniforms);for(unsigned i=out->vertex.word_count;i<4096;i++)CHECK(out->vertex.words[i]==0);for(unsigned i=out->fragment.word_count;i<4096;i++)CHECK(out->fragment.words[i]==0);}
static int nested_done;
static int reenter(void*p){if(!nested_done++){SatchelCFInput nested=input();failure(&nested,SATCHEL_CF_BUSY);}return 0;}
static int mutate_done;
static int mutate_source(void*p){(void)p;if(!mutate_done++){vs[0]^=1;fs[88]^=7;}return 0;}
static void save(const char*directory,unsigned location,unsigned rotate,const SatchelCFPair*out){char path[1024];for(int s=0;s<2;s++){snprintf(path,sizeof path,"%s/original-%u-%u-%s.spv",directory,location,rotate,s?"fragment":"vertex");FILE*f=fopen(path,"wb");CHECK(f!=NULL);const SatchelCFModule*m=s?&out->fragment:&out->vertex;CHECK(fwrite(m->words,4,m->word_count,f)==m->word_count);CHECK(fclose(f)==0);}}
static atomic_uint race_start,race_ok,race_busy,race_bad;
static void *racer(void *unused){(void)unused;while(!atomic_load_explicit(&race_start,memory_order_acquire)){}SatchelCFInput x=input();for(int n=0;n<40;n++){SatchelCFPair out,before;memset(&out,0x5a,sizeof out);before=out;int rc=satchel_captured_flat_compile(&x,&out,sizeof out);if(rc==0)atomic_fetch_add(&race_ok,1);else if(rc==SATCHEL_CF_BUSY&&!memcmp(&out,&before,sizeof out))atomic_fetch_add(&race_busy,1);else atomic_fetch_add(&race_bad,1);}return NULL;}
static void padded(unsigned count){uint64_t ops[65];unsigned n=0;for(unsigned off=80;off<136;off+=8)if(((off-80)&31)!=0){uint64_t op=r64(vs+off);if(op!=0xe30000000007000fULL)ops[n++]=op;}while(n<count-1)ops[n++]=0x010000000007f064ULL;ops[n++]=0xe30000000007000fULL;memset(vs+80,0,sizeof vs-80);unsigned at=80;for(unsigned i=0;i<n;i++){if(((at-80)&31)==0)at+=8;w64(vs+at,ops[i]);at+=8;}}
int main(int argc,char**argv){
 CHECK(argc==3);original(0,0);SatchelCFInput x=input();SatchelCFPair out;
 /* No managed warmup or testing export precedes this production operation. */
 if(!strcmp(argv[1],"cold-cancel")){calls=0;cancel_at=1;x.cancelled=cancel;failure(&x,SATCHEL_CF_CANCELLED);x.cancelled=NULL;}
 else if(!strcmp(argv[1],"cold-limit")){x.max_work=1;failure(&x,SATCHEL_CF_LIMIT);x.max_work=65536;}
 else if(!strcmp(argv[1],"cold-deadline")){x.timeout_ns=1;failure(&x,SATCHEL_CF_DEADLINE);x.timeout_ns=1000000000;}
 else CHECK(!strcmp(argv[1],"cold-success"));
 success(&x,&baseline);CHECK(baseline.vertex_facts.attribute_mask==3&&baseline.vertex_facts.varying_mask==1&&baseline.fragment_facts.varying_mask==1);CHECK(baseline.vertex_facts.semantic_ops==5&&baseline.fragment_facts.semantic_ops==5);
 for(unsigned l=0;l<8;l++)for(unsigned r=0;r<4;r++){original(l,r);x=input();success(&x,&out);CHECK(out.vertex_facts.varying_mask==(1u<<l)&&out.fragment_facts.varying_mask==(1u<<l));if(l)CHECK(memcmp(out.vertex.words,baseline.vertex.words,sizeof out.vertex.words));if(l||r)CHECK(memcmp(out.fragment.words,baseline.fragment.words,sizeof out.fragment.words));save(argv[2],l,r,&out);}
 original(0,0);x=input();for(unsigned b=0;b<24;b++){x.host.known=SATCHEL_CF_HOST_ALL^(1ULL<<b);failure(&x,SATCHEL_CF_UNSUPPORTED);}x=input();
 for(unsigned b=0;b<8;b++){x.state.known=255^(1u<<b);failure(&x,SATCHEL_CF_UNSUPPORTED);}x=input();
 for(unsigned b=0;b<12;b++){x.state.extra_known=4095^(1u<<b);failure(&x,SATCHEL_CF_UNSUPPORTED);}x=input();
 x.state.y_negate=1;failure(&x,SATCHEL_CF_UNSUPPORTED);x=input();x.state.float_vertex_mask=1;failure(&x,SATCHEL_CF_UNSUPPORTED);x=input();
 x.host.physical_bgra8_color_attachment=0;failure(&x,SATCHEL_CF_UNSUPPORTED);x=input();x.host.current_color_format=2;failure(&x,SATCHEL_CF_UNSUPPORTED);x=input();x.host.values=0;failure(&x,SATCHEL_CF_UNSUPPORTED);x=input();
 for(unsigned size=0;size<80;size++){x.vertex.sph_size=size;failure(&x,SATCHEL_CF_INVALID);}x=input();x.vertex.code_size=1025;failure(&x,SATCHEL_CF_INVALID);x=input();x.vertex.code=NULL;failure(&x,SATCHEL_CF_INVALID);x=input();x.vertex.code=(const void*)(UINTPTR_MAX-8);failure(&x,SATCHEL_CF_INVALID);x=input();
 x.version=2;failure(&x,SATCHEL_CF_INVALID);x=input();x.reserved[3]=1;failure(&x,SATCHEL_CF_INVALID);x=input();x.max_work=65537;failure(&x,SATCHEL_CF_INVALID);x=input();x.timeout_ns=0;failure(&x,SATCHEL_CF_INVALID);x=input();
 for(unsigned bit=0;bit<32;bit++){original(0,0);vs[bit/8]^=1u<<(bit%8);failure(&x,SATCHEL_CF_UNSUPPORTED);}original(0,0);
 for(unsigned n=1;n<baseline.work_used;n=n*2){x.max_work=n;failure(&x,SATCHEL_CF_LIMIT);x=input();success(&x,&out);CHECK(!memcmp(&out.vertex,&baseline.vertex,sizeof out.vertex));CHECK(!memcmp(&out.fragment,&baseline.fragment,sizeof out.fragment));}
 for(int n=1;n<=2000;n=n*2){calls=0;cancel_at=n;x.cancelled=cancel;failure(&x,SATCHEL_CF_CANCELLED);x.cancelled=NULL;success(&x,&out);}x=input();
 original(0,0);padded(64);x=input();x.vertex.code_size=((64+2)/3+64)*8;success(&x,&out);CHECK(out.vertex_facts.semantic_ops==64);original(0,0);padded(65);x=input();x.vertex.code_size=((65+2)/3+65)*8;failure(&x,SATCHEL_CF_UNSUPPORTED);original(0,0);x=input();
 for(unsigned n=baseline.work_used/4;n<baseline.work_used;n+=baseline.work_used/4){calls=0;cancel_at=(int)n;x.cancelled=cancel;failure(&x,SATCHEL_CF_CANCELLED);x.cancelled=NULL;success(&x,&out);}
 x.cancelled=reenter;success(&x,&out);CHECK(nested_done>0);x=input();
 /* Adversarial test-only source mutation proves no live source reads after copy;
  * production callers must obey the pure callback contract. */
 x.cancelled=mutate_source;success(&x,&out);CHECK(!memcmp(&out.vertex,&baseline.vertex,sizeof out.vertex));CHECK(!memcmp(&out.fragment,&baseline.fragment,sizeof out.fragment));original(0,0);x=input();
 memset(&out,0xa5,sizeof out);SatchelCFPair before=out;x.vertex.code=(const uint8_t*)&out;CHECK(satchel_captured_flat_compile(&x,&out,sizeof out)==SATCHEL_CF_INVALID);CHECK(!memcmp(&out,&before,sizeof out));x=input();CHECK(satchel_captured_flat_compile(&x,&out,sizeof out-1)==SATCHEL_CF_INVALID);CHECK(!memcmp(&out,&before,sizeof out));
 pthread_t a,b;CHECK(pthread_create(&a,NULL,racer,NULL)==0);CHECK(pthread_create(&b,NULL,racer,NULL)==0);atomic_store_explicit(&race_start,1,memory_order_release);CHECK(pthread_join(a,NULL)==0);CHECK(pthread_join(b,NULL)==0);CHECK(atomic_load(&race_bad)==0);CHECK(atomic_load(&race_ok)>0&&atomic_load(&race_busy)>0);CHECK(atomic_load(&race_ok)+atomic_load(&race_busy)==80);
 printf("%u original C ABI assertions passed; mode=%s, work=%u, SPIR-V=%u/%u words.\n",checks,argv[1],baseline.work_used,baseline.vertex.word_count,baseline.fragment.word_count);return 0;
}
