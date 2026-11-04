#!/usr/bin/env python3
"""Run existing private Mac title inputs, retaining only numeric proof records."""
import json
import os
from pathlib import Path
import re
import runpy
import subprocess
import sys
import threading
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
BUILD = Path.home() / 'SatchelBuild'
OUT = Path(os.environ.get('SESSION_MAC_OUT', BUILD / 'session/mac-stop'))
binary = OUT / 'mac-stop-probe'
assert binary.is_file()
inputs = {
    'Castlevania': (BUILD / 'games/castlevania/patched_v0', BUILD / 'castlevania/runtime-shaders/cache'),
    'TOTK': (BUILD / 'zelda/rewritten', BUILD / 'zelda/work/shader/cache'),
}
results = []
pattern = re.compile(r'^\[session-proof\] phase=([a-z-]+) result=(\d+) guest_threads=(\d+) guest_mappings=(\d+) rss=(\d+) frames=(\d+)$')
for name in sys.argv[1:] or inputs:
    runpy.run_path(str(ROOT / 'NativeApp/build_support.py'))['wait']()
    # Avoid competing with a foreground game measurement.
    while subprocess.run(['pgrep', '-x', 'dmhost'], stdout=subprocess.DEVNULL).returncode == 0:
        print('Waiting for another Mac game host.', flush=True); time.sleep(5)
    patched, libraries = inputs[name]
    package = BUILD / 'anygame/packages' / name
    for path in [patched / 'main.patched.bin', libraries, package / 'profile.tsv', package / 'romfs.img']:
        assert path.exists(), path
    folder = OUT / ('run-' + name + '-' + str(time.time_ns()))
    for part in ['save', 'temp', 'aoc', 'archive']: (folder / part).mkdir(parents=True)
    env = {k: v for k, v in os.environ.items() if not k.startswith('DM_')}
    env.update(DM_PROFILE=str(package / 'profile.tsv'), DM_ROMFS=str(package / 'romfs.img'),
               DM_ROOT=str(folder), DM_SAVE=str(folder / 'save'), DM_TEMP=str(folder / 'temp'),
               DM_AOC_DIR=str(folder / 'aoc'), DM_MSL_DIR=str(libraries),
               DM_NVN_SHMAP=str(package / 'dm/shader_map.tsv'), DM_SAVE_SYNC='0',
               DM_AUDIO='0', DM_OPMODE='0', DM_PSO_PREBUILD_THREADS='2',
               DM_PSO_ARCHIVE=str(folder / 'archive'), DM_PSO_PREPARE='0')
    start = time.monotonic()
    process = subprocess.Popen(['nice', '-n', '10', str(binary), str(patched)], cwd=folder, env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
    timed_out = threading.Event()
    def timeout():
        timed_out.set(); process.kill()
    timer = threading.Timer(120, timeout); timer.start()
    rows, omitted = [], 0
    try:
        for line in process.stdout:
            match = pattern.fullmatch(line.strip())
            if match:
                row = dict(zip(['phase', 'result', 'guestThreads', 'guestMappings', 'rss', 'frames'], match.groups()))
                for field in list(row)[1:]: row[field] = int(row[field])
                rows.append(row); print(name + ' ' + line.strip(), flush=True)
            elif line.startswith("[session-host-stack] ") and len(line) < 500: print(line.strip(), flush=True)
            else: omitted += 1
        process.wait()
    finally:
        timer.cancel()
        if process.poll() is None: process.kill(); process.wait()
    result = dict(title=name, exit=process.returncode, timeout=timed_out.is_set(),
                  seconds=round(time.monotonic() - start, 3), rows=rows, omittedLines=omitted,
                  fullRestart=False, isolatedFreshSave=str(folder / 'save'))
    (folder / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    results.append(result)
(OUT / 'real-results.json').write_text(json.dumps(results, indent=2) + '\n')
print('Real-title stop attempts recorded. Full restart was not attempted.', flush=True)
sys.exit(0 if all(r['exit'] == 0 for r in results) else 1)
