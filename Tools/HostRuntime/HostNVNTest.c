#include "HostNVN.h"
#include "VulkanDevice.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static unsigned checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"Host glue line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
enum{CALL=0x100000,BOOT=0x100100,STUB=0x200000,DATA=0x300000};
static SatchelCPUProbe *make(void){SatchelGuestDispatchLayout l;CHECK(satchel_guest_dispatch_build_stubs(NULL,0,&l));uint8_t *b=malloc(l.region_size);CHECK(b&&satchel_guest_dispatch_build_stubs(b,l.region_size,&l));uint32_t code[]={0xd63f0120,0xd4000841};SatchelCPUProbeRegion r[]={{CALL,code,sizeof(code),4096,5},{STUB,b,l.region_size,l.region_size,5},{DATA,NULL,0,65536,3}};char e[160];SatchelCPUProbe *p=satchel_cpu_probe_create(r,3,CALL,e,sizeof(e));free(b);CHECK(p);uint64_t points[]={BOOT,STUB};CHECK(satchel_cpu_probe_set_breakpoints(p,points,2));return p;}
static SatchelCPUProbeResult stop(SatchelCPUProbe *p,uint64_t target){CHECK(satchel_cpu_probe_set_pc(p,CALL));CHECK(satchel_cpu_probe_set_x(p,9,target));SatchelCPUProbeResult r=satchel_cpu_probe_run(p,32,NULL,NULL);CHECK(r.reason==SATCHEL_CPU_BREAKPOINT);return r;}
static SatchelHostNVN *owner(unsigned events,uint32_t ms,uint64_t end){SatchelHostNVN *o=satchel_host_nvn_create(BOOT,STUB,end,ms,events);CHECK(o);SatchelHostNVNReport r;CHECK(satchel_host_nvn_report(o,&r)&&!r.created&&!r.reached&&!r.events);return o;}
static int always(void *p){(void)p;return 1;}
static void cleanup(SatchelHostNVN **o,SatchelCPUProbe *p){CHECK(satchel_host_nvn_handoff(*o));satchel_cpu_probe_destroy(p);SatchelHostNVNReport r;CHECK(satchel_host_nvn_finish(o,5000,&r)&&!*o);CHECK(!r.retiring_contexts&&!r.pending_devices&&!r.active_contexts&&!r.live_devices&&!r.live_buffers&&!r.live_submissions&&!r.live_targets&&!r.live_pipelines&&!r.buffer_bytes&&!r.target_bytes&&!r.live_compute_pipelines&&!r.live_raster_runs&&!r.raster_host_bytes);}
int main(void){
 CHECK(!satchel_host_nvn_create(BOOT,STUB,1,30001,1));CHECK(!satchel_host_nvn_create(BOOT,STUB,1,1,100001));
 SatchelCPUProbe *p=make();SatchelHostNVN *o=owner(100,30000,satchel_host_nvn_now_ns()-1);SatchelCPUProbeResult r=stop(p,BOOT);CHECK(satchel_host_nvn_handle(o,p,&r,NULL,NULL)==SATCHEL_GUEST_DISPATCH_CANCELLED);SatchelHostNVNReport hr;CHECK(satchel_host_nvn_report(o,&hr)&&!hr.created);cleanup(&o,p);
 p=make();o=owner(100,30000,satchel_host_nvn_now_ns()+30000000000ull);r=stop(p,STUB);CHECK(satchel_host_nvn_handle(o,p,&r,NULL,NULL)==SATCHEL_GUEST_DISPATCH_NOT_OURS);CHECK(satchel_host_nvn_report(o,&hr)&&!hr.created);cleanup(&o,p);
 p=make();o=owner(100,30000,satchel_host_nvn_now_ns()+30000000000ull);r=stop(p,BOOT);CHECK(satchel_host_nvn_handle(o,p,&r,always,NULL)==SATCHEL_GUEST_DISPATCH_CANCELLED);CHECK(satchel_host_nvn_report(o,&hr)&&!hr.created);cleanup(&o,p);
 // Fresh shared child, parent destroyed: the glue uses the supplied stopped CPU,
 // not the original/main pointer. No scheduler-success claim in this fixture.
 p=make();char e[160];SatchelCPUProbe *child=satchel_cpu_probe_create_shared(p,CALL,e,sizeof(e));CHECK(child);satchel_cpu_probe_destroy(p);p=child;
 o=owner(1,30000,satchel_host_nvn_now_ns()+30000000000ull);const char name[]="nvnDeviceBuilderSetDefaults";CHECK(satchel_cpu_probe_write(p,DATA,name,sizeof(name)));CHECK(satchel_cpu_probe_set_x(p,0,DATA));r=stop(p,BOOT);CHECK(satchel_host_nvn_handle(o,p,&r,NULL,NULL)==SATCHEL_GUEST_DISPATCH_RETURNED);uint64_t target;CHECK(satchel_cpu_probe_get_x(p,0,&target)&&target>=STUB);r=satchel_cpu_probe_run(p,8,NULL,NULL);CHECK(r.reason==SATCHEL_CPU_SVC);CHECK(satchel_cpu_probe_set_x(p,0,DATA+512));r=stop(p,target);CHECK(satchel_host_nvn_handle(o,p,&r,NULL,NULL)==SATCHEL_GUEST_DISPATCH_LIMIT);CHECK(satchel_host_nvn_report(o,&hr)&&hr.created&&hr.returned_calls==1);cleanup(&o,p);
 p=make();o=owner(10,1,satchel_host_nvn_now_ns()+30000000000ull);CHECK(satchel_cpu_probe_write(p,DATA,name,sizeof(name)));CHECK(satchel_cpu_probe_set_x(p,0,DATA));r=stop(p,BOOT);CHECK(satchel_host_nvn_handle(o,p,&r,NULL,NULL)==SATCHEL_GUEST_DISPATCH_RETURNED);struct timespec pause={0,2000000};nanosleep(&pause,NULL);CHECK(satchel_host_nvn_should_cancel(o));r=stop(p,BOOT);CHECK(satchel_host_nvn_handle(o,p,&r,NULL,NULL)==SATCHEL_GUEST_DISPATCH_CANCELLED);cleanup(&o,p);
 p=make();o=owner(10,30000,satchel_host_nvn_now_ns()+30000000000ull);CHECK(satchel_cpu_probe_write(p,DATA,name,sizeof(name)));CHECK(satchel_cpu_probe_set_x(p,0,DATA));r=stop(p,BOOT);CHECK(satchel_cpu_probe_set_x(p,30,DATA));CHECK(satchel_host_nvn_handle(o,p,&r,NULL,NULL)==SATCHEL_GUEST_DISPATCH_BAD_RETURN);uint64_t v;CHECK(satchel_cpu_probe_get_x(p,0,&v)&&v==DATA);cleanup(&o,p);
 // Exhaust the real bounded retirement reservation at attachment; the fifth
 // partially initialized owner must remain safely handoff-able.
 SatchelCPUProbe *cpus[5];SatchelHostNVN *owners[5];
 for(unsigned i=0;i<5;i++){
  cpus[i]=make();owners[i]=owner(10,30000,satchel_host_nvn_now_ns()+30000000000ull);
  CHECK(satchel_cpu_probe_write(cpus[i],DATA,name,sizeof(name)));CHECK(satchel_cpu_probe_set_x(cpus[i],0,DATA));
  r=stop(cpus[i],BOOT);SatchelGuestDispatchStatus st=satchel_host_nvn_handle(owners[i],cpus[i],&r,NULL,NULL);
  CHECK(st==(i<4?SATCHEL_GUEST_DISPATCH_RETURNED:SATCHEL_GUEST_DISPATCH_BACKEND_UNAVAILABLE));
 }
 for(unsigned i=0;i<5;i++){CHECK(satchel_host_nvn_handoff(owners[i]));satchel_cpu_probe_destroy(cpus[i]);}
 for(unsigned i=0;i<5;i++){CHECK(satchel_host_nvn_finish(&owners[i],5000,&hr));CHECK(!owners[i]);}
 CHECK(!hr.active_contexts&&!hr.retiring_contexts&&!hr.pending_devices);
 p=make();o=owner(10,30000,satchel_host_nvn_now_ns()+30000000000ull);
 const char unknown[]="original_unknown_nvn_name";CHECK(satchel_cpu_probe_write(p,DATA,unknown,sizeof(unknown)));CHECK(satchel_cpu_probe_set_x(p,0,DATA));r=stop(p,BOOT);
 CHECK(satchel_host_nvn_handle(o,p,&r,NULL,NULL)==SATCHEL_GUEST_DISPATCH_UNKNOWN_NAME);CHECK(satchel_cpu_probe_get_x(p,0,&v)&&v==DATA);cleanup(&o,p);
 CHECK(!satchel_vulkan_device_live_count());printf("Host NVN glue: %u original assertions passed.\n",checks);return 0;
}
