#!/usr/bin/env python3
"""Run task children at nice 10 with a disk and resident-memory watchdog."""
import json
import fcntl
import os
from pathlib import Path
import signal
import subprocess
import sys
import time
import shutil
import runpy

xcode_is_building = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'NativeApp/build_support.py'))['xcode_is_building']

out = Path(os.environ.get('ANYGAME_OUT', Path.home() / 'SatchelBuild/anygame'))
out.mkdir(parents=True, exist_ok=True)

def processes():
    rows = subprocess.check_output(['ps', '-axo', 'pid=,ppid=,rss=,comm='], text=True)
    return {int(p): (int(parent), int(rss) * 1024, name) for p, parent, rss, name in
            (line.strip().split(None, 3) for line in rows.splitlines())}

def descendants(table, root):
    owned = {root}
    while True:
        new = owned | {p for p, row in table.items() if row[0] in owned}
        if new == owned: return owned
        owned = new

if len(sys.argv) < 2: raise SystemExit('A command is required.')
# Serialize this worker's drivers even while another is staging before compilation.
lock = (out / 'heavy.lock').open('a')
fcntl.flock(lock, fcntl.LOCK_EX)
while True:
    table = processes()
    heavy = {'xcodebuild', 'metal', 'metallib', 'metalfe', 'shader-harness', 'source-harness', 'pipeline-harness', 'clang', 'clang++', 'swift-frontend', 'dotnet', 'ilc'}
    busy = any((Path(row[2]).name in heavy or Path(row[2]).name.endswith('-harness')) and
               (Path(row[2]).name != 'xcodebuild' or xcode_is_building(pid)) for pid, row in table.items())
    pids = [str(p) for p, row in table.items() if Path(row[2]).name.lower().startswith('python') and p != os.getpid()]
    if pids:
        args = subprocess.check_output(['ps', '-p', ','.join(pids), '-o', 'args='], text=True)
        busy |= any(name in args for name in ['own_compilers.py', 'dmsh_build.py', 'NativeShaders/check-pipeline.py'])
    if not busy: break
    print('Waiting for active heavy work.', flush=True)
    time.sleep(10)
if shutil.disk_usage(out).free < 36 * 1024**3: raise SystemExit('Disk reserve: need 36 GiB before starting.')
child = subprocess.Popen(['nice', '-n', '10', *sys.argv[1:]], start_new_session=True)
peaks = {}; minimum = shutil.disk_usage(out).free; failure = None
try:
    while child.poll() is None:
        table = processes(); owned = descendants(table, child.pid)
        for p in owned & table.keys():
            peaks[str(p)] = max(peaks.get(str(p), 0), table[p][1])
            if table[p][1] >= 9 * 1024**3: failure = 'Resident memory guard: owned process reached 9 GiB.'
        minimum = min(minimum, shutil.disk_usage(out).free)
        if minimum < 35.5 * 1024**3: failure = 'Disk reserve guard: stopping before 35 GiB.'
        if failure:
            os.killpg(child.pid, signal.SIGSTOP)
            os.killpg(child.pid, signal.SIGKILL)
            break
        time.sleep(0.5)
    code = child.wait()
finally:
    if child.poll() is None:
        os.killpg(child.pid, signal.SIGKILL); child.wait()
    (out / ('limits-' + str(child.pid) + '.json')).write_text(json.dumps({'rssPeaks': peaks, 'minimumFreeBytes': minimum, 'failure': failure, 'exit': child.returncode}, indent=2) + '\n')
if failure: print(failure, flush=True)
sys.exit(code if code >= 0 else 1)
