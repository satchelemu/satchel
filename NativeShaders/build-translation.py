#!/usr/bin/env python3
"""Build the same Swift/C++/NativeAOT translation path used by iOS."""
from pathlib import Path
import runpy
import subprocess
import sys
DEPENDENCY_PATHS = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'scripts/dependency_paths.py'))

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ['Text', 'ShaderHelpers', 'Variants', 'Fetch', 'BaseTransforms', 'VertexEntries', 'Feedback',
           'Reflection', 'RasterFacts', 'ReflectionJSON', 'Spirv', 'BufferAnalysis', 'ShaderRecord', 'SourcePipeline', 'Geometry', 'Tessellation', 'Translation', 'TranslationHarness']
# Translation.swift times its NativeAOT call with SatchelShaderMetrics (ShaderCompiler.swift, which
# uses ShaderAPI's types). A harness that builds only SOURCES adds these two; scripts that already
# name them (build-api.py, build-geometry.py) keep their own lists.
METRICS = ['ShaderAPI', 'ShaderCompiler']

def main():
    platform = sys.argv[1] if len(sys.argv) > 1 else 'macosx'
    if platform not in ('macosx', 'iphoneos'):
        raise ValueError('Expected macosx or iphoneos')
    # Resolved here, not at import: other tools import this file for SOURCES and METRICS.
    # Shader products are read-only inputs; the harness goes to the caller's work folder.
    products, work = DEPENDENCY_PATHS['shader_deps'](), DEPENDENCY_PATHS['shader_work']()
    work.mkdir(parents=True, exist_ok=True)
    cross = products / ('cross-' + platform) / 'libGameShaderBridge.a'
    if platform == 'iphoneos':
        sdk = subprocess.check_output(['xcrun', '--sdk', platform, '--show-sdk-path'], text=True).strip()
        flags = ['-sdk', sdk, '-target', 'arm64-apple-ios17.0', '-F', products/'aot', '-framework', 'SatchelShaderProbe']
        output = work/'translation-harness-ios'
    else:
        aot = products / 'aot/repro/host-publish/SatchelShaderProbe.dylib'
        flags = [aot, '-Xlinker', '-rpath', '-Xlinker', aot.parent]
        output = work/'translation-harness'
    subprocess.run(['xcrun', '--sdk', platform, 'swiftc', '-O', '-j', '2', '-import-objc-header', ROOT/'NativeShaders/TranslationBridge.h',
        *[ROOT/'NativeShaders'/(name+'.swift') for name in SOURCES + METRICS], cross, '-lc++', *flags, '-o', output], check=True)
    print('Built native translation harness for ' + platform + '; no execution implied')

if __name__ == '__main__': main()
