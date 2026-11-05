#import <Metal/Metal.h>
#include <pthread.h>
#include <stdatomic.h>
#include <assert.h>
#include <stdio.h>
static unsigned summaries;
#define MLOG(...) do { summaries++; printf(__VA_ARGS__); puts(""); } while (0)
#include "FrameTiming.inc"

int main(void) { @autoreleasepool {
    unsetenv("DM_SATCHEL_FRAME_TIMING"); satchel_timing_reset();
    satchel_timing_begin(1); satchel_timing_done(1, 1, 2, 0, 1);
    satchel_timing_cpu_frame(1, 1, 2); satchel_timing_summary(300, 16.667);
    assert(!satchel_timing_frames[1].used && satchel_timing_window.count == 0 && summaries == 0);
    setenv("DM_SATCHEL_FRAME_TIMING", "1", 1); satchel_timing_reset();
    id<MTLDevice> device = MTLCreateSystemDefaultDevice(); assert(device);
    id<MTLCommandQueue> queue = [device newCommandQueue];
    id<MTL4CommandQueue> queue4 = [device newMTL4CommandQueue]; assert(queue4);
    id<MTL4CommandAllocator> allocator = [device newCommandAllocator];
    id<MTL4CommandBuffer> buffer4 = [device newCommandBuffer];
    id<MTLBuffer> buffer = [device newBufferWithLength:1048576 options:MTLResourceStorageModeShared];
    id<MTLResidencySet> residency = [device newResidencySetWithDescriptor:[MTLResidencySetDescriptor new] error:nil];
    [residency addAllocation:buffer]; [residency commit]; [queue4 addResidencySet:residency];
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    for (uint64_t frame = 0; frame < 48; frame++) {
        satchel_timing_begin(frame); satchel_timing_begin(frame);
        id<MTLCommandBuffer> cb = [queue commandBuffer];
        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
        [blit fillBuffer:buffer range:NSMakeRange(0, buffer.length) value:7]; [blit endEncoding];
        [cb addCompletedHandler:^(id<MTLCommandBuffer> result) {
            satchel_timing_done(frame, result.GPUStartTime, result.GPUEndTime, result.status != MTLCommandBufferStatusCompleted, 0);
            dispatch_semaphore_signal(done);
        }];
        [cb commit]; assert(!dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC)));
        [allocator reset]; [buffer4 beginCommandBufferWithAllocator:allocator];
        id<MTL4ComputeCommandEncoder> compute = [buffer4 computeCommandEncoder];
        [compute fillBuffer:buffer range:NSMakeRange(0, buffer.length) value:9]; [compute endEncoding]; [buffer4 endCommandBuffer];
        MTL4CommitOptions *options = [MTL4CommitOptions new];
        [options addFeedbackHandler:^(id<MTL4CommitFeedback> result) {
            satchel_timing_done(frame, result.GPUStartTime, result.GPUEndTime, result.error != nil, 1);
            dispatch_semaphore_signal(done);
        }];
        [queue4 commit:&buffer4 count:1 options:options];
        assert(!dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 10 * NSEC_PER_SEC)));
        satchel_timing_cpu_frame(frame, .1, 16.667);
    }
    for (uint64_t f = 48; f < 56; f++) satchel_timing_cpu_frame(f, .1, 16.667);
    assert(satchel_timing_window.count == 48 && satchel_timing_window.invalid == 0);
    assert(satchel_timing_window.gpu[0] > 0);
    satchel_timing_summary(56, 16.667);
    assert(satchel_timing_window.count == 0 && summaries == 1);
    satchel_timing_reset(); assert(!satchel_timing_frames[0].used);
    unsetenv("DM_SATCHEL_FRAME_TIMING"); satchel_timing_reset(); assert(!satchel_timing_on());
    puts("PASS: real Metal and Metal 4 feedback, paired summary, off path and session reset (synthetic workload)");
} }
