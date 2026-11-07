// Exact first use joins a bounded worker. Descriptors and functions remain alive
// until completion; no stand-in pipeline or omitted draw is introduced here.
// Bounded mode (DM_PSO_AHEAD=1) only: a build the submitting thread waits for takes the next
// free lane before speculative and background builds. Other modes keep first-come order.
static pthread_t satchel_submitter;
static int satchel_lanes_ahead(void) { const char *e = getenv("DM_PSO_AHEAD"); return e && !strcmp(e, "1"); }
// Measurement knobs (DM_PSO_LANES 1 to 8, default 2; DM_PSO_SPECULATIVE 0 to 128, 0 keeps each mode's
// default). Unset, every mode schedules exactly as before. They change when a pipeline is built, never which.
static int satchel_env_int(const char *name, int fallback, int low, int high) {
    const char *e = getenv(name); if (!e || !*e) return fallback;
    char *end; long v = strtol(e, &end, 10); return *end || v < low || v > high ? fallback : (int)v;
}
static int satchel_pipeline_lanes(void) { static int n; if (!n) n = satchel_env_int("DM_PSO_LANES", 2, 1, 8); return n; }
__attribute__((unused)) static int satchel_speculative_setting(void) { static int n = -1; if (n < 0) n = satchel_env_int("DM_PSO_SPECULATIVE", 0, 0, 128); return n; }
// With a raised limit the submitting thread takes the next free lane before speculation.
static int satchel_lanes_ordered(void) { return satchel_lanes_ahead() || satchel_speculative_setting(); }
// Speculative builds wait in admission order on serial feeders instead of the global queue: a queued
// build stays queued until a lane is near, so its draw can take it and build it at once (pso_pend_take).
__attribute__((unused)) static dispatch_queue_t satchel_speculative_queue(void) {
    if (!satchel_speculative_setting()) return dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0);
    static dispatch_queue_t feeders[8]; static dispatch_once_t once; static _Atomic unsigned next;
    dispatch_once(&once, ^{
        for (int i = 0; i < satchel_pipeline_lanes(); i++)
            feeders[i] = dispatch_queue_create("Satchel.pipeline-ahead", dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INITIATED, 0));
    });
    return feeders[atomic_fetch_add(&next, 1) % (unsigned)satchel_pipeline_lanes()];
}
static void satchel_pipeline_work(dispatch_block_t work) {
    static dispatch_queue_t lanes[8]; static dispatch_once_t once;
    static char key; static unsigned occupied, full;
    static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    static pthread_cond_t available = PTHREAD_COND_INITIALIZER;
    dispatch_once(&once, ^{
        int count = satchel_pipeline_lanes(); full = (1u << count) - 1;
        for (int i = 0; i < count; i++) {
            lanes[i] = dispatch_queue_create("Satchel.pipeline", dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INITIATED, 0));
            dispatch_queue_set_specific(lanes[i], &key, &key, NULL);
        }
    });
    if (dispatch_get_specific(&key)) { work(); return; }
    // Reserve a free lane, so a long compile cannot strand work behind itself
    // while another lane is idle. Nested work already owns its lane.
    static unsigned urgent_waiting;
    int ordered = satchel_lanes_ordered(), urgent = ordered && satchel_submitter && pthread_equal(pthread_self(), satchel_submitter);
    pthread_mutex_lock(&mutex);
    if (urgent) urgent_waiting++;
    while (occupied == full || (ordered && !urgent && urgent_waiting)) pthread_cond_wait(&available, &mutex);
    if (urgent) urgent_waiting--;
    unsigned lane = (unsigned)__builtin_ctz(~occupied);
    occupied |= 1u << lane;
    pthread_mutex_unlock(&mutex);
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    dispatch_async(lanes[lane], ^{
        work();
        pthread_mutex_lock(&mutex); occupied &= ~(1u << lane);
        if (ordered) pthread_cond_broadcast(&available); else pthread_cond_signal(&available);
        pthread_mutex_unlock(&mutex);
        dispatch_semaphore_signal(done);
    });
    dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
}
// Cache ownership and duplicate-request waits stay outside the compiler lanes.
// Only the Metal call occupies a lane; the caller joins before using its result.
#define SATCHEL_PIPELINE_RESULT(type, expr) ({ \
    __block type result; __block NSError *failure; \
    uint64_t start = satchel_shader_now(); \
    satchel_pipeline_work(^{ @autoreleasepool { result = SATCHEL_SHADER_TIME("pipeline-create", (expr)); } }); \
    satchel_shader_event("pipeline-use", start); \
    if (error) *error = failure; \
    result; })
static id<MTLComputePipelineState> satchel_compute_build(id<MTLDevice> device, id<MTLFunction> function, NSError **error) {
    return SATCHEL_PIPELINE_RESULT(id<MTLComputePipelineState>, [device newComputePipelineStateWithFunction:function error:&failure]);
}
static id<MTLRenderPipelineState> satchel_render_device(id<MTLDevice> device, MTLRenderPipelineDescriptor *descriptor, NSError **error) {
    return SATCHEL_PIPELINE_RESULT(id<MTLRenderPipelineState>, [device newRenderPipelineStateWithDescriptor:descriptor error:&failure]);
}
static id<MTLRenderPipelineState> satchel_render_compile(id<MTL4Compiler> compiler, MTL4RenderPipelineDescriptor *descriptor,
                                                  MTL4CompilerTaskOptions *options, NSError **error) {
    return SATCHEL_PIPELINE_RESULT(id<MTLRenderPipelineState>, [compiler newRenderPipelineStateWithDescriptor:descriptor compilerTaskOptions:options error:&failure]);
}
static id<MTLRenderPipelineState> satchel_specialize_locked(id<MTL4Compiler> compiler, MTL4RenderPipelineDescriptor *descriptor,
                                                         id<MTLRenderPipelineState> base, NSError **error) {
    // Concurrent specialization on the same device loses an AGX dynamic-library
    // allocation in the Mac reproducer, even after every public owner retires.
    // Serialize this call inside a worker, so callers never hold this lock while
    // waiting for a compiler lane. Other pipeline compilation stays concurrent.
    static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&mutex);
    @try {
        return [compiler newRenderPipelineStateBySpecializationWithDescriptor:descriptor pipeline:base error:error];
    } @finally {
        pthread_mutex_unlock(&mutex);
    }
}
static id<MTLRenderPipelineState> satchel_render_specialize(id<MTL4Compiler> compiler, MTL4RenderPipelineDescriptor *descriptor,
                                                     id<MTLRenderPipelineState> base, NSError **error) {
    return SATCHEL_PIPELINE_RESULT(id<MTLRenderPipelineState>, satchel_specialize_locked(compiler, descriptor, base, &failure));
}
