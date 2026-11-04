#!/usr/bin/env python3
"""Build isolated native geometry parity and runtime compiler harness."""
import importlib.util
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
WORK = Path.home()/'SatchelBuild/m5'

spec=importlib.util.spec_from_file_location('translation_build', ROOT/'NativeShaders/build-translation.py')
build=importlib.util.module_from_spec(spec);spec.loader.exec_module(build)
sources=[x for x in build.SOURCES if x!='TranslationHarness']+['ShaderAPI','ShaderStore','ShaderCompiler','ShaderPublication','ShaderRuntimeBridge','ShaderDiscovery','GeometryChecks','GeometryHarness']
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
aot=WORK/'aot/repro/host-publish/SatchelShaderProbe.dylib'
subprocess.run(['xcrun','--sdk','macosx','swiftc','-O','-j','2','-sdk',sdk,'-target','arm64-apple-macos14.0',
    '-import-objc-header',ROOT/'NativeShaders/TranslationBridge.h',
    *[ROOT/'NativeShaders'/(s+'.swift') for s in sources],WORK/'cross-macosx/libGameShaderBridge.a',
    aot,'-Xlinker','-rpath','-Xlinker',aot.parent,'-lc++','-framework','Metal','-o',WORK/'geometry-harness'],check=True)
print('Built isolated native geometry harness')
