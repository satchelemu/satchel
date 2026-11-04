// HID/control and front-end touch publishers must already be stopped. Clearing
// the live fingers alone would leave latched taps and the gesture history behind.
int satchel_touch_session_reset(void) {
    if (!satchel_session_quiescent()) return EBUSY;
    pthread_mutex_lock(&g_tl);
    memset(g_f, 0, sizeof g_f); memset(g_gf, 0, sizeof g_gf);
    g_tsamp = 0; g_tlast_ns = 0;
    memset(g_gr, 0, sizeof g_gr); g_gw = 0;
    g_glast = (gesture_props){0}; g_gdown = (gesture_props){0};
    g_gctx = 0; g_gforce = g_gtap_ok = g_gslop = 0;
    g_gupd_ns = g_gtap_ns = 0; g_gpan_dt = 0;
    pthread_mutex_unlock(&g_tl);
    atomic_store(&dm_touch_observer, NULL);
    return 0;
}
