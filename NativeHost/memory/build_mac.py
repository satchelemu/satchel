#!/usr/bin/env python3
"""Build the pinned Mac host with identical overlays for scene comparisons."""
from pathlib import Path
import argparse
import hashlib
import json
import os
import runpy
import shlex
import shutil
import subprocess
import sys
DEPENDENCY_PATHS = runpy.run_path(str(Path(__file__).resolve().parents[2] / 'scripts/dependency_paths.py'))

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--baseline', action='store_true')
a = parser.parse_args()
OUT = Path(os.environ['SATCHEL_OUT']).resolve()
if OUT.is_relative_to(ROOT): raise SystemExit('Build outputs must be outside the checkout.')
OUT.mkdir(parents=True, exist_ok=True)
runpy.run_path(str(ROOT / 'NativeApp/build_support.py'))['wait']()
sys.path.insert(0, str(ROOT / 'NativeHost'))
import shader_dependency
shader_source = shader_dependency.stage(ROOT, OUT, 'macosx')
# The same shader products shader_dependency staged from, so the dylib matches the staged sources.
aot = DEPENDENCY_PATHS['shader_deps']() / 'aot/repro/host-publish/SatchelShaderProbe.dylib'
runtime_inputs = {
    'aotPath': str(aot), 'aotSHA256': hashlib.sha256(aot.read_bytes()).hexdigest(),
    'stagedShaders': json.loads((shader_source / 'source-manifest.json').read_text())['files'],
}
library = OUT / 'libPhoneShaderRuntime.dylib'
subprocess.run(['nice', '-n', '10', 'xcrun', 'swiftc', '-target', 'arm64-apple-macos15.0', '-O', '-num-threads', '2', '-emit-library',
    '-import-objc-header', shader_source / 'Bridge.h', *[shader_source / (name + '.swift') for name in shader_dependency.SOURCES],
    ROOT / 'App/NativeGameProfile.swift', ROOT / 'NativeHost/PhoneShaderRuntime.swift', ROOT / 'NativeHost/ShaderWarmupResources.swift',
    shader_source / 'libGameShaderBridge.a', shader_source / 'libShaderArchive.a', aot, '-lc++', '-framework', 'Metal',
    '-Xlinker', '-rpath', '-Xlinker', aot.parent, '-o', library], check=True)
original = DEPENDENCY_PATHS['deps_root']() / 'NX2Apple/engine'
assert subprocess.check_output(['git', '-C', str(original), 'rev-parse', 'HEAD'], text=True).strip() == 'f8a5618d5f505a4c0a0bcac7c413872d3a53c009'
engine = OUT / 'engine'
if not engine.exists():
    shutil.copytree(original, engine, ignore=shutil.ignore_patterns('.git', 'gen', '__pycache__'), symlinks=True)
reference = Path.home() / 'SatchelBuild/castlevania/engine/dm/host/gen'
for name in ['0100BA4026E94000/game_profile.h', 'vpx/libvpx.a', 'msl-macosx/host_msl_gen.h']:
    target = engine / 'dm/host/gen' / name; target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(reference / name, target)
runpy.run_path(str(ROOT / 'NativeHost/display_overlay.py'))['apply'](ROOT, original, OUT, {'SatchelNativeHost': [], 'SatchelNativeGraphics': []})
for source in (OUT / 'overlays/display/dm').rglob('*'):
    if source.is_file(): shutil.copyfile(source, engine / 'dm' / source.relative_to(OUT / 'overlays/display/dm'))
metal = runpy.run_path(str(ROOT / 'NativeHost/graphics/overlay.py'))['prepare'](engine / 'dm/gfx/nvn/nvn_mtl.m', OUT / 'graphics')
metal = runpy.run_path(str(ROOT / 'NativeHost/memory/overlay.py'))['prepare'](metal, OUT / 'memory', reclaim=not a.baseline)
# Extra Mac-only diagnostics expose revisions retained outside the active shader
# map. Production shader cache files and the app overlay remain untouched.
text = metal.read_text()
replace = runpy.run_path(str(ROOT / 'NativeHost/memory/overlay.py'))['replace']
text = replace(text, '    const char *path = getenv("SATCHEL_SCENE_MEMORY");',
               '    uint64_t retired_shaders = satchel_phone_retired.count;\n    const char *path = getenv("SATCHEL_SCENE_MEMORY");')
text = replace(text, 'upload_buffers\\tupload_bytes\\n', 'upload_buffers\\tupload_bytes\\tretired_shaders\\n')
text = replace(text, '\\t'.join(['%llu'] * 24) + '\\n', '\\t'.join(['%llu'] * 25) + '\\n')
text = replace(text, 'upload_buffers, upload_bytes);', 'upload_buffers, upload_bytes, retired_shaders);')
metal.write_text(text)
shutil.copyfile(metal, engine / 'dm/gfx/nvn/nvn_mtl.m')
for source in metal.parent.glob('*.inc'): shutil.copyfile(source, engine / 'dm/gfx/nvn' / source.name)
# Diagnostics must never print raw guest bytes, including on an unexpected crash.
crash = (original / 'dm/host/crash.c').read_text()
first = crash.index('    for (int i = 0; i < 29; i++) fprintf(stderr, "x%d=')
last = crash.index('    dm_backtrace(', first); crash = crash[:first] + crash[last:]
first = crash.index('    {   uint64_t sp = uc->uc_mcontext->__ss.__sp;   // The frame just popped')
last = crash.index('    {   uint64_t sp = uc->uc_mcontext->__ss.__sp; int n = 0;', first); crash = crash[:first] + crash[last:]
crash = runpy.run_path(str(ROOT / 'NativeHost/session/crash_overlay.py'))['safe_stack_scan'](crash)
(engine / 'dm/host/crash.c').write_text(crash)
env = dict(os.environ, GAME='0100BA4026E94000', DM_GAME='0100BA4026E94000',
           DM_GAMES_DIR=str(Path.home() / 'SatchelBuild/castlevania/profile'), DM_GAMES_FIRST='1', J='2', MAKEFLAGS='-j2')
dry = subprocess.run(['make', '-n', '-B', 'GAME=0100BA4026E94000', 'dmhost'], cwd=engine / 'dm/host', env=env, capture_output=True, text=True, check=True)
line = next(line for line in dry.stdout.splitlines() if ' -o dmhost ' in line)
command = shlex.split(line); command[command.index('-o') + 1] = str(OUT / 'scene-harness')
command += [str(library), '-Wl,-rpath,' + str(OUT)]
subprocess.run(['nice', '-n', '10', *command], cwd=engine / 'dm/host', env=env, check=True)
assert hashlib.sha256(aot.read_bytes()).hexdigest() == runtime_inputs['aotSHA256'], 'Shared translator changed during the build'
(OUT / 'build-inputs.json').write_text(json.dumps(runtime_inputs, indent=2) + '\n')
print('PASS: instrumented Mac host built, ' + ('baseline' if a.baseline else 'candidate'))
