#!/usr/bin/env python3
"""Compile and run bounded timing checks and real Metal synthetic feedback."""
from pathlib import Path
import os
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'NativeApp'))
from build_support import wait
out = Path(os.environ['SATCHEL_OUT']) / 'timing-checks'
out.mkdir(parents=True, exist_ok=True)
source = root / 'NativeHost/graphics'
wait()
for name, flags in [('frame-timing-check.c', ['-std=c11', '-Wall', '-Wextra', '-Werror']),
                    ('frame-timing-metal-check.m', ['-fobjc-arc', '-fmodules', '-framework', 'Foundation', '-framework', 'Metal'])]:
    exe = out / Path(name).stem
    subprocess.run(['nice', '-n', '10', 'xcrun', 'clang', *flags, '-I', source, source / name, '-o', exe], check=True)
    subprocess.run([exe], env=dict(os.environ, MTL_DEBUG_LAYER='1'), check=True)
