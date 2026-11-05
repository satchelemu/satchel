"""Lifecycle hooks on the pinned MIT engine, applied after session ownership."""
from pathlib import Path
import runpy
HERE = Path(__file__).resolve().parent
replace = runpy.run_path(str(HERE / 'owners_overlay.py'))['replace']


def host(destination):
    path = destination / 'hle_fs.c'
    path.write_text(path.read_text() + '\n' + (HERE / 'SessionSaveLifecycle.inc').read_text())
    path = destination / 'hle_audio.c'
    text = path.read_text()
    text = replace(text, 'static int g_suspended;', 'static int g_suspended;\nstatic _Atomic int satchel_audio_held;')
    text = replace(text, 'static void null_start(aout *a) {',
                   'static void null_start(aout *a) { if (atomic_load(&satchel_audio_held)) { a->backend = BK_NONE; return; }')
    # A held output starts no device: its start waits for the release below.
    text = replace(text, 'if (!a->au || g_suspended || a->start_phase != AU_IDLE || satchel_session_stopping()) return;',
                   'if (!a->au || g_suspended || a->start_phase != AU_IDLE || satchel_session_stopping() || atomic_load(&satchel_audio_held)) return;')
    # Reset every output, including one that has no unit yet. The engine keeps a started output
    # on its one silent consumer through the reset; a held output stays without a consumer.
    text = replace(text, 'static void au_replace(aout *a) {\n    au_dispose(a->au);', 'static void au_replace(aout *a) {\n    if (a->au) au_dispose(a->au);')
    first = text.index('void dm_audio_reset(void)')
    text = text[:first] + text[first:].replace('aout *a = g_aos[i]; if (!a || !a->au) continue;', 'aout *a = g_aos[i]; if (!a) continue;', 1)
    text = replace(text, 'g_suspended = 0; atomic_store(&g_render_calls, 0);', 'g_suspended = 0; atomic_store(&satchel_audio_held, 0); atomic_store(&g_render_calls, 0);')
    text += '''
// A full guest pause retains queued PCM at its exact consumer position.
// An audio-only interruption still uses the paced silent fallback.
void satchel_audio_hold(int on) {
    pthread_mutex_lock(&g_aos_lock);
    atomic_store(&satchel_audio_held, on != 0);
    for (int i = 0; i < AOMAX; i++) {
        aout *a = g_aos[i]; if (!a) continue;
        if (on) { au_halt(a); if (a->backend == BK_NULL) null_stop(a); }   // the device, a start in flight and the silent sink
        else if (a->state == STATE_STARTED && a->backend == BK_NONE) {
            // As the engine's own start: the silent sink consumes until the device calls back.
            null_start(a); au_request_start(a);
        }
    }
    pthread_mutex_unlock(&g_aos_lock);
}
int satchel_audio_outputs_ready(void) {
    pthread_mutex_lock(&g_aos_lock); int ready = 1;
    for (int i = 0; i < AOMAX; i++)
        if (g_aos[i] && g_aos[i]->state == STATE_STARTED && !g_aos[i]->au && real_audio()) ready = 0;
    pthread_mutex_unlock(&g_aos_lock); return ready;
}
'''
    path.write_text(text)


def graphics(text):
    text = replace(text, '@interface NvMetalDisplayLinkDelegate', '''static _Atomic int satchel_link_paused;
static unsigned satchel_link_callbacks;
static void satchel_link_leave(int *entered) {
    if (!*entered) return;
    pthread_mutex_lock(&g_pq_mu); satchel_link_callbacks--;
    pthread_cond_broadcast(&g_pq_cv); pthread_mutex_unlock(&g_pq_mu);
}
static void satchel_link_quiesce(void) {
    pthread_mutex_lock(&g_pq_mu);
    while (satchel_link_callbacks) pthread_cond_wait(&g_pq_cv, &g_pq_mu);
    pthread_mutex_unlock(&g_pq_mu);
}
@interface NvMetalDisplayLinkDelegate''')
    text = replace(text, 'g_link_rl = CFRunLoopGetCurrent(); g_link = link;',
                   'pthread_mutex_lock(&g_pq_mu); g_link_rl = CFRunLoopGetCurrent(); g_link = link; link.paused = atomic_load(&satchel_link_paused); pthread_mutex_unlock(&g_pq_mu);')
    text = replace(text, 'if (atomic_load(&satchel_present_stop)) return;\n    double cb', '''int entered __attribute__((cleanup(satchel_link_leave))) = 0;
    pthread_mutex_lock(&g_pq_mu);
    if (atomic_load(&satchel_present_stop) || atomic_load(&satchel_link_paused)) { pthread_mutex_unlock(&g_pq_mu); return; }
    satchel_link_callbacks++; entered = 1; pthread_mutex_unlock(&g_pq_mu);
    double cb''')
    # This flag is consulted by a link created after suspension as well.
    text = replace(text, 'void mtl_link_pause(int on) { CAMetalDisplayLink *l = g_link; if (l) l.paused = on != 0; }',
                   'void mtl_link_pause(int on) { atomic_store(&satchel_link_paused, on != 0); pthread_mutex_lock(&g_pq_mu); CAMetalDisplayLink *l = g_link; if (l) l.paused = on != 0; pthread_mutex_unlock(&g_pq_mu); }')
    # Avoid creating a Metal device and submitting an empty command buffer on a
    # background event before the game has ever used graphics.
    text = replace(text, 'void mtl_drain(void) { present_drop_for_suspend(); mtl_present_flush(); snap_drain();',
                   'static void satchel_link_quiesce(void);\nvoid mtl_drain(void) { if (!g_dev) return; present_drop_for_suspend(); mtl_present_flush(); rec_drain(); satchel_link_quiesce(); snap_drain();')
    text = replace(text, 'static id<MTLRenderPipelineState> pso_pend_take(const pso_key *k) {',
                   'static unsigned satchel_pp_waiters[PSO_PEND_MAX];\nstatic id<MTLRenderPipelineState> pso_pend_take(const pso_key *k) {')
    text = replace(text, 'if (at >= 0) { while (!g_pp[at].done)', 'if (at >= 0) { satchel_pp_waiters[at]++; while (!g_pp[at].done)')
    text = replace(text, 'ps = g_pp[at].ps; g_pp[at].ps = nil; g_pp[at].used = 0; }',
                   'ps = g_pp[at].ps; g_pp[at].ps = nil; g_pp[at].used = 0; satchel_pp_waiters[at]--; }')
    text += '''
// Finished speculative pipelines are optional cache entries, never live draw
// state. A demand waiter owns its slot until it has retained the result.
unsigned satchel_mtl_memory_pressure(void) {
    unsigned count = 0;
    pthread_mutex_lock(&g_pp_mu);
    for (int i = 0; i < PSO_PEND_MAX; i++) {
        if (g_pp[i].used && g_pp[i].done && !g_pp[i].demand && !satchel_pp_waiters[i]) {
            g_pp[i].ps = nil; g_pp[i].d = nil; g_pp[i].used = 0; count++;
        }
    }
    pthread_mutex_unlock(&g_pp_mu);
    return count;
}
'''
    return text
