// Real Metal plus the production presentation, copy and encoding workers.
// The fixture also holds a guest-backed buffer across retirement retries.
#define front_metal_layer session_fixture_layer
#include "nvn_mtl.m"
#undef front_metal_layer
#include <assert.h>
#include <sys/mman.h>

static CAMetalLayer *fixture_layer;
void *session_fixture_layer(void) { return (__bridge void *)fixture_layer; }
static void wait_flag(_Atomic int *flag) {
    uint64_t until = now_ns() + UINT64_C(5000000000);
    while (!atomic_load(flag)) { assert(now_ns() < until); usleep(1000); }
}
static uint64_t rss(void) {
    mach_task_basic_info_data_t info; mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    assert(task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) == KERN_SUCCESS);
    return info.resident_size;
}
int main(void) {
    setenv("DM_PSO_ARCHIVE", "off", 1); setenv("DM_PSO_MANIFEST", "off", 1);
    setenv("DM_PSO_PREPARE", "0", 1); setenv("DM_NVN_ENC_THREAD", "1", 1);
    uint64_t baseline = 0;
    for (int cycle = 0; cycle <= 5; cycle++) {
        @autoreleasepool {
            assert(!satchel_mtl_session_prepare()); assert(!satchel_session_begin()); mtl_init();
            fixture_layer = [CAMetalLayer layer]; fixture_layer.device = g_dev;
            fixture_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
            fixture_layer.drawableSize = CGSizeMake(64, 64);
            present_start();
            g_link_started = 1;
            assert(!satchel_native_create(&satchel_present_workers, NULL, link_main, NULL));
            size_t bytes = 32u << 20;
            uint8_t *source = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
            uint8_t *destination = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
            __attribute__((objc_precise_lifetime)) id<MTLBuffer> borrower = [g_dev newBufferWithBytesNoCopy:source length:bytes options:MTLResourceStorageModeShared deallocator:nil];
            assert(borrower); if (!g_keep) g_keep = [NSMutableArray array]; [g_keep addObject:borrower];
            assert(source != MAP_FAILED && destination != MAP_FAILED); memset(source, 0x40 + cycle, bytes);
            uint64_t copied = ubc_push(destination, source, bytes);
            _Atomic uint64_t note = 0;
            rec_t *record = rec_put(RO_NOTE, 0); record->a[0] = (uintptr_t)&note; record->a[1] = 17;
            rec_publish();
            __block _Atomic int entered = 0, completed = 0, nested = 0;
            dispatch_semaphore_t release = dispatch_semaphore_create(0);
            id<MTLCommandQueue> queue = [g_dev newCommandQueue];
            id<MTLCommandBuffer> buffer = [queue commandBuffer];
            [buffer addCompletedHandler:satchel_mtl_completed(^(id<MTLCommandBuffer> cb) {
                assert(cb.status == MTLCommandBufferStatusCompleted);
                atomic_store(&entered, 1); dispatch_semaphore_wait(release, DISPATCH_TIME_FOREVER);
                assert(source[0] == 0x40 + cycle); atomic_store(&completed, 1);
            })];
            [buffer commit]; wait_flag(&entered);
            dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
                dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 50000000), dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{ atomic_store(&nested, 1); });
            });
            satchel_session_request_stop(); satchel_mtl_request_stop(); assert(!satchel_session_join(2000));
            assert(!satchel_native_join(&satchel_present_workers, 2000));
            assert(satchel_mtl_producers_join(20) == ETIMEDOUT);
            assert(!atomic_load(&completed));
            dispatch_semaphore_signal(release);
            assert(!satchel_mtl_producers_join(5000));
            assert(atomic_load(&completed) && atomic_load(&nested));
            assert(atomic_load(&note) == 17 && g_ubc_done == copied);
            assert(!memcmp(source, destination, bytes));
            queue = nil; buffer = nil; fixture_layer = nil;
            assert(satchel_mtl_session_retire() == EBUSY);
            assert(satchel_mtl_session_retire() == EBUSY);
            assert(borrower.length == bytes && source[0] == 0x40 + cycle);
            borrower = nil;
            assert(!satchel_mtl_session_retire());
            assert(!munmap(source, bytes) && !munmap(destination, bytes));
            assert(!satchel_session_end());
        }
        usleep(50000);
        uint64_t resident = rss(); if (!cycle) baseline = resident;
        assert(resident < baseline + (8u << 20));
        printf("graphics-cycle=%d rss=%llu delta=%lld native_workers=0 gpu_callback=joined nested_jobs=joined copies=drained encoder=joined link=joined present=joined\n",
               cycle, (unsigned long long)resident, (long long)resident - (long long)baseline);
    }
    puts("PASS: five native graphics producer restarts with real Metal; typed retirement and retained-borrower retry exercised");
    return 0;
}
