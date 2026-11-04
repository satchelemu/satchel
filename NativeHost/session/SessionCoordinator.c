#include "SessionBoundary.h"
#include "SessionState.h"
#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <malloc/malloc.h>

// One serial host coordinator owns prepare/stop. Guest/native workers never
// call these entry points. A failed stage is retried with admission still shut.
static unsigned stop_stage;
__attribute__((weak, noinline)) void satchel_session_phase(const char *phase, int result) { (void)phase; (void)result; }
static int restore_services(void) { return satchel_state_restore(SATCHEL_STATE_SERVICES); }
// The Swift phone compiler overrides this in the app. Mac probes have no compiler.
__attribute__((weak, noinline)) void satchel_phone_shader_session_reset(void) {}
static int reset_phone_shaders(void) { satchel_phone_shader_session_reset(); return 0; }
static int join_diagnostics(void) { return satchel_diagnostics_join(2000); }
static int join_guest(void) { return satchel_session_join(5000); }
static int join_hid(void) { return satchel_hid_join(2000); }
static int join_memory(void) { return satchel_memory_join(2000); }
static int join_oe(void) { return satchel_oe_join(2000); }
static int join_sdk(void) { return satchel_sdk16_join(2000); }
static int join_graphics(void) { return satchel_mtl_producers_join(15000); }
int satchel_host_session_prepare(void) {
    if (satchel_session_active()) return EBUSY;
    const char *unsupported[] = {"DM_NVN_CAPTURE", "DM_PROBE", "DM_PROBE_WATCH", "DM_PROBE_POLL",
        "DM_HITCH", "DM_TLS_KEY_RACE", "DM_TEST_KEYRACE", "DM_FD_LOG", "DM_OODLE_JOB", "DM_OODLE_SERVE", "DM_CHEATS"};
    for (unsigned i = 0; i < sizeof unsupported / sizeof unsupported[0]; i++)
        if (getenv(unsupported[i])) return ENOTSUP;
    const char *sync = getenv("DM_SAVE_SYNC");
    if (sync && *sync && strcmp(sync, "0")) return ENOTSUP;
    const char *root = getenv("DM_ROOT");
    if (root && *root) {
        char path[4096]; struct stat st;
        int length = snprintf(path, sizeof path, "%s/cheats", root);
        if (length < 0 || length >= sizeof path) return ENAMETOOLONG;
        if (!stat(path, &st)) return ENOTSUP;
        if (errno != ENOENT) return errno;
    }
    int result = satchel_mtl_session_prepare(); if (result) return result;
    result = satchel_session_begin(); if (!result) stop_stage = 0;
    return result;
}
int satchel_host_session_stop(void) {
    if (!satchel_session_active()) return 0;
    if (!stop_stage) {
        satchel_session_request_stop(); satchel_kernel_request_stop(); satchel_clock_request_stop();
        satchel_hid_request_stop(); satchel_memory_request_stop(); satchel_diagnostics_request_stop(); satchel_oe_request_stop(); satchel_sdk16_request_stop();
        satchel_movie_request_stop(); satchel_audren_request_stop(); satchel_audio_request_stop(); satchel_mtl_request_stop();
        void dm_gfx_suspend(int); dm_gfx_suspend(0);
        stop_stage = 1;
    }
    static const struct { const char *name; int (*run)(void); } stages[] = {
        {"guest-join", join_guest}, {"diagnostics-join", join_diagnostics},
        {"diagnostics-reset", satchel_diagnostics_session_reset}, {"control-join", join_hid}, {"memory-workers-join", join_memory},
        {"notification-join", join_oe}, {"vsync-join", join_sdk}, {"graphics-producers-join", join_graphics},
        {"movie-reset", satchel_movie_session_reset}, {"audren-reset", satchel_audren_session_reset},
        {"audio-reset", satchel_audio_session_reset}, {"helpers-reset", satchel_guest_helpers_session_reset},
        {"hid-reset", satchel_hid_session_reset}, {"graphics-retire", satchel_mtl_session_retire},
        {"phone-shaders-reset", reset_phone_shaders},
        {"cheats-reset", satchel_cheats_session_reset}, {"os-reset", satchel_os_resources_session_reset}, {"sdk-reset", satchel_sdk16_session_reset},
        {"notification-reset", satchel_oe_session_reset}, {"sf-reset", satchel_sf_session_reset},
        {"addons-reset", satchel_misc_session_reset}, {"network-reset", satchel_net_session_reset},
        {"friends-reset", satchel_friends_session_reset}, {"offline-reset", satchel_offline_session_reset},
        {"keyboard-reset", satchel_swkbd_session_reset}, {"fps-reset", satchel_fpslock_session_reset},
        {"ue-reset", satchel_uemalloc_session_reset}, {"service-caches-reset", restore_services},
        {"dirty-reset", satchel_dirty_session_reset}, {"nvn-reset", satchel_nvn_session_reset},
        {"files-reset", satchel_fs_session_reset}, {"kernel-reset", satchel_kernel_session_reset},
        {"loader-reset", satchel_loader_session_reset}, {"memory-reset", satchel_memory_session_reset},
        {"gates-reset", satchel_gate_session_reset}, {"clock-reset", satchel_clock_session_reset},
        {"bootstrap-reset", satchel_bootstrap_session_reset}, {"profile-reset", satchel_profile_session_reset}, {"ended", satchel_session_end},
    };
    while (stop_stage <= sizeof stages / sizeof stages[0]) {
        const unsigned i = stop_stage - 1;
        int result = stages[i].run();
        satchel_session_phase(stages[i].name, result);
        if (result) return result;
        stop_stage++;
    }
    // The next title should reuse address space without retaining allocator
    // slack from the old renderer. This releases only already freed blocks.
    malloc_zone_pressure_relief(NULL, 0);
    return 0;
}
