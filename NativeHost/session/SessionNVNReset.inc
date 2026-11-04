static void satchel_pk_free(const void *key, const void *value, void *context) {
    (void)key; (void)context;
    free((void *)value);
}
int satchel_nvn_session_reset(void) {
    if (!satchel_session_quiescent() || g_inflight_api) return EBUSY;
    // Metal no longer owns no-copy buffers on these aliases. Failed unmaps
    // retain the corresponding record so a retry cannot double free it.
    while (g_nvp) {
        struct vpool *v = g_vp[g_nvp - 1];
        if (munmap(v->base, v->len)) return errno;
        for (uint64_t i = 0; i < (v->size + (2u << 20) - 1) >> 21; i++) free(v->pt[i]);
        free(v->pt); free(v); g_nvp--;
    }
    free(g_vp); g_vp = NULL; g_cvp = 0;
    free(g_ft); g_ft = NULL; g_nft = g_cft = 0;
    free(g_fl); g_fl = NULL; g_nfl = g_cfl = 0;
    free(satchel_ft_hit); satchel_ft_hit = NULL; satchel_ft_chit = 0;
    if (g_pk) { CFDictionaryApplyFunction(g_pk, satchel_pk_free, NULL); CFRelease(g_pk); g_pk = NULL; }
    for (int i = 0; i < g_nhostseg; i++) free(g_hostseg[i].mem);
    free(g_hostseg); g_hostseg = NULL; g_nhostseg = g_caphostseg = 0; g_hostseg_bytes = 0;
    memset(g_tpb, 0, sizeof g_tpb); memset(g_spb, 0, sizeof g_spb);
    g_ntpb = g_nspb = g_tnext = g_snext = 0;
    if (g_tsv) fclose(g_tsv); g_tsv = NULL;
    g_frame = 0; g_hz = g_present_cap = g_fs_pshare = -1;
    g_display_mode = g_susp = g_fs_k = 0; g_game_itv = 1; g_itv2_since = 0;
    g_fs = (nvn_fstep){0}; g_fg = (nvn_fgpu){0};
    g_rt_comp_ms = g_rt_period_ms = 0;
    return satchel_state_restore(SATCHEL_STATE_NVN);
}
