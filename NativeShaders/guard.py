#!/usr/bin/env python3
"""Wait for resources, then sample only this command's descendants every 5 seconds."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import time

WORK = Path(os.environ.get('SATCHEL_SHADER_DEPS', Path.home().joinpath('SatchelBuild/m5')))
RESERVE = 35 * 1024**3
SWAP_GROWTH_LIMIT = 1024**3
MINIMUM_MEMORY_FREE = 10
# Stop below the requested 10 GiB maximum to leave sampling headroom.
RSS_LIMIT = 9 * 1024**3
SAMPLE_SECONDS = 5

def swap_used():
    text = subprocess.check_output(['sysctl', '-n', 'vm.swapusage'], text=True)
    match = re.search(r'used\s*=\s*([\d.]+)([KMGT])', text)
    if not match:
        raise RuntimeError('Cannot read vm.swapusage; no work started')
    return int(float(match[1]) * 1024**('KMGT'.index(match[2]) + 1))

def swap_baseline():
    # A new explicitly selected work phase may establish its startup baseline.
    # Keep that file unchanged across every step and retry in the phase.
    path=Path(os.environ.get('SATCHEL_M5_BASELINE_FILE', str(WORK/'resource-baseline-v2.json')))
    if path.parent.resolve() != WORK.resolve():
        raise RuntimeError('Swap baseline must be a task-owned file')
    if not path.exists():
        value={'swap_bytes':swap_used(),'started':time.strftime('%Y-%m-%d %H:%M:%S')}
        try:
            with path.open('x') as output: json.dump(value,output)
        except FileExistsError: pass
    return json.loads(path.read_text())['swap_bytes']

def memory_free():
    output=subprocess.check_output(['/usr/bin/memory_pressure','-Q'],text=True)
    match=re.search(r'System-wide memory free percentage:\s*(\d+)%',output)
    if not match: raise RuntimeError('Cannot read memory pressure; no work started')
    return int(match[1])

def resources(extra=512 * 1024**2):
    free, swap = shutil.disk_usage(WORK).free, swap_used()
    baseline, pressure = swap_baseline(), memory_free()
    reason = None
    if free < RESERVE + extra:
        reason = f'Disk reserve: {free} bytes free, need {RESERVE + extra}'
    if swap > baseline + SWAP_GROWTH_LIMIT:
        reason = f'Swap growth: {swap - baseline} bytes above startup, maximum {SWAP_GROWTH_LIMIT}'
    if pressure < MINIMUM_MEMORY_FREE:
        reason = f'Memory pressure: {pressure} percent free, minimum {MINIMUM_MEMORY_FREE}'
    return free, swap, pressure, reason

def busy():
    rows = subprocess.check_output(['ps', '-ww', '-axo', 'pid=,stat=,comm=,args='], text=True).splitlines()
    names = {'xcodebuild', 'metal', 'metallib', 'metalfe', 'dotnet', 'shader-harness'}
    scripts = {'dmsh_build.py', 'own_compilers.py'}
    found = []
    for row in rows:
        fields = row.split(None, 3)
        if len(fields) != 4 or 'Z' in fields[1]: continue
        pid, state, command, arguments = fields
        base = Path(command).name.strip('()')
        script = next((Path(a).name for a in arguments.split()[1:3] if Path(a).name in scripts), None)
        if base in names or (script and 'python' in Path(arguments.split()[0]).name.lower()):
            found.append(pid + ' ' + (script or base))
    return found

def family(root):
    # No command arguments are recorded. Each RSS is the value reported by ps.
    rows = subprocess.check_output(['ps', '-axo', 'pid=,ppid=,rss=,comm='], text=True).splitlines()
    table = {}
    for row in rows:
        fields = row.split(None, 3)
        if len(fields) == 4:
            pid, parent, rss, name = fields
            table[int(pid)] = (int(parent), int(rss) * 1024, Path(name).name)
    owned = {root}
    while True:
        added = {pid for pid, (parent, _, _) in table.items() if parent in owned} - owned
        if not added: break
        owned.update(added)
    return {pid: table[pid] for pid in owned if pid in table}

def atomic_json(path, value):
    temporary = path.with_suffix('.part')
    temporary.write_text(json.dumps(value, indent=2) + '\n')
    temporary.replace(path)

def stop_owned(child):
    descendants = family(child.pid)
    def running(pid):
        result=subprocess.run(['ps','-p',str(pid),'-o','stat='],capture_output=True,text=True)
        state=result.stdout.strip()
        return bool(state) and 'Z' not in state
    def send(pid, sig):
        if not running(pid): return
        try: os.kill(pid,sig)
        except ProcessLookupError: pass
        except PermissionError:
            # Darwin can return EPERM when a process exits between the sample
            # and the signal. A live owned process must still be stoppable.
            if running(pid): raise
    def group(sig):
        try: os.killpg(child.pid,sig)
        except ProcessLookupError: pass
        except PermissionError:
            if any(running(pid) for pid in descendants): raise
    group(signal.SIGTERM)
    # A compiler can create its own process group. Only currently proven
    # descendants are signalled, never another worker's compiler.
    for pid in descendants:
        send(pid, signal.SIGTERM)
    try: child.wait(timeout=2)
    except subprocess.TimeoutExpired:
        for pid in family(child.pid):
            send(pid, signal.SIGKILL)
        group(signal.SIGKILL)
        child.wait()
    # The root may exit before a child that ignores SIGTERM.
    group(signal.SIGKILL)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--light', action='store_true', help='Skip compiler-slot wait, retain all resource limits')
    parser.add_argument('--rss-limit-mib', type=int, default=9216, help='May only lower the production ceiling')
    parser.add_argument('command', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command
    if command[:1] == ['--']: command = command[1:]
    if not command or not 1 <= args.rss_limit_mib <= 9216:
        parser.error('Provide a command and an RSS ceiling of at most 9216 MiB')
    WORK.mkdir(exist_ok=True)
    logs = WORK/'limits'; logs.mkdir(exist_ok=True)
    path = logs/(time.strftime('%Y%m%d-%H%M%S') + '-' + str(os.getpid()) + '.json')
    report = {'status':'waiting', 'sample_seconds':SAMPLE_SECONDS, 'rss_limit_bytes':args.rss_limit_mib*1024**2,
              'requested_maximum_rss_bytes':10*1024**3, 'swap_growth_limit_bytes':SWAP_GROWTH_LIMIT,
              'swap_baseline_bytes':swap_baseline(), 'minimum_memory_free_percent':100,
              'swap_baseline_file':os.environ.get('SATCHEL_M5_BASELINE_FILE', str(WORK/'resource-baseline-v2.json')),
              'processes':{}, 'peak_swap_bytes_during_work':0}
    lock = None
    if not args.light:
        lock = (WORK/'heavy-build.lock').open('a')
    locked = False
    def cancelled_wait(signum, frame):
        report.update(status='cancelled while waiting', exit_code=128+signum)
        atomic_json(path, report)
        raise SystemExit(128+signum)
    signal.signal(signal.SIGTERM, cancelled_wait)
    prior = None
    while True:
        free, swap, pressure, reason = resources()
        if not reason and lock:
            try:
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                locked = True
            except BlockingIOError:
                reason = 'Waiting for shared heavy build lock'
        if not reason:
            # Recheck after acquiring the slot. A waiting guard owns no slot.
            free, swap, pressure, reason = resources()
            active = busy() if not args.light else []
            if not reason and active: reason = 'Waiting for heavy build slot: ' + ', '.join(active)
        if reason and locked:
            fcntl.flock(lock, fcntl.LOCK_UN); locked = False
        report.update(free_bytes=free, swap_bytes=swap, memory_free_percent=pressure, wait_reason=reason, lock_held=locked)
        atomic_json(path, report)
        if not reason: break
        if reason != prior: print(reason, flush=True); prior = reason
        time.sleep(SAMPLE_SECONDS)
    env = dict(os.environ, DOTNET_PROCESSOR_COUNT='2', CMAKE_BUILD_PARALLEL_LEVEL='2', J='2',
               PYTHONDONTWRITEBYTECODE='1', SATCHEL_M5_WATCHDOG_PID=str(os.getpid()))
    child = subprocess.Popen(['nice', '-n', '10', *command], env=env, start_new_session=True)
    report.update(status='running', root_pid=child.pid, minimum_free_bytes_during_work=free)
    def interrupted(signum, frame): raise SystemExit(128 + signum)
    signal.signal(signal.SIGTERM, interrupted)
    try:
        while True:
            free, swap, pressure, reason = resources(128 * 1024**2)
            report.update(free_bytes=free, swap_bytes=swap)
            report['minimum_memory_free_percent']=min(pressure,report['minimum_memory_free_percent'])
            report['minimum_free_bytes_during_work'] = min(free, report['minimum_free_bytes_during_work'])
            report['peak_swap_bytes_during_work'] = max(swap, report['peak_swap_bytes_during_work'])
            for pid, (_, rss, name) in family(child.pid).items():
                peak = report['processes'].setdefault(str(pid), {'name':name, 'peak_rss_bytes':0})
                peak['peak_rss_bytes'] = max(peak['peak_rss_bytes'], rss)
                if rss > report['rss_limit_bytes']:
                    reason = f'RSS limit: owned PID {pid} uses {rss} bytes, ceiling {report["rss_limit_bytes"]}'
            atomic_json(path, report)
            if reason: raise RuntimeError(reason)
            if child.poll() is not None: break
            time.sleep(SAMPLE_SECONDS)
    except BaseException as error:
        try: stop_owned(child)
        except Exception as cleanup:
            report['cleanup_error']=type(cleanup).__name__ + ': ' + str(cleanup)
        report.update(status='stopped', reason=str(error), exit_code=child.returncode)
        atomic_json(path, report)
        raise
    report.update(status='finished', exit_code=child.returncode)
    atomic_json(path, report)
    print('Resource samples: ' + str(path), flush=True)
    raise SystemExit(child.returncode)

if __name__ == '__main__': main()
