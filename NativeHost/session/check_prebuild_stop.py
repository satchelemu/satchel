#!/usr/bin/env python3
"""Exercise the pinned prebuild loop and completion prefix with synthetic pairs."""
from pathlib import Path
import runpy
import subprocess

HERE = Path(__file__).resolve().parent
OUT = Path.home() / 'SatchelBuild/session/soak-prebuild-check'
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'NXNative-LICENSE.txt').write_bytes((Path.home() / 'SatchelDeps/NX2Apple/engine/LICENSE').read_bytes())
runpy.run_path(str(HERE.parents[1] / 'NativeApp/build_support.py'))['wait']()
source = (Path.home() / 'SatchelDeps/NX2Apple/engine/dm/gfx/nvn/nvn_mtl.m').read_text()
fixed = runpy.run_path(str(HERE / 'graphics_overlay.py'))['metal'](source)
prefix = '''#import <Foundation/Foundation.h>
#include <stdatomic.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
static _Atomic int satchel_present_stop, g_pre_next, g_pre_done, g_arch_new;
static _Atomic int g_warm_running;
static int calls, cancel, stamp, g_pre_running;
#define MLOG(...) ((void)0)
static uint64_t bp_now(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
}
int mtl_warm_wait(int timeout_ms);
static void arch_flush(void) {}
static void arch_flush_wait(void) {}
static void pre_pair(id vertex, id fragment, int zero) {
    (void)vertex; (void)fragment; (void)zero;
    calls++;
    // Model a stop arriving during the first in-flight compiler operation.
    if (cancel) atomic_store(&satchel_present_stop, 1);
}
static void exercise(int stopping) {
    cancel = stopping; calls = stamp = 0; g_pre_running = 1;
    atomic_store(&satchel_present_stop, 0); atomic_store(&g_pre_next, 0);
    atomic_store(&g_pre_done, 0);
    int n = 8, zero = 0;
    NSMutableArray *pairs = [NSMutableArray array];
    for (int i = 0; i < n; i++) [pairs addObject:@[@"synthetic-v", @"synthetic-f"]];
'''
suffix = '''
    stamp = 1; g_pre_running = 0;
    };
    done();
}
int main(void) { @autoreleasepool {
    exercise(0);
    if (calls != 8 || stamp != 1 || g_pre_running) return 2;
    exercise(1);
    printf("[prebuild-stop-check] pairs_after_stop=%d completed_stamp=%d running=%d\\n",
           calls - 1, stamp, g_pre_running);
    atomic_store(&g_warm_running, 1);
    int waited = mtl_warm_wait(200);
    printf("[prebuild-stop-check] cancelled_warm_wait_ms=%d\\n", waited);
    return calls != 1 || stamp || g_pre_running || waited > 50;
} }
'''
for label, text in [('before', source), ('after', fixed)]:
    first = text.index('        for (;;) {\n            ', text.index('static int pso_prebuild('))
    last = text.index('\n    });\n    dispatch_block_t done = ^{', first)
    completion = text.index('    dispatch_block_t done = ^{', last)
    end = text.index('        NSMutableArray<NSString *> *fails', completion)
    path, binary = OUT / (label + '.m'), OUT / label
    wait_first = text.index('int mtl_warm_wait(int timeout_ms) {')
    wait_last = text.index('\n}', wait_first) + 2
    path.write_text(prefix + text[first:last] + '\n' + text[completion:end] + suffix +
                    '\n' + text[wait_first:wait_last] + '\n')
    subprocess.run(['nice', '-n', '10', 'xcrun', 'clang', '-O1', '-fobjc-arc', '-Wall',
                    '-Wextra', '-Werror', str(path), '-framework', 'Foundation', '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)], timeout=5)
    assert result.returncode == (1 if label == 'before' else 0), (label, result.returncode)
    print('PASS: ' + label + ' prebuild cancellation behavior', flush=True)
