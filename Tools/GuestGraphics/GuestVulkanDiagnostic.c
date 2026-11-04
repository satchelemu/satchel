#include "GuestVulkanDiagnostic.h"
#include "GuestDispatch.h"
#include "VulkanDevice.h"
#include <stdlib.h>
#include <string.h>
static const uint64_t CALLER=0x100000,BOOTSTRAP=0x100100,STUBS=0x200000,DATA=0x300000,BUILDER=0x300100,DEVICE=0x301000;
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint32_t get32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int invoke(SatchelCPUProbe *cpu,SatchelGuestDispatch *dispatch,uint64_t target,uint64_t x0,uint64_t x1,
    SatchelGuestVulkanCancelled cancel,void *context,SatchelGuestVulkanDiagnostic *report,uint64_t *value){
    if(!satchel_cpu_probe_set_pc(cpu,CALLER) || !satchel_cpu_probe_set_x(cpu,9,target) ||
        !satchel_cpu_probe_set_x(cpu,0,x0) || !satchel_cpu_probe_set_x(cpu,1,x1))return 0;
    SatchelCPUProbeResult stop=satchel_cpu_probe_run(cpu,32,cancel,context);report->instructions+=stop.steps;report->cpu_stop_reason=stop.reason;
    if(stop.reason!=SATCHEL_CPU_BREAKPOINT)return 0;
    SatchelGuestDispatchReport result={0};
    const SatchelGuestDispatchStatus status=satchel_guest_dispatch_handle(dispatch,cpu,&stop,cancel,context,&result);
    report->dispatch_status=status;report->dispatch_events=result.events;
    if(status!=SATCHEL_GUEST_DISPATCH_RETURNED)return 0;
    stop=satchel_cpu_probe_run(cpu,8,cancel,context);report->instructions+=stop.steps;report->cpu_stop_reason=stop.reason;
    return stop.reason==SATCHEL_CPU_SVC && stop.svc==0x42 && satchel_cpu_probe_get_x(cpu,0,value);
}
static int resolve(SatchelCPUProbe *cpu,SatchelGuestDispatch *dispatch,const char *name,SatchelGuestVulkanCancelled cancel,
    void *context,SatchelGuestVulkanDiagnostic *report,uint64_t *address){
    return satchel_cpu_probe_write(cpu,DATA,name,strlen(name)+1) && invoke(cpu,dispatch,BOOTSTRAP,DATA,0,cancel,context,report,address);
}
int satchel_guest_vulkan_diagnostic(SatchelGuestVulkanDiagnostic *report,SatchelGuestVulkanCancelled cancel,void *context){
    if(!report)return 0;memset(report,0,sizeof(*report));report->status=1;report->stage=1;
    if(cancel && cancel(context))return 0;
    report->baseline_devices=satchel_vulkan_device_live_count();
    if(report->baseline_devices>=SATCHEL_GUEST_DEVICE_REGISTRY_LIMIT)return 0;
    SatchelCPUProbe *cpu=NULL;SatchelGuestDispatch *dispatch=NULL;
    uint8_t *data=malloc(65536),*stubs=NULL;SatchelGuestDispatchLayout layout={0};
    uint8_t code[4096],vector[16],actual_vector[16],object[SATCHEL_GUEST_DISPATCH_DEVICE_SIZE],before=0,after=0;
    uint64_t target=0,value=0,callee=0;SatchelGuestDeviceCaps caps={0};SatchelGuestDeviceBackend backend={0};char error[160]={0};
    if(!data || !satchel_guest_dispatch_build_stubs(NULL,0,&layout))goto done;
    stubs=malloc(layout.region_size);if(!stubs || !satchel_guest_dispatch_build_stubs(stubs,layout.region_size,&layout))goto done;
    for(unsigned i=0;i<sizeof(code);i+=4)put32(code+i,0xd4200000);
    put32(code,0xd63f0120);put32(code+4,0xd4000841); /* Original BLR X9; SVC #0x42 return marker. */
    memset(data,0x7a,65536);for(unsigned i=0;i<16;i++)vector[i]=(uint8_t)(i*7);
    SatchelCPUProbeRegion regions[]={{CALLER,code,sizeof(code),sizeof(code),5},{STUBS,stubs,layout.region_size,layout.region_size,5},{DATA,data,65536,65536,3}};
    cpu=satchel_cpu_probe_create(regions,3,CALLER,error,sizeof(error));if(!cpu)goto done;
    report->stage=2;dispatch=satchel_guest_dispatch_create(BOOTSTRAP,STUBS,1024,10000);if(!dispatch)goto done;
    if(!satchel_vulkan_guest_device_backend(&backend) || !satchel_guest_dispatch_set_device_backend(dispatch,&backend))goto done;
    uint64_t points[2];if(!satchel_guest_dispatch_breakpoints(dispatch,points) || !satchel_cpu_probe_set_breakpoints(cpu,points,2))goto done;
    if(!satchel_cpu_probe_set_q(cpu,8,vector) || !satchel_cpu_probe_set_x(cpu,19,0x23456789))goto done;
    report->stage=3;
    if(!resolve(cpu,dispatch,"nvnDeviceBuilderSetDefaults",cancel,context,report,&target) ||
        !invoke(cpu,dispatch,target,BUILDER,0,cancel,context,report,&value) || value!=BUILDER)goto done;
    report->stage=4;
    if(!resolve(cpu,dispatch,"nvnDeviceInitialize",cancel,context,report,&target) ||
        !invoke(cpu,dispatch,target,DEVICE,BUILDER,cancel,context,report,&value) || value!=1)goto done;
    report->device_created=satchel_guest_dispatch_device_count(dispatch)==1 && satchel_vulkan_device_live_count()==report->baseline_devices+1;
    report->stage=5;
    if(!report->device_created || !satchel_guest_dispatch_device_capabilities(dispatch,DEVICE,&caps) || caps.version!=1 || !caps.api_version ||
        !caps.max_compute_invocations || !caps.portability_subset)goto done;
    report->api_version=caps.api_version;
    if(!resolve(cpu,dispatch,"nvnDeviceGetInteger",cancel,context,report,&target))goto done;
    const uint32_t expected[]={caps.max_compute_group_size[0],caps.max_compute_group_size[1],caps.max_compute_group_size[2],
        caps.max_compute_invocations,caps.max_compute_group_count[0],caps.max_compute_group_count[1],caps.max_compute_group_count[2]};
    for(unsigned i=0;i<7;i++){
        uint8_t query[6];memset(query,0xa5,sizeof(query));
        if(!satchel_cpu_probe_write(cpu,DATA+512,query,sizeof(query)) || !satchel_cpu_probe_set_x(cpu,2,DATA+513) ||
            !invoke(cpu,dispatch,target,DEVICE,0x1c+i,cancel,context,report,&value) || value!=DEVICE ||
            !satchel_cpu_probe_read(cpu,DATA+512,query,sizeof(query)) || query[0]!=0xa5 || query[5]!=0xa5 || get32(query+1)!=expected[i])goto done;
        ++report->compute_queries;
    }
    if(!satchel_cpu_probe_read(cpu,DEVICE-1,&before,1) || !satchel_cpu_probe_read(cpu,DEVICE+sizeof(object),&after,1))goto done;
    report->guards_matched=before==0x7a && after==0x7a;if(!report->guards_matched)goto done;
    report->stage=6;
    if(!resolve(cpu,dispatch,"nvnDeviceFinalize",cancel,context,report,&target) ||
        !invoke(cpu,dispatch,target,DEVICE,0,cancel,context,report,&value) || value!=DEVICE)goto done;
    report->device_finalized=satchel_guest_dispatch_device_count(dispatch)==0 && satchel_vulkan_device_live_count()==report->baseline_devices;
    if(!report->device_finalized || !satchel_cpu_probe_read(cpu,DEVICE,object,sizeof(object)))goto done;
    for(unsigned i=0;i<sizeof(object);i++)if(object[i])goto done;
    if(!satchel_cpu_probe_read(cpu,DEVICE-1,&before,1) || !satchel_cpu_probe_read(cpu,DEVICE+sizeof(object),&after,1))goto done;
    report->guards_matched=report->guards_matched && before==0x7a && after==0x7a;
    report->state_preserved=satchel_cpu_probe_get_x(cpu,19,&callee) && callee==0x23456789 &&
        satchel_cpu_probe_get_q(cpu,8,actual_vector) && !memcmp(vector,actual_vector,sizeof(vector));
    if(!report->guards_matched || !report->state_preserved)goto done;
    report->stage=7;report->status=0;
 done:
    satchel_guest_dispatch_destroy(dispatch);satchel_cpu_probe_destroy(cpu);free(stubs);free(data);
    report->final_devices=satchel_vulkan_device_live_count();report->cleanup_matched=report->final_devices==report->baseline_devices;
    if(!report->cleanup_matched)report->status=2;
    return report->status==0;
}
