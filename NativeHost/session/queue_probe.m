#import <Metal/Metal.h>
#include <stdio.h>
// Metal owns the default feedback queue. No guest data is used by this fixture.
int main(void) {
    for (int i = 0; i < 5; i++) { @autoreleasepool {
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        dispatch_queue_t feedback = dispatch_queue_create("satchel.session.queue-proof", DISPATCH_QUEUE_SERIAL);
        MTL4CommandQueueDescriptor *descriptor = [MTL4CommandQueueDescriptor new];
        descriptor.feedbackQueue = nil;
        id<MTL4CommandQueue> queue = [device newMTL4CommandQueueWithDescriptor:descriptor error:nil];
        if (!queue) return 2;
        fprintf(stdout, "queue-created %d\n", i); fflush(stdout);
        queue = nil;
        dispatch_sync(feedback, ^{});
        fprintf(stdout, "queue-drained %d\n", i); fflush(stdout);
        feedback = nil;
        fprintf(stdout, "queue-released %d\n", i); fflush(stdout);
    } }
}
