// Caller holds g_smu. A finalized descriptor may be replayed until its pool is
// finalized. Past that boundary its ghost cannot name valid guest storage.
// GPU users keep the existing deferred ownership until their fence completes.
static void satchel_ghost_drop_pool(const void *pool) {
    for (uint32_t i = 0; i < satchel_ghost_pool.cap;) {
        if (satchel_ghost_pool.vals[i] != pool) { i++; continue; }
        uint64_t serial; memcpy(&serial, satchel_ghost_pool.keys + (size_t)i * 8, 8);
        id object = hm_get_id(&g_ghost, &serial);
        if (object) obj_drop(object);
        hm_del_id(&g_ghost, &serial);
        nvn_hm_del(&satchel_ghost_pool, &serial);
        g_ser_gen++;
    }
}
