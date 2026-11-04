#!/usr/bin/env python3
"""Build actual Release host archives from a temporary checkout containing spaces."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
OUT = Path(os.environ['SATCHEL_OUT']).resolve()
if OUT.is_relative_to(ROOT): raise SystemExit('SATCHEL_OUT must be outside the checkout.')
OUT.mkdir(parents=True, exist_ok=True)
# Copy source files only. Include this task's untracked additions, never build products.
files = subprocess.check_output(['git', '-C', str(ROOT), 'ls-files', '-z', '--cached', '--others', '--exclude-standard']).decode().split('\0')
with tempfile.TemporaryDirectory(prefix='checkout with spaces ', dir=OUT) as directory:
    checkout = Path(directory)
    for name in filter(None, files):
        source = ROOT / name
        if not source.is_file(): continue
        target = checkout / name; target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, target)
    host = OUT / 'host'
    env = dict(os.environ, SATCHEL_HOST_OUT=str(host), SATCHEL_SESSION_BOUNDARY='1', PYTHONDONTWRITEBYTECODE='1')
    subprocess.run(['nice', '-n', '10', sys.executable, '-B', checkout / 'NativeHost/build.py', '--archives-only'], env=env, check=True)
    products = host / 'DerivedData/Build/Products/Release-iphoneos'
    archives = [products / ('lib' + name + '.a') for name in ['SatchelNativeHost', 'SatchelNativeGraphics', 'SatchelNativeMedia']]
    assert all(path.is_file() and path.stat().st_size for path in archives)
    (OUT / 'result.json').write_text(json.dumps({'passed': True, 'checkout': str(checkout), 'archives': [str(p) for p in archives]}, indent=2) + '\n')
print('PASS: Release host archives built from a temporary checkout with spaces.')
