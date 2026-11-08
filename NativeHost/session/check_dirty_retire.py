#!/usr/bin/env python3
"""Exercise pinned dirty-range growth and safe reclamation across sessions."""
import os
from pathlib import Path
import argparse
import runpy
import subprocess
DEPENDENCY_PATHS = runpy.run_path(str(Path(__file__).resolve().parents[2] / 'scripts/dependency_paths.py'))

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline-ref', help='Also reproduce retained arrays from this git revision')
args = parser.parse_args()
ENGINE = DEPENDENCY_PATHS['deps_root']() / 'NX2Apple/engine'
OUT = Path(os.environ.get('SESSION_CHECK_OUT', Path.home() / 'SatchelBuild/session')) / 'soak-dirty-check'
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'NXNative-LICENSE.txt').write_bytes((ENGINE / 'LICENSE').read_bytes())
runpy.run_path(str(HERE.parents[1] / 'NativeApp/build_support.py'))['wait']()
versions = []
if args.baseline_ref:
    base = {'__file__': str(HERE / 'dirty_overlay.py')}
    exec(compile(subprocess.check_output(['git', 'show', args.baseline_ref + ':NativeHost/session/dirty_overlay.py'], text=True),
                 '<baseline dirty overlay>', 'exec'), base)
    versions.append(('before', base))
versions.append(('after', runpy.run_path(str(HERE / 'dirty_overlay.py'))))
source_dir = OUT / 'dm/gfx/nvn'; source_dir.mkdir(parents=True, exist_ok=True)
(OUT / 'dm/host').mkdir(exist_ok=True)
(OUT / 'dm/host/dm.h').write_text('''#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#define LOG(...) ((void)fprintf(stderr, __VA_ARGS__))
#define die(...) do { LOG(__VA_ARGS__); abort(); } while (0)
void dm_fault_hook_register(int (*)(void *, int, void *));
void dm_guest_write_prep_register(void (*)(void *, size_t));
''')
(source_dir / 'nvn_dirty.h').write_bytes((ENGINE / 'dm/gfx/nvn/nvn_dirty.h').read_bytes())
tracker = '''
static void *tracked[4096];
static unsigned tracked_live;
static void remember(void *p) {
    assert(p);
    for (unsigned i = 0; i < 4096; i++) if (!tracked[i]) { tracked[i] = p; tracked_live++; return; }
    abort();
}
static void *tracked_malloc(size_t n) { void *p = malloc(n); remember(p); return p; }
static void *tracked_calloc(size_t n, size_t s) { void *p = calloc(n, s); remember(p); return p; }
static void tracked_free(void *p) {
    if (!p) return;
    for (unsigned i = 0; i < 4096; i++) if (tracked[i] == p) {
        tracked[i] = NULL; tracked_live--; free(p); return;
    }
    abort();
}
#define malloc tracked_malloc
#define calloc tracked_calloc
#define free tracked_free
'''
fixture = '''
static int quiescent;
static int (*fault_hook)(void *, int, void *);
void dm_fault_hook_register(int (*fn)(void *, int, void *)) { fault_hook = fn; }
void dm_guest_write_prep_register(void (*fn)(void *, size_t)) { (void)fn; }
int satchel_session_quiescent(void) { return quiescent; }
int main(void) {
    size_t page = (size_t)getpagesize();
    char *mem = mmap(NULL, page * 8, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(mem != MAP_FAILED);
    for (int cycle = 0; cycle < 5; cycle++) {
        for (int n = 1; n <= 8; n *= 2) {
            nvn_dirty_add_range(mem, page * (size_t)n);
            assert(nvn_dirty_tracked(mem, page * (size_t)n));
            nvn_dirty_protect(mem, page * (size_t)n);
            assert(fault_hook(mem, 1, NULL));
            mem[0] = (char)n;
            if (n < 8) nvn_dirty_remove_range(mem);
        }
        unsigned retained = tracked_live;
        quiescent = 0;
        assert(satchel_dirty_session_reset() == EBUSY && tracked_live == retained);
        quiescent = 1; atomic_store(&satchel_dirty_readers, 1);
        assert(satchel_dirty_session_reset() == EBUSY && tracked_live == retained);
        atomic_store(&satchel_dirty_readers, 0);
        assert(!satchel_dirty_session_reset());
        mem[page * 7] = 1;
        printf("[dirty-retire] cycle=%d remaining_allocations=%u live_ranges=%d\\n", cycle, tracked_live, (int)g_live);
        if (tracked_live || g_live || g_nr) return 1;
    }
    assert(!munmap(mem, page * 8));
    return 0;
}
'''
for label, overlay in versions:
    source = source_dir / (label + '.c')
    overlay['prepare'](ENGINE / 'dm/gfx/nvn/nvn_dirty.c', source)
    text = source.read_text().replace('#include <sched.h>\n', '#include <sched.h>\n' + tracker)
    # The baseline has no retired-list node allocation, so keep this wrapper used.
    text += fixture.replace('    size_t page =', '    free(malloc(1));\n    size_t page =')
    source.write_text(text)
    binary = OUT / label
    subprocess.run(['nice', '-n', '10', 'xcrun', 'clang', '-O1', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(HERE), str(source), '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)], timeout=5)
    assert result.returncode == (1 if label == 'before' else 0), (label, result.returncode)
    print('PASS: ' + label + ' dirty range lifetime behavior', flush=True)
