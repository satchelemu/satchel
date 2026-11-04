#!/usr/bin/env python3
"""Verify private compiler queue cancellation and active child completion."""
import os
from pathlib import Path
import signal
import subprocess
import sys
import tempfile
import time

HERE = Path(__file__).resolve().parent
OUT = Path.home() / 'SatchelBuild/session/cache-stop-check'
OUT.mkdir(parents=True, exist_ok=True)
worker = '''import pathlib, runpy, signal, sys, time
module = runpy.run_path(sys.argv[1])
signal.signal(signal.SIGTERM, module['cancel'])
root = pathlib.Path(sys.argv[2])
if sys.argv[3] == 'queue':
    (root / 'ready').touch()
    time.sleep(60)
else:
    code = "import os,pathlib,sys,time; p=pathlib.Path(sys.argv[1]); (p/'pid').write_text(str(os.getpid())); (p/'ready').touch(); time.sleep(0.5); (p/'finished').touch()"
    module['compile_command']([sys.executable, '-c', code, str(root)])
(root / 'unexpected-success').touch()
'''
for mode in ['queue', 'active']:
    with tempfile.TemporaryDirectory(dir=OUT) as temporary:
        root = Path(temporary)
        process = subprocess.Popen([sys.executable, '-B', '-c', worker,
                                    str(HERE / 'mac_shader_cache.py'), str(root), mode])
        deadline = time.monotonic() + 5
        while not (root / 'ready').exists():
            assert process.poll() is None and time.monotonic() < deadline
            time.sleep(.01)
        start = time.monotonic()
        process.send_signal(signal.SIGTERM)
        assert process.wait(timeout=3) == 75
        elapsed = time.monotonic() - start
        assert not (root / 'unexpected-success').exists()
        if mode == 'queue':
            assert elapsed < .5
        else:
            assert (root / 'finished').exists()
            assert elapsed >= .35
            child = int((root / 'pid').read_text())
            try:
                os.kill(child, 0)
            except ProcessLookupError:
                pass
            else:
                raise AssertionError('Compiler child outlived helper')
        print(f'PASS: {mode} cancellation exit=75 elapsed={elapsed:.3f}s', flush=True)

# Foundation's terminate method signals the process group, killing an active
# compiler too. Exercise the actual NSTask launch with helper-only signalling.
with tempfile.TemporaryDirectory(dir=OUT) as temporary:
    root = Path(temporary)
    source, driver = root / 'driver.m', root / 'driver'
    source.write_text(r'''#import <Foundation/Foundation.h>
#include <signal.h>
#include <unistd.h>
int main(int argc, char **argv) { @autoreleasepool {
    NSTask *task = [NSTask new];
    task.executableURL = [NSURL fileURLWithPath:@(argv[1])];
    task.arguments = @[@"-B", @"-c", @(argv[2]), @(argv[3]), @(argv[4]), @"active"];
    if (![task launchAndReturnError:nil]) return 90;
    NSString *ready = [@(argv[4]) stringByAppendingPathComponent:@"ready"];
    for (int i = 0; i < 500 && access(ready.fileSystemRepresentation, F_OK); i++) usleep(10000);
    kill(task.processIdentifier, SIGTERM);
    [task waitUntilExit];
    return task.terminationStatus;
} }
''')
    subprocess.run(['nice', '-n', '10', 'xcrun', 'clang', '-fobjc-arc', '-framework',
                    'Foundation', str(source), '-o', str(driver)], check=True)
    result = subprocess.run([str(driver), sys.executable, worker,
                             str(HERE / 'mac_shader_cache.py'), str(root)], timeout=10)
    assert result.returncode == 75
    assert (root / 'finished').exists()
    assert not (root / 'unexpected-success').exists()
    try:
        os.kill(int((root / 'pid').read_text()), 0)
    except ProcessLookupError:
        pass
    else:
        raise AssertionError('NSTask compiler child outlived helper')
    print('PASS: NSTask helper exits 75 after compiler child finishes and is reaped', flush=True)
