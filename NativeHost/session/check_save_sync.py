#!/usr/bin/env python3
"""Mac synthetic check of the session-scoped cloud save sender. No game or hardware inputs."""
from pathlib import Path
import os
import runpy
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
ENGINE = Path(os.environ.get('SATCHEL_DEPS_ROOT', Path.home() / 'SatchelDeps')) / 'NX2Apple/engine'
OUT = Path(os.environ.get('SAVE_SYNC_CHECK_OUT', Path.home() / 'SatchelBuild/session/save-sync-check'))
OUT.mkdir(parents=True, exist_ok=True)
assert subprocess.check_output(['git', '-C', ENGINE, 'rev-parse', 'HEAD'], text=True).strip() == 'bbc3b73784751d2a706932dc69fd67627e3445d4'
runpy.run_path(str(HERE / 'save_sync_overlay.py'))['prepare'](ENGINE / 'dm/host/save_sync.c', OUT / 'save_sync.c')
shutil.copyfile(HERE / 'save_sync_probe.c', OUT / 'save_sync_probe.c')
exe = OUT / 'save-sync-probe'
subprocess.run(['nice', '-n', '10', 'xcrun', 'clang', '-O1', '-g', '-Wall', '-Werror', '-Wno-unused-function',
                '-fsanitize=address,undefined', '-I' + str(OUT), '-I' + str(ENGINE / 'dm/host'),
                str(OUT / 'save_sync_probe.c'), '-o', str(exe)], check=True)
with tempfile.TemporaryDirectory(dir=OUT) as scratch:
    subprocess.run([str(exe), scratch], check=True, timeout=120)
