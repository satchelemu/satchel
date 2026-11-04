/* Original host-only C/Swift test bridge; adapted from reviewed original
 * RasterDispatchTest and ProcessWindowDispatchTest. No game shader/asset input.
 * CHECK jumps only after a C ABI call has returned, never from a backend or
 * user callback; no jump crosses C++ frames. All heap/token ownership lives in
 * static fixture state before a later check can fail. Finish is non-jumping. */
#include "WindowFrameBridge.h"
#include "GuestVulkanWindows.h"
#include "SwitchProcess.h"
#include "Fixtures.h"
#include "VulkanCompute.h"
#include "VulkanProvoking.h"
#include <stdatomic.h>
#include <setjmp.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
enum {PIXELS=0x8000,VERT=0x6000,WI_BUILDER=0x32a000,WI_OBJECT=0x32b000,WI_LIST=0x32c000,WI_OUTPUT=0x32d000};
typedef struct {SatchelCPUProbe *cpu;SatchelGuestDispatch *dispatch;uint64_t instructions,events,gpu[2];} Interpreted;
static Interpreted fixture;static SatchelSwitchProcess*process;static SatchelGuestVI*vi;static SatchelGuestWindowBorrow borrow;
static uint64_t ticket,token,next_token=1,epoch,deadline;static unsigned stage,failure_line,cpu_destroyed,ready,retiring;
static SatchelGuestTextureFrameResources baseline;static jmp_buf failure;static atomic_flag active=ATOMIC_FLAG_INIT;
static uint64_t now(void){struct timespec t;if(clock_gettime(CLOCK_MONOTONIC,&t))return UINT64_MAX;return (uint64_t)t.tv_sec*UINT64_C(1000000000)+(uint64_t)t.tv_nsec;}
static int cancelled(void*p){(void)p;return now()>=deadline;}
#define CHECK(v) do{if(!(v)){failure_line=__LINE__;longjmp(failure,1);}}while(0)
static int create_process(void);static int attach_window(SatchelGuestDispatch*);
enum {ICALL=0x8000000,IBOOT=0x8000100,ISTUBS=0x8200000,IDATA=0x300000,IDB=0x300100,IDEV=0x301000,IPB=0x304100,IPOOL=0x305000,IQ=0x308000,IQB=0x30a000,ICMD=0x30b000,ICTRL=0x30c000,IHANDLE=0x30d000,IPROGRAM=0x30e000,ISHDATA=0x30f000,ISHCONTROL=0x310000,ITB=0x313000,ITEX=0x314000,ISTATE=0x320000,ISTORAGE=0x800000,IQMEM=0xa00000};

static uint64_t invokei(Interpreted *f,uint64_t target,const uint64_t args[6],uint32_t expected){CHECK(satchel_cpu_probe_set_pc(f->cpu,ICALL));CHECK(satchel_cpu_probe_set_x(f->cpu,9,target));for(unsigned i=0;i<6;i++)CHECK(satchel_cpu_probe_set_x(f->cpu,i,args[i]));SatchelCPUProbeResult stop=satchel_cpu_probe_run(f->cpu,32,NULL,NULL);f->instructions+=stop.steps;CHECK(stop.reason==SATCHEL_CPU_BREAKPOINT);SatchelGuestDispatchReport out;uint32_t actual=satchel_guest_dispatch_handle(f->dispatch,f->cpu,&stop,cancelled,NULL,&out);f->events=out.events;if(actual!=expected)fprintf(stderr,"dispatch expected%u actual%u operation%u\n",expected,actual,out.operation_id);CHECK(actual==expected);uint64_t value=0;if(actual==SATCHEL_GUEST_DISPATCH_RETURNED){stop=satchel_cpu_probe_run(f->cpu,8,NULL,NULL);f->instructions+=stop.steps;CHECK(stop.reason==SATCHEL_CPU_SVC && stop.svc==0x42);}CHECK(satchel_cpu_probe_get_x(f->cpu,0,&value));return value;}
static uint64_t calli(Interpreted *f,const char *name,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,uint64_t g,uint32_t expected){CHECK(satchel_cpu_probe_write(f->cpu,IDATA,name,strlen(name)+1));uint64_t args[6]={IDATA};uint64_t target=invokei(f,IBOOT,args,SATCHEL_GUEST_DISPATCH_RETURNED);uint64_t real[6]={a,b,c,d,e,g};return invokei(f,target,real,expected);}
#define I(f,n,a,b,c,d,e,g) calli(f,n,a,b,c,d,e,g,SATCHEL_GUEST_DISPATCH_RETURNED)
#define V(f,n,a,b,c,d,e,g) CHECK(I(f,n,a,b,c,d,e,g)==(uint64_t)(a))
static void makei(Interpreted *f){memset(f,0,sizeof(*f));CHECK(create_process());f->cpu=satchel_switch_process_cpu(process);CHECK(f->cpu);f->dispatch=satchel_guest_dispatch_create(IBOOT,ISTUBS,4096,30000);CHECK(f->dispatch);SatchelGuestDeviceBackendV2 backend;CHECK(satchel_vulkan_guest_device_backend_v2(&backend));CHECK(satchel_guest_dispatch_set_device_backend_v2(f->dispatch,&backend));CHECK(attach_window(f->dispatch));uint64_t points[2];CHECK(satchel_guest_dispatch_breakpoints(f->dispatch,points));CHECK(satchel_cpu_probe_set_breakpoints(f->cpu,points,2));V(f,"nvnDeviceBuilderSetDefaults",IDB,0,0,0,0,0);CHECK(I(f,"nvnDeviceInitialize",IDEV,IDB,0,0,0,0)==1);
 CHECK(I(f,"nvnDeviceGetWindowOriginMode",IDEV,0,0,0,0,0)==0);CHECK(I(f,"nvnDeviceGetDepthMode",IDEV,0,0,0,0,0)==0);V(f,"nvnDeviceSetWindowOriginMode",IDEV,1,0,0,0,0);V(f,"nvnDeviceSetDepthMode",IDEV,1,0,0,0,0);
 for(unsigned i=0;i<2;i++){V(f,"nvnMemoryPoolBuilderSetDefaults",IPB,0,0,0,0,0);V(f,"nvnMemoryPoolBuilderSetDevice",IPB,IDEV,0,0,0,0);V(f,"nvnMemoryPoolBuilderSetStorage",IPB,ISTORAGE+i*65536,65536,0,0,0);V(f,"nvnMemoryPoolBuilderSetFlags",IPB,0x24,0,0,0,0);CHECK(I(f,"nvnMemoryPoolInitialize",IPOOL+i*256,IPB,0,0,0,0)==1);f->gpu[i]=I(f,"nvnMemoryPoolGetBufferAddress",IPOOL+i*256,0,0,0,0,0);}
 V(f,"nvnQueueBuilderSetDefaults",IQB,0,0,0,0,0);V(f,"nvnQueueBuilderSetDevice",IQB,IDEV,0,0,0,0);V(f,"nvnQueueBuilderSetCommandMemorySize",IQB,4096,0,0,0,0);V(f,"nvnQueueBuilderSetComputeMemorySize",IQB,4096,0,0,0,0);V(f,"nvnQueueBuilderSetControlMemorySize",IQB,4096,0,0,0,0);V(f,"nvnQueueBuilderSetQueueMemory",IQB,IQMEM,12288,0,0,0);CHECK(I(f,"nvnQueueInitialize",IQ,IQB,0,0,0,0)==1);CHECK(I(f,"nvnCommandBufferInitialize",ICMD,IDEV,0,0,0,0)==1);V(f,"nvnCommandBufferAddCommandMemory",ICMD,IPOOL,0,4096,0,0);V(f,"nvnCommandBufferAddControlMemory",ICMD,ICTRL,128,0,0,0);
}
static void shaderi(Interpreted *f,unsigned stage,unsigned offset,unsigned ctl,unsigned color){RasterFixture r;fixture_init(&r,stage);if(stage==1){fixture_ald(&r,0,0,0,4);fixture_mov(&r,0,0);fixture_ast(&r,0,0,4);}else for(unsigned i=0;i<4;i++)fixture_imm(&r,i,i==color||i==3?0x3f800000:0);fixture_exit(&r);SatchelRasterStageInput input=fixture_input(&r,stage);uint8_t control[0x880]={0},area[0x500]={0};uint32_t c1=0x80+(uint32_t)r.cursor,size=c1+64;fixture32(control,0x98761234);fixture32(control+0x20,sizeof(control));fixture32(control+0x700,c1);fixture32(control+0x704,size);fixture32(area,0x12345678);memcpy(area+0x30,input.sph,input.sph_size);memcpy(area+0x80,input.code,input.code_size);memset(area+c1,0x6a,64);CHECK(satchel_cpu_probe_write(f->cpu,ISHCONTROL+ctl,control,sizeof(control)));CHECK(satchel_cpu_probe_write(f->cpu,ISTORAGE+65536+offset,area,size));V(f,"nvnMemoryPoolFlushMappedRange",IPOOL+256,offset,size,0,0,0);}
static void programi(Interpreted *f,unsigned index){CHECK(I(f,"nvnProgramInitialize",IPROGRAM+256*index,IDEV,0,0,0,0)==1);uint8_t entries[32];fixture64(entries,f->gpu[1]);fixture64(entries+8,ISHCONTROL);fixture64(entries+16,f->gpu[1]+(index+1)*4096);fixture64(entries+24,ISHCONTROL+(index+1)*4096);CHECK(satchel_cpu_probe_write(f->cpu,ISHDATA,entries,32));CHECK(I(f,"nvnProgramSetShaders",IPROGRAM+256*index,2,ISHDATA,0,0,0)==1);SatchelVulkanRasterStats stats;CHECK(satchel_vulkan_raster_stats(&stats)==SATCHEL_VULKAN_OK&&!stats.live_pipelines);}
static SatchelGuestTextureLayout texturei(Interpreted *f){V(f,"nvnTextureBuilderSetDefaults",ITB,0,0,0,0,0);V(f,"nvnTextureBuilderSetDevice",ITB,IDEV,0,0,0,0);V(f,"nvnTextureBuilderSetSize2D",ITB,31,17,0,0,0);V(f,"nvnTextureBuilderSetFormat",ITB,0x25,0,0,0,0);V(f,"nvnTextureBuilderSetStorage",ITB,IPOOL+256,PIXELS,0,0,0);CHECK(I(f,"nvnTextureInitialize",ITEX,ITB,0,0,0,0)==1);
 SatchelGuestTextureDescription desc={.version=1,.size=sizeof(desc),.target=1,.format=0x25,.width=31,.height=17,.depth=1,.levels=1};SatchelGuestTextureLayout layout;CHECK(satchel_guest_texture_layout(&desc,&layout)==SATCHEL_GTL_OK);CHECK(I(f,"nvnTextureBuilderGetStorageSize",ITB,0,0,0,0,0)==layout.storage_size);uint8_t tile[8192];memset(tile,0x6d,sizeof(tile));CHECK(satchel_cpu_probe_write(f->cpu,ISTORAGE+65536+PIXELS,tile,layout.storage_size));V(f,"nvnMemoryPoolFlushMappedRange",IPOOL+256,PIXELS,layout.storage_size,0,0,0);return layout;
}
static void statei(Interpreted *f){V(f,"nvnColorStateSetDefaults",ISTATE,0,0,0,0,0);V(f,"nvnChannelMaskStateSetDefaults",ISTATE+8,0,0,0,0,0);V(f,"nvnPolygonStateSetDefaults",ISTATE+16,0,0,0,0,0);V(f,"nvnDepthStencilStateSetDefaults",ISTATE+24,0,0,0,0,0);V(f,"nvnDepthStencilStateSetDepthWriteEnable",ISTATE+24,0,0,0,0,0);V(f,"nvnMultisampleStateSetDefaults",ISTATE+32,0,0,0,0,0);V(f,"nvnVertexAttribStateSetDefaults",ISTATE+64,0,0,0,0,0);V(f,"nvnVertexAttribStateSetFormat",ISTATE+64,0x2e,0,0,0,0);V(f,"nvnVertexStreamStateSetDefaults",ISTATE+72,0,0,0,0,0);V(f,"nvnVertexStreamStateSetStride",ISTATE+72,16,0,0,0,0);
 uint8_t bytes[48]={0};fixture64(bytes,ITEX);for(unsigned i=0;i<4;i++)fixture32(bytes+8+i*4,i*2);float clear[4]={0,0,1,1};memcpy(bytes+24,clear,16);CHECK(satchel_cpu_probe_write(f->cpu,ISTATE+128,bytes,sizeof(bytes)));
 V(f,"nvnCommandBufferBeginRecording",ICMD,0,0,0,0,0);V(f,"nvnCommandBufferSetRenderTargets",ICMD,1,ISTATE+128,0,0,0);V(f,"nvnCommandBufferBindProgram",ICMD,IPROGRAM,3,0,0,0);V(f,"nvnCommandBufferBindVertexBuffer",ICMD,0,f->gpu[1]+VERT,48,0,0);V(f,"nvnCommandBufferBindVertexAttribState",ICMD,1,ISTATE+64,0,0,0);V(f,"nvnCommandBufferBindVertexStreamState",ICMD,1,ISTATE+72,0,0,0);
 V(f,"nvnCommandBufferSetViewport",ICMD,0,0,31,17,0);V(f,"nvnCommandBufferSetScissor",ICMD,0,0,31,17,0);uint8_t q[16]={0};CHECK(satchel_cpu_probe_set_q(f->cpu,0,q));fixture32(q,0x3f800000);CHECK(satchel_cpu_probe_set_q(f->cpu,1,q));V(f,"nvnCommandBufferSetDepthRange",ICMD,0,0,0,0,0);
 const char *names[]={"nvnCommandBufferBindColorState","nvnCommandBufferBindChannelMaskState","nvnCommandBufferBindPolygonState","nvnCommandBufferBindDepthStencilState","nvnCommandBufferBindMultisampleState"};const unsigned offsets[]={0,8,16,24,32};for(unsigned i=0;i<5;i++)V(f,names[i],ICMD,ISTATE+offsets[i],0,0,0,0);
 V(f,"nvnCommandBufferSetDepthClamp",ICMD,0,0,0,0,0);V(f,"nvnCommandBufferSetRasterizerDiscard",ICMD,0,0,0,0,0);V(f,"nvnCommandBufferSetSampleMask",ICMD,UINT32_MAX,0,0,0,0);V(f,"nvnCommandBufferSetRenderEnable",ICMD,1,0,0,0,0);V(f,"nvnCommandBufferSetViewportSwizzles",ICMD,0,1,ISTATE+136,0,0);
}
static int create_process(void){
 SatchelGuestDispatchLayout layout;if(!satchel_guest_dispatch_build_stubs(NULL,0,&layout)||layout.region_size>65536)return 0;
 static uint8_t stubs[65536];if(!satchel_guest_dispatch_build_stubs(stubs,layout.region_size,&layout))return 0;
 uint8_t image[0x8000]={0};fixture32(image,0xd63f0120);fixture32(image+4,0xd4000841);
 SatchelNSOInfo info={.image_size=sizeof(image),.bss_size=0x5ff0,.segments={{0,8,0,8},{0,4096,0x1000,4096},{0,16,0x2000,16}}};
 SatchelSwitchProcessModule modules[]={{"rtld",info,image,sizeof(image)},{"main",info,image,sizeof(image)}};
 SatchelProcessMetadata meta={.program_id=1,.address_space_size=UINT64_C(1)<<39,.main_thread_stack_size=0x4000,.address_space_bits=39,.address_space_type=3,.flags0=7,.is_64_bit=true,.main_thread_priority=44,.has_core_priority=true,.allowed_core_mask=7,.allowed_priority_mask=UINT64_C(0xfffffffffffffff0)};
 SatchelSwitchDispatchCode code={stubs,layout.region_size};char error[128];process=satchel_switch_process_create_with_dispatch_and_identity(modules,2,&meta,&code,error,sizeof(error));if(!process)return 0;
 SatchelCPUProbe*cpu=satchel_switch_process_cpu(process);if(!cpu||satchel_switch_process_module_address(process,0)!=ICALL||satchel_switch_process_dispatch_address(process)!=ISTUBS)return 0;
 /* Explicit original fixture RAM via trusted host API, not SVC-map proof. */
 return satchel_cpu_probe_resize_rw(cpu,IDATA,262144)==SATCHEL_CPU_MAP_OK&&satchel_cpu_probe_resize_rw(cpu,ISTORAGE,131072)==SATCHEL_CPU_MAP_OK&&satchel_cpu_probe_resize_rw(cpu,IQMEM,12288)==SATCHEL_CPU_MAP_OK;
}
static SatchelSwitchIdentityReservation identity(const SatchelGuestVIReservation*r){return (SatchelSwitchIdentityReservation){r->version,r->size,r->process_id,r->generation,r->address,r->length};}
static int validate_vi(void*p,SatchelCPUProbe*c,const SatchelGuestVIReservation*r){(void)p;SatchelSwitchIdentityReservation d=identity(r);return process&&satchel_switch_process_validate_identity(process,c,&d);}
static int claim(void*p,SatchelCPUProbe*c,const SatchelGuestVIReservation*r){(void)p;SatchelSwitchIdentityReservation d=identity(r);return process&&satchel_switch_process_claim_identity(process,c,&d);}
static int output_guard(void*p,SatchelCPUProbe*c,uint64_t a,size_t n){return satchel_guest_dispatch_check_output_range(p,c,a,n);}
static int context(void*p,SatchelCPUProbe*c,uint64_t id,uint64_t generation){(void)p;SatchelSwitchContextIdentity d={1,sizeof(d),id,generation};return process&&satchel_switch_process_validate_context(process,c,&d);}
static int policy(void*p,const SatchelGuestVIInfo*i,int32_t interval,uint32_t width,uint32_t height,SatchelGuestWindowPresentation*out){(void)p;if(interval!=7||width!=31||height!=17||i->surface_epoch!=epoch)return 0;*out=(SatchelGuestWindowPresentation){1,sizeof(*out),SATCHEL_FRAME_RGBA8_UNORM,SATCHEL_FRAME_LINEAR_SRGB,SATCHEL_FRAME_TOP_LEFT,epoch,1};return 1;}
static int attach_window(SatchelGuestDispatch*d){
 SatchelSwitchIdentityReservation r;SatchelSwitchContextIdentity c;if(!satchel_switch_process_identity_reservation(process,&r)||!satchel_switch_process_context_identity(process,fixture.cpu,&c))return 0;
 SatchelGuestVIConfig cfg={1,sizeof(cfg),{r.version,r.size,r.process_id,r.generation,r.address,r.length},epoch,100000,30000};if(satchel_guest_vi_create(&cfg,fixture.cpu,claim,NULL,&vi))return 0;
 borrow=(SatchelGuestWindowBorrow){.cpu=fixture.cpu,.vi={fixture.cpu,epoch,validate_vi,NULL,output_guard,d,cancelled,NULL},.process_id=c.process_id,.context_generation=c.context_generation,.trap_pc=ISTUBS,.return_pc=ICALL+4,.validate_context=context,.policy=policy};
 return satchel_guest_dispatch_enable_vulkan_windows(d,vi,&ticket);
}
static uint64_t vi_call(unsigned op,uint64_t in){CHECK(!cancelled(NULL));CHECK(satchel_cpu_probe_set_pc(fixture.cpu,ICALL));SatchelGuestVIResult result;CHECK(!satchel_guest_vi_execute(vi,&borrow.vi,op,WI_OUTPUT+64,in,ICALL,ICALL+4,&result));uint8_t b[8];CHECK(satchel_cpu_probe_read(fixture.cpu,WI_OUTPUT+64,b,8));uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)b[i]<<(8*i);return v;}
static uint64_t window_call(const char*name,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e){
 CHECK(!cancelled(NULL));SatchelGuestDispatchLayout layout;CHECK(satchel_guest_dispatch_build_stubs(NULL,0,&layout));uint64_t target=0;for(unsigned i=1;i<=layout.stub_count;i++)if(!strcmp(name,satchel_guest_dispatch_name(i)))target=ISTUBS+layout.first_stub_offset+(i-1)*layout.stub_stride;CHECK(target);
 CHECK(satchel_cpu_probe_set_pc(fixture.cpu,ICALL));CHECK(satchel_cpu_probe_set_x(fixture.cpu,9,target));uint64_t args[]={a,b,c,d,e,0};for(unsigned i=0;i<6;i++)CHECK(satchel_cpu_probe_set_x(fixture.cpu,i,args[i]));SatchelCPUProbeResult stop=satchel_cpu_probe_run(fixture.cpu,16,cancelled,NULL);fixture.instructions+=stop.steps;CHECK(stop.reason==SATCHEL_CPU_BREAKPOINT&&stop.pc==ISTUBS);
 uint32_t status=SATCHEL_GUEST_DISPATCH_WINDOW_WAIT;for(unsigned i=0;i<10000;i++){CHECK(!cancelled(NULL));SatchelGuestDispatchReport report={0};status=satchel_guest_dispatch_handle_windows(fixture.dispatch,fixture.cpu,&stop,&borrow,&report);fixture.events=report.events;if(!status)break;CHECK(status==SATCHEL_GUEST_DISPATCH_WINDOW_WAIT);uint32_t p=satchel_guest_dispatch_window_poll(ticket,1,0);CHECK(p==SATCHEL_GWIN_OK||p==SATCHEL_GWIN_WAIT);}
 CHECK(!status);SatchelCPUProbeResult done=satchel_cpu_probe_run(fixture.cpu,8,cancelled,NULL);fixture.instructions+=done.steps;CHECK(done.reason==SATCHEL_CPU_SVC&&done.svc==0x42);uint64_t value;CHECK(satchel_cpu_probe_get_x(fixture.cpu,0,&value));return value;
}
#define W(n,a,b,c,d,e) window_call(n,a,b,c,d,e)
static void setup(void){
 Interpreted*f=&fixture;stage=1;makei(f);stage=2;shaderi(f,1,0,0,0);shaderi(f,5,4096,4096,0);programi(f,0);(void)texturei(f);V(f,"nvnTextureBuilderSetStorage",ITB,IPOOL+256,0xa000,0,0,0);CHECK(I(f,"nvnTextureInitialize",ITEX+256,ITB,0,0,0,0)==1);
 stage=3;uint64_t display=vi_call(SATCHEL_GVI_OPEN_DEFAULT,0),layer=vi_call(SATCHEL_GVI_CREATE_LAYER,display),native=vi_call(SATCHEL_GVI_GET_NATIVE,layer);uint8_t list[16];fixture64(list,ITEX);fixture64(list+8,ITEX+256);CHECK(satchel_cpu_probe_write(f->cpu,WI_LIST,list,sizeof(list)));
 W("nvnWindowBuilderSetDefaults",WI_BUILDER,0,0,0,0);W("nvnWindowBuilderSetDevice",WI_BUILDER,IDEV,0,0,0);W("nvnWindowBuilderSetNativeWindow",WI_BUILDER,native,0,0,0);W("nvnWindowBuilderSetTextures",WI_BUILDER,2,WI_LIST,0,0);CHECK(W("nvnWindowInitialize",WI_OBJECT,WI_BUILDER,0,0,0)==1);W("nvnWindowSetPresentInterval",WI_OBJECT,7,0,0,0);W("nvnWindowSetCrop",WI_OBJECT,0,0,31,17);
 stage=4;float vertices[12]={-1,-1,0.5,1,3,-1,0.5,1,-1,3,0.5,1};CHECK(satchel_cpu_probe_write(f->cpu,ISTORAGE+65536+VERT,vertices,sizeof(vertices)));V(f,"nvnMemoryPoolFlushMappedRange",IPOOL+256,VERT,sizeof(vertices),0,0,0);statei(f);V(f,"nvnCommandBufferClearColor",ICMD,0,ISTATE+152,15,0,0);V(f,"nvnCommandBufferDrawArrays",ICMD,4,0,3,0,0);uint64_t handle=I(f,"nvnCommandBufferEndRecording",ICMD,0,0,0,0,0);uint8_t bytes[8];fixture64(bytes,handle);CHECK(satchel_cpu_probe_write(f->cpu,IHANDLE,bytes,8));V(f,"nvnQueueSubmitCommands",IQ,1,IHANDLE,0,0,0);
 stage=5;W("nvnQueueAcquireTexture",IQ,WI_OBJECT,WI_OUTPUT,0,0);uint8_t index[4];CHECK(satchel_cpu_probe_read(f->cpu,WI_OUTPUT,index,4)&&!index[0]&&!index[1]&&!index[2]&&!index[3]);W("nvnQueuePresentTexture",IQ,WI_OBJECT,0,0,0);
 stage=6;for(unsigned i=0;i<10000;i++){CHECK(!cancelled(NULL));uint32_t s=satchel_guest_dispatch_window_poll(ticket,1,0);CHECK(s==SATCHEL_GWIN_OK||s==SATCHEL_GWIN_WAIT);SatchelGuestWindowStats stats;CHECK(!satchel_guest_dispatch_window_stats(ticket,&stats));if(stats.snapshots_completed==1&&!stats.gpu_snapshots&&!stats.pending_orders){SatchelFrameStats frames;CHECK(!satchel_guest_frames_stats(&frames)&&frames.ready==baseline.frame_ready+1);ready=1;break;}}CHECK(ready);stage=10;
}

static int snapshot(SatchelGuestTextureFrameResources*out){SatchelVulkanBufferStats b;SatchelVulkanTargetStats t;SatchelVulkanRasterStats r;SatchelVulkanProvokingStats p;SatchelFrameStats f;SatchelGuestTextureFrameStats s;SatchelGuestRetirementReport d;if(satchel_vulkan_buffer_stats(&b)||satchel_vulkan_target_stats(&t)||satchel_vulkan_raster_stats(&r)||satchel_vulkan_provoking_stats(&p)||satchel_guest_frames_stats(&f)||satchel_guest_texture_frames_stats(&s)||!satchel_guest_dispatch_retirement_stats(&d))return 0;memset(out,0,sizeof(*out));out->devices=satchel_vulkan_device_live_count();out->buffers=b.live_buffers;out->jobs=satchel_vulkan_submission_live_count();out->targets=t.live_targets;out->pipelines=r.live_pipelines;out->compute_pipelines=satchel_vulkan_compute_pipeline_live_count();out->index_buffers=p.live_index_buffers;out->frame_owners=f.owners;out->frames=f.frames;out->frame_snapshotting=f.snapshotting;out->frame_ready=f.ready;out->frame_leased=f.leased;out->frame_retiring=f.retiring;out->snapshot_owners=s.owners;out->snapshot_gpu_owners=s.gpu_owners;out->reserved_contexts=d.reserved_contexts;out->active_contexts=d.active_contexts;out->retiring_contexts=d.retiring_contexts;out->pending_devices=d.pending_devices;out->buffer_bytes=b.allocated_bytes;out->target_bytes=t.allocated_bytes;out->index_bytes=p.allocated_bytes;out->frame_bytes=f.bytes;out->staging_bytes=s.staging_bytes;return 1;}
static void destroy_cpu(void){if(process){satchel_switch_process_destroy(process);process=NULL;cpu_destroyed=1;}fixture.cpu=NULL;memset(&borrow,0,sizeof(borrow));}
static void report(SatchelWindowFrameFixtureReport*out,uint32_t status,uint64_t old_token){SatchelWindowFrameFixtureReport r={.version=1,.size=sizeof(r),.status=status,.stage=stage,.cpu_destroyed=cpu_destroyed,.ready=ready,.resources_retained=token!=0,.width=31,.height=17,.row_stride=124,.expected_r=255,.expected_a=255,.failure_line=failure_line,.fixture_token=old_token,.window_ticket=ticket,.epoch=epoch,.byte_size=31*17*4,.instructions=fixture.instructions,.dispatch_events=fixture.events,.baseline=baseline};if(!snapshot(&r.current)){r.status=SATCHEL_WINDOW_FIXTURE_FAILED;r.resources_retained=token!=0;}r.cleanup_matched=!r.resources_retained&&!memcmp(&r.baseline,&r.current,sizeof(baseline));*out=r;}
uint32_t satchel_window_frame_fixture_begin(uint64_t wanted_epoch,uint32_t destroy_before_take,SatchelWindowFrameFixtureReport*out){
 if(!out||((uintptr_t)out%_Alignof(SatchelWindowFrameFixtureReport))||!wanted_epoch||destroy_before_take>1)return SATCHEL_WINDOW_FIXTURE_INVALID;if(atomic_flag_test_and_set(&active))return SATCHEL_WINDOW_FIXTURE_BUSY;
 if(token){report(out,SATCHEL_WINDOW_FIXTURE_BUSY,token);atomic_flag_clear(&active);return out->status;}
 if(!snapshot(&baseline)||next_token==UINT64_MAX){*out=(SatchelWindowFrameFixtureReport){.version=1,.size=sizeof(*out),.status=SATCHEL_WINDOW_FIXTURE_FAILED};atomic_flag_clear(&active);return SATCHEL_WINDOW_FIXTURE_FAILED;}
 memset(&fixture,0,sizeof(fixture));epoch=wanted_epoch;stage=failure_line=cpu_destroyed=ready=retiring=0;ticket=0;uint64_t start=now();deadline=start==UINT64_MAX?0:start+UINT64_C(10000000000);token=next_token++;
 if(setjmp(failure)){report(out,SATCHEL_WINDOW_FIXTURE_FAILED,token);atomic_flag_clear(&active);return out->status;}
 CHECK(start!=UINT64_MAX);setup();if(destroy_before_take)destroy_cpu();report(out,SATCHEL_WINDOW_FIXTURE_READY,token);atomic_flag_clear(&active);return out->status;
}
uint32_t satchel_window_frame_fixture_finish(uint64_t wanted,SatchelWindowFrameFixtureReport*out){
 if(!out||((uintptr_t)out%_Alignof(SatchelWindowFrameFixtureReport))||!wanted)return SATCHEL_WINDOW_FIXTURE_INVALID;if(atomic_flag_test_and_set(&active))return SATCHEL_WINDOW_FIXTURE_BUSY;if(wanted!=token){atomic_flag_clear(&active);return SATCHEL_WINDOW_FIXTURE_INVALID;}
 uint32_t status=SATCHEL_WINDOW_FIXTURE_BUSY;ready=0;
 if(fixture.dispatch){if(!satchel_guest_dispatch_handoff(&fixture.dispatch))goto done;retiring=1;}
 destroy_cpu();
 if(retiring){SatchelGuestRetirementReport r={0};SatchelGuestRetirementStatus s=satchel_guest_dispatch_retirement_poll(NULL,NULL,&r);if(s!=SATCHEL_GUEST_RETIRE_IDLE)goto done;retiring=0;ticket=0;}
 if(vi&&satchel_guest_vi_try_destroy(&vi)!=SATCHEL_GVI_OK)goto done;
 token=0;status=SATCHEL_WINDOW_FIXTURE_READY;
 done:report(out,status,wanted);atomic_flag_clear(&active);return out->status;
}
