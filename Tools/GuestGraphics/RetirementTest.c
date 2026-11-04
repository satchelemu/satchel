#include "GuestDispatch.h"
#include "VulkanSubmission.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
static _Atomic unsigned checks;
static void check(int value){unsigned number=atomic_fetch_add(&checks,1)+1;if(!value){fprintf(stderr,"guest retirement check%u failed\n",number);exit(1);}}
typedef struct {pthread_mutex_t mutex;pthread_cond_t condition;int entered,release;} Pause;
typedef struct {Pause *pause;SatchelGuestDeviceBackendV2 actual;uint64_t token;int hold_drain,hold_destroy,fail_destroy;unsigned reentered;SatchelGuestDispatch **reentry_target;} Shim;
static void reenter(Shim *s){
    if(!s->reentry_target)return;
    SatchelGuestDispatch *before=*s->reentry_target;SatchelGuestRetirementReport report={0};
    check(!satchel_guest_dispatch_retire(s->reentry_target) && *s->reentry_target==before);
    check(!satchel_guest_dispatch_handoff(s->reentry_target) && *s->reentry_target==before);
    check(satchel_guest_dispatch_retirement_poll(NULL,NULL,&report)==SATCHEL_GUEST_RETIRE_INVALID);
    satchel_guest_dispatch_destroy(before);check(*s->reentry_target==before);
    SatchelGuestDeviceBackend table=s->actual.device;table.version=1;
    check(!satchel_guest_dispatch_set_device_backend(before,&table));++s->reentered;
}
static SatchelGuestBackendStatus create(void *context,uint32_t flags,uint64_t *token,SatchelGuestDeviceCaps *caps){Shim *s=context;reenter(s);
    if(s->pause){pthread_mutex_lock(&s->pause->mutex);s->pause->entered=1;pthread_cond_broadcast(&s->pause->condition);while(!s->pause->release)pthread_cond_wait(&s->pause->condition,&s->pause->mutex);pthread_mutex_unlock(&s->pause->mutex);}
    SatchelGuestBackendStatus status=s->actual.device.create(NULL,flags,token,caps);if(status==SATCHEL_GUEST_BACKEND_OK)s->token=*token;return status;}
static SatchelGuestBackendStatus destroy(void *context,uint64_t token){Shim *s=context;reenter(s);if(s->hold_destroy)return SATCHEL_GUEST_BACKEND_LIMIT;if(s->fail_destroy)return SATCHEL_GUEST_BACKEND_FAILURE;return s->actual.device.destroy(NULL,token);}
static SatchelGuestBackendStatus caps(void *context,uint64_t token,SatchelGuestDeviceCaps *result){Shim *s=context;return s->actual.device.capabilities(NULL,token,result);}
static SatchelGuestBackendStatus drain(void *context,uint64_t token){Shim *s=context;reenter(s);return s->hold_drain?SATCHEL_GUEST_BACKEND_LIMIT:s->actual.drain(NULL,token);}
typedef struct {SatchelCPUProbe *cpu;SatchelGuestDispatch *dispatch;SatchelGuestDispatchLayout layout;} Fixture;
static void put32(uint8_t *p,uint32_t value){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(value>>(i*8));}
static int attach(Fixture *f,Shim *s,int version){
    check(satchel_vulkan_guest_device_backend_v2(&s->actual));
    SatchelGuestDeviceBackendV2 backend={{(uint32_t)version,0,s,create,destroy,caps},drain};
    return version==2?satchel_guest_dispatch_set_device_backend_v2(f->dispatch,&backend):satchel_guest_dispatch_set_device_backend(f->dispatch,&backend.device);
}
static Fixture make(Shim *s,int version){
    Fixture f={0};uint8_t code[4096];uint8_t *data=calloc(1,65536);check(data!=NULL);
    for(unsigned i=0;i<sizeof(code);i+=4)put32(code+i,0xd4200000);put32(code,0xd63f0120);put32(code+4,0xd4000841);
    check(satchel_guest_dispatch_build_stubs(NULL,0,&f.layout));uint8_t *stubs=malloc(f.layout.region_size);check(stubs!=NULL);check(satchel_guest_dispatch_build_stubs(stubs,f.layout.region_size,&f.layout));
    SatchelCPUProbeRegion regions[]={{0x100000,code,4096,4096,5},{0x200000,stubs,f.layout.region_size,f.layout.region_size,5},{0x300000,data,65536,65536,3}};
    char error[160]={0};f.cpu=satchel_cpu_probe_create(regions,3,0x100000,error,sizeof(error));free(stubs);free(data);check(f.cpu!=NULL);
    f.dispatch=satchel_guest_dispatch_create(0x100100,0x200000,1000,30000);check(f.dispatch!=NULL);
    if(version)check(attach(&f,s,version));
    uint64_t points[2];check(satchel_guest_dispatch_breakpoints(f.dispatch,points));check(satchel_cpu_probe_set_breakpoints(f.cpu,points,2));return f;
}
static void invoke(Fixture *f,const char *name,uint64_t x0,uint64_t x1){
    uint64_t target=0;for(uint32_t i=1;i<=f->layout.stub_count;i++)if(!strcmp(name,satchel_guest_dispatch_name(i)))target=0x200000+16+(uint64_t)(i-1)*8;
    check(target!=0 && satchel_cpu_probe_set_pc(f->cpu,0x100000));check(satchel_cpu_probe_set_x(f->cpu,9,target));check(satchel_cpu_probe_set_x(f->cpu,0,x0));check(satchel_cpu_probe_set_x(f->cpu,1,x1));
    SatchelCPUProbeResult stop=satchel_cpu_probe_run(f->cpu,32,NULL,NULL);check(stop.reason==SATCHEL_CPU_BREAKPOINT && stop.steps==3);
    SatchelGuestDispatchReport report={0};check(satchel_guest_dispatch_handle(f->dispatch,f->cpu,&stop,NULL,NULL,&report)==SATCHEL_GUEST_DISPATCH_RETURNED);
}
static void initialize(Fixture *f){invoke(f,"nvnDeviceBuilderSetDefaults",0x300100,0);invoke(f,"nvnDeviceInitialize",0x301000,0x300100);uint64_t value=0;check(satchel_cpu_probe_get_x(f->cpu,0,&value) && value==1);}
static SatchelGuestRetirementReport stats(void){SatchelGuestRetirementReport report={0};check(satchel_guest_dispatch_retirement_stats(&report));return report;}
static int cancel(void *context){(void)context;return 1;}
static void collect(void){
    struct timespec pause={0,1000000};SatchelGuestRetirementReport report={0};SatchelGuestRetirementStatus status=SATCHEL_GUEST_RETIRE_PENDING;
    for(unsigned i=0;i<5000 && status==SATCHEL_GUEST_RETIRE_PENDING;i++){status=satchel_guest_dispatch_retirement_poll(NULL,NULL,&report);if(status==SATCHEL_GUEST_RETIRE_PENDING)nanosleep(&pause,NULL);}
    check(status==SATCHEL_GUEST_RETIRE_IDLE && !report.retiring_contexts && !report.pending_devices);
}
static uint64_t submit(Shim *s){uint64_t buffer=0,job=0;SatchelVulkanBufferInfo info={0};check(satchel_vulkan_buffer_create(s->token,16,7,&buffer,&info)==SATCHEL_VULKAN_OK);check(satchel_vulkan_buffer_fill(s->token,buffer,0,16,0x13579bdf,&job,NULL,NULL)==SATCHEL_VULKAN_OK && job);return job;}
typedef struct {Fixture *fixture;_Atomic int started,done;int result;} HandoffThread;
static void *initialize_thread(void *context){Fixture *f=context;invoke(f,"nvnDeviceInitialize",0x301000,0x300100);return NULL;}
static void *handoff_thread(void *context){HandoffThread *work=context;atomic_store(&work->started,1);work->result=satchel_guest_dispatch_handoff(&work->fixture->dispatch);atomic_store(&work->done,1);return NULL;}
int main(void){
    check(!stats().reserved_contexts && !satchel_vulkan_device_live_count());
    check(!satchel_guest_dispatch_retire(NULL));check(!satchel_guest_dispatch_retirement_stats(NULL));check(satchel_guest_dispatch_retirement_poll(NULL,NULL,NULL)==SATCHEL_GUEST_RETIRE_INVALID);
    Shim s={0};Fixture spare=make(NULL,0),f=make(&s,2);s.reentry_target=&spare.dispatch;initialize(&f);check(s.reentered==1);
    invoke(&f,"nvnDeviceFinalize",0x301000,0);check(s.reentered==2);initialize(&f);check(s.reentered==3);uint64_t job=submit(&s);
    s.hold_drain=1;s.reentry_target=&spare.dispatch;
    check(satchel_guest_dispatch_retire(&f.dispatch) && !f.dispatch);check(!satchel_guest_dispatch_retire(&f.dispatch));
    satchel_cpu_probe_destroy(f.cpu);f.cpu=NULL; /* Retired owner must never dereference the freed CPU. */
    check(stats().retiring_contexts==1 && stats().pending_devices==1);
    SatchelGuestRetirementReport report={0};check(satchel_guest_dispatch_retirement_poll(cancel,NULL,&report)==SATCHEL_GUEST_RETIRE_CANCELLED && report.pending_devices==1);
    check(satchel_guest_dispatch_retirement_poll(NULL,NULL,&report)==SATCHEL_GUEST_RETIRE_PENDING && s.reentered==4 && report.backend_failures==1);
    check(satchel_vulkan_device_destroy(s.token)==SATCHEL_VULKAN_BUSY && satchel_vulkan_submission_live_count()==1);
    s.hold_drain=0;collect();check(s.reentered>=6 && !stats().reserved_contexts && !satchel_vulkan_device_live_count() && !satchel_vulkan_submission_live_count());
    SatchelVulkanSubmissionReport completion={0};check(satchel_vulkan_submission_wait(s.token,job,0,NULL,NULL,&completion)==SATCHEL_VULKAN_INVALID);
    satchel_guest_dispatch_destroy(spare.dispatch);satchel_cpu_probe_destroy(spare.cpu);
    // Version1 compatibility: void destroy retains failed/BUSY ownership. A
    // legacy caller still owning the job reaps it, then collection can finish.
    memset(&s,0,sizeof(s));f=make(&s,1);initialize(&f);job=submit(&s);s.hold_destroy=1;
    satchel_guest_dispatch_destroy(f.dispatch);f.dispatch=NULL;satchel_cpu_probe_destroy(f.cpu);
    check(stats().retiring_contexts==1 && stats().pending_devices==1 && satchel_vulkan_device_live_count()==1);
    s.hold_destroy=0;check(satchel_guest_dispatch_retirement_poll(NULL,NULL,&report)==SATCHEL_GUEST_RETIRE_PENDING);
    check(satchel_vulkan_submission_wait(s.token,job,5000,NULL,NULL,&completion)==SATCHEL_VULKAN_OK && completion.completed);collect();check(!satchel_vulkan_device_live_count());
    // Active and retiring contexts share the same pre-reserved cap. Admission
    // fails before any fifth backend/device ownership can be created.
    Shim shims[4]={0};Fixture fixtures[4];
    for(unsigned i=0;i<4;i++){fixtures[i]=make(&shims[i],2);initialize(&fixtures[i]);shims[i].hold_drain=1;if(i<2){check(satchel_guest_dispatch_retire(&fixtures[i].dispatch));satchel_cpu_probe_destroy(fixtures[i].cpu);}}
    check(stats().active_contexts==2 && stats().retiring_contexts==2 && stats().reserved_contexts==4);
    Shim fifth={0};Fixture rejected=make(NULL,0);check(!attach(&rejected,&fifth,2));check(!fifth.token && stats().reserved_contexts==4);
    check(satchel_guest_dispatch_retire(&rejected.dispatch) && !rejected.dispatch);satchel_cpu_probe_destroy(rejected.cpu);
    for(unsigned i=2;i<4;i++){check(satchel_guest_dispatch_retire(&fixtures[i].dispatch));satchel_cpu_probe_destroy(fixtures[i].cpu);}
    check(satchel_guest_dispatch_retirement_poll(NULL,NULL,&report)==SATCHEL_GUEST_RETIRE_PENDING && report.pending_devices==4 && report.retiring_contexts==4);
    for(unsigned i=0;i<4;i++)shims[i].hold_drain=0;collect();check(!stats().reserved_contexts && !satchel_vulkan_device_live_count());
    memset(&s,0,sizeof(s));f=make(&s,2);initialize(&f);s.fail_destroy=1;check(satchel_guest_dispatch_retire(&f.dispatch));satchel_cpu_probe_destroy(f.cpu);
    check(satchel_guest_dispatch_retirement_poll(NULL,NULL,&report)==SATCHEL_GUEST_RETIRE_PENDING && report.last_backend_status==SATCHEL_GUEST_BACKEND_FAILURE && report.pending_devices==1);
    s.fail_destroy=0;collect();check(!stats().reserved_contexts && !satchel_vulkan_device_live_count());
    SatchelVulkanBufferStats buffers={0};check(satchel_vulkan_buffer_stats(&buffers)==SATCHEL_VULKAN_OK && !buffers.live_buffers && !buffers.allocated_bytes && !satchel_vulkan_submission_live_count());
    // A second runtime worker can hand off an inactive context after freeing
    // its CPU while another original handler is inside a backend callback.
    // Handoff waits behind that handler; strict retire/poll remain nonblocking.
    Pause pause={PTHREAD_MUTEX_INITIALIZER,PTHREAD_COND_INITIALIZER,0,0};Shim running={0},closing={0};
    Fixture active=make(&running,2),inactive=make(&closing,2);initialize(&inactive);
    invoke(&active,"nvnDeviceBuilderSetDefaults",0x300100,0);running.pause=&pause;
    pthread_t interpreter,cleanup;check(!pthread_create(&interpreter,NULL,initialize_thread,&active));
    pthread_mutex_lock(&pause.mutex);while(!pause.entered)pthread_cond_wait(&pause.condition,&pause.mutex);pthread_mutex_unlock(&pause.mutex);
    satchel_cpu_probe_destroy(inactive.cpu);inactive.cpu=NULL;
    HandoffThread work={.fixture=&inactive};check(!pthread_create(&cleanup,NULL,handoff_thread,&work));
    struct timespec brief={0,1000000};while(!atomic_load(&work.started))nanosleep(&brief,NULL);
    check(!satchel_guest_dispatch_retire(&inactive.dispatch) && inactive.dispatch!=NULL);
    check(satchel_guest_dispatch_retirement_poll(NULL,NULL,&report)==SATCHEL_GUEST_RETIRE_BUSY);
    check(!atomic_load(&work.done));
    pthread_mutex_lock(&pause.mutex);pause.release=1;pthread_cond_broadcast(&pause.condition);pthread_mutex_unlock(&pause.mutex);
    check(!pthread_join(interpreter,NULL));check(!pthread_join(cleanup,NULL));
    check(work.result==1 && atomic_load(&work.done) && !inactive.dispatch && stats().retiring_contexts==1 && stats().active_contexts==1);
    collect();check(satchel_vulkan_device_live_count()==1);
    check(satchel_guest_dispatch_handoff(&active.dispatch) && !active.dispatch);satchel_cpu_probe_destroy(active.cpu);collect();
    check(!stats().reserved_contexts && !satchel_vulkan_device_live_count());
    check(!pthread_mutex_destroy(&pause.mutex));check(!pthread_cond_destroy(&pause.condition));
    check(!satchel_guest_dispatch_handoff(NULL));
    printf("Original interpreted device retirement: %u ASan/UBSan assertions passed; pending jobs, legacy/explicit ownership, reservation cap, freed CPU independence, cancellation and callback reentry verified.\n",checks);
}
