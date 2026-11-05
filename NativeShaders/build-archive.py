#!/usr/bin/env python3
"""Build the pinned BSD-licensed zstd decoder without compiler or CLI programs."""
from pathlib import Path
import os
import shutil
import subprocess
import sys

WORK = Path(os.environ.get('SATCHEL_SHADER_DEPS', Path.home().joinpath('SatchelBuild/m5')))
ROOT = Path(__file__).resolve().parents[1]
PIN = 'f8745da6ff1ad1e7bab384bd1f9d742439278e99'

from build_receipt import inputs, record

def main():
    before = inputs('archive')
    platform = sys.argv[1] if len(sys.argv)>1 else 'macosx'
    if platform not in ('macosx','iphoneos'): raise ValueError('Unsupported SDK')
    source = WORK/'zstd'
    if subprocess.check_output(['git','-C',source,'rev-parse','HEAD'],text=True).strip()!=PIN:
        raise RuntimeError('zstd revision mismatch')
    if subprocess.check_output(['git','-C',source,'status','--porcelain'],text=True).strip():
        raise RuntimeError('zstd source is modified')
    output = WORK/('archive-'+platform); output.mkdir(exist_ok=True)
    amalgamation = output/'zstddeclib.c'
    subprocess.run([sys.executable,source/'build/single_file_libs/combine.py','-r',source/'lib',
        '-x','legacy/zstd_legacy.h','-o',amalgamation,source/'build/single_file_libs/zstddeclib-in.c'],check=True)
    sdk = subprocess.check_output(['xcrun','--sdk',platform,'--show-sdk-path'],text=True).strip()
    target = 'arm64-apple-ios17.0' if platform=='iphoneos' else 'arm64-apple-macos14.0'
    objects = []
    for path in [amalgamation,ROOT/'Tools/ShaderBridge/ArchiveBridge.c']:
        obj = output/(path.stem+'.o'); objects.append(obj)
        subprocess.run(['xcrun','clang','-O2','-target',target,'-isysroot',sdk,'-I',source/'lib','-c',path,'-o',obj],check=True)
    subprocess.run(['xcrun','libtool','-static','-o',output/'libShaderArchive.a',*objects],check=True)
    shutil.copy2(source/'LICENSE',output/'Zstandard.BSD.txt')
    record('archive', output, ['libShaderArchive.a'], before)
    print('Built BSD zstd decoder '+PIN+' for '+platform)

if __name__=='__main__': main()
