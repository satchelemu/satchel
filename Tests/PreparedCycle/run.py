#!/usr/bin/env python3
"""Compile production cycle policy and run synthetic sessions on macOS."""
import os
from pathlib import Path
import runpy
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(os.environ.get('SATCHEL_OUT', Path.home() / 'SatchelBuild/session/cycle-tests'))
OUT.mkdir(parents=True, exist_ok=True)
runpy.run_path(str(ROOT / 'NativeApp/build_support.py'))['wait']()
exe = OUT / 'cycle-check'
subprocess.run(['nice', '-n', '10', 'xcrun', 'swiftc', '-Onone',
                str(ROOT / 'App/PreparedGameCycle.swift'), str(Path(__file__).with_name('main.swift')),
                '-o', str(exe)], check=True)
subprocess.run([exe], check=True)
