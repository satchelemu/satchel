#!/usr/bin/env python3
"""Run real pinned kernel, gate, TLS and transactional FS with synthetic guests."""
import os
from pathlib import Path
import runpy
import shutil
import subprocess
import tempfile
DEPENDENCY_PATHS = runpy.run_path(str(Path(__file__).resolve().parents[2] / 'scripts/dependency_paths.py'))

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
OUT = Path(os.environ.get('SESSION_OUT', Path.home() / 'SatchelBuild/session/stop-probe'))
ENGINE = DEPENDENCY_PATHS['deps_root']() / 'NX2Apple/engine'
HOST = ENGINE / 'dm/host'
OUT.mkdir(parents=True, exist_ok=True)
assert subprocess.check_output(['git', '-C', ENGINE, 'rev-parse', 'HEAD'], text=True).strip() == 'f8a5618d5f505a4c0a0bcac7c413872d3a53c009'
runpy.run_path(str(ROOT / 'NativeApp/build_support.py'))['wait']()
if shutil.disk_usage(OUT).free < 35 * 1024**3:
    raise SystemExit('Probe blocked: less than 35 GiB free')
source = runpy.run_path(str(HERE / 'overlay.py'))['prepare'](HOST, OUT / 'overlay')
base = ['nice', '-n', '10', 'xcrun', 'clang', '-O1', '-g', '-Wall', '-Werror',
        '-Wno-unused-function', '-Wno-unused-variable', '-fno-optimize-sibling-calls',
        '-I' + str(source), '-I' + str(HOST), '-I' + str(HOST / 'tests/memory_profile'), '-I' + str(HERE),
        '-I' + str(ENGINE / 'dm/third_party/libvpx'), '-I' + str(ENGINE / 'dm/third_party/stb_vorbis')]
objects = []
for i, path in enumerate([HERE / 'stop_probe.c', HERE / 'SessionBoundary.c', HERE / 'fixture.S',
                           HOST / 'entry.S', HOST / 'tests/t_fs_stub.c', HOST / 'hle_bigstack.c',
                           HOST / 'save_sync.c', source / 'hle_profile.c',
                           HERE / 'movie_probe.c', HERE / 'audio_probe.c', HERE / 'audren_probe.c',
                           HERE / 'helper_probe.c', HERE / 'memory_probe.c', HERE / 'SessionNativeWorkers.c', HERE / 'SessionState.c', HERE / 'SessionInputGate.c',
                           HERE / 'hid_probe.c', HERE / 'native_worker_probe.c', ENGINE / 'dm/gfx/nvn/nvn_dirty.c']):
    obj = OUT / ('probe-' + str(i) + '.o')
    flags = ['-finstrument-functions'] if path.suffix == '.c' and path.name not in ('SessionBoundary.c', 'SessionNativeWorkers.c', 'SessionInputGate.c', 'SessionState.c') else []
    if path.name == 't_fs_stub.c':
        # The probe links the real kernel and thread code, so the engine stub's own copies get private names.
        flags += ['-Ddm_guest_as_contains=fs_stub_guest_as_contains', '-Dguest_sym=fs_stub_guest_sym',
                  '-Ddm_thread_service_request=fs_stub_thread_service_request']
    subprocess.run(base + flags + ['-c', str(path), '-o', str(obj)], check=True)
    objects.append(str(obj))
binary = OUT / 'stop-probe'
vpx = DEPENDENCY_PATHS['mac_host_gen']() / 'vpx/libvpx.a'
subprocess.run(base + objects + [str(vpx), '-Wl,-dead_strip', '-framework', 'AudioToolbox',
                                '-o', str(binary)], check=True)
# Keep the existing engine's transaction checks intact. They include fault injection.
save_binary = OUT / 'save-transactions'
(source / 'tests').mkdir(exist_ok=True)
shutil.copyfile(HOST / 'tests/t_fs_commit_list.c', source / 'tests/t_fs_commit_list.c')
subprocess.run(base + [str(source / 'tests/t_fs_commit_list.c'), str(HOST / 'tests/t_fs_stub.c'),
                      str(HOST / 'hle_bigstack.c'), str(HOST / 'save_sync.c'),
                      str(source / 'hle_profile.c'), str(HERE / 'SessionBoundary.c'),
                      str(HERE / 'SessionNativeWorkers.c'),
                      '-o', str(save_binary)], check=True)
env = {k: v for k, v in os.environ.items() if not k.startswith('DM_')}
with tempfile.TemporaryDirectory(prefix='save-fixture-', dir=OUT) as fixture:
    subprocess.run([str(save_binary)], env={**env, 'TMPDIR': fixture}, check=True, timeout=60)
with tempfile.TemporaryDirectory(prefix='session-fixture-', dir=OUT) as fixture:
    subprocess.run([str(binary), fixture], env=env, check=True, timeout=60)
for obj in objects:
    Path(obj).unlink()
print('PASS: synthetic stop/join cycles and existing transactional-save checks. No full game executed.')
