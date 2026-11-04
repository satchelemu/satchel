#include "Fixture.h"
#include <string.h>
SatchelCPUProbe *vi_report_fixture_create(SatchelGuestVIEntries *entries,SatchelGuestVIBorrow *borrow,SatchelCPUProbeResult *stop){
 const uint32_t original[]={0xd503201f,0xd503201f,0xd503201f,0xd503201f};
 SatchelCPUProbeRegion region={0x10000,original,sizeof(original),sizeof(original),SATCHEL_CPU_READ|SATCHEL_CPU_EXECUTE};
 char error[160];SatchelCPUProbe *cpu=satchel_cpu_probe_create(&region,1,0x10000,error,sizeof(error));if(!cpu)return NULL;
 uint64_t bp=0x10000;
 if(!satchel_cpu_probe_set_x(cpu,0,0x20000)||!satchel_cpu_probe_set_x(cpu,1,0x234567)||!satchel_cpu_probe_set_x(cpu,30,0x1000c)||!satchel_cpu_probe_set_breakpoints(cpu,&bp,1)){satchel_cpu_probe_destroy(cpu);return NULL;}
 *stop=satchel_cpu_probe_run(cpu,1,NULL,NULL);if(stop->reason!=SATCHEL_CPU_BREAKPOINT||stop->steps){satchel_cpu_probe_destroy(cpu);return NULL;}
 *entries=(SatchelGuestVIEntries){.version=1,.size=sizeof(*entries),.present_mask=7,.address={0x10000,0x10004,0x10008}};
 *borrow=(SatchelGuestVIBorrow){.cpu=cpu};return cpu;
}
int vi_report_fixture_unchanged(SatchelCPUProbe *cpu){
 uint64_t x0,x1,lr;SatchelCPUProbeResult now=satchel_cpu_probe_run(cpu,0,NULL,NULL);
 return now.reason==SATCHEL_CPU_BUDGET&&now.pc==0x10000&&now.steps==0&&satchel_cpu_probe_get_x(cpu,0,&x0)&&x0==0x20000&&satchel_cpu_probe_get_x(cpu,1,&x1)&&x1==0x234567&&satchel_cpu_probe_get_x(cpu,30,&lr)&&lr==0x1000c;
}
