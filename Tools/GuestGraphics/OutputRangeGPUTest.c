/* Test-only wrapper around the byte-identical original interpreted diagnostic.
 * No game inputs; the real Vulkan composite and checked CPU remain production. */
#include "GuestDispatch.h"
#include <stdio.h>
#include <stdlib.h>
static SatchelGuestDispatchStatus range_handle(SatchelGuestDispatch *,SatchelCPUProbe *,const SatchelCPUProbeResult *,SatchelCPUProbeCancelled,void *,SatchelGuestDispatchReport *);
#define satchel_guest_dispatch_handle range_handle
#include "GuestTextureFrameDiagnostic.c"
#undef satchel_guest_dispatch_handle
static unsigned assertions,queries,aliases,live_checks,released_checks,successful_devices;
static int live_device,live_pool[2],live_texture,live_queue,live_command,live_control;
#define CHECK(x) do{assertions++;if(!(x)){fprintf(stderr,"Actual-composite output guard assertion line%u: %s\n",__LINE__,#x);exit(1);}}while(0)
typedef struct {uint64_t x[31],hash;uint8_t q[32][16];uint32_t fpcr,fpsr;SatchelCPUProbeResult stop;SatchelGuestTextureFrameResources resources;} Saved;
static uint64_t hashbyte(uint64_t h,uint8_t b){return (h^b)*UINT64_C(1099511628211);}
static void save(SatchelCPUProbe *cpu,Saved *s){memset(s,0,sizeof(*s));for(unsigned i=0;i<31;i++)CHECK(satchel_cpu_probe_get_x(cpu,i,&s->x[i]));for(unsigned i=0;i<32;i++)CHECK(satchel_cpu_probe_get_q(cpu,i,s->q[i]));CHECK(satchel_cpu_probe_get_fp_status(cpu,&s->fpcr,&s->fpsr)==SATCHEL_CPU_FP_OK);s->stop=satchel_cpu_probe_run(cpu,0,NULL,NULL);CHECK(snapshot(&s->resources));uint8_t page[4096];uint64_t h=UINT64_C(1469598103934665603);const uint64_t bases[]={DATA,STORAGE,QMEM,UINT64_C(0x20000000)};const unsigned sizes[]={262144,131072,12288,4096};for(unsigned j=0;j<4;j++)for(unsigned i=0;i<sizes[j];i+=4096){int read=satchel_cpu_probe_read(cpu,bases[j]+i,page,sizeof(page));h=hashbyte(h,(uint8_t)read);if(read)for(unsigned k=0;k<sizeof(page);k++)h=hashbyte(h,page[k]);}s->hash=h;}
static void query(SatchelGuestDispatch*d,SatchelCPUProbe*cpu,uint64_t a,size_t n,int expected){Saved before,after;save(cpu,&before);CHECK(satchel_guest_dispatch_check_output_range(d,cpu,a,n)==expected);save(cpu,&after);CHECK(!memcmp(&before,&after,sizeof(before)));queries++;}
static void protected(SatchelGuestDispatch*d,SatchelCPUProbe*cpu,uint64_t a,size_t n){query(d,cpu,a,n,0);query(d,cpu,a-4,8,0);live_checks++;uint64_t page=a&~UINT64_C(4095);CHECK(satchel_cpu_probe_map_alias(cpu,UINT64_C(0x20000000),page,4096)==SATCHEL_CPU_MAP_OK);query(d,cpu,UINT64_C(0x20000000)+(a-page),8,0);query(d,cpu,DATA+0x30000,8192,1);CHECK(satchel_cpu_probe_unmap_alias(cpu,UINT64_C(0x20000000),page,4096)==SATCHEL_CPU_MAP_OK);aliases++;query(d,cpu,a,n,0);}
static void audit(SatchelGuestDispatch*d,SatchelCPUProbe*cpu){query(d,cpu,DATA+0x30000,8192,1);query(d,cpu,CALLER,4,0);query(d,cpu,DATA,0,0);query(d,cpu,UINT64_MAX-3,8,0);query(d,cpu,DATA+262144-4,8,0);if(live_device)protected(d,cpu,DEV,0x3000);for(unsigned i=0;i<2;i++)if(live_pool[i]){protected(d,cpu,POOL+i*256,256);protected(d,cpu,STORAGE+i*65536,65536);}if(live_texture)protected(d,cpu,TEX,0xc0);if(live_queue){protected(d,cpu,QUEUE,64);protected(d,cpu,QMEM,12288);}if(live_command)protected(d,cpu,CMD,64);if(live_control)protected(d,cpu,CONTROL,128);}
static SatchelGuestDispatchStatus range_handle(SatchelGuestDispatch*d,SatchelCPUProbe*cpu,const SatchelCPUProbeResult*stop,SatchelCPUProbeCancelled cancel,void*ctx,SatchelGuestDispatchReport*r){uint64_t arg=0;CHECK(satchel_cpu_probe_get_x(cpu,0,&arg));SatchelGuestDispatchStatus status=satchel_guest_dispatch_handle(d,cpu,stop,cancel,ctx,r);if(status!=SATCHEL_GUEST_DISPATCH_RETURNED||!r->operation_id)return status;const char*n=satchel_guest_dispatch_name(r->operation_id);CHECK(n);int changed=0;uint64_t value=0;CHECK(satchel_cpu_probe_get_x(cpu,0,&value));
 if(!strcmp(n,"nvnDeviceInitialize")&&value==1){live_device=1;live_pool[0]=live_pool[1]=live_texture=live_queue=live_command=live_control=0;successful_devices++;changed=1;}
 else if(!strcmp(n,"nvnMemoryPoolInitialize")&&value==1){CHECK(arg==POOL||arg==POOL+256);live_pool[(arg-POOL)/256]=1;changed=1;}
 else if(!strcmp(n,"nvnTextureInitialize")&&value==1){live_texture=1;changed=1;}
 else if(!strcmp(n,"nvnQueueInitialize")&&value==1){live_queue=1;changed=1;}
 else if(!strcmp(n,"nvnCommandBufferInitialize")&&value==1){live_command=1;changed=1;}
 else if(!strcmp(n,"nvnCommandBufferAddControlMemory")){live_control=1;changed=1;}
 else if(!strcmp(n,"nvnTextureFinalize")){live_texture=0;query(d,cpu,TEX,0xc0,1);released_checks++;}
 else if(!strcmp(n,"nvnCommandBufferFinalize")){live_command=live_control=0;query(d,cpu,CMD,64,1);query(d,cpu,CONTROL,128,1);released_checks+=2;}
 else if(!strcmp(n,"nvnQueueFinalize")){live_queue=0;query(d,cpu,QUEUE,64,1);query(d,cpu,QMEM,12288,1);released_checks+=2;}
 else if(!strcmp(n,"nvnMemoryPoolFinalize")){CHECK(arg==POOL||arg==POOL+256);unsigned i=(unsigned)((arg-POOL)/256);live_pool[i]=0;query(d,cpu,arg,256,1);query(d,cpu,STORAGE+i*65536,65536,1);released_checks+=2;}
 else if(!strcmp(n,"nvnDeviceFinalize")){live_device=0;query(d,cpu,DEV,0x3000,1);released_checks++;}
 if(changed)audit(d,cpu);return status;}
int original_diagnostic_main(void);
int main(void){CHECK(original_diagnostic_main()==0);CHECK(successful_devices>=2&&aliases>=18&&live_checks>=18&&released_checks>=20&&queries>100);SatchelGuestTextureFrameResources final;CHECK(snapshot(&final));CHECK(!final.devices&&!final.buffers&&!final.jobs&&!final.targets&&!final.pipelines&&!final.compute_pipelines&&!final.index_buffers&&!final.frames&&!final.frame_owners&&!final.snapshot_owners&&!final.snapshot_gpu_owners&&!final.active_contexts&&!final.retiring_contexts);printf("Actual Vulkan composite output guard: %u assertions, %u read-only queries, %u physical aliases, %u live-resource cases, %u released-range cases; original diagnostic preserved.\n",assertions,queries,aliases,live_checks,released_checks);return 0;}
