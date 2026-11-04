/* Test-only link replacements; never compiled into the production archive. */
#include "GuestCapturedProgramDiagnostic.h"
#include "GuestPools.h"
#include "VulkanSubmission.h"
#include "VulkanDevice.h"
#include "VulkanBuffer.h"
#include "VulkanRenderTarget.h"
#include "VulkanRaster.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static unsigned checks,hold_wait,hold_pool,fail_first_clock;
#define CHECK(v) do{checks++;if(!(v)){fprintf(stderr,"pending diagnostic assertion line%d\n",__LINE__);abort();}}while(0)
int diagnostic_test_clock_gettime(clockid_t id,struct timespec *out){if(fail_first_clock){fail_first_clock=0;return -1;}return clock_gettime(id,out);}
SatchelVulkanStatus diagnostic_test_wait(uint64_t d,uint64_t j,uint32_t timeout,SatchelVulkanCancelled cancel,void*ctx,SatchelVulkanSubmissionReport*r){if(hold_wait&&timeout==0&&!cancel){memset(r,0,sizeof(*r));r->status=SATCHEL_VULKAN_TIMEOUT;r->resources_retained=1;return SATCHEL_VULKAN_TIMEOUT;}return satchel_vulkan_submission_wait(d,j,timeout,cancel,ctx,r);}
SatchelGuestPoolStatus diagnostic_test_pool_destroy(SatchelGuestPools**p){if(hold_pool)return SATCHEL_GUEST_POOL_BUSY;return satchel_guest_pools_try_destroy(p);}
static int cancel_pending(void*context){(void)context;uint32_t n=satchel_vulkan_submission_live_count();return n&&n!=UINT32_MAX;}
static int cancel_now(void*context){(void)context;return 1;}
static void empty(void){SatchelVulkanBufferStats b;SatchelVulkanTargetStats t;SatchelVulkanRasterStats r;CHECK(satchel_vulkan_buffer_stats(&b)==SATCHEL_VULKAN_OK&&!b.live_buffers&&!b.allocated_bytes);CHECK(satchel_vulkan_target_stats(&t)==SATCHEL_VULKAN_OK&&!t.live_targets&&!t.allocated_bytes);CHECK(satchel_vulkan_raster_stats(&r)==SATCHEL_VULKAN_OK&&!r.live_pipelines);CHECK(!satchel_vulkan_device_live_count()&&!satchel_vulkan_submission_live_count());}
int main(void){SatchelGuestCapturedProgramDiagnostic r={0};fail_first_clock=1;CHECK(satchel_guest_captured_program_diagnostic(&r,NULL,NULL)==SATCHEL_GUEST_CAPTURED_DIAG_CANCELLED);CHECK(!fail_first_clock&&!r.cpu_destroyed&&!r.source_admissions&&!r.resources_retained&&r.cleanup_matched);empty();hold_wait=1;CHECK(satchel_guest_captured_program_diagnostic(&r,cancel_pending,NULL)==SATCHEL_GUEST_CAPTURED_DIAG_CANCELLED);CHECK(r.resources_retained&&r.cpu_destroyed&&r.draws_submitted==1&&!r.fences_completed);CHECK(satchel_vulkan_device_live_count()==1&&satchel_vulkan_submission_live_count()==1);CHECK(satchel_guest_captured_program_diagnostic(&r,NULL,NULL)==SATCHEL_GUEST_CAPTURED_DIAG_BUSY);CHECK(r.resources_retained&&!r.cpu_destroyed&&!r.source_admissions);hold_wait=0;hold_pool=1;
 for(unsigned n=0;n<100&&satchel_vulkan_submission_live_count();n++){struct timespec t={0,1000000};nanosleep(&t,NULL);CHECK(satchel_guest_captured_program_diagnostic(&r,cancel_now,NULL)==SATCHEL_GUEST_CAPTURED_DIAG_BUSY);}CHECK(!satchel_vulkan_submission_live_count()&&r.resources_retained&&!r.cleanup_matched);CHECK(satchel_vulkan_device_live_count()==1);SatchelVulkanBufferStats b;CHECK(satchel_vulkan_buffer_stats(&b)==SATCHEL_VULKAN_OK&&b.live_buffers==1);CHECK(satchel_guest_captured_program_diagnostic(&r,NULL,NULL)==SATCHEL_GUEST_CAPTURED_DIAG_BUSY&&r.resources_retained&&!r.source_admissions);hold_pool=0;CHECK(satchel_guest_captured_program_diagnostic(&r,cancel_now,NULL)==SATCHEL_GUEST_CAPTURED_DIAG_CANCELLED&&r.prior_reaped&&!r.resources_retained&&r.cleanup_matched);empty();CHECK(satchel_guest_captured_program_diagnostic(&r,NULL,NULL)==SATCHEL_GUEST_CAPTURED_DIAG_OK&&r.cleanup_matched&&r.fences_completed==4);empty();printf("PASS %u test-only pending-fence/pool-cleanup retry assertions; real GPU submission, CPU-free bounded retention, no production fault seam\n",checks);return 0;}
