#!/usr/bin/env python3
"""Verify the watchdog stops a task descendant and leaves its sibling alive."""
import json
import os
from pathlib import Path
import runpy
import subprocess
import sys
DEPENDENCY_PATHS = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'scripts/dependency_paths.py'))

def main():
    if not os.environ.get('SATCHEL_M5_WATCHDOG_PID'):
        raise RuntimeError('Run this check through guard.py --light')
    here=Path(__file__).resolve().parent
    # The inner guard inherits this environment, so it samples into the same work folder.
    logs=DEPENDENCY_PATHS['shader_work']()/'limits'
    sentinel=subprocess.Popen([sys.executable,'-c','import time; time.sleep(30)'])
    inner=None
    try:
        allocate='import time; data=bytearray(80*1024*1024); time.sleep(25)'
        parent='import subprocess,sys; subprocess.run([sys.executable,"-c",'+repr(allocate)+'],check=True)'
        inner=subprocess.Popen([sys.executable,here/'guard.py','--light','--rss-limit-mib','64',
                                sys.executable,'-c',parent],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        output=inner.communicate(timeout=30)[0]
        (logs/('guard-test-'+str(inner.pid)+'.log')).write_text(output)
        assert inner.returncode != 0 and 'RSS limit:' in output
        assert sentinel.poll() is None, 'Watchdog stopped a sibling outside its child family'
        paths=sorted(logs.glob('*-'+str(inner.pid)+'.json'))
        report=json.loads(paths[-1].read_text())
        assert report['status']=='stopped' and report['reason'].startswith('RSS limit:'), output
        assert 'cleanup_error' not in report, report.get('cleanup_error')
        assert max(p['peak_rss_bytes'] for p in report['processes'].values())>64*1024**2
        assert report['peak_swap_bytes_during_work']<=report['swap_baseline_bytes']+1024**3
        assert report['minimum_memory_free_percent']>=10
        print('Watchdog passed: nested child stopped at reduced 64 MiB threshold; sibling survived; samples persisted.')
    finally:
        if inner is not None and inner.poll() is None:
            inner.terminate();inner.wait(timeout=10)
        sentinel.terminate();sentinel.wait(timeout=10)

if __name__=='__main__': main()
