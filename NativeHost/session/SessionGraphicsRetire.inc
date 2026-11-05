// All guest calls, producers, GPU feedback and archive jobs must be joined.
// Destructors run while guest mappings still exist. No callback publishes writes
// after this point. Initial POD values are restored only after typed retirement.
extern void satchel_retire_checkpoint(unsigned);
#define SATCHEL_RETIRE_MARK(n) do { satchel_retire_checkpoint(n); } while (0)
#include "SessionGraphicsState.inc"
static void satchel_hm_retire(nvn_hmap *map, void (*release)(void *)) {
    nvn_hm_clear(map, release);
    free(map->keys); free(map->vals);
    map->keys = NULL; map->vals = NULL; map->cap = 0;
}
static void satchel_want_retire(want_list *list) {
    for (size_t i = 0; i < list->n; i++) free(list->keys[i]);
    free(list->keys); list->keys = NULL; list->n = list->cap = list->loaded = 0;
}
static void satchel_post_discard(post_op *ops, int count) {
    for (int i = 0; i < count; i++) {
        if (ops[i].kind == 3 && ops[i].vis) {
            vis_list *list = ops[i].vis;
            for (int j = 0; j < list->n; j++) CFRelease(list->s[j].buf);
            free(list);
        } else if (ops[i].kind == 4 && ops[i].vis) {
            mirror_rb *readback = ops[i].vis;
            if (readback->buf) CFRelease(readback->buf);
            if (readback->src) CFRelease(readback->src);
            free(readback);
        }
    }
    free(ops);
}
int satchel_mtl_session_prepare(void) {
    // Binary replay capture has a distinct owner, outside the app's shader capture.
    if (getenv("DM_NVN_CAPTURE")) return ENOTSUP;
    satchel_mtl_capture_state();
    return 0;
}
static NSHashTable *satchel_borrowers;
static int satchel_retired_owners;
int satchel_mtl_session_retire(void) {
    if (!satchel_session_quiescent() || !satchel_drain_started) return EBUSY;
    int result = satchel_mtl_producers_join(0); if (result) return result;
    if (dispatch_group_wait(satchel_mtl_jobs(), DISPATCH_TIME_NOW)) return EBUSY;
    if (g_rb_head || g_rb_busy || g_pq_inflight) return EBUSY;
    // A submitted post record must have published before the drain completed.
    for (unsigned i = 0; i < sizeof g_postr / sizeof g_postr[0]; i++)
        if (g_postr[i].ops || g_postr[i].sc) return EBUSY;
    if (!satchel_retired_owners) {
    satchel_borrowers = [NSHashTable weakObjectsHashTable];
    @autoreleasepool { for (id buffer in g_keep) [satchel_borrowers addObject:buffer]; }
    SATCHEL_RETIRE_MARK(1);
    @autoreleasepool {
        // Ending an unsubmitted encoder only closes encoding, never commits it.
        if (g_rx_enc) [(id<MTL4CommandEncoder>)g_rx_enc endEncoding];
        else { if (g_enc) [g_enc endEncoding]; if (g_blit) [g_blit endEncoding]; }
        g_rx_enc = nil; g_enc = nil; g_blit = nil;
    SATCHEL_RETIRE_MARK(2);
        satchel_post_discard(g_post, g_npost); g_post = NULL; g_npost = 0;
        seg_clk_put(g_sclk); g_sclk = NULL;
    SATCHEL_RETIRE_MARK(3);
        for (int i = 0; i < g_nrtm; i++) if (g_rtm[i].tex) CFRelease(g_rtm[i].tex);
        for (int i = 0; i < g_nabs; i++) abs_free(i);
    SATCHEL_RETIRE_MARK(4);
        for (int i = 0; i < g_ntreg; i++) treg_drop_views(&g_treg[i]);
        for (int i = 0; i < g_nsreg; i++) {
            if (g_sview[i]) CFRelease(g_sview[i]);
            if (g_sview_w[i]) CFRelease(g_sview_w[i]);
        }
    SATCHEL_RETIRE_MARK(5);
        vis_clear();
        for (int i = 0; i < 4; i++) defer_release_one(&g_defer_slots[i]);
        for (int i = 0; i < g_nfzs; i++) free(g_fzs[i].S_);
    SATCHEL_RETIRE_MARK(6);
        satchel_hm_retire(&g_vc, vc_set_free);
    SATCHEL_RETIRE_MARK(7);
        nvn_hmap *objects[] = {&g_by_serial, &g_alias, &g_ghost, &g_pso, &g_dss,
            &g_pbase, &g_pso_hy, &g_cps, &g_pso_mesh, &g_pso_mtk};
        for (unsigned i = 0; i < sizeof objects / sizeof objects[0]; i++) satchel_hm_retire(objects[i], hm_rel);
    SATCHEL_RETIRE_MARK(8);
        nvn_hmap *allocations[] = {&g_image_desc, &g_vbuf_unaligned, &g_ir, &g_geo_ix};
        for (unsigned i = 0; i < sizeof allocations / sizeof allocations[0]; i++) satchel_hm_retire(allocations[i], free);
    SATCHEL_RETIRE_MARK(9);
        nvn_hmap *indices[] = {&g_alias_seen, &g_tex_live, &g_cpu_tex, &g_cpu_tex_retired,
            &g_treg_serial, &g_pso_fail, &g_pbase_pending, &g_cpu_unc};
        for (unsigned i = 0; i < sizeof indices / sizeof indices[0]; i++) satchel_hm_retire(indices[i], NULL);
    SATCHEL_RETIRE_MARK(10);
        if (g_alias_set) CFRelease(g_alias_set); g_alias_set = NULL;
        while (g_queue_states) { queue_state *next = g_queue_states->next; free(g_queue_states); g_queue_states = next; }
    SATCHEL_RETIRE_MARK(11);
        rec_blk *blocks[] = {g_rb_cur, g_rb_free};
        for (unsigned i = 0; i < 2; i++) for (rec_blk *b = blocks[i], *next; b; b = next) {
            next = b->next;
            for (int j = 0; j < b->n; j++) for (unsigned k = 0; k < b->r[j].nobj; k++)
                if (b->r[j].a[k]) CFRelease((CFTypeRef)(uintptr_t)b->r[j].a[k]);
            free(b);
        }
    SATCHEL_RETIRE_MARK(12);
#define SATCHEL_FREE(value) do { free(value); value = NULL; } while (0)
        SATCHEL_FREE(g_pools); SATCHEL_FREE(g_pidx); SATCHEL_FREE(g_vs); SATCHEL_FREE(g_up_blits);
        SATCHEL_FREE(g_pg_pass); SATCHEL_FREE(g_treg); SATCHEL_FREE(g_treg_transient);
        SATCHEL_FREE(g_sreg); SATCHEL_FREE(g_sreg_ok); SATCHEL_FREE(g_sview); SATCHEL_FREE(g_sview_w);
        SATCHEL_FREE(g_srid); SATCHEL_FREE(g_srid_w); SATCHEL_FREE(g_sbk);
        SATCHEL_FREE(g_sdx); SATCHEL_FREE(g_sdx_tmp); SATCHEL_FREE(g_sdx_op);
        SATCHEL_FREE(g_rtx_seq); SATCHEL_FREE(g_drx_seq);
        // The f8a5618d pin: placement sparse tile tables and the inline uniform update pages.
        SATCHEL_FREE(g_sp_free); SATCHEL_FREE(g_sp_late); SATCHEL_FREE(g_sp_tl); SATCHEL_FREE(g_sp_ph); SATCHEL_FREE(g_um);
#undef SATCHEL_FREE
    SATCHEL_RETIRE_MARK(13);
        if (g_pg_f) fclose(g_pg_f);
        if (g_pso_record) fclose(g_pso_record);
        if (g_pso_user) fclose(g_pso_user);
        satchel_want_retire(&g_bf_want); satchel_want_retire(&g_dual_want);
        // ARC assignments release textures, heaps, pipelines, dictionaries,
        // allocators and residency sets. These are never zeroed with memcpy.
    SATCHEL_RETIRE_MARK(14);
        satchel_mtl_release_strong();
    }
    SATCHEL_RETIRE_MARK(15);
    @autoreleasepool { result = satchel_state_restore(SATCHEL_STATE_GRAPHICS); }
    if (result) return result;
    satchel_retired_owners = 1;
    }
    @autoreleasepool { if (satchel_borrowers.allObjects.count || atomic_load(&g_image_heap_n)) return EBUSY; }
    satchel_borrowers = nil;
    atomic_fetch_add(&g_pool_gen, 1); atomic_fetch_add(&g_ser_gen, 1); atomic_fetch_add(&g_reg_gen, 1);
    satchel_present_started = satchel_ubc_started = satchel_drain_started = 0;
    satchel_pso_ready = 0;
    if ((result = satchel_native_reset(&satchel_present_workers)) || (result = satchel_native_reset(&satchel_encode_workers)) ||
        (result = satchel_native_reset(&satchel_graphics_drain))) return result;
    atomic_store(&satchel_present_stop, 0); atomic_store(&satchel_encode_stop, 0);
    satchel_retired_owners = 0;
    return 0;
}
