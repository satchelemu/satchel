#!/usr/bin/env python3
"""Build the Prepare phase 1 measurement client without a game package copy."""
import json
import os
from pathlib import Path
import subprocess
import sys
import runpy
DEPENDENCY_PATHS = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'scripts/dependency_paths.py'))

ROOT = Path(__file__).resolve().parents[1]
SOURCE = Path(os.environ.get('SATCHEL_WARMUP_SOURCE', ROOT))
OUT = Path(os.environ['SATCHEL_OUT']).resolve()
if OUT.is_relative_to(ROOT):
    raise SystemExit('Build output must be outside the checkout.')
OUT.mkdir(parents=True, exist_ok=True)
sys.path.insert(0, str(ROOT / 'NativeHost'))
import shader_dependency
staged = shader_dependency.stage(SOURCE, OUT, 'macosx')
# The same shader products shader_dependency staged from, so the dylib matches the staged sources.
aot = DEPENDENCY_PATHS['shader_deps']() / 'aot/repro/host-publish/SatchelShaderProbe.dylib'
common = ['xcrun', 'swiftc', '-O', '-j', '2', '-num-threads', '2', '-target', 'arm64-apple-macos15.0',
          '-import-objc-header', staged / 'Bridge.h',
          *[staged / (name + '.swift') for name in shader_dependency.SOURCES],
          staged / 'libGameShaderBridge.a', staged / 'libShaderArchive.a', aot,
          '-Xlinker', '-rpath', '-Xlinker', aot.parent, '-lc++', '-framework', 'Metal']
if SOURCE == ROOT:
    common += ['-D', 'WARMUP_SPEED']
for source, name in [('NativeShaders/WarmupSpeedHarness.swift', 'warmup-speed-harness'),
                     ('NativeHost/graphics/request-equivalence.swift', 'request-equivalence')]:
    subprocess.run([*common, ROOT / source, '-o', OUT / name], check=True)
print('Built warm-up timing and request-equivalence clients')
