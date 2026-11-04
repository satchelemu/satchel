// The coordinator must retire every CPU/GPU borrower before unmapping. This
// function owns the kernel arena only, not module images or graphics objects.
void satchel_memory_request_stop(void) { satchel_native_stop(&satchel_memory_workers); }
int satchel_memory_join(unsigned timeout_ms) { return satchel_native_join(&satchel_memory_workers, timeout_ms); }
extern void nvn_dirty_stat(int *, int *, int *) __attribute__((weak_import));
int satchel_memory_session_reset(void) {
    if (!satchel_session_quiescent()) return EBUSY;
    int result = satchel_native_join(&satchel_memory_workers, 0);
    if (result) return result;
    if (nvn_dirty_stat) {
        int live = 0, peak = 0, capacity = 0;
        nvn_dirty_stat(&live, &peak, &capacity);
        if (live) return EBUSY;
    }
    pthread_mutex_lock(&satchel_memory_init_mu);
    pthread_mutex_lock(&g_map_lock);
    pthread_mutex_lock(&g_rgn_lock);
    if (g_as && munmap(g_as, g_reserve)) result = errno;
    if (!result) {
        g_as = g_heap = g_alias = g_stack = NULL;
        g_reserve = g_heap_cap = g_alias_cap = g_stack_cap = 0;
        atomic_store(&g_heap_cur, 0); atomic_store(&g_alias_mapped, 0);
        free(g_alias_bits); g_alias_bits = NULL;
        free(g_alias_exposed); g_alias_exposed = NULL;
        free(g_rgn); g_rgn = NULL; g_nrgn = g_rgn_cap = 0;
        memset(g_map, 0, sizeof g_map); g_nmap = 0;
        memset(g_seen, 0, sizeof g_seen);
        // Guest threads have exited. These TLS values belong to this coordinator.
        t_rgn_size = t_rgn_via = 0; memset(t_rgn_who, 0, sizeof t_rgn_who);
    }
    pthread_mutex_unlock(&g_rgn_lock);
    pthread_mutex_unlock(&g_map_lock);
    pthread_mutex_unlock(&satchel_memory_init_mu);
    if (!result) result = satchel_native_reset(&satchel_memory_workers);
    return result;
}
