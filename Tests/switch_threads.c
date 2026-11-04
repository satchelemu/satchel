#define main original_process_regression_main
#include "switch_process.c"
#undef main
#include <time.h>

static SatchelSwitchProcess *thread_fixture(void) {
    uint8_t image[0x12000]={0};
    const uint32_t code[]={0xd40003e1,0xd40002c1,0xd4000181,0xd40001c1,0xd40004a1,0xd4000421,
        0xd4000101,0xd4000121,0xd4000301,0xd4000321,0xd4000141,0xd40002e1,0xd4000161,0xd40003c1,0xd4000fe1};
    memcpy(image,code,sizeof(code));memcpy(image+0x1000,"sm:",4);
    // Main writes a visible progress marker, then reaches an honest unsupported
    // boundary. Child arguments/register snapshots are only original fixtures.
    const uint32_t main_code[]={0x52824684,0xb9000284,0xd4000fe1}; // MOV W4,1234; STR W4,[X20]; SVC0x7f
    memcpy(image+0x80,main_code,sizeof(main_code));
    const uint32_t child[]={
        0xaa0003f3,0xf9000e61,0x3d801260,0x3d80167f,0x910003e5,0xf9001a65,0xf9001e72,
        0xd53bd065,0xf9001265,0xb94110a6,0xb9001266,0x128fffe1,0xd40004a1,0xf9001661,
        0x128fffe2,0xd40001c1,0xb9007261,0xf9003e62,0x128fffe1,0xd4000181,0xb9008261,
        0xb940a260,0xd4000421,0xb900a660,
        0xaa1303e1,0xd2800022,0x92800003,0xd4000301,
        0xb9000660,0xb9000a61,0x528eeee4,0xb9000e64,0xd4000141};
    memcpy(image+0x100,child,sizeof(child));
    const uint32_t lifetime_child[]={0xaa0003f3,0xaa0003e1,0xd2800022,0x92800003,0xd4000301,0xb9000660,0xd4000fe1,0xd4000141};
    memcpy(image+0x200,lifetime_child,sizeof(lifetime_child));
    const uint32_t cancel_child[]={0xd4000321,0xd4000141};memcpy(image+0x280,cancel_child,sizeof(cancel_child));
    uint32_t exit_instruction=0xd4000141;memcpy(image+0x300,&exit_instruction,4);
    uint32_t affinity_instruction=0xd40001e1;memcpy(image+0x360,&affinity_instruction,4);
    const uint32_t mutex_svc[]={0xd4000341,0xd4000361,0xd4000081,0xd40000a1};memcpy(image+0x380,mutex_svc,sizeof(mutex_svc));
    uint32_t spin=0x14000000;memcpy(image+0x3c0,&spin,4);
    const uint32_t mutex_child[]={0xaa0003f3,0xb9400260,0xf9400661,0xb9400662,0xd4000341,
        0xb9001260,0x528b4b44,0xb9001664,0xd4000fe1,0xf9400660,0xd4000361,0xd4000141};
    memcpy(image+0x400,mutex_child,sizeof(mutex_child));
    uint32_t multiply=0x1e610802;memcpy(image+0x340,&multiply,4);
    const uint32_t context_code[]={0xc85f7c01,0xc8027c03,0xffffffff};memcpy(image+0x480,context_code,sizeof(context_code));
    SatchelNSOInfo nso={.image_size=sizeof(image),.bss_size=sizeof(image)-0x2010,
        .segments={{0,0x500,0,0x500},{0,4,0x1000,4},{0,16,0x2000,16}}};
    SatchelSwitchProcessModule modules[]={{"rtld",nso,image,sizeof(image)},{"main",nso,image,sizeof(image)}};
    SatchelProcessMetadata metadata={.program_id=1,.address_space_size=UINT64_C(1)<<39,.main_thread_stack_size=0x4000,
        .address_space_bits=39,.address_space_type=3,.flags0=7,.is_64_bit=true,.system_resource_size=16*1024*1024,
        .main_thread_priority=44,.default_cpu_id=0,.has_core_priority=true,.allowed_core_mask=7,.allowed_priority_mask=UINT64_MAX & ~UINT64_C(15)};
    char error[256];SatchelSwitchProcess *p=satchel_switch_process_create(modules,2,&metadata,error,sizeof(error));CHECK(p);
    return p;
}
static SatchelSwitchThreadInfo find_thread(SatchelSwitchProcess *p,uint32_t handle) {
    SatchelSwitchThreadInfo info={0};
    for(unsigned i=0;i<32;i++) if(satchel_switch_process_thread_info(p,i,&info)&&info.handle==handle)return info;
    CHECK(0);return info;
}
static uint32_t thread_create(SatchelSwitchProcess *p,uint64_t entry,uint64_t argument,uint64_t stack,int priority,int core,uint32_t expected) {
    SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);CHECK(cpu);
    CHECK(satchel_cpu_probe_set_pc(cpu,0x8000018));
    CHECK(satchel_cpu_probe_set_x(cpu,0,UINT64_MAX)&&satchel_cpu_probe_set_x(cpu,1,entry)&&satchel_cpu_probe_set_x(cpu,2,argument)&&
        satchel_cpu_probe_set_x(cpu,3,stack)&&satchel_cpu_probe_set_x(cpu,4,(uint32_t)priority)&&satchel_cpu_probe_set_x(cpu,5,(uint32_t)core));
    SatchelSwitchRunResult result=satchel_switch_process_run_scheduled(p,1,NULL,NULL);
    CHECK(result.state==SATCHEL_SWITCH_RUN_CPU_STOP&&result.cpu.reason==SATCHEL_CPU_BUDGET&&result.cpu.steps==1);
    uint64_t value,handle;CHECK(satchel_cpu_probe_get_x(cpu,0,&value)&&value==expected);
    CHECK(satchel_cpu_probe_get_x(cpu,1,&handle));CHECK(expected?handle==0:handle>=0x81);
    CHECK(satchel_cpu_probe_get_x(cpu,2,&value)&&value==argument);
    return (uint32_t)handle;
}
static void thread_svc(SatchelSwitchProcess *p,uint64_t pc,uint64_t x0,uint32_t expected) {
    SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);
    CHECK(satchel_cpu_probe_set_pc(cpu,pc)&&satchel_cpu_probe_set_x(cpu,0,x0));
    SatchelSwitchRunResult result=satchel_switch_process_run_scheduled(p,1,NULL,NULL);
    CHECK(result.state==SATCHEL_SWITCH_RUN_CPU_STOP&&result.cpu.reason==SATCHEL_CPU_BUDGET&&result.cpu.steps==1);
    uint64_t value;CHECK(satchel_cpu_probe_get_x(cpu,0,&value)&&value==expected);
}
static SatchelSwitchRunResult thread_wait(SatchelSwitchProcess *p,uint64_t handles,int count,int64_t ns,uint64_t budget) {
    SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);
    CHECK(satchel_cpu_probe_set_pc(cpu,0x8000020)&&satchel_cpu_probe_set_x(cpu,0,UINT64_MAX)&&satchel_cpu_probe_set_x(cpu,1,handles)&&
        satchel_cpu_probe_set_x(cpu,2,(uint32_t)count)&&satchel_cpu_probe_set_x(cpu,3,(uint64_t)ns));
    return satchel_switch_process_run_scheduled(p,budget,NULL,NULL);
}
static void make_events(SatchelSwitchProcess *p,uint32_t *message,uint32_t *suspended,uint32_t *gpu) {
    uint8_t bytes[256],name[8]={'a','p','p','l','e','t','O','E'};
    uint32_t sm=port_call(p,0x8001000,0);
    tipc_sm_request(bytes,0,1,NULL,0);sm_ipc(p,sm,bytes,1,0,0);
    tipc_sm_request(bytes,1,0,name,8);uint32_t root=lm_reply(p,sm,bytes,1,NULL,0);
    applet_proxy_request(bytes,0,0xffff8001,1,1,0);uint32_t proxy=lm_reply(p,root,bytes,1,NULL,0);
    uint32_t commands[3]={0,1,20},children[3];
    for(unsigned i=0;i<3;i++) {applet_profile_request(bytes,0,commands[i],NULL,0);children[i]=lm_reply(p,proxy,bytes,1,NULL,0);}
    applet_profile_request(bytes,0,0,NULL,0);*message=applet_event_reply(p,children[0],bytes,0);
    applet_profile_request(bytes,0,91,NULL,0);*suspended=applet_event_reply(p,children[1],bytes,0);
    applet_profile_request(bytes,0,130,NULL,0);*gpu=applet_event_reply(p,children[2],bytes,0);
    for(unsigned i=0;i<3;i++)handle_close(p,children[i],0);
    handle_close(p,proxy,0);handle_close(p,root,0);handle_close(p,sm,0);
}
static void main_boundary(SatchelSwitchProcess *p,SatchelCPUProbe *main_cpu) {
    CHECK(satchel_cpu_probe_set_x(main_cpu,20,0x8006000)&&satchel_cpu_probe_set_pc(main_cpu,0x8000080));
    SatchelSwitchRunResult result=satchel_switch_process_run_scheduled(p,200,NULL,NULL);
    CHECK(result.state==SATCHEL_SWITCH_RUN_CPU_STOP&&result.cpu.reason==SATCHEL_CPU_SVC&&result.cpu.svc==0x7f);
    CHECK(satchel_switch_process_cpu(p)==main_cpu);
    uint32_t marker;CHECK(satchel_cpu_probe_read(main_cpu,0x8006000,&marker,4)&&marker==0x1234);
}
static void test_watcher(void) {
    SatchelSwitchProcess *p=thread_fixture();SatchelCPUProbe *main_cpu=satchel_switch_process_cpu(p);
    uint32_t message,suspended,gpu;make_events(p,&message,&suspended,&gpu);
    uint64_t argument=0x8004000,stack=initial_stack39+0x3fef;
    CHECK(satchel_cpu_probe_write(main_cpu,argument,&gpu,4));
    uint32_t child_sm=port_call(p,0x8001000,0);
    CHECK(satchel_cpu_probe_write(main_cpu,argument+160,&child_sm,4));
    uint32_t child=thread_create(p,0x8000101,argument,stack,28,2,0);
    SatchelSwitchThreadInfo info=find_thread(p,child);
    CHECK(info.state==SATCHEL_SWITCH_THREAD_CREATED&&info.cpu_id==2&&info.affinity_mask==4&&info.priority==28&&!info.instructions);
    CHECK(info.tls_address==0x31000200&&info.id!=find_thread(p,0x80).id);
    uint8_t tls[0x200],expected_tls[0x200]={0};memcpy(expected_tls+0x110,&child,4);
    CHECK(satchel_cpu_probe_read(main_cpu,info.tls_address,tls,sizeof(tls))&&!memcmp(tls,expected_tls,sizeof(tls)));
    thread_svc(p,0x800001c,0xffff8001,0xe401);
    thread_svc(p,0x800001c,0xffff8000,0xfa01);
    uint8_t initialize[256], main_tls[256], after_tls[256];
    tipc_sm_request(initialize,0,1,NULL,0);
    CHECK(satchel_cpu_probe_write(main_cpu,info.tls_address,initialize,sizeof(initialize)));
    CHECK(satchel_cpu_probe_read(main_cpu,0x31000000,main_tls,sizeof(main_tls)));
    thread_svc(p,0x800001c,child,0);
    main_boundary(p,main_cpu);
    CHECK(satchel_cpu_probe_read(main_cpu,0x31000000,after_tls,sizeof(after_tls))&&!memcmp(main_tls,after_tls,sizeof(main_tls)));
    CHECK(satchel_switch_process_is_sm_initialized(p,child_sm));
    CHECK(satchel_cpu_probe_read(main_cpu,argument+164,after_tls,4)&&after_tls[0]==0&&after_tls[1]==0&&after_tls[2]==0&&after_tls[3]==0);

    info=find_thread(p,child);CHECK(info.state==SATCHEL_SWITCH_THREAD_WAITING&&info.instructions>0);
    uint32_t value;CHECK(satchel_cpu_probe_read(main_cpu,argument+12,&value,4)&&value==0);
    uint64_t value64;CHECK(satchel_cpu_probe_read(main_cpu,argument+24,&value64,8)&&value64==0); // ChildX1, not main handle.
    CHECK(satchel_cpu_probe_read(main_cpu,argument+32,&value64,8)&&value64==info.tls_address);
    CHECK(satchel_cpu_probe_read(main_cpu,argument+40,&value64,8)&&value64==info.id);
    CHECK(satchel_cpu_probe_read(main_cpu,argument+48,&value64,8)&&value64==(stack&~UINT64_C(15)));
    CHECK(satchel_cpu_probe_read(main_cpu,argument+56,&value64,8)&&(value64&1));
    CHECK(satchel_cpu_probe_read(main_cpu,argument+16,&value,4)&&value==child);
    CHECK(satchel_cpu_probe_read(main_cpu,argument+112,&value,4)&&value==2);
    CHECK(satchel_cpu_probe_read(main_cpu,argument+120,&value64,8)&&value64==4);
    CHECK(satchel_cpu_probe_read(main_cpu,argument+128,&value,4)&&value==28);

    uint8_t vectors[32],zeros[32]={0};CHECK(satchel_cpu_probe_read(main_cpu,argument+64,vectors,sizeof(vectors))&&!memcmp(vectors,zeros,sizeof(vectors)));
    handle_close(p,gpu,0);CHECK(!satchel_switch_process_is_readable_event(p,gpu));
    CHECK(find_thread(p,child).state==SATCHEL_SWITCH_THREAD_WAITING);
    CHECK(satchel_switch_process_report_gpu_error(p));
    CHECK(find_thread(p,child).state==SATCHEL_SWITCH_THREAD_RUNNABLE);
    main_boundary(p,main_cpu);
    info=find_thread(p,child);CHECK(info.state==SATCHEL_SWITCH_THREAD_EXITED);
    CHECK(satchel_cpu_probe_read(main_cpu,argument+4,&value,4)&&value==0);
    CHECK(satchel_cpu_probe_read(main_cpu,argument+8,&value,4)&&value==0);
    CHECK(satchel_cpu_probe_read(main_cpu,argument+12,&value,4)&&value==0x7777);
    CHECK(satchel_cpu_probe_write(main_cpu,argument,&child,4));
    SatchelSwitchRunResult waited=thread_wait(p,argument,1,0,1);
    CHECK(waited.state==SATCHEL_SWITCH_RUN_CPU_STOP&&waited.cpu.reason==SATCHEL_CPU_BUDGET);
    CHECK(satchel_cpu_probe_get_x(main_cpu,0,&value64)&&value64==0);CHECK(satchel_cpu_probe_get_x(main_cpu,1,&value64)&&value64==0);
    handle_close(p,child,0);CHECK(satchel_switch_process_thread_count(p)==1);
    thread_svc(p,0x800001c,child,0xe401);
    handle_close(p,message,0);handle_close(p,suspended,0);handle_close(p,child_sm,0);satchel_switch_process_destroy(p);
}
static void test_wait_order_and_timers(void) {
    SatchelSwitchProcess *p=thread_fixture();SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);
    uint32_t message,suspended,gpu;make_events(p,&message,&suspended,&gpu);uint64_t list=0x8004000,value;
    uint32_t handles[2]={suspended,0xffff8000};CHECK(satchel_cpu_probe_write(cpu,list,handles,sizeof(handles)));
    CHECK(thread_wait(p,list,65,0,1).cpu.reason==SATCHEL_CPU_BUDGET);CHECK(satchel_cpu_probe_get_x(cpu,0,&value)&&value==0xee01);
    CHECK(thread_wait(p,UINT64_MAX,1,0,1).cpu.reason==SATCHEL_CPU_BUDGET);CHECK(satchel_cpu_probe_get_x(cpu,0,&value)&&value==0xe601);
    CHECK(thread_wait(p,list,2,0,1).cpu.reason==SATCHEL_CPU_BUDGET);CHECK(satchel_cpu_probe_get_x(cpu,0,&value)&&value==0xe401);
    thread_svc(p,0x8000024,0xffff8000,0); // Queue cancellation on running main.
    CHECK(thread_wait(p,list,1,0,1).cpu.reason==SATCHEL_CPU_BUDGET);CHECK(satchel_cpu_probe_get_x(cpu,0,&value)&&value==0xea01);
    CHECK(satchel_switch_process_report_gpu_error(p));handles[0]=gpu;handles[1]=0xffff8000;CHECK(satchel_cpu_probe_write(cpu,list,handles,8));
    CHECK(thread_wait(p,list,2,0,1).cpu.reason==SATCHEL_CPU_BUDGET);CHECK(satchel_cpu_probe_get_x(cpu,0,&value)&&value==0xe401); // Validate all before ready.
    handles[1]=gpu;CHECK(satchel_cpu_probe_write(cpu,list,handles,8));
    CHECK(thread_wait(p,list,2,0,1).cpu.reason==SATCHEL_CPU_BUDGET);CHECK(satchel_cpu_probe_get_x(cpu,0,&value)&&!value);
    CHECK(satchel_cpu_probe_get_x(cpu,1,&value)&&!value); // Lowest ready index.
    handles[0]=suspended;CHECK(satchel_cpu_probe_write(cpu,list,handles,4));
    CHECK(thread_wait(p,list,1,-1,1).cpu.reason==SATCHEL_CPU_BUDGET);CHECK(satchel_cpu_probe_get_x(cpu,0,&value)&&value==0xec01); // Pending cancel survived both earlier outcomes.
    thread_svc(p,0x800002c,gpu,0);thread_svc(p,0x800002c,gpu,0xfa01);
    CHECK(satchel_switch_process_gpu_error_event_signaled(p)==0);
    SatchelSwitchRunResult blocked=thread_wait(p,list,1,5000000,100);
    CHECK(blocked.state==SATCHEL_SWITCH_RUN_BLOCKED&&blocked.cpu.steps==1&&blocked.next_deadline_tick!=UINT64_MAX);
    CHECK(find_thread(p,0x80).state==SATCHEL_SWITCH_THREAD_WAITING);
    SatchelSwitchRunResult cancelled=satchel_switch_process_run_scheduled(p,100,cancel,NULL);
    CHECK(cancelled.state==SATCHEL_SWITCH_RUN_CPU_STOP&&cancelled.cpu.reason==SATCHEL_CPU_CANCELLED&&cancelled.cpu.steps==0);
    CHECK(find_thread(p,0x80).state==SATCHEL_SWITCH_THREAD_WAITING);
    uint64_t breakpoint=0x8000024;CHECK(satchel_cpu_probe_set_breakpoints(cpu,&breakpoint,1));
    struct timespec delay={0,20000000};nanosleep(&delay,NULL);
    SatchelSwitchRunResult timed=satchel_switch_process_run_scheduled(p,100,NULL,NULL);
    CHECK(timed.state==SATCHEL_SWITCH_RUN_CPU_STOP&&timed.cpu.reason==SATCHEL_CPU_BREAKPOINT&&timed.cpu.steps==0);
    CHECK(satchel_cpu_probe_get_x(cpu,0,&value)&&value==0xea01);CHECK(satchel_cpu_probe_get_x(cpu,1,&value)&&value==UINT32_MAX);
    CHECK(satchel_cpu_probe_set_breakpoints(cpu,NULL,0));
    handle_close(p,message,0);handle_close(p,suspended,0);handle_close(p,gpu,0);satchel_switch_process_destroy(p);
}
static void test_recycled_threads(void) {
    SatchelSwitchProcess *p=thread_fixture();SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);
    uint64_t previous_id=0;uint32_t previous_handle=0;
    for(unsigned i=0;i<80;i++) {
        uint32_t handle=thread_create(p,0x8000300,0,initial_stack39+0x2000,28,2,0);
        SatchelSwitchThreadInfo info=find_thread(p,handle);
        CHECK(info.id>previous_id&&handle>previous_handle&&info.tls_address==0x31000200);
        previous_id=info.id;previous_handle=handle;
        uint8_t tls[0x200],expected[0x200]={0};memcpy(expected+0x110,&handle,4);
        CHECK(satchel_cpu_probe_read(cpu,info.tls_address,tls,sizeof(tls))&&!memcmp(tls,expected,sizeof(tls)));
        thread_svc(p,0x800001c,handle,0);main_boundary(p,cpu);
        CHECK(find_thread(p,handle).state==SATCHEL_SWITCH_THREAD_EXITED);
        memset(tls,0xa7,sizeof(tls));CHECK(satchel_cpu_probe_write(cpu,info.tls_address,tls,sizeof(tls)));
        handle_close(p,handle,0);CHECK(satchel_switch_process_thread_count(p)==1);
    }
    thread_create(p,0x8000300,0,0,64,0,0xe001);thread_create(p,0x8000300,0,0,28,3,0xe201);
    // Started lower-priority same-core children keep real run refs after their
    // handles close; all32 concurrent contexts consume four actual TLS pages.
    for(unsigned i=1;i<32;i++) {
        uint32_t handle=thread_create(p,0x8000300,0,initial_stack39+0x2000,50,0,0);
        SatchelSwitchThreadInfo info=find_thread(p,handle);
        CHECK(info.tls_address==0x31000000+(i/8)*4096+(i%8)*0x200);
        thread_svc(p,0x800001c,handle,0);handle_close(p,handle,0);
        CHECK(satchel_switch_process_thread_count(p)==i+1);
    }
    thread_create(p,0x8000300,0,0,50,0,0x10801);
    CHECK(satchel_cpu_probe_set_pc(cpu,0x8000028));
    SatchelSwitchRunResult exited=satchel_switch_process_run_scheduled(p,200,NULL,NULL);
    CHECK(exited.state==SATCHEL_SWITCH_RUN_EXITED&&exited.cpu.steps==32);
    CHECK(satchel_switch_process_thread_count(p)==1); // Main handle retains its exited object.
    satchel_switch_process_destroy(p);
}
static void test_thread_fp_inheritance(void) {
    SatchelSwitchProcess *p=thread_fixture();SatchelCPUProbe *parent=satchel_switch_process_cpu(p);
    uint64_t q0[2]={UINT64_C(0x7fefffffffffffff),UINT64_MAX},q1[2]={UINT64_C(0x4000000000000000),UINT64_MAX};
    CHECK(satchel_cpu_probe_set_q(parent,0,(const uint8_t*)q0)&&satchel_cpu_probe_set_q(parent,1,(const uint8_t*)q1));
    CHECK(satchel_cpu_probe_set_pc(parent,0x8000340));
    CHECK(satchel_switch_process_run_scheduled(p,1,NULL,NULL).cpu.reason==SATCHEL_CPU_BUDGET);
    uint32_t control=99,status=99;
    CHECK(satchel_cpu_probe_get_fp_status(parent,&control,&status)==SATCHEL_CPU_FP_OK&&control==0&&status==20);
    uint32_t child=thread_create(p,0x8000300,0,initial_stack39+0x2000,28,2,0);
    uint64_t point=0x8000300;CHECK(satchel_cpu_probe_set_breakpoints(parent,&point,1));
    thread_svc(p,0x800001c,child,0);
    CHECK(satchel_cpu_probe_set_pc(parent,0x8000080)&&satchel_cpu_probe_set_x(parent,20,0x8006000));
    SatchelSwitchRunResult stopped=satchel_switch_process_run_scheduled(p,100,NULL,NULL);
    CHECK(stopped.state==SATCHEL_SWITCH_RUN_CPU_STOP&&stopped.cpu.reason==SATCHEL_CPU_BREAKPOINT&&stopped.thread_id==find_thread(p,child).id);
    SatchelCPUProbe *active=satchel_switch_process_cpu(p);CHECK(active!=parent);
    CHECK(satchel_cpu_probe_get_fp_status(active,&control,&status)==SATCHEL_CPU_FP_OK&&!control&&status==20);
    uint8_t q[16],zero[16]={0};
    for(unsigned i=0;i<32;i++)CHECK(satchel_cpu_probe_get_q(active,i,q)&&!memcmp(q,zero,16));
    CHECK(satchel_cpu_probe_get_q(parent,0,q)&&!memcmp(q,q0,16));
    CHECK(satchel_cpu_probe_set_breakpoints(active,NULL,0));
    // Resolved CPU_STOP resumes automatically by scheduling, no fake exit.
    for(unsigned i=0;i<3&&find_thread(p,child).state!=SATCHEL_SWITCH_THREAD_EXITED;i++)main_boundary(p,parent);
    CHECK(find_thread(p,child).state==SATCHEL_SWITCH_THREAD_EXITED);
    handle_close(p,child,0);satchel_switch_process_destroy(p);
}
static void test_all_tls_storage_exclusion(void) {
    SatchelSwitchProcess *p=thread_fixture();SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);
    TestStorage backing={.size=4096};SatchelSwitchStorage provider={&backing,backing.size,test_storage_read};
    CHECK(satchel_switch_process_attach_storage(p,&provider));
    uint32_t children[8];
    for(unsigned i=0;i<8;i++)children[i]=thread_create(p,0x8000300,0,0,28,2,0);
    SatchelSwitchThreadInfo last=find_thread(p,children[7]);CHECK(last.tls_address==0x31001000);
    uint8_t bytes[256],name[8]={'f','s','p','-','s','r','v'};
    uint32_t sm=port_call(p,0x8001000,0);tipc_sm_request(bytes,0,1,NULL,0);sm_ipc(p,sm,bytes,1,0,0);
    tipc_sm_request(bytes,1,0,name,8);uint32_t fsp=lm_reply(p,sm,bytes,1,NULL,0);
    sm_request(bytes,1,1,name,4,0,0);sm_ipc(p,fsp,bytes,1,0,0);
    applet_profile_request(bytes,0,200,NULL,0);uint32_t storage=lm_reply(p,fsp,bytes,1,NULL,0);
    const unsigned offsets[]={0,0x300,0xf00};
    for(unsigned i=0;i<3;i++) {
        uint64_t address=last.tls_address+offsets[i];uint8_t before[64],after[64];
        CHECK(satchel_cpu_probe_read(cpu,address,before,sizeof(before)));
        storage_request_bytes(bytes,0,0,0,64,address,64,0);sm_ipc(p,storage,bytes,0,0,0);
        SatchelSwitchIPCInfo info;CHECK(satchel_switch_process_last_ipc(p,&info)&&info.blocked_reason==SATCHEL_SWITCH_IPC_BLOCK_BUFFER);
        CHECK(!backing.calls&&satchel_cpu_probe_read(cpu,address,after,sizeof(after))&&!memcmp(before,after,sizeof(before)));
    }
    for(unsigned i=0;i<8;i++)handle_close(p,children[i],0);
    CHECK(satchel_switch_process_thread_count(p)==1);
    // Released slot pages remain truly allocated/protected until process teardown.
    storage_request_bytes(bytes,0,0,0,64,last.tls_address+0x300,64,0);sm_ipc(p,storage,bytes,0,0,0);
    CHECK(!backing.calls);
    handle_close(p,storage,0);handle_close(p,fsp,0);handle_close(p,sm,0);satchel_switch_process_destroy(p);
}
static void test_reaped_main_lifetime(void) {
    SatchelSwitchProcess *p=thread_fixture();SatchelCPUProbe *main_cpu=satchel_switch_process_cpu(p);
    uint32_t message,suspended,gpu;make_events(p,&message,&suspended,&gpu);
    uint64_t argument=0x8004000;CHECK(satchel_cpu_probe_write(main_cpu,argument,&gpu,4));
    uint32_t child=thread_create(p,0x8000200,argument,initial_stack39+0x2000,28,2,0);
    thread_svc(p,0x800001c,child,0);main_boundary(p,main_cpu);
    CHECK(find_thread(p,child).state==SATCHEL_SWITCH_THREAD_WAITING);
    handle_close(p,child,0);handle_close(p,0x80,0);
    CHECK(satchel_cpu_probe_set_pc(main_cpu,0x8000028));
    SatchelSwitchRunResult blocked=satchel_switch_process_run_scheduled(p,100,NULL,NULL);
    main_cpu=NULL; // The old context was destroyed. Never dereference it again.
    CHECK(blocked.state==SATCHEL_SWITCH_RUN_BLOCKED&&blocked.next_deadline_tick==UINT64_MAX&&blocked.cpu.steps==1);
    CHECK(satchel_switch_process_cpu(p)==NULL&&satchel_switch_process_thread_count(p)==1);
    CHECK(blocked.thread_id!=0&&blocked.thread_id!=find_thread(p,child).id);
    SatchelSwitchRunResult again=satchel_switch_process_run_scheduled(p,100,NULL,NULL);
    CHECK(again.state==SATCHEL_SWITCH_RUN_BLOCKED&&again.cpu.steps==0&&again.thread_id==blocked.thread_id&&again.cpu.pc==blocked.cpu.pc);
    again=satchel_switch_process_run_scheduled(p,100,cancel,NULL);
    CHECK(again.state==SATCHEL_SWITCH_RUN_CPU_STOP&&again.cpu.reason==SATCHEL_CPU_CANCELLED&&again.cpu.steps==0&&
        again.thread_id==blocked.thread_id&&again.cpu.pc==blocked.cpu.pc);
    again=satchel_switch_process_run_scheduled(p,0,NULL,NULL);
    CHECK(again.state==SATCHEL_SWITCH_RUN_CPU_STOP&&again.cpu.reason==SATCHEL_CPU_BUDGET&&again.cpu.steps==0&&again.thread_id==blocked.thread_id);
    uint64_t tick=UINT64_MAX;CHECK(satchel_switch_process_get_system_tick(p,&tick)&&tick!=UINT64_MAX);
    CHECK(!satchel_switch_process_get_system_tick(NULL,&tick));
    CHECK(satchel_switch_process_report_gpu_error(p));
    SatchelSwitchRunResult resumed=satchel_switch_process_run_scheduled(p,100,NULL,NULL);
    CHECK(resumed.state==SATCHEL_SWITCH_RUN_CPU_STOP&&resumed.cpu.reason==SATCHEL_CPU_SVC&&resumed.cpu.svc==0x7f);
    SatchelCPUProbe *child_cpu=satchel_switch_process_cpu(p);CHECK(child_cpu);
    uint32_t status=99;CHECK(satchel_cpu_probe_read(child_cpu,argument+4,&status,4)&&status==0);
    CHECK(find_thread(p,child).cpu_id==2&&find_thread(p,child).handle_references==0);
    CHECK(satchel_cpu_probe_set_pc(child_cpu,0x800021c));
    SatchelSwitchRunResult exited=satchel_switch_process_run_scheduled(p,100,NULL,NULL);
    CHECK(exited.state==SATCHEL_SWITCH_RUN_EXITED&&exited.cpu.steps==1);
    // Final retained exited context keeps real process RAM alive for teardown;
    // it is not runnable and no additional hidden context was allocated.
    CHECK(satchel_switch_process_thread_count(p)==1&&find_thread(p,child).state==SATCHEL_SWITCH_THREAD_EXITED);
    CHECK(satchel_switch_process_get_system_tick(p,&tick));
    satchel_switch_process_destroy(p);
}
#if defined(SATCHEL_CPU_TESTING)
extern int satchel_cpu_probe_test_set_clock(SatchelCPUProbe *,uint64_t,int);
static void test_deadline_before_gpu_signal(void) {
    SatchelSwitchProcess *p=thread_fixture();SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);
    CHECK(satchel_cpu_probe_test_set_clock(cpu,0,0));
    uint32_t message,suspended,gpu;make_events(p,&message,&suspended,&gpu);
    CHECK(satchel_cpu_probe_write(cpu,0x8004000,&gpu,4));
    SatchelSwitchRunResult waiting=thread_wait(p,0x8004000,1,1000,100);
    CHECK(waiting.state==SATCHEL_SWITCH_RUN_BLOCKED&&waiting.next_deadline_tick==21);
    CHECK(satchel_cpu_probe_test_set_clock(cpu,1250,0)); // Tick24: timeout preceded the fault.
    CHECK(satchel_switch_process_report_gpu_error(p));
    uint64_t value;CHECK(satchel_cpu_probe_get_x(cpu,0,&value)&&value==0xea01);
    CHECK(satchel_cpu_probe_get_x(cpu,1,&value)&&value==UINT32_MAX);
    CHECK(find_thread(p,0x80).state==SATCHEL_SWITCH_THREAD_RUNNABLE&&satchel_switch_process_gpu_error_event_signaled(p)==1);
    uint64_t tick=777;CHECK(satchel_switch_process_get_system_tick(p,&tick)&&tick==24);
    CHECK(satchel_cpu_probe_test_set_clock(cpu,1250,1));tick=777;
    CHECK(!satchel_switch_process_get_system_tick(p,&tick)&&tick==777);
    CHECK(satchel_cpu_probe_test_set_clock(cpu,1250,0));
    CHECK(!satchel_switch_process_get_system_tick(NULL,&tick)&&tick==777);
    satchel_switch_process_destroy(p);
}
#endif
static void test_real_wait_cancel(void) {
    SatchelSwitchProcess *p=thread_fixture();SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);
    uint32_t message,suspended,gpu;make_events(p,&message,&suspended,&gpu);
    CHECK(satchel_cpu_probe_write(cpu,0x8004000,&gpu,4));
    uint32_t child=thread_create(p,0x8000280,0x80,initial_stack39+0x2000,50,0,0);
    thread_svc(p,0x800001c,child,0);
    uint64_t point=0x8000024;CHECK(satchel_cpu_probe_set_breakpoints(cpu,&point,1));
    SatchelSwitchRunResult result=thread_wait(p,0x8004000,1,-1,100);
    CHECK(result.state==SATCHEL_SWITCH_RUN_CPU_STOP&&result.cpu.reason==SATCHEL_CPU_BREAKPOINT&&result.cpu.steps==3);
    uint64_t value;CHECK(satchel_cpu_probe_get_x(cpu,0,&value)&&value==0xec01);
    CHECK(satchel_cpu_probe_get_x(cpu,1,&value)&&value==UINT32_MAX);
    CHECK(find_thread(p,child).state==SATCHEL_SWITCH_THREAD_EXITED&&find_thread(p,0x80).state==SATCHEL_SWITCH_THREAD_RUNNABLE);
    CHECK(satchel_switch_process_gpu_error_event_signaled(p)==0);
    CHECK(satchel_cpu_probe_set_breakpoints(cpu,NULL,0));handle_close(p,child,0);
    CHECK(thread_wait(p,0x8004000,1,0,1).cpu.reason==SATCHEL_CPU_BUDGET);
    CHECK(satchel_cpu_probe_get_x(cpu,0,&value)&&value==0xea01);
    result=thread_wait(p,0x8004000,1,-1,100);
    CHECK(result.state==SATCHEL_SWITCH_RUN_BLOCKED&&result.next_deadline_tick==UINT64_MAX);
    satchel_switch_process_destroy(p);
}
#if defined(SATCHEL_CPU_TESTING)
static void test_sleep_deadlines(void) {
    SatchelSwitchProcess *p=thread_fixture();SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);
    CHECK(satchel_cpu_probe_test_set_clock(cpu,0,0));
    CHECK(satchel_cpu_probe_set_pc(cpu,0x8000030)&&satchel_cpu_probe_set_x(cpu,0,1000));
    SatchelSwitchRunResult result=satchel_switch_process_run_scheduled(p,100,NULL,NULL);
    CHECK(result.state==SATCHEL_SWITCH_RUN_BLOCKED&&result.cpu.steps==1&&result.next_deadline_tick==21);
    CHECK(find_thread(p,0x80).state==SATCHEL_SWITCH_THREAD_SLEEPING);
    CHECK(satchel_cpu_probe_test_set_clock(cpu,1250,0));
    result=satchel_switch_process_run_scheduled(p,100,NULL,NULL);
    CHECK(result.state==SATCHEL_SWITCH_RUN_CPU_STOP&&result.cpu.reason==SATCHEL_CPU_SVC&&result.cpu.svc==0x7f&&result.cpu.steps==2);
    uint64_t value;CHECK(satchel_cpu_probe_get_x(cpu,0,&value)&&value==24);
    CHECK(find_thread(p,0x80).state==SATCHEL_SWITCH_THREAD_RUNNABLE);
    CHECK(satchel_cpu_probe_set_pc(cpu,0x8000030)&&satchel_cpu_probe_set_x(cpu,0,1));
    result=satchel_switch_process_run_scheduled(p,100,NULL,NULL);
    CHECK(result.state==SATCHEL_SWITCH_RUN_BLOCKED&&result.next_deadline_tick==INT64_MAX);
    satchel_switch_process_destroy(p);
    p=thread_fixture();cpu=satchel_switch_process_cpu(p);
    CHECK(satchel_cpu_probe_set_pc(cpu,0x8000030)&&satchel_cpu_probe_set_x(cpu,0,(uint64_t)-3));
    CHECK(satchel_switch_process_run_scheduled(p,1,NULL,NULL).cpu.reason==SATCHEL_CPU_BUDGET);
    CHECK(satchel_cpu_probe_get_x(cpu,0,&value)&&value==(uint64_t)-3);
    CHECK(satchel_cpu_probe_set_pc(cpu,0x8000030)&&satchel_cpu_probe_set_x(cpu,0,(uint64_t)-1));
    result=satchel_switch_process_run_scheduled(p,100,NULL,NULL);
    CHECK(result.state==SATCHEL_SWITCH_RUN_CPU_STOP&&result.cpu.reason==SATCHEL_CPU_SVC&&result.cpu.svc==0xb&&result.cpu.steps==1);
    CHECK(find_thread(p,0x80).state==SATCHEL_SWITCH_THREAD_RUNNABLE);
    satchel_switch_process_destroy(p);
}
#endif
static void set_affinity(SatchelSwitchProcess *p,uint32_t handle,int32_t core,uint64_t mask,uint32_t expected) {
    SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);CHECK(cpu);
    CHECK(satchel_cpu_probe_set_pc(cpu,0x8000360)&&satchel_cpu_probe_set_x(cpu,0,handle)&&
        satchel_cpu_probe_set_x(cpu,1,(uint32_t)core)&&satchel_cpu_probe_set_x(cpu,2,mask));
    SatchelSwitchRunResult result=satchel_switch_process_run_scheduled(p,1,NULL,NULL);
    CHECK(result.state==SATCHEL_SWITCH_RUN_CPU_STOP&&result.cpu.reason==SATCHEL_CPU_BUDGET&&result.cpu.steps==1);
    uint64_t value;CHECK(satchel_cpu_probe_get_x(cpu,0,&value)&&value==expected);
    CHECK(satchel_cpu_probe_get_x(cpu,1,&value)&&value==(uint32_t)core);
    CHECK(satchel_cpu_probe_get_x(cpu,2,&value)&&value==mask);
}
static void check_affinity_query(SatchelSwitchProcess *p,uint32_t handle,int32_t core,uint64_t mask) {
    SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);CHECK(cpu);
    CHECK(satchel_cpu_probe_set_pc(cpu,0x800000c)&&satchel_cpu_probe_set_x(cpu,2,handle));
    SatchelSwitchRunResult result=satchel_switch_process_run_scheduled(p,1,NULL,NULL);
    CHECK(result.cpu.reason==SATCHEL_CPU_BUDGET&&result.cpu.steps==1);
    uint64_t value;CHECK(satchel_cpu_probe_get_x(cpu,0,&value)&&!value);
    CHECK(satchel_cpu_probe_get_x(cpu,1,&value)&&value==(uint32_t)core);
    CHECK(satchel_cpu_probe_get_x(cpu,2,&value)&&value==mask);
}
static void test_affinity(void) {
    SatchelSwitchProcess *p=thread_fixture();
    uint32_t child=thread_create(p,0x8000300,0,initial_stack39+0x3000,28,2,0);
    set_affinity(p,0xdeadbeef,-99,UINT64_MAX,0xe401); // handle precedes malformed mask/core
    set_affinity(p,0xffff8001,0,1,0xe401); // current process is not a thread
    for(uint64_t mask=0;mask<16;mask++) for(int core=-4;core<6;core++) {
        set_affinity(p,child,2,4,0);
        uint32_t status=0;
        int ideal=core;uint64_t wanted=mask;
        if(core==-2){ideal=0;wanted=1;}
        else if(mask&~UINT64_C(7))status=0xe201;
        else if(!mask)status=0xe801;
        else if(core>=0&&core<64){if(!(mask&(UINT64_C(1)<<core)))status=0xe801;}
        else if(core!=-1&&core!=-3)status=0xe201;
        if(!status&&core==-3){ideal=2;if(!(mask&4))status=0xe801;}
        set_affinity(p,child,core,mask,status);
        SatchelSwitchThreadInfo info=find_thread(p,child);
        CHECK(info.state==SATCHEL_SWITCH_THREAD_CREATED&&!info.instructions);
        CHECK(info.ideal_core_id==(status?2:ideal)&&info.affinity_mask==(status?4:wanted));
        CHECK(info.affinity_mask&(UINT64_C(1)<<info.cpu_id));
        check_affinity_query(p,child,info.ideal_core_id,info.affinity_mask);
    }
    set_affinity(p,child,-1,3,0); // formerly assigned core2 is excluded: highest eligible core1
    CHECK(find_thread(p,child).cpu_id==1&&find_thread(p,child).ideal_core_id==-1);
    set_affinity(p,child,-3,7,0xe801); // no concrete previous ideal can be preserved in admitted mask
    set_affinity(p,child,-2,UINT64_MAX,0); // process-default replaces even a malformed supplied mask
    CHECK(find_thread(p,child).cpu_id==0&&find_thread(p,child).ideal_core_id==0&&find_thread(p,child).affinity_mask==1);
    set_affinity(p,child,2,7,0); // new ideal does not force migration while assigned0 remains permitted
    CHECK(find_thread(p,child).cpu_id==0&&find_thread(p,child).ideal_core_id==2);
    check_affinity_query(p,child,2,7);
    // Active-thread pseudo mutation migrates the main's next quantum onto core2.
    set_affinity(p,0xffff8000,2,4,0);
    CHECK(find_thread(p,0x80).cpu_id==2&&find_thread(p,0x80).ideal_core_id==2);
    check_affinity_query(p,0xffff8000,2,4);
    set_affinity(p,0xffff8000,-2,0,0);
    CHECK(find_thread(p,0x80).cpu_id==0&&find_thread(p,0x80).affinity_mask==1);
    // Created child really runs on its retained allowed assignment, then exits.
    thread_svc(p,0x800001c,child,0);
    SatchelCPUProbe *main_cpu=satchel_switch_process_cpu(p);CHECK(satchel_cpu_probe_set_pc(main_cpu,0x8000038));
    SatchelSwitchRunResult result=satchel_switch_process_run_scheduled(p,8,NULL,NULL);
    CHECK(result.state==SATCHEL_SWITCH_RUN_CPU_STOP&&result.cpu.svc==0x7f);
    SatchelSwitchThreadInfo info=find_thread(p,child);
    CHECK(info.state==SATCHEL_SWITCH_THREAD_EXITED&&info.instructions==1&&info.cpu_id==0&&info.affinity_mask==7);
    handle_close(p,child,0);set_affinity(p,child,2,4,0xe401);
    satchel_switch_process_destroy(p);
}
typedef struct { uint32_t owner,tag;uint64_t address;uint32_t result,marker; } MutexArguments;
static void mutex_set_word(SatchelCPUProbe *cpu,uint64_t address,uint32_t word) { CHECK(satchel_cpu_probe_write(cpu,address,&word,4)); }
static uint32_t mutex_word(SatchelCPUProbe *cpu,uint64_t address) { uint32_t word;CHECK(satchel_cpu_probe_read(cpu,address,&word,4));return word; }
static SatchelSwitchRunResult mutex_svc_on(SatchelSwitchProcess *p,SatchelCPUProbe *cpu,uint64_t id,uint64_t pc,uint64_t x0,uint64_t x1,uint64_t x2) {
    CHECK(satchel_cpu_probe_set_pc(cpu,pc)&&satchel_cpu_probe_set_x(cpu,0,x0)&&satchel_cpu_probe_set_x(cpu,1,x1)&&satchel_cpu_probe_set_x(cpu,2,x2));
    for(unsigned n=0;n<1000;n++) {
        SatchelSwitchRunResult result=satchel_switch_process_run_scheduled(p,1,NULL,NULL);
        CHECK(result.state==SATCHEL_SWITCH_RUN_CPU_STOP&&result.cpu.reason==SATCHEL_CPU_BUDGET);
        if(result.thread_id==id)return result;
    }
    CHECK(0);return (SatchelSwitchRunResult){0};
}
static void mutex_status(SatchelCPUProbe *cpu,uint32_t status) { uint64_t value;CHECK(satchel_cpu_probe_get_x(cpu,0,&value)&&value==status); }
static uint32_t mutex_child_create(SatchelSwitchProcess *p,SatchelCPUProbe *main_cpu,uint64_t args,uint64_t address,uint32_t owner,uint32_t tag,int priority,int core) {
    uint32_t child=thread_create(p,0x8000400,args,initial_stack39+0x3000,priority,core,0);
    MutexArguments arguments={owner,tag?tag:child,address,0xaaaaaaaa,0xbbbbbbbb};
    CHECK(satchel_cpu_probe_write(main_cpu,args,&arguments,sizeof(arguments)));return child;
}
static void mutex_start(SatchelSwitchProcess *p,SatchelCPUProbe *main_cpu,uint32_t child) {
    mutex_svc_on(p,main_cpu,find_thread(p,0x80).id,0x800001c,child,0,0);mutex_status(main_cpu,0);
    CHECK(satchel_cpu_probe_set_pc(main_cpu,0x80003c0));
}
static void mutex_queue(SatchelSwitchProcess *p,uint32_t child) {
    for(unsigned n=0;n<20 && find_thread(p,child).state!=SATCHEL_SWITCH_THREAD_MUTEX_WAIT;n++) {
        SatchelSwitchRunResult result=satchel_switch_process_run_scheduled(p,64,NULL,NULL);
        CHECK(result.state==SATCHEL_SWITCH_RUN_CPU_STOP&&result.cpu.reason==SATCHEL_CPU_BUDGET);
    }
    CHECK(find_thread(p,child).state==SATCHEL_SWITCH_THREAD_MUTEX_WAIT);
}
static SatchelCPUProbe *mutex_resume_marker(SatchelSwitchProcess *p,uint32_t child,SatchelCPUProbe *reader,uint64_t args,uint32_t status) {
    for(unsigned n=0;n<20;n++) {
        SatchelSwitchRunResult result=satchel_switch_process_run_scheduled(p,64,NULL,NULL);
        CHECK(result.state==SATCHEL_SWITCH_RUN_CPU_STOP);
        if(result.cpu.reason==SATCHEL_CPU_SVC) {
            CHECK(result.cpu.svc==0x7f&&result.thread_id==find_thread(p,child).id);
            MutexArguments a;CHECK(satchel_cpu_probe_read(reader,args,&a,sizeof(a))&&a.result==status&&a.marker==0x5a5a);
            return satchel_switch_process_cpu(p);
        }
        CHECK(result.cpu.reason==SATCHEL_CPU_BUDGET);
    }
    CHECK(0);return NULL;
}
static void mutex_finish_child(SatchelSwitchProcess *p,uint32_t child,SatchelCPUProbe *cpu) {
    // Resume the original child after its explicit inspection boundary: it
    // loads its real mutex address, invokes Unlock, and exits.
    CHECK(satchel_cpu_probe_set_pc(cpu,0x8000424));
    for(unsigned n=0;n<20&&find_thread(p,child).state!=SATCHEL_SWITCH_THREAD_EXITED;n++) {
        SatchelSwitchRunResult result=satchel_switch_process_run_scheduled(p,4,NULL,NULL);
        CHECK(result.state==SATCHEL_SWITCH_RUN_CPU_STOP&&result.cpu.reason==SATCHEL_CPU_BUDGET);
    }
    CHECK(find_thread(p,child).state==SATCHEL_SWITCH_THREAD_EXITED);
}
static void test_wait_classification(void) {
    SatchelSwitchProcess *p=thread_fixture();SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);
    uint32_t message,suspended,gpu;make_events(p,&message,&suspended,&gpu);
    SatchelSwitchThreadWaitInfo info={0},zero={0};
    CHECK(satchel_switch_process_thread_wait_info(p,0,&info)&&!memcmp(&info,&zero,sizeof(info)));
    uint32_t handles[]={message,gpu,suspended,0x80,message};
    CHECK(satchel_cpu_probe_write(cpu,0x8004000,handles,sizeof(handles)));
    SatchelSwitchRunResult result=thread_wait(p,0x8004000,5,-1,20);
    CHECK(result.state==SATCHEL_SWITCH_RUN_BLOCKED);
    CHECK(satchel_switch_process_thread_wait_info(p,0,&info)&&info.applet_message_events==2&&info.gpu_error_events==1&&info.suspension_events==1&&info.threads==1&&!info.other_events);
    SatchelSwitchThreadWaitInfo previous=info;CHECK(!satchel_switch_process_thread_wait_info(p,32,&info)&&!memcmp(&info,&previous,sizeof(info)));
    CHECK(!satchel_switch_process_thread_wait_info(p,1,&info)&&!memcmp(&info,&previous,sizeof(info)));
    CHECK(!satchel_switch_process_thread_wait_info(NULL,0,&info)&&!memcmp(&info,&previous,sizeof(info)));
    CHECK(!satchel_switch_process_thread_wait_info(p,0,NULL));
    // Classify the retained object, even after its original guest handle closes
    // (the existing waiter fixtures independently validate this lifetime).
    CHECK(satchel_switch_process_report_gpu_error(p));
    CHECK(satchel_switch_process_thread_wait_info(p,0,&info)&&!memcmp(&info,&zero,sizeof(info)));
    satchel_switch_process_destroy(p);
}
static void test_mutex_validation(void) {
    SatchelSwitchProcess *p=thread_fixture();SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);uint64_t id=find_thread(p,0x80).id;
    const uint64_t address=0x8007000;
    const uint64_t bad_addresses[]={address+1,0,0x8001000,UINT64_C(0xffffff8000000001),UINT64_MAX};
    const uint32_t errors[]={0xcc01,0xd401,0xd401,0xd401,0xcc01};
    for(unsigned i=0;i<5;i++) {
        mutex_svc_on(p,cpu,id,0x8000384,bad_addresses[i],0,0);mutex_status(cpu,errors[i]);
    }
    // Lock reads first: a changed word succeeds even with an invalid owner.
    mutex_set_word(cpu,address,0);mutex_svc_on(p,cpu,id,0x8000380,0xdeadbeef,address,0x87);mutex_status(cpu,0);
    CHECK(mutex_word(cpu,address)==0);
    mutex_set_word(cpu,address,0x40000000|0xdead);
    mutex_svc_on(p,cpu,id,0x8000380,0xdead,address,0x87);mutex_status(cpu,0xe401);
    mutex_set_word(cpu,address,0xffff8000);
    mutex_svc_on(p,cpu,id,0x8000380,0xffff8000,address,0x87);mutex_status(cpu,0xe401);
    mutex_svc_on(p,cpu,id,0x8000380,0x80,address+1,0x87);mutex_status(cpu,0xcc01);
    mutex_svc_on(p,cpu,id,0x8000380,0x80,0,0x87);mutex_status(cpu,0xd401);
    mutex_svc_on(p,cpu,id,0x8000384,address,0,0);mutex_status(cpu,0);CHECK(!mutex_word(cpu,address));
    SatchelSwitchMutexInfo stats;CHECK(satchel_switch_process_mutex_info(p,&stats)&&stats.lock_requests==5&&stats.unlock_requests==6&&!stats.waits&&!stats.current_waiters&&!stats.handoffs);
    memset(&stats,0xa5,sizeof(stats));SatchelSwitchMutexInfo before=stats;CHECK(!satchel_switch_process_mutex_info(NULL,&stats)&&!memcmp(&stats,&before,sizeof(stats)));CHECK(!satchel_switch_process_mutex_info(p,NULL));
    satchel_switch_process_destroy(p);
}
static void test_mutex_handoff_and_cancel(void) {
    SatchelSwitchProcess *p=thread_fixture();SatchelCPUProbe *main_cpu=satchel_switch_process_cpu(p);uint64_t main_id=find_thread(p,0x80).id;
    uint64_t address=0x8007000,args=0x8006000;uint32_t child=mutex_child_create(p,main_cpu,args,address,0x80,0x2468,28,2);
    mutex_set_word(main_cpu,address,0x40000080);mutex_start(p,main_cpu,child);mutex_queue(p,child);
    CHECK(find_thread(p,0x80).priority==44&&find_thread(p,0x80).effective_priority==28&&find_thread(p,0x80).wait_references==1);
    mutex_svc_on(p,main_cpu,main_id,0x8000008,0,0x80,0);mutex_status(main_cpu,0);uint64_t current;CHECK(satchel_cpu_probe_get_x(main_cpu,1,&current)&&current==28);
    mutex_svc_on(p,main_cpu,main_id,0x8000024,child,0,0);mutex_status(main_cpu,0);CHECK(find_thread(p,child).state==SATCHEL_SWITCH_THREAD_MUTEX_WAIT);
    CHECK(mutex_word(main_cpu,address)==0x40000080); // CancelSynchronization cannot manufacture an unlock.
    mutex_svc_on(p,main_cpu,main_id,0x8000384,address,0,0);mutex_status(main_cpu,0);
    CHECK(mutex_word(main_cpu,address)==0x2468&&find_thread(p,child).state==SATCHEL_SWITCH_THREAD_RUNNABLE);
    CHECK(find_thread(p,0x80).effective_priority==44&&!find_thread(p,0x80).wait_references);
    CHECK(satchel_cpu_probe_set_pc(main_cpu,0x80003c0));SatchelCPUProbe *child_cpu=mutex_resume_marker(p,child,main_cpu,args,0);
    // The earlier noncancellable mutex wait leaves cancellation pending for
    // the child's next genuinely cancellable synchronization wait.
    CHECK(satchel_cpu_probe_set_x(child_cpu,3,UINT64_MAX));
    mutex_svc_on(p,child_cpu,find_thread(p,child).id,0x8000020,0,0,0);mutex_status(child_cpu,0xec01);
    mutex_finish_child(p,child,child_cpu);CHECK(!mutex_word(main_cpu,address));
    SatchelSwitchMutexInfo stats;CHECK(satchel_switch_process_mutex_info(p,&stats)&&stats.waits==1&&stats.handoffs==1&&!stats.current_waiters);
    satchel_switch_process_destroy(p);
}
static void test_mutex_priority_and_references(void) {
    SatchelSwitchProcess *p=thread_fixture();SatchelCPUProbe *main_cpu=satchel_switch_process_cpu(p);uint64_t main_id=find_thread(p,0x80).id;
    uint64_t address=0x8007000;uint32_t low=mutex_child_create(p,main_cpu,0x8006000,address,0x80,0,30,2);
    uint32_t high=mutex_child_create(p,main_cpu,0x8006100,address,0x80,0,20,2);
    mutex_set_word(main_cpu,address,0x40000080);mutex_start(p,main_cpu,low);mutex_queue(p,low);
    CHECK(find_thread(p,0x80).effective_priority==30);
    mutex_start(p,main_cpu,high);mutex_queue(p,high);CHECK(find_thread(p,0x80).effective_priority==20&&find_thread(p,0x80).wait_references==2);
    mutex_svc_on(p,main_cpu,main_id,0x8000384,address,0,0);mutex_status(main_cpu,0);
    CHECK(mutex_word(main_cpu,address)==(high|0x40000000)&&find_thread(p,low).state==SATCHEL_SWITCH_THREAD_MUTEX_WAIT);
    CHECK(find_thread(p,0x80).effective_priority==44&&find_thread(p,0x80).wait_references==1);
    CHECK(satchel_cpu_probe_set_pc(main_cpu,0x80003c0));SatchelCPUProbe *high_cpu=mutex_resume_marker(p,high,main_cpu,0x8006100,0);
    // Closing the new owner's handle cannot destroy its running context. Once
    // it actually exits, finalization cancels its inherited remaining queue.
    mutex_svc_on(p,high_cpu,find_thread(p,high).id,0x8000004,high,0,0);mutex_status(high_cpu,0);
    CHECK(satchel_cpu_probe_set_pc(high_cpu,0x8000300));
    SatchelCPUProbe *low_cpu=mutex_resume_marker(p,low,main_cpu,0x8006000,0xfa01);CHECK(low_cpu);
    CHECK(!find_thread(p,0x80).wait_references&&find_thread(p,0x80).effective_priority==44);
    CHECK(mutex_word(main_cpu,address)==(high|0x40000000)); // Finalization does not invent a user-word rewrite.
    SatchelSwitchMutexInfo stats;CHECK(satchel_switch_process_mutex_info(p,&stats)&&stats.waits==2&&stats.handoffs==1&&!stats.current_waiters);
    satchel_switch_process_destroy(p);
}
static void test_mutex_transitive_and_equal_order(void) {
    SatchelSwitchProcess *p=thread_fixture();SatchelCPUProbe *main_cpu=satchel_switch_process_cpu(p);uint64_t main_id=find_thread(p,0x80).id;
    uint64_t a=0x8007000,b=0x8007004;
    uint32_t middle=mutex_child_create(p,main_cpu,0x8006000,a,0x80,0,30,2);
    uint32_t equal=mutex_child_create(p,main_cpu,0x8006100,a,0x80,0,20,2);
    uint32_t donor=mutex_child_create(p,main_cpu,0x8006200,b,middle,0,20,1);
    mutex_set_word(main_cpu,a,0x40000080);mutex_set_word(main_cpu,b,middle|0x40000000);
    mutex_start(p,main_cpu,middle);mutex_queue(p,middle);mutex_start(p,main_cpu,equal);mutex_queue(p,equal);
    mutex_start(p,main_cpu,donor);mutex_queue(p,donor);
    CHECK(find_thread(p,middle).priority==30&&find_thread(p,middle).effective_priority==20);
    CHECK(find_thread(p,0x80).effective_priority==20);
    // The middle waiter changes priority after equal was already queued there.
    // Reinsert it behind equal; original earlier arrival alone must not win.
    mutex_svc_on(p,main_cpu,main_id,0x8000384,a,0,0);mutex_status(main_cpu,0);CHECK(mutex_word(main_cpu,a)==(equal|0x40000000));
    CHECK(satchel_cpu_probe_set_pc(main_cpu,0x80003c0));SatchelCPUProbe *equal_cpu=mutex_resume_marker(p,equal,main_cpu,0x8006100,0);
    mutex_svc_on(p,equal_cpu,find_thread(p,equal).id,0x8000384,a,0,0);mutex_status(equal_cpu,0);
    CHECK(mutex_word(main_cpu,a)==middle&&find_thread(p,middle).effective_priority==20);
    CHECK(satchel_cpu_probe_set_pc(equal_cpu,0x8000300));SatchelCPUProbe *middle_cpu=mutex_resume_marker(p,middle,main_cpu,0x8006000,0);
    CHECK(find_thread(p,0x80).effective_priority==44&&!find_thread(p,0x80).wait_references);
    mutex_svc_on(p,middle_cpu,find_thread(p,middle).id,0x8000384,b,0,0);mutex_status(middle_cpu,0);
    CHECK(mutex_word(main_cpu,b)==donor&&find_thread(p,middle).effective_priority==30&&!find_thread(p,middle).wait_references);
    CHECK(satchel_cpu_probe_set_pc(middle_cpu,0x8000300));mutex_resume_marker(p,donor,main_cpu,0x8006200,0);
    SatchelSwitchMutexInfo stats;CHECK(satchel_switch_process_mutex_info(p,&stats)&&stats.waits==3&&stats.handoffs==3&&!stats.current_waiters);
    satchel_switch_process_destroy(p);
}

static void test_mutex_failed_write_handoff(void) {
    SatchelSwitchProcess *p=thread_fixture();SatchelCPUProbe *main_cpu=satchel_switch_process_cpu(p);uint64_t main_id=find_thread(p,0x80).id;
    uint64_t address=0x8007000,destination=initial_stack39+0x20000;
    uint32_t first=mutex_child_create(p,main_cpu,0x8006000,address,0x80,0,28,2);
    uint32_t second=mutex_child_create(p,main_cpu,0x8006100,address,0x80,0,28,2);
    mutex_set_word(main_cpu,address,0x40000080);mutex_start(p,main_cpu,first);mutex_queue(p,first);
    mutex_start(p,main_cpu,second);mutex_queue(p,second);
    CHECK(find_thread(p,0x80).effective_priority==28&&find_thread(p,0x80).wait_references==2);
    // A genuine MapMemory removes user access to the source word while queued.
    mutex_svc_on(p,main_cpu,main_id,0x8000388,destination,address,4096);mutex_status(main_cpu,0);
    SatchelSwitchMemoryInfo info;CHECK(satchel_switch_process_query(p,address,&info)&&!info.permissions&&info.attributes==1);
    mutex_svc_on(p,main_cpu,main_id,0x8000384,address,0,0);mutex_status(main_cpu,0xd401);
    CHECK(mutex_word(main_cpu,destination)==0x40000080); // No write through inaccessible source.
    CHECK(find_thread(p,first).state==SATCHEL_SWITCH_THREAD_RUNNABLE&&find_thread(p,second).state==SATCHEL_SWITCH_THREAD_MUTEX_WAIT);
    CHECK(find_thread(p,0x80).effective_priority==44&&find_thread(p,0x80).wait_references==1);
    // Restore the original backing using the matching genuine UnmapMemory.
    mutex_svc_on(p,main_cpu,main_id,0x800038c,destination,address,4096);mutex_status(main_cpu,0);
    CHECK(satchel_cpu_probe_set_pc(main_cpu,0x80003c0));SatchelCPUProbe *first_cpu=mutex_resume_marker(p,first,main_cpu,0x8006000,0xd401);
    mutex_svc_on(p,first_cpu,find_thread(p,first).id,0x8000384,address,0,0);mutex_status(first_cpu,0);
    CHECK(mutex_word(main_cpu,address)==second); // Kernel queue transferred despite previous failed write.
    CHECK(satchel_cpu_probe_set_pc(first_cpu,0x8000300));mutex_resume_marker(p,second,main_cpu,0x8006100,0);
    SatchelSwitchMutexInfo stats;CHECK(satchel_switch_process_mutex_info(p,&stats)&&stats.waits==2&&stats.handoffs==2&&!stats.current_waiters);
    satchel_switch_process_destroy(p);
}
static int mutex_cancel_callback(void *context) { (void)context;return 1; }
static void test_mutex_deadlock_cancel_and_cleanup(void) {
    SatchelSwitchProcess *p=thread_fixture();SatchelCPUProbe *main_cpu=satchel_switch_process_cpu(p);uint64_t main_id=find_thread(p,0x80).id;
    uint64_t a=0x8007000,b=0x8007004;uint32_t child=mutex_child_create(p,main_cpu,0x8006000,a,0x80,0,28,2);
    mutex_set_word(main_cpu,a,0x40000080);mutex_set_word(main_cpu,b,child|0x40000000);
    mutex_start(p,main_cpu,child);mutex_queue(p,child);
    mutex_svc_on(p,main_cpu,main_id,0x8000380,child,b,0x80);
    CHECK(find_thread(p,0x80).state==SATCHEL_SWITCH_THREAD_MUTEX_WAIT&&find_thread(p,child).state==SATCHEL_SWITCH_THREAD_MUTEX_WAIT);
    CHECK(find_thread(p,0x80).effective_priority==28&&find_thread(p,child).effective_priority==28);
    SatchelSwitchRunResult result=satchel_switch_process_run_scheduled(p,100,NULL,NULL);
    CHECK(result.state==SATCHEL_SWITCH_RUN_BLOCKED&&result.cpu.steps==0&&result.next_deadline_tick==UINT64_MAX);
    SatchelSwitchMutexInfo stats;CHECK(satchel_switch_process_mutex_info(p,&stats)&&stats.waits==2&&stats.current_waiters==2&&!stats.handoffs);
    result=satchel_switch_process_run_scheduled(p,100,mutex_cancel_callback,NULL);
    CHECK(result.state==SATCHEL_SWITCH_RUN_CPU_STOP&&result.cpu.reason==SATCHEL_CPU_CANCELLED&&!result.cpu.steps);
    CHECK(find_thread(p,0x80).state==SATCHEL_SWITCH_THREAD_MUTEX_WAIT&&find_thread(p,child).state==SATCHEL_SWITCH_THREAD_MUTEX_WAIT);
    CHECK(mutex_word(main_cpu,a)==0x40000080&&mutex_word(main_cpu,b)==(child|0x40000000));
    satchel_switch_process_destroy(p); // Actual cleanup of all contexts and retained wait edges; no invented wake.
}

static uint32_t launch_event_reply(SatchelSwitchProcess *p, uint32_t handle, uint8_t request[256], int domain, int expected_signal) {
    SatchelCPUProbe *cpu = satchel_switch_process_cpu(p);
    CHECK(satchel_cpu_probe_write(cpu, 0x31000000, request, 256));
    CHECK(satchel_cpu_probe_set_pc(cpu, 0x8000014) && satchel_cpu_probe_set_x(cpu, 0, handle));
    SatchelCPUProbeResult result = satchel_switch_process_run(p, 1, NULL, NULL);
    CHECK(result.reason == SATCHEL_CPU_BUDGET && result.steps == 1);
    uint64_t value; CHECK(satchel_cpu_probe_get_x(cpu, 0, &value) && value == 0);
    uint8_t actual[256], expected[48] = {0};
    CHECK(satchel_cpu_probe_read(cpu, 0x31000000, actual, 256));
    uint32_t copied = 0; memcpy(&copied, actual + 12, 4);
    CHECK(copied && satchel_switch_process_is_readable_event(p, copied));
    size_t end = domain ? 48 : 32;
    metadata_word(expected + 4, 0x80000000u | (uint32_t)((end - 16) / 4));
    metadata_word(expected + 8, 2); metadata_word(expected + 12, copied);
    metadata_word(expected + (domain ? 32 : 16), 0x4f434653);
    CHECK(!memcmp(actual, expected, end));
    CHECK(!memcmp(actual + end, request + end, 256 - end));
    CHECK(satchel_switch_process_applet_event_signaled(p) == expected_signal);
    return copied;
}
static void make_launch_events(SatchelSwitchProcess *p,uint32_t *message,uint32_t *suspended,uint32_t *gpu) {
    uint8_t bytes[256],name[8]={'a','p','p','l','e','t','O','E'};
    uint32_t sm=port_call(p,0x8001000,0);
    tipc_sm_request(bytes,0,1,NULL,0);sm_ipc(p,sm,bytes,1,0,0);
    tipc_sm_request(bytes,1,0,name,8);uint32_t root=lm_reply(p,sm,bytes,1,NULL,0);
    applet_proxy_request(bytes,0,0xffff8001,1,1,0);uint32_t proxy=lm_reply(p,root,bytes,1,NULL,0);
    uint32_t commands[3]={0,1,20},children[3];
    for(unsigned i=0;i<3;i++) {applet_profile_request(bytes,0,commands[i],NULL,0);children[i]=lm_reply(p,proxy,bytes,1,NULL,0);}
    applet_profile_request(bytes,0,0,NULL,0);*message=launch_event_reply(p,children[0],bytes,0,1);
    applet_profile_request(bytes,0,91,NULL,0);*suspended=launch_event_reply(p,children[1],bytes,0,1);
    applet_profile_request(bytes,0,130,NULL,0);*gpu=launch_event_reply(p,children[2],bytes,0,1);
    for(unsigned i=0;i<3;i++)handle_close(p,children[i],0);
    handle_close(p,proxy,0);handle_close(p,root,0);handle_close(p,sm,0);
}
static uint32_t launch_common(SatchelSwitchProcess *p,int domain,uint32_t *object) {
    uint8_t bytes[256],name[8]={'a','p','p','l','e','t','O','E'};
    uint32_t sm=port_call(p,0x8001000,0);tipc_sm_request(bytes,0,1,NULL,0);sm_ipc(p,sm,bytes,1,0,0);
    tipc_sm_request(bytes,1,0,name,8);uint32_t root=lm_reply(p,sm,bytes,1,NULL,0),proxy;
    if(domain) {
        sm_request(bytes,0,0,name,5,0,0);metadata_word(bytes+4,8);const uint8_t id[4]={1};lm_reply(p,root,bytes,0,id,4);
        applet_proxy_request(bytes,1,0xffff8001,1,1,0);domain_reply(p,root,bytes,2,0);proxy=2;
    } else {applet_proxy_request(bytes,0,0xffff8001,1,1,0);proxy=lm_reply(p,root,bytes,1,NULL,0);}
    applet_profile_request(bytes,domain?proxy:0,0,NULL,0);
    if(domain){domain_reply(p,root,bytes,3,0);*object=3;}
    else {*object=lm_reply(p,proxy,bytes,1,NULL,0);handle_close(p,proxy,0);handle_close(p,root,0);}
    handle_close(p,sm,0);return domain?root:*object;
}
static void launch_receive(SatchelSwitchProcess *p,uint32_t handle,uint32_t object,int domain,uint32_t message,int empty) {
    uint8_t bytes[256];applet_profile_request(bytes,domain?object:0,1,NULL,0);
    if(!empty){uint8_t value[4];metadata_word(value,message);applet_value_reply(p,handle,bytes,domain,value,4);return;}
    SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);CHECK(satchel_cpu_probe_write(cpu,0x31000000,bytes,256));
    CHECK(satchel_cpu_probe_set_pc(cpu,0x8000014)&&satchel_cpu_probe_set_x(cpu,0,handle));
    SatchelSwitchRunResult result=satchel_switch_process_run_scheduled(p,1,NULL,NULL);
    CHECK(result.state==SATCHEL_SWITCH_RUN_CPU_STOP&&result.cpu.reason==SATCHEL_CPU_BUDGET);
    uint64_t status;CHECK(satchel_cpu_probe_get_x(cpu,0,&status)&&!status);
    uint8_t actual[256],expected[48]={0};size_t size=domain?48:32;
    metadata_word(expected+4,(uint32_t)((size-8)/4));metadata_word(expected+(domain?32:16),0x4f434653);metadata_word(expected+(domain?40:24),0x680);
    CHECK(satchel_cpu_probe_read(cpu,0x31000000,actual,256)&&!memcmp(actual,expected,size)&&!memcmp(actual+size,bytes+size,256-size));
}
static void test_foreground_launch_queue(void) {
    CHECK(!satchel_switch_process_begin_foreground_launch(NULL));
    for(int domain=0;domain<2;domain++) {
        SatchelSwitchProcess *p=thread_fixture();SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);
        SatchelSwitchAppletMessageInfo info,zero={0};SatchelSwitchAppletState state;
        CHECK(satchel_switch_process_applet_message_info(p,&info)&&!memcmp(&info,&zero,sizeof(info)));
        CHECK(satchel_switch_process_applet_state(p,&state)&&state.focus_state==3&&!state.foreground_rights);
        CHECK(satchel_switch_process_begin_foreground_launch(p)&&satchel_switch_process_begin_foreground_launch(p));
        CHECK(satchel_switch_process_applet_state(p,&state)&&state.focus_state==1&&!state.foreground_rights);
        CHECK(satchel_switch_process_applet_message_info(p,&info)&&info.phase==SATCHEL_SWITCH_APPLET_FOREGROUND&&info.pending==2&&info.enqueued==2&&!info.received);
        CHECK(satchel_switch_process_applet_event_signaled(p)==-1&&satchel_switch_process_gpu_error_event_signaled(p)==-1&&!satchel_switch_process_gpu_error_detected(p));
        uint32_t object,handle=launch_common(p,domain,&object);uint8_t bytes[256];
        applet_profile_request(bytes,domain?object:0,9,NULL,0);const uint8_t focus=1;applet_value_reply(p,handle,bytes,domain,&focus,1);
        applet_profile_request(bytes,domain?object:0,0,NULL,0);uint32_t event=launch_event_reply(p,handle,bytes,domain,1);
        CHECK(satchel_switch_process_applet_event_signaled(p)==1);
        CHECK(satchel_cpu_probe_write(cpu,0x8004000,&event,4));SatchelSwitchRunResult result=thread_wait(p,0x8004000,1,-1,1);
        CHECK(result.state==SATCHEL_SWITCH_RUN_CPU_STOP&&result.cpu.reason==SATCHEL_CPU_BUDGET);uint64_t status;CHECK(satchel_cpu_probe_get_x(cpu,0,&status)&&!status);
        CHECK(satchel_switch_process_applet_event_signaled(p)==1); // Manual wait never consumes the queue/event.
        uint32_t invalid=9;applet_profile_request(bytes,domain?object:0,1,&invalid,4);sm_ipc(p,handle,bytes,0,0,0);
        CHECK(satchel_switch_process_applet_message_info(p,&info)&&info.pending==2&&!info.received&&satchel_switch_process_applet_event_signaled(p)==1);
        launch_receive(p,handle,object,domain,15,0);
        CHECK(satchel_switch_process_applet_message_info(p,&info)&&info.pending==1&&info.received==1&&satchel_switch_process_applet_event_signaled(p)==1);
        // A real guest reset can clear this manual event. A subsequent receive
        // still consumes the owned notification, not an arbitrary signal.
        thread_svc(p,0x800002c,event,0);CHECK(satchel_switch_process_applet_event_signaled(p)==0);
        launch_receive(p,handle,object,domain,1,0);
        CHECK(satchel_switch_process_applet_message_info(p,&info)&&!info.pending&&info.received==2&&info.enqueued==2&&satchel_switch_process_applet_event_signaled(p)==0);
        launch_receive(p,handle,object,domain,0,1);launch_receive(p,handle,object,domain,0,1);
        CHECK(satchel_switch_process_begin_foreground_launch(p)); // Idempotent even after all messages are consumed.
        CHECK(satchel_switch_process_applet_message_info(p,&info)&&!info.pending&&info.enqueued==2&&info.received==2);
        applet_profile_request(bytes,domain?object:0,0,NULL,0);uint32_t duplicate=launch_event_reply(p,handle,bytes,domain,0);
        CHECK(duplicate!=event&&satchel_switch_process_applet_event_signaled(p)==0);
        SatchelSwitchAppletMessageInfo saved=info;CHECK(!satchel_switch_process_applet_message_info(NULL,&info)&&!memcmp(&info,&saved,sizeof(info)));CHECK(!satchel_switch_process_applet_message_info(p,NULL));
        CHECK(!satchel_switch_process_gpu_error_detected(p)&&satchel_switch_process_gpu_error_event_signaled(p)==-1&&satchel_switch_process_suspended_tick_event_signaled(p)==-1);
        satchel_switch_process_destroy(p);
    }
    SatchelSwitchProcess *p=thread_fixture();uint32_t object,handle=launch_common(p,0,&object);
    CHECK(!satchel_switch_process_begin_foreground_launch(p)); // A previously running unactivated process cannot be silently activated.
    SatchelSwitchAppletState state;SatchelSwitchAppletMessageInfo info;CHECK(satchel_switch_process_applet_state(p,&state)&&state.focus_state==3);
    CHECK(satchel_switch_process_applet_message_info(p,&info)&&!info.phase&&!info.pending&&!info.enqueued);
    launch_receive(p,handle,object,0,0,1);CHECK(satchel_switch_process_applet_event_signaled(p)==-1);
    satchel_switch_process_destroy(p);
}
static void test_launch_message_and_gpu_wait_separation(void) {
    SatchelSwitchProcess *p=thread_fixture();SatchelCPUProbe *main_cpu=satchel_switch_process_cpu(p);
    CHECK(satchel_switch_process_begin_foreground_launch(p));uint32_t message,suspended,gpu;make_launch_events(p,&message,&suspended,&gpu);
    uint32_t object,common=launch_common(p,0,&object);uint64_t args=0x8005000;
    CHECK(satchel_cpu_probe_write(main_cpu,args,&gpu,4));uint32_t child=thread_create(p,0x8000200,args,initial_stack39+0x3000,28,2,0);
    thread_svc(p,0x800001c,child,0);main_boundary(p,main_cpu);
    CHECK(find_thread(p,child).state==SATCHEL_SWITCH_THREAD_WAITING);
    SatchelSwitchThreadWaitInfo waits;CHECK(satchel_switch_process_thread_wait_info(p,1,&waits)&&waits.gpu_error_events==1&&!waits.applet_message_events);
    CHECK(satchel_cpu_probe_write(main_cpu,0x8004000,&message,4));SatchelSwitchRunResult result=thread_wait(p,0x8004000,1,-1,1);
    CHECK(result.state==SATCHEL_SWITCH_RUN_CPU_STOP&&result.cpu.reason==SATCHEL_CPU_BUDGET);uint64_t status;CHECK(satchel_cpu_probe_get_x(main_cpu,0,&status)&&!status);
    CHECK(satchel_switch_process_gpu_error_event_signaled(p)==0&&satchel_switch_process_suspended_tick_event_signaled(p)==0);
    launch_receive(p,common,object,0,15,0);launch_receive(p,common,object,0,1,0);
    result=thread_wait(p,0x8004000,1,-1,64);CHECK(result.state==SATCHEL_SWITCH_RUN_BLOCKED&&result.next_deadline_tick==UINT64_MAX);
    CHECK(satchel_switch_process_thread_wait_info(p,0,&waits)&&waits.applet_message_events==1&&!waits.gpu_error_events);
    CHECK(satchel_switch_process_thread_wait_info(p,1,&waits)&&waits.gpu_error_events==1&&!waits.applet_message_events);
    CHECK(!satchel_switch_process_gpu_error_detected(p)&&satchel_switch_process_gpu_error_event_signaled(p)==0&&satchel_switch_process_applet_event_signaled(p)==0);
    satchel_switch_process_destroy(p);
}

// Numeric identity fixtures use original code only. The borrowed pointer alone
// is never the retained identity when allocator addresses may be recycled.
static SatchelSwitchContextIdentity context_of(SatchelSwitchProcess *p,SatchelCPUProbe *cpu) {
    SatchelSwitchContextIdentity identity={0};
    CHECK(satchel_switch_process_context_identity(p,cpu,&identity));
    CHECK(identity.version==1&&identity.size==24&&identity.process_id&&identity.context_generation);
    CHECK(satchel_switch_process_validate_context(p,cpu,&identity));return identity;
}
static void context_refused(SatchelSwitchProcess *p,SatchelCPUProbe *cpu,const SatchelSwitchContextIdentity *identity) {
    SatchelSwitchContextIdentity out,old;memset(&out,0xa7,sizeof(out));old=out;
    CHECK(!satchel_switch_process_context_identity(p,cpu,&out)&&!memcmp(&out,&old,sizeof(out)));
    CHECK(!satchel_switch_process_validate_context(p,cpu,identity));
}
typedef struct {SatchelSwitchProcess *p;SatchelCPUProbe *cpu;SatchelSwitchContextIdentity identity;unsigned calls;} ContextCallback;
static int context_during_run(void *opaque) {
    ContextCallback *callback=opaque;callback->calls++;
    context_refused(callback->p,callback->cpu,&callback->identity);return 1;
}
static void test_context_identity_basic(void) {
    SatchelSwitchProcess *p=thread_fixture();SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);
    SatchelSwitchContextIdentity identity=context_of(p,cpu);
    CHECK(identity.context_generation==find_thread(p,0x80).id);
    SatchelSwitchContextIdentity copy=identity;
    CHECK(!satchel_switch_process_context_identity(NULL,cpu,&copy)&&!memcmp(&copy,&identity,sizeof(copy)));
    CHECK(!satchel_switch_process_context_identity(p,NULL,&copy)&&!memcmp(&copy,&identity,sizeof(copy)));
    CHECK(!satchel_switch_process_context_identity(p,cpu,NULL));
    CHECK(!satchel_switch_process_validate_context(NULL,cpu,&identity)&&!satchel_switch_process_validate_context(p,NULL,&identity));
    CHECK(!satchel_switch_process_validate_context(p,cpu,NULL));
    _Alignas(8) uint8_t misaligned[sizeof(identity)+8];memset(misaligned,0xa7,sizeof(misaligned));uint8_t before[sizeof(misaligned)];memcpy(before,misaligned,sizeof(before));
    CHECK(!satchel_switch_process_context_identity(p,cpu,(SatchelSwitchContextIdentity *)(misaligned+4))&&!memcmp(before,misaligned,sizeof(before)));
    CHECK(!satchel_switch_process_validate_context(p,cpu,(const SatchelSwitchContextIdentity *)(misaligned+4)));
    for(unsigned i=0;i<4;i++) {
        copy=identity;
        if(i==0)copy.version++;if(i==1)copy.size--;if(i==2)copy.process_id++;if(i==3)copy.context_generation++;
        CHECK(!satchel_switch_process_validate_context(p,cpu,&copy));
    }
    SatchelSwitchProcess *other=thread_fixture();SatchelCPUProbe *foreign=satchel_switch_process_cpu(other);
    SatchelSwitchContextIdentity other_identity=context_of(other,foreign);
    CHECK(other_identity.process_id!=identity.process_id&&other_identity.context_generation!=identity.context_generation);
    context_refused(p,foreign,&other_identity);context_refused(other,cpu,&identity);
    CHECK(!satchel_switch_process_validate_context(p,cpu,&other_identity));
    char error[256];SatchelCPUProbe *sibling=satchel_cpu_probe_create_shared(cpu,0x80003c0,error,sizeof(error));CHECK(sibling);
    context_refused(p,sibling,&identity);CHECK(satchel_cpu_probe_try_destroy(sibling));
    // A destroyed unowned address and an invalid synthetic pointer must reject
    // by membership; neither is dereferenced by the API.
    context_refused(p,sibling,&identity);context_refused(p,(SatchelCPUProbe *)(uintptr_t)1,&identity);
    ContextCallback callback={p,cpu,identity,0};CHECK(satchel_cpu_probe_set_pc(cpu,0x80003c0));
    SatchelCPUProbeResult stopped=satchel_cpu_probe_run(cpu,1,context_during_run,&callback);
    CHECK(stopped.reason==SATCHEL_CPU_CANCELLED&&stopped.steps==0&&callback.calls==1);
    CHECK(satchel_switch_process_validate_context(p,cpu,&identity));
    sibling=satchel_cpu_probe_create_shared(cpu,0x80003c0,error,sizeof(error));CHECK(sibling);callback.calls=0;
    stopped=satchel_cpu_probe_run(sibling,1,context_during_run,&callback);
    CHECK(stopped.reason==SATCHEL_CPU_CANCELLED&&stopped.steps==0&&callback.calls==1);
    CHECK(satchel_cpu_probe_try_destroy(sibling));CHECK(satchel_switch_process_validate_context(p,cpu,&identity));
    // A host provisional guest return changes PC, not context generation.
    CHECK(satchel_cpu_probe_set_x(cpu,30,0x80003c0)&&satchel_cpu_probe_set_pc(cpu,0x80003c0));
    CHECK(satchel_switch_process_validate_context(p,cpu,&identity));
    copy=context_of(p,cpu);CHECK(!memcmp(&copy,&identity,sizeof(copy)));
    stopped=satchel_cpu_probe_run(cpu,1,NULL,NULL);CHECK(stopped.pc==0x80003c0&&stopped.reason==SATCHEL_CPU_BUDGET);
    satchel_switch_process_destroy(other);satchel_switch_process_destroy(p);
}
static void test_context_identity_purity(void) {
    SatchelSwitchProcess *p=thread_fixture();SatchelCPUProbe *cpu=satchel_switch_process_cpu(p);
    uint64_t x[31],q[32][2],tls0,tls1;uint32_t fpcr,fpsr;
    for(unsigned i=0;i<31;i++){x[i]=UINT64_C(0x1234567800000000)+i;CHECK(satchel_cpu_probe_set_x(cpu,i,x[i]));}
    for(unsigned i=0;i<32;i++){q[i][0]=UINT64_C(0xfff0000000000001)+i;q[i][1]=~q[i][0];CHECK(satchel_cpu_probe_set_q(cpu,i,(uint8_t *)q[i]));}
    CHECK(satchel_cpu_probe_get_tls(cpu,&tls0,&tls1)&&satchel_cpu_probe_get_fp_status(cpu,&fpcr,&fpsr)==SATCHEL_CPU_FP_OK);
    uint64_t memory[4]={1,2,3,4},readback[4];CHECK(satchel_cpu_probe_write(cpu,0x8006000,memory,sizeof(memory)));
    CHECK(satchel_cpu_probe_set_pc(cpu,0x80003c0));
    SatchelSwitchContextIdentity identity=context_of(p,cpu);
    for(unsigned repeat=0;repeat<8;repeat++){SatchelSwitchContextIdentity copy=context_of(p,cpu);CHECK(!memcmp(&copy,&identity,sizeof(copy)));}
    for(unsigned i=0;i<31;i++){uint64_t value;CHECK(satchel_cpu_probe_get_x(cpu,i,&value)&&value==x[i]);}
    for(unsigned i=0;i<32;i++){uint64_t value[2];CHECK(satchel_cpu_probe_get_q(cpu,i,(uint8_t *)value)&&!memcmp(value,q[i],16));}
    uint64_t a,b;uint32_t c,d;CHECK(satchel_cpu_probe_get_tls(cpu,&a,&b)&&a==tls0&&b==tls1);
    CHECK(satchel_cpu_probe_get_fp_status(cpu,&c,&d)==SATCHEL_CPU_FP_OK&&c==fpcr&&d==fpsr);
    CHECK(satchel_cpu_probe_read(cpu,0x8006000,readback,sizeof(readback))&&!memcmp(memory,readback,sizeof(memory)));
    SatchelCPUProbeResult stopped=satchel_cpu_probe_run(cpu,1,NULL,NULL);CHECK(stopped.pc==0x80003c0&&stopped.reason==SATCHEL_CPU_BUDGET);
    // Original LDXR/STXR pair proves exact no-op checks preserve the actual
    // exclusive reservation and do not alter the stopped next-instruction PC.
    CHECK(satchel_cpu_probe_set_x(cpu,0,0x8006000)&&satchel_cpu_probe_set_x(cpu,3,UINT64_C(0xfedcba9876543210))&&satchel_cpu_probe_set_pc(cpu,0x8000480));
    stopped=satchel_cpu_probe_run(cpu,1,NULL,NULL);CHECK(stopped.reason==SATCHEL_CPU_BUDGET&&stopped.pc==0x8000484);
    CHECK(satchel_switch_process_validate_context(p,cpu,&identity));SatchelSwitchContextIdentity copy=context_of(p,cpu);CHECK(!memcmp(&copy,&identity,24));
    // Failed identity validation must also preserve the reservation.
    copy.context_generation++;CHECK(!satchel_switch_process_validate_context(p,cpu,&copy));
    stopped=satchel_cpu_probe_run(cpu,1,NULL,NULL);CHECK(stopped.reason==SATCHEL_CPU_BUDGET&&stopped.pc==0x8000488);
    uint64_t status,value;CHECK(satchel_cpu_probe_get_x(cpu,2,&status)&&status==0);
    CHECK(satchel_cpu_probe_read(cpu,0x8006000,&value,8)&&value==UINT64_C(0xfedcba9876543210));
    // Identity is not a terminal-usability certificate. The retained runnable
    // context remains identifiable after an original unsupported instruction.
    CHECK(satchel_cpu_probe_set_pc(cpu,0x8000488));stopped=satchel_cpu_probe_run(cpu,1,NULL,NULL);CHECK(stopped.reason==SATCHEL_CPU_UNSUPPORTED);
    CHECK(satchel_switch_process_validate_context(p,cpu,&identity));copy=context_of(p,cpu);CHECK(!memcmp(&copy,&identity,24));
    satchel_switch_process_destroy(p);
}
static void context_select(SatchelSwitchProcess *p,uint64_t id) {
    for(unsigned i=0;i<8;i++) {
        SatchelSwitchRunResult result=satchel_switch_process_run_scheduled(p,1,NULL,NULL);
        CHECK(result.state==SATCHEL_SWITCH_RUN_CPU_STOP&&result.cpu.reason==SATCHEL_CPU_BUDGET);
        if(result.thread_id==id)return;
    }
    CHECK(0);
}
static void context_wait_for_state(SatchelSwitchProcess *p,uint32_t handle,unsigned state) {
    for(unsigned i=0;i<8&&find_thread(p,handle).state!=state;i++) {
        SatchelSwitchRunResult result=satchel_switch_process_run_scheduled(p,1,NULL,NULL);
        CHECK(result.state==SATCHEL_SWITCH_RUN_CPU_STOP&&result.cpu.reason==SATCHEL_CPU_BUDGET);
    }
    CHECK(find_thread(p,handle).state==state);
}
static void test_context_identity_lifecycle(void) {
    SatchelSwitchProcess *p=thread_fixture();SatchelCPUProbe *main_cpu=satchel_switch_process_cpu(p);
    SatchelSwitchContextIdentity main_id=context_of(p,main_cpu);
    uint32_t child=thread_create(p,0x80003c0,0,initial_stack39+0x2000,28,2,0);
    SatchelSwitchThreadInfo info=find_thread(p,child);CHECK(info.state==SATCHEL_SWITCH_THREAD_CREATED);
    SatchelSwitchContextIdentity invented=main_id;invented.context_generation=info.id;
    CHECK(!satchel_switch_process_validate_context(p,main_cpu,&invented));
    thread_svc(p,0x800001c,child,0);CHECK(satchel_cpu_probe_set_pc(main_cpu,0x80003c0));
    context_select(p,info.id);SatchelCPUProbe *child_cpu=satchel_switch_process_cpu(p);CHECK(child_cpu!=main_cpu);
    SatchelSwitchContextIdentity child_id=context_of(p,child_cpu);CHECK(child_id.context_generation==info.id&&child_id.process_id==main_id.process_id);
    context_refused(p,main_cpu,&main_id);CHECK(!satchel_switch_process_validate_context(p,child_cpu,&main_id));
    context_select(p,main_id.context_generation);CHECK(satchel_switch_process_validate_context(p,main_cpu,&main_id));context_refused(p,child_cpu,&child_id);
    context_select(p,child_id.context_generation);CHECK(satchel_switch_process_validate_context(p,child_cpu,&child_id));
    CHECK(satchel_cpu_probe_set_pc(child_cpu,0x8000020)&&satchel_cpu_probe_set_x(child_cpu,1,0)&&satchel_cpu_probe_set_x(child_cpu,2,0)&&satchel_cpu_probe_set_x(child_cpu,3,UINT64_MAX));
    context_wait_for_state(p,child,SATCHEL_SWITCH_THREAD_WAITING);context_refused(p,child_cpu,&child_id);
    context_select(p,main_id.context_generation);
    SatchelSwitchRunResult blocked=thread_wait(p,0,0,-1,32);CHECK(blocked.state==SATCHEL_SWITCH_RUN_BLOCKED);
    context_refused(p,main_cpu,&main_id);context_refused(p,child_cpu,&child_id);
    satchel_switch_process_destroy(p);
    // Exit, handle release, and slot reuse preserve neither pointer identity nor
    // numeric generation. An old token must reject even against the new CPU.
    p=thread_fixture();main_cpu=satchel_switch_process_cpu(p);main_id=context_of(p,main_cpu);
    uint64_t previous=0;
    for(unsigned iteration=0;iteration<8;iteration++) {
        child=thread_create(p,0x80003c0,0,initial_stack39+0x2000,28,2,0);info=find_thread(p,child);
        CHECK(info.id>previous&&info.tls_address==0x31000200);thread_svc(p,0x800001c,child,0);
        CHECK(satchel_cpu_probe_set_pc(main_cpu,0x80003c0));context_select(p,info.id);child_cpu=satchel_switch_process_cpu(p);
        SatchelSwitchContextIdentity fresh=context_of(p,child_cpu);
        if(iteration)CHECK(!satchel_switch_process_validate_context(p,child_cpu,&child_id));
        child_id=fresh;previous=info.id;CHECK(satchel_cpu_probe_set_pc(child_cpu,0x8000300));
        context_wait_for_state(p,child,SATCHEL_SWITCH_THREAD_EXITED);context_refused(p,child_cpu,&child_id);
        context_select(p,main_id.context_generation);handle_close(p,child,0);
        CHECK(satchel_switch_process_thread_count(p)==1);context_refused(p,child_cpu,&child_id);
    }
    CHECK(satchel_cpu_probe_set_pc(main_cpu,0x8000300));SatchelSwitchRunResult exited=satchel_switch_process_run_scheduled(p,8,NULL,NULL);
    CHECK(exited.state==SATCHEL_SWITCH_RUN_EXITED);context_refused(p,main_cpu,&main_id);satchel_switch_process_destroy(p);
}
static void test_context_identity_reaped_current(void) {
    SatchelSwitchProcess *p=thread_fixture();SatchelCPUProbe *main_cpu=satchel_switch_process_cpu(p);SatchelSwitchContextIdentity main_id=context_of(p,main_cpu);
    uint32_t message,suspended,gpu;make_events(p,&message,&suspended,&gpu);
    uint64_t argument=0x8004000;CHECK(satchel_cpu_probe_write(main_cpu,argument,&gpu,4));
    uint32_t child=thread_create(p,0x8000200,argument,initial_stack39+0x2000,28,2,0);
    thread_svc(p,0x800001c,child,0);main_boundary(p,main_cpu);handle_close(p,child,0);handle_close(p,0x80,0);
    CHECK(satchel_cpu_probe_set_pc(main_cpu,0x8000028));SatchelSwitchRunResult blocked=satchel_switch_process_run_scheduled(p,100,NULL,NULL);
    CHECK(blocked.state==SATCHEL_SWITCH_RUN_BLOCKED&&satchel_switch_process_cpu(p)==NULL);
    context_refused(p,main_cpu,&main_id); // Expired raw pointer is compared only.
    CHECK(satchel_switch_process_report_gpu_error(p));SatchelSwitchRunResult result=satchel_switch_process_run_scheduled(p,100,NULL,NULL);
    CHECK(result.state==SATCHEL_SWITCH_RUN_CPU_STOP&&result.cpu.reason==SATCHEL_CPU_SVC&&result.cpu.svc==0x7f);
    SatchelCPUProbe *child_cpu=satchel_switch_process_cpu(p);SatchelSwitchContextIdentity child_id=context_of(p,child_cpu);
    CHECK(child_id.process_id==main_id.process_id&&child_id.context_generation!=main_id.context_generation);
    CHECK(!satchel_switch_process_validate_context(p,child_cpu,&main_id));
    CHECK(satchel_cpu_probe_set_pc(child_cpu,0x800021c));result=satchel_switch_process_run_scheduled(p,100,NULL,NULL);
    CHECK(result.state==SATCHEL_SWITCH_RUN_EXITED);context_refused(p,child_cpu,&child_id);satchel_switch_process_destroy(p);
}
static void test_context_identity(void) {
    test_context_identity_basic();test_context_identity_purity();test_context_identity_lifecycle();test_context_identity_reaped_current();
}

int main(int argc,char **argv) {
    test_context_identity();
    if(argc==2&&!strcmp(argv[1],"--context-only")){printf("%u original context-identity assertions passed\n",checks);return 0;}
    CHECK(argc==1);
    test_foreground_launch_queue();test_launch_message_and_gpu_wait_separation();test_wait_classification();test_mutex_failed_write_handoff();test_mutex_deadlock_cancel_and_cleanup();test_mutex_validation();test_mutex_handoff_and_cancel();test_mutex_priority_and_references();test_mutex_transitive_and_equal_order();test_affinity();test_watcher();test_wait_order_and_timers();test_recycled_threads();test_thread_fp_inheritance();test_all_tls_storage_exclusion();test_reaped_main_lifetime();test_real_wait_cancel();
#if defined(SATCHEL_CPU_TESTING)
    test_deadline_before_gpu_signal();test_sleep_deadlines();
#endif
    printf("%u original scheduled-thread assertions passed\n",checks);return 0;
}
