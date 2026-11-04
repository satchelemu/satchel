#include "hle.h"
#undef HLE_FORCE
#undef HLE_FORCE_BIG
#define HLE_FORCE(mangled, why, ret, cname, ...) static ret cname(__VA_ARGS__)
#define HLE_FORCE_BIG HLE_FORCE
#include "hle_hid.c"
#include "hle_touch.c"
#include <assert.h>

static int touched(void) {
    uint8_t state[0x290];
    return touch_fill(state, now_ns());
}
void crash_dump_now(const char *why) { (void)why; abort(); }
void dm_gfx_shot_at(long n, const char *path) { (void)n; (void)path; abort(); }
void dm_gfx_texraw(uint64_t n, const char *path) { (void)n; (void)path; abort(); }
void dm_gfx_texlist(int n) { (void)n; abort(); }
void dm_gfx_texdump_next(const char *path) { (void)path; abort(); }
void dm_gfx_passdump_next(long n) { (void)n; abort(); }
void dm_ctl_peek(uint64_t a, int n) { (void)a; (void)n; abort(); }
void dm_ctl_memdump(uint64_t a, uint64_t n, const char *p) { (void)a; (void)n; (void)p; abort(); }
void dm_ctl_cvar_i32(uint64_t a, int32_t n) { (void)a; (void)n; abort(); }
void dm_ctl_cvar_f32(uint64_t a, float n) { (void)a; (void)n; abort(); }

void owners_hid_start(const char *root, int cycle) {
    int publisher = satchel_input_enter(); assert(publisher);
    satchel_input_stop(); assert(!satchel_input_enter());
    assert(satchel_input_join(5) == ETIMEDOUT);
    satchel_input_leave(&publisher); assert(!satchel_input_join(0)); satchel_input_reset();
    char ctl[1200], rec[1200];
    snprintf(ctl, sizeof ctl, "%s/control-%d.txt", root, cycle);
    snprintf(rec, sizeof rec, "%s/recording-%d.txt", root, cycle);
    FILE *file = fopen(ctl, "w"); assert(file);
    fputs(cycle % 2 ? "drag 0 0 1 1 3600\n" : "A~3600\n", file);
    assert(!fclose(file));
    setenv("DM_CTL", ctl, 1); setenv("DM_REC", rec, 1);
    setenv("DM_KEYS", "f9999999:A~f9999999,9999999:B", 1);
    hid_keys_init();
    uint64_t until = now_ns() + 2000000000;
    while (satchel_native_live(&satchel_hid_workers) != 3 ||
           (cycle % 2 ? !touched() : !atomic_load(&g_sc_btn))) {
        assert(now_ns() < until); usleep(1000);
    }
    assert(g_rec_file && g_steps && g_nsteps == 2);
    assert(satchel_hid_session_reset() == EBUSY);
    assert(satchel_touch_session_reset() == EBUSY);
    // A press released before a guest read remains latched. Gesture sampling
    // also retains history until the session reset explicitly clears it.
    dm_touch touch = {.id = 7, .x = 0.4f, .y = 0.7f};
    dm_touch_script(&touch, 1); dm_touch_script(NULL, 0);
    gesture_state gesture[16]; assert(gesture_fill(gesture, 16, now_ns()) > 0);
    atomic_store(&g_style_ev[0], (void *)(uintptr_t)1);
}

void owners_hid_verify(void) {
    assert(!satchel_hid_join(2000));
    assert(!satchel_hid_session_reset());
    assert(!satchel_native_live(&satchel_hid_workers));
    assert(!g_rec_file && !g_rec_buf && !g_steps && !g_nsteps);
    assert(!atomic_load(&g_style_ev[0]) && !atomic_load(&g_sc_btn));
    assert(!atomic_load(&dm_touch_observer));
    assert(!touched());
    gesture_state gesture[16]; assert(!gesture_fill(gesture, 16, now_ns()));
    assert(!g_gw && g_tsamp == 1 && g_tlast_ns != 0);
}
