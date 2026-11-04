#include "GuestNVNComputeDiagnostic.h"
#include "GuestCompute.h"
#include "VulkanScalar.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"NVN diagnostic assertion line%u check%u\n",__LINE__,checks);exit(1);}}while(0)
typedef struct {SatchelGuestNVNComputeDiagnostic *report;unsigned stage,reenter;} Context;
static int always(void *p){(void)p;return 1;}
static int cancel(void *p){Context *c=p;if(c->reenter){c->reenter=0;SatchelGuestNVNComputeDiagnostic other;CHECK(!satchel_guest_nvn_compute_diagnostic(&other,NULL,NULL) && other.memory.status==SATCHEL_GUEST_POOL_DIAG_BUSY);}return c->stage && c->report->memory.stage==c->stage;}
static void reap(void){for(unsigned i=0;i<5000;i++){SatchelGuestNVNComputeDiagnostic r;CHECK(!satchel_guest_nvn_compute_diagnostic(&r,always,NULL));if(!r.memory.resources_retained){CHECK(!satchel_vulkan_device_live_count() && !satchel_vulkan_submission_live_count() && !satchel_vulkan_compute_pipeline_live_count());return;}struct timespec delay={0,1000000};nanosleep(&delay,NULL);}CHECK(0);}
int main(void){SatchelGuestNVNComputeDiagnostic r;Context context={&r,0,1};CHECK(satchel_guest_nvn_compute_diagnostic(&r,cancel,&context));CHECK(r.memory.status==0 && r.memory.stage==10 && r.cases_completed==4 && r.pipelines_compiled==4 && r.fences_completed==4 && r.program_admissions==2);CHECK(r.instructions && r.dispatch_events && r.program_initialized && r.program_finalized && r.unflushed_preserved && r.flushed_changed && r.changed_code_rebound);CHECK(r.handle_finalized==2 && r.command_finalized && r.queue_finalized && r.device_finalized);CHECK(r.memory.values_matched && r.memory.guards_matched && r.memory.state_preserved && r.memory.cleanup_matched && !r.memory.resources_retained);CHECK(!r.final_pipelines && !r.memory.final_devices && !r.memory.final_buffers && !r.memory.final_jobs && !r.memory.final_bytes);
 printf("Original interpreted NVN diagnostic success: %llu instructions/%llu events, %u cases/%u real pipelines/%u fences\n",(unsigned long long)r.instructions,(unsigned long long)r.dispatch_events,r.cases_completed,r.pipelines_compiled,r.fences_completed);
 for(unsigned stage=1;stage<=9;stage++){context.stage=stage;CHECK(!satchel_guest_nvn_compute_diagnostic(&r,cancel,&context));CHECK(r.memory.status==SATCHEL_GUEST_POOL_DIAG_CANCELLED);reap();}
 printf("NVN diagnostic: %u original sanitizer assertions, cancellation at nine stages and reentry/CPU-free retirement\n",checks);return 0;}
