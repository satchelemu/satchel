#!/usr/bin/env python3
"""Exercise the pinned pipeline structs with the real typed retirement helper.

The second part runs the large pool table (g_big) through the engine's own removal and the
retirement's release of its chunk tables, under AddressSanitizer: a removed pool must not be
freed again at stop. --baseline-ref REV also runs that revision's SessionGraphicsState.inc,
which must fail.
"""
import argparse
import os
from pathlib import Path
import re
import runpy
import subprocess

HERE = Path(__file__).resolve().parent
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline-ref', help='Also reproduce the large pool double free from this git revision')
args = parser.parse_args()
OUT = Path(os.environ.get('SESSION_CHECK_OUT', Path.home() / 'SatchelBuild/session')) / 'soak-pipeline-check'
ENGINE = Path(os.environ.get('SATCHEL_DEPS_ROOT', Path.home() / 'SatchelDeps')) / 'NX2Apple/engine/dm/gfx/nvn/nvn_mtl.m'
source = ENGINE.read_text()
state = (HERE / 'SessionGraphicsState.inc').read_text()
helper = re.search(r'static void satchel_mtl_release_pending\(void\) \{[\s\S]*?\n\}', state)[0]
structs = []
registrations = []
for name in ['g_pp', 'g_sm_amp']:
    structs.append(re.search(r'static struct \{[^\n]+\} ' + name + r'\[\w+\];', source)[0])
    registrations.append(re.search(r'SATCHEL_STATE\(SATCHEL_STATE_GRAPHICS, ' + name + r'\);', state)[0])
assert 'satchel_mtl_release_pending();' in state.split('static void satchel_mtl_release_strong(void) {', 1)[1]
fixture = '''#import <Foundation/Foundation.h>
#include "SessionState.h"
#include <stdio.h>
#include <string.h>
// Test doubles retain the pinned fields' ownership without creating Metal resources.
#define MTLRenderPipelineState SatchelPipelineTestProtocol
#define MTLRenderPipelineDescriptor SatchelPipelineTestDescriptor
@protocol MTLRenderPipelineState <NSObject> @end
@interface MTLRenderPipelineDescriptor : NSObject @end
@implementation MTLRenderPipelineDescriptor @end
@interface SatchelPipelineTestObject : NSObject <MTLRenderPipelineState> @end
@implementation SatchelPipelineTestObject @end
typedef struct { unsigned key; } pso_key;
enum { PSO_PEND_MAX = 384, SM_AMP_MAX = 64 };
int satchel_session_quiescent(void) { return 1; }
'''+ '\n'.join(structs) + '\n' + helper + '''
int main(int argc, const char **argv) {
    int typed = argc > 1 && !strcmp(argv[1], "typed");
    ''' + '\n'.join(registrations) + '''
    for (int cycle = 0; cycle < 5; cycle++) {
        __weak id pending, descriptor, amplified;
        @autoreleasepool {
            g_pp[PSO_PEND_MAX - 1].ps = [SatchelPipelineTestObject new];
            g_pp[PSO_PEND_MAX - 1].d = [MTLRenderPipelineDescriptor new];
            g_pp[PSO_PEND_MAX - 1].used = 1;
            g_sm_amp[SM_AMP_MAX - 1].ps = [SatchelPipelineTestObject new];
            g_sm_amp[SM_AMP_MAX - 1].done = 1;
            pending = g_pp[PSO_PEND_MAX - 1].ps;
            descriptor = g_pp[PSO_PEND_MAX - 1].d;
            amplified = g_sm_amp[SM_AMP_MAX - 1].ps;
        }
        @autoreleasepool {
            if (typed) satchel_mtl_release_pending();
            if (satchel_state_restore(SATCHEL_STATE_GRAPHICS)) return 2;
        }
        int released = !pending && !descriptor && !amplified;
        int reset = !g_pp[PSO_PEND_MAX - 1].used && !g_sm_amp[SM_AMP_MAX - 1].done;
        printf("[pipeline-retire] cycle=%d released=%d scalar_reset=%d\\n", cycle, released, reset);
        if (!released || !reset) return 1;
    }
    return 0;
}
'''
OUT.mkdir(parents=True, exist_ok=True)
runpy.run_path(str(HERE.parents[1] / 'NativeApp/build_support.py'))['wait']()
(OUT / 'fixture.m').write_text(fixture)
subprocess.run(['nice', '-n', '10', 'xcrun', 'clang', '-O1', '-I' + str(HERE), '-c',
                str(HERE / 'SessionState.c'), '-o', str(OUT / 'state.o')], check=True)
subprocess.run(['nice', '-n', '10', 'xcrun', 'clang', '-O1', '-fobjc-arc', '-Wall', '-Wextra', '-Werror',
                '-I' + str(HERE), str(OUT / 'fixture.m'), str(OUT / 'state.o'),
                '-framework', 'Foundation', '-o', str(OUT / 'fixture')], check=True)
for mode, expected in [('raw', 1), ('typed', 0)]:
    result = subprocess.run([str(OUT / 'fixture'), mode], timeout=5)
    assert result.returncode == expected, (mode, result.returncode)
    print('PASS: ' + mode + ' pipeline lifetime behavior', flush=True)

# Large pools. mtl_pool_del frees a pool's chunk table and moves the last entry into its
# place; the vacated slot keeps the old copy. The retirement must free each table once.
def big_release(state_text):
    body = state_text.split('static void satchel_mtl_release_strong(void) {', 1)[1]
    return body[body.index('SATCHEL_RETIRE_MARK(330);') + len('SATCHEL_RETIRE_MARK(330);'):body.index('SATCHEL_RETIRE_MARK(329);')]
big_enum = re.search(r'enum \{ BIG_MIN = [^\n]*?\};', source)[0]
big_type = re.search(r'typedef struct \{ uintptr_t lo, hi, a, b; const void \*mem; int \*chunk, full; NSMutableArray \*old; \} big_pool;', source)[0]
big_table = 'static big_pool g_big[BIG_MAX]; static int g_nbig;'
big_remove = 'free(g_big[bg].chunk); g_big[bg] = g_big[--g_nbig];'
assert big_table in source and big_remove in source, 'the engine changed its large pool table; review this check'
versions = [('after', state)]
if args.baseline_ref:
    versions.insert(0, ('before', subprocess.check_output(['git', '-C', HERE.parents[1], 'show', args.baseline_ref + ':NativeHost/session/SessionGraphicsState.inc'], text=True)))
big_main = r'''
static void big_add(void) { g_big[g_nbig++] = (big_pool){0, 0, 0, 0, NULL, calloc(16, sizeof(int)), 0, [NSMutableArray array]}; }
int main(void) {
    for (int cycle = 0; cycle < 3; cycle++) {
        @autoreleasepool {
            big_add(); big_add(); big_add();
            big_del(1);              // a middle pool: the last moves into its place
            big_del(g_nbig - 1);     // the last pool
            release();
        }
        printf("[big-pool-retire] cycle=%d live=%d\n", cycle, g_nbig);
        for (int i = 0; i < BIG_MAX; i++) if (g_big[i].chunk || g_big[i].old) return 1;
    }
    return 0;
}
'''
for label, text in versions:
    fixture = ('#import <Foundation/Foundation.h>\n#include <stdio.h>\n#include <stdlib.h>\n#define SATCHEL_RETIRE_MARK(n) ((void)0)\n'
               + big_enum + '\n' + big_type + '\n' + big_table + '\n'
               + 'static void big_del(int bg) { ' + big_remove + ' }\n'
               + 'static void release(void) {' + big_release(text) + '}\n' + big_main)
    path = OUT / ('big-pool-' + label + '.m')
    path.write_text(fixture)
    binary = OUT / ('big-pool-' + label)
    subprocess.run(['nice', '-n', '10', 'xcrun', 'clang', '-g', '-O1', '-fobjc-arc', '-Wall', '-Werror', '-fsanitize=address',
                    str(path), '-framework', 'Foundation', '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)], timeout=30, capture_output=True, text=True)
    if label == 'before':
        assert result.returncode != 0 and 'double-free' in result.stderr, ('the baseline release should double free', result.returncode, result.stderr[-600:])
        print('PASS: the baseline retirement frees a removed large pool chunk table again (AddressSanitizer: double-free)', flush=True)
    else:
        assert result.returncode == 0, (result.returncode, result.stdout[-600:], result.stderr[-1500:])
        print(result.stdout, end='')
        print('PASS: large pool chunk tables are freed once at stop after the game removed pools', flush=True)
