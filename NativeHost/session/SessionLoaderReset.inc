// The loader maps every offline NRO before guest execution. Register its sealed
// executable pieces now, even when the guest activates that NRO much later.
static void satchel_loader_ranges(module_t *m) {
    if (!satchel_session_active()) return;
    size_t text = (m->text_end + 0x3fff) & ~(size_t)0x3fff;
    size_t patch = m->patch_off < 0 ? m->patch_len : m->size - (size_t)m->patch_off;
    if ((text && satchel_session_guest_range(m->base, text)) ||
        (patch && satchel_session_guest_range(m->base + m->patch_off, patch)) ||
        (m->back_len && satchel_session_guest_range(m->base + m->back_off, m->back_len)))
        die("session executable range registration failed");
}

// All native borrowers must be retired first. A failed unmap preserves that
// module record for retry. No guest destructor runs after terminal cancellation.
int satchel_loader_session_reset(void) {
    if (!satchel_session_quiescent()) return EBUSY;
    // A JIT image layout (nn::jit served from a capture) reserves one region for several
    // modules. No owner unmaps it yet: refuse, as the nn::jit reset does.
    if (g_jit_base || g_njit_mod) return EBUSY;
    pthread_rwlock_wrlock(&g_modules_mu);
    int result = 0;
    for (int i = 0; i < g_nmod; i++) {
        module_t *m = &g_mod[i];
        if (!m->base) continue;
        size_t prefix = m->patch_off < 0 ? (size_t)-m->patch_off : 0;
#if TARGET_OS_IPHONE
        if (vm_deallocate(mach_task_self(), (vm_address_t)(m->base - prefix), prefix + m->size) != KERN_SUCCESS) result = EIO;
#else
        if (munmap(m->base - prefix, prefix + m->size)) result = errno;
#endif
        if (result) break;
        m->base = NULL;
        free(m->symh); m->symh = NULL;
        free(m->pristine); m->pristine = NULL;
    }
    if (!result) {
        free(g_mod); g_mod = NULL; g_nmod = 0;
        free(g_loaded); g_loaded = NULL; g_nloaded = g_mod_capacity = 0;
        free(g_force_hits); g_force_hits = NULL;
        free(g_fidx); g_fidx = NULL; g_fmask = g_nforce = 0; g_force = NULL;
        g_game_image = &_mh_execute_header; g_module_dir[0] = 0;
        g_fps_mod = NULL; dm_fps_state = NULL; satchel_sdk_major = -1;
        satchel_sdk_ounce = satchel_nvn2_found = -1; satchel_jit_dir_init = 0; satchel_jit_dir[0] = 0;
        memset(g_refused, 0, sizeof g_refused); g_nrefused = 0;
        g_symchk_n = g_symchk_bad = 0;
    }
    pthread_rwlock_unlock(&g_modules_mu);
    return result;
}
