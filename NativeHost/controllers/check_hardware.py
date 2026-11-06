#!/usr/bin/env python3
"""Source checks for gaps that only real controllers expose. No build, hardware or game input.

Synthetic snapshots post their own connect notification and bypass GameController's
enumeration, so these checks pin the paths a paired Bluetooth pad takes instead: discovery
at launch and on return, any vendor name or product category, controllers without optional
elements, focus navigation on every full-screen app screen, and the Info.plist keys.
"""
from pathlib import Path
import os
import re
import runpy

ROOT = Path(__file__).resolve().parents[2]
ENGINE = Path(os.environ.get('SATCHEL_DEPS_ROOT', Path.home() / 'SatchelDeps')) / 'NX2Apple/engine'
failures = []


def check(condition, message):
    if not condition:
        failures.append(message)


# In game: the overlaid engine front end, exactly as the app build produces it (display
# overlay first, then the controller overlay), written outside the repository.
OUT = Path(os.environ.get('SATCHEL_OUT', Path.home() / 'SatchelBuild/controllers-hw')) / 'hardware-check'
OUT.mkdir(parents=True, exist_ok=True)
runpy.run_path(str(ROOT / 'NativeHost/display_overlay.py'))['apply'](ROOT, ENGINE, OUT, {'SatchelNativeHost': [], 'SatchelNativeGraphics': []})
overlay = runpy.run_path(str(ROOT / 'NativeHost/controllers/overlay.py'))
pad = overlay['gamepad']((OUT / 'overlays/display/dm/ios/Sources/DmGamepad.m').read_text())
init = pad[pad.index('- (instancetype)initWithFrame:'):pad.index('- (void)applyLayout')]
for name in ['GCControllerDidConnectNotification', 'GCControllerDidDisconnectNotification', 'GCControllerDidBecomeCurrentNotification']:
    check(name in init, 'game view does not observe ' + name)
check('[self padsChanged:nil]' in init, 'game view does not adopt controllers connected before it existed')
active = re.search(r'void dm_gamepad_active\(void\) \{.*', pad).group(0)
check('padsChanged:nil' in active, 'return from the background does not rescan controllers')
slotted = pad[pad.index('static BOOL pads_slotted('):pad.index('- (void)padsChanged:(NSNotification *)note {')]
check('seen: category' in slotted, 'the host log does not name every controller iOS reports')
# Input must never depend on a controller's name or category, only on its profile.
for token in ['vendorName isEqual', 'productCategory isEqual', 'vendorName containsString', 'productCategory containsString']:
    check(token not in slotted and token not in pad[pad.index('static void read_pad('):pad.index('static void pad_keep_system_buttons(')],
          'game input depends on ' + token)

# App: the shared monitor sees every profile from launch and keeps polling in all run loop modes.
navigation = (ROOT / 'App/ControllerNavigation.swift').read_text()
check('.filter { $0.extendedGamepad != nil }' not in navigation, 'app navigation ignores controllers without an extended profile')
check('forMode: .common' in navigation, 'controller polling stops while a scroll view tracks')
check('GCController.controllers()' in navigation and '.GCControllerDidConnect' in navigation, 'monitor does not enumerate and observe controllers')
check('ControllerInputMonitor.shared.start()' in (ROOT / 'App/SatchelApp.swift').read_text(), 'monitor does not start at launch')
outside_preview = re.sub(r'#if PREVIEW_GAME.*?#endif', '', navigation, flags=re.S)
check('UserDefaults' not in outside_preview and '@AppStorage' not in outside_preview, 'controller navigation is gated behind a setting')
check(re.search(r'func controllerNavigation\(enabled: Bool = true', navigation) is not None, 'controller navigation is not on by default')
profiles = (ROOT / 'App/ControllerProfiles.m').read_text()
check('vendorName' not in profiles, 'controller profiles depend on the vendor name')
check('pad.buttonOptions != nil' in profiles, 'the menu shortcut assumes an Options button')

# Every full-screen app screen with actions accepts controller focus and back.
for name in ['HomebrewView.swift', 'GameDetailView.swift', 'SettingsView.swift', 'ImportFlowView.swift', 'InGameMenuView.swift', 'ControllersView.swift']:
    check('.controllerNavigation(' in (ROOT / 'App' / name).read_text(), name + ' has no controller navigation')
check('.controllerAction("settings-page-' in (ROOT / 'App/SettingsView.swift').read_text(), 'Settings sections are not reachable by controller')
diagnostic = (ROOT / 'App/ControllersView.swift').read_text()
for token in ['controller-diagnostic-none', 'controller-diagnostic-plist', 'showDiagnostics(true)']:
    check(token in diagnostic, 'Settings > Controllers lacks ' + token)

# Info.plist keys come from the host project builder; the built app is checked by verify_build.py.
host = (ROOT / 'NativeHost/build.py').read_text()
check("GCSupportsControllerUserInteraction=True" in host, 'host build drops GCSupportsControllerUserInteraction')
check("GCSupportedGameControllers=[{'ProfileName':'ExtendedGamepad'}]" in host, 'host build drops GCSupportedGameControllers')

if failures:
    raise SystemExit('FAIL:\n' + '\n'.join(failures))
print('PASS: launch and return discovery, name-independent input, compact pads, focus on every screen, diagnostics and Info.plist declarations')
