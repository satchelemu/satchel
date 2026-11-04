#include "GuestDispatch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static unsigned checks;
static void check(int condition) {if(!condition){fprintf(stderr,"guest dispatch check %u failed\n",checks+1);exit(1);}++checks;}
static void put32(uint8_t *p,uint32_t value){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(value>>(8*i));}
static uint32_t u32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static const uint64_t CALLER=0x100000,BOOTSTRAP=0x100100,STUBS=0x200000,DATA=0x300000;
typedef struct {SatchelCPUProbe *cpu;SatchelGuestDispatch *dispatch;SatchelGuestDispatchLayout layout;} Fixture;
static Fixture make(unsigned permissions,unsigned event_limit,unsigned timeout) {
    Fixture f={0};uint8_t code[4096],data[4096];
    for(unsigned i=0;i<sizeof(code);i+=4)put32(code+i,0xd4200000);
    // Original tiny guest caller: BLR X9; SVC #0x42. The SVC only marks return
    // in this CPU test; it is never serviced as a fictional Horizon operation.
    put32(code,0xd63f0120);put32(code+4,0xd4000841);
    memset(data,0x7a,sizeof(data));
    check(satchel_guest_dispatch_build_stubs(NULL,0,&f.layout));
    uint8_t *stubs=malloc(f.layout.region_size);check(stubs!=NULL);
    check(satchel_guest_dispatch_build_stubs(stubs,f.layout.region_size,&f.layout));
    SatchelCPUProbeRegion regions[]={
        {CALLER,code,sizeof(code),sizeof(code),SATCHEL_CPU_READ|SATCHEL_CPU_EXECUTE},
        {STUBS,stubs,f.layout.region_size,f.layout.region_size,SATCHEL_CPU_READ|SATCHEL_CPU_EXECUTE},
        {DATA,data,sizeof(data),sizeof(data),permissions}};
    char error[160]={0};f.cpu=satchel_cpu_probe_create(regions,3,CALLER,error,sizeof(error));free(stubs);check(f.cpu!=NULL);
    f.dispatch=satchel_guest_dispatch_create(BOOTSTRAP,STUBS,event_limit,timeout);check(f.dispatch!=NULL);
    uint64_t points[2];check(satchel_guest_dispatch_breakpoints(f.dispatch,points));check(satchel_cpu_probe_set_breakpoints(f.cpu,points,2));
    return f;
}
static void destroy(Fixture f){satchel_guest_dispatch_destroy(f.dispatch);satchel_cpu_probe_destroy(f.cpu);}
static void name(Fixture *f,const char *text){check(satchel_cpu_probe_write(f->cpu,DATA,text,strlen(text)+1));}
static uint64_t reg(Fixture *f,unsigned r){uint64_t value;check(satchel_cpu_probe_get_x(f->cpu,r,&value));return value;}
static SatchelCPUProbeResult call(Fixture *f,uint64_t target,uint64_t x0,uint64_t x1){
    check(satchel_cpu_probe_set_pc(f->cpu,CALLER));check(satchel_cpu_probe_set_x(f->cpu,9,target));check(satchel_cpu_probe_set_x(f->cpu,0,x0));check(satchel_cpu_probe_set_x(f->cpu,1,x1));
    SatchelCPUProbeResult stop=satchel_cpu_probe_run(f->cpu,100,NULL,NULL);check(stop.reason==SATCHEL_CPU_BREAKPOINT);return stop;
}
static SatchelGuestDispatchStatus handle(Fixture *f,SatchelCPUProbeResult *stop,SatchelGuestDispatchReport *report){return satchel_guest_dispatch_handle(f->dispatch,f->cpu,stop,NULL,NULL,report);}
static uint64_t resolve(Fixture *f,const char *text){
    name(f,text);SatchelCPUProbeResult stop=call(f,BOOTSTRAP,DATA,0);check(stop.pc==BOOTSTRAP && stop.steps==1);
    SatchelGuestDispatchReport report;check(handle(f,&stop,&report)==SATCHEL_GUEST_DISPATCH_RETURNED);check(report.operation_id==0);
    uint64_t address=reg(f,0);check(address>=STUBS+16 && address<STUBS+f->layout.region_size);
    SatchelCPUProbeResult returned=satchel_cpu_probe_run(f->cpu,10,NULL,NULL);check(returned.reason==SATCHEL_CPU_SVC && returned.svc==0x42);
    return address;
}
static int cancelled(void *context){(void)context;return 1;}
int main(void){
    SatchelGuestDispatchLayout layout={0};uint8_t tiny[8];memset(tiny,0xaa,sizeof(tiny));
    check(!satchel_guest_dispatch_build_stubs(tiny,sizeof(tiny),&layout));check(tiny[0]==0xaa);
    check(satchel_guest_dispatch_build_stubs(NULL,0,&layout));check(layout.stub_count==537 && layout.region_size==8192 && layout.trap_offset==0);
    check(!satchel_guest_dispatch_create(BOOTSTRAP,STUBS,0,1000));check(!satchel_guest_dispatch_create(BOOTSTRAP,STUBS,1,0));
    check(!satchel_guest_dispatch_create(STUBS+4,STUBS,1,1000));check(!satchel_guest_dispatch_create(BOOTSTRAP+1,STUBS,1,1000));
    check(satchel_guest_dispatch_name(0)!=NULL && satchel_guest_dispatch_name(538)==NULL);
    Fixture f=make(SATCHEL_CPU_READ|SATCHEL_CPU_WRITE,10000,30000);
    uint64_t defaults=resolve(&f,"nvnDeviceBuilderSetDefaults"),flags=resolve(&f,"nvnDeviceBuilderSetFlags"),getproc=resolve(&f,"nvnDeviceGetProcAddress"),initialize=resolve(&f,"nvnDeviceInitialize");
    uint8_t vector[16];for(unsigned i=0;i<16;i++)vector[i]=(uint8_t)(i*7);check(satchel_cpu_probe_set_q(f.cpu,8,vector));check(satchel_cpu_probe_set_x(f.cpu,19,0x12345678));
    SatchelGuestDispatchReport report;SatchelCPUProbeResult stop=call(&f,defaults,DATA+513,0);check(stop.pc==STUBS && stop.steps==3);
    check(handle(&f,&stop,&report)==SATCHEL_GUEST_DISPATCH_RETURNED);check(reg(&f,0)==DATA+513 && reg(&f,19)==0x12345678);
    uint8_t copied[16];check(satchel_cpu_probe_get_q(f.cpu,8,copied) && !memcmp(copied,vector,16));
    uint8_t object[66];check(satchel_cpu_probe_read(f.cpu,DATA+512,object,sizeof(object)));
    check(object[0]==0x7a && object[65]==0x7a && u32(object+1)==0x504c4442 && u32(object+5)==0 && u32(object+9)==1);
    for(unsigned i=13;i<65;i++)check(object[i]==0);
    stop=call(&f,flags,DATA+513,UINT64_C(0x1122334489abcdef));check(handle(&f,&stop,&report)==SATCHEL_GUEST_DISPATCH_RETURNED);
    check(satchel_cpu_probe_read(f.cpu,DATA+512,object,sizeof(object)) && u32(object+5)==0x89abcdef && object[0]==0x7a && object[65]==0x7a);
    name(&f,"nvnDeviceBuilderSetDefaults");stop=call(&f,getproc,0,DATA);check(handle(&f,&stop,&report)==SATCHEL_GUEST_DISPATCH_RETURNED && reg(&f,0)==defaults);
    stop=call(&f,initialize,DATA+513,DATA+1024);check(handle(&f,&stop,&report)==SATCHEL_GUEST_DISPATCH_BACKEND_UNAVAILABLE);
    check(satchel_cpu_probe_run(f.cpu,1,NULL,NULL).reason==SATCHEL_CPU_BREAKPOINT && reg(&f,0)==DATA+513);
    uint64_t integer=resolve(&f,"nvnDeviceGetInteger");stop=call(&f,integer,DATA+513,0);check(handle(&f,&stop,&report)==SATCHEL_GUEST_DISPATCH_UNSUPPORTED_QUERY);
    stop=call(&f,flags,DATA+2048,5);check(handle(&f,&stop,&report)==SATCHEL_GUEST_DISPATCH_BAD_OBJECT);
    stop=call(&f,defaults,DATA+513,0);check(satchel_cpu_probe_set_x(f.cpu,30,0xdeadbeef));uint8_t before[64];check(satchel_cpu_probe_read(f.cpu,DATA+513,before,64));
    check(handle(&f,&stop,&report)==SATCHEL_GUEST_DISPATCH_BAD_RETURN);check(satchel_cpu_probe_read(f.cpu,DATA+513,object,64) && !memcmp(before,object,64));
    check(satchel_cpu_probe_run(f.cpu,1,NULL,NULL).pc==STUBS);
    name(&f,"nvnDefinitelyNotAnAPI");stop=call(&f,BOOTSTRAP,DATA,0);check(handle(&f,&stop,&report)==SATCHEL_GUEST_DISPATCH_UNKNOWN_NAME && reg(&f,0)==DATA);
    stop=call(&f,BOOTSTRAP,UINT64_MAX-4,0);check(handle(&f,&stop,&report)==SATCHEL_GUEST_DISPATCH_MEMORY_FAULT);
    char long_name[128];memset(long_name,'N',sizeof(long_name));check(satchel_cpu_probe_write(f.cpu,DATA,long_name,sizeof(long_name)));stop=call(&f,BOOTSTRAP,DATA,0);check(handle(&f,&stop,&report)==SATCHEL_GUEST_DISPATCH_UNKNOWN_NAME);
    stop=call(&f,defaults,DATA+4080,0);check(handle(&f,&stop,&report)==SATCHEL_GUEST_DISPATCH_MEMORY_FAULT);
    stop=call(&f,defaults,DATA+513,0);check(satchel_cpu_probe_set_x(f.cpu,16,538));check(handle(&f,&stop,&report)==SATCHEL_GUEST_DISPATCH_INVALID);
    stop=call(&f,defaults,DATA+513,0);check(satchel_guest_dispatch_handle(f.dispatch,f.cpu,&stop,cancelled,NULL,&report)==SATCHEL_GUEST_DISPATCH_CANCELLED);
    check(satchel_cpu_probe_run(f.cpu,1,NULL,NULL).pc==STUBS);
    // A stale stop cannot dispatch after the CPU has already moved elsewhere.
    check(satchel_cpu_probe_set_pc(f.cpu,CALLER));check(handle(&f,&stop,&report)==SATCHEL_GUEST_DISPATCH_INVALID);
    // Every original slot executes two guest instructions into the one trap,
    // preserving the caller's LR while exposing only a bounded operation ID.
    for(uint32_t i=0;i<layout.stub_count;i++) {
        stop=call(&f,STUBS+layout.first_stub_offset+(uint64_t)i*layout.stub_stride,0,0);
        check(stop.pc==STUBS && stop.steps==3 && reg(&f,16)==i+1 && reg(&f,30)==CALLER+4);
    }
    destroy(f);
    f=make(SATCHEL_CPU_READ,10,1000);stop=call(&f,defaults,DATA+512,0);check(handle(&f,&stop,&report)==SATCHEL_GUEST_DISPATCH_MEMORY_FAULT);
    check(satchel_cpu_probe_run(f.cpu,1,NULL,NULL).pc==STUBS && reg(&f,0)==DATA+512);
    check(satchel_cpu_probe_read(f.cpu,DATA+512,object,64));for(unsigned i=0;i<64;i++)check(object[i]==0x7a);destroy(f);
    // Zero-CPU-step callback-return loop is still bounded by dispatch events.
    f=make(SATCHEL_CPU_READ|SATCHEL_CPU_WRITE,2,1000);name(&f,"nvnDeviceBuilderSetDefaults");stop=call(&f,getproc,0,DATA);check(satchel_cpu_probe_set_x(f.cpu,30,STUBS));
    check(handle(&f,&stop,&report)==SATCHEL_GUEST_DISPATCH_RETURNED);
    stop=satchel_cpu_probe_run(f.cpu,100,NULL,NULL);check(stop.reason==SATCHEL_CPU_BREAKPOINT && stop.steps==0);
    check(handle(&f,&stop,&report)==SATCHEL_GUEST_DISPATCH_RETURNED);
    stop=satchel_cpu_probe_run(f.cpu,100,NULL,NULL);check(handle(&f,&stop,&report)==SATCHEL_GUEST_DISPATCH_LIMIT && report.events==2);destroy(f);
    f=make(SATCHEL_CPU_READ|SATCHEL_CPU_WRITE,10,1);stop=call(&f,defaults,DATA+512,0);struct timespec pause={0,3000000};nanosleep(&pause,NULL);
    check(handle(&f,&stop,&report)==SATCHEL_GUEST_DISPATCH_LIMIT);destroy(f);
    printf("Guest dispatch: %u sanitizer-backed original interpreter checks passed; no game graphics executed.\n",checks);
}
