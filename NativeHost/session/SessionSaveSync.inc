// Satchel session stop for cloud save sync (appended to the engine's save_sync.c by
// save_sync_overlay.py). Runs after the guest has joined, so no Commit can arrive, and
// before the file system state is reset, because a send clones the committed save through
// hle_fs.c. A send still unfinished at the deadline fails this stage with ETIMEDOUT; the
// stop is retried and the sender keeps its state. A save that did not reach the cloud is
// sent at the next session's first mount (save_sync.h: this device's changed save goes out).
#ifndef SATCHEL_SAVE_SYNC_STOP_MS
#define SATCHEL_SAVE_SYNC_STOP_MS 20000
#endif
int satchel_save_sync_session_stop(void) {
    if (atomic_load(&g_on)) {
        if (!save_sync_flush(SATCHEL_SAVE_SYNC_STOP_MS))
            SLOG("the last save did not reach the cloud before the game stopped; it goes at the next start");
        struct timespec ts; deadline(SATCHEL_SAVE_SYNC_STOP_MS, &ts);
        pthread_mutex_lock(&g_mu);
        satchel_sync_stop = 1; pthread_cond_broadcast(&g_cv);
        while (!satchel_sync_exited && pthread_cond_timedwait(&g_cv, &g_mu, &ts) != ETIMEDOUT) {}
        int exited = satchel_sync_exited;
        pthread_mutex_unlock(&g_mu);
        if (!exited) { SRED("the sender is still sending; stopping again later"); return ETIMEDOUT; }
        int r = pthread_join(satchel_sync_thread, NULL);
        if (r) return r;
        atomic_store(&g_on, 0);
    }
    pthread_mutex_lock(&g_mount_mu); g_mounted = 0; pthread_mutex_unlock(&g_mount_mu);
    pthread_mutex_lock(&g_mu);
    g_want = g_busy = g_failed = 0; g_commits = g_sent = 0;
    satchel_sync_stop = satchel_sync_exited = 0;
    g_save[0] = g_title[0] = g_cloud[0] = 0;
    pthread_mutex_unlock(&g_mu);
    return 0;
}
