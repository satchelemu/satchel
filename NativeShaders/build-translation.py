#!/usr/bin/env python3
"""Build the same Swift/C++/NativeAOT translation path used by iOS."""
from pathlib import Path
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
WORK = Path(os.environ.get('SATCHEL_SHADER_DEPS', Path.home().joinpath('SatchelBuild/m5')))
SOURCES = ['Text', 'ShaderHelpers', 'Variants', 'Fetch', 'BaseTransforms', 'VertexEntries', 'Feedback',
           'Reflection', 'RasterFacts', 'ReflectionJSON', 'Spirv', 'BufferAnalysis', 'ShaderRecord', 'SourcePipeline', 'Geometry', 'Translation', 'TranslationHarness']

def main():
    platform = sys.argv[1] if len(sys.argv) > 1 else 'macosx'
    if platform not in ('macosx', 'iphoneos'):
        raise ValueError('Expected macosx or iphoneos')
    cross = WORK / ('cross-' + platform) / 'libGameShaderBridge.a'
    if platform == 'iphoneos':
        sdk = subprocess.check_output(['xcrun', '--sdk', platform, '--show-sdk-path'], text=True).strip()
        flags = ['-sdk', sdk, '-target', 'arm64-apple-ios17.0', '-F', WORK/'aot', '-framework', 'SatchelShaderProbe']
        output = WORK/'translation-harness-ios'
    else:
        aot = WORK / 'aot/repro/host-publish/SatchelShaderProbe.dylib'
        flags = [aot, '-Xlinker', '-rpath', '-Xlinker', aot.parent]
        output = WORK/'translation-harness'
    subprocess.run(['xcrun', '--sdk', platform, 'swiftc', '-O', '-j', '2', '-import-objc-header', ROOT/'NativeShaders/TranslationBridge.h',
        *[ROOT/'NativeShaders'/(name+'.swift') for name in SOURCES], cross, '-lc++', *flags, '-o', output], check=True)
    print('Built native translation harness for ' + platform + '; no execution implied')

if __name__ == '__main__': main()
