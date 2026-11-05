#!/usr/bin/env python3
"""Compile the production warm-up and actual package verifier for Mac measurement."""
from pathlib import Path
import json,os,shutil,subprocess
root=Path(os.environ.get('SATCHEL_WARMUP_SOURCE', Path(__file__).resolve().parents[2]))
out=Path(os.environ.get('SATCHEL_WARMUP_OUT',Path.home()/'SatchelBuild/dlc/warmup/mac'))
out=out.expanduser().resolve()
assert not out.is_relative_to(root), 'Keep warm-up products outside the repository'
out.mkdir(parents=True,exist_ok=True)
def run(args):subprocess.run([str(x) for x in args],check=True,cwd=root)
header=out/'Bridge.h'
header.write_text(''.join('#include "'+str(root/p)+'"\n' for p in ['NativeShaders/ArchiveTranslationBridge.h','Tools/ShaderBridge/RuntimeShaderBridge.h','NativeHost/graphics/WarmupPipelines.h']))
objects=[]
providers=[root/'NativeHost'/p for p in ['graphics/WarmupPipelines.m','graphics/PipelineSettings.m','PlayerSettings.m']]
# Older comparison snapshots predate the controller provider dependency.
if (root/'App/ControllerProfiles.m').exists(): providers.append(root/'App/ControllerProfiles.m')
for source in providers:
 obj=out/(source.stem+'.o');objects.append(obj)
 run(['xcrun','clang','-O1','-fobjc-arc','-mmacosx-version-min=26.0','-c',source,'-o',obj])
source=(root/'NativeHost/PreparedGame.swift').read_text().split('\n/// The background time around the package check, ended once.')[0].replace('import SwiftUI','import Foundation')
for scope in ['fm','FileManager.default']:
 for directory in ['documentDirectory','applicationSupportDirectory']:
  source=source.replace(f'{scope}.urls(for: .{directory}, in: .userDomainMask)[0]','URL(fileURLWithPath: CommandLine.arguments[2])')
verifier=out/'Verifier.swift';verifier.write_text(source+'\nfunc SatchelNativeSessionStarted() -> Bool { false }\n')
manifest=json.loads((root/'NativeShaders/integration.json').read_text())
sources=[root/p for p in manifest['swiftSources']]
sources += [root/p for p in ['App/NativeGameProfile.swift','App/PreparedAddOn.swift','NativeHost/ShaderWarmupResources.swift','NativePrepare/WarmupInventory.swift','NativePrepare/WarmupRunner.swift']]
source_main=Path(os.environ.get('SATCHEL_WARMUP_MAIN',root/'Tests/Warmup/Harness.swift'))
if source_main.name == 'Checks.swift':
 sources.append(root/'Tests/Warmup/M5Checks.swift')
 fixture=out/'synthetic.metal'
 fixture.write_text('#include <metal_stdlib>\nusing namespace metal; kernel void warm_fixture() {}\n')
 run(['xcrun','--sdk','macosx','metal','-std=metal3.2','-mmacosx-version-min=26.0','-c',fixture,'-o',out/'synthetic.air'])
 run(['xcrun','--sdk','macosx','metallib',out/'synthetic.air','-o',out/'synthetic.metallib'])
deps=Path(os.environ.get('SATCHEL_DEPS_ROOT',Path.home()/'SatchelDeps'))/'m5';aot=deps/'aot/repro/host-publish/SatchelShaderProbe.dylib'
run(['xcrun','swiftc','-swift-version','5','-O','-num-threads','2','-target','arm64-apple-macos26.0','-import-objc-header',header,*sources,verifier,source_main,*objects,
 deps/'cross-macosx/libGameShaderBridge.a',deps/'archive-macosx/libShaderArchive.a',aot,'-Xlinker','-rpath','-Xlinker',aot.parent,'-lc++','-framework','Metal','-framework','GameController','-o',out/'warmup'])
for name in ['NativeGameProfiles.json','NativeRuntimeProfiles.json']:(out/name).write_bytes((root/'App'/name).read_bytes())
# Shared build guards recognize this executable as heavy shader work.
shutil.copy2(out/'warmup',out/'shader-harness')
print('Built production warm-up Mac harness')
