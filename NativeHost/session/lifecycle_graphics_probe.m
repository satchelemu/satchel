// Synthetic Metal work through the production suspend gate and callback drain.
#define front_metal_layer lifecycle_fixture_layer
#include "nvn_mtl.m"
#undef front_metal_layer
#include <assert.h>
static CAMetalLayer *fixture_layer;
void *lifecycle_fixture_layer(void) { return (__bridge void *)fixture_layer; }
static _Atomic int finish, submissions;
extern void dm_gfx_enter(void), dm_gfx_leave(void), dm_gfx_suspend(int);
static void *producer(void *unused) {
    (void)unused;
    @autoreleasepool {
        id<MTLCommandQueue> queue = [g_dev newCommandQueue]; assert(queue);
        while (!atomic_load(&finish)) {
            dm_gfx_enter();
            id<MTLCommandBuffer> buffer = [queue commandBuffer]; [buffer commit]; [buffer waitUntilCompleted];
            assert(buffer.status == MTLCommandBufferStatusCompleted);
            atomic_fetch_add(&submissions, 1); dm_gfx_leave();
        }
    }
    return NULL;
}
static void *leave_callback(void *unused) {
    (void)unused; usleep(50000); int entered = 1; satchel_link_leave(&entered); return NULL;
}
int main(void) {
    setenv("DM_PSO_ARCHIVE", "off", 1); setenv("DM_PSO_MANIFEST", "off", 1);
    setenv("DM_PSO_PREPARE", "0", 1);
    @autoreleasepool {
        assert(!satchel_mtl_session_prepare()); assert(!satchel_session_begin()); mtl_init();
        fixture_layer = [CAMetalLayer layer]; fixture_layer.device = g_dev;
        fixture_layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
        fixture_layer.drawableSize = CGSizeMake(64, 64);
        pthread_t worker; assert(!pthread_create(&worker, NULL, producer, NULL));
        for (int cycle = 0; cycle < 20; cycle++) {
            usleep(10000); mtl_present_discard(1); mtl_link_pause(1); dm_gfx_suspend(1);
            if (!cycle) {
                g_link_started = 1;
                assert(!satchel_native_create(&satchel_present_workers, NULL, link_main, NULL));
                uint64_t until = now_ns() + UINT64_C(2000000000);
                for (;;) {
                    pthread_mutex_lock(&g_pq_mu); int ready = g_link != nil;
                    if (ready) assert(g_link.paused);
                    pthread_mutex_unlock(&g_pq_mu);
                    if (ready) break;
                    assert(now_ns() < until); usleep(1000);
                }
            }
            int before = atomic_load(&submissions); usleep(10000); assert(before == atomic_load(&submissions));
            assert(atomic_load(&satchel_link_paused));
            // An already admitted callback must finish before the drain returns.
            pthread_mutex_lock(&g_pq_mu); satchel_link_callbacks++; pthread_mutex_unlock(&g_pq_mu);
            pthread_t callback; assert(!pthread_create(&callback, NULL, leave_callback, NULL));
            uint64_t start = now_ns(); mtl_drain(); assert(now_ns() - start >= 40000000);
            assert(!pthread_join(callback, NULL));
            mtl_present_discard(0); mtl_link_pause(0); dm_gfx_suspend(0);
        }
        atomic_store(&finish, 1); assert(!pthread_join(worker, NULL));
        NSError *error = nil;
        id<MTLLibrary> library = [g_dev newLibraryWithSource:@"#include <metal_stdlib>\nusing namespace metal; vertex float4 v(uint i [[vertex_id]]) { return float4(0, 0, 0, 1); } fragment float4 f() { return float4(1); }" options:nil error:&error];
        if (!library) fprintf(stderr, "Synthetic library: %s\n", error.localizedDescription.UTF8String);
        assert(library); MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];
        descriptor.vertexFunction = [library newFunctionWithName:@"v"];
        descriptor.fragmentFunction = [library newFunctionWithName:@"f"];
        descriptor.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        id<MTLRenderPipelineState> pipeline = [g_dev newRenderPipelineStateWithDescriptor:descriptor error:&error];
        if (!pipeline) fprintf(stderr, "Synthetic pipeline: %s\n", error.localizedDescription.UTF8String);
        assert(pipeline);
        __weak id released = pipeline;
        g_pp[0].used = g_pp[0].done = 1; g_pp[0].ps = pipeline; pipeline = nil;
        g_pp[1].used = g_pp[1].done = g_pp[1].demand = 1;
        g_pp[2].used = 1; g_pp[2].done = 0;
        g_pp[3].used = g_pp[3].done = 1; satchel_pp_waiters[3] = 1;
        assert(satchel_mtl_memory_pressure() == 1 && !g_pp[0].used && !released);
        assert(g_pp[1].used && g_pp[2].used && g_pp[3].used);
        g_pp[1].used = g_pp[2].used = g_pp[3].used = 0; satchel_pp_waiters[3] = 0;
        satchel_session_request_stop(); satchel_mtl_request_stop(); assert(!satchel_session_join(2000));
        assert(!satchel_mtl_producers_join(5000)); assert(!satchel_mtl_session_retire()); assert(!satchel_session_end());
        fixture_layer = nil;
    }
    puts("PASS: 20 real Metal suspend cycles, zero submissions while held, callback drain waits, safe speculative pipeline release");
}
