#!/usr/bin/env python3
"""Exercise the pinned pipeline structs with the real typed retirement helper."""
from pathlib import Path
import re
import runpy
import subprocess

HERE = Path(__file__).resolve().parent
OUT = Path.home() / 'SatchelBuild/session/soak-pipeline-check'
ENGINE = Path.home() / 'SatchelDeps/NX2Apple/engine/dm/gfx/nvn/nvn_mtl.m'
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
