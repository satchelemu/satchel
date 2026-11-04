#!/usr/bin/env python3
"""Compile the production renderer with the existing pinned-input and exact-export checks."""
import argparse
import json
from pathlib import Path
import plistlib
import runpy
import shutil
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source', type=Path, required=True)
parser.add_argument('--dependencies', type=Path, required=True)
args = parser.parse_args()
root = args.source.resolve()
snapshot = args.dependencies.resolve()/'tmp-snapshot-2026-11-01/tmp'
recipe = runpy.run_path(str(root/'Tools/GuestGraphics/build_raster.py'))
validate = recipe['validate_dependencies']
validate.__globals__['MVK'] = snapshot/'satchel-vulkan-backend'
validate.__globals__['REF'] = snapshot/'satchel-switch-integration/NX2Apple'
# Verify official tar digests, every consumed header/library byte, both pins and the NVN oracle.
packages = validate(True)
package, library = packages['ios']
output = snapshot/'satchel-vulkan-backend/build/ios'
framework = output/'SatchelVulkan.framework'
headers = framework/'Headers'; headers.mkdir(parents=True, exist_ok=True)
vulkan = root/'Tools/VulkanBackend'; guest = root/'Tools/GuestGraphics'
for name in ['VulkanBackend.h','VulkanDevice.h','VulkanBuffer.h','VulkanSubmission.h','VulkanTransferBatch.h','VulkanCompute.h','VulkanScalar.h','VulkanRenderTarget.h','VulkanRaster.h','VulkanRasterBatch.h','VulkanFlat.h','VulkanCapturedHost.h','VulkanProvoking.h','RasterCompiler.h']:
    shutil.copy2(vulkan/name, headers/name)
shutil.copy2(root/'Tools/ScalarCompiler/ScalarCompiler.h', headers/'ScalarCompiler.h')
shutil.copy2(guest/'GuestDeviceBackend.h', headers/'GuestDeviceBackend.h')
(framework/'Modules').mkdir(exist_ok=True)
(framework/'Modules/module.modulemap').write_text('framework module SatchelVulkan { umbrella header "VulkanBackend.h" header "VulkanFlat.h" header "VulkanCapturedHost.h" header "VulkanProvoking.h" export * }\n')
api = json.loads((guest/'raster-source-lock.json').read_text())['framework_exports']
exports = output/'exports.txt'; exports.write_text(''.join(n+'\n' for n in sorted(api)))
sdk = subprocess.check_output(['xcrun','--sdk','iphoneos','--show-sdk-path'], text=True).strip()
flags = ['-std=c++17','-O2','-fvisibility=hidden','-Wall','-Wextra','-Werror','-Wno-missing-field-initializers','-target','arm64-apple-ios17.0','-isysroot',sdk,'-I',vulkan,'-I',guest,'-I',package/'include']
links = [x for name in ['Foundation','Metal','QuartzCore','CoreGraphics','IOSurface','UIKit'] for x in ['-framework',name]]
command = ['xcrun','clang++',*flags,'-dynamiclib',vulkan/'VulkanBackend.cpp',library,*links,'-Wl,-install_name,@rpath/SatchelVulkan.framework/SatchelVulkan','-Wl,-exported_symbols_list,'+str(exports),'-o',framework/'SatchelVulkan']
subprocess.run(list(map(str,command)), check=True)
symbols = subprocess.check_output(['nm','-gU',str(framework/'SatchelVulkan')], text=True)
assert {line.split()[-1] for line in symbols.splitlines()} == set(api), 'Framework public ABI differs'
assert 'IOKit' not in subprocess.check_output(['otool','-L',str(framework/'SatchelVulkan')],text=True)
shutil.copytree(vulkan/'Notices',framework/'Notices',dirs_exist_ok=True)
shutil.copy2(vulkan/'ComputeOriginal.json',framework/'Notices/compute-original.json')
for directory,names in [('ScalarCompiler',['NOTICES.md','Reference-LICENSE.txt','source-lock.json']),('RasterCompiler',['NOTICES.md','Reference-LICENSE.txt','NVN-Header-LICENSE.txt','admission-checkpoint.json'])]:
    notices=framework/'Notices'/directory;notices.mkdir(exist_ok=True)
    for name in names: shutil.copy2(root/'Tools'/directory/name,notices/name)
(framework/'Info.plist').write_bytes(plistlib.dumps(dict(CFBundleIdentifier='com.dell.satchel.vulkan',CFBundleName='SatchelVulkan',CFBundleExecutable='SatchelVulkan',CFBundlePackageType='FMWK',CFBundleVersion='1',CFBundleShortVersionString='0.1',MinimumOSVersion='17.0',CFBundleSupportedPlatforms=['iPhoneOS'])))
(output/'production-rebuild.json').write_text(json.dumps({'command':list(map(str,command)),'exports':sorted(api),'scope':'Production compile and pin/export validation only. Historical GPU test gates were not rerun by this command.'},indent=2)+'\n')
print('Built unsigned production renderer. No GPU or phone execution claimed.')
