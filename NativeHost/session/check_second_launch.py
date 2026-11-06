#!/usr/bin/env python3
"""A game started after another one in the same process must keep the host's big stacks.

Phone evidence (2026-11-02): after Mario Kart quit, Odyssey's first draw ran Metal and
Foundation code on its own small guest stack, overflowed it, the crash report faulted
reading below that stack, and the app froze on Mario Kart's last frame. This checks the
three generated pieces behind that: the NVN dispatch guard, the crash gate alarm, and a
stack scan that survives a stack at the edge of an unmapped page.

Phone evidence (2026-11-05): Quit in Castlevania aborted the app in
satchel_os_resources_session_reset. The engine frees its game_env copy when the profile's
game_env is empty, and the session reset freed it again. Part 4 runs the generated getenv
table and the reset over two sessions; --baseline-ref REV also runs that revision's
services overlay, which must fail.
"""
import argparse
import os
from pathlib import Path
import re
import runpy
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
ENGINE = Path(os.environ.get('SATCHEL_DEPS_ROOT', Path.home() / 'SatchelDeps')) / 'NX2Apple/engine'
OUT = Path(os.environ.get('SESSION_SECOND_OUT', Path.home() / 'SatchelBuild/mkdebug/second-launch-check'))
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline-ref', help='Also reproduce the game_env double free from this git revision')
args = parser.parse_args()
OUT.mkdir(parents=True, exist_ok=True)
assert subprocess.check_output(['git', '-C', ENGINE, 'rev-parse', 'HEAD'], text=True).strip() == 'f8a5618d5f505a4c0a0bcac7c413872d3a53c009'

# 1. The slot table is filled once per process. Its constructor then wraps entries with
# big-stack and profiling thunks. A guard reset at session end refills the raw table on
# the next title's first lookup and drops those wrappers.
nvn = runpy.run_path(str(HERE / 'nvn_overlay.py'))['prepare'](ENGINE / 'dm/gfx/nvn/hle_nvn.c', OUT / 'hle_nvn.c').read_text()
body = nvn[nvn.index('static void nvn_init_slots(void) {'):]
body = body[:body.index('\n}\n')]
assert not re.search(r'static int \w+; SATCHEL_STATE\(', body), 'nvn_init_slots guard is reset with the session: ' + body.splitlines()[1]
assert 'if (satchel_nvn_slots_ready) return; satchel_nvn_slots_ready = 1;' in body, 'nvn_init_slots has no process-owned guard'
assert not re.search(r'satchel_nvn_slots_ready\s*=\s*0', nvn), 'the process-owned NVN slot guard is cleared'
assert 'nvn_big_entry' in nvn, 'the engine no longer installs big-stack NVN wrappers; review this check'
print('PASS: NVN dispatch slots are initialized once per process, so big-stack wrappers survive a session', flush=True)

# 2. A fatal report owns the crash gate; its alarm must end the process on any thread.
crash = runpy.run_path(str(HERE / 'overlay.py'))['bound_crash_report']((ENGINE / 'dm/host/crash.c').read_text())
assert 'if (sig == SIGALRM && atomic_load(&g_crash_owner)) _exit(128 + sig);' in crash
print('PASS: the crash gate alarm exits even when it lands on the UI thread', flush=True)

# 3. Run the engine's stack scan, before and after the overlay, with sp 0x30 bytes above
# an unmapped page, as on the phone (sp 0x712be60030, fault 0x712be5ff30).
raw = (ENGINE / 'dm/host/crash.c').read_text()
safe = runpy.run_path(str(HERE / 'crash_overlay.py'))['safe_stack_scan'](raw)
prefix = '''#include <dlfcn.h>
#include <mach/mach.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/ucontext.h>
#include <unistd.h>
typedef struct { void *base; uint64_t size; const char *name; } module_t;
static module_t *g_mod; static int g_nmod;
static void scan(ucontext_t *uc) {
'''
fixture = '''
}
int main(void) {
    size_t page = (size_t)getpagesize();
    uint8_t *map = mmap(NULL, 2 * page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (map == MAP_FAILED || mprotect(map, page, PROT_NONE)) return 64;
    struct __darwin_mcontext64 mc = {0}; ucontext_t uc = {0}; uc.uc_mcontext = &mc;
    mc.__ss.__sp = (uint64_t)(uintptr_t)(map + page + 0x30);
    scan(&uc);
    return 0;
}
'''
for label, text in [('before', raw), ('after', safe)]:
    first = text.index('    {   uint64_t sp = uc->uc_mcontext->__ss.__sp; int n = 0;')
    last = text.index('    if (g_dump_hook) g_dump_hook();', first)
    path = OUT / ('scan-' + label + '.c')
    path.write_text(prefix + text[first:last] + fixture)
    binary = OUT / ('scan-' + label)
    subprocess.run(['nice', '-n', '10', 'xcrun', 'clang', '-O1', '-Wall', '-Werror', str(path), '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)], timeout=10, capture_output=True, text=True)
    if label == 'before':
        assert result.returncode in (-10, -11), ('the original scan should fault', result.returncode)
        print('PASS: the original stack scan faults at an unmapped page below sp (signal %d)' % -result.returncode, flush=True)
    else:
        assert result.returncode == 0 and 'stack scan sp=' in result.stderr, (result.returncode, result.stderr[-300:])
        print('PASS: the overlaid stack scan reads past the unmapped page and finishes', flush=True)

# 4. Stop after the game asked getenv with an empty game_env, then a second session, under
# AddressSanitizer. The pieces are cut from the generated hle_os.c: the object map, the
# game_env table and the os resources reset.
services = [('after', runpy.run_path(str(HERE / 'services_overlay.py')))]
if args.baseline_ref:
    original = subprocess.check_output(['git', '-C', ROOT, 'show', args.baseline_ref + ':NativeHost/session/services_overlay.py'], text=True)
    baseline = {'__file__': str(HERE / 'services_overlay.py'), '__name__': 'baseline_services_overlay'}
    exec(compile(original, '<baseline services overlay>', 'exec'), baseline)
    services.insert(0, ('before', baseline))
env_prefix = r"""#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define LOG(...) ((void)0)
static void die(const char *message) { fprintf(stderr, "die: %s\n", message); abort(); }
static struct { char genv[1024]; } profile;
static void *dm_profile_value(void) { return &profile; }
#define dm_profile() ((__typeof__(&profile))dm_profile_value())
int satchel_session_quiescent(void) { return 1; }
static void *(*g_sa_alloc)(void *, size_t), *(*g_sa_alloc_al)(void *, size_t, size_t);
static int g_sa_big_align, satchel_sa_ready;
"""
env_main = r"""
static const char *lookup(const char *name) {
    size_t n = strlen(name);
    for (const char *const *p = game_env(); *p; p++) if (!strncmp(*p, name, n) && (*p)[n] == '=') return *p + n + 1;
    return NULL;
}
static int session(const char *env, int ask) {
    snprintf(profile.genv, sizeof profile.genv, "%s", env);
    if (ask) {
        const char *value = lookup("DOTNET_GCRegionRange"), *want = strstr(env, "DOTNET_GCRegionRange=") ? "0x40000000" : NULL;
        if (want ? !value || strcmp(value, want) : value != NULL) return 3;
    }
    int rc = satchel_os_resources_session_reset();
    printf("[env-stop-check] game_env=\"%s\" asked=%d reset=%d\n", env, ask, rc); fflush(stdout);
    return rc || g_env_kv || satchel_env_storage ? 2 : 0;
}
int main(void) {
    unsetenv("DM_GAME_ENV");
    // Castlevania: getenv with an empty game_env, Quit, then another game in the same process.
    int rc = session("", 1); if (rc) return rc;
    if ((rc = session("", 1))) return rc;
    if ((rc = session("DOTNET_GCRegionRange=0x40000000 A=1", 1))) return rc;
    if ((rc = session("", 1))) return rc;
    return session("NOEQUALS", 1) || session("", 0);
}
"""
raw_os = (ENGINE / 'dm/host/hle_os.c').read_text()
for label, overlay in services:
    text = overlay['common'](raw_os)
    pieces = [text[text.index('#define OM_SIZE'):text.index('uint64_t now_ns(void) {')],
              text[text.index('static char *satchel_env_storage;'):text.index('HLE_FORCE("getenv"')],
              text[text.index('int satchel_os_resources_session_reset(void) {'):]]
    path = OUT / ('env-stop-' + label + '.c')
    path.write_text(env_prefix + ''.join(pieces) + env_main)
    binary = OUT / ('env-stop-' + label)
    subprocess.run(['nice', '-n', '10', 'xcrun', 'clang', '-g', '-O1', '-Wall', '-Werror', '-Wno-unused-function',
                    '-fsanitize=address', str(path), '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)], timeout=30, capture_output=True, text=True)
    if label == 'before':
        assert result.returncode != 0 and 'double-free' in result.stderr, ('the baseline overlay should double free', result.returncode, result.stderr[-600:])
        print('PASS: the baseline services overlay frees an empty game_env twice at stop (AddressSanitizer: double-free)', flush=True)
    else:
        assert result.returncode == 0, (result.returncode, result.stdout[-600:], result.stderr[-1500:])
        print(result.stdout, end='')
        print('PASS: stop after getenv with an empty game_env, and later sessions, free the game_env copy once', flush=True)
