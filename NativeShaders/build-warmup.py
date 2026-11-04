#!/usr/bin/env python3
"""Build a bounded timing client using the actual integration source manifest."""
import json
from pathlib import Path
import subprocess

ROOT=Path(__file__).resolve().parents[1]
WORK=Path.home()/'SatchelBuild/m5'
manifest=json.loads((ROOT/'NativeShaders/integration.json').read_text())
sources=next(v for v in manifest.values() if isinstance(v,list) and 'NativeShaders/ShaderAPI.swift' in v)
aot=WORK/'aot/repro/host-publish/SatchelShaderProbe.dylib'
sdk=subprocess.check_output(['xcrun','--sdk','macosx','--show-sdk-path'],text=True).strip()
subprocess.run(['xcrun','--sdk','macosx','swiftc','-O','-j','2','-sdk',sdk,'-target','arm64-apple-macos14.0',
    '-import-objc-header',ROOT/'NativeShaders/ArchiveTranslationBridge.h',*[ROOT/s for s in sources],ROOT/'NativeShaders/WarmupHarness.swift',
    WORK/'cross-macosx/libGameShaderBridge.a',WORK/'archive-macosx/libShaderArchive.a',aot,'-Xlinker','-rpath','-Xlinker',aot.parent,
    '-lc++','-framework','Metal','-o',WORK/'warmup-harness'],check=True)
print('Built bounded Prepare warm-up timing client')
