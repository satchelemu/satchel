"""Typed service retirement, applied after the kernel and media lifetime patches."""
from pathlib import Path
import runpy

HERE = Path(__file__).resolve().parent
replace = runpy.run_path(str(HERE / 'owners_overlay.py'))['replace']
FILES = ['hle_cheats.c', 'hle_codec.c', 'hle_glslc.c', 'hle_sdk16.c', 'hle_oe.c', 'hle_sf.c',
         'hle_misc.c', 'hle_net.c', 'hle_friends.c', 'hle_offline.c', 'hle_swkbd.c', 'hle_fpslock.c', 'hle_time.c', 'hle_uemalloc.c', 'hle_lifecycle.c']
PREAMBLE = '\n#include "SessionBoundary.h"\n#include "SessionState.h"\n#include "SessionNativeWorkers.h"\n#include <errno.h>\n'


def remember(text, declaration, names):
    return replace(text, declaration, declaration + ' ' + ' '.join(
        'SATCHEL_STATE(SATCHEL_STATE_SERVICES, ' + n + ');' for n in names.split()))


def reset(text, name, body):
    return text + '\nint satchel_' + name + '_session_reset(void) {\n    if (!satchel_session_quiescent()) return EBUSY;\n' + body + '\n    return 0;\n}\n'


def common(text):
    text = PREAMBLE + text
    text = replace(text, 'static struct { const void *k; void *v; } g_om[OM_SIZE];',
                   'static struct { const void *k; void *v; void (*retire)(void *); } g_om[OM_SIZE];')
    text = replace(text, 'void objmap_put(const void *k, void *v) {',
                   'void satchel_objmap_put_owned(const void *k, void *v, void (*retire)(void *)) {')
    text = replace(text, 'g_om[i].k = k; g_om[i].v = v; break;',
                   'g_om[i].k = k; g_om[i].v = v; g_om[i].retire = retire; break;')
    text = replace(text, 'void *objmap_del(const void *k) {',
                   'void objmap_put(const void *k, void *v) { satchel_objmap_put_owned(k, v, NULL); }\nvoid *objmap_del(const void *k) {')
    text = replace(text, 'g_om[i].k = (void *)1; g_om[i].v = NULL;',
                   'g_om[i].k = (void *)1; g_om[i].v = NULL; g_om[i].retire = NULL;')
    text = replace(text, 'static const char **g_env_kv;',
                   'static char *satchel_env_storage;\nstatic pthread_mutex_t satchel_env_mu = PTHREAD_MUTEX_INITIALIZER;\nstatic const char **g_env_kv;')
    text = replace(text, 'char *s = strdup(e); int k = 0;',
                   'char *s = satchel_env_storage = strdup(e); if (!s) die("game_env allocation failed"); int k = 0;')
    text = replace(text, 'static const char *const *game_env(void) { static pthread_once_t once = PTHREAD_ONCE_INIT; pthread_once(&once, game_env_init); return g_env_kv; }',
                   'static const char *const *game_env(void) { pthread_mutex_lock(&satchel_env_mu); if (!g_env_kv) game_env_init(); pthread_mutex_unlock(&satchel_env_mu); return g_env_kv; }')
    text = remember(text, 'static char *(*orig)(const char *);', 'orig')
    text = replace(text, 'static pthread_once_t g_sa_once = PTHREAD_ONCE_INIT;',
                   'static pthread_mutex_t satchel_sa_mu = PTHREAD_MUTEX_INITIALIZER;\nstatic int satchel_sa_ready;')
    text = replace(text, 'pthread_once(&g_sa_once, sa_init);',
                   'pthread_mutex_lock(&satchel_sa_mu); if (!satchel_sa_ready) { sa_init(); satchel_sa_ready = 1; } pthread_mutex_unlock(&satchel_sa_mu);')
    return reset(text, 'os_resources', '''    pthread_mutex_lock(&g_om_lock);
    // Media owners must have removed their entries first. Unknown owners veto
    // the whole operation before any object is retired.
    for (unsigned i = 0; i < OM_SIZE; i++) if (g_om[i].v && !g_om[i].retire) {
        pthread_mutex_unlock(&g_om_lock); return EBUSY;
    }
    for (unsigned i = 0; i < OM_SIZE; i++) if (g_om[i].v) g_om[i].retire(g_om[i].v);
    memset(g_om, 0, sizeof g_om); pthread_mutex_unlock(&g_om_lock);
    free(g_env_kv); g_env_kv = NULL; free(satchel_env_storage); satchel_env_storage = NULL;
    g_sa_alloc = NULL; g_sa_alloc_al = NULL; g_sa_big_align = satchel_sa_ready = 0;''')


def codec(text):
    text = remember(text, 'static void (*soft_ctor)(void *);', 'soft_ctor')
    assert text.count('objmap_put(hw, s);') == 2
    return text.replace('objmap_put(hw, s);', 'satchel_objmap_put_owned(hw, s, free);')


def glslc(text):
    text = remember(text, 'static const char *dir; static int init;', 'dir init')
    text = remember(text, 'static int v[2] = {-1, -1};', 'v')
    return remember(text, 'static uint8_t (*orig)(void *);', 'orig')


def sdk16(text):
    text = replace(text, 'static pthread_once_t font_load_once = PTHREAD_ONCE_INIT;',
                   'static int satchel_font_ready;')
    assert text.count('pthread_once(&font_load_once, font_load_init);') == 2
    text = text.replace('pthread_once(&font_load_once, font_load_init);',
                        'pthread_mutex_lock(&font_mu); if (!satchel_font_ready) { font_load_init(); satchel_font_ready = 1; } pthread_mutex_unlock(&font_mu);')
    text = remember(text, 'static int std_done, ext_done;', 'std_done ext_done')
    text = replace(text, 'static vsync_event_t *g_vs_events;',
                   'static vsync_event_t *g_vs_events;\nstatic SatchelNativeWorkers satchel_vsync_workers = SATCHEL_NATIVE_WORKERS_INIT;')
    text = replace(text, '    for (;;) {\n        if (!e && mtl_link_wait_vsync',
                   '    while (!satchel_native_stopping(&satchel_vsync_workers)) {\n        if (!e && mtl_link_wait_vsync')
    text = replace(text, 'struct timespec ts = {(time_t)((next - t) / 1000000000ull), (long)((next - t) % 1000000000ull)}; nanosleep(&ts, NULL);',
                   'uint64_t ms = (next - t + 999999) / 1000000; if (satchel_native_wait(&satchel_vsync_workers, ms > 100 ? 100 : (unsigned)ms)) break; if (now_ns() < next) continue;')
    text = replace(text, '    free(v); return NULL;', '''    pthread_mutex_lock(&g_vs_mu);
    vsync_event_t **p = &g_vs_events; while (*p && *p != v) p = &(*p)->next;
    if (*p) *p = v->next;
    pthread_mutex_unlock(&g_vs_mu);
    free(v); return NULL;''')
    text = replace(text, 'pthread_t th; if (pthread_create(&th, NULL, vsync_thread, v)) die("cannot start the vsync thread"); pthread_detach(th);',
                   'int rc = satchel_native_create(&satchel_vsync_workers, NULL, vsync_thread, v); if (rc) { g_vs_events = v->next; free(v); if (rc != ECANCELED) die("cannot start the vsync thread"); }')
    text += '''
void satchel_sdk16_request_stop(void) { satchel_native_stop(&satchel_vsync_workers); }
int satchel_sdk16_join(unsigned ms) { return satchel_native_join(&satchel_vsync_workers, ms); }
'''
    return reset(text, 'sdk16', '''    int rc = satchel_native_join(&satchel_vsync_workers, 0); if (rc) return rc;
    if (g_vs_events) return EBUSY;
    font_t *fonts[] = {&std_f, &ext_f, &small_f};
    for (unsigned i = 0; i < 3; i++) if (fonts[i]->p) {
        if (munmap(fonts[i]->p, fonts[i]->n)) return errno;
        *fonts[i] = (font_t){0};
    }
    small_done = satchel_font_ready = font_neng = 0;
    font_load = NULL; font_err = NULL; font_init = NULL; font_fini = NULL;
    memset(font_eng, 0, sizeof font_eng); memset(g_ev_netclock, 0, sizeof g_ev_netclock);
    g_nfp_state = 0;
    return satchel_native_reset(&satchel_vsync_workers);''')


def prepare(host, destination, overrides=None):
    os_path = destination / 'hle_os.c'
    os_path.write_text(common(os_path.read_text()))
    thread_path = destination / 'hle_thread.c'
    thread_path.write_text(PREAMBLE + kernel_services(thread_path.read_text()))
    transforms = {'hle_cheats.c': cheats, 'hle_codec.c': codec, 'hle_glslc.c': glslc, 'hle_sdk16.c': sdk16, 'hle_oe.c': oe, 'hle_fpslock.c': fpslock, 'hle_time.c': lambda s: s,
        'hle_uemalloc.c': lambda s: simple(s, 'uemalloc', '    if (g_zone) malloc_destroy_zone(g_zone); g_zone = NULL; g_used = g_peak = 0; memset(g_vt, 0, sizeof g_vt); g_what = NULL;'),
        'hle_lifecycle.c': lifecycle,
        'hle_sf.c': lambda s: simple(s, 'sf', '    memset(g_sf, 0, sizeof g_sf);'),
        'hle_misc.c': lambda s: simple(s, 'misc', '    memset(g_aoc, 0, sizeof g_aoc); g_naoc = -1;'),
        'hle_net.c': lambda s: simple(s, 'net', '    g_nifm_hold = 0;'),
        'hle_friends.c': lambda s: simple(s, 'friends', '    memset(g_ev_notify, 0, sizeof g_ev_notify); memset(g_ev_invite, 0, sizeof g_ev_invite);'),
        'hle_offline.c': lambda s: simple(s, 'offline', '    memset(g_ev_ldn, 0, sizeof g_ev_ldn);'),
        'hle_swkbd.c': lambda s: simple(s, 'swkbd', '    memset(g_ikb, 0, sizeof g_ikb);')
    }
    for name, transform in transforms.items():
        path = (overrides or {}).get(name, host / name)
        text = transform(path.read_text())
        if name not in ('hle_lifecycle.c', 'hle_cheats.c'):
            text = runpy.run_path(str(HERE / 'state_overlay.py'))['scalar_caches'](text, 'SATCHEL_STATE_SERVICES')
        (destination / name).write_text(PREAMBLE + text)


def oe(text):
    text = replace(text, 'static _Atomic int g_opmode = -1;',
                   'static _Atomic int g_opmode = -1;\nstatic SatchelNativeWorkers satchel_oe_workers = SATCHEL_NATIVE_WORKERS_INIT;')
    text = replace(text, 'if (due > now) usleep((useconds_t)((due - now) / 1000));',
                   'while (due > now && !satchel_native_stopping(&satchel_oe_workers)) { uint64_t ms = (due - now + 999999) / 1000000; if (satchel_native_wait(&satchel_oe_workers, ms > 100 ? 100 : (unsigned)ms)) break; now = now_ns(); }\n        if (satchel_native_stopping(&satchel_oe_workers)) break;')
    text = replace(text, '    return NULL;\n}\nstatic void oe_script_start(void)',
                   '    free(arg); return NULL;\n}\nstatic void oe_script_start(void)')
    text = replace(text, '    pthread_t th; if (pthread_create(&th, NULL, oe_script, strdup(e))) die("DM_OE_SCRIPT thread");\n    pthread_detach(th);',
                   '    char *copy = strdup(e); if (!copy) die("DM_OE_SCRIPT allocation");\n    int rc = satchel_native_create(&satchel_oe_workers, NULL, oe_script, copy); if (rc) { free(copy); if (rc != ECANCELED) die("DM_OE_SCRIPT thread"); }')
    text = replace(text, 'static pthread_once_t g_oe_once = PTHREAD_ONCE_INIT;',
                   'static pthread_mutex_t satchel_oe_mu = PTHREAD_MUTEX_INITIALIZER;\nstatic int satchel_oe_ready;\nstatic void satchel_oe_start(void) { pthread_mutex_lock(&satchel_oe_mu); if (!satchel_oe_ready) { satchel_oe_ready = 1; oe_script_start(); } pthread_mutex_unlock(&satchel_oe_mu); }')
    assert text.count('pthread_once(&g_oe_once, oe_script_start);') == 3
    text = text.replace('pthread_once(&g_oe_once, oe_script_start);', 'satchel_oe_start();')
    text = replace(text, '        while (!g_mqn) {',
                   '        while (!g_mqn && !satchel_session_stopping()) {')
    text = replace(text, '        if (h_oe_TryPopMsg(&m)) return m;',
                   '        if (satchel_session_stopping()) return 0;\n        if (h_oe_TryPopMsg(&m)) return m;')
    text += '''
void satchel_oe_request_stop(void) {
    satchel_native_stop(&satchel_oe_workers);
    pthread_mutex_lock(&g_mq_lock); pthread_cond_broadcast(&g_mq_cond); pthread_mutex_unlock(&g_mq_lock);
}
int satchel_oe_join(unsigned ms) { return satchel_native_join(&satchel_oe_workers, ms); }
'''
    return reset(text, 'oe', '''    int rc = satchel_native_join(&satchel_oe_workers, 0); if (rc) return rc;
    g_opmode = -1; g_mqh = g_mqn = 0; memset(g_mq, 0, sizeof g_mq);
    g_ntf_opmode = g_ntf_perfmode = g_mq_ev_on = 0; memset(g_mq_ev, 0, sizeof g_mq_ev);
    g_focus = g_seen = 1; g_focus_mode = -1; g_focus_in_ns = g_applets = 0;
    g_inactivity_mode = g_nosleep = g_cpu_oc = satchel_oe_ready = 0;
    return satchel_native_reset(&satchel_oe_workers);''')


def simple(text, name, body):
    return reset(text, name, body)


def fpslock(text):
    text = remember(text, 'static uint8_t *base;', 'base') if text.count('static uint8_t *base;') == 1 else text.replace('static uint8_t *base;', 'static uint8_t *base; SATCHEL_STATE(SATCHEL_STATE_SERVICES, base);')
    text = remember(text, 'static lock_t ue[3];', 'ue')
    text = remember(text, 'static const lock_t *rows[NLOCKS + 3];', 'rows')
    text = remember(text, 'static const char *src;', 'src')
    return reset(text, 'fpslock', '    memset(&fb, 0, sizeof fb);')


def lifecycle(text):
    # SIGTERM is a process event. Install its source once across sessions.
    return replace(text, 'void dm_term_install(void) {',
                   'void dm_term_install(void) {\n    static _Atomic int installed; if (atomic_exchange(&installed, 1)) return;')


def kernel_services(text):
    text = remember(text, 'static uint32_t (*create)(void *, int, int);', 'create')
    text = remember(text, 'static uint32_t (*getw)(const void *);', 'getw')
    text = replace(text, 'void hle_bind_system_event(void *ev, int autoclear) {', """typedef struct {
    void *event; int autoclear;
    uint32_t (*create)(void *, int, int), (*getw)(const void *);
    uint32_t result, handle;
} satchel_se_call;
static __attribute__((no_instrument_function)) void satchel_se_guest(void *p) {
    satchel_se_call *call = p;
    call->result = call->create(call->event, call->autoclear, 1);
    if (!call->result) call->handle = call->getw(call->event);
}
void hle_bind_system_event(void *ev, int autoclear) {""")
    text = replace(text, '    uint32_t r = create(ev, autoclear, 1);\n    if (r) die("CreateSystemEvent failed %#x", r);\n    wh = getw(ev);', """    satchel_se_call call = {ev, autoclear, create, getw, 0, 0};
    if (satchel_session_active()) { if (!satchel_session_call_guest(satchel_se_guest, &call)) return; }
    else satchel_se_guest(&call);
    if (call.result) die("CreateSystemEvent failed %#x", call.result);
    wh = call.handle;""")
    return text


def cheats(text):
    text = replace(text, '        atexit(dm_cheats_exit);',
        '        static int installed; if (!installed) { atexit(dm_cheats_exit); installed = 1; }')
    return reset(text, 'cheats', """    // Cheat workers are deliberately excluded by prepare preflight. Do not
    // release their guest alias if a caller bypassed that contract.
    if (g_tick_started || g_alias || g_ncheat || atomic_load(&g_active)) return EBUSY;
    g_main_mod = NULL; g_dir[0] = g_path[0] = g_build[0] = g_tid[0] = 0;
    g_non = g_nothers = g_nskip = g_nsrc = g_ncand = 0;
    g_report[0] = g_version[0] = g_carried[0] = g_meta_src[0] = 0;
    free(g_changed); g_changed = NULL; g_nchanged = g_changed_state = 0;
    atomic_store(&g_exiting, 0); atomic_store(&g_present_ns, 0);
    atomic_store(&g_master, 1); g_safe = 0;""")
