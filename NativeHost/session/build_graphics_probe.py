#!/usr/bin/env python3
"""Reuse the Mac host objects for a real Metal producer ownership fixture."""
import json
import os
from pathlib import Path
import runpy
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
OUT = Path(os.environ.get('SESSION_MAC_OUT', Path.home() / 'SatchelBuild/session/mac-stop'))
runpy.run_path(str(ROOT / 'NativeApp/build_support.py'))['wait']()
build = json.loads((OUT / 'compile.json').read_text())
obj = OUT / 'graphics-probe.o'
subprocess.run(build['base'] + ['-iquote', str(OUT / 'graphics-policy'), '-fobjc-arc', '-finstrument-functions',
                              '-c', str(HERE / 'graphics_probe.m'), '-o', str(obj)], check=True)
objects = [obj] + [o for s, o in zip(build['sources'], build['compiled']) if Path(s).name not in ('nvn_mtl.m', 'mac_stop_probe.c')]
command = build['base'] + [str(p) for p in objects] + [build['vpx']]
for framework in build['frameworks']: command += ['-framework', framework]
binary = OUT / 'graphics-probe'
subprocess.run(command + ['-o', str(binary)], check=True)
env = {k: v for k, v in os.environ.items() if not k.startswith('DM_')}
subprocess.run([str(binary)], env=env, check=True, timeout=90)
