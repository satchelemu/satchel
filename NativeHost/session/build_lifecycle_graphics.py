#!/usr/bin/env python3
"""Link the lifecycle fixture against the real Mac host objects."""
import json
import os
from pathlib import Path
import runpy
import subprocess
import signal
import time
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
OUT = Path(os.environ['SESSION_MAC_OUT'])
runpy.run_path(str(ROOT / 'NativeApp/build_support.py'))['wait']()
build = json.loads((OUT / 'compile.json').read_text())
obj = OUT / 'lifecycle-graphics.o'
subprocess.run(build['base'] + ['-iquote', str(OUT / 'graphics-policy'), '-fobjc-arc', '-finstrument-functions',
                              '-c', str(HERE / 'lifecycle_graphics_probe.m'), '-o', str(obj)], check=True)
objects = [obj] + [o for s, o in zip(build['sources'], build['compiled']) if Path(s).name not in ('nvn_mtl.m', 'mac_stop_probe.c')]
command = build['base'] + [str(p) for p in objects] + [build['vpx']]
for framework in build['frameworks']: command += ['-framework', framework]
binary = OUT / 'lifecycle-graphics'
subprocess.run(command + ['-o', str(binary)], check=True)
env = {k: v for k, v in os.environ.items() if not k.startswith('DM_')}
subprocess.run([str(binary)], env=env, check=True, timeout=90)

obj = OUT / 'lifecycle-audio.o'
subprocess.run(build['base'] + ['-finstrument-functions', '-c', str(HERE / 'lifecycle_audio_probe.c'), '-o', str(obj)], check=True)
objects = [obj] + [o for s, o in zip(build['sources'], build['compiled']) if Path(s).name not in ('hle_audio.c', 'mac_stop_probe.c')]
command = build['base'] + [str(p) for p in objects] + [build['vpx']]
for framework in build['frameworks']: command += ['-framework', framework]
binary = OUT / 'lifecycle-audio'
subprocess.run(command + ['-o', str(binary)], check=True)
child = subprocess.Popen([str(binary)], env=env)
deadline = time.monotonic() + 30
try:
    while True:
        pid, status = os.waitpid(child.pid, os.WUNTRACED | os.WNOHANG)
        if pid:
            assert os.WIFSTOPPED(status), status
            break
        assert time.monotonic() < deadline, 'audio fixture did not reach suspension'
        time.sleep(.01)
    time.sleep(.25)
    os.kill(child.pid, signal.SIGCONT)
    assert child.wait(timeout=60) == 0
finally:
    if child.poll() is None: child.kill(); child.wait()
print('PASS: whole-process SIGSTOP/SIGCONT preserves guest clock and held PCM')
