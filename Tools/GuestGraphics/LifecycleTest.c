#include "GuestDispatch.h"
#include "VulkanDevice.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
static void check(int good){if(!good){fprintf(stderr,"guest Vulkan lifecycle check%u failed\n",checks+1);exit(1);}++checks;}
static const uint64_t CALLER=0x100000,BOOTSTRAP=0x100100,STUBS=0x200000,DATA=0x300000,BUILDER=0x300100,DEVICE=0x301000;
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(i*8));}
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t get64(const uint8_t *p){return get32(p)|(uint64_t)get32(p+4)<<32;}
typedef struct{SatchelCPUProbe *cpu;SatchelGuestDispatch *dispatch;SatchelGuestDispatchLayout layout;} Fixture;
static Fixture make(unsigned permission){
    Fixture f={0};uint8_t code[4096];uint8_t *data=malloc(160*1024);check(data!=NULL);memset(data,0x7a,160*1024);
    memset(data+256,0,64);put32(data+256,0x504c4442);put32(data+264,1);
    for(unsigned i=0;i<sizeof(code);i+=4)put32(code+i,0xd4200000);put32(code,0xd63f0120);put32(code+4,0xd4000841);
    check(satchel_guest_dispatch_build_stubs(NULL,0,&f.layout));uint8_t *stubs=malloc(f.layout.region_size);check(stubs!=NULL);
    check(satchel_guest_dispatch_build_stubs(stubs,f.layout.region_size,&f.layout));
    SatchelCPUProbeRegion regions[]={{CALLER,code,sizeof(code),sizeof(code),5},{STUBS,stubs,f.layout.region_size,f.layout.region_size,5},{DATA,data,160*1024,160*1024,permission}};
    char error[160]={0};f.cpu=satchel_cpu_probe_create(regions,3,CALLER,error,sizeof(error));free(stubs);free(data);check(f.cpu!=NULL);
    f.dispatch=satchel_guest_dispatch_create(BOOTSTRAP,STUBS,10000,30000);check(f.dispatch!=NULL);
    SatchelGuestDeviceBackend backend={0};check(satchel_vulkan_guest_device_backend(&backend));check(satchel_guest_dispatch_set_device_backend(f.dispatch,&backend));
    check(!satchel_guest_dispatch_set_device_backend(f.dispatch,&backend));
    uint64_t points[2];check(satchel_guest_dispatch_breakpoints(f.dispatch,points));check(satchel_cpu_probe_set_breakpoints(f.cpu,points,2));return f;
}
static void destroy(Fixture f){satchel_guest_dispatch_destroy(f.dispatch);satchel_cpu_probe_destroy(f.cpu);}
static uint64_t stub(const Fixture *f,const char *name){for(uint32_t i=1;i<=f->layout.stub_count;i++)if(!strcmp(name,satchel_guest_dispatch_name(i)))return STUBS+16+(uint64_t)(i-1)*8;check(0);return 0;}
static uint64_t reg(Fixture *f,unsigned index){uint64_t value=0;check(satchel_cpu_probe_get_x(f->cpu,index,&value));return value;}
static SatchelCPUProbeResult call(Fixture *f,const char *name,uint64_t x0,uint64_t x1){
    check(satchel_cpu_probe_set_pc(f->cpu,CALLER));check(satchel_cpu_probe_set_x(f->cpu,9,stub(f,name)));check(satchel_cpu_probe_set_x(f->cpu,0,x0));check(satchel_cpu_probe_set_x(f->cpu,1,x1));
    SatchelCPUProbeResult stop=satchel_cpu_probe_run(f->cpu,100,NULL,NULL);check(stop.reason==SATCHEL_CPU_BREAKPOINT && stop.steps==3 && stop.pc==STUBS);return stop;
}
static SatchelGuestDispatchStatus handle(Fixture *f,SatchelCPUProbeResult *stop){SatchelGuestDispatchReport report={0};return satchel_guest_dispatch_handle(f->dispatch,f->cpu,stop,NULL,NULL,&report);}
int main(void){
    check(satchel_vulkan_device_live_count()==0);Fixture f=make(3);SatchelCPUProbeResult stop;
    uint8_t old[SATCHEL_GUEST_DISPATCH_DEVICE_SIZE],current[SATCHEL_GUEST_DISPATCH_DEVICE_SIZE];
    check(satchel_cpu_probe_read(f.cpu,DEVICE,old,sizeof(old)));
    for(unsigned bit=0;bit<32;bit++){
        stop=call(&f,"nvnDeviceBuilderSetFlags",BUILDER,UINT64_C(1)<<bit);check(handle(&f,&stop)==SATCHEL_GUEST_DISPATCH_RETURNED);
        stop=call(&f,"nvnDeviceInitialize",DEVICE,BUILDER);check(handle(&f,&stop)==SATCHEL_GUEST_DISPATCH_UNSUPPORTED_FLAGS && satchel_vulkan_device_live_count()==0);
    }
    stop=call(&f,"nvnDeviceBuilderSetDefaults",BUILDER,0);check(handle(&f,&stop)==SATCHEL_GUEST_DISPATCH_RETURNED);
    stop=call(&f,"nvnDeviceInitialize",DEVICE,BUILDER);check(satchel_cpu_probe_set_x(f.cpu,30,1));check(handle(&f,&stop)==SATCHEL_GUEST_DISPATCH_BAD_RETURN && satchel_vulkan_device_live_count()==0);
    check(satchel_cpu_probe_read(f.cpu,DEVICE,current,sizeof(current)) && !memcmp(old,current,sizeof(old)));
    stop=call(&f,"nvnDeviceInitialize",DEVICE,BUILDER);check(handle(&f,&stop)==SATCHEL_GUEST_DISPATCH_RETURNED && reg(&f,0)==1);
    check(satchel_vulkan_device_live_count()==1 && satchel_guest_dispatch_device_count(f.dispatch)==1);
    check(satchel_cpu_probe_run(f.cpu,10,NULL,NULL).reason==SATCHEL_CPU_SVC);
    SatchelGuestDeviceCaps caps={0};check(satchel_guest_dispatch_device_capabilities(f.dispatch,DEVICE,&caps) && caps.version==1 && caps.api_version && caps.max_compute_invocations && caps.portability_subset==1 && caps.enabled_features==0);
    check(satchel_cpu_probe_read(f.cpu,DEVICE,current,sizeof(current)) && get32(current)==0x504c4456 && get64(current+8)==1);
    uint8_t before_guard=0,after_guard=0;check(satchel_cpu_probe_read(f.cpu,DEVICE-1,&before_guard,1) && satchel_cpu_probe_read(f.cpu,DEVICE+sizeof(current),&after_guard,1) && before_guard==0x7a && after_guard==0x7a);
    // Original interpreted GetInteger calls use W1 selectors and checked X2 int*.
    const uint64_t output=DATA+513;uint8_t guarded[6],unchanged[6];memset(guarded,0xa5,sizeof(guarded));
    const uint32_t limits[]={caps.max_compute_group_size[0],caps.max_compute_group_size[1],caps.max_compute_group_size[2],
        caps.max_compute_invocations,caps.max_compute_group_count[0],caps.max_compute_group_count[1],caps.max_compute_group_count[2]};
    for(unsigned i=0;i<7;i++){
        check(satchel_cpu_probe_write(f.cpu,output-1,guarded,sizeof(guarded)));
        stop=call(&f,"nvnDeviceGetInteger",DEVICE,UINT64_C(0xfedcba9800000000)|(0x1c+i));check(satchel_cpu_probe_set_x(f.cpu,2,output));
        check(handle(&f,&stop)==SATCHEL_GUEST_DISPATCH_RETURNED && reg(&f,0)==DEVICE && reg(&f,2)==output);
        check(satchel_cpu_probe_read(f.cpu,output-1,unchanged,sizeof(unchanged)) && unchanged[0]==0xa5 && unchanged[5]==0xa5 && get32(unchanged+1)==limits[i]);
        check(satchel_cpu_probe_run(f.cpu,10,NULL,NULL).reason==SATCHEL_CPU_SVC);
    }
    // No fabricated SDK/compiler versions or generic zero for unknown selectors.
    check(satchel_cpu_probe_write(f.cpu,output-1,guarded,sizeof(guarded)));
    for(uint32_t selector=0;selector<0x68;selector++)if(selector<0x1c || selector>0x22){
        stop=call(&f,"nvnDeviceGetInteger",DEVICE,selector);check(satchel_cpu_probe_set_x(f.cpu,2,output));
        check(handle(&f,&stop)==SATCHEL_GUEST_DISPATCH_UNSUPPORTED_QUERY && reg(&f,0)==DEVICE);
        check(satchel_cpu_probe_read(f.cpu,output-1,unchanged,sizeof(unchanged)) && !memcmp(guarded,unchanged,sizeof(guarded)));
        check(satchel_cpu_probe_run(f.cpu,0,NULL,NULL).pc==STUBS);
    }
    const uint64_t invalid_outputs[]={CALLER+256,DATA+160*1024-3,UINT64_MAX-1,0};
    for(unsigned i=0;i<sizeof(invalid_outputs)/sizeof(invalid_outputs[0]);i++){
        stop=call(&f,"nvnDeviceGetInteger",DEVICE,0x1c);check(satchel_cpu_probe_set_x(f.cpu,2,invalid_outputs[i]));
        check(handle(&f,&stop)==SATCHEL_GUEST_DISPATCH_MEMORY_FAULT && reg(&f,0)==DEVICE && satchel_cpu_probe_run(f.cpu,0,NULL,NULL).pc==STUBS);
    }
    stop=call(&f,"nvnDeviceGetInteger",DEVICE,0x1c);check(satchel_cpu_probe_set_x(f.cpu,2,output));check(satchel_cpu_probe_set_x(f.cpu,30,1));
    check(handle(&f,&stop)==SATCHEL_GUEST_DISPATCH_BAD_RETURN && reg(&f,0)==DEVICE);
    check(satchel_cpu_probe_read(f.cpu,output-1,unchanged,sizeof(unchanged)) && !memcmp(guarded,unchanged,sizeof(guarded)));
    stop=call(&f,"nvnDeviceGetInteger",DEVICE,0x1c);check(satchel_cpu_probe_set_x(f.cpu,2,DEVICE+8));
    check(handle(&f,&stop)==SATCHEL_GUEST_DISPATCH_BAD_OBJECT);
    stop=call(&f,"nvnDeviceGetInteger",0,0x1c);check(satchel_cpu_probe_set_x(f.cpu,2,output));check(handle(&f,&stop)==SATCHEL_GUEST_DISPATCH_BAD_OBJECT);
    stop=call(&f,"nvnDeviceInitialize",DEVICE,BUILDER);check(handle(&f,&stop)==SATCHEL_GUEST_DISPATCH_BAD_OBJECT && satchel_vulkan_device_live_count()==1);
    stop=call(&f,"nvnDeviceInitialize",DEVICE+4,BUILDER);check(handle(&f,&stop)==SATCHEL_GUEST_DISPATCH_BAD_OBJECT && satchel_vulkan_device_live_count()==1);
    stop=call(&f,"nvnDeviceFinalize",DEVICE,0);check(satchel_cpu_probe_set_x(f.cpu,30,1));check(handle(&f,&stop)==SATCHEL_GUEST_DISPATCH_BAD_RETURN && satchel_vulkan_device_live_count()==1);
    stop=call(&f,"nvnDeviceFinalize",DEVICE,0);check(handle(&f,&stop)==SATCHEL_GUEST_DISPATCH_RETURNED && reg(&f,0)==DEVICE && satchel_vulkan_device_live_count()==0);
    uint8_t zero[SATCHEL_GUEST_DISPATCH_DEVICE_SIZE]={0};check(satchel_cpu_probe_read(f.cpu,DEVICE,old,sizeof(old)) && !memcmp(old,zero,sizeof(old)));
    stop=call(&f,"nvnDeviceFinalize",DEVICE,0);check(handle(&f,&stop)==SATCHEL_GUEST_DISPATCH_BAD_OBJECT);
    // Reuse gets a new guest generation; replaying an old object cannot destroy it.
    stop=call(&f,"nvnDeviceInitialize",DEVICE,BUILDER);check(handle(&f,&stop)==SATCHEL_GUEST_DISPATCH_RETURNED);
    check(satchel_cpu_probe_read(f.cpu,DEVICE,old,sizeof(old)) && get64(old+8)==2);
    check(satchel_cpu_probe_write(f.cpu,DEVICE,current,sizeof(current)));
    stop=call(&f,"nvnDeviceGetInteger",DEVICE,0x1c);check(satchel_cpu_probe_set_x(f.cpu,2,output));check(handle(&f,&stop)==SATCHEL_GUEST_DISPATCH_BAD_OBJECT);
    stop=call(&f,"nvnDeviceFinalize",DEVICE,0);check(handle(&f,&stop)==SATCHEL_GUEST_DISPATCH_BAD_OBJECT && satchel_vulkan_device_live_count()==1);
    check(satchel_cpu_probe_write(f.cpu,DEVICE,old,sizeof(old)));
    destroy(f);check(satchel_vulkan_device_live_count()==0); /* Teardown owns outstanding device. */
    f=make(1);check(satchel_cpu_probe_read(f.cpu,DEVICE,old,sizeof(old)));
    stop=call(&f,"nvnDeviceInitialize",DEVICE,BUILDER);check(handle(&f,&stop)==SATCHEL_GUEST_DISPATCH_MEMORY_FAULT);
    check(satchel_cpu_probe_run(f.cpu,1,NULL,NULL).pc==STUBS && reg(&f,0)==DEVICE && satchel_vulkan_device_live_count()==0 && satchel_guest_dispatch_device_count(f.dispatch)==0);
    check(satchel_cpu_probe_read(f.cpu,DEVICE,current,sizeof(current)) && !memcmp(old,current,sizeof(old)));destroy(f);
    f=make(3);
    for(unsigned i=0;i<SATCHEL_GUEST_DEVICE_REGISTRY_LIMIT;i++){
        stop=call(&f,"nvnDeviceInitialize",DEVICE+(uint64_t)i*(SATCHEL_GUEST_DISPATCH_DEVICE_SIZE+64),BUILDER);
        check(handle(&f,&stop)==SATCHEL_GUEST_DISPATCH_RETURNED && satchel_vulkan_device_live_count()==i+1);
    }
    stop=call(&f,"nvnDeviceInitialize",DEVICE+8*(SATCHEL_GUEST_DISPATCH_DEVICE_SIZE+64),BUILDER);check(handle(&f,&stop)==SATCHEL_GUEST_DISPATCH_DEVICE_LIMIT && satchel_vulkan_device_live_count()==8);
    destroy(f);check(satchel_vulkan_device_live_count()==0);
    printf("Original interpreted NVN stubs -> persistent real Vulkan lifecycle: %u ASan/UBSan checks passed; no game shader/frame submitted.\n",checks);
}
