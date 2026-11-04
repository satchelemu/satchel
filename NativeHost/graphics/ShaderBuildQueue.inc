// Exact first use joins a bounded worker. Descriptors and functions remain alive
// until completion; no stand-in pipeline or omitted draw is introduced here.
static void satchel_pipeline_work(dispatch_block_t work) {
    static dispatch_queue_t lanes[2]; static dispatch_once_t once;
    static char key; static unsigned occupied;
    static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    static pthread_cond_t available = PTHREAD_COND_INITIALIZER;
    dispatch_once(&once, ^{
        for (int i = 0; i < 2; i++) {
            lanes[i] = dispatch_queue_create("Satchel.pipeline", dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_USER_INITIATED, 0));
            dispatch_queue_set_specific(lanes[i], &key, &key, NULL);
        }
    });
    if (dispatch_get_specific(&key)) { work(); return; }
    // Reserve a free lane, so a long compile cannot strand work behind itself
    // while the other lane is idle. Nested work already owns its lane.
    pthread_mutex_lock(&mutex);
    while (occupied == 3) pthread_cond_wait(&available, &mutex);
    unsigned lane = (occupied & 1) ? 1 : 0;
    occupied |= 1u << lane;
    pthread_mutex_unlock(&mutex);
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    dispatch_async(lanes[lane], ^{
        work();
        pthread_mutex_lock(&mutex); occupied &= ~(1u << lane);
        pthread_cond_signal(&available); pthread_mutex_unlock(&mutex);
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
