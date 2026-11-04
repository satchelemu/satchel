#!/usr/bin/env python3
"""Mac synthetic HID and fake Core Haptics checks. No game or hardware inputs."""
from pathlib import Path
import os
import runpy
import subprocess

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
ENGINE = Path.home() / 'SatchelDeps/NX2Apple/engine'
HOST = ENGINE / 'dm/host'
OUT = Path(os.environ.get('PAD_CHECK_OUT', Path.home() / 'SatchelBuild/session/pad-check'))
OUT.mkdir(parents=True, exist_ok=True)
assert subprocess.check_output(['git', '-C', ENGINE, 'rev-parse', 'HEAD'], text=True).strip() == 'bbc3b73784751d2a706932dc69fd67627e3445d4'
runpy.run_path(str(ROOT / 'NativeApp/build_support.py'))['wait']()
overlay = runpy.run_path(str(HERE / 'overlay.py'))
(OUT / 'pad_slots.h').write_text(overlay['slots']((HOST / 'pad_slots.h').read_text()))
(OUT / 'hle_hid.c').write_text(overlay['hid']((HOST / 'hle_hid.c').read_text()))
# Reuse only upstream's synthetic stubs, never its game-specific fixtures.
stubs = (HOST / 'tests/t_hid.c').read_text().split('static void t_slots(void)')[0]
stubs = stubs.replace('#include "../hle_hid.c"', '#include "hle_hid.c"').replace('#include "../pad_slots.h"', '#include "pad_slots.h"')
(OUT / 'controllers.c').write_text(stubs + (HERE / 'probe.inc').read_text())
base = ['nice', '-n', '10', 'xcrun', 'clang', '-O1', '-g', '-Wall', '-Werror', '-Wno-unused-function', '-I' + str(OUT), '-I' + str(HOST), '-I' + str(HOST / 'tests/memory_profile')]
for name, sources, extra in [
    ('controllers', [OUT / 'controllers.c', HOST / 'hle_haptics.c'], ['-fsanitize=undefined,address']),
    ('haptics', [HOST / 'tests/t_haptics.c', HOST / 'hle_haptics.c'], [])]:
    subprocess.run(base + [str(p) for p in sources] + extra + ['-o', str(OUT / name)], check=True)
    subprocess.run([str(OUT / name)], check=True, timeout=30)
# Tests use fake engines on the real serial queue. No NSApplication or controller is opened.
apple = overlay['haptics']((HOST / 'haptics_apple.m').read_text())
(OUT / 'haptics_apple.m').write_text(apple)
test = (HOST / 'tests/t_haptics_apple.m').read_text().replace('#import "../haptics_apple.m"', '#import "haptics_apple.m"')
test = test.replace('#import "haptics_apple.m"', '#import <IOKit/hid/IOHIDManager.h>\nstatic CFSetRef satchel_no_devices(IOHIDManagerRef manager) { (void)manager; return NULL; }\n#define IOHIDManagerCopyDevices satchel_no_devices\n#import "haptics_apple.m"\n#undef IOHIDManagerCopyDevices')
test = test.replace('    dm_haptics_apple_shutdown();', (HERE / 'queue_probe.inc').read_text() + '\n    dm_haptics_apple_shutdown();')
(OUT / 'apple.m').write_text(test)
subprocess.run(base + ['-fobjc-arc', str(OUT / 'apple.m'), str(HOST / 'hle_haptics.c'), '-framework', 'Cocoa', '-framework', 'GameController', '-framework', 'CoreHaptics', '-framework', 'IOKit', '-o', str(OUT / 'apple')], check=True)
subprocess.run([str(OUT / 'apple')], check=True, timeout=30)
print('PASS: synthetic HID, haptic state and fake Apple engine suites')

import shutil
shutil.copyfile(ROOT / 'NativeHost/PlayerProfiles.plist', OUT / 'PlayerProfiles.plist')
settings = OUT / 'controller-settings'
subprocess.run(base + ['-fobjc-arc', '-fmodules', '-framework', 'Foundation', '-framework', 'CoreGraphics', '-I' + str(ROOT / 'NativeHost'), str(ROOT / 'NativeHost/PlayerSettings.m'), str(ROOT / 'NativeHost/display/check.m'), '-o', str(settings)], check=True)
subprocess.run([str(settings), 'setup'], check=True)
for args, fps, scale, mode in [([], '0', '1.0', 'fit'), (['-SatchelScreenMode', 'stretch'], '0', '1.0', 'stretch'), ([], '0', '1.0', 'fit')]:
    subprocess.run([str(settings), 'read', *args], env=dict(os.environ, EXPECT_FPS=fps, EXPECT_SCALE=scale, EXPECT_MODE=mode), check=True)
print('PASS: screen and vibration persistence across processes and titles')
