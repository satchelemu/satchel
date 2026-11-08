#!/usr/bin/env python3
"""Build the public source API separately from the frozen parity executable."""
from pathlib import Path
import importlib.util
import runpy
import subprocess
import sys
DEPENDENCY_PATHS = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'scripts/dependency_paths.py'))

ROOT = Path(__file__).resolve().parents[1]

def main():
    platform = sys.argv[1] if len(sys.argv) > 1 else 'macosx'
    if platform not in ('macosx', 'iphoneos'):
        raise ValueError('Expected macosx or iphoneos')
    # Shader products are read-only inputs; the API library and harness go to the caller's work folder.
    products, work = DEPENDENCY_PATHS['shader_deps'](), DEPENDENCY_PATHS['shader_work']()
    spec = importlib.util.spec_from_file_location('translation_build', ROOT/'NativeShaders/build-translation.py')
    build = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(build)
    general = '--general' in sys.argv
    sources = [s for s in build.SOURCES if s != 'TranslationHarness'] + ['ShaderAPI']
    if general: sources += ['ShaderStore', 'ShaderCompiler', 'ShaderWarmup', 'PipelineArchive', 'ShaderDiscovery', 'ShaderArchiveDecoder', 'ShaderPublication', 'ShaderRuntimeBridge']
    output = work/(('api-general-' if general else 'api-')+platform)
    output.mkdir(parents=True, exist_ok=True)
    sdk = subprocess.check_output(['xcrun', '--sdk', platform, '--show-sdk-path'], text=True).strip()
    if platform == 'iphoneos':
        flags = ['-target', 'arm64-apple-ios17.0', '-F', products/'aot', '-framework', 'SatchelShaderProbe']
    else:
        aot = products/'aot/repro/host-publish/SatchelShaderProbe.dylib'
        flags = ['-target', 'arm64-apple-macos14.0', aot, '-Xlinker', '-rpath', '-Xlinker', aot.parent]
    bridge = ROOT/'NativeShaders'/('ArchiveTranslationBridge.h' if general else 'TranslationBridge.h')
    extra = [products/('archive-'+platform)/'libShaderArchive.a', '-framework', 'Metal'] if general else []
    subprocess.run(['xcrun', '--sdk', platform, 'swiftc', '-O', '-j', '1', '-sdk', sdk,
        '-import-objc-header', bridge,
        '-emit-library', '-emit-module', '-module-name', 'SatchelShaders',
        '-emit-module-path', output/'SatchelShaders.swiftmodule',
        *[ROOT/'NativeShaders'/(s+'.swift') for s in sources],
        products/('cross-'+platform)/'libGameShaderBridge.a', '-lc++', *extra, *flags,
        '-o', output/'libSatchelShaders.dylib'], check=True)
    if platform == 'macosx':
        harnessExtra = [ROOT/'NativeShaders/WarmupChecks.swift'] if general else []
        subprocess.run(['xcrun', '--sdk', platform, 'swiftc', '-O', '-j', '1', '-sdk', sdk,
            '-import-objc-header', bridge,
            *[ROOT/'NativeShaders'/(s+'.swift') for s in sources],
            *harnessExtra, ROOT/'NativeShaders'/('GeneralHarness.swift' if general else 'APIHarness.swift'), products/('cross-'+platform)/'libGameShaderBridge.a',
            '-lc++', *extra, *flags, '-o', output/('general-harness' if general else 'api-harness')], check=True)
    print('Built public shader API for '+platform+'; no execution implied')

if __name__ == '__main__': main()
