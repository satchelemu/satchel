#!/usr/bin/env python3
"""Build a synthetic UIKit fixture against the real menu, layout and gamepad sources."""
from pathlib import Path
import json
import subprocess
import time
ROOT = Path(__file__).resolve().parents[2]
OUT = Path.home() / 'SatchelBuild/display2'
ENGINE = OUT / 'engine'
SIM = OUT / 'simulator'
SIM.mkdir(exist_ok=True)
settings = {'PRODUCT_BUNDLE_IDENTIFIER': 'com.dell.satchel.displayfixture', 'CODE_SIGNING_ALLOWED': 'NO',
            'IPHONEOS_DEPLOYMENT_TARGET': '27.1', 'CLANG_ENABLE_OBJC_ARC': 'YES', 'GCC_PREPROCESSOR_DEFINITIONS': ['$(inherited)', 'DEBUG=1'],
            'HEADER_SEARCH_PATHS': [str(ROOT/'NativeHost'), str(ENGINE/'dm/host'), str(ENGINE/'dm/host/gen/0100F2C0115B6000'), str(ENGINE/'dm/gfx')],
            'OTHER_LDFLAGS': ['-framework', 'UIKit', '-framework', 'GameController', '-framework', 'CoreMotion', '-framework', 'CoreText']}
project = {'name': 'DisplayFixture', 'targets': {'DisplayFixture': {'type': 'application', 'platform': 'iOS',
    'settings': {'base': settings}, 'sources': [{'path':str(ROOT/'NativeHost'/n)} for n in ['DisplayControls.m','PlayerSettings.m','InGameMenu.m','display/simulator.m']] +
        [{'path':str(OUT/'overlays/display/dm/ios/Sources/DmGamepad.m')}, {'path':str(ROOT/'NativeHost/PlayerProfiles.plist'), 'buildPhase':'resources'}],
    'info': {'path':str(SIM/'Info.plist'), 'properties': {'CFBundleDisplayName':'Satchel display fixture', 'UILaunchScreen':{},
        'UISupportedInterfaceOrientations':['UIInterfaceOrientationLandscapeLeft','UIInterfaceOrientationLandscapeRight'],
        'UIApplicationSceneManifest':{'UIApplicationSupportsMultipleScenes':False, 'UISceneConfigurations':{}}}}}}}
(SIM/'project.json').write_text(json.dumps(project))
subprocess.run(['xcodegen','generate','--spec',SIM/'project.json','--project',SIM],check=True)
while True:
    matches = subprocess.run(['pgrep','-f','xcodebuild'],capture_output=True,text=True)
    active = any(Path(subprocess.run(['ps','-p',pid,'-o','comm='],capture_output=True,text=True).stdout.strip()).name == 'xcodebuild' for pid in matches.stdout.split())
    if not active: break
    print('Waiting for xcodebuild before simulator fixture.',flush=True); time.sleep(15)
subprocess.run(['nice','-n','10','xcodebuild','-quiet','-project',SIM/'DisplayFixture.xcodeproj','-scheme','DisplayFixture',
    '-configuration','Debug','-sdk','iphonesimulator','-destination','generic/platform=iOS Simulator','-derivedDataPath',SIM/'DerivedData','-jobs','2','build'],check=True)
