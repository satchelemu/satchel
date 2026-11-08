#!/usr/bin/env python3
"""Exercise the actual crash gate with its alarm delivered to a non-owner."""
import os
from pathlib import Path
import runpy
import subprocess
DEPENDENCY_PATHS = runpy.run_path(str(Path(__file__).resolve().parents[2] / 'scripts/dependency_paths.py'))

HERE = Path(__file__).resolve().parent
OUT = Path(os.environ.get('SESSION_CHECK_OUT', Path.home() / 'SatchelBuild/session')) / 'soak-crash-check'
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'NXNative-LICENSE.txt').write_bytes((DEPENDENCY_PATHS['deps_root']() / 'NX2Apple/engine/LICENSE').read_bytes())
runpy.run_path(str(HERE.parents[1] / 'NativeApp/build_support.py'))['wait']()
source = (DEPENDENCY_PATHS['deps_root']() / 'NX2Apple/engine/dm/host/crash.c').read_text()
fixed = runpy.run_path(str(HERE / 'overlay.py'))['bound_crash_report'](source)
prefix = '''#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
static _Atomic uintptr_t g_crash_owner;
'''
fixture = '''
static _Atomic int ready;
static void *owner(void *unused) {
    (void)unused;
    sigset_t blocked; sigemptyset(&blocked); sigaddset(&blocked, SIGALRM);
    pthread_sigmask(SIG_BLOCK, &blocked, NULL);
    crash_gate(SIGBUS);
    atomic_store(&ready, 1);
    for (;;) pause();
    return NULL;
}
static void timeout_handler(int sig) { crash_gate(sig); }
int main(void) {
    struct sigaction action = {.sa_handler = timeout_handler};
    sigaction(SIGALRM, &action, NULL);
    pthread_t thread; if (pthread_create(&thread, NULL, owner, NULL)) return 64;
    while (!atomic_load(&ready)) usleep(1000);
    // Deliver the alarm to this non-owner deterministically.
    raise(SIGALRM);
    return 65;
}
'''
for label, text in [('before', source), ('after', fixed)]:
    first = text.index('static void crash_gate(int sig) {')
    last = text.index('\nstatic void on_crash', first)
    path = OUT / (label + '.c')
    path.write_text(prefix + text[first:last] + fixture)
    binary = OUT / label
    subprocess.run(['nice', '-n', '10', 'xcrun', 'clang', '-O1', '-Wall', '-Wextra', '-Werror',
                    str(path), '-o', str(binary)], check=True)
    try:
        result = subprocess.run([str(binary)], timeout=2, env={k: v for k, v in os.environ.items()
                                                            if not k.startswith('DM_')})
        assert label == 'after' and result.returncode == 128 + 14, (label, result.returncode)
        print('PASS: corrected gate exits 142 when a non-owner receives SIGALRM', flush=True)
    except subprocess.TimeoutExpired:
        assert label == 'before', 'Corrected crash gate still hung'
        print('PASS: original gate reproduces the non-owner timeout hang', flush=True)
