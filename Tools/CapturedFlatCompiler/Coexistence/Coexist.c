/* Original public-bitfield shader fixtures. No game inputs or private runtime
 * object handles cross the C ABI. Each library remains loaded for process life. */
#define _DARWIN_C_SOURCE
#include "SatchelShaderFixture.h"
#include "SatchelShaderProbe.h"
#include "CapturedFlat.h"
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
static atomic_uint checks;
#define CHECK(c) do {atomic_fetch_add(&checks,1);if(!(c)){fprintf(stderr,"Coexistence assertion line %d: %s\n",__LINE__,#c);abort();}}while(0)
typedef int (*Old)(const SatchelShaderFixtureInput*,uint8_t*,uint32_t,SatchelShaderFixtureReflection*);
typedef int (*New)(const SatchelCFInput*,SatchelCFPair*,uint32_t);
typedef int (*Feature)(void);
typedef int (*Probe)(const uint8_t*,int,uint8_t*,int);
static Old old_compile;static New new_compile;static Feature old_features;static Probe old_probe;
typedef struct {uint64_t code[8];uint32_t cb[8];SatchelShaderFixtureInput input;} OldSource;
typedef struct {uint8_t vertex[136],fragment[136];SatchelCFInput input;} NewSource;
typedef struct {uint8_t bytes[16384];SatchelShaderFixtureReflection info;} OldOutput;
static OldOutput old_reference;static SatchelCFPair new_reference;
static uint64_t ldc(unsigned rd,unsigned bytes,unsigned offset){return UINT64_C(0xef90000000000000)|((uint64_t)(bytes==8?5:4)<<48)|(UINT64_C(3)<<36)|((uint64_t)offset<<20)|(UINT64_C(7)<<16)|(UINT64_C(255)<<8)|rd;}
static void old_source(OldSource*s){memset(s,0,sizeof *s);uint64_t code[]={0,ldc(0,8,0),ldc(2,4,12),ldc(3,4,16),0,UINT64_C(0x5c10000000000000)|(UINT64_C(3)<<20)|(UINT64_C(7)<<16)|(UINT64_C(2)<<8)|4,UINT64_C(0xeed8000000000000)|(UINT64_C(4)<<48)|(UINT64_C(1)<<45)|(UINT64_C(7)<<16)|4,UINT64_C(0xe30000000007000f)};memcpy(s->code,code,sizeof code);uint32_t cb[]={0x10000,0,16,5,7,0,0,0};memcpy(s->cb,cb,sizeof cb);s->input=(SatchelShaderFixtureInput){.version=1,.struct_size=sizeof s->input,.code=(const uint8_t*)s->code,.constant_buffer=(const uint8_t*)s->cb,.code_size=64,.constant_buffer_size=32,.local_size_x=1,.local_size_y=1,.local_size_z=1,.constant_buffer_slot=3,.uniform_binding=1,.storage_binding=2,.storage_size=16,.storage_offset_alignment=16,.storage_gpu_address=0x10000};}
static void w32(uint8_t*p,uint32_t x){memcpy(p,&x,4);}static void w64(uint8_t*p,uint64_t x){memcpy(p,&x,8);}
static void new_source(NewSource*s,unsigned location,unsigned rotate){memset(s,0,sizeof *s);uint8_t*v=s->vertex,*f=s->fragment;w32(v,0x60461);w32(f,0x61462);w32(v+16,0xff000);w32(f+16,0xff000);w32(v+24,255);w32(v+52,0xf0000000);w32(v+56,15u<<(location*4));w32(f+24+(location/4)*4,0x55u<<((location%4)*8));w32(f+72,15);uint64_t code[]={0,0xefd9ff800807ff00ULL,0xeff1ff800707ff00ULL,0xefd9ff800907ff04ULL,0,0xeff1ff800807ff04ULL+((uint64_t)location*16<<20),0xe30000000007000fULL};for(unsigned i=0;i<7;i++)w64(v+80+i*8,code[i]);for(unsigned i=0;i<4;i++){unsigned slot=i<3?i+1:5;uint64_t off=0x80+location*16+((i+rotate)%4)*4;w64(f+80+slot*8,0xe003ff800ff7ff00ULL|(off<<28)|i);}w64(f+128,0xe30000000007000fULL);
 s->input=(SatchelCFInput){.version=1,.size=sizeof s->input,.vertex={v,v+80,80,56},.fragment={f,f+80,80,56},.state={.known=255,.single_vertex_program=1,.viewport_transform_enabled=1,.depth_zero_to_one=1,.origin_upper_left=1,.alpha_test_always=1,.extra_known=4095,.topology=3,.point_size_bits=0x3f800000,.float_vertex_mask=3,.float_output_mask=1},.host={.known=SATCHEL_CF_HOST_ALL,.values=SATCHEL_CF_HOST_QUERYHOSTSUPPORTSBGRAFORMAT,.storage_alignment=16,.physical_bgra8_color_attachment=1,.admitted_color_format=1,.current_color_format=1},.max_work=65536,.timeout_ns=1000000000};}
static void old_ok(OldSource*s,OldOutput*out){memset(out,0,sizeof *out);CHECK(old_compile(&s->input,out->bytes,sizeof out->bytes,&out->info)==0);CHECK(out->info.version==1&&out->info.struct_size==sizeof out->info);CHECK(out->info.spirv_size>=20&&out->info.spirv_size<=sizeof out->bytes);CHECK(out->info.buffer_count==3&&out->info.active_buffer_count==2);CHECK(out->info.buffers[1].binding==1&&out->info.buffers[2].binding==2);uint32_t magic;memcpy(&magic,out->bytes,4);CHECK(magic==0x07230203);}
static void new_ok(NewSource*s,SatchelCFPair*out){memset(out,0,sizeof *out);CHECK(new_compile(&s->input,out,sizeof *out)==0);CHECK(out->version==1&&out->size==sizeof *out&&out->dynamic_code_features==0);CHECK(out->vertex.word_count>=5&&out->vertex.word_count<=4096&&out->fragment.word_count>=5&&out->fragment.word_count<=4096);CHECK(out->vertex.words[0]==0x07230203&&out->fragment.words[0]==0x07230203);CHECK(out->vertex.words[3]<=256&&out->fragment.words[3]<=256);CHECK(out->vertex_facts.declared_uniforms==1&&out->fragment_facts.declared_uniforms==1&&!out->vertex_facts.active_uniforms&&!out->fragment_facts.active_uniforms);}
static void old_failure(OldSource*s){OldOutput out,before;memset(&out,0xa5,sizeof out);before=out;CHECK(old_compile(&s->input,out.bytes,sizeof out.bytes,&out.info)==SATCHEL_SHADER_FIXTURE_INVALID);CHECK(!memcmp(&out,&before,sizeof out));}
static void new_failure(NewSource*s,int status){SatchelCFPair out,before;memset(&out,0xa5,sizeof out);before=out;CHECK(new_compile(&s->input,&out,sizeof out)==status);CHECK(!memcmp(&out,&before,sizeof out));}
typedef struct {unsigned calls,old_calls;} Callback;
static int nested(void*opaque){Callback*c=opaque;c->calls++;if(c->calls==1){OldSource s;OldOutput out;old_source(&s);old_ok(&s,&out);old_reference=out;c->old_calls++;}return 0;}
static int cancel_now(void*opaque){Callback*c=opaque;c->calls++;return 1;}
static atomic_int start,ready;
static void *worker(void*which){atomic_fetch_add(&ready,1);while(!atomic_load(&start)){}if(which){NewSource s;SatchelCFPair out;for(unsigned i=0;i<256;i++){new_source(&s,i%8,(i/8)%4);new_ok(&s,&out);CHECK(out.vertex_facts.varying_mask==(1u<<(i%8)));}}else{OldSource s;OldOutput out;old_source(&s);for(unsigned i=0;i<256;i++){s.cb[3]=i;s.cb[4]=~i;old_ok(&s,&out);CHECK(!memcmp(&out,&old_reference,sizeof out));}}return NULL;}
static void *nested_worker(void*unused){(void)unused;NewSource s;SatchelCFPair out;Callback c={0};new_source(&s,0,0);s.input.cancelled=nested;s.input.context=&c;new_ok(&s,&out);CHECK(c.old_calls==1);CHECK(!memcmp(&out.vertex,&new_reference.vertex,sizeof out.vertex));return NULL;}
static void provenance(void*old_handle,void*new_handle,const char*old_path,const char*new_path){Dl_info old_info,new_info;CHECK(dladdr((void*)old_compile,&old_info)!=0&&dladdr((void*)new_compile,&new_info)!=0);CHECK(old_info.dli_fbase!=new_info.dli_fbase);char a[4096],b[4096];CHECK(realpath(old_path,a)&&realpath(old_info.dli_fname,b)&&!strcmp(a,b));CHECK(realpath(new_path,a)&&realpath(new_info.dli_fname,b)&&!strcmp(a,b));void*old_debug=dlsym(old_handle,"DotNetRuntimeDebugHeader"),*new_debug=dlsym(new_handle,"DotNetRuntimeDebugHeader");CHECK(old_debug&&new_debug&&old_debug!=new_debug);CHECK(dlsym(old_handle,"satchel_captured_flat_compile")==NULL);CHECK(dlsym(new_handle,"satchel_shader_fixture_compile")==NULL);}
int main(int argc,char**argv){CHECK(argc==4);void*old_handle,*new_handle;
 if(!strcmp(argv[3],"new-first")||!strcmp(argv[3],"nested-first")){new_handle=dlopen(argv[2],RTLD_NOW|RTLD_LOCAL);old_handle=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);}else{old_handle=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL);new_handle=dlopen(argv[2],RTLD_NOW|RTLD_LOCAL);}CHECK(old_handle&&new_handle);
#ifdef COEXIST_LINKED
 old_compile=satchel_shader_fixture_compile;new_compile=satchel_captured_flat_compile;old_features=satchel_shader_runtime_features;old_probe=satchel_shader_probe;
 CHECK(dlsym(old_handle,"satchel_shader_fixture_compile")== (void*)old_compile&&dlsym(new_handle,"satchel_captured_flat_compile")== (void*)new_compile);
#else
 old_compile=(Old)dlsym(old_handle,"satchel_shader_fixture_compile");new_compile=(New)dlsym(new_handle,"satchel_captured_flat_compile");old_features=(Feature)dlsym(old_handle,"satchel_shader_runtime_features");old_probe=(Probe)dlsym(old_handle,"satchel_shader_probe");
#endif
 CHECK(old_compile&&new_compile&&old_features&&old_probe);provenance(old_handle,new_handle,argv[1],argv[2]);OldSource old;NewSource fresh;old_source(&old);new_source(&fresh,0,0);Callback c={0};
 /* No managed warmup/test export or runtime-features call before these. */
 if(!strcmp(argv[3],"nested-first")){fresh.input.cancelled=nested;fresh.input.context=&c;new_ok(&fresh,&new_reference);CHECK(c.old_calls==1);fresh.input.cancelled=NULL;fresh.input.context=NULL;}
 else if(!strcmp(argv[3],"new-first")){new_ok(&fresh,&new_reference);old_ok(&old,&old_reference);}
 else if(!strcmp(argv[3],"old-first")){old_ok(&old,&old_reference);new_ok(&fresh,&new_reference);}
 else if(!strcmp(argv[3],"cold-cancel")){fresh.input.cancelled=cancel_now;fresh.input.context=&c;new_failure(&fresh,SATCHEL_CF_CANCELLED);CHECK(c.calls==1);fresh.input.cancelled=NULL;fresh.input.context=NULL;old_ok(&old,&old_reference);new_ok(&fresh,&new_reference);}
 else if(!strcmp(argv[3],"cold-limit")){fresh.input.max_work=1;new_failure(&fresh,SATCHEL_CF_LIMIT);fresh.input.max_work=65536;old_ok(&old,&old_reference);new_ok(&fresh,&new_reference);}
 else if(!strcmp(argv[3],"cold-invalid")){old.input.version=0;old_failure(&old);fresh.input.version=0;new_failure(&fresh,SATCHEL_CF_INVALID);old.input.version=1;fresh.input.version=1;old_ok(&old,&old_reference);new_ok(&fresh,&new_reference);}
 else CHECK(0);
 CHECK(old_features()==0);uint64_t exit_code[4]={0,0xe30000000007000fULL,0x50b0000000070f00ULL,0x50b0000000070f00ULL};uint8_t exit_spv[16384];CHECK(old_probe((uint8_t*)exit_code,sizeof exit_code,exit_spv,sizeof exit_spv)>0);
 OldOutput out;SatchelCFPair pair;for(unsigned n=0;n<32;n++){old_ok(&old,&out);CHECK(!memcmp(&out,&old_reference,sizeof out));new_source(&fresh,n%8,n/8);new_ok(&fresh,&pair);CHECK(pair.vertex_facts.varying_mask==(1u<<(n%8)));old.input.version=0;old_failure(&old);old.input.version=1;fresh.input.max_work=1;new_failure(&fresh,SATCHEL_CF_LIMIT);}
 /* Borrowed callback/source pages become inaccessible after return. Subsequent
  * calls must neither revisit the context nor read original source pointers. */
 size_t page=(size_t)sysconf(_SC_PAGESIZE);void*memory=mmap(NULL,page*3,PROT_READ|PROT_WRITE,MAP_ANON|MAP_PRIVATE,-1,0);CHECK(memory!=MAP_FAILED);CHECK(sizeof(OldSource)<page&&sizeof(NewSource)<page&&sizeof(Callback)<page);
 OldSource*owned_old=memory;NewSource*owned_new=(void*)((uint8_t*)memory+page);Callback*owned_callback=(void*)((uint8_t*)memory+page*2);memset(owned_callback,0,sizeof *owned_callback);old_source(owned_old);new_source(owned_new,0,0);owned_new->input.cancelled=nested;owned_new->input.context=owned_callback;old_ok(owned_old,&out);new_ok(owned_new,&pair);CHECK(owned_callback->old_calls==1);CHECK(mprotect(memory,page*3,PROT_NONE)==0);
 new_source(&fresh,0,0);for(unsigned n=0;n<16;n++){old_ok(&old,&out);new_ok(&fresh,&pair);CHECK(!memcmp(&pair.vertex,&new_reference.vertex,sizeof pair.vertex)&&!memcmp(&pair.fragment,&new_reference.fragment,sizeof pair.fragment));}CHECK(munmap(memory,page*3)==0);
 pthread_attr_t attr;CHECK(pthread_attr_init(&attr)==0);CHECK(pthread_attr_setstacksize(&attr,512*1024)==0);pthread_t a,b;CHECK(pthread_create(&a,&attr,worker,NULL)==0);CHECK(pthread_create(&b,&attr,worker,(void*)1)==0);while(atomic_load(&ready)!=2){}atomic_store(&start,1);CHECK(pthread_join(a,NULL)==0);CHECK(pthread_join(b,NULL)==0);CHECK(pthread_create(&a,&attr,nested_worker,NULL)==0);CHECK(pthread_join(a,NULL)==0);CHECK(pthread_attr_destroy(&attr)==0);
 old_ok(&old,&out);new_ok(&fresh,&pair);CHECK(!memcmp(&out,&old_reference,sizeof out));CHECK(!memcmp(&pair.vertex,&new_reference.vertex,sizeof pair.vertex)&&!memcmp(&pair.fragment,&new_reference.fragment,sizeof pair.fragment));
 /* Deliberately no dlclose: NativeAOT unloading is unsupported. */
 printf("%u coexistence assertions passed; mode=%s; separate images/debug headers;512 concurrent cross-library compiles; no unload or GPU/device claim.\n",atomic_load(&checks),argv[3]);return 0;}
