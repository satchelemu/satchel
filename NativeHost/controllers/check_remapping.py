#!/usr/bin/env python3
"""Synthetic GameController reader and JSON persistence checks on macOS."""
from pathlib import Path
import os
import subprocess
import runpy
import shutil

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(os.environ['SATCHEL_OUT']) / 'remapping'
OUT.mkdir(parents=True, exist_ok=True)
shutil.copyfile(ROOT / 'NativeHost/PlayerProfiles.plist', OUT / 'PlayerProfiles.plist')
runpy.run_path(str(ROOT / 'NativeApp/build_support.py'))['wait']()
subprocess.run(['nice', '-n', '10', 'xcrun', 'clang', '-fobjc-arc', '-fmodules', '-Wall', '-Werror',
    '-Wno-nullability-completeness', '-I' + str(ROOT / 'App'), '-I' + str(ROOT / 'NativeHost'),
    str(ROOT / 'App/ControllerProfiles.m'), str(ROOT / 'NativeHost/PlayerSettings.m'),
    str(ROOT / 'NativeHost/controllers/remapping_check.m'), '-framework', 'Foundation',
    '-framework', 'GameController', '-o', str(OUT / 'satchel-controls-check')], check=True)
for args in [[], ['read']]:
    subprocess.run([str(OUT / 'satchel-controls-check'), *args], check=True)
