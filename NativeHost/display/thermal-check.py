#!/usr/bin/env python3
"""Test production thermal functions with a synthetic clock and heat source on the Mac."""
from pathlib import Path
import os
import subprocess
import time
ROOT = Path(__file__).resolve().parents[2]
OUT = Path(os.environ.get('SATCHEL_DISPLAY_OUT', Path.home() / 'SatchelBuild/display2'))
source = (OUT / 'overlays/display/dm/gfx/nvn/hle_nvn.c').read_text()
first = source.index('#define THERMAL_CALM_NS ')
last = source.index('static int fstep_on(void)', first)
# This is the actual generated production source, not a second implementation.
(OUT / 'thermal-production.h').write_text(source[first:last])
while True:
    matches = subprocess.run(['pgrep', '-f', 'xcodebuild'], capture_output=True, text=True)
    active = any(Path(subprocess.run(['ps', '-p', pid, '-o', 'comm='], capture_output=True, text=True).stdout.strip()).name == 'xcodebuild' for pid in matches.stdout.split())
    if not active: break
    print('Waiting for xcodebuild before thermal harness compile.', flush=True)
    time.sleep(15)
exe = OUT / 'thermal-check'
subprocess.run(['nice', '-n', '10', 'xcrun', 'clang', '-std=c11', '-Wall', '-Wextra', '-Werror', '-I', OUT,
                ROOT / 'NativeHost/display/thermal-check.c', '-o', exe], check=True)
subprocess.run([exe], env=dict(os.environ, DM_THERMAL_CAP='1'), check=True)
subprocess.run([exe], env=dict(os.environ, DM_THERMAL_CAP='0'), check=True)
