#!/usr/bin/env python3
"""Build a bounded timing client using the actual integration source manifest."""
import json
from pathlib import Path
import runpy
import subprocess
DEPENDENCY_PATHS = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'scripts/dependency_paths.py'))

ROOT=Path(__file__).resolve().parents[1]
# Shader products are read-only inputs; the client goes to the caller's work folder.
PRODUCTS=DEPENDENCY_PATHS['shader_deps']()
WORK=DEPENDENCY_PATHS['shader_work']()
manifest=json.loads((ROOT/'NativeShaders/integration.json').read_text())
sources=next(v for v in manifest.values() if isinstance(v,list) and 'NativeShaders/ShaderAPI.swift' in v)
aot=PRODUCTS/'aot/repro/host-publish/SatchelShaderProbe.dylib'
WORK.mkdir(parents=True,exist_ok=True)
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
subprocess.run(['xcrun','--sdk','macosx','swiftc','-O','-j','2','-sdk',sdk,'-target','arm64-apple-macos14.0',
    '-import-objc-header',ROOT/'NativeShaders/ArchiveTranslationBridge.h',*[ROOT/s for s in sources],ROOT/'NativeShaders/WarmupHarness.swift',
    PRODUCTS/'cross-macosx/libGameShaderBridge.a',PRODUCTS/'archive-macosx/libShaderArchive.a',aot,'-Xlinker','-rpath','-Xlinker',aot.parent,
    '-lc++','-framework','Metal','-o',WORK/'warmup-harness'],check=True)
print('Built bounded Prepare warm-up timing client')
