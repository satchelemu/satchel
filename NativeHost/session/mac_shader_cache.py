#!/usr/bin/env python3
"""Private Mac probe cache, using unchanged pinned shader variant generators."""
from pathlib import Path
import contextlib
import fcntl
import importlib
import io
import json
import os
import re
import runpy
import signal
import subprocess
import sys
import tempfile
import time

HERE = Path(__file__).resolve().parent
BUILD = Path.home() / 'SatchelBuild'
cancelled = False
compiling = False


def cancel(signum, frame):
    global cancelled
    cancelled = True
    if not compiling:
        raise SystemExit(75)


def compile_command(command):
    global compiling
    if cancelled:
        raise SystemExit(75)
    compiling = True
    started = time.monotonic()
    try:
        result = subprocess.run(command, capture_output=True)
    finally:
        compiling = False
        print('[mac-cache-wait] phase=compiler milliseconds=' +
              str(round((time.monotonic() - started) * 1000)), flush=True)
    if result.returncode:
        raise RuntimeError('Metal compiler returned status ' + str(result.returncode))
    if cancelled:
        raise SystemExit(75)


def prepare(source):
    text = '#include <signal.h>\n' + source.read_text()
    anchor = 'static __thread int t_lib_layer;'
    assert text.count(anchor) == 1
    text = text.replace(anchor, anchor + '\nstatic __thread int satchel_mac_cache_cancelled;', 1)
    anchor = 'lib_from_file(NSString *path, NSError **err) {'
    assert text.count(anchor) == 1
    text = text.replace(anchor, 'lib_from_file(NSString *inputPath, NSError **err) {\n    satchel_mac_cache_cancelled = atomic_load(&satchel_present_stop);\n    if (satchel_mac_cache_cancelled) return nil;\n    NSString *__strong path = inputPath;', 1)
    anchor = '    int fd = open(path.fileSystemRepresentation, O_RDONLY); struct stat st;'
    assert text.count(anchor) == 1
    text = text.replace(anchor, '''    const char *cache = getenv("SATCHEL_MAC_CACHE_ROOT");
    if (cache && access(path.fileSystemRepresentation, R_OK)) {
        NSString *title = [@(getenv("DM_ROOT")) lastPathComponent];
        NSString *folder = [[@(cache) stringByAppendingPathComponent:title]
            stringByAppendingPathComponent:inputPath.stringByDeletingLastPathComponent.lastPathComponent];
        NSString *library = [folder stringByAppendingPathComponent:path.lastPathComponent];
        if (access(library.fileSystemRepresentation, R_OK)) {
            NSTask *task = [NSTask new];
            task.executableURL = [NSURL fileURLWithPath:@(getenv("SATCHEL_MAC_PYTHON"))];
            task.arguments = @[@"-B", @(getenv("SATCHEL_MAC_COMPILER")), path, library];
            task.standardError = NSFileHandle.fileHandleWithNullDevice;
            if (![task launchAndReturnError:nil]) MDIE("Mac shader compiler launch failed");
            BOOL cancelSent = NO;
            while ([task isRunning]) {
                if (atomic_load(&satchel_present_stop) && !cancelSent) {
                    // NSTask terminate signals the process group, including the
                    // compiler child. Only the helper owns cancellation.
                    kill(task.processIdentifier, SIGTERM); cancelSent = YES;
                }
                usleep(10000);
            }
            [task waitUntilExit];
            if (cancelSent && (task.terminationStatus == 75 || task.terminationStatus == SIGTERM)) {
                satchel_mac_cache_cancelled = 1;
                MLOG("[mac-cache] cancelled library");
                return nil;
            }
            if (task.terminationStatus) MDIE("Mac shader compiler failed status=%d", task.terminationStatus);
            MLOG("[mac-cache] compiled library");
        }
        path = library;
    }
''' + anchor, 1)
    anchor = '    id<MTLLibrary> lib = lib_from_file(lp, &e);'
    assert text.count(anchor) == 1
    text = text.replace(anchor, anchor + '\n    if (!lib && atomic_load(&satchel_present_stop)) return nil;', 1)
    # A cancelled private load is not an unmapped shader. Propagate it through
    # the two demand callers without suppressing failures during gameplay.
    for anchor in ['if (!vs) { RED_ONCE(13, "program has no vertex shader',
                   'if (!cs) { RED_ONCE(28, "the dispatched program has no compute shader']:
        assert text.count(anchor) == 1
        result = 'nil' if anchor.startswith('if (!vs)') else ''
        text = text.replace(anchor, anchor.replace('{ RED_ONCE',
            '{ if (satchel_mac_cache_cancelled && atomic_load(&satchel_present_stop)) return ' + result + '; RED_ONCE'), 1)
    source.write_text(text)
    return source


def compile_library(requested, library):
    allowed = [BUILD / p for p in ['zelda/work/shader/cache', 'games/odyssey/shader/cache', 'castlevania/runtime-shaders/cache']]
    for path in map(Path, json.loads(os.environ.get('SATCHEL_MAC_SOURCE_CACHES', '[]'))):
        if path.resolve().is_relative_to((BUILD / 'session').resolve()):
            allowed.append(path)
    assert any(requested.resolve().is_relative_to(p.resolve()) for p in allowed)
    assert library.resolve().is_relative_to(Path(os.environ.get('SATCHEL_MAC_CACHE_ROOT', BUILD / 'session/soak-mac/libraries')).resolve())
    match = re.fullmatch(r'([0-9a-f]{16})_(b|bp|bv|ba|bl|bf)\.metallib', requested.name)
    assert match
    key, kind = match.groups()
    library.parent.mkdir(parents=True, exist_ok=True)
    # One compiler at a time, below the two-job limit. The outer process guard
    # owns and limits every compiler descendant, including this helper.
    with (library.parent / '.compile.lock').open('a') as lock:
        started = time.monotonic()
        try:
            fcntl.flock(lock, fcntl.LOCK_EX)
        finally:
            print('[mac-cache-wait] phase=lock milliseconds=' +
                  str(round((time.monotonic() - started) * 1000)), flush=True)
        if library.is_file():
            return
        started = time.monotonic()
        try:
            runpy.run_path(str(HERE.parents[1] / 'NativeApp/build_support.py'))['wait']()
        finally:
            print('[mac-cache-wait] phase=heavy milliseconds=' +
                  str(round((time.monotonic() - started) * 1000)), flush=True)
        with tempfile.TemporaryDirectory(prefix='.compile-', dir=library.parent) as temporary:
            temporary = Path(temporary)
            source = requested.with_suffix('.metal')
            if not source.is_file():
                sys.path.insert(0, str(Path.home() / 'SatchelDeps/NX2Apple/engine/dm/gfx/tools'))
                base = requested.with_name(key + '_b.metal').read_text()
                reflection = json.loads(requested.with_name(key + '_b.json').read_text())
                modules = {'bp': ('pull', 'dmsh_pull'), 'bv': ('vin', 'dmsh_vin'),
                           'ba': ('alpha', 'dmsh_alpha'), 'bl': ('logic', 'dmsh_logic'),
                           'bf': ('fetch', 'dmsh_fetch')}
                assert kind in modules
                field, module = modules[kind]
                variant = importlib.import_module(module).variant
                with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                    text, info = variant(base, reflection['bindings'], om_only_ok=True) if kind == 'bf' else variant(base)
                assert text
                if kind == 'bp': info = {k: v for k, v in info.items() if v is not None}
                if kind == 'bv': info = {'entry': 'main0', 'entry_z': 'main0_z', 'shadow': info}
                if kind in {'ba', 'bl'}: info = {'entries': info}
                assert json.dumps(info, sort_keys=True) == json.dumps(reflection[field], sort_keys=True)
                source = temporary / (key + '_' + kind + '.metal')
                source.write_text(text)
            air, partial = temporary / 'library.air', temporary / 'library.metallib'
            for command in [['xcrun', '-sdk', 'macosx', 'metal', '-c', '-O1', '-fpreserve-invariance', str(source), '-o', str(air)],
                            ['xcrun', '-sdk', 'macosx', 'metallib', str(air), '-o', str(partial)]]:
                compile_command(['nice', '-n', '10', *command])
            os.replace(partial, library)


if __name__ == '__main__':
    # Queue/lock waits cancel immediately. An active compiler finishes before
    # cancellation is acknowledged, so no compiler child outlives the helper.
    signal.signal(signal.SIGTERM, cancel)
    try:
        compile_library(*map(Path, sys.argv[1:]))
    except Exception as error:
        # Compiler diagnostics can contain translated shader source. Never emit it.
        print('Mac cache preparation failed: ' + (str(error) if isinstance(error, RuntimeError) else type(error).__name__))
        raise SystemExit(1)
