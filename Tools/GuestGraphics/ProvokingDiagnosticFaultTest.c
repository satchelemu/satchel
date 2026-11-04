#include "GuestProvokingDiagnostic.h"
#include "VulkanProvoking.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static unsigned checks,clock_fail,wait_hold,cancel_on_wait,destroy_fail,cancelled;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"provoking fault assertion line%d check%u\n",__LINE__,checks);exit(1);}}while(0)
int provoking_diag_clock(clockid_t c,struct timespec*t){if(clock_fail){clock_fail--;return -1;}return clock_gettime(c,t);}
SatchelVulkanStatus provoking_diag_wait(uint64_t d,uint64_t job,uint32_t ms,SatchelVulkanCancelled c,void*x,SatchelVulkanSubmissionReport*r){if(cancel_on_wait&&c&&ms==0){cancelled=1;cancel_on_wait=0;}if(wait_hold&&!c){memset(r,0,sizeof(*r));r->resources_retained=1;return SATCHEL_VULKAN_BUSY;}return satchel_vulkan_submission_wait(d,job,ms,c,x,r);}
SatchelVulkanStatus provoking_diag_destroy(uint64_t d,uint64_t p){if(destroy_fail){destroy_fail--;return SATCHEL_VULKAN_API_ERROR;}return satchel_vulkan_raster_destroy(d,p);}
static int cancel(void*x){(void)x;return cancelled;}
static int always(void*x){(void)x;return 1;}
static void drain(SatchelGuestProvokingDiagnostic*r){for(unsigned i=0;i<100;i++){uint32_t status=satchel_guest_provoking_diagnostic(r,always,NULL);CHECK(status==SATCHEL_GUEST_PROVOKING_BUSY||status==SATCHEL_GUEST_PROVOKING_CANCELLED);if(!r->resources_retained)return;struct timespec t={0,1000000};nanosleep(&t,NULL);}CHECK(0);}
int main(void){SatchelGuestProvokingDiagnostic r={0};clock_fail=1;CHECK(satchel_guest_provoking_diagnostic(&r,NULL,NULL)==SATCHEL_GUEST_PROVOKING_CANCELLED&&!r.resources_retained&&!r.pairs_compiled&&r.cleanup_matched);CHECK(!clock_fail);
 cancel_on_wait=1;wait_hold=1;CHECK(satchel_guest_provoking_diagnostic(&r,cancel,NULL)==SATCHEL_GUEST_PROVOKING_CANCELLED&&r.resources_retained&&r.draws_submitted==1&&r.pending_cancellations==1);CHECK(r.final.index_buffers==1&&r.final.index_bytes>=4095*4&&r.final.jobs==1);SatchelVulkanProvokingStats s={0};CHECK(satchel_vulkan_provoking_stats(&s)==SATCHEL_VULKAN_OK&&s.live_index_buffers==1);CHECK(satchel_guest_provoking_diagnostic(&r,always,NULL)==SATCHEL_GUEST_PROVOKING_BUSY&&r.resources_retained&&r.final.index_bytes==s.allocated_bytes);wait_hold=0;drain(&r);CHECK(!r.final.devices&&!r.final.buffers&&!r.final.jobs&&!r.final.index_buffers&&!r.final.index_bytes);
 cancelled=0;destroy_fail=1;CHECK(satchel_guest_provoking_diagnostic(&r,NULL,NULL)==SATCHEL_GUEST_PROVOKING_BUSY&&r.resources_retained&&r.stage!=10);CHECK(r.fences_completed==2&&r.final.index_buffers==1&&!r.final.jobs);drain(&r);CHECK(!r.final.index_buffers&&!r.final.index_bytes);CHECK(satchel_guest_provoking_diagnostic(&r,NULL,NULL)==SATCHEL_GUEST_PROVOKING_OK&&r.stage==10&&r.cleanup_matched);printf("PASS %u test-only provoking clock/fence/cleanup retry assertions; real GPU jobs and retained index ownership, no production seams\n",checks);return 0;}
