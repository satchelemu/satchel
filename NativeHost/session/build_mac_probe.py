#!/usr/bin/env python3
"""Build the real host stop probe, with two compiler jobs and external outputs."""
from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
import os
from pathlib import Path
import runpy
import shutil
import subprocess
DEPENDENCY_PATHS = runpy.run_path(str(Path(__file__).resolve().parents[2] / 'scripts/dependency_paths.py'))

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
ENGINE = DEPENDENCY_PATHS['deps_root']() / 'NX2Apple/engine'
HOST = ENGINE / 'dm/host'
OUT = Path(os.environ.get('SESSION_MAC_OUT', Path.home() / 'SatchelBuild/session/mac-stop'))
OUT.mkdir(parents=True, exist_ok=True)
runpy.run_path(str(ROOT / 'NativeApp/build_support.py'))['wait']()
assert shutil.disk_usage(OUT).free >= 35 * 1024**3
assert subprocess.check_output(['git', '-C', ENGINE, 'rev-parse', 'HEAD'], text=True).strip() == 'f8a5618d5f505a4c0a0bcac7c413872d3a53c009'
shutil.copyfile(ENGINE / 'LICENSE', OUT / 'NXNative-LICENSE.txt')
# Match the canonical app's privacy overlay before applying session changes.
privacy = OUT / 'crash-private.c'
crash = (HOST / 'crash.c').read_text()
first = crash.index('    for (int i = 0; i < 29; i++) fprintf(stderr, "x%d=')
last = crash.index('    dm_backtrace(', first)
crash = crash[:first] + crash[last:]
first = crash.index('    {   uint64_t sp = uc->uc_mcontext->__ss.__sp;   // The frame just popped')
last = crash.index('    {   uint64_t sp = uc->uc_mcontext->__ss.__sp; int n = 0;', first)
privacy.write_text(crash[:first] + crash[last:])
overlay = runpy.run_path(str(HERE / 'overlay.py'))['prepare'](HOST, OUT / 'host', {'crash.c': privacy})
# Use the app's capture and lifetime overlays for private shader diagnostics too.
# Captures remain disabled unless the runner requests a private output folder.
capture = OUT / 'capture-source'
capture.mkdir(exist_ok=True)
for name in ['nvn_mtl.m', 'hle_nvn.c']:
    shutil.copyfile(ENGINE / 'dm/gfx/nvn' / name, capture / name)
for patch in ['NativeGames/runtime-capture.patch', 'NativeHost/phone-capture-lifetime.patch']:
    subprocess.run(['patch', '-p4', '--fuzz=0', '-i', ROOT / patch], cwd=capture, check=True)
shutil.copyfile(ROOT / 'NativeGames/RuntimeCapture.inc', capture / 'RuntimeCapture.inc')
metal = runpy.run_path(str(HERE / 'graphics_overlay.py'))['prepare'](
    ENGINE, OUT / 'graphics', capture / 'nvn_mtl.m')
metal = runpy.run_path(str(ROOT / 'NativeHost/graphics/overlay.py'))['prepare'](metal, OUT / 'graphics-policy')
metal = runpy.run_path(str(HERE / 'mac_shader_cache.py'))['prepare'](metal)
runpy.run_path(str(HERE / 'diagnostics_overlay.py'))['prepare'](HOST / 'memstat.m', OUT / 'host/memstat.m')
sources = [HOST / name for name in ['main.c', 'loader.c', 'hle.c', 'crash.c', 'probe.c', 'thread.c',
                                  'save_sync.c', 'memstat.m', 'haptics_apple.m', 'front_mac.m', 'entry.S']]
sources += sorted(HOST.glob('hle_*.c'))
for directory in ['gfx', 'gfx/nvn', 'gfx/tex', 'gfx/vk', 'gfx/gl']:
    sources += sorted(p for p in (ENGINE / 'dm' / directory).iterdir() if p.suffix in ('.c', '.m', '.S'))
sources = list(dict.fromkeys(sources))
sources = [overlay / p.name if p.parent == HOST and (overlay / p.name).exists() else p for p in sources]
nvn = runpy.run_path(str(HERE / 'nvn_overlay.py'))['prepare'](capture / 'hle_nvn.c', OUT / 'graphics/hle_nvn.c')
dirty = runpy.run_path(str(HERE / 'dirty_overlay.py'))['prepare'](ENGINE / 'dm/gfx/nvn/nvn_dirty.c', OUT / 'graphics/nvn_dirty.c')
sources = [nvn if p.name == 'hle_nvn.c' else dirty if p.name == 'nvn_dirty.c' else p for p in sources]
sources = [metal if p.name == 'nvn_mtl.m' else p for p in sources]
sources += [HERE / n for n in ['SessionBoundary.c', 'SessionNativeWorkers.c', 'SessionInputGate.c', 'SessionCoordinator.c', 'SessionState.c', 'mac_probe_metrics.m', 'mac_stop_probe.c']]
generated = DEPENDENCY_PATHS['mac_host_gen']()
includes = [HERE, overlay, HOST, ENGINE / 'dm/gfx/nvn', ENGINE / 'dm/gfx/nvn/include',
            generated / '0100BA4026E94000', generated / 'msl-macosx',
            ENGINE / 'dm/third_party/libvpx', ENGINE / 'dm/third_party/stb_vorbis',
            ENGINE / 'dm/third_party/vulkan-headers/include']
for directory in includes: assert directory.is_dir(), directory
base = ['nice', '-n', '10', 'xcrun', 'clang', '-O1', '-g', '-Wall', '-Wno-unused-function',
        '-mmacosx-version-min=26.0', '-fno-optimize-sibling-calls'] + ['-I' + str(p) for p in includes]
headers = sorted(p for folder in [HERE, OUT / 'host', OUT / 'graphics', ENGINE / 'dm/host', ENGINE / 'dm/gfx']
                 for p in folder.rglob('*') if p.is_file() and p.suffix in ('.h', '.inc'))
header_hash = hashlib.sha256(b''.join(p.read_bytes() for p in headers)).digest()
objects = OUT / 'objects'; objects.mkdir(exist_ok=True)
def compile_one(item):
    index, source = item
    obj = objects / (str(index) + '-' + source.stem + '.o')
    flags = ['-fobjc-arc'] if source.suffix == '.m' else []
    if source.suffix in ('.c', '.m') and source.name not in ('SessionBoundary.c', 'SessionNativeWorkers.c', 'SessionInputGate.c', 'SessionCoordinator.c', 'SessionState.c'):
        flags += ['-finstrument-functions']
    if source.name == 'main.c': flags += ['-Dmain=dm_original_main']
    command = base + flags + ['-c', str(source), '-o', str(obj)]
    digest = hashlib.sha256(header_hash + source.read_bytes() + json.dumps(command).encode()).hexdigest()
    stamp = obj.with_suffix('.key')
    if not obj.exists() or not stamp.exists() or stamp.read_text() != digest:
        print('Compile ' + source.name, flush=True)
        subprocess.run(command, check=True)
        stamp.write_text(digest)
    return str(obj)
with ThreadPoolExecutor(max_workers=2) as pool:
    compiled = list(pool.map(compile_one, enumerate(sources)))
frameworks = ['Cocoa', 'QuartzCore', 'Metal', 'MetalFX', 'GameController', 'CoreHaptics', 'IOKit',
              'AudioToolbox', 'VideoToolbox', 'CoreMedia', 'CoreVideo', 'ImageIO', 'UniformTypeIdentifiers']
command = base + compiled + [str(generated / 'vpx/libvpx.a')]
for framework in frameworks: command += ['-framework', framework]
subprocess.run(command + ['-o', str(OUT / 'mac-stop-probe')], check=True)
(OUT / 'compile.json').write_text(json.dumps(dict(base=base, compiled=compiled, sources=[str(p) for p in sources],
                                                frameworks=frameworks, vpx=str(generated / 'vpx/libvpx.a')),
                                      indent=2) + '\n')
(OUT / 'sources.json').write_text(json.dumps({str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}, indent=2) + '\n')
print('Built real host stop probe. No title executed.', flush=True)
