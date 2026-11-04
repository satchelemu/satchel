#include "GuestTransfers.h"
#include "VulkanTransferBatch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
static unsigned checks;
static void check_at(int v,unsigned line){++checks;if(!v){fprintf(stderr,"guest transfer assertion failed line%u check%u\n",line,checks);exit(1);}}
#define check(v) check_at((v),__LINE__)
enum {DEVICE=0x300000,BUILDER=0x304000,POOL=0x305000,QUEUE=0x308000,QBUILDER=0x30a000,COMMAND=0x30b000,CONTROL=0x30c000,HANDLES=0x30d000,STORAGE=0x400000,QMEM=0x600000};
static void put32(uint8_t *p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static void put64(uint8_t *p,uint64_t v){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(v>>(8*i));}
static uint64_t get64(const uint8_t *p){uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
static unsigned fail_submit,fail_write,retained_error;
/* Failure shims only refuse work or report a real retained job as an error.
 * They never fabricate a successful driver call or fence. */
SatchelVulkanStatus satchel_transfer_test_batch(uint64_t device,const SatchelVulkanTransferCommand *commands,size_t count,uint64_t *job,SatchelVulkanCancelled cancel,void *context){
    if(fail_submit){--fail_submit;*job=0;return SATCHEL_VULKAN_API_ERROR;}
    SatchelVulkanStatus status=satchel_vulkan_transfer_batch(device,commands,count,job,cancel,context);
    if(retained_error && *job){--retained_error;return SATCHEL_VULKAN_API_ERROR;}return status;
}
SatchelCPUWriteResult satchel_transfer_test_writev(SatchelCPUProbe *cpu,const SatchelCPUProbeWrite *writes,size_t count){if(fail_write){--fail_write;return SATCHEL_CPU_WRITE_ALLOCATION;}return satchel_cpu_probe_writev(cpu,writes,count);}
typedef struct {SatchelCPUProbe *cpu;SatchelGuestPools *pools;SatchelGuestTransfers *transfers;uint64_t backend,gpu[3];unsigned reenter;} Fixture;
static int guard(void *context,uint64_t address,uint64_t size){(void)context;return !(address<DEVICE+0x3000 && DEVICE<address+size);}
static int validate(void *context,SatchelCPUProbe *cpu,uint64_t address,uint64_t *backend,uint64_t *generation){
    Fixture *f=context;uint8_t bytes[16];SatchelGuestDeviceCaps caps={0};
    if(f->reenter){f->reenter=0;SatchelGuestTransferResult out={0};SatchelGuestTransferRequest r={SATCHEL_GT_QUEUE_DEFAULTS,{QBUILDER,0,0,0,0}};SatchelGuestTransfers *same=f->transfers;
        check(satchel_guest_transfers_execute(same,cpu,&r,&out,NULL,NULL)==SATCHEL_GUEST_POOL_BUSY);check(satchel_guest_transfers_try_destroy(&same)==SATCHEL_GUEST_POOL_BUSY && same==f->transfers);}
    if(address!=DEVICE || !satchel_cpu_probe_read(cpu,address,bytes,16) || get64(bytes)!=0x1504c4456ULL || get64(bytes+8)!=1 || satchel_vulkan_device_capabilities(f->backend,&caps)!=SATCHEL_VULKAN_OK)return 0;
    *backend=f->backend;*generation=1;return 1;
}
static uint64_t pool(Fixture *f,unsigned op,uint64_t object,uint64_t a,uint64_t b,unsigned expected){SatchelGuestPoolRequest r={op,object,a,b};SatchelGuestPoolResult out={0};check(satchel_guest_pools_execute(f->pools,f->cpu,&r,&out)==expected && out.status==expected);return out.value;}
static uint64_t call_cancel(Fixture *f,unsigned op,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,unsigned expected,SatchelCPUProbeCancelled cancel,void *context){
    SatchelGuestTransferRequest r={op,{a,b,c,d,e}};SatchelGuestTransferResult out={99,99,99};uint32_t actual=satchel_guest_transfers_execute(f->transfers,f->cpu,&r,&out,cancel,context);
    if(actual!=expected)fprintf(stderr,"operation%u expected%u actual%u\n",op,expected,actual);
    check(actual==expected && out.status==expected);if(expected)check(!out.set_value && !out.value);return out.value;
}
static uint64_t call(Fixture *f,unsigned op,uint64_t a,uint64_t b,uint64_t c,uint64_t d,uint64_t e,unsigned expected){return call_cancel(f,op,a,b,c,d,e,expected,NULL,NULL);}
#define ok(f,op,a,b,c,d,e) call(f,op,a,b,c,d,e,SATCHEL_GUEST_POOL_OK)
static void baseline(void){SatchelVulkanBufferStats stats={0};check(satchel_vulkan_buffer_stats(&stats)==SATCHEL_VULKAN_OK && !stats.live_buffers && !stats.allocated_bytes && !satchel_vulkan_device_live_count() && !satchel_vulkan_submission_live_count());}
static void make(Fixture *f){memset(f,0,sizeof(*f));uint8_t code[4]={0};put32(code,0xd4200000);SatchelCPUProbeRegion regions[]={{0x100000,code,4,4096,5},{DEVICE,NULL,0,131072,3},{STORAGE,NULL,0,262144,3},{QMEM,NULL,0,1048576,3}};
    char error[160]={0};f->cpu=satchel_cpu_probe_create(regions,4,0x100000,error,sizeof(error));check(f->cpu!=NULL);SatchelGuestDeviceCaps caps={0};check(satchel_vulkan_device_create(0,&f->backend,&caps)==SATCHEL_VULKAN_OK);
    uint8_t tag[16];put64(tag,0x1504c4456ULL);put64(tag+8,1);check(satchel_cpu_probe_write(f->cpu,DEVICE,tag,16));
    f->pools=satchel_guest_pools_create_protected(validate,guard,f);check(f->pools!=NULL);f->transfers=satchel_guest_transfers_create(f->pools,validate,guard,f);check(f->transfers!=NULL);
    uint8_t pattern[256];memset(pattern,0xa5,sizeof(pattern));for(unsigned i=0;i<3;i++){
        check(satchel_cpu_probe_write(f->cpu,STORAGE+65536*i,pattern,sizeof(pattern)));
        pool(f,SATCHEL_GUEST_POOL_BUILDER_DEFAULTS,BUILDER,0,0,0);pool(f,SATCHEL_GUEST_POOL_BUILDER_DEVICE,BUILDER,DEVICE,0,0);pool(f,SATCHEL_GUEST_POOL_BUILDER_STORAGE,BUILDER,STORAGE+65536*i,65536,0);pool(f,SATCHEL_GUEST_POOL_BUILDER_FLAGS,BUILDER,0x24,0,0);
        check(pool(f,SATCHEL_GUEST_POOL_INITIALIZE,POOL+256*i,BUILDER,0,0)==1);f->gpu[i]=pool(f,SATCHEL_GUEST_POOL_GPU_ADDRESS,POOL+256*i,0,0,0);}
    f->reenter=1;ok(f,SATCHEL_GT_QUEUE_DEVICE,QBUILDER,DEVICE,0,0,0);check(!f->reenter);ok(f,SATCHEL_GT_QUEUE_DEFAULTS,QBUILDER,0,0,0,0);
    ok(f,SATCHEL_GT_QUEUE_COMMAND_SIZE,QBUILDER,4096,0,0,0);ok(f,SATCHEL_GT_QUEUE_COMPUTE_SIZE,QBUILDER,0,0,0,0);ok(f,SATCHEL_GT_QUEUE_CONTROL_SIZE,QBUILDER,4096,0,0,0);
    check(ok(f,SATCHEL_GT_QUEUE_MEMORY_SIZE,QBUILDER,0,0,0,0)==8192);ok(f,SATCHEL_GT_QUEUE_MEMORY,QBUILDER,QMEM,8192,0,0);check(ok(f,SATCHEL_GT_QUEUE_INITIALIZE,QUEUE,QBUILDER,0,0,0)==1);
    check(ok(f,SATCHEL_GT_COMMAND_INITIALIZE,COMMAND,DEVICE,0,0,0)==1);ok(f,SATCHEL_GT_COMMAND_MEMORY,COMMAND,POOL,0,4096,0);ok(f,SATCHEL_GT_CONTROL_MEMORY,COMMAND,CONTROL,128,0,0);
}
static void drop(Fixture *f){for(unsigned i=0;i<5000 && f->transfers;i++){uint32_t status=satchel_guest_transfers_try_destroy(&f->transfers);check(status==0 || status==SATCHEL_GUEST_POOL_BUSY);if(status){struct timespec t={0,1000000};nanosleep(&t,NULL);}}check(!f->transfers);check(satchel_guest_pools_try_destroy(&f->pools)==0 && !f->pools);if(f->cpu)check(satchel_cpu_probe_try_destroy(f->cpu));check(satchel_vulkan_device_destroy(f->backend)==SATCHEL_VULKAN_OK);baseline();}
static uint64_t record(Fixture *f){ok(f,SATCHEL_GT_BEGIN,COMMAND,0,0,0,0);check(ok(f,SATCHEL_GT_IS_RECORDING,COMMAND,0,0,0,0)==1);
    ok(f,SATCHEL_GT_CLEAR,COMMAND,f->gpu[1]+16,16,0x11223344,0);ok(f,SATCHEL_GT_COPY,COMMAND,f->gpu[1]+16,f->gpu[2]+32,16,0);uint64_t id=ok(f,SATCHEL_GT_END,COMMAND,0,0,0,0);check(id!=0);uint8_t h[8];put64(h,id);check(satchel_cpu_probe_write(f->cpu,HANDLES,h,8));return id;}
static int cancelled(void *context){unsigned *calls=context;return ++*calls>=2;}
static int always(void *context){(void)context;return 1;}
static void unchanged(Fixture *f,uint64_t address,const void *expected,size_t size){uint8_t bytes[256];check(size<=sizeof(bytes));check(satchel_cpu_probe_read(f->cpu,address,bytes,size));check(!memcmp(bytes,expected,size));}
int main(void){baseline();Fixture f;make(&f);uint64_t handle=record(&f);uint32_t children=0;check(satchel_guest_transfers_device_children(f.transfers,DEVICE,&children)==0 && children==3);
    check(ok(&f,SATCHEL_GT_COMMAND_USED,COMMAND,0,0,0,0)==112);check(ok(&f,SATCHEL_GT_CONTROL_USED,COMMAND,0,0,0,0)==64);
    pool(&f,SATCHEL_GUEST_POOL_FINALIZE,POOL,0,0,SATCHEL_GUEST_POOL_BUSY);pool(&f,SATCHEL_GUEST_POOL_FINALIZE,POOL+256,0,0,SATCHEL_GUEST_POOL_BUSY);
    call(&f,SATCHEL_GT_BEGIN,COMMAND,0,0,0,0,SATCHEL_GUEST_POOL_BUSY);
    uint8_t object[64],zeros[64]={0};check(satchel_cpu_probe_read(f.cpu,QUEUE,object,64));
    // Failure before GPU ownership leaves bounded preparation bytes, not a fake
    // pending object or consumed recording. No allocating rollback is attempted.
    fail_submit=1;call(&f,SATCHEL_GT_QUEUE_SUBMIT,QUEUE,1,HANDLES,0,0,SATCHEL_GUEST_POOL_BACKEND);unchanged(&f,QUEUE,object,64);check(!satchel_vulkan_submission_live_count());
    uint8_t prepared[96],guard_bytes[32];check(satchel_cpu_probe_read(f.cpu,QMEM+64,prepared,sizeof(prepared)));check(get64(prepared+24)==f.gpu[1]+16 && get64(prepared+48+8)==f.gpu[1]+16);
    check(satchel_cpu_probe_read(f.cpu,QMEM+64+96,guard_bytes,sizeof(guard_bytes)));check(!memcmp(guard_bytes,zeros,sizeof(guard_bytes)));
    unsigned cancel_calls=0;call_cancel(&f,SATCHEL_GT_QUEUE_SUBMIT,QUEUE,1,HANDLES,0,0,SATCHEL_GUEST_TRANSFER_CANCELLED,cancelled,&cancel_calls);check(cancel_calls==2 && !satchel_vulkan_submission_live_count());unchanged(&f,QUEUE,object,64);
    // Successful real GPU work remains owned even when the API reports failure.
    retained_error=1;call(&f,SATCHEL_GT_QUEUE_SUBMIT,QUEUE,1,HANDLES,0,0,SATCHEL_GUEST_POOL_BACKEND);check(satchel_vulkan_submission_live_count()==1);
    call(&f,SATCHEL_GT_HANDLE_FINALIZE,DEVICE,handle,0,0,0,SATCHEL_GUEST_POOL_BUSY);call(&f,SATCHEL_GT_QUEUE_FINALIZE,QUEUE,0,0,0,0,SATCHEL_GUEST_POOL_BUSY);
    call_cancel(&f,SATCHEL_GT_QUEUE_FINISH,QUEUE,0,0,0,0,SATCHEL_GUEST_TRANSFER_CANCELLED,always,NULL);check(satchel_vulkan_submission_live_count()==1);
    // Real completion can precede a failed CPU status write. A retry must not
    // re-wait a consumed backend token, or release the recording prematurely.
    fail_write=1;call(&f,SATCHEL_GT_QUEUE_FINISH,QUEUE,0,0,0,0,SATCHEL_GUEST_POOL_ALLOCATION);check(!satchel_vulkan_submission_live_count());call(&f,SATCHEL_GT_HANDLE_FINALIZE,DEVICE,handle,0,0,0,SATCHEL_GUEST_POOL_BUSY);
    ok(&f,SATCHEL_GT_QUEUE_FINISH,QUEUE,0,0,0,0);pool(&f,SATCHEL_GUEST_POOL_INVALIDATE,POOL+512,0,256,0);
    uint8_t expected[256];memset(expected,0xa5,sizeof(expected));for(unsigned i=32;i<48;i+=4)put32(expected+i,0x11223344);unchanged(&f,STORAGE+131072,expected,256);
    check(satchel_cpu_probe_read(f.cpu,QUEUE,object,64));check(get64(object+16)==1 && get64(object+24)==1 && get64(object+32)==0);
    // Handles remain valid after Finish until the actual explicit public device
    // finalizer; command-buffer finalization need not discard closed handles.
    ok(&f,SATCHEL_GT_COMMAND_FINALIZE,COMMAND,0,0,0,0);ok(&f,SATCHEL_GT_QUEUE_SUBMIT,QUEUE,1,HANDLES,0,0);ok(&f,SATCHEL_GT_QUEUE_FINISH,QUEUE,0,0,0,0);
    ok(&f,SATCHEL_GT_HANDLE_FINALIZE,DEVICE,handle,0,0,0);call(&f,SATCHEL_GT_HANDLE_FINALIZE,DEVICE,handle,0,0,0,SATCHEL_GUEST_POOL_BAD_OBJECT);call(&f,SATCHEL_GT_QUEUE_SUBMIT,QUEUE,1,HANDLES,0,0,SATCHEL_GUEST_POOL_BAD_OBJECT);ok(&f,SATCHEL_GT_QUEUE_FINALIZE,QUEUE,0,0,0,0);drop(&f);
    // Tampering, unsupported operations and admission failures never submit.
    make(&f);handle=record(&f);uint8_t original[112];check(satchel_cpu_probe_read(f.cpu,STORAGE,original,sizeof(original)));uint8_t corrupt=0;check(satchel_cpu_probe_write(f.cpu,STORAGE,&corrupt,1));call(&f,SATCHEL_GT_QUEUE_SUBMIT,QUEUE,1,HANDLES,0,0,SATCHEL_GUEST_POOL_BAD_OBJECT);check(satchel_cpu_probe_write(f.cpu,STORAGE,original,sizeof(original)));
    call(&f,SATCHEL_GT_QUEUE_SUBMIT,QUEUE,0,HANDLES,0,0,SATCHEL_GUEST_POOL_INVALID);call(&f,SATCHEL_GT_QUEUE_SUBMIT,QUEUE,9,HANDLES,0,0,SATCHEL_GUEST_POOL_INVALID);call(&f,SATCHEL_GT_QUEUE_SUBMIT,QUEUE,1,UINT64_MAX,0,0,SATCHEL_GUEST_POOL_MEMORY);
    fail_write=1;call(&f,SATCHEL_GT_QUEUE_SUBMIT,QUEUE,1,HANDLES,0,0,SATCHEL_GUEST_POOL_ALLOCATION);check(!satchel_vulkan_submission_live_count());ok(&f,SATCHEL_GT_HANDLE_FINALIZE,DEVICE,handle,0,0,0);
    ok(&f,SATCHEL_GT_BEGIN,COMMAND,0,0,0,0);call(&f,SATCHEL_GT_END,COMMAND,0,0,0,0,SATCHEL_GUEST_POOL_UNSUPPORTED);call(&f,SATCHEL_GT_CLEAR,COMMAND,f.gpu[0],4,0,0,SATCHEL_GUEST_POOL_BAD_OBJECT);
    call(&f,SATCHEL_GT_CLEAR,COMMAND,f.gpu[1]+1,4,0,0,SATCHEL_GUEST_POOL_UNSUPPORTED);call(&f,SATCHEL_GT_COPY,COMMAND,f.gpu[1],f.gpu[2],4,1,SATCHEL_GUEST_POOL_UNSUPPORTED);call(&f,SATCHEL_GT_COPY,COMMAND,f.gpu[1],f.gpu[1]+1,4,0,SATCHEL_GUEST_POOL_UNSUPPORTED);
    for(unsigned i=0;i<32;i++)ok(&f,SATCHEL_GT_CLEAR,COMMAND,f.gpu[1]+4*i,4,i,0);call(&f,SATCHEL_GT_CLEAR,COMMAND,f.gpu[1]+128,4,0,0,SATCHEL_GUEST_POOL_LIMIT_REACHED);
    uint64_t newer=ok(&f,SATCHEL_GT_END,COMMAND,0,0,0,0);check(newer>handle);uint8_t h[8];put64(h,newer);check(satchel_cpu_probe_write(f.cpu,HANDLES,h,8));ok(&f,SATCHEL_GT_QUEUE_SUBMIT,QUEUE,1,HANDLES,0,0);
    // CPU-free retirement owns a real queued job and all leases after CPU dies.
    check(satchel_cpu_probe_try_destroy(f.cpu));f.cpu=NULL;drop(&f);
    // Fixed handle capacity, independent retained command/control allocations,
    // generation-safe command-object reuse, and exact command grant accounting.
    make(&f);ok(&f,SATCHEL_GT_COMMAND_FINALIZE,COMMAND,0,0,0,0);uint64_t ids[16]={0};
    for(unsigned i=0;i<16;i++){
        ok(&f,SATCHEL_GT_COMMAND_INITIALIZE,COMMAND,DEVICE,0,0,0);ok(&f,SATCHEL_GT_COMMAND_MEMORY,COMMAND,POOL,i*64,64,0);ok(&f,SATCHEL_GT_CONTROL_MEMORY,COMMAND,CONTROL+i*64,64,0,0);
        ok(&f,SATCHEL_GT_BEGIN,COMMAND,0,0,0,0);ok(&f,SATCHEL_GT_CLEAR,COMMAND,f.gpu[1],4,i,0);call(&f,SATCHEL_GT_CLEAR,COMMAND,f.gpu[1]+4,4,i,0,SATCHEL_GUEST_POOL_LIMIT_REACHED);
        ids[i]=ok(&f,SATCHEL_GT_END,COMMAND,0,0,0,0);if(i)check(ids[i]>ids[i-1]);ok(&f,SATCHEL_GT_COMMAND_FINALIZE,COMMAND,0,0,0,0);
    }
    ok(&f,SATCHEL_GT_COMMAND_INITIALIZE,COMMAND,DEVICE,0,0,0);ok(&f,SATCHEL_GT_COMMAND_MEMORY,COMMAND,POOL,16*64,64,0);ok(&f,SATCHEL_GT_CONTROL_MEMORY,COMMAND,CONTROL+16*64,64,0,0);
    ok(&f,SATCHEL_GT_BEGIN,COMMAND,0,0,0,0);ok(&f,SATCHEL_GT_CLEAR,COMMAND,f.gpu[1],4,0,0);call(&f,SATCHEL_GT_END,COMMAND,0,0,0,0,SATCHEL_GUEST_POOL_LIMIT_REACHED);
    ok(&f,SATCHEL_GT_HANDLE_FINALIZE,DEVICE,ids[0],0,0,0);uint64_t reused=ok(&f,SATCHEL_GT_END,COMMAND,0,0,0,0);check(reused>ids[15]);call(&f,SATCHEL_GT_HANDLE_FINALIZE,DEVICE,ids[0],0,0,0,SATCHEL_GUEST_POOL_BAD_OBJECT);
    uint8_t list[64];for(unsigned i=0;i<8;i++)put64(list+i*8,ids[i+1]);check(satchel_cpu_probe_write(f.cpu,HANDLES,list,sizeof(list)));ok(&f,SATCHEL_GT_QUEUE_SUBMIT,QUEUE,8,HANDLES,0,0);ok(&f,SATCHEL_GT_QUEUE_FINISH,QUEUE,0,0,0,0);drop(&f);
    make(&f);for(unsigned i=0;i<7;i++)ok(&f,SATCHEL_GT_COMMAND_INITIALIZE,0x310000+i*256,DEVICE,0,0,0);call(&f,SATCHEL_GT_COMMAND_INITIALIZE,0x311000,DEVICE,0,0,0,SATCHEL_GUEST_POOL_LIMIT_REACHED);
    call(&f,SATCHEL_GT_QUEUE_INITIALIZE,0x314000,QBUILDER,0,0,0,SATCHEL_GUEST_POOL_BAD_OBJECT); // Existing owned queue backing cannot be reused.
    call(&f,SATCHEL_GT_COMMAND_MEMORY,COMMAND,POOL,0,64,0,SATCHEL_GUEST_POOL_UNSUPPORTED);drop(&f);
    printf("guest transfer checks passed: %u assertions (original direct state-machine fixtures; no interpreted dispatch or game rendering)\n",checks);return 0;
}
