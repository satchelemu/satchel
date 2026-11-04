#!/usr/bin/env python3
"""Canonical Satchel build. No installation, launch or game inputs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[1]
OUT = Path(os.environ.get('SATCHEL_OUT', Path.home() / 'SatchelBuild/oneapp'))

from build_support import wait as idle

def run(args, env):
    idle()
    subprocess.run([str(x) for x in args], env=env, check=True)

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--migration-bridge', action='store_true', help='Temporary Dev update, migration screen only')
parser.add_argument('--unsigned', action='store_true', help='Compile only; not installable and no provisioning verification')
parser.add_argument('--ui-tests-simulator', action='store_true', help='Run the synthetic navigation suite, with no native archives or phone actions')
args = parser.parse_args()
if args.ui_tests_simulator:
    run([sys.executable, '-B', ROOT / 'Tests/UI/Navigation/run-simulator.py'], dict(os.environ, SATCHEL_OUT=str(OUT)))
    sys.exit(0)
OUT.mkdir(parents=True, exist_ok=True)
host = OUT / 'host'
# Cache validity comes from repository source content, never an old generated project in another worker's tree.
inputs = list((ROOT / 'NativeHost').glob('*.m')) + list((ROOT / 'NativeHost').glob('*.h'))
inputs += [ROOT / 'NativeHost/build.py', ROOT / 'NativeHost/display_overlay.py',
           ROOT / 'NativeHost/display/screen-modes.patch', ROOT / 'NativeHost/display/frame-shot.patch',
           ROOT / 'NativeGames/runtime-capture.patch', ROOT / 'NativeGames/RuntimeCapture.inc',
           ROOT / 'NativeHost/phone-capture-lifetime.patch', ROOT / 'NativeHost/phone-shaders.patch',
           ROOT / 'NativeHost/PhoneShaderRuntime.inc', ROOT / 'project.yml']
inputs += [ROOT / 'NativeHost/phone-prefetch.patch', ROOT / 'NativeHost/graphics/shader_builds.py', ROOT / 'NativeHost/graphics/ShaderBuildQueue.inc']
inputs += [ROOT / 'NativeHost/graphics/ShaderMetrics.inc', ROOT / 'NativeHost/graphics/shader_metrics.py']
inputs += [ROOT / 'NativeHost/services' / name for name in
           ['overlay.py', 'ServiceIPC.inc', 'SessionIPC.inc', 'EngineAdapter.inc', 'RYUJINX-LICENSE.txt', 'SystemIPC.inc', 'Round8IPC.inc', 'Round8Trace.inc', 'ClockState.inc', 'FileQueries.inc', 'MissingServices.inc', 'coverage/MissingServiceNames.inc']]
inputs += [ROOT / 'NativeHost/controllers' / name for name in ['overlay.py', 'HapticQueue.inc']]
session = ROOT / 'NativeHost/session'
inputs += list(session.glob('Session*.c')) + list(session.glob('Session*.h'))
inputs += list(session.glob('*.inc')) + list(session.glob('*overlay.py')) + list(session.glob('*.patch'))
inputs += [ROOT / 'NativeHost/graphics' / name for name in
           ['PipelineSettings.m', 'PipelineSettings.h', 'overlay.py', 'pipeline.patch', 'stalls.patch', 'host-time.patch', 'HostPause.h', 'loading.patch', 'RomFSRead.h']]
inputs += [ROOT / 'NativeHost/memory' / name for name in ['overlay.py', 'SceneMemory.inc', 'GhostPool.inc']]
# Reusable sessions are the canonical app path. An explicit zero is diagnostic only.
os.environ.setdefault('SATCHEL_SESSION_BOUNDARY', '1')
fingerprint = hashlib.sha256(os.environ['SATCHEL_SESSION_BOUNDARY'].encode() +
                             b''.join(p.read_bytes() for p in sorted(inputs))).hexdigest()
stamp = host / 'satchel-host-inputs.json'
archives = [host / 'DerivedData/Build/Products/Release-iphoneos' / ('lib' + name + '.a')
            for name in ['SatchelNativeHost', 'SatchelNativeGraphics', 'SatchelNativeMedia']]
env = dict(os.environ, SATCHEL_HOST_OUT=str(host), PYTHONDONTWRITEBYTECODE='1')
if not stamp.exists() or json.loads(stamp.read_text()) != fingerprint or not all(p.is_file() for p in archives):
    run(['nice', '-n', '10', sys.executable, '-B', ROOT / 'NativeHost/build.py', '--archives-only'], env)
    stamp.write_text(json.dumps(fingerprint) + '\n')
env['SATCHEL_PREPARE_OUT'] = str(OUT / ('bridge' if args.migration_bridge else 'app'))
run(['nice', '-n', '10', sys.executable, '-B', ROOT / 'NativePrepare/build-dev.py',
     '--migration-bridge' if args.migration_bridge else '--single-app'] + (['--unsigned'] if args.unsigned else []), env)
print('Built Satchel migration bridge.' if args.migration_bridge else 'Built the single Satchel app.')
