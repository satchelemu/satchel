#!/usr/bin/env python3
"""Build the exact SPIRV-Cross revision as an in-process game bridge."""
import concurrent.futures
import hashlib
from pathlib import Path
import os
import subprocess
import sys
import json
import shutil

ROOT = Path(__file__).resolve().parents[1]
WORK = Path(os.environ.get('SATCHEL_SHADER_DEPS', Path.home().joinpath('SatchelBuild/m5')))
SOURCE = WORK / 'spirv-cross'
PIN = 'f11ba9f0b21ba8fc15153d50a2a1ae31ab1cf8f7'

def main():
    if subprocess.check_output(['git', '-C', SOURCE, 'rev-parse', 'HEAD'], text=True).strip() != PIN:
        raise RuntimeError('SPIRV-Cross pin mismatch')
    if subprocess.check_output(['git', '-C', SOURCE, 'status', '--porcelain'], text=True).strip():
        raise RuntimeError('SPIRV-Cross source is modified')
    platform = sys.argv[1] if len(sys.argv) > 1 else 'macosx'
    out = WORK / ('cross-' + platform)
    out.mkdir(exist_ok=True)
    sdk = subprocess.check_output(['xcrun', '--sdk', platform, '--show-sdk-path'], text=True).strip()
    target = 'arm64-apple-ios17.0' if platform == 'iphoneos' else 'arm64-apple-macos14.0'
    flags = ['-std=c++17', '-O1', '-fexceptions', '-target', target, '-isysroot', sdk, '-I', SOURCE]
    sources = [SOURCE / (s + '.cpp') for s in ('spirv_cross', 'spirv_parser', 'spirv_cross_parsed_ir', 'spirv_cfg', 'spirv_glsl', 'spirv_msl')]
    sources.append(ROOT / 'Tools/ShaderBridge/GameShaderBridge.cpp')
    fingerprint = hashlib.sha256(repr([str(x) for x in flags]).encode())
    for header in sorted([*SOURCE.glob('*.h'), *SOURCE.glob('*.hpp'), ROOT/'Tools/ShaderBridge/GameShaderBridge.h']):
        fingerprint.update(header.read_bytes())
    stamp = out / 'configuration.sha256'
    changed = not stamp.exists() or stamp.read_text() != fingerprint.hexdigest()
    def compile(source):
        obj = out / (source.stem + '.o')
        if changed or not obj.exists() or obj.stat().st_mtime < source.stat().st_mtime:
            subprocess.run(['xcrun', 'clang++', *flags, '-c', source, '-o', obj], check=True)
        return obj
    with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
        objects = list(pool.map(compile, sources))
    subprocess.run(['xcrun', 'libtool', '-static', '-o', out / 'libGameShaderBridge.a', *objects], check=True)
    if platform == 'macosx':
        subprocess.run(['xcrun', 'clang++', '-dynamiclib', *objects, '-o', out / 'libGameShaderBridge.dylib'], check=True)
    stamp.write_text(fingerprint.hexdigest())
    shutil.copy2(SOURCE/'LICENSE', out/'SPIRV-Cross.LICENSE')
    print('Built ' + platform + ' in-process bridge at ' + PIN)

if __name__ == '__main__': main()
