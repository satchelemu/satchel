#!/usr/bin/env python3
"""iPhone Duo layout harness: XCTest over Satchel's production display layout.

Builds a host app from NativeHost/DisplayControls.m, PlayerSettings.m, InGameMenu.m,
App/ControllerProfiles.m and the engine gamepad with Satchel's display and controller
overlays (the same generated copy the simulator UI suite uses), then runs
DuoLayoutTests.m and the orientation policy check. Synthetic: no game, keys or phone.

Use a private simulator, never the shared one: SATCHEL_DUO_SIMULATOR=<udid> is required.
An iPhone Duo simulator gives the real inner-screen bounds for the rotation case.
Products go to SATCHEL_DUO_OUT (default ~/SatchelBuild/duo-harness).
"""
from pathlib import Path
import json
import os
import runpy
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
ENGINE = Path(os.environ.get('SATCHEL_DEPS_ROOT', Path.home() / 'SatchelDeps')) / 'NX2Apple/engine'
OUT = Path(os.environ.get('SATCHEL_DUO_OUT', Path.home() / 'SatchelBuild/duo-harness'))
simulator = os.environ.get('SATCHEL_DUO_SIMULATOR')
if not simulator:
    raise SystemExit('Set SATCHEL_DUO_SIMULATOR to a private simulator UDID (xcrun simctl create ... iPhone-Duo ...).')
OUT.mkdir(parents=True, exist_ok=True)
assert subprocess.check_output(['git', '-C', ENGINE, 'rev-parse', 'HEAD'], text=True).strip() == 'bbc3b73784751d2a706932dc69fd67627e3445d4'

subprocess.run([sys.executable, '-B', str(HERE / 'test_orientation_policy.py')], check=True)

# The same generated gamepad as Tests/UI/Navigation/configure.py: display patch, then controllers.
sys.path.insert(0, str(ROOT / 'NativeHost'))
from display_overlay import apply  # noqa: E402
apply(ROOT, ENGINE, OUT, {'SatchelNativeHost': [], 'SatchelNativeGraphics': []})
pad = OUT / 'overlays/display/dm/ios/Sources/DmGamepad.m'
controllers = runpy.run_path(str(ROOT / 'NativeHost/controllers/overlay.py'))
pad.write_text(controllers['gamepad'](pad.read_text()))
(OUT / 'overlays/controllers').mkdir(parents=True, exist_ok=True)
(OUT / 'overlays/controllers/pad_slots.h').write_text(controllers['slots']((ENGINE / 'dm/host/pad_slots.h').read_text()))

settings = {
    'PRODUCT_BUNDLE_IDENTIFIER': 'com.dell.satchel.duoharness', 'CODE_SIGNING_ALLOWED': 'NO',
    'IPHONEOS_DEPLOYMENT_TARGET': '27.1', 'CLANG_ENABLE_OBJC_ARC': 'YES', 'TARGETED_DEVICE_FAMILY': '1',
    'GCC_PREPROCESSOR_DEFINITIONS': ['$(inherited)', 'DEBUG=1'],
    # Xcode splits unquoted build-setting values on spaces; the checkout lives under "Repo - Personal".
    'HEADER_SEARCH_PATHS': [f'"{p}"' for p in [OUT / 'overlays/controllers', HERE / 'Fixtures', ROOT / 'NativeHost',
                            ROOT / 'App', ENGINE / 'dm/host', ENGINE / 'dm/gfx']],
    'OTHER_LDFLAGS': ['-framework', 'UIKit', '-framework', 'GameController', '-framework', 'CoreMotion',
                      '-framework', 'CoreText', '-Wl,-U,_satchel_pipeline_skipped_draws'],
}
info = {'CFBundleDisplayName': 'Satchel Duo harness', 'UILaunchScreen': {}, 'UIRequiresFullScreen': True,
        'UISupportedInterfaceOrientations': ['UIInterfaceOrientationLandscapeLeft', 'UIInterfaceOrientationLandscapeRight'],
        'UIApplicationSceneManifest': {'UIApplicationSupportsMultipleScenes': False, 'UISceneConfigurations': {}}}
project = {'name': 'SatchelDuoHarness', 'targets': {
    'SatchelDuoHarness': {'type': 'application', 'platform': 'iOS', 'settings': {'base': settings},
        'sources': [{'path': str(ROOT / 'NativeHost' / n)} for n in ['DisplayControls.m', 'PlayerSettings.m', 'InGameMenu.m']] +
                   [{'path': str(ROOT / 'App/ControllerProfiles.m')}, {'path': str(pad)}, {'path': str(HERE / 'HarnessApp.m')},
                    {'path': str(ROOT / 'NativeHost/PlayerProfiles.plist'), 'buildPhase': 'resources'}],
        'info': {'path': str(OUT / 'Info.plist'), 'properties': info}},
    'SatchelDuoHarnessTests': {'type': 'bundle.unit-test', 'platform': 'iOS',
        'settings': {'base': dict(settings, PRODUCT_BUNDLE_IDENTIFIER='com.dell.satchel.duoharness.tests',
                                  GENERATE_INFOPLIST_FILE=True, TEST_HOST='$(BUILT_PRODUCTS_DIR)/SatchelDuoHarness.app/SatchelDuoHarness',
                                  BUNDLE_LOADER='$(TEST_HOST)')},
        'sources': [{'path': str(HERE / 'DuoLayoutTests.m')}], 'dependencies': [{'target': 'SatchelDuoHarness'}]}},
    'schemes': {'SatchelDuoHarness': {'build': {'targets': {'SatchelDuoHarness': 'all', 'SatchelDuoHarnessTests': ['test']}},
                                      'test': {'targets': ['SatchelDuoHarnessTests']}}}}
(OUT / 'project.json').write_text(json.dumps(project, indent=2) + '\n')
subprocess.run(['xcodegen', 'generate', '--spec', str(OUT / 'project.json'), '--project', str(OUT)], check=True)
runpy.run_path(str(ROOT / 'NativeApp/build_support.py'))['wait']()
subprocess.run(['xcrun', 'simctl', 'boot', simulator], check=False, capture_output=True)
subprocess.run(['xcrun', 'simctl', 'bootstatus', simulator, '-b'], check=True)
result = OUT / ('duo-' + time.strftime('%Y%m%d-%H%M%S') + '.xcresult')
print('Result bundle:', result, flush=True)
subprocess.run(['nice', '-n', '10', 'xcodebuild', 'test', '-project', str(OUT / 'SatchelDuoHarness.xcodeproj'),
                '-scheme', 'SatchelDuoHarness', '-destination', 'platform=iOS Simulator,id=' + simulator,
                '-derivedDataPath', str(OUT / 'DerivedData'), '-resultBundlePath', str(result),
                '-parallel-testing-enabled', 'NO', '-jobs', '2', 'CODE_SIGNING_ALLOWED=NO'], check=True)
