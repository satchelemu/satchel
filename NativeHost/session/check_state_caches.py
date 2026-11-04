#!/usr/bin/env python3
"""Exercise real scalar restoration and ARC retirement after cache rewriting."""
import argparse
from pathlib import Path
import runpy
import subprocess

HERE = Path(__file__).resolve().parent
OUT = Path.home() / 'SatchelBuild/session/soak-state-check'
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--baseline-ref', help='Also reproduce the defect from this git revision')
args = parser.parse_args()
OUT.mkdir(parents=True, exist_ok=True)
runpy.run_path(str(HERE.parents[1] / 'NativeApp/build_support.py'))['wait']()
versions = [('after', runpy.run_path(str(HERE / 'state_overlay.py')))]
if args.baseline_ref:
    original = subprocess.check_output(['git', 'show', args.baseline_ref + ':NativeHost/session/state_overlay.py'], text=True)
    baseline = {}
    exec(compile(original, '<baseline state overlay>', 'exec'), baseline)
    versions.insert(0, ('before', baseline))
fixture = '''#import <Foundation/Foundation.h>
#include "SessionGraphicsLocal.h"
#include <stdio.h>
// This fixture has no workers or guest mappings.
int satchel_session_quiescent(void) { return 1; }
static int cached_scalar(void) { static int value = 7; return value++; }
static id<NSObject> cached_object(void) {
    static __strong id<NSObject> values[2][2];
    if (!values[0][0]) values[0][0] = [NSObject new];
    return values[0][0];
}
int main(void) {
    @autoreleasepool {
        __strong id direct = [NSObject new];
        __weak id weak_direct = direct;
        satchel_arc_retire((void *)&direct, sizeof direct);
        if (direct || weak_direct) return 3;
        for (int cycle = 0; cycle < 5; cycle++) {
            int initial = cached_scalar();
            __weak id observed;
            @autoreleasepool { observed = cached_object(); }
            if (satchel_state_restore(SATCHEL_STATE_GRAPHICS)) return 2;
            int restored = cached_scalar() == initial;
            int released = observed == nil;
            printf("[state-cache-check] cycle=%d scalar_restored=%d object_released=%d\\n", cycle, restored, released);
            if (!restored || !released) return 1;
            if (satchel_state_restore(SATCHEL_STATE_GRAPHICS)) return 2;
        }
    }
    return 0;
}
'''
state = OUT / 'state.o'
subprocess.run(['nice', '-n', '10', 'xcrun', 'clang', '-O1', '-I' + str(HERE), '-c',
                str(HERE / 'SessionState.c'), '-o', str(state)], check=True)
for label, overlay in versions:
    text = overlay['arc_caches'](overlay['scalar_caches'](fixture, 'SATCHEL_STATE_GRAPHICS'))
    source, binary = OUT / (label + '.m'), OUT / label
    source.write_text(text)
    subprocess.run(['nice', '-n', '10', 'xcrun', 'clang', '-O1', '-fobjc-arc', '-Wall', '-Wextra', '-Werror',
                    '-I' + str(HERE), str(source), str(state), '-framework', 'Foundation', '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)], timeout=5)
    assert result.returncode == (1 if label == 'before' else 0), (label, result.returncode)
    print('PASS: ' + label + ' cache lifetime behavior', flush=True)
