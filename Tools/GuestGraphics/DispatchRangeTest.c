#include "GuestCheckedPoolServices.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
static unsigned checks;
static void check_at(int good,unsigned line){checks++;if(!good){fprintf(stderr,"range check%u failed line%u\n",checks,line);exit(1);}}
#define check(x) check_at((x),__LINE__)
enum{CODE=0x100000,BOOT=0x100100,STUBS=0x200000,DATA=0x300000,DEVICE=0x301000,CHILD=0x308000,ALIAS=0x500000};
typedef struct {SatchelCPUProbe*cpu;SatchelGuestDispatch*d;SatchelGuestDispatchLayout layout;int child,hold,reenter,invalid_caps,hold_device;unsigned calls;} Fixture;
static void word(uint8_t*p,uint32_t w){memcpy(p,&w,4);}
static SatchelGuestBackendStatus create(void*c,uint32_t flags,uint64_t*t,SatchelGuestDeviceCaps*cap){Fixture*f=c;(void)flags;*t=1;memset(cap,0,sizeof(*cap));cap->version=f->invalid_caps?0:1;return SATCHEL_GUEST_BACKEND_OK;}
static SatchelGuestBackendStatus done(void*c,uint64_t t){(void)c;(void)t;return SATCHEL_GUEST_BACKEND_OK;}
static SatchelGuestBackendStatus destroy_device(void*c,uint64_t t){Fixture*f=c;(void)t;return f->hold_device?SATCHEL_GUEST_BACKEND_FAILURE:SATCHEL_GUEST_BACKEND_OK;}
static SatchelGuestBackendStatus capabilities(void*c,uint64_t t,SatchelGuestDeviceCaps*cap){(void)c;(void)t;memset(cap,0,sizeof(*cap));cap->version=1;return SATCHEL_GUEST_BACKEND_OK;}
static Fixture*attaching;
static void*legacy_create(SatchelGuestPoolDeviceValidator v,SatchelGuestPoolRangeGuard g,void*c){(void)v;(void)g;(void)c;return attaching;}
static void*checked_create(SatchelGuestPoolDeviceValidator v,SatchelGuestPoolCheckedRangeGuard g,void*c){(void)v;(void)g;(void)c;return attaching;}
static SatchelGuestPoolStatus release(void**p){Fixture*f=*p;if(f->hold)return SATCHEL_GUEST_POOL_BUSY;*p=NULL;return SATCHEL_GUEST_POOL_OK;}
static SatchelGuestPoolStatus execute(void*p,SatchelCPUProbe*c,const SatchelGuestPoolRequest*q,SatchelGuestPoolResult*r){(void)p;(void)c;(void)q;(void)r;return SATCHEL_GUEST_POOL_UNSUPPORTED;}
static SatchelGuestPoolStatus resolve(void*p,SatchelCPUProbe*c,uint64_t d,uint64_t a,uint64_t n,SatchelGuestPoolBinding*b){(void)p;(void)c;(void)d;(void)a;(void)n;(void)b;return SATCHEL_GUEST_POOL_UNSUPPORTED;}
static SatchelGuestPoolStatus children(void*p,uint64_t d,uint32_t*n){(void)p;(void)d;*n=0;return SATCHEL_GUEST_POOL_OK;}
static SatchelGuestPoolStatus legacy_range(void*p,uint64_t a,uint64_t n){(void)p;(void)a;(void)n;return SATCHEL_GUEST_POOL_OK;}
static pthread_mutex_t pause_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pause_cond=PTHREAD_COND_INITIALIZER;
static int pause_enabled,entered,resume;
static SatchelGuestPoolStatus checked_range(void*p,SatchelCPUProbe*cpu,uint64_t a,uint64_t n){
 Fixture*f=p;f->calls++;
 if(f->reenter){check(!satchel_guest_dispatch_check_output_range(f->d,cpu,DATA,8));SatchelGuestDispatch*copy=f->d;check(!satchel_guest_dispatch_handoff(&copy)&&copy==f->d);}
 if(pause_enabled){pthread_mutex_lock(&pause_lock);entered=1;pthread_cond_signal(&pause_cond);while(!resume)pthread_cond_wait(&pause_cond,&pause_lock);pthread_mutex_unlock(&pause_lock);}
 int overlap=1;if(f->child&&(!satchel_cpu_probe_physical_overlap(cpu,a,n,CHILD,4096,&overlap)||overlap))return SATCHEL_GUEST_POOL_BAD_OBJECT;
 return SATCHEL_GUEST_POOL_OK;
}
static void make(Fixture*f,int service){
 memset(f,0,sizeof(*f));uint8_t code[4096]={0};word(code,0xd63f0120);word(code+4,0xd4000841);
 word(code+8,0xc85f7c01);word(code+12,0xc8027c03); // Original LDXR/STXR.
 check(satchel_guest_dispatch_build_stubs(NULL,0,&f->layout));uint8_t*stubs=malloc(f->layout.region_size);check(stubs!=NULL);
 check(satchel_guest_dispatch_build_stubs(stubs,f->layout.region_size,&f->layout));
 SatchelCPUProbeRegion regions[]={{CODE,code,4096,4096,5},{STUBS,stubs,f->layout.region_size,f->layout.region_size,5},{DATA,NULL,0,65536,3}};
 char error[128];f->cpu=satchel_cpu_probe_create(regions,3,CODE,error,sizeof(error));free(stubs);check(f->cpu!=NULL);
 f->d=satchel_guest_dispatch_create(BOOT,STUBS,1000,30000);check(f->d!=NULL);
 uint64_t points[2];check(satchel_guest_dispatch_breakpoints(f->d,points)&&satchel_cpu_probe_set_breakpoints(f->cpu,points,2));
 SatchelGuestDeviceBackendV2 backend={{2,0,f,create,destroy_device,capabilities},done};check(satchel_guest_dispatch_set_device_backend_v2(f->d,&backend));
 if(service){attaching=f;SatchelGuestCheckedPoolServices s={{1,legacy_create,release,execute,resolve,children,legacy_range},checked_create,checked_range};
 check(service==1?satchel_guest_dispatch_attach_pool_services(f->d,&backend,&s.pools):satchel_guest_dispatch_attach_checked_pool_services(f->d,&backend,&s));attaching=NULL;}
}
static void call(Fixture*f,const char*name,uint64_t x0,uint64_t x1){
 uint64_t target=0;for(unsigned i=1;i<=f->layout.stub_count;i++)if(!strcmp(name,satchel_guest_dispatch_name(i)))target=STUBS+16+(i-1)*8;
 check(target&&satchel_cpu_probe_set_pc(f->cpu,CODE)&&satchel_cpu_probe_set_x(f->cpu,9,target)&&satchel_cpu_probe_set_x(f->cpu,0,x0)&&satchel_cpu_probe_set_x(f->cpu,1,x1));
 SatchelCPUProbeResult stop=satchel_cpu_probe_run(f->cpu,16,NULL,NULL);check(stop.reason==SATCHEL_CPU_BREAKPOINT);SatchelGuestDispatchReport report;
 check(satchel_guest_dispatch_handle(f->d,f->cpu,&stop,NULL,NULL,&report)==SATCHEL_GUEST_DISPATCH_RETURNED);
}
static void drop(Fixture*f){check(satchel_guest_dispatch_handoff(&f->d)&&!f->d);satchel_cpu_probe_destroy(f->cpu);f->cpu=NULL;
 SatchelGuestRetirementReport r;check(satchel_guest_dispatch_retirement_poll(NULL,NULL,&r)==SATCHEL_GUEST_RETIRE_IDLE&&!r.retiring_contexts);}
static int during_run(void*p){Fixture*f=p;check(!satchel_guest_dispatch_check_output_range(f->d,f->cpu,DATA,8));return 1;}
static void*guard_thread(void*p){Fixture*f=p;return(void*)(uintptr_t)satchel_guest_dispatch_check_output_range(f->d,f->cpu,DATA,8);}
int main(void){
 Fixture f;make(&f,0);
 check(!satchel_guest_dispatch_check_output_range(NULL,f.cpu,DATA,8));check(!satchel_guest_dispatch_check_output_range(f.d,NULL,DATA,8));
 check(!satchel_guest_dispatch_check_output_range(f.d,f.cpu,DATA,0));check(!satchel_guest_dispatch_check_output_range(f.d,f.cpu,UINT64_MAX-2,8));
 check(!satchel_guest_dispatch_check_output_range(f.d,f.cpu,CODE,8));check(!satchel_guest_dispatch_check_output_range(f.d,f.cpu,DATA+65532,8));
 check(satchel_guest_dispatch_check_output_range(f.d,f.cpu,DATA+1,8));
 check(satchel_cpu_probe_run(f.cpu,1,during_run,&f).reason==SATCHEL_CPU_CANCELLED);
 call(&f,"nvnDeviceBuilderSetDefaults",DATA+256,0);call(&f,"nvnDeviceInitialize",DEVICE,DATA+256);
 for(unsigned i=0;i<SATCHEL_GUEST_DISPATCH_DEVICE_SIZE;i+=127)check(!satchel_guest_dispatch_check_output_range(f.d,f.cpu,DEVICE+i,8));
 check(!satchel_guest_dispatch_check_output_range(f.d,f.cpu,DEVICE-7,8));check(satchel_guest_dispatch_check_output_range(f.d,f.cpu,DEVICE-8,8));
 check(!satchel_guest_dispatch_check_output_range(f.d,f.cpu,DEVICE+SATCHEL_GUEST_DISPATCH_DEVICE_SIZE-1,8));
 check(satchel_guest_dispatch_check_output_range(f.d,f.cpu,DEVICE+SATCHEL_GUEST_DISPATCH_DEVICE_SIZE,8));
 check(satchel_cpu_probe_map_alias(f.cpu,ALIAS,DEVICE,4096)==SATCHEL_CPU_MAP_OK);
 check(!satchel_guest_dispatch_check_output_range(f.d,f.cpu,ALIAS+8,8));check(satchel_cpu_probe_unmap_alias(f.cpu,ALIAS,DEVICE,4096)==SATCHEL_CPU_MAP_OK);
 // Guard cannot consume registers/PC/bytes or the exclusive reservation.
 check(satchel_cpu_probe_set_pc(f.cpu,CODE+8)&&satchel_cpu_probe_set_x(f.cpu,0,DATA)&&satchel_cpu_probe_set_x(f.cpu,3,0x1234));
 check(satchel_cpu_probe_run(f.cpu,1,NULL,NULL).reason==SATCHEL_CPU_BUDGET);
 uint64_t regs[31],after;uint8_t vectors[32][16],vector[16],before[65536],bytes[65536];
 for(unsigned i=0;i<31;i++)check(satchel_cpu_probe_get_x(f.cpu,i,&regs[i]));for(unsigned i=0;i<32;i++)check(satchel_cpu_probe_get_q(f.cpu,i,vectors[i]));
 check(satchel_cpu_probe_read(f.cpu,DATA,before,sizeof(before)));
 check(satchel_guest_dispatch_check_output_range(f.d,f.cpu,DATA,8));check(!satchel_guest_dispatch_check_output_range(f.d,f.cpu,DEVICE,8));
 for(unsigned i=0;i<31;i++)check(satchel_cpu_probe_get_x(f.cpu,i,&after)&&after==regs[i]);for(unsigned i=0;i<32;i++)check(satchel_cpu_probe_get_q(f.cpu,i,vector)&&!memcmp(vector,vectors[i],16));
 check(satchel_cpu_probe_run(f.cpu,0,NULL,NULL).pc==CODE+12);check(satchel_cpu_probe_read(f.cpu,DATA,bytes,sizeof(bytes))&&!memcmp(bytes,before,sizeof(bytes)));
 check(satchel_cpu_probe_run(f.cpu,1,NULL,NULL).reason==SATCHEL_CPU_BUDGET);check(satchel_cpu_probe_get_x(f.cpu,2,&after)&&!after);
 call(&f,"nvnDeviceFinalize",DEVICE,0);check(satchel_guest_dispatch_check_output_range(f.d,f.cpu,DEVICE,8));drop(&f);
 // Failed initialization with failed backend cleanup retains a provisional
 // device slot; its original guest bytes are still protected until teardown.
 make(&f,0);f.invalid_caps=1;f.hold_device=1;
 call(&f,"nvnDeviceBuilderSetDefaults",DATA+256,0);
 uint64_t initialize=0;for(unsigned i=1;i<=f.layout.stub_count;i++)if(!strcmp(satchel_guest_dispatch_name(i),"nvnDeviceInitialize"))initialize=STUBS+16+(i-1)*8;
 check(initialize&&satchel_cpu_probe_set_pc(f.cpu,CODE)&&satchel_cpu_probe_set_x(f.cpu,9,initialize)&&satchel_cpu_probe_set_x(f.cpu,0,DEVICE)&&satchel_cpu_probe_set_x(f.cpu,1,DATA+256));
 SatchelCPUProbeResult failed=satchel_cpu_probe_run(f.cpu,16,NULL,NULL);check(failed.reason==SATCHEL_CPU_BREAKPOINT);SatchelGuestDispatchReport failed_report;
 check(satchel_guest_dispatch_handle(f.d,f.cpu,&failed,NULL,NULL,&failed_report)==SATCHEL_GUEST_DISPATCH_INTERNAL);
 check(satchel_guest_dispatch_device_count(f.d)==0); // Provisional is not initialized.
 check(!satchel_guest_dispatch_check_output_range(f.d,f.cpu,DEVICE,8));check(satchel_guest_dispatch_check_output_range(f.d,f.cpu,DATA,8));
 check(satchel_cpu_probe_map_alias(f.cpu,ALIAS,DEVICE,4096)==SATCHEL_CPU_MAP_OK);check(!satchel_guest_dispatch_check_output_range(f.d,f.cpu,ALIAS,8));
 check(satchel_cpu_probe_unmap_alias(f.cpu,ALIAS,DEVICE,4096)==SATCHEL_CPU_MAP_OK);f.hold_device=0;drop(&f);
 make(&f,1);check(!satchel_guest_dispatch_check_output_range(f.d,f.cpu,DATA,8)&&!f.calls);drop(&f);
 make(&f,2);f.child=1;f.reenter=1;
 check(satchel_guest_dispatch_check_output_range(f.d,f.cpu,DATA,8)&&f.calls==1);check(!satchel_guest_dispatch_check_output_range(f.d,f.cpu,CHILD,8));
 check(satchel_cpu_probe_map_alias(f.cpu,ALIAS,CHILD,4096)==SATCHEL_CPU_MAP_OK);check(!satchel_guest_dispatch_check_output_range(f.d,f.cpu,ALIAS,8));
 check(satchel_cpu_probe_unmap_alias(f.cpu,ALIAS,CHILD,4096)==SATCHEL_CPU_MAP_OK);
 f.reenter=0;pause_enabled=1;pthread_t thread;check(!pthread_create(&thread,NULL,guard_thread,&f));
 pthread_mutex_lock(&pause_lock);while(!entered)pthread_cond_wait(&pause_cond,&pause_lock);pthread_mutex_unlock(&pause_lock);
 check(!satchel_guest_dispatch_check_output_range(f.d,f.cpu,DATA,8));pthread_mutex_lock(&pause_lock);resume=1;pthread_cond_signal(&pause_cond);pthread_mutex_unlock(&pause_lock);
 void*returned;check(!pthread_join(thread,&returned)&&(uintptr_t)returned==1);pause_enabled=0;
 // A retained retirement owner refuses before consulting the freed CPU.
 f.hold=1;SatchelGuestDispatch*retired=f.d;check(satchel_guest_dispatch_handoff(&f.d)&&!f.d);satchel_cpu_probe_destroy(f.cpu);
 check(!satchel_guest_dispatch_check_output_range(retired,f.cpu,DATA,8));f.cpu=NULL;f.hold=0;
 SatchelGuestRetirementReport r;check(satchel_guest_dispatch_retirement_poll(NULL,NULL,&r)==SATCHEL_GUEST_RETIRE_IDLE&&!r.retiring_contexts);
 printf("Dispatch checked-output guard: %u assertions passed; synthetic backend/service only.\n",checks);
}
