#!/usr/bin/env python3
"""Exercise background shader work, cache reuse and signals with a synthetic EXIT shader."""
import json, os, subprocess, sys
from pathlib import Path
import runpy
DEPENDENCY_PATHS = runpy.run_path(str(Path(__file__).resolve().parents[1] / 'scripts/dependency_paths.py'))
root=Path(__file__).resolve().parents[1]
out=Path(os.environ['SATCHEL_OUT']) / 'scheduling-check'
assert not out.resolve().is_relative_to(root)
out.mkdir(parents=True, exist_ok=True)
sources=json.loads((root/'NativeShaders/integration.json').read_text())['swiftSources']
deps=DEPENDENCY_PATHS['shader_deps']()
aot=deps/'aot/repro/host-publish/SatchelShaderProbe.dylib'
command=['xcrun','swiftc','-O','-num-threads','2','-target','arm64-apple-macos15.0','-import-objc-header',root/'NativeShaders/ArchiveTranslationBridge.h',*[root/s for s in sources],root/'App/NativeGameProfile.swift',root/'NativeHost/PhoneShaderRuntime.swift',root/'NativeHost/ShaderWarmupResources.swift',root/'NativeHost/ShaderSchedulingChecks.swift',deps/'cross-macosx/libGameShaderBridge.a',deps/'archive-macosx/libShaderArchive.a',aot,'-Xlinker','-rpath','-Xlinker',aot.parent,'-lc++','-framework','Metal','-o',out/'check']
subprocess.run(command,check=True)
subprocess.run([out/'check',out/'synthetic-cache'],check=True)
subprocess.run([out/'check',out/'synthetic-cache'],check=True)
