// Included only by the generated nvn_mtl.m overlay, after framework imports.
// Track scheduling, not completion fences alone. A GPU fence may signal before
// the callback has finished writing guest memory or releasing native objects.
#include "SessionNativeWorkers.h"
#include "SessionBoundary.h"
static SatchelNativeWorkers satchel_present_workers = SATCHEL_NATIVE_WORKERS_INIT;
static SatchelNativeWorkers satchel_encode_workers = SATCHEL_NATIVE_WORKERS_INIT;
static SatchelNativeWorkers satchel_graphics_drain = SATCHEL_NATIVE_WORKERS_INIT;
static _Atomic int satchel_present_stop, satchel_encode_stop;
static int satchel_present_started, satchel_ubc_started, satchel_drain_started;

static dispatch_group_t satchel_mtl_jobs(void) {
    static dispatch_group_t group;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ group = dispatch_group_create(); });
    return group;
}
static dispatch_block_t satchel_mtl_job(dispatch_block_t body) {
    dispatch_group_t group = satchel_mtl_jobs();
    dispatch_group_enter(group);
    return ^{ @autoreleasepool { @try { body(); } @finally { dispatch_group_leave(group); } } };
}
static void satchel_mtl_async(dispatch_queue_t queue, dispatch_block_t body) {
    dispatch_async(queue, satchel_mtl_job(body));
}
static void satchel_mtl_after(dispatch_time_t when, dispatch_queue_t queue, dispatch_block_t body) {
    dispatch_after(when, queue, satchel_mtl_job(body));
}
static void satchel_mtl_group_async(dispatch_group_t group, dispatch_queue_t queue, dispatch_block_t body) {
    dispatch_group_async(group, queue, satchel_mtl_job(body));
}
static void satchel_mtl_group_notify(dispatch_group_t group, dispatch_queue_t queue, dispatch_block_t body) {
    dispatch_group_notify(group, queue, satchel_mtl_job(body));
}
static MTLCommandBufferHandler satchel_mtl_completed(MTLCommandBufferHandler body) {
    dispatch_group_t group = satchel_mtl_jobs(); dispatch_group_enter(group);
    return ^(id<MTLCommandBuffer> value) {
        @try { body(value); } @finally { dispatch_group_leave(group); }
    };
}
static MTL4CommitFeedbackHandler satchel_mtl_feedback(MTL4CommitFeedbackHandler body) {
    dispatch_group_t group = satchel_mtl_jobs(); dispatch_group_enter(group);
    return ^(id<MTL4CommitFeedback> value) {
        @try { body(value); } @finally { dispatch_group_leave(group); }
    };
}
static MTLSharedEventNotificationBlock satchel_mtl_event(MTLSharedEventNotificationBlock body) {
    dispatch_group_t group = satchel_mtl_jobs(); dispatch_group_enter(group);
    return ^(id<MTLSharedEvent> event, uint64_t value) {
        @try { body(event, value); } @finally { dispatch_group_leave(group); }
    };
}
// Includes such as nvn_geometry.inc also inherit this ownership wrapper.
#define dispatch_async satchel_mtl_async
#define dispatch_after satchel_mtl_after
#define dispatch_group_async satchel_mtl_group_async
#define dispatch_group_notify satchel_mtl_group_notify
