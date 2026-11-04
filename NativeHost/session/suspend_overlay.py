"""Keep the UI thread's graphics suspend bounded while a submission builds shaders.

A submission holds the graphics gate for its whole replay, and runtime shader
translation and pipeline builds happen inside it. Mario Kart 8 Deluxe spends up to
20 s in one submission on first use. The pause menu and the background transition
called dm_gfx_suspend(1) on the main thread, which waited for that submission without
a limit; iOS then killed the frozen app (0x8BADF00D, Satchel-2026-11-02-133313 and
-190635). Now the suspend closes the gate at once and waits at most SATCHEL_SUSPEND_WAIT_MS.
A submission still inside finishes on its own thread, and the last one to leave drains
the GPU before any new graphics call may enter.
"""
from pathlib import Path
import runpy

replace = runpy.run_path(str(Path(__file__).resolve().with_name('owners_overlay.py')))['replace']

ENTER = 'static void gfx_enter(void) { pthread_mutex_lock(&g_susp_mu); while (g_susp) pthread_cond_wait(&g_susp_cv, &g_susp_mu); g_inflight_api++; pthread_mutex_unlock(&g_susp_mu); dm_host_work++; }\n'
LEAVE = 'static void gfx_leave(void) { dm_host_work--; pthread_mutex_lock(&g_susp_mu); g_inflight_api--; pthread_cond_broadcast(&g_susp_cv); pthread_mutex_unlock(&g_susp_mu); }\n'
SUSPEND = '''void dm_gfx_suspend(int on) {
    pthread_mutex_lock(&g_susp_mu);
    g_susp = on != 0;
    if (on) while (g_inflight_api) pthread_cond_wait(&g_susp_cv, &g_susp_mu);
    else pthread_cond_broadcast(&g_susp_cv);
    pthread_mutex_unlock(&g_susp_mu);
    if (on) { dm_vk_drain(); dm_gl_drain(); mtl_drain(); LOG("[nvn] graphics suspended: in-flight GPU work drained"); } else LOG("[nvn] graphics resumed");   // the Vulkan and GL queues first: presents on the NVN queue wait for their frames
}
'''

GATE = '''#ifndef SATCHEL_SUSPEND_WAIT_MS
#define SATCHEL_SUSPEND_WAIT_MS 500
#endif
// satchel_susp_late: a suspend returned while a call was still inside; the last call out drains.
// satchel_susp_draining: that drain is running; no graphics call may enter until it ends.
static int satchel_susp_late, satchel_susp_draining;
void dm_vk_drain(void); void dm_gl_drain(void); void mtl_drain(void);
static void satchel_gfx_drain(void) { dm_vk_drain(); dm_gl_drain(); mtl_drain(); }   // the Vulkan and GL queues first: presents on the NVN queue wait for their frames
static void gfx_enter(void) { pthread_mutex_lock(&g_susp_mu); while (g_susp || satchel_susp_draining) pthread_cond_wait(&g_susp_cv, &g_susp_mu); g_inflight_api++; pthread_mutex_unlock(&g_susp_mu); dm_host_work++; }
static void gfx_leave(void) {
    dm_host_work--; pthread_mutex_lock(&g_susp_mu);
    if (g_susp && satchel_susp_late && g_inflight_api == 1) {
        satchel_susp_late = 0; satchel_susp_draining = 1; pthread_mutex_unlock(&g_susp_mu);
        satchel_gfx_drain(); LOG("[nvn] graphics suspended: the late call drained in-flight GPU work");
        pthread_mutex_lock(&g_susp_mu); satchel_susp_draining = 0;
    }
    g_inflight_api--; pthread_cond_broadcast(&g_susp_cv); pthread_mutex_unlock(&g_susp_mu);
}
'''

BOUNDED = '''void dm_gfx_suspend(int on) {
    pthread_mutex_lock(&g_susp_mu);
    g_susp = on != 0;
    int late = 0;
    if (on) {
        struct timespec until; clock_gettime(CLOCK_REALTIME, &until);
        until.tv_nsec += (long)(SATCHEL_SUSPEND_WAIT_MS % 1000) * 1000000L; until.tv_sec += SATCHEL_SUSPEND_WAIT_MS / 1000 + until.tv_nsec / 1000000000L; until.tv_nsec %= 1000000000L;
        while (g_inflight_api && pthread_cond_timedwait(&g_susp_cv, &g_susp_mu, &until) != ETIMEDOUT) {}
        late = satchel_susp_late = g_inflight_api > 0;
    } else { satchel_susp_late = 0; pthread_cond_broadcast(&g_susp_cv); }
    pthread_mutex_unlock(&g_susp_mu);
    if (on && late) LOG("[nvn] graphics suspended: a call is still building shaders after %d ms; it drains when it leaves", SATCHEL_SUSPEND_WAIT_MS);
    else if (on) { satchel_gfx_drain(); LOG("[nvn] graphics suspended: in-flight GPU work drained"); } else LOG("[nvn] graphics resumed");
}
'''


def bounded_suspend(text):
    text = replace(text, ENTER + LEAVE, GATE)
    return replace(text, SUSPEND, BOUNDED)
