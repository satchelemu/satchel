#!/usr/bin/env python3
"""Run the pinned engine's crop regressions and screenshot checks on real Metal."""
from pathlib import Path
import argparse, os, subprocess

p = argparse.ArgumentParser()
p.add_argument('--engine', type=Path, required=True, help='Private engine with frame-shot.patch applied')
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
a.output.mkdir(parents=True, exist_ok=True)
tests = a.engine / 'dm/gfx/tests'
s = (tests / 't_present_crop.m').read_text()
old = 'void *front_metal_layer(void) { return NULL; }'
assert s.count(old) == 1
s = s.replace(old, 'static CAMetalLayer *shot_test_layer;\nvoid *front_metal_layer(void) { return (__bridge void *)shot_test_layer; }')
s = '#define main crop_main\n' + s + '\n' + Path(__file__).with_name('frame-shot-test.inc').read_text()
(tests / 't_frame_shot.m').write_text(s)
recipe = '''\ninclude Makefile\nt_frame_shot: t_frame_shot.m $(MTL_T_DEP)\n\tclang $(CFLAGS) -fobjc-arc -o $@ t_frame_shot.m ../../host/crash.c ../../host/hle_bigstack.c t_host_stubs.c $(filter-out ../nvn/nvn_mtl.m,$(GFX)) $(FW)\n'''
(tests / 'frame-shot.mk').write_text(recipe)
subprocess.run(['make', '-j2', '-f', 'frame-shot.mk', 't_frame_shot'], cwd=tests, check=True)
for mode in ['', '20x10', 'raw']:
    subprocess.run([tests / 't_frame_shot'], cwd=tests,
                   env=dict(os.environ, SHOT_TEST_OUT=str(a.output), DM_NVN_SHOT_OUT=mode), check=True)
