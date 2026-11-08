#!/usr/bin/env python3
"""Build isolated native geometry parity and runtime compiler harness."""
import importlib.util
from pathlib import Path
import runpy
import subprocess
DEPENDENCY_PATHS = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'scripts/dependency_paths.py'))

ROOT = Path(__file__).resolve().parents[1]
# Shader products are read-only inputs; the harness goes to the caller's work folder.
PRODUCTS = DEPENDENCY_PATHS['shader_deps']()
WORK = DEPENDENCY_PATHS['shader_work']()

spec=importlib.util.spec_from_file_location('translation_build', ROOT/'NativeShaders/build-translation.py')
build=importlib.util.module_from_spec(spec);spec.loader.exec_module(build)
sources=[x for x in build.SOURCES if x!='TranslationHarness']+['ShaderAPI','ShaderStore','ShaderCompiler','ShaderPublication','ShaderRuntimeBridge','ShaderDiscovery','GeometryChecks','GeometryHarness']
WORK.mkdir(parents=True,exist_ok=True)
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
aot=PRODUCTS/'aot/repro/host-publish/SatchelShaderProbe.dylib'
subprocess.run(['xcrun','--sdk','macosx','swiftc','-O','-j','2','-sdk',sdk,'-target','arm64-apple-macos14.0',
    '-import-objc-header',ROOT/'NativeShaders/TranslationBridge.h',
    *[ROOT/'NativeShaders'/(s+'.swift') for s in sources],PRODUCTS/'cross-macosx/libGameShaderBridge.a',
    aot,'-Xlinker','-rpath','-Xlinker',aot.parent,'-lc++','-framework','Metal','-o',WORK/'geometry-harness'],check=True)
print('Built isolated native geometry harness')
