#!/usr/bin/env python3
"""Build baseline and service-overlay Mac hosts from the pinned engine.

The pinned engine checkout stays read-only: its generated inputs (the compile-time
profile header, the host Metal shader header, libvpx and dav1d) are built into
SATCHEL_OUT, with the compile lines of dm/host/Makefile.
"""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import argparse
import hashlib
import json
import os
import runpy
import subprocess
import sys
DEPENDENCY_PATHS = runpy.run_path(str(Path(__file__).resolve().parents[2] / 'scripts/dependency_paths.py'))

parser = argparse.ArgumentParser()
parser.add_argument('--source-shaders', action='store_true', help='Mac diagnostic: compile retained Metal sources when libraries are absent')
args = parser.parse_args()
ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
OUT = Path(os.environ.get('SATCHEL_OUT', Path(os.environ.get('SATCHEL_BUILD_ROOT', Path.home().joinpath('SatchelBuild'))) / 'anygame/services')) / 'mac'
if OUT.resolve().is_relative_to(ROOT): raise SystemExit('Service build outputs must be outside the worktree.')
OUT.mkdir(parents=True, exist_ok=True)
runpy.run_path(str(ROOT / 'NativeApp/build_support.py'))['wait']()
ENGINE = DEPENDENCY_PATHS['deps_root']() / 'NX2Apple/engine'
HOST = ENGINE / 'dm/host'
pin = (ROOT / 'scripts/deps/upstream/nxnative-queue/README.md').read_text()
if 'f8a5618d5f505a4c0a0bcac7c413872d3a53c009' not in pin or not (ENGINE / 'dm/fork_patches.txt').is_file():
    raise SystemExit('The Mac service hosts need the materialized engine pin (SATCHEL_DEPS_ROOT).')
MACOS_MIN = '26.6'   # dm/mac/min_os.mk
GAME = '0100BA4026E94000'   # compile-time profile only; every run loads its own DM_PROFILE
GEN = OUT / 'gen'
subprocess.run([sys.executable, '-B', str(ENGINE / 'dm/games/game.py'), 'header', GAME, str(GEN / GAME / 'game_profile.h')], check=True)
subprocess.run(['nice', '-n', '10', sys.executable, '-B', str(ENGINE / 'dm/gfx/tools/host_msl.py'), '--sdk', 'macosx', '--min', MACOS_MIN,
                str(GEN / 'msl-macosx/host_msl_gen.h'), *map(str, sorted((ENGINE / 'dm/gfx/msl').glob('*.metal')))], check=True)


def cached(command, source, output, extra=b''):
    key = hashlib.sha256(json.dumps(command).encode() + source.read_bytes() + extra).hexdigest()
    stamp = output.with_suffix(output.suffix + '.key')
    if not output.exists() or not stamp.exists() or stamp.read_text() != key:
        output.parent.mkdir(parents=True, exist_ok=True)
        print('Compile ' + source.name, flush=True); subprocess.run(command, check=True); stamp.write_text(key)
    return str(output)


def library(name, sources, flags, headers):
    root = ENGINE / 'dm/third_party' / name
    extra = b''.join(p.read_bytes() for p in headers)
    jobs = [(['nice', '-n', '10', 'xcrun', 'clang', *flags, '-c', str(s), '-o', str(GEN / name / (str(s.relative_to(root)) + '.o'))], s,
             GEN / name / (str(s.relative_to(root)) + '.o')) for s in sources]
    with ThreadPoolExecutor(max_workers=2) as pool: objects = list(pool.map(lambda j: cached(j[0], j[1], j[2], extra), jobs))
    archive = GEN / name / ('lib' + name + '.a')
    archive.unlink(missing_ok=True)
    subprocess.run(['ar', 'rcs', str(archive)] + objects, check=True)
    return archive


VPX = ENGINE / 'dm/third_party/libvpx'
vpx = library('libvpx', sorted(VPX.rglob('*.c')), ['-O2', '-g', '-mmacosx-version-min=' + MACOS_MIN, '-I' + str(VPX), '-I' + str(VPX / 'config')],
              sorted((VPX / 'config').glob('*.h')))
DAV1D = ENGINE / 'dm/third_party/dav1d'
dav1d_sources = sorted([p for p in (DAV1D / 'src').glob('*.c') if not p.name.endswith('_tmpl.c')] + [DAV1D / 'src/arm/cpu.c'] +
                       list((DAV1D / 'config/tmpl').glob('*.c')) +
                       [p for p in (DAV1D / 'src/arm/64').glob('*.S') if not p.name.endswith('_tmpl.S') and p.name != 'util.S'])
dav1d_headers = sorted(set((DAV1D / 'config').glob('*.h')) | set((DAV1D / 'src').glob('*.h')) | set((DAV1D / 'src').glob('*_tmpl.c')) |
                       set((DAV1D / 'src/arm').glob('*.h')) | {DAV1D / 'src/arm/asm.S', DAV1D / 'src/arm/64/util.S'} |
                       set((DAV1D / 'src/arm/64').glob('*_tmpl.S')) | set((DAV1D / 'include').glob('*/*.h')))
dav1d = library('dav1d', dav1d_sources, ['-O2', '-g', '-mmacosx-version-min=' + MACOS_MIN, '-fvisibility=hidden', '-fomit-frame-pointer', '-fno-align-functions',
                                         '-I' + str(DAV1D), '-I' + str(DAV1D / 'include'), '-I' + str(DAV1D / 'config')], dav1d_headers)

overlay_tools = runpy.run_path(str(HERE / 'overlay.py'))
overlay = overlay_tools['prepare'](HOST / 'hle_sf.c', OUT / 'services')
filesystem = overlay_tools['prepare_fs'](HOST / 'hle_fs.c', OUT / 'services')
clock = overlay_tools['prepare_time'](HOST / 'hle_time.c', OUT / 'services')
kernel = overlay_tools['prepare_svc'](HOST / 'hle_svc.c', OUT / 'services')
# The Makefile's SRC and GFX lists. Its link line names stb_vorbis.c too, but hle_movie.c
# already includes that single-file decoder, so only its include directory is used here.
sources = [HOST / n for n in ['main.c', 'loader.c', 'hle.c', 'crash.c', 'probe.c', 'thread.c', 'save_sync.c', 'memstat.m', 'haptics_apple.m']]
sources += sorted(HOST.glob('hle_*.c')) + [HOST / 'front_mac.m', HOST / 'web_apple.m', HOST / 'entry.S']
for directory, suffixes in [('gfx', ('.c',)), ('gfx/nvn', ('.c', '.m', '.S')), ('gfx/tex', ('.c', '.m')), ('gfx/vk', ('.m',)), ('gfx/gl', ('.m',))]:
    sources += sorted(p for p in (ENGINE / 'dm' / directory).iterdir() if p.suffix in suffixes)
sources = list(dict.fromkeys(sources))
if args.source_shaders:
    original = ENGINE / 'dm/gfx/nvn/nvn_mtl.m'
    text = original.read_text()
    anchor = 'static id<MTLLibrary> lib_from_file(NSString *path, NSError **err) {'
    assert text.count(anchor) == 1
    text = text.replace(anchor, anchor + '\n' + (HERE / 'MacSourceShaders.inc').read_text())
    generated = OUT / 'source-shaders'; generated.mkdir(exist_ok=True)
    target = generated / original.name; target.write_text(text)
    sources = [target if p == original else p for p in sources]
support = overlay_tools['prepare_support']
supported = {p.name: support(p, OUT / 'services') for p in sources if p.name in ('hle_thread.c', 'hle_misc.c')}
includes = [ENGINE / 'dm/gfx/nvn', HOST, ENGINE / 'dm/gfx/nvn/include', GEN / 'msl-macosx', ENGINE / 'dm/third_party/vulkan-headers/include',
            GEN / GAME, VPX, DAV1D / 'include', ENGINE / 'dm/third_party/stb_vorbis']
base = ['nice', '-n', '10', 'xcrun', 'clang', '-O1', '-g', '-Wall', '-Wno-unused-function', '-mmacosx-version-min=' + MACOS_MIN] + ['-I' + str(p) for p in includes]
objects = OUT / 'objects'; objects.mkdir(exist_ok=True)
headers = sorted(p for d in [HOST, ENGINE / 'dm/gfx', GEN] for p in d.rglob('*') if p.suffix in ('.h', '.inc') and p.is_file())
header_hash = b''.join(p.read_bytes() for p in headers) + b''.join(p.read_bytes() for p in list(HERE.glob('*.inc')) + [HERE / 'coverage/MissingServiceNames.inc'])


def compile_one(source):
    obj = objects / (source.stem + '-' + hashlib.sha256(str(source).encode()).hexdigest()[:8] + '.o')
    flags = ['-fobjc-arc'] if source.suffix == '.m' else []
    return cached(base + flags + ['-c', str(source), '-o', str(obj)], source, obj, hashlib.sha256(header_hash).digest())


with ThreadPoolExecutor(max_workers=2) as pool: compiled = list(pool.map(compile_one, sources))
changed = {p.name: compile_one(p) for p in [overlay, clock, filesystem, kernel, *supported.values()]}
frameworks = ['Cocoa', 'WebKit', 'UserNotifications', 'QuartzCore', 'Metal', 'MetalFX', 'GameController', 'CoreHaptics', 'IOKit',
              'AudioToolbox', 'VideoToolbox', 'CoreMedia', 'CoreVideo', 'ImageIO', 'UniformTypeIdentifiers']
link = [str(vpx), str(dav1d)]
for name in frameworks: link += ['-framework', name]
for variant in ['baseline', 'services']:
    selected = [changed[src.name] if variant == 'services' and src.name in changed else obj for src, obj in zip(sources, compiled)]
    subprocess.run(base + ['-fobjc-arc'] + selected + link + ['-o', str(OUT / ('dmhost-' + variant))], check=True)
(OUT / 'sources.json').write_text(json.dumps({str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sources + [overlay, clock, filesystem, kernel, *supported.values()]}, indent=2) + '\n')
print('PASS: baseline and service-overlay Mac hosts built from the pinned engine; they differ only in the service, clock, filesystem, kernel, thread and misc overlays')
