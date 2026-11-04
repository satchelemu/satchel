#!/usr/bin/env python3
"""Build baseline and service-overlay Mac hosts from the existing allowed baseline snapshot."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import hashlib
import json
import os
import runpy
import subprocess

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
OUT = Path(os.environ.get('SATCHEL_OUT', Path(os.environ.get('SATCHEL_BUILD_ROOT', Path.home().joinpath('SatchelBuild'))) / 'anygame/services')) / 'mac'
if OUT.resolve().is_relative_to(ROOT): raise SystemExit('Service build outputs must be outside the worktree.')
OUT.mkdir(parents=True, exist_ok=True)
runpy.run_path(str(ROOT / 'NativeApp/build_support.py'))['wait']()
# This is the already-built Castlevania Mac baseline used by all three allowed boot runs.
ENGINE = Path(os.environ.get('SATCHEL_BUILD_ROOT', Path.home().joinpath('SatchelBuild'))) / 'castlevania/engine'
HOST = ENGINE / 'dm/host'
PINNED = Path(os.environ.get('SATCHEL_DEPS_ROOT', Path.home().joinpath('SatchelDeps'))) / 'NX2Apple/engine/dm/host'
assert (HOST / 'hle_sf.c').read_bytes() == (PINNED / 'hle_sf.c').read_bytes()
overlay = runpy.run_path(str(HERE / 'overlay.py'))['prepare'](HOST / 'hle_sf.c', OUT / 'services')
filesystem = runpy.run_path(str(HERE / 'overlay.py'))['prepare_fs'](HOST / 'hle_fs.c', OUT / 'services')
clock = runpy.run_path(str(HERE / 'overlay.py'))['prepare_time'](HOST / 'hle_time.c', OUT / 'services')
# Keep the reference rendering, capture and memory overlays identical in both builds.
sources = [HOST / n for n in ['main.c', 'loader.c', 'hle.c', 'crash.c', 'probe.c', 'thread.c',
                              'save_sync.c', 'memstat.m', 'haptics_apple.m', 'front_mac.m', 'entry.S']]
sources += sorted(HOST.glob('hle_*.c'))
for directory, suffixes in [('gfx',('.c',)), ('gfx/nvn',('.c','.m','.S')), ('gfx/tex',('.c','.m')), ('gfx/vk',('.m',)), ('gfx/gl',('.m',))]:
    sources += sorted(p for p in (ENGINE / 'dm' / directory).iterdir() if p.suffix in suffixes)
sources = list(dict.fromkeys(sources))
support = runpy.run_path(str(HERE / 'overlay.py'))['prepare_support']
sources = [support(p, OUT / 'services') if p.name in ('hle_thread.c', 'hle_misc.c') else p for p in sources]
includes = [HOST, ENGINE / 'dm/gfx/nvn/include', HOST / 'gen/msl-macosx',
            ENGINE / 'dm/third_party/vulkan-headers/include', HOST / 'gen/0100BA4026E94000',
            ENGINE / 'dm/third_party/libvpx', ENGINE / 'dm/third_party/stb_vorbis']
base = ['nice','-n','10','xcrun','clang','-O1','-g','-Wall','-Wno-unused-function','-mmacosx-version-min=26.0'] + ['-I'+str(p) for p in includes]
objects = OUT / 'objects'; objects.mkdir(exist_ok=True)
headers = sorted(p for d in [HOST, ENGINE / 'dm/gfx'] for p in d.rglob('*') if p.suffix in ('.h','.inc') and p.is_file())
header_hash = hashlib.sha256(b''.join(p.read_bytes() for p in headers)).digest()
def compile_one(source):
    obj = objects / (source.stem + '-' + hashlib.sha256(str(source).encode()).hexdigest()[:8] + '.o')
    flags = ['-fobjc-arc'] if source.suffix == '.m' else []
    command = base + flags + ['-c',str(source),'-o',str(obj)]
    key = hashlib.sha256(header_hash + source.read_bytes() + json.dumps(command).encode() +
                         b''.join(p.read_bytes() for p in list(HERE.glob('*.inc')) + [HERE / 'coverage/MissingServiceNames.inc'])).hexdigest()
    stamp = obj.with_suffix('.key')
    if not obj.exists() or not stamp.exists() or stamp.read_text() != key:
        print('Compile '+source.name, flush=True); subprocess.run(command,check=True); stamp.write_text(key)
    return str(obj)
with ThreadPoolExecutor(max_workers=2) as pool: compiled = list(pool.map(compile_one,sources))
changed = {p.name: compile_one(p) for p in [overlay, clock, filesystem]}
frameworks = ['Cocoa','QuartzCore','Metal','MetalFX','GameController','CoreHaptics','IOKit',
              'AudioToolbox','VideoToolbox','CoreMedia','CoreVideo','ImageIO','UniformTypeIdentifiers']
link = [str(HOST / 'gen/vpx/libvpx.a')]
for name in frameworks: link += ['-framework',name]
for variant in ['baseline','services']:
    selected = [changed[src.name] if variant=='services' and src.name in changed else obj for src,obj in zip(sources,compiled)]
    subprocess.run(base + selected + link + ['-o',str(OUT / ('dmhost-'+variant))],check=True)
(OUT / 'sources.json').write_text(json.dumps({str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in sources+[overlay, clock, filesystem]},indent=2)+'\n')
print('PASS: baseline and service-overlay Mac hosts built from identical sources except service, clock and filesystem overlays')
