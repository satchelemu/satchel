#!/usr/bin/env python3
"""A game started after another one in the same process must keep the host's big stacks.

Phone evidence (2026-11-02): after Mario Kart quit, Odyssey's first draw ran Metal and
Foundation code on its own small guest stack, overflowed it, the crash report faulted
reading below that stack, and the app froze on Mario Kart's last frame. This checks the
three generated pieces behind that: the NVN dispatch guard, the crash gate alarm, and a
stack scan that survives a stack at the edge of an unmapped page.
"""
import os
from pathlib import Path
import re
import runpy
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
ENGINE = Path.home() / 'SatchelDeps/NX2Apple/engine'
OUT = Path(os.environ.get('SESSION_SECOND_OUT', Path.home() / 'SatchelBuild/mkdebug/second-launch-check'))
OUT.mkdir(parents=True, exist_ok=True)
assert subprocess.check_output(['git', '-C', ENGINE, 'rev-parse', 'HEAD'], text=True).strip() == 'bbc3b73784751d2a706932dc69fd67627e3445d4'

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
