// Front-end controller, motion and touch publishers must be disabled and joined
// before reset. Existing lifecycle notification delivery is not a stop barrier.
void satchel_hid_request_stop(void) { satchel_input_stop(); satchel_native_stop(&satchel_hid_workers); }
int satchel_hid_join(unsigned timeout_ms) { int rc = satchel_input_join(timeout_ms); return rc ? rc : satchel_native_join(&satchel_hid_workers, timeout_ms); }
int satchel_hid_session_reset(void) {
    if (!satchel_session_quiescent()) return EBUSY;
    int result = satchel_input_join(0); if (result) return result;
    result = satchel_native_join(&satchel_hid_workers, 0);
    if (result) return result;
    result = satchel_touch_session_reset();
    if (result) return result;
    atomic_store(&dm_touch_observer, NULL);
    dm_rec_finish(); atomic_store(&g_rec_on, 0);
    if (g_rec_file) { int rc = fclose(g_rec_file); g_rec_file = NULL; if (rc) result = EIO; }
    if (result) return result;
    free(g_steps); g_steps = NULL; g_nsteps = g_cap_steps = 0;
    atomic_store(&g_sync_on, 0); g_sync_next = g_sync_held = 0;
    free(g_rec_buf); g_rec_buf = NULL; g_rec_len = g_rec_cap = 0;
    g_rec_cur = (rec_state){0}; g_rec_frame = (rec_state){0};
    g_rec_from = g_rec_at = 0; g_rec_have = g_rec_touch = g_rec_tlatch = 0;
    g_rec_tx = g_rec_ty = 0;
    g_btn = g_axes = g_latch = g_sc_btn = g_sc_axes = g_sc_latch = 0;
    g_sampling = 1 << 16; g_shake_t0 = g_shake_end = 0; g_shake_on = 0;
    g_players = 1; g_ids_supported = 0x3ff; g_styles_supported = 0xffffffffu;
    g_hold_type = g_comm_mode = g_hh_mode = 0;
    for (int p = 0; p < DM_PLAYERS; p++) {
        g_pad_btn[p] = g_pad_axes[p] = g_pad_latch[p] = 0;
        atomic_store(&g_style_ev[p], NULL);
        g_motion[p].st = (motion_state){0};
        g_motion[p].last_ns = g_motion[p].received_ns = g_motion[p].logged = 0;
        g_hid_motion[p].st = (motion_state){0};
        g_hid_motion[p].last_ns = g_hid_motion[p].received_ns = g_hid_motion[p].logged = 0;
    }
    g_phone_motion.st = (motion_state){0};
    g_phone_motion.last_ns = g_phone_motion.received_ns = g_phone_motion.logged = 0;
    // The next first input reads the next package's environment.
    satchel_motion_ready = 0;
    result = satchel_native_reset(&satchel_hid_workers);
    if (!result) satchel_input_reset();
    return result;
}
