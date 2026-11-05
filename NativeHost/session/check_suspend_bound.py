#!/usr/bin/env python3
"""The UI thread's graphics suspend must not wait for a submission that is building shaders.

Runs the real gate and dm_gfx_suspend from the pinned engine, before and after
suspend_overlay.py: a submission holds the gate for 2 s (as Mario Kart's first-use
shader builds do, for up to 20 s) while the main thread suspends graphics.
"""
import os
from pathlib import Path
import runpy
import subprocess

HERE = Path(__file__).resolve().parent
ENGINE = Path(os.environ.get('SATCHEL_DEPS_ROOT', Path.home() / 'SatchelDeps')) / 'NX2Apple/engine'
OUT = Path(os.environ.get('SESSION_SUSPEND_OUT', Path.home() / 'SatchelBuild/mkdebug/suspend-check'))
OUT.mkdir(parents=True, exist_ok=True)
assert subprocess.check_output(['git', '-C', ENGINE, 'rev-parse', 'HEAD'], text=True).strip() == 'f8a5618d5f505a4c0a0bcac7c413872d3a53c009'

raw = (ENGINE / 'dm/gfx/nvn/hle_nvn.c').read_text()
fixed = runpy.run_path(str(HERE / 'suspend_overlay.py'))['bounded_suspend'](raw)
prefix = '''#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#define LOG(...) (fprintf(stderr, __VA_ARGS__), fputc('\\n', stderr))
static _Atomic int drains; static pthread_t drained_on;
void mtl_drain(void) { atomic_fetch_add(&drains, 1); drained_on = pthread_self(); }
'''
fixture = '''
static uint64_t ms(void) { return clock_gettime_nsec_np(CLOCK_MONOTONIC) / 1000000; }
static _Atomic int inside, second_in;
static pthread_t late;
static void *submission(void *unused) { (void)unused; gfx_enter(); atomic_store(&inside, 1); usleep(2000000); gfx_leave(); return NULL; }
static void *next_call(void *unused) { (void)unused; gfx_enter(); atomic_store(&second_in, 1); gfx_leave(); return NULL; }
int main(void) {
    pthread_create(&late, NULL, submission, NULL);
    while (!atomic_load(&inside)) usleep(1000);
    uint64_t t0 = ms(); dm_gfx_suspend(1); uint64_t waited = ms() - t0;
    printf("suspend returned after %llu ms\\n", (unsigned long long)waited);
    if (waited > 1000) return 10;
    pthread_t other; pthread_create(&other, NULL, next_call, NULL);
    pthread_join(late, NULL);
    usleep(100000);
    if (atomic_load(&drains) != 1 || !pthread_equal(drained_on, late)) return 11;   // the late submission drained, once
    if (atomic_load(&second_in)) return 12;                                         // no new call while suspended
    dm_gfx_suspend(0); pthread_join(other, NULL);
    if (!atomic_load(&second_in)) return 13;
    dm_gfx_suspend(1); if (atomic_load(&drains) != 2) return 14;                    // an idle suspend drains at once
    dm_gfx_suspend(0);
    return 0;
}
'''
for label, text in [('before', raw), ('after', fixed)]:
    first = text.index('static pthread_mutex_t g_susp_mu')
    last = text.index('\n// One submission at a time', first)
    path = OUT / (label + '.c')
    path.write_text(prefix + text[first:last] + fixture)
    binary = OUT / label
    subprocess.run(['nice', '-n', '10', 'xcrun', 'clang', '-O1', '-Wall', '-Werror', '-Wno-unused-function',
                    str(path), '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)], timeout=20, capture_output=True, text=True)
    if label == 'before':
        assert result.returncode == 10, ('the engine suspend should wait for the whole submission', result.returncode, result.stdout)
        print('PASS: the engine suspend waits for the whole submission (%s)' % result.stdout.strip(), flush=True)
    else:
        assert result.returncode == 0, ('bounded suspend failed', result.returncode, result.stdout, result.stderr[-400:])
        print('PASS: the bounded suspend returns at once (%s), keeps new calls out, and the late call drains once' % result.stdout.strip(), flush=True)
