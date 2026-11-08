#!/usr/bin/env python3
"""Mac-only settings, geometry and signed-artifact checks, using synthetic inputs."""
from pathlib import Path
import hashlib
import json
import os
import plistlib
import subprocess
import time
import shutil
import runpy
DEPENDENCY_PATHS = runpy.run_path(str(Path(__file__).resolve().parents[2] / 'scripts/dependency_paths.py'))

ROOT = Path(__file__).resolve().parents[2]

def xcode_build_running():
    matches = subprocess.run(['pgrep', '-f', 'xcodebuild'], capture_output=True, text=True)
    for pid in matches.stdout.split():
        command = subprocess.run(['ps', '-p', pid, '-o', 'comm='], capture_output=True, text=True).stdout.strip()
        if Path(command).name == 'xcodebuild':
            return True
    return False


OUT = Path(os.environ.get('SATCHEL_DISPLAY_OUT', Path.home() / 'SatchelBuild/display2'))
while xcode_build_running():
    print('Waiting for xcodebuild before the small Mac harness compile.', flush=True)
    time.sleep(15)
exe = OUT / 'display-settings-check'
shutil.copyfile(ROOT / 'NativeHost/PlayerProfiles.plist', OUT / 'PlayerProfiles.plist')
subprocess.run(['nice', '-n', '10', 'xcrun', 'clang', '-fobjc-arc', '-fmodules', '-framework', 'Foundation',
                '-framework', 'CoreGraphics', '-I', ROOT / 'NativeHost', ROOT / 'NativeHost/PlayerSettings.m',
                ROOT / 'App/ControllerProfiles.m', '-framework', 'GameController', ROOT / 'NativeHost/display/check.m', '-o', exe], check=True)
subprocess.run([exe, 'setup'], check=True)
def check(args, fps='0', scale='1.0', mode='fit'):
    result = subprocess.run([exe, 'read', *args], env=dict(os.environ, EXPECT_FPS=fps, EXPECT_SCALE=scale, EXPECT_MODE=mode), check=True, capture_output=True, text=True)
    print(result.stdout, end='')
    print(result.stderr, end='')
    if args == ['-SatchelRenderScale', '0.45']:
        assert 'allowed render scale range: 0.5 through 1.0' in result.stderr
check([])
check(['-SatchelFPSTarget', '30', '-SatchelRenderScale', '1.0', '-SatchelScreenMode', 'stretch'], '30', '1.0', 'stretch')
check(['-SatchelFPSTarget', '60', '-SatchelRenderScale', '0.5', '-SatchelScreenMode', 'zoom'], '60', '0.5', 'fit')
for value in ['NaN', 'inf', '0.45', '0.49', '1.01', '0.75junk', '', '0.5.5', '-0.75']:
    check(['-SatchelRenderScale', value])
check(['-SatchelFPSTarget', '120', '-SatchelScreenMode', 'wide'])
check(['-SatchelScreenMode'])
check(['-SatchelFPSTarget', '-SatchelScreenMode', 'zoom'], mode='fit')
check(['-SatchelFPSTarget', '0'], '0')
check(['-SatchelRenderScale', '0.7', '-SatchelRenderScale', '0.8'], scale='0.8')
check([])  # Argument overrides do not persist.
# Verify the package verifier itself stayed byte-identical to the assignment baseline.
source = ROOT / 'NativeHost/PreparedGame.swift'
baseline = subprocess.check_output(['git', 'show', '288b2cd:NativeHost/PreparedGame.swift'], cwd=ROOT, text=True)
assert source.read_text().split('\nstruct PreparedGameView: View {')[0] == baseline.split('\nstruct PreparedGameView: View {')[0]
project = json.loads((OUT / 'project.json').read_text())
host_sources = [row['path'] for row in project['targets']['SatchelNativeHost']['sources']]
for suffix in ['front_ios.m', 'DmGamepad.m', 'hle_hid.c', 'hle_audio.c']:
    assert any(path.endswith('/' + suffix) and '/overlays/display/' in path for path in host_sources)
assert any('/overlays/display/' in row['path'] and row['path'].endswith('/hle_nvn.c') for row in project['targets']['SatchelNativeGraphics']['sources'])
app = OUT / 'dev/DerivedData/Build/Products/Release-iphoneos/SatchelDev.app'
assert plistlib.loads((app / 'PlayerProfiles.plist').read_bytes()) == plistlib.loads((ROOT / 'NativeHost/PlayerProfiles.plist').read_bytes())
info = plistlib.loads((app / 'Info.plist').read_bytes())
assert info['GCSupportsControllerUserInteraction'] is True
assert info['GCSupportedGameControllers'] == [{'ProfileName': 'ExtendedGamepad'}]
assert info['DMShowFPS'] is True
symbols = subprocess.check_output(['nm', '-gU', app / 'SatchelDev'], text=True)
for name in ['_SatchelApplyPlayerSettings', '_SatchelChangeScreenMode', '_SatchelInstallDisplayControls', '_satchel_display_command', '_dm_gamepad_view', '_mtl_grab_next_present', '_SatchelMenuPerform', '_SatchelMenuState', '_SatchelNativeSetMenuPaused', '_SatchelInteractiveBounds', '_SatchelMenuVolume', '_nvn_thermal_limited', '_SatchelQueuePlayerMode']:
    assert any(line.endswith(' ' + name) for line in symbols.splitlines()), name
strings = subprocess.check_output(['strings', app / 'SatchelDev'], text=True)
for token in ["disconnected: player %d", "connected: player %d", 'three-finger double tap']:
    assert token in strings, token
all_symbols = subprocess.check_output(['nm', app / 'SatchelDev'], text=True)
assert 'PlayerSettingsViewV4body' in all_symbols, 'Settings view body was not linked'
linked = subprocess.check_output(['otool', '-L', app / 'SatchelDev'], text=True)
assert '/GameController.framework/' in linked
# Neither the dependency source nor this task's engine copy was mutated by the overlays.
source_engine = DEPENDENCY_PATHS['deps_root']() / 'NX2Apple/engine'
for path, digest in json.loads((OUT / 'engine-source-hashes.json').read_text()).items():
    assert hashlib.sha256((source_engine / path).read_bytes()).hexdigest() == digest
    assert hashlib.sha256((OUT / 'engine' / path).read_bytes()).hexdigest() == digest
print('PASS: verifier unchanged; display/controller code linked; plist and FPS overlay enabled; engine copies pristine')
