#include "GuestVIDispatch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define TEXT UINT64_C(0x100000)
#define STRINGS UINT64_C(0x200000)
#define SYMBOLS UINT64_C(0x201000)
#define OUTPUT UINT64_C(0x300000)
#define IDENTITY UINT64_C(0x400000)
#define PROTECTED UINT64_C(0x500000)
#define ALIAS UINT64_C(0x600000)
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"Original VI assertion failed at line %d\n",__LINE__);exit(1);}}while(0)
static uint64_t next_process=1;
typedef struct {SatchelCPUProbe*cpu;SatchelGuestVI*owner;SatchelGuestVIConfig config;SatchelGuestVIBorrow borrow;
 SatchelGuestVIEntries entries;SatchelGuestSymbolTable table;int claimed,deny_validate,deny_guard,cancel,cancel_at,cancel_calls,reenter;
 unsigned claim_calls;uint8_t metadata[8192];} Fixture;
static void put32(uint8_t*p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8*i));}
static void put64(uint8_t*p,uint64_t n){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(n>>(8*i));}
static uint64_t read64(SatchelCPUProbe*c,uint64_t a){uint8_t p[8];CHECK(satchel_cpu_probe_read(c,a,p,8));uint64_t n=0;for(unsigned i=0;i<8;i++)n|=(uint64_t)p[i]<<(8*i);return n;}
static int validate(void*ctx,SatchelCPUProbe*c,const SatchelGuestVIReservation*r){Fixture*f=ctx;if(f->deny_validate||c!=f->cpu||memcmp(r,&f->config.reservation,sizeof(*r)))return 0;
 // Synthetic Process fixture knows dedicated initial backing and owns ALL host
 // map changes. Exact NONE no-op additionally rejects locked/alias views.
 return satchel_cpu_probe_set_permissions(c,IDENTITY,4096,0,0)==SATCHEL_CPU_MAP_OK;}
static int claim(void*ctx,SatchelCPUProbe*c,const SatchelGuestVIReservation*r){Fixture*f=ctx;f->claim_calls++;if(f->claimed||!validate(ctx,c,r))return 0;f->claimed=1;return 1;}
static int guard(void*ctx,SatchelCPUProbe*c,uint64_t a,size_t n){Fixture*f=ctx;if(f->deny_guard)return 0;int overlap=1;return satchel_cpu_probe_physical_overlap(c,a,n,PROTECTED,4096,&overlap)&&!overlap;}
static int cancel(void*ctx){Fixture*f=ctx;f->cancel_calls++;if(f->reenter){SatchelGuestVIStats s;CHECK(satchel_guest_vi_stats(f->owner,&s)==SATCHEL_GVI_BUSY);}return f->cancel||(f->cancel_at&&f->cancel_calls>=f->cancel_at);}
static int read_guest(void*ctx,uint64_t a,void*out,size_t n){return satchel_cpu_probe_read(((Fixture*)ctx)->cpu,a,out,n);}
static int cancelled_lookup(void*ctx){return ((Fixture*)ctx)->cancel;}
static SatchelGuestVIStats stats(Fixture*f){SatchelGuestVIStats s;CHECK(satchel_guest_vi_stats(f->owner,&s)==SATCHEL_GVI_OK);return s;}
static void setup(Fixture*f,int create_owner){memset(f,0,sizeof(*f));uint8_t code[4096]={0},output[8192],protected[4096];memset(output,0xa5,sizeof(output));memset(protected,0x55,sizeof(protected));
 // Independent original BLR X19 caller, NOP return; named callees are RET data
 // stopped BEFORE execution. No original game instructions or pointers.
 put32(code,0xd63f0260);put32(code+4,0xd503201f);for(unsigned op=1;op<=7;op++)put32(code+0x100+op*16,0xd65f03c0);
 unsigned string_at=1;for(unsigned op=1;op<=7;op++){const char*n=satchel_guest_vi_entry_name(op);size_t length=strlen(n)+1;memcpy(f->metadata+string_at,n,length);uint8_t*s=f->metadata+4096+op*24;put32(s,string_at);s[4]=0x12;s[6]=1;put64(s+8,0x100+op*16);put64(s+16,4);string_at+=(unsigned)length;}
 SatchelCPUProbeRegion regions[]={{TEXT,code,sizeof(code),sizeof(code),5},{STRINGS,f->metadata,sizeof(f->metadata),sizeof(f->metadata),3},{OUTPUT,output,sizeof(output),sizeof(output),3},{IDENTITY,NULL,0,4096,3},{PROTECTED,protected,sizeof(protected),sizeof(protected),3}};char err[128];f->cpu=satchel_cpu_probe_create(regions,5,TEXT,err,sizeof(err));CHECK(f->cpu);
 CHECK(satchel_cpu_probe_set_permissions(f->cpu,IDENTITY,4096,3,0)==SATCHEL_CPU_MAP_OK);
 uint64_t process=next_process++;f->config=(SatchelGuestVIConfig){1,sizeof(SatchelGuestVIConfig),{1,sizeof(SatchelGuestVIReservation),process,1000+process,IDENTITY,4096},77,100000,30000};
 f->borrow=(SatchelGuestVIBorrow){f->cpu,77,validate,f,guard,f,cancel,f};
 f->table=(SatchelGuestSymbolTable){TEXT,TEXT,4096,STRINGS,SYMBOLS,string_at,8*24};
 CHECK(satchel_guest_vi_resolve_entries(&f->table,1,read_guest,cancelled_lookup,f,1000,&f->entries)==SATCHEL_GVI_OK);
 uint64_t points[8];CHECK(satchel_guest_vi_entry_points(&f->entries,points,7)==7);points[7]=TEXT+0x300;CHECK(satchel_cpu_probe_set_breakpoints(f->cpu,points,8));
 if(create_owner){CHECK(satchel_guest_vi_create(&f->config,f->cpu,claim,f,&f->owner)==SATCHEL_GVI_OK);CHECK(f->claimed&&f->claim_calls==1);}
}
static void fixture(Fixture*f){setup(f,1);}
static void finish(Fixture*f){CHECK(satchel_guest_vi_try_destroy(&f->owner)==SATCHEL_GVI_OK);CHECK(!f->owner);CHECK(satchel_cpu_probe_try_destroy(f->cpu));f->cpu=NULL;}
static SatchelGuestVIStatus invoke_lr(Fixture*f,unsigned op,uint64_t x0,uint64_t x1,uint64_t lr,int expect_refusal){
 CHECK(satchel_cpu_probe_set_pc(f->cpu,TEXT));CHECK(satchel_cpu_probe_set_x(f->cpu,0,x0));CHECK(satchel_cpu_probe_set_x(f->cpu,1,x1));CHECK(satchel_cpu_probe_set_x(f->cpu,19,f->entries.address[op-1]));CHECK(satchel_cpu_probe_set_x(f->cpu,20,0x1122334455667788));
 SatchelCPUProbeResult stop=satchel_cpu_probe_run(f->cpu,2,NULL,NULL);CHECK(stop.reason==SATCHEL_CPU_BREAKPOINT&&stop.steps==1&&stop.pc==f->entries.address[op-1]);if(lr)CHECK(satchel_cpu_probe_set_x(f->cpu,30,lr));
 SatchelGuestVIStats before=stats(f);uint64_t regs[31];for(unsigned i=0;i<31;i++)CHECK(satchel_cpu_probe_get_x(f->cpu,i,&regs[i]));uint8_t memory[8192],after[8192];CHECK(satchel_cpu_probe_read(f->cpu,OUTPUT,memory,sizeof(memory)));
 SatchelGuestVIResult result={0};SatchelGuestVIStatus s=satchel_guest_vi_dispatch(&f->entries,f->owner,&f->borrow,&stop,&result);CHECK(result.status==s&&result.operation==op);
 SatchelCPUProbeResult current=satchel_cpu_probe_run(f->cpu,0,NULL,NULL);CHECK(current.reason==SATCHEL_CPU_BUDGET);CHECK(current.pc==(s==SATCHEL_GVI_OK?(lr?lr:TEXT+4):stop.pc));
 for(unsigned i=0;i<31;i++){uint64_t v;CHECK(satchel_cpu_probe_get_x(f->cpu,i,&v));CHECK(v==((s==SATCHEL_GVI_OK&&op<=3&&i==0)?0:regs[i]));}
 if(expect_refusal){CHECK(s!=SATCHEL_GVI_OK);SatchelGuestVIStats post=stats(f);CHECK(!memcmp(&before,&post,6*sizeof(uint32_t)));CHECK(satchel_cpu_probe_read(f->cpu,OUTPUT,after,sizeof(after)));CHECK(!memcmp(memory,after,sizeof(memory)));}
 return s;
}
static SatchelGuestVIStatus invoke(Fixture*f,unsigned op,uint64_t a,uint64_t b,int refused){return invoke_lr(f,op,a,b,0,refused);}
static void open_layer(Fixture*f,uint64_t*d,uint64_t*l,uint64_t*n){CHECK(invoke(f,1,OUTPUT+16,0,0)==SATCHEL_GVI_OK);*d=read64(f->cpu,OUTPUT+16);CHECK(invoke(f,2,OUTPUT+32,*d,0)==SATCHEL_GVI_OK);*l=read64(f->cpu,OUTPUT+32);CHECK(invoke(f,3,OUTPUT+48,*l,0)==SATCHEL_GVI_OK);*n=read64(f->cpu,OUTPUT+48);CHECK(*d==IDENTITY&&*l==IDENTITY+16&&*n==IDENTITY+32);}
static void positive(void){Fixture f;fixture(&f);uint64_t d,l,n;open_layer(&f,&d,&l,&n);CHECK(invoke(&f,3,OUTPUT+64,l,0)==SATCHEL_GVI_OK);CHECK(read64(f.cpu,OUTPUT+64)==n);CHECK(stats(&f).issued==3);
 CHECK(invoke(&f,1,OUTPUT+80,0,1)==SATCHEL_GVI_BUSY);CHECK(invoke(&f,2,OUTPUT+80,d,1)==SATCHEL_GVI_BUSY);CHECK(invoke(&f,2,OUTPUT+80,d+16,1)==SATCHEL_GVI_BAD_OBJECT);CHECK(invoke(&f,3,OUTPUT+80,l+16,1)==SATCHEL_GVI_BAD_OBJECT);CHECK(invoke(&f,5,d,0,1)==SATCHEL_GVI_BUSY);
 uint64_t refs[32];SatchelGuestVIInfo info={0};for(unsigned i=0;i<32;i++)CHECK(satchel_guest_vi_retain_native(f.owner,&f.borrow,n,&refs[i],&info)==SATCHEL_GVI_OK);CHECK(info.native_identity==n&&info.surface_epoch==77&&info.display_generation==1&&info.layer_generation==2&&info.native_generation==3);uint64_t sentinel=99;SatchelGuestVIInfo previous=info;CHECK(satchel_guest_vi_retain_native(f.owner,&f.borrow,n,&sentinel,&info)==SATCHEL_GVI_LIMIT);CHECK(sentinel==99&&!memcmp(&info,&previous,sizeof(info)));CHECK(invoke(&f,4,l,0,1)==SATCHEL_GVI_BUSY);
 for(unsigned i=0;i<32;i++)CHECK(satchel_guest_vi_release_native(f.owner,refs[i])==SATCHEL_GVI_OK);CHECK(satchel_guest_vi_release_native(f.owner,refs[0])==SATCHEL_GVI_BAD_OBJECT);CHECK(invoke(&f,4,l,0,0)==SATCHEL_GVI_OK);CHECK(invoke(&f,4,l,0,1)==SATCHEL_GVI_BAD_OBJECT);CHECK(invoke(&f,2,OUTPUT+32,d,0)==SATCHEL_GVI_OK);uint64_t l2=read64(f.cpu,OUTPUT+32);CHECK(l2!=l);CHECK(invoke(&f,3,OUTPUT+48,l,1)==SATCHEL_GVI_BAD_OBJECT);CHECK(invoke(&f,4,l2,0,0)==SATCHEL_GVI_OK);CHECK(invoke(&f,5,d,0,0)==SATCHEL_GVI_OK);CHECK(invoke(&f,1,OUTPUT+16,0,0)==SATCHEL_GVI_OK);CHECK(read64(f.cpu,OUTPUT+16)!=d);CHECK(invoke(&f,5,d,0,1)==SATCHEL_GVI_BAD_OBJECT);
 uint8_t bytes[8192];CHECK(satchel_cpu_probe_read(f.cpu,OUTPUT,bytes,sizeof(bytes)));for(unsigned i=0;i<sizeof(bytes);i++)if(!((i>=16&&i<24)||(i>=32&&i<40)||(i>=48&&i<56)||(i>=64&&i<72)))CHECK(bytes[i]==0xa5);
 finish(&f);SatchelGuestVI*new_owner=(void*)1;CHECK(satchel_guest_vi_create(&f.config,NULL,claim,&f,&new_owner)==SATCHEL_GVI_INVALID&&!new_owner);
}
static void failures(void){Fixture f;fixture(&f);
 CHECK(invoke_lr(&f,1,OUTPUT+16,0,IDENTITY,1)==SATCHEL_GVI_BAD_RETURN);CHECK(invoke(&f,1,IDENTITY,0,1)==SATCHEL_GVI_MEMORY);CHECK(invoke(&f,1,OUTPUT+8192-4,0,1)==SATCHEL_GVI_MEMORY);CHECK(invoke(&f,1,PROTECTED+16,0,1)==SATCHEL_GVI_MEMORY);CHECK(invoke(&f,6,OUTPUT,0,1)==SATCHEL_GVI_UNSUPPORTED);CHECK(invoke(&f,7,OUTPUT,0,1)==SATCHEL_GVI_UNSUPPORTED);
 f.deny_guard=1;CHECK(invoke(&f,1,OUTPUT,0,1)==SATCHEL_GVI_MEMORY);f.deny_guard=0;f.deny_validate=1;CHECK(invoke(&f,1,OUTPUT,0,1)==SATCHEL_GVI_RESERVATION);f.deny_validate=0;f.borrow.surface_epoch=78;CHECK(invoke(&f,1,OUTPUT,0,1)==SATCHEL_GVI_NO_SURFACE);f.borrow.surface_epoch=77;f.cancel=1;CHECK(invoke(&f,1,OUTPUT,0,1)==SATCHEL_GVI_CANCELLED);f.cancel=0;f.cancel_calls=0;f.cancel_at=2;CHECK(invoke(&f,1,OUTPUT,0,1)==SATCHEL_GVI_CANCELLED);f.cancel_at=0;f.reenter=1;CHECK(invoke(&f,1,OUTPUT,0,0)==SATCHEL_GVI_OK);f.reenter=0;
 CHECK(satchel_cpu_probe_set_permissions(f.cpu,IDENTITY,4096,0,3)==SATCHEL_CPU_MAP_OK);CHECK(invoke(&f,3,OUTPUT,0,1)==SATCHEL_GVI_RESERVATION);CHECK(satchel_cpu_probe_set_permissions(f.cpu,IDENTITY,4096,3,1)==SATCHEL_CPU_MAP_OK);CHECK(invoke(&f,3,OUTPUT,0,1)==SATCHEL_GVI_RESERVATION);CHECK(satchel_cpu_probe_set_permissions(f.cpu,IDENTITY,4096,1,3)==SATCHEL_CPU_MAP_OK);CHECK(satchel_cpu_probe_map_alias(f.cpu,ALIAS,IDENTITY,4096)==SATCHEL_CPU_MAP_OK);int overlap=0;CHECK(satchel_cpu_probe_physical_overlap(f.cpu,IDENTITY,4096,ALIAS,4096,&overlap)&&overlap);CHECK(invoke(&f,3,OUTPUT,0,1)==SATCHEL_GVI_RESERVATION);CHECK(satchel_cpu_probe_unmap_alias(f.cpu,ALIAS,IDENTITY,4096)==SATCHEL_CPU_MAP_OK);CHECK(satchel_cpu_probe_set_permissions(f.cpu,IDENTITY,4096,3,0)==SATCHEL_CPU_MAP_OK);
 CHECK(satchel_cpu_probe_map_alias(f.cpu,ALIAS,PROTECTED,4096)==SATCHEL_CPU_MAP_OK);CHECK(invoke(&f,2,ALIAS+16,read64(f.cpu,OUTPUT),1)==SATCHEL_GVI_MEMORY);CHECK(satchel_cpu_probe_unmap_alias(f.cpu,ALIAS,PROTECTED,4096)==SATCHEL_CPU_MAP_OK);
 CHECK(invoke(&f,2,OUTPUT+16,read64(f.cpu,OUTPUT),0)==SATCHEL_GVI_OK);finish(&f);
}
static void retirement(void){Fixture f;fixture(&f);uint64_t d,l,n;open_layer(&f,&d,&l,&n);uint64_t ref;SatchelGuestVIInfo info;CHECK(satchel_guest_vi_retain_native(f.owner,&f.borrow,n,&ref,&info)==SATCHEL_GVI_OK);CHECK(satchel_guest_vi_detach(f.owner,78)==SATCHEL_GVI_NO_SURFACE);CHECK(satchel_guest_vi_detach(f.owner,77)==SATCHEL_GVI_OK);CHECK(invoke(&f,3,OUTPUT+80,l,1)==SATCHEL_GVI_NO_SURFACE);uint64_t unchanged=99;CHECK(satchel_guest_vi_retain_native(f.owner,&f.borrow,n,&unchanged,&info)==SATCHEL_GVI_NO_SURFACE&&unchanged==99);CHECK(invoke(&f,4,l,0,1)==SATCHEL_GVI_BUSY);CHECK(satchel_guest_vi_release_native(f.owner,ref)==SATCHEL_GVI_OK);CHECK(invoke(&f,4,l,0,0)==SATCHEL_GVI_OK);CHECK(invoke(&f,5,d,0,0)==SATCHEL_GVI_OK);finish(&f);
 fixture(&f);open_layer(&f,&d,&l,&n);CHECK(satchel_guest_vi_retain_native(f.owner,&f.borrow,n,&ref,&info)==SATCHEL_GVI_OK);SatchelGuestVI*saved=f.owner;CHECK(satchel_guest_vi_try_destroy(&f.owner)==SATCHEL_GVI_BUSY&&f.owner==saved);CHECK(invoke(&f,4,l,0,1)==SATCHEL_GVI_BUSY);CHECK(satchel_cpu_probe_try_destroy(f.cpu));f.cpu=NULL;memset(&f.borrow,0,sizeof(f.borrow));CHECK(satchel_guest_vi_release_native(f.owner,ref)==SATCHEL_GVI_OK);CHECK(satchel_guest_vi_try_destroy(&f.owner)==SATCHEL_GVI_OK&&!f.owner);
}
static void exhaustion(void){Fixture f;fixture(&f);for(unsigned i=0;i<256;i++){CHECK(invoke(&f,1,OUTPUT,0,0)==SATCHEL_GVI_OK);uint64_t id=read64(f.cpu,OUTPUT);CHECK(id==IDENTITY+(uint64_t)i*16);CHECK(invoke(&f,5,id,0,0)==SATCHEL_GVI_OK);}CHECK(stats(&f).issued==256);CHECK(invoke(&f,1,OUTPUT,0,1)==SATCHEL_GVI_LIMIT);finish(&f);}
static void resolver(void){Fixture f;fixture(&f);SatchelGuestVIEntries out,old;memset(&out,0xa5,sizeof(out));old=out;SatchelGuestSymbolTable t=f.table;
 f.cancel=1;CHECK(satchel_guest_vi_resolve_entries(&t,1,read_guest,cancelled_lookup,&f,1000,&out)==SATCHEL_GVI_CANCELLED&&!memcmp(&out,&old,sizeof(out)));f.cancel=0;
 t.symbols_size=4*24;CHECK(satchel_guest_vi_resolve_entries(&t,1,read_guest,NULL,&f,1000,&out)==SATCHEL_GVI_OK&&out.present_mask==7);t.symbols_size=3*24;out=old;CHECK(satchel_guest_vi_resolve_entries(&t,1,read_guest,NULL,&f,1000,&out)==SATCHEL_GVI_SYMBOL&&!memcmp(&out,&old,sizeof(out)));
 t=f.table;SatchelGuestSymbolTable both[2]={t,t};CHECK(satchel_guest_vi_resolve_entries(both,2,read_guest,NULL,&f,1000,&out)==SATCHEL_GVI_SYMBOL);uint8_t entry[24];CHECK(satchel_cpu_probe_read(f.cpu,SYMBOLS+2*24,entry,24));uint8_t changed[24];memcpy(changed,entry,24);put64(changed+8,0x110);CHECK(satchel_cpu_probe_write(f.cpu,SYMBOLS+2*24,changed,24));CHECK(satchel_guest_vi_resolve_entries(&t,1,read_guest,NULL,&f,1000,&out)==SATCHEL_GVI_SYMBOL);CHECK(satchel_cpu_probe_write(f.cpu,SYMBOLS+2*24,entry,24));
 memcpy(changed,entry,24);changed[4]=0x11;CHECK(satchel_cpu_probe_write(f.cpu,SYMBOLS+2*24,changed,24));CHECK(satchel_guest_vi_resolve_entries(&t,1,read_guest,NULL,&f,1000,&out)==SATCHEL_GVI_SYMBOL);CHECK(satchel_cpu_probe_write(f.cpu,SYMBOLS+2*24,entry,24));
 t.strings_size=0;CHECK(satchel_guest_vi_resolve_entries(&t,1,read_guest,NULL,&f,1000,&out)==SATCHEL_GVI_SYMBOL);t=f.table;CHECK(satchel_guest_vi_resolve_entries(&t,0,read_guest,NULL,&f,1000,&out)==SATCHEL_GVI_INVALID);uint64_t points[7];memset(points,0xa5,sizeof(points));uint64_t copy[7];memcpy(copy,points,sizeof(copy));CHECK(satchel_guest_vi_entry_points(&f.entries,points,2)==0&&!memcmp(points,copy,sizeof(copy)));SatchelGuestVIEntries alias=f.entries;alias.address[4]=alias.address[3];CHECK(satchel_guest_vi_entry_points(&alias,points,7)==0);
 CHECK(satchel_cpu_probe_set_pc(f.cpu,TEXT+0x300));SatchelCPUProbeResult stop=satchel_cpu_probe_run(f.cpu,1,NULL,NULL);CHECK(stop.reason==SATCHEL_CPU_BREAKPOINT);SatchelGuestVIResult result={99,99,99};CHECK(satchel_guest_vi_dispatch(&f.entries,f.owner,&f.borrow,&stop,&result)==SATCHEL_GVI_NOT_OURS&&result.status==99);finish(&f);
}
#ifdef VI_FAULT_TEST
static int fail_write,clock_mode,clock_calls,fail_allocate,fail_get=-1;
void *vi_test_calloc(size_t n,size_t size){if(fail_allocate){fail_allocate=0;return NULL;}return calloc(n,size);}
int vi_test_get_x(SatchelCPUProbe*c,unsigned reg,uint64_t*out){if(fail_get==(int)reg){fail_get=-1;return 0;}return satchel_cpu_probe_get_x(c,reg,out);}
static uint64_t clock_value=UINT64_C(1000000000);
int vi_test_clock_gettime(clockid_t id,struct timespec*out){
 if(!clock_mode)return clock_gettime(id,out);
 clock_calls++;if(clock_mode==2||(clock_mode==3&&clock_calls==2))return -1;
 uint64_t n=clock_value;if(clock_mode==1&&clock_calls==2)n+=UINT64_C(2000000);
 out->tv_sec=(time_t)(n/1000000000);out->tv_nsec=(long)(n%1000000000);return 0;
}
SatchelCPUWriteResult vi_test_writev(SatchelCPUProbe*c,const SatchelCPUProbeWrite*w,size_t n){if(fail_write){fail_write=0;return SATCHEL_CPU_WRITE_ALLOCATION;}return satchel_cpu_probe_writev(c,w,n);}
static void faults(void){Fixture f;fixture(&f);for(unsigned op=1;op<=3;op++){uint64_t arg=op==1?0:read64(f.cpu,OUTPUT+(op-2)*16);fail_write=1;CHECK(invoke(&f,op,OUTPUT+(op-1)*16,arg,1)==SATCHEL_GVI_ALLOCATION);CHECK(invoke(&f,op,OUTPUT+(op-1)*16,arg,0)==SATCHEL_GVI_OK);}CHECK(stats(&f).issued==3);finish(&f);}
#endif

static void constructors(void){Fixture f;fixture(&f);SatchelGuestVI*other=(void*)1;unsigned calls=f.claim_calls;
 CHECK(satchel_guest_vi_create(&f.config,f.cpu,claim,&f,&other)==SATCHEL_GVI_BUSY&&!other&&f.claim_calls==calls);
 SatchelGuestVIConfig bad=f.config;bad.reservation.generation++;CHECK(satchel_guest_vi_create(&bad,f.cpu,claim,&f,&other)==SATCHEL_GVI_RESERVATION&&!other);CHECK(f.claim_calls==calls+1);
 bad=f.config;bad.reservation.address=UINT64_MAX-4095;CHECK(satchel_guest_vi_create(&bad,f.cpu,claim,&f,&other)==SATCHEL_GVI_INVALID&&!other);
 bad=f.config;bad.reservation.length=8192;CHECK(satchel_guest_vi_create(&bad,f.cpu,claim,&f,&other)==SATCHEL_GVI_INVALID&&!other);
 _Alignas(8) uint8_t raw[sizeof(SatchelGuestVIConfig)+8];memcpy(raw+4,&f.config,sizeof(f.config));CHECK(satchel_guest_vi_create((SatchelGuestVIConfig*)(raw+4),f.cpu,claim,&f,&other)==SATCHEL_GVI_INVALID&&!other);
 CHECK(satchel_guest_vi_try_destroy(&f.owner)==SATCHEL_GVI_OK);CHECK(satchel_guest_vi_create(&f.config,f.cpu,claim,&f,&other)==SATCHEL_GVI_RESERVATION&&!other);CHECK(f.claimed);CHECK(satchel_cpu_probe_try_destroy(f.cpu));
 Fixture all[5];for(unsigned i=0;i<4;i++)fixture(&all[i]);setup(&all[4],0);CHECK(satchel_guest_vi_create(&all[4].config,all[4].cpu,claim,&all[4],&other)==SATCHEL_GVI_LIMIT&&!other&&!all[4].claimed&&!all[4].claim_calls);CHECK(satchel_cpu_probe_try_destroy(all[4].cpu));for(unsigned i=0;i<4;i++)finish(&all[i]);
}
static void reservation_and_refs(void){Fixture f,g;fixture(&f);fixture(&g);uint64_t d,l,n;open_layer(&f,&d,&l,&n);uint64_t ref=0;SatchelGuestVIInfo info;
 CHECK(satchel_guest_vi_retain_native(f.owner,&f.borrow,n,&ref,&info)==SATCHEL_GVI_OK);CHECK(satchel_guest_vi_release_native(g.owner,ref)==SATCHEL_GVI_BAD_OBJECT&&stats(&f).references==1&&!stats(&g).references);
 uint64_t value=99;SatchelGuestVIInfo before;memset(&info,0xa5,sizeof(info));before=info;f.deny_validate=1;CHECK(satchel_guest_vi_retain_native(f.owner,&f.borrow,n,&value,&info)==SATCHEL_GVI_RESERVATION&&value==99&&!memcmp(&info,&before,sizeof(info)));f.deny_validate=0;
 uint64_t pid=f.config.reservation.process_id;f.config.reservation.process_id++;CHECK(invoke(&f,3,OUTPUT+80,l,1)==SATCHEL_GVI_RESERVATION);f.config.reservation.process_id=pid;
 SatchelCPUProbe*cpu=f.borrow.cpu;f.borrow.cpu=g.cpu;CHECK(satchel_cpu_probe_set_pc(g.cpu,f.entries.address[2]));SatchelGuestVIResult result;CHECK(satchel_guest_vi_execute(f.owner,&f.borrow,3,OUTPUT,l,f.entries.address[2],TEXT+4,&result)==SATCHEL_GVI_RESERVATION);f.borrow.cpu=cpu;
 CHECK(satchel_guest_vi_release_native(f.owner,ref)==SATCHEL_GVI_OK);finish(&g);finish(&f);
}
static void resolved_refusal(Fixture*f,const SatchelGuestSymbolTable*t,size_t count,SatchelGuestVIStatus status){SatchelGuestVIEntries out,old;memset(&out,0xa5,sizeof(out));old=out;CHECK(satchel_guest_vi_resolve_entries(t,count,read_guest,NULL,f,1000,&out)==status);CHECK(!memcmp(&out,&old,sizeof(out)));}
static void resolver_edges(void){Fixture f;fixture(&f);uint8_t entry[24],changed[24];CHECK(satchel_cpu_probe_read(f.cpu,SYMBOLS+24,entry,24));
 for(unsigned variant=0;variant<5;variant++){memcpy(changed,entry,24);switch(variant){case 0:put32(changed,UINT32_MAX);break;case 1:put64(changed+8,4096);break;case 2:changed[5]=2;break;case 3:changed[6]=0;break;case 4:put64(changed+16,UINT64_MAX);break;}CHECK(satchel_cpu_probe_write(f.cpu,SYMBOLS+24,changed,24));resolved_refusal(&f,&f.table,1,SATCHEL_GVI_SYMBOL);}
 CHECK(satchel_cpu_probe_write(f.cpu,SYMBOLS+24,entry,24));uint8_t bad=1,zero=0;CHECK(satchel_cpu_probe_write(f.cpu,STRINGS,&bad,1));resolved_refusal(&f,&f.table,1,SATCHEL_GVI_SYMBOL);CHECK(satchel_cpu_probe_write(f.cpu,STRINGS,&zero,1));CHECK(satchel_cpu_probe_write(f.cpu,SYMBOLS,&bad,1));resolved_refusal(&f,&f.table,1,SATCHEL_GVI_SYMBOL);CHECK(satchel_cpu_probe_write(f.cpu,SYMBOLS,&zero,1));
 uint8_t imports[8*24];CHECK(satchel_cpu_probe_read(f.cpu,SYMBOLS,imports,sizeof(imports)));for(unsigned op=1;op<=7;op++){imports[op*24+6]=0;memset(imports+op*24+8,0,16);}CHECK(satchel_cpu_probe_write(f.cpu,SYMBOLS+0x800,imports,sizeof(imports)));SatchelGuestSymbolTable tables[2]={f.table,f.table};tables[1].symbols_address+=0x800;SatchelGuestVIEntries out;CHECK(satchel_guest_vi_resolve_entries(tables,2,read_guest,NULL,&f,1000,&out)==SATCHEL_GVI_OK&&out.present_mask==127);
 resolved_refusal(&f,tables+1,1,SATCHEL_GVI_SYMBOL);SatchelGuestVIEntries wrong=f.entries;wrong.present_mask&=~1u;uint64_t points[7]={0};CHECK(satchel_guest_vi_entry_points(&wrong,points,7)==0);
 finish(&f);
}
static void budgets(void){Fixture f;setup(&f,0);f.config.max_events=1;CHECK(satchel_guest_vi_create(&f.config,f.cpu,claim,&f,&f.owner)==SATCHEL_GVI_OK);CHECK(invoke(&f,1,OUTPUT,0,0)==SATCHEL_GVI_OK);CHECK(invoke(&f,5,read64(f.cpu,OUTPUT),0,1)==SATCHEL_GVI_LIMIT);finish(&f);
 setup(&f,0);f.config.timeout_ms=1;CHECK(satchel_guest_vi_create(&f.config,f.cpu,claim,&f,&f.owner)==SATCHEL_GVI_OK);CHECK(invoke(&f,1,OUTPUT,0,0)==SATCHEL_GVI_OK);struct timespec delay={0,5000000};nanosleep(&delay,NULL);CHECK(invoke(&f,5,read64(f.cpu,OUTPUT),0,0)==SATCHEL_GVI_OK);finish(&f);
}


#ifdef VI_FAULT_TEST
static void construction_argument_faults(void){Fixture f;setup(&f,0);SatchelGuestVI*out=(void*)1;fail_allocate=1;CHECK(satchel_guest_vi_create(&f.config,f.cpu,claim,&f,&out)==SATCHEL_GVI_ALLOCATION&&!out&&!f.claimed&&!f.claim_calls);CHECK(satchel_guest_vi_create(&f.config,f.cpu,claim,&f,&f.owner)==SATCHEL_GVI_OK);CHECK(f.claimed&&f.claim_calls==1);
 for(unsigned i=0;i<3;i++){unsigned reg=(unsigned[]){0,1,30}[i];CHECK(satchel_cpu_probe_set_pc(f.cpu,TEXT));CHECK(satchel_cpu_probe_set_x(f.cpu,0,OUTPUT));CHECK(satchel_cpu_probe_set_x(f.cpu,1,0));CHECK(satchel_cpu_probe_set_x(f.cpu,19,f.entries.address[0]));SatchelCPUProbeResult stop=satchel_cpu_probe_run(f.cpu,2,NULL,NULL);CHECK(stop.reason==SATCHEL_CPU_BREAKPOINT);uint64_t memory=read64(f.cpu,OUTPUT),x0;SatchelGuestVIStats before=stats(&f);SatchelGuestVIResult result={99,98,97};fail_get=(int)reg;CHECK(satchel_guest_vi_dispatch(&f.entries,f.owner,&f.borrow,&stop,&result)==SATCHEL_GVI_INVALID);CHECK(result.status==99&&result.operation==98&&result.events==97);CHECK(read64(f.cpu,OUTPUT)==memory);CHECK(satchel_cpu_probe_get_x(f.cpu,0,&x0)&&x0==OUTPUT);CHECK(satchel_cpu_probe_run(f.cpu,0,NULL,NULL).pc==stop.pc);SatchelGuestVIStats after=stats(&f);CHECK(!memcmp(&before,&after,sizeof(before)));}
 finish(&f);
}
static void deadline_faults(void){Fixture f;setup(&f,0);f.config.timeout_ms=1;CHECK(satchel_guest_vi_create(&f.config,f.cpu,claim,&f,&f.owner)==SATCHEL_GVI_OK);
 for(int mode=1;mode<=3;mode++){clock_mode=mode;clock_calls=0;CHECK(invoke(&f,1,OUTPUT,0,1)==(mode==1?SATCHEL_GVI_LIMIT:SATCHEL_GVI_CANCELLED));CHECK(!stats(&f).issued);}
 clock_mode=4;clock_calls=0;CHECK(invoke(&f,1,OUTPUT,0,0)==SATCHEL_GVI_OK);uint64_t display=read64(f.cpu,OUTPUT);clock_value+=UINT64_C(3600000000000);clock_calls=0;CHECK(invoke(&f,2,OUTPUT+16,display,0)==SATCHEL_GVI_OK);uint64_t layer=read64(f.cpu,OUTPUT+16);clock_value+=UINT64_C(3600000000000);clock_calls=0;CHECK(invoke(&f,3,OUTPUT+32,layer,0)==SATCHEL_GVI_OK);clock_value+=UINT64_C(3600000000000);clock_calls=0;CHECK(invoke(&f,4,layer,0,0)==SATCHEL_GVI_OK);clock_value+=UINT64_C(3600000000000);clock_calls=0;CHECK(invoke(&f,5,display,0,0)==SATCHEL_GVI_OK);clock_mode=0;finish(&f);
}
#endif

int main(void){positive();failures();retirement();exhaustion();resolver();constructors();reservation_and_refs();resolver_edges();budgets();
#ifdef VI_FAULT_TEST
 faults();deadline_faults();construction_argument_faults();
#endif
 printf("Original VI assertions: %u\n",checks);return 0;}
