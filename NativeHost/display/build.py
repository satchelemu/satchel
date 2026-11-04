#!/usr/bin/env python3
"""Build display's host archive, then the canonical preparation app. No device actions."""
from pathlib import Path
import os
import json
import copy
import hashlib
import shutil
import subprocess
import sys
import time

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
OUT = Path(os.environ.get('SATCHEL_DISPLAY_OUT', Path.home() / 'SatchelBuild/display2'))
HOST = Path.home() / 'SatchelBuild/host'
os.environ.update(SATCHEL_HOST_OUT=str(OUT), SATCHEL_PREPARE_OUT=str(OUT / 'dev'), PYTHONDONTWRITEBYTECODE='1')
sys.path.insert(0, str(ROOT / 'NativeHost'))
import build



def xcode_build_running():
    matches = subprocess.run(['pgrep', '-f', 'xcodebuild'], capture_output=True, text=True)
    for pid in matches.stdout.split():
        command = subprocess.run(['ps', '-p', pid, '-o', 'comm='], capture_output=True, text=True).stdout.strip()
        if Path(command).name == 'xcodebuild':
            return True
    return False


def idle():
    while xcode_build_running():
        print('Waiting for the other xcodebuild to finish.', flush=True)
        time.sleep(15)


def run(args, **kwargs):
    subprocess.run([str(x) for x in args], check=True, **kwargs)


OUT.mkdir(parents=True, exist_ok=True)
# Generic utility assets and the unchanged graphics/media archives are reused.
shutil.copytree(HOST / 'NativeAssets', OUT / 'NativeAssets', dirs_exist_ok=True)
products = OUT / 'DerivedData/Build/Products/Release-iphoneos'
products.mkdir(parents=True, exist_ok=True)
for name in ['SatchelNativeGraphics', 'SatchelNativeMedia']:
    shutil.copyfile(HOST / 'DerivedData/Build/Products/Release-iphoneos' / ('lib' + name + '.a'), products / ('lib' + name + '.a'))
idle()
env = build.prepare()
# The host's resource script expects utility metallibs at engine/dm/host/gen/msl-iphoneos.
source = HOST / 'engine/dm/host/gen/msl-iphoneos'
if source.exists():
    shutil.copytree(source, OUT / 'engine/dm/host/gen/msl-iphoneos', dirs_exist_ok=True)
idle()
run(['nice', '-n', '10', 'xcodebuild', '-quiet', '-project', OUT / 'SatchelDev.xcodeproj',
     '-target', 'SatchelNativeHost', '-configuration', 'Release', '-sdk', 'iphoneos',
     'SYMROOT=' + str(OUT / 'DerivedData/Build/Products'), 'OBJROOT=' + str(OUT / 'DerivedData/Build/Intermediates.noindex'),
     '-jobs', '2', 'build'], env=env)
# Rebuild only the changed graphics translation unit, preserving every other archive member.
# Full host builds already select this overlay through display_overlay.py.
project = json.loads((OUT / 'project.json').read_text())
thermal = copy.deepcopy(project['targets']['SatchelNativeGraphics'])
thermal['sources'] = [row for row in thermal['sources'] if Path(row['path']).name == 'hle_nvn.c']
assert len(thermal['sources']) == 1 and '/overlays/display/' in thermal['sources'][0]['path']
thermal['settings']['base']['PRODUCT_NAME'] = 'SatchelDisplayThermal'
project['targets']['SatchelDisplayThermal'] = thermal
thermal_project = OUT / 'thermal-project.json'
thermal_project.write_text(json.dumps(project, indent=2) + '\n')
run(['xcodegen', 'generate', '--spec', thermal_project, '--project', OUT])
idle()
run(['nice', '-n', '10', 'xcodebuild', '-quiet', '-project', OUT / 'SatchelDev.xcodeproj',
     '-target', 'SatchelDisplayThermal', '-configuration', 'Release', '-sdk', 'iphoneos',
     'SYMROOT=' + str(OUT / 'DerivedData/Build/Products'), 'OBJROOT=' + str(OUT / 'DerivedData/Build/Intermediates.noindex'),
     '-jobs', '2', 'build'], env=env)
objects = OUT / 'thermal-object'; objects.mkdir(exist_ok=True)
run(['ar', '-x', products / 'libSatchelDisplayThermal.a', 'hle_nvn.o'], cwd=objects)
archive = products / 'libSatchelNativeGraphics.a'
assert subprocess.check_output(['ar', '-t', archive], text=True).splitlines().count('hle_nvn.o') == 1
members = subprocess.check_output(['ar', '-t', archive], text=True).splitlines()
def member_hashes():
    return {name: hashlib.sha256(subprocess.check_output(['ar', '-p', archive, name])).hexdigest()
            for name in members if name not in ('__.SYMDEF', 'hle_nvn.o')}
unchanged = member_hashes()
run(['ar', '-r', archive, objects / 'hle_nvn.o'])
run(['ranlib', archive])
assert member_hashes() == unchanged
(OUT / 'graphics-preserved-members.json').write_text(json.dumps(unchanged, indent=2) + '\n')
assert subprocess.check_output(['ar', '-t', archive], text=True).splitlines().count('hle_nvn.o') == 1
idle()
run(['python3', ROOT / 'NativePrepare/build-dev.py'])
