#!/usr/bin/env python3
"""Synthetic lifecycle checks. All products and save fixtures are task owned."""
import os
from pathlib import Path
import runpy
import subprocess

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
OUT = Path(os.environ.get('SESSION_LIFE_OUT', Path.home() / 'SatchelBuild/session/lifecycle'))
OUT.mkdir(parents=True, exist_ok=True)
runpy.run_path(str(ROOT / 'NativeApp/build_support.py'))['wait']()
base = ['nice', '-n', '10', 'xcrun', 'clang', '-O1', '-g', '-Wall', '-Werror', '-I' + str(HERE)]
native = OUT / 'lifecycle-host-call.o'
subprocess.run(base + ['-finstrument-functions', '-fno-optimize-sibling-calls', '-c', str(HERE / 'lifecycle_host_call.c'), '-o', str(native)], check=True)
for name, sources in [
    ('lifecycle-policy', ['SessionLifecycle.c', 'lifecycle_probe.c']),
    ('lifecycle-guest', ['SessionBoundary.c', 'lifecycle_guest_probe.c', 'lifecycle_guest.S'])]:
    binary = OUT / name
    subprocess.run(base + [str(HERE / p) for p in sources] + ([str(native)] if name == 'lifecycle-guest' else []) + ['-o', str(binary)], check=True)
    subprocess.run([str(binary)], check=True, timeout=30)

# Real transactional save code, killed by the parent at exact commit boundaries.
import shutil
import signal
ENGINE = Path(os.environ.get('SATCHEL_DEPS_ROOT', Path.home() / 'SatchelDeps')) / 'NX2Apple/engine'
HOST = ENGINE / 'dm/host'
assert subprocess.check_output(['git', '-C', ENGINE, 'rev-parse', 'HEAD'], text=True).strip() == 'f8a5618d5f505a4c0a0bcac7c413872d3a53c009'
source = runpy.run_path(str(HERE / 'overlay.py'))['prepare'](HOST, OUT / 'overlay')
(source / 'tests').mkdir(exist_ok=True)
shutil.copyfile(HOST / 'tests/t_fs_commit_list.c', source / 'tests/t_fs_commit_list.c')
fsbase = base + ['-Wno-unused-function', '-Wno-unused-variable', '-I' + str(source), '-I' + str(HOST), '-I' + str(HOST / 'tests/memory_profile')]
binary = OUT / 'lifecycle-save'
subprocess.run(fsbase + [str(HERE / 'lifecycle_save_probe.c'), str(HOST / 'tests/t_fs_stub.c'),
    str(HOST / 'hle_bigstack.c'), str(HOST / 'save_sync.c'), str(source / 'hle_profile.c'),
    str(HERE / 'SessionBoundary.c'), str(HERE / 'SessionNativeWorkers.c'), '-o', str(binary)], check=True)
for stage in range(4):
    fixture = OUT / ('kill-' + str(stage))
    # Every invocation gets new task-owned synthetic data, with no real saves.
    import tempfile
    fixture = Path(tempfile.mkdtemp(prefix='kill-' + str(stage) + '-', dir=OUT))
    save = fixture / 'save'; save.mkdir()
    for name in ['a', 'b']: (save / name).write_text('old')
    child = subprocess.Popen([str(binary), 'write', str(save), str(stage)])
    import time
    deadline = time.monotonic() + 10
    try:
        while True:
            pid, status = os.waitpid(child.pid, os.WUNTRACED | os.WNOHANG)
            if pid:
                assert os.WIFSTOPPED(status), status
                break
            assert time.monotonic() < deadline, 'child never reached kill boundary'
            time.sleep(.01)
        os.kill(child.pid, signal.SIGKILL)
        assert child.wait() == -signal.SIGKILL
    finally:
        if child.poll() is None: child.kill(); child.wait()
    expected = 'old' if stage < 2 else 'new'
    subprocess.run([str(binary), 'verify', str(save), expected], check=True, timeout=10)
    print('PASS: SIGKILL stage=' + str(stage) + ' recovered=' + expected, flush=True)
# Compile the actual launch policy separately from SwiftUI. Cold UIKit launch
# remains a device check, but a dead process cannot restore this session owner.
text = (ROOT / 'NativeHost/PreparedGame.swift').read_text()
first = text.index('enum PreparedLaunch {')
last = text.index('\n}', first) + 2
swift = OUT / 'launch-policy.swift'
swift.write_text('import Foundation\nimport Darwin\n' + text[first:last] + '''
precondition(PreparedLaunch.current == nil)
UserDefaults.standard.setVolatileDomain(["SatchelAutoLaunchPreparedGame": true, "SatchelAutoPrepare": true], forName: UserDefaults.registrationDomain)
precondition(UserDefaults.standard.bool(forKey: "SatchelAutoLaunchPreparedGame"))
precondition(!PreparedLaunch.launchFlag("SatchelAutoLaunchPreparedGame", arguments: ["Satchel"]))
precondition(!PreparedLaunch.launchFlag("SatchelAutoPrepare", arguments: ["Satchel"]))
precondition(PreparedLaunch.launchFlag("SatchelAutoLaunchPreparedGame", arguments: ["Satchel", "-SatchelAutoLaunchPreparedGame", "YES"]))
precondition(!PreparedLaunch.launchFlag("SatchelAutoLaunchPreparedGame", arguments: ["Satchel", "-SatchelAutoLaunchPreparedGame", "NO"]))
if ProcessInfo.processInfo.arguments.contains("kill") { PreparedLaunch.current = "synthetic-session"; raise(SIGSTOP); exit(2) }
print("PASS: fresh process has no selected session and ignores persisted auto-launch preferences")
''')
binary = OUT / 'launch-policy'
subprocess.run(['nice', '-n', '10', 'xcrun', 'swiftc', '-j', '2', str(swift), '-o', str(binary)], check=True)
child = subprocess.Popen([str(binary), 'kill'])
deadline = time.monotonic() + 10
try:
    while True:
        pid, status = os.waitpid(child.pid, os.WUNTRACED | os.WNOHANG)
        if pid:
            assert os.WIFSTOPPED(status), status
            break
        assert time.monotonic() < deadline
        time.sleep(.01)
    os.kill(child.pid, signal.SIGKILL); assert child.wait() == -signal.SIGKILL
finally:
    if child.poll() is None: child.kill(); child.wait()
subprocess.run([str(binary)], check=True, timeout=10)
