#!/usr/bin/env python3
"""Simulator-only suite entry, invoked by NativeApp/build.py."""
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(os.environ['SATCHEL_OUT'])
project = OUT / 'simulator'
result = OUT / ('simulator-' + time.strftime('%Y%m%d-%H%M%S') + '.xcresult')
subprocess.run(['python3', '-B', str(ROOT / 'Tests/UI/Navigation/configure.py'),
                'sim-main', '--out', str(project)], check=True)
simulator = os.environ.get('SATCHEL_UI_SIMULATOR', 'B1F54F54-BA40-4A62-86E7-2E9E6AF666C0')
command = ['nice', '-n', '10', 'xcodebuild', 'test-without-building', '-project', str(project / 'SatchelPreview.xcodeproj'),
           '-scheme', 'SatchelPreviewNavigation', '-configuration', 'Release',
           '-destination', 'platform=iOS Simulator,id=' + simulator,
           '-derivedDataPath', str(project / 'DerivedData'), '-resultBundlePath', str(result),
           '-parallel-testing-enabled', 'NO', '-jobs', '2', 'ONLY_ACTIVE_ARCH=YES']
build = command.copy()
build[4] = 'build-for-testing'
result_index = build.index('-resultBundlePath')
del build[result_index:result_index + 2]
subprocess.run(build, check=True)
subprocess.run(['xcrun', 'simctl', 'bootstatus', simulator, '-b'], check=True)
subprocess.run(['xcrun', 'simctl', 'install', simulator,
                str(project / 'DerivedData/Build/Products/Release-iphonesimulator/NavigationBackground.app')], check=True)
for test in filter(None, os.environ.get('SATCHEL_UI_ONLY_TESTING', '').split(',')):
    command.append('-only-testing:SatchelNavigationUITests/NavigationJourneyTests/' + test)
print('Result bundle:', result, flush=True)
subprocess.run(command, check=True)
