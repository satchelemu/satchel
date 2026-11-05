// Exact descriptors use the existing pending queue. A ready pulling function may
// supply the equivalent fallback; no shader lookup or mutable guest input escapes
// to these workers. Shader translation and unsupported draws still require joins.
static int satchel_skip_context, satchel_skipped;
static int satchel_skip_can_draw(void);
static _Atomic uint64_t satchel_skips_current, satchel_skips_last;
uint64_t satchel_pipeline_skipped_draws(void) { return atomic_load(&satchel_skips_last); }
static void satchel_pipeline_end_frame(void) {
    uint64_t count = atomic_exchange(&satchel_skips_current, 0);
    atomic_store(&satchel_skips_last, count);
    const char *path = getenv("DM_PSO_SKIP_STATS");
    if (path) {
        char row[96]; int n = snprintf(row, sizeof row, "%llu\t%llu\n",
            (unsigned long long)atomic_load(&g_cur_frame), (unsigned long long)count);
        int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd >= 0) { if (n > 0 && n < sizeof row) (void)write(fd, row, n); close(fd); }
    }
}
static int satchel_skip_on(void) {
    const char *value = getenv("DM_PSO_SKIP");
    return value && !strcmp(value, "1");
}
static int satchel_async_on(void) {
    const char *value = getenv("DM_PSO_ASYNC");
    return value && !strcmp(value, "1");
}
static uint64_t satchel_pipeline_budget_ns(void) {
    const char *value = getenv("DM_PSO_BUDGET_MS");
    if (!value || !*value) return 4000000;
    char *end = NULL; double ms = strtod(value, &end);
    if (!end || *end || !isfinite(ms) || ms < 0 || ms > 16) return 4000000;
    return (uint64_t)(ms * 1000000);
}
@interface SatchelFallbackBuild : NSObject {
@public
    pso_key key;
    id<MTLRenderPipelineState> pipeline;
    NvShader *vertex, *fragment;
    int done;
}
@end
@implementation SatchelFallbackBuild @end
// Submit-thread ownership of the dictionary; completion fields use g_pp_mu.
// Completed results move into the existing hybrid cache on the next request.
static NSMutableDictionary<NSData *, SatchelFallbackBuild *> *satchel_fallbacks;
static uint64_t satchel_async_frame = UINT64_MAX, satchel_async_remaining;
static void satchel_async_reset(void) {
    // Session retirement calls this only after the tracked compiler jobs join.
    satchel_fallbacks = nil; satchel_async_frame = UINT64_MAX; satchel_async_remaining = 0;
    satchel_skip_context = satchel_skipped = 0;
    atomic_store(&satchel_skips_current, 0); atomic_store(&satchel_skips_last, 0);
}
static dispatch_queue_t satchel_fallback_lane(void) {
    static dispatch_queue_t lanes[2]; static dispatch_once_t once; static unsigned next;
    dispatch_once(&once, ^{
        for (int i = 0; i < 2; i++) lanes[i] = dispatch_queue_create("Satchel.pipeline-fallback",
            dispatch_queue_attr_make_with_qos_class(DISPATCH_QUEUE_SERIAL, QOS_CLASS_UTILITY, 0));
    });
    return lanes[next++ % 2];
}
static void satchel_fallback_collect(void) {
    for (NSData *identity in satchel_fallbacks.allKeys) {
        SatchelFallbackBuild *job = satchel_fallbacks[identity];
        pthread_mutex_lock(&g_pp_mu);
        int done = job->done; id<MTLRenderPipelineState> pipeline = job->pipeline;
        pthread_mutex_unlock(&g_pp_mu);
        if (!done) continue;
        if (pipeline) { hm_put_id(&g_pso_hy, &job->key, pipeline); g_hy_made++; }
        [satchel_fallbacks removeObjectForKey:identity];
    }
}
static void satchel_fallback_start(const pso_key *key, NvShader *vs, NvShader *fs,
                                   MTLRenderPipelineDescriptor *exact) {
    if (!vs->has_pull || vs->layer_out || !exact.rasterizationEnabled) return;
    if (hm_get_id(&g_pso_hy, key)) return;
    if (!satchel_fallbacks) satchel_fallbacks = [NSMutableDictionary dictionary];
    NSData *identity = [NSData dataWithBytes:key length:sizeof *key];
    if (satchel_fallbacks[identity] || satchel_fallbacks.count >= 64) return;
    // Never join pull_load here: that can create a Metal library and must not
    // consume a supposedly bounded pipeline wait. Retain the exact revision.
    if (!os_unfair_lock_trylock(&g_pull_lock)) return;
    id<MTLFunction> function = vs->pfn_state == 1 ? vs->pfn : nil;
    os_unfair_lock_unlock(&g_pull_lock);
    if (!function) return;
    MTLRenderPipelineDescriptor *descriptor = [exact copy];
    descriptor.vertexFunction = function;
    descriptor.vertexDescriptor = [MTLVertexDescriptor vertexDescriptor];
    SatchelFallbackBuild *job = [SatchelFallbackBuild new];
    job->key = *key; job->vertex = vs; job->fragment = fs;
    satchel_fallbacks[identity] = job;
    dispatch_async(satchel_fallback_lane(), ^{ @autoreleasepool {
        NSError *error = nil;
        id<MTLRenderPipelineState> pipeline = pso_build(descriptor, &error);
        pthread_mutex_lock(&g_pp_mu);
        job->pipeline = pipeline; job->done = 1;
        pthread_cond_broadcast(&g_pp_cv); pthread_mutex_unlock(&g_pp_mu);
    } });
}
// The optional wait budget is shared by all requests in a presented frame.
// This is a scheduling deadline, not a real-time OS guarantee. No draw is dropped
// in async mode. Skip mode additionally requires a complete terminal-pass proof.
static id<MTLRenderPipelineState> satchel_async_take(const pso_key *key, NvShader *vs, NvShader *fs,
                                                   int *took, int *pulling) {
    satchel_skipped = 0;
    uint64_t current = atomic_load(&g_cur_frame);
    if (satchel_async_frame != current) { satchel_async_frame = current; satchel_async_remaining = satchel_pipeline_budget_ns(); }
    uint64_t start = satchel_shader_now(), deadline = start + satchel_async_remaining;
    satchel_fallback_collect();
    id<MTLRenderPipelineState> pipeline = nil;
    int state = pso_pend_try(key, &pipeline);
    if (state == 2) { *took = 1; return pipeline; }
    if (!state) {
        // Capture the descriptor while the submit thread owns renderer state.
        MTLRenderPipelineDescriptor *descriptor = pso_desc(key, vs, fs, nil, 0);
        pthread_mutex_lock(&g_pp_mu); uint32_t gen = 0;
        int slot = pso_pend_slot(key, 0, &gen);
        pthread_mutex_unlock(&g_pp_mu);
        if (slot >= 0) pso_pend_start(slot, gen, descriptor, pp_bg_queue());
        else { satchel_shader_event("pipeline-capacity-wait", start); return nil; }
        satchel_fallback_start(key, vs, fs, descriptor);
    }
    for (;;) {
        state = pso_pend_try(key, &pipeline);
        if (state == 2) { *took = 1; break; }
        satchel_fallback_collect();
        pipeline = hm_get_id(&g_pso_hy, key);
        if (pipeline) { *pulling = 1; break; }
        uint64_t now = satchel_shader_now();
        if (!state || now >= deadline) break;
        pthread_mutex_lock(&g_pp_mu);
        int at = pso_pend_find(key);
        // Recheck under the lock to avoid losing completion before waiting.
        if (at >= 0 && !g_pp[at].done) {
            now = satchel_shader_now();
            if (now < deadline) {
                uint64_t left = deadline - now;
                struct timespec relative = {(time_t)(left / 1000000000), (long)(left % 1000000000)};
                pthread_cond_timedwait_relative_np(&g_pp_cv, &g_pp_mu, &relative);
            }
        }
        pthread_mutex_unlock(&g_pp_mu);
    }
    uint64_t elapsed = satchel_shader_now() - start;
    satchel_async_remaining = elapsed >= satchel_async_remaining ? 0 : satchel_async_remaining - elapsed;
    // Only a live unfinished job can trigger omission. Failed compilation and
    // queue exhaustion keep the existing error and required-wait paths.
    if (!pipeline && state == 1 && satchel_skip_on() && satchel_skip_context && satchel_skip_can_draw()) {
        satchel_skipped = 1;
        atomic_fetch_add(&satchel_skips_current, 1);
        satchel_shader_event("pipeline-skipped", start);
    } else satchel_shader_event(*pulling ? "pipeline-fallback" : *took ? "pipeline-ready" : "pipeline-required-wait", start);
    return pipeline;
}
