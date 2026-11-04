/* Original legacy assertions use the new composite. This shared VI dependency
 * is synthetic and unused by those non-Window calls; actual Process Window
 * identity/lease behavior is separately covered by ProcessWindowDispatchTest. */
#include "GuestVulkanWindows.h"
#include <stdlib.h>
static SatchelCPUProbe *reg_cpu;static SatchelGuestVI *reg_vi;
static SatchelCPUProbe *reg_create(const SatchelCPUProbeRegion*r,size_t n,uint64_t pc,char*e,size_t cap){reg_cpu=satchel_cpu_probe_create(r,n,pc,e,cap);return reg_cpu;}
static int reg_claim(void*c,SatchelCPUProbe*p,const SatchelGuestVIReservation*r){(void)c;return p==reg_cpu&&r->address==0xc00000&&satchel_cpu_probe_set_permissions(p,r->address,r->length,0,0)==SATCHEL_CPU_MAP_OK;}
static int reg_enable(SatchelGuestDispatch*d){if(!reg_vi){if(satchel_cpu_probe_resize_rw(reg_cpu,0xc00000,4096)!=SATCHEL_CPU_MAP_OK||satchel_cpu_probe_set_permissions(reg_cpu,0xc00000,4096,3,0)!=SATCHEL_CPU_MAP_OK)abort();SatchelGuestVIConfig c={1,sizeof(c),{1,sizeof(SatchelGuestVIReservation),700,1700,0xc00000,4096},77,100000,30000};if(satchel_guest_vi_create(&c,reg_cpu,reg_claim,NULL,&reg_vi))abort();}uint64_t ticket=0;return satchel_guest_dispatch_enable_vulkan_windows(d,reg_vi,&ticket)&&ticket;}
#define satchel_cpu_probe_create reg_create
#define satchel_guest_dispatch_enable_vulkan_captured_raster reg_enable
