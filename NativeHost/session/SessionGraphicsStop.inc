// Request phase: release guest presentation waits, but keep encoding and copy
// consumers alive until every guest submission and presentation thread returns.
void satchel_mtl_request_stop(void) {
    atomic_store(&satchel_present_stop, 1);
    atomic_store(&g_present_discard, 1);
    satchel_native_stop(&satchel_present_workers);
    pthread_mutex_lock(&g_pq_mu);
    pthread_cond_broadcast(&g_pq_cv);
    pthread_mutex_unlock(&g_pq_mu);
    pthread_mutex_lock(&g_link_mu);
    pthread_cond_broadcast(&g_link_cv);
    pthread_mutex_unlock(&g_link_mu);
}

static void *satchel_mtl_drain_worker(void *unused) {
    (void)unused;
    @autoreleasepool {
        rec_drain();
        ubc_wait(ubc_head());
        // A presenter can be waiting for a snapshot's last unpublished record.
        // Publish before joining it. This worker, not the caller, owns the wait.
        int joined;
        do { joined = satchel_native_join(&satchel_present_workers, 1000); } while (joined == ETIMEDOUT);
        if (joined) abort();
        rec_drain();
        // No first-use Metal initialization when the session never rendered.
        if (g_dev) mtl_drain();
        dispatch_group_wait(satchel_mtl_jobs(), DISPATCH_TIME_FOREVER);
        arch_flush_wait();
        dispatch_group_wait(satchel_mtl_jobs(), DISPATCH_TIME_FOREVER);
        atomic_store(&satchel_encode_stop, 1);
        satchel_native_stop(&satchel_encode_workers);
        pthread_mutex_lock(&g_ubc_mu); pthread_cond_broadcast(&g_ubc_cv); pthread_mutex_unlock(&g_ubc_mu);
        pthread_mutex_lock(&g_rb_mu); pthread_cond_broadcast(&g_rb_work); pthread_mutex_unlock(&g_rb_mu);
    }
    return NULL;
}

// A timeout retains every object and mapping. The native drain may still be in
// a driver wait or compiler call. Retry joins that same drain, never a new one.
int satchel_mtl_producers_join(unsigned timeout_ms) {
    if (!satchel_session_quiescent() || !atomic_load(&satchel_present_stop)) return EBUSY;
    int result;
    if (!satchel_drain_started) {
        result = satchel_native_create(&satchel_graphics_drain, NULL, satchel_mtl_drain_worker, NULL);
        if (result) return result;
        satchel_drain_started = 1; satchel_native_stop(&satchel_graphics_drain);
    }
    result = satchel_native_join(&satchel_graphics_drain, timeout_ms);
    if (result) return result;
    return satchel_native_join(&satchel_encode_workers, timeout_ms);
}
