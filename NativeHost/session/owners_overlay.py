"""Session ownership hooks, applied after display and media overlays.

Exact replacements fail on upstream drift. Graphics policy and pipeline archive
implementation remain in their original source; this overlay owns lifetimes.
"""
from pathlib import Path


def replace(text, old, new):
    assert text.count(old) == 1, old
    return text.replace(old, new)


def hoist(text, signature, declaration, names):
    """Move a function's static caches to file scope under satchel_ names, so a reset can reach them.
    declaration is the function's static declaration line; names maps each cache to (file-scope name, C declaration)."""
    import re
    start = text.index(signature)
    brace = text.index('{', start)
    depth, end = 0, None
    for token in re.finditer(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[{}]', text[brace:]):
        if token[0] == '{': depth += 1
        elif token[0] == '}':
            depth -= 1
            if not depth: end = brace + token.end(); break
    assert end is not None, signature
    body = text[brace:end]
    assert body.count(declaration) == 1, declaration
    body = body.replace(declaration, '')
    for old, (new, _) in names.items():
        body, count = re.subn(r'\b' + old + r'\b', new, body)
        assert count, old
    return text[:start] + ''.join(c + '\n' for _, c in names.values()) + text[start:brace] + body + text[end:]


def control(text):
    text = '#include "SessionInputGate.h"\n' + text
    import re
    for name in ['dm_input_set', 'dm_pad_set_player', 'dm_pad_players', 'dm_motion_set',
                 'dm_motion_set_switch', 'dm_motion_set_hid', 'dm_motion_clear', 'dm_motion_clear_hid']:
        pattern = r'(void ' + name + r'\([^{};]*\) \{)'
        text, count = re.subn(pattern, r'\1 SATCHEL_INPUT_SCOPE();', text)
        assert count == 1, name

    text = replace(text, '#include "motion_fusion.h"', '''#include "motion_fusion.h"
#include "SessionBoundary.h"
#include "SessionNativeWorkers.h"
#include <errno.h>
static SatchelNativeWorkers satchel_hid_workers = SATCHEL_NATIVE_WORKERS_INIT;
static void satchel_hid_sleep_us(uint64_t us) {
    while (us && !satchel_native_stopping(&satchel_hid_workers)) {
        unsigned chunk = us > 100000 ? 100 : (unsigned)((us + 999) / 1000);
        if (satchel_native_wait(&satchel_hid_workers, chunk)) break;
        us = us > (uint64_t)chunk * 1000 ? us - (uint64_t)chunk * 1000 : 0;
    }
}
''')
    text = replace(text, 'for (int i = 0; i < g_nsteps; i++) {',
                   'for (int i = 0; i < g_nsteps && !satchel_native_stopping(&satchel_hid_workers); i++) {')
    text = replace(text, 'for (;;) { struct timespec t;',
                   'for (;;) { if (satchel_native_stopping(&satchel_hid_workers)) return NULL; struct timespec t;')
    text = replace(text, 'while (dm_guest_frame() < end) usleep(5000);',
                   'while (dm_guest_frame() < end && !satchel_native_stopping(&satchel_hid_workers)) satchel_hid_sleep_us(5000);')
    text = replace(text, 'pthread_t th; if (g_nsteps && !pthread_create(&th, NULL, keys_main, NULL)) { pthread_detach(th);',
                   'if (g_nsteps && !satchel_native_create(&satchel_hid_workers, NULL, keys_main, NULL)) {')
    text = replace(text, '    for (;;) {\n        FILE *f = fopen(path, "r");\n        if (f && !fseek(f, off, SEEK_SET)) {',
                   '    while (!satchel_native_stopping(&satchel_hid_workers)) {\n        FILE *f = fopen(path, "r");\n        if (f && !fseek(f, off, SEEK_SET)) {')
    text = replace(text, 'while (fgets(line, sizeof line, f)) {',
                   'while (!satchel_native_stopping(&satchel_hid_workers) && fgets(line, sizeof line, f)) {')
    text = replace(text, 'for (int i = 0; i <= n; i++) { dm_touch t',
                   'for (int i = 0; i <= n && !satchel_native_stopping(&satchel_hid_workers); i++) { dm_touch t')
    text = replace(text, '    return NULL;\n}\nstatic void ctl_start(void)',
                   '    free(arg);\n    return NULL;\n}\nstatic void ctl_start(void)')
    text = replace(text, 'pthread_t th; if (!pthread_create(&th, NULL, ctl_main, strdup(e))) { pthread_detach(th);',
                   'char *path = strdup(e); if (!path) return;\n    int rc = satchel_native_create(&satchel_hid_workers, NULL, ctl_main, path);\n    if (rc) free(path);\n    else {')
    text = replace(text, 'for (;;) { usleep(REC_FLUSH_MS * 1000); rec_flush(); }',
                   'while (!satchel_native_wait(&satchel_hid_workers, REC_FLUSH_MS)) rec_flush();')
    text = replace(text, 'pthread_t th; if (!pthread_create(&th, NULL, rec_main, NULL)) pthread_detach(th);',
                   'if (satchel_native_create(&satchel_hid_workers, NULL, rec_main, NULL)) { atomic_store(&g_rec_on, 0); fclose(g_rec_file); g_rec_file = NULL; return; }')
    text = replace(text, '    atexit(dm_rec_finish);',
                   '    static pthread_once_t once = PTHREAD_ONCE_INIT; pthread_once(&once, satchel_rec_exit_once);')
    text = replace(text, 'static void rec_start(void) {',
                   'static void satchel_rec_exit_once(void) { atexit(dm_rec_finish); }\nstatic void rec_start(void) {')
    text = text.replace('usleep(', 'satchel_hid_sleep_us(')
    text = replace(text, 'static pthread_once_t g_motion_once = PTHREAD_ONCE_INIT;',
                   'static pthread_mutex_t satchel_motion_init_mu = PTHREAD_MUTEX_INITIALIZER;\nstatic int satchel_motion_ready;')
    text = replace(text, 'static void motion_env(void) {',
                   'static void motion_env(void) {\n    g_motion_gain = 0.5f;')
    text = replace(text, 'static void motion_set(int p,', '''static void satchel_motion_init(void) {
    pthread_mutex_lock(&satchel_motion_init_mu);
    if (!satchel_motion_ready) { motion_env(); satchel_motion_ready = 1; }
    pthread_mutex_unlock(&satchel_motion_init_mu);
}
static void motion_set(int p,''')
    assert text.count('pthread_once(&g_motion_once, motion_env);') == 2
    text = text.replace('pthread_once(&g_motion_once, motion_env);', 'satchel_motion_init();')
    text += '\n' + Path(__file__).with_name('SessionHIDReset.inc').read_text()
    return text


def memory(text):
    # Keep the leading SDK include block intact for the host timing overlay.
    text = replace(text, '#include <sched.h>', '''#include <sched.h>
#include "SessionBoundary.h"
#include "SessionNativeWorkers.h"
static SatchelNativeWorkers satchel_memory_workers = SATCHEL_NATIVE_WORKERS_INIT;
static pthread_mutex_t satchel_memory_init_mu = PTHREAD_MUTEX_INITIALIZER;
''')
    text = replace(text, 'if (aw && atoi(aw) > 0) { pthread_t th; if (!pthread_create(&th, NULL, alias_windows_thread, (void *)(uintptr_t)atoi(aw))) pthread_detach(th); }',
                   'if (aw && atoi(aw) > 0) satchel_native_create(&satchel_memory_workers, NULL, alias_windows_thread, (void *)(uintptr_t)atoi(aw));')
    text = replace(text, 'if (rf && *rf) { pthread_t th; if (!pthread_create(&th, NULL, regions_thread, (void *)rf)) pthread_detach(th); }',
                   'if (rf && *rf) { char *path = strdup(rf); if (path && satchel_native_create(&satchel_memory_workers, NULL, regions_thread, path)) free(path); }')
    text = replace(text, '    static pthread_once_t once = PTHREAD_ONCE_INIT;\n    pthread_once(&once, mem_init_once);',
                   '    pthread_mutex_lock(&satchel_memory_init_mu);\n    if (!g_as) mem_init_once();\n    pthread_mutex_unlock(&satchel_memory_init_mu);')
    text = replace(text, '    for (;;) {\n        sleep(30);',
                   '    while (!satchel_native_wait(&satchel_memory_workers, 30000)) {')
    text = replace(text, '    for (;;) {\n        FILE *f = fopen(tmp, "w");',
                   '    while (!satchel_native_stopping(&satchel_memory_workers)) {\n        FILE *f = fopen(tmp, "w");')
    text = replace(text, '        sleep(2);\n    }\n    return NULL;',
                   '        satchel_native_wait(&satchel_memory_workers, 2000);\n    }\n    free(arg);\n    return NULL;')
    text = replace(text, 'case I_ProgramId: { static uint64_t pid; if (!pid) { const char *e = getenv("DM_PROGRAM_ID"); pid = e ? strtoull(e, NULL, 16) : (dm_profile()->program_id); } v = pid; break; }',
                   'case I_ProgramId: { const char *e = getenv("DM_PROGRAM_ID"); v = e ? strtoull(e, NULL, 16) : dm_profile()->program_id; break; }')
    text += '\n' + Path(__file__).with_name('SessionMemoryReset.inc').read_text()
    return text


def touch(text):
    text = '#include "SessionInputGate.h"\n' + text
    import re
    for name in ['dm_touch_set', 'dm_touch_script']:
        text, count = re.subn(r'(void ' + name + r'\([^{};]*\) \{)', r'\1 SATCHEL_INPUT_SCOPE();', text)
        assert count == 1, name

    text = replace(text, '#include "front.h"',
                   '#include "front.h"\n#include "SessionBoundary.h"\n#include <errno.h>')
    # Preserve the mutex. No producer may enter after its owner closes admission.
    text += '\n' + Path(__file__).with_name('SessionTouchReset.inc').read_text()
    return text


def loader(text):
    text = replace(text, '#include <mach-o/ldsyms.h>',
                   '#include <mach-o/ldsyms.h>\n#include "SessionBoundary.h"')
    # The app clears the image after dlclose: the engine's setter then names the executable again.
    text = replace(text, 'void dm_set_game_image_header(const void *mh) { g_game_image = mh; }',
                   'void dm_set_game_image_header(const void *mh) { g_game_image = mh ? mh : (const void *)&_mh_execute_header; }')
    text = replace(text, '    create_keys();',
                   '    static pthread_once_t keys_once = PTHREAD_ONCE_INIT; pthread_once(&keys_once, create_keys);')
    text = replace(text, 'int dm_sdk_major(void) {\n    static int major = -1;',
                   'static int satchel_sdk_major = -1;\nint dm_sdk_major(void) {\n#define major satchel_sdk_major')
    text = replace(text, '    return major;\n}', '    return major;\n#undef major\n}')
    # The same for the SDK generation, the NVN2 constant table and the JIT image folder of the f8a5618d pin.
    text = hoist(text, 'int dm_sdk_ounce(void) {', '    static int ounce = -1;\n',
                 {'ounce': ('satchel_sdk_ounce', 'static int satchel_sdk_ounce = -1;')})
    text = hoist(text, 'int dm_sdk_nvn2_item(unsigned item, int *out) {', ' static int found = -1;',
                 {'found': ('satchel_nvn2_found', 'static int satchel_nvn2_found = -1;')})
    text = hoist(text, 'const char *dm_jit_serve_dir(void) {', '    static int init; static char dir[1024];\n',
                 {'init': ('satchel_jit_dir_init', 'static int satchel_jit_dir_init;'), 'dir': ('satchel_jit_dir', 'static char satchel_jit_dir[1024];')})
    text = replace(text, 'static void map_image(module_t *m, const char *dir);',
                   'static void map_image(module_t *m, const char *dir);\nstatic void satchel_loader_ranges(module_t *m);')
    text = replace(text, 'read_layout(m, dir); map_image(m, dir); parse_dynamic(m); register_gates(m, dir);',
                   'read_layout(m, dir); map_image(m, dir); satchel_loader_ranges(m); parse_dynamic(m); register_gates(m, dir);')
    text = replace(text, 't = now_ns(); map_image(m, dir); tm += now_ns() - t;',
                   't = now_ns(); map_image(m, dir); satchel_loader_ranges(m); tm += now_ns() - t;')
    text = replace(text, 'void run_module_inits(void) {', '''static __attribute__((no_instrument_function)) void satchel_module_init(void *argument) {
    module_t *m = argument;
    ((void (*)(void))(m->base + m->init))();
}
void run_module_inits(void) {''')
    # Only the original call in the loop, after insertion of the trampoline.
    text = replace(text, '        ((void (*)(void))(m->base + m->init))();',
                   '        if (satchel_session_active()) { if (!satchel_session_call_guest(satchel_module_init, m)) return; }\n        else satchel_module_init(m);')
    text += '\n' + Path(__file__).with_name('SessionLoaderReset.inc').read_text()
    return text


def gates(text):
    text = replace(text, '#include "hle.h"', '#include "hle.h"\n#include <errno.h>')
    text = replace(text, '    static unsigned *stub_idx, stub_mask;', '')
    text = replace(text, 'static unsigned g_nstub;',
                   'static unsigned g_nstub;\nstatic unsigned *stub_idx, stub_mask;')
    text += '''
int satchel_gate_session_reset(void) {
    if (!satchel_session_quiescent()) return EBUSY;
    free(stub_idx); stub_idx = NULL; stub_mask = 0;
    g_nstub = 0; memset(g_stub_name, 0, sizeof g_stub_name); memset(g_stub_seen, 0, sizeof g_stub_seen);
    g_ngate = 0; memset(g_gate, 0, sizeof g_gate); memset(g_obs_have, 0, sizeof g_obs_have);
    // g_obs_fn and the swhle hash contain process code only. Keep registrations.
    return 0;
}
'''
    return text


def bootstrap(text):
    text = replace(text, '#include "front.h"', '#include "front.h"\n#include "SessionBoundary.h"\n#include <errno.h>')
    text = replace(text, 'static char *g_argv[64]', 'static char *satchel_argument_storage;\nstatic char *g_argv[64]')
    text = replace(text, 'char *s = strdup(prof->args);', 'char *s = satchel_argument_storage = strdup(prof->args);')
    begin = text.index('static void guest_main(void) {')
    guest = text.index('    // Copy Start step by step:', begin)
    end = text.index('\n}\n', guest)
    setup = text[begin:guest]
    setup = setup.replace('static void guest_main(void) {', '')
    body = text[guest:end]
    replacement = '''static __attribute__((no_instrument_function)) void satchel_boot_guest(void *argument) {
    uint32_t hmain = (uint32_t)(uintptr_t)argument;
''' + body + '''
}
static void guest_main(void) {''' + setup + '''
    if (satchel_session_active()) satchel_session_run_guest(satchel_boot_guest, (void *)(uintptr_t)hmain);
    else satchel_boot_guest((void *)(uintptr_t)hmain);
}'''
    text = text[:begin] + replacement + text[end + 2:]
    text += '''
// The coordinator owns begin and the boot pthread. Loading runs before any
// root guest scope seals the executable ranges, including preloaded NROs.
void satchel_host_session_load(int argc, char **argv) { host_preload(argc, argv); }
int satchel_bootstrap_session_reset(void) {
    if (!satchel_session_quiescent()) return EBUSY;
    free(satchel_argument_storage); satchel_argument_storage = NULL;
    memset(g_argv, 0, sizeof g_argv); g_argv[0] = "game"; g_argc = 1;
    memset(&g_flag, 0, sizeof g_flag); g_t0_ns = 0;
    return 0;
}
void *satchel_host_session_enter(void *unused) {
    (void)unused; void crash_thread_init(void); crash_thread_init();
    guest_main(); satchel_kernel_thread_exit(); return NULL;
}
'''
    return text


def prepare(host, destination, overrides=None):
    destination.mkdir(parents=True, exist_ok=True)
    for name, transform in [('hle_hid.c', control), ('hle_touch.c', touch), ('hle_svc.c', memory),
                            ('loader.c', loader), ('main.c', bootstrap)]:
        source = (overrides or {}).get(name, host / name)
        (destination / name).write_text(transform(source.read_text()))
    gate = destination / 'hle.c'
    if gate.exists(): gate.write_text(gates(gate.read_text()))
    return destination
