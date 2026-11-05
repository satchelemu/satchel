#!/usr/bin/env python3
"""Verify controller integration in the unsigned or signed canonical app artifact."""
from pathlib import Path
import hashlib
import json
import os
import plistlib
import subprocess

ROOT = Path(__file__).resolve().parents[2]
OUT = Path(os.environ.get('SATCHEL_OUT', Path.home() / 'SatchelBuild/session/pad-app'))
app = OUT / 'app/DerivedData/Build/Products/Release-iphoneos/Satchel.app'
binary = app / 'Satchel'
info = plistlib.loads((app / 'Info.plist').read_bytes())
assert info['CFBundleIdentifier'] == 'com.dell.satchel'
assert info['GCSupportsControllerUserInteraction']
assert info['GCSupportedGameControllers'] == [{'ProfileName': 'ExtendedGamepad'}]
orientations = info['UISupportedInterfaceOrientations']
assert all('Landscape' in name for name in orientations)
symbols = subprocess.check_output(['nm', '-gU', binary], text=True)
for name in ['satchel_hid_controllers', 'satchel_hid_suppress_player', 'satchel_hid_release', 'satchel_haptics_suspend', 'satchel_gamepad_refresh', 'SatchelMenuControllerDisconnected', 'SatchelPlayerHaptics', 'SatchelChangePlayerHaptics']:
    assert any(line.endswith(' _' + name) for line in symbols.splitlines()), name
# App-owned controller functions are local text symbols in the optimized Release app.
# The native archive exports above retain their existing public-symbol assertions.
local_symbols = subprocess.check_output(['nm', binary], text=True).splitlines()
for name in ['SatchelReadController', 'SatchelSaveControllerProfile', 'SatchelControllerMenuHeld', 'SatchelControllerEdge']:
    assert any(line.split()[-2:] in [['t', '_' + name], ['T', '_' + name]] for line in local_symbols), name
linked = subprocess.check_output(['otool', '-L', binary], text=True)
assert '/GameController.framework/' in linked and '/CoreHaptics.framework/' in linked
# The complete pinned source, including its MIT notice, remains pristine.
engine = Path(os.environ.get('SATCHEL_DEPS_ROOT', Path.home() / 'SatchelDeps')) / 'NX2Apple/engine'
for name, digest in json.loads((OUT / 'host/engine-source-hashes.json').read_text()).items():
    assert hashlib.sha256((engine / name).read_bytes()).hexdigest() == digest, name
    assert hashlib.sha256((OUT / 'host/engine' / name).read_bytes()).hexdigest() == digest, name
notices = app / 'NativeAssets/Notices'
assert (notices / 'Ryujinx-services.txt').read_bytes() == (ROOT / 'NativeHost/services/RYUJINX-LICENSE.txt').read_bytes()
print('PASS: controller and haptics symbols linked; Apple frameworks, landscape, bundle ID and MIT notices preserved; pinned engine pristine')
