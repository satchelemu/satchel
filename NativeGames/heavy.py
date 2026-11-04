"""Serialize heavy work with the other Satchel workers."""
from pathlib import Path
import fcntl, os, shutil, subprocess, time

_slot_lock = None

def wait_for_slot(output):
    global _slot_lock
    # Cooperating workers may name the same advisory lock for the whole child
    # lifetime. Process scanning still covers builders that do not use it.
    if _slot_lock is None and (name := os.environ.get('SATCHEL_HEAVY_LOCK')):
        _slot_lock = open(name, 'a')
        print('Waiting for the shared heavy-build lock.', flush=True)
        fcntl.flock(_slot_lock, fcntl.LOCK_EX)
    while True:
        result = subprocess.run(['pgrep', '-fl', r'(^|/)(xcodebuild|metal|metallib|metalfe|shader-harness|dotnet|ilc)( |$)|(dmsh_build|own_compilers)[.]py'], text=True, capture_output=True)
        if result.returncode not in (0, 1):
            raise RuntimeError('Cannot inspect active heavy builds: ' + result.stderr.strip())
        if not result.stdout.strip(): break
        print('Waiting for the active heavy build.', flush=True)
        time.sleep(10)
    if shutil.disk_usage(Path(output)).free < 36 * 1024**3:
        raise RuntimeError('Heavy build blocked: less than 36 GiB free, preserving the 35 GiB reserve.')
