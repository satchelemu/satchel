#include "GuestTextureFrameDiagnostic.h"
#include "GuestVulkanTextureFrames.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
static unsigned checks;static int allocation_failure,clock_failure,handoff_failure,destroy_failure,release_failure,poll_failure,retire_failure,cancel_after_begin,clock_after_begin,cancelled;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"texture diagnostic fault line%u: %s\n",__LINE__,#x);exit(1);}}while(0)
void *tfd_malloc(size_t n){if(allocation_failure){allocation_failure=0;return NULL;}return malloc(n);}
int tfd_clock(clockid_t id,struct timespec*t){if(clock_failure){clock_failure=0;return -1;}return clock_gettime(id,t);}
int tfd_handoff(SatchelGuestDispatch**d){if(handoff_failure){handoff_failure--;return 0;}return satchel_guest_dispatch_handoff(d);}
SatchelGuestTextureFrameStatus tfd_begin(SatchelGuestDispatch*d,SatchelCPUProbe*c,const SatchelGuestTextureFrameRequest*q,uint64_t*t,SatchelFrameCancelled f,void*x){SatchelGuestTextureFrameStatus s=satchel_guest_dispatch_texture_frame_begin(d,c,q,t,f,x);if(cancel_after_begin&&*t)cancelled=1;if(clock_after_begin&&*t)clock_failure=1;return s;}
SatchelGuestTextureFrameStatus tfd_destroy(uint64_t t){if(destroy_failure)return SATCHEL_GTF_BUSY;return satchel_guest_dispatch_texture_frame_destroy(t);}
SatchelGuestTextureFrameStatus tfd_release(uint64_t t,uint64_t l){if(release_failure)return SATCHEL_GTF_BUSY;return satchel_guest_dispatch_texture_frame_release(t,l);}
SatchelGuestTextureFrameStatus tfd_poll(uint64_t t,uint32_t n,int c,SatchelGuestTextureFrameReport*r){if(poll_failure){poll_failure=0;return SATCHEL_GTF_BACKEND;}return satchel_guest_dispatch_texture_frame_poll(t,n,c,r);}
SatchelGuestRetirementStatus tfd_retire(SatchelCPUProbeCancelled c,void*x,SatchelGuestRetirementReport*r){if(retire_failure)return SATCHEL_GUEST_RETIRE_BUSY;return satchel_guest_dispatch_retirement_poll(c,x,r);}
static int yes(void*x){(void)x;return 1;}
static int external(void*x){(void)x;return cancelled;}
static void drain(void){SatchelGuestTextureFrameDiagnostic r;for(unsigned i=0;i<500;i++){SatchelGuestRetirementReport retired;uint32_t global=satchel_guest_dispatch_retirement_poll(NULL,NULL,&retired);CHECK(global==SATCHEL_GUEST_RETIRE_IDLE||global==SATCHEL_GUEST_RETIRE_PENDING||global==SATCHEL_GUEST_RETIRE_BUSY);uint32_t s=satchel_guest_texture_frame_diagnostic(&r,yes,NULL);CHECK(s==SATCHEL_GUEST_TFD_BUSY||s==SATCHEL_GUEST_TFD_CANCELLED);if(!r.resources_retained){CHECK(r.cleanup_matched);return;}struct timespec t={0,1000000};nanosleep(&t,NULL);}CHECK(0);}
int main(void){SatchelGuestTextureFrameDiagnostic r;clock_failure=1;CHECK(satchel_guest_texture_frame_diagnostic(&r,NULL,NULL)==SATCHEL_GUEST_TFD_CANCELLED&&!r.cpu_destroyed&&!r.resources_retained&&r.cleanup_matched);allocation_failure=1;CHECK(satchel_guest_texture_frame_diagnostic(&r,NULL,NULL)==SATCHEL_GUEST_TFD_FAILED&&!r.cpu_destroyed&&!r.resources_retained&&r.cleanup_matched);
 cancel_after_begin=1;destroy_failure=1;CHECK(satchel_guest_texture_frame_diagnostic(&r,external,NULL)==SATCHEL_GUEST_TFD_CANCELLED&&r.cpu_destroyed&&r.resources_retained&&r.snapshots_started==1&&!r.snapshots_completed);CHECK(satchel_guest_texture_frame_diagnostic(&r,yes,NULL)==SATCHEL_GUEST_TFD_BUSY&&r.resources_retained&&!r.cpu_destroyed&&!r.instructions);cancel_after_begin=cancelled=destroy_failure=0;drain();
 clock_after_begin=1;destroy_failure=1;CHECK(satchel_guest_texture_frame_diagnostic(&r,NULL,NULL)==SATCHEL_GUEST_TFD_CANCELLED&&r.cpu_destroyed&&r.resources_retained&&!r.snapshots_completed);clock_after_begin=clock_failure=destroy_failure=0;drain();
 release_failure=1;CHECK(satchel_guest_texture_frame_diagnostic(&r,NULL,NULL)==SATCHEL_GUEST_TFD_BUSY&&r.resources_retained&&r.cpu_destroyed&&r.cpu_free_lease&&r.snapshots_completed==1);CHECK(satchel_guest_texture_frame_diagnostic(&r,yes,NULL)==SATCHEL_GUEST_TFD_BUSY&&r.resources_retained&&!r.cpu_destroyed);SatchelGuestRetirementReport pinned;CHECK(satchel_guest_dispatch_retirement_poll(NULL,NULL,&pinned)==SATCHEL_GUEST_RETIRE_PENDING&&pinned.retiring_contexts);release_failure=0;drain();
 poll_failure=1;destroy_failure=1;CHECK(satchel_guest_texture_frame_diagnostic(&r,NULL,NULL)==SATCHEL_GUEST_TFD_FAILED&&r.cpu_destroyed&&r.resources_retained&&!r.snapshots_completed);destroy_failure=0;drain();
 handoff_failure=3;CHECK(satchel_guest_texture_frame_diagnostic(&r,NULL,NULL)==SATCHEL_GUEST_TFD_FAILED&&r.cpu_destroyed&&r.resources_retained&&!r.cpu_free_lease);drain();
 retire_failure=1;CHECK(satchel_guest_texture_frame_diagnostic(&r,NULL,NULL)==SATCHEL_GUEST_TFD_FAILED&&r.cpu_destroyed&&r.resources_retained);retire_failure=0;drain();CHECK(satchel_guest_texture_frame_diagnostic(&r,NULL,NULL)==SATCHEL_GUEST_TFD_OK&&r.stage==10&&r.cleanup_matched&&!r.resources_retained);printf("Texture frame diagnostic: %u injected-failure assertions; zero lost ticket/lease/retirement ownership.\n",checks);return 0;}
