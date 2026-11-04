#include "HostNVN.h"
#include "GuestVulkanRaster.h"
#include "VulkanDevice.h"
#include "VulkanBuffer.h"
#include "VulkanSubmission.h"
#include "VulkanRenderTarget.h"
#include "VulkanRaster.h"
#include "VulkanCompute.h"
#include "GuestRasterRun.h"
#include <stdlib.h>
#include <time.h>
struct SatchelHostNVN {uint64_t bootstrap,stubs,deadline,start;uint32_t max_events,closed;SatchelGuestDispatch *dispatch;SatchelHostNVNReport report;};
uint64_t satchel_host_nvn_now_ns(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return 0;return (uint64_t)t.tv_sec*1000000000u+(uint64_t)t.tv_nsec;}
typedef struct {SatchelHostNVN *owner;SatchelCPUProbeCancelled callback;void *context;} Cancel;
int satchel_host_nvn_should_cancel(const SatchelHostNVN *o){uint64_t now=satchel_host_nvn_now_ns();return !o||!now||now>=o->deadline||(o->start&&now-o->start>=(uint64_t)o->report.window_ms*1000000u);}
static int cancelled(void *raw){Cancel *c=raw;return satchel_host_nvn_should_cancel(c->owner)||(c->callback&&c->callback(c->context));}
SatchelHostNVN *satchel_host_nvn_create(uint64_t b,uint64_t s,uint64_t end,uint32_t ms,uint32_t events){
 if(!b||!s||(b&3)||(s&4095)||!end||!ms||ms>30000||!events||events>100000)return NULL;
 SatchelHostNVN *o=calloc(1,sizeof(*o));if(o){o->bootstrap=b;o->stubs=s;o->deadline=end;o->max_events=events;o->report.window_ms=ms;}return o;
}
SatchelGuestDispatchStatus satchel_host_nvn_handle(SatchelHostNVN *o,SatchelCPUProbe *cpu,const SatchelCPUProbeResult *stop,SatchelCPUProbeCancelled cb,void *ctx){
 if(!o||!cpu||!stop||o->closed)return SATCHEL_GUEST_DISPATCH_INVALID;
 if(stop->reason!=SATCHEL_CPU_BREAKPOINT)return SATCHEL_GUEST_DISPATCH_NOT_OURS;
 Cancel c={o,cb,ctx};
 if(cancelled(&c)){o->report.status=SATCHEL_GUEST_DISPATCH_CANCELLED;return SATCHEL_GUEST_DISPATCH_CANCELLED;}
 if(!o->dispatch){
  if(stop->pc!=o->bootstrap){o->report.status=SATCHEL_GUEST_DISPATCH_NOT_OURS;return SATCHEL_GUEST_DISPATCH_NOT_OURS;}
  o->report.reached=1;o->start=satchel_host_nvn_now_ns();
  o->dispatch=satchel_guest_dispatch_create(o->bootstrap,o->stubs,o->max_events,o->report.window_ms);
  if(!o->dispatch){o->report.status=SATCHEL_GUEST_DISPATCH_RESOURCE_LIMIT;return SATCHEL_GUEST_DISPATCH_RESOURCE_LIMIT;}
  o->report.created=1;uint64_t points[2];SatchelGuestDeviceBackendV2 backend={0};
  if(!satchel_guest_dispatch_breakpoints(o->dispatch,points)||points[0]!=o->bootstrap||points[1]!=o->stubs||
     !satchel_cpu_probe_check_range(cpu,points[0],4,SATCHEL_CPU_EXECUTE)||!satchel_cpu_probe_check_range(cpu,points[1],4,SATCHEL_CPU_EXECUTE)||
     !satchel_vulkan_guest_device_backend_v2(&backend)||!satchel_guest_dispatch_set_device_backend_v2(o->dispatch,&backend)||
     !satchel_guest_dispatch_enable_vulkan_raster(o->dispatch)){
   o->report.status=SATCHEL_GUEST_DISPATCH_BACKEND_UNAVAILABLE;o->closed=1;return SATCHEL_GUEST_DISPATCH_BACKEND_UNAVAILABLE;
  }
 }
 SatchelGuestDispatchReport r={0};SatchelGuestDispatchStatus s=satchel_guest_dispatch_handle(o->dispatch,cpu,stop,cancelled,&c,&r);
 o->report.status=s;o->report.operation_id=r.operation_id;o->report.events=r.events;
 if(s==SATCHEL_GUEST_DISPATCH_RETURNED)o->report.returned_calls++;else o->closed=1;
 return s;
}
int satchel_host_nvn_report(const SatchelHostNVN *o,SatchelHostNVNReport *r){if(!o||!r)return 0;*r=o->report;if(o->start){uint64_t n=satchel_host_nvn_now_ns();if(n>=o->start)r->elapsed_ms=(n-o->start)/1000000u;}return 1;}
int satchel_host_nvn_handoff(SatchelHostNVN *o){if(!o)return 0;o->closed=1;return !o->dispatch||satchel_guest_dispatch_handoff(&o->dispatch);}
typedef struct {uint64_t end;} Cleanup;
static int cleanup_cancel(void *p){uint64_t n=satchel_host_nvn_now_ns();return !n||n>=((Cleanup *)p)->end;}
int satchel_host_nvn_finish(SatchelHostNVN **owner,uint32_t ms,SatchelHostNVNReport *out){
 if(!owner||!*owner||!out||ms>5000)return 0;SatchelHostNVN *o=*owner;
 if(!satchel_host_nvn_handoff(o))return 0;
 SatchelHostNVNReport r;satchel_host_nvn_report(o,&r);SatchelGuestRetirementReport rr={0};
 Cleanup c={satchel_host_nvn_now_ns()+(uint64_t)ms*1000000u};SatchelGuestRetirementStatus s=SATCHEL_GUEST_RETIRE_PENDING;
 do{s=satchel_guest_dispatch_retirement_poll(cleanup_cancel,&c,&rr);if(s==SATCHEL_GUEST_RETIRE_IDLE||cleanup_cancel(&c))break;struct timespec pause={0,1000000};nanosleep(&pause,NULL);}while(1);
 if(!satchel_guest_dispatch_retirement_stats(&rr))return 0;
 r.cleanup_status=s;r.retiring_contexts=rr.retiring_contexts;r.pending_devices=rr.pending_devices;r.active_contexts=rr.active_contexts;r.backend_failures=rr.backend_failures;
 SatchelGuestRasterRunStats runs={0};if(satchel_guest_raster_run_stats(&runs)!=SATCHEL_GRUN_OK)return 0;
 r.live_raster_runs=runs.live_runs;r.raster_host_bytes=runs.host_bytes;r.live_compute_pipelines=satchel_vulkan_compute_pipeline_live_count();
 r.live_devices=satchel_vulkan_device_live_count();r.live_submissions=satchel_vulkan_submission_live_count();
 SatchelVulkanBufferStats b={0};SatchelVulkanTargetStats t={0};SatchelVulkanRasterStats p={0};
 if(satchel_vulkan_buffer_stats(&b)!=SATCHEL_VULKAN_OK||satchel_vulkan_target_stats(&t)!=SATCHEL_VULKAN_OK||satchel_vulkan_raster_stats(&p)!=SATCHEL_VULKAN_OK)return 0;
 r.live_buffers=b.live_buffers;r.buffer_bytes=b.allocated_bytes;r.live_targets=t.live_targets;r.target_bytes=t.allocated_bytes;r.live_pipelines=p.live_pipelines;
 *out=r;free(o);*owner=NULL;return 1;
}
