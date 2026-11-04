#!/usr/bin/env python3
"""Check optional pause accounting across returns and concurrent host calls."""
import argparse
import os
import struct
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--out', type=Path, required=True)
a = p.parse_args()
a.out.mkdir(parents=True, exist_ok=True)
fixture = a.out / 'pause-check.c'
fixture.write_text(r'''
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <time.h>
static uint64_t now_ns(void) { return clock_gettime_nsec_np(CLOCK_UPTIME_RAW); }
#define LOG(...) ((void)0)
#include "HostPause.h"
static satchel_pause_stat stats;
static int operation(int error) {
    errno = EDOM;
    SATCHEL_PAUSE_SCOPE(&stats, "fixture", 0);
    assert(errno == EDOM);
    if (error) return -1;
    return 0;
}
static void *worker(void *arg) {
    for (int i = 0; i < 1000; i++) { assert(operation(i & 1) == ((i & 1) ? -1 : 0)); assert(errno == EDOM); }
    return 0;
}
int main(void) {
    assert(operation(0) == 0);
    assert(operation(1) == -1);
    pthread_t threads[8];
    for (int i = 0; i < 8; i++) assert(!pthread_create(&threads[i], 0, worker, 0));
    for (int i = 0; i < 8; i++) assert(!pthread_join(threads[i], 0));
    if (atoi(getenv("DM_HOST_PAUSE"))) {
        assert(atomic_load(&stats.calls) == 8002);
        assert(atomic_load(&stats.max_ns) > 0);
        assert(atomic_load(&stats.ns) >= atomic_load(&stats.max_ns));
        assert(atomic_load(&stats.cpu) > 0);
    } else {
        assert(!atomic_load(&stats.calls));
        assert(!atomic_load(&stats.ns));
        assert(!atomic_load(&stats.cpu));
    }
    puts("pause accounting passed: 8002 calls, success/error returns, eight workers");
}
''')
exe = a.out / 'pause-check'
subprocess.run(['xcrun', 'clang', '-std=gnu11', '-O2', '-Wall', '-Wextra',
                '-Wno-unused-parameter', '-I', str(Path(__file__).parent),
                str(fixture), '-o', str(exe)], check=True)
for enabled in ['0', '1']:
    subprocess.run([str(exe)], env=dict(os.environ, DM_HOST_PAUSE=enabled, DM_HOST_TRACE=''), check=True)

trace = a.out / 'accounting-trace'
subprocess.run([str(exe)], env=dict(os.environ, DM_HOST_PAUSE='1', DM_HOST_TRACE=str(trace)), check=True)
data = trace.with_suffix('.test.bin').read_bytes()
magic, epoch, count, capacity = struct.unpack_from('<4Q', data)
assert magic == 0x31524354534c50 and count == 8002 and capacity >= count
rows = [struct.unpack_from('<6Q', data, 64 + i * 48) for i in range(count)]
assert all(end >= start > 0 and thread > 0 and code == 0 and size == 0 for start, cpu, thread, code, size, end in rows)
assert len({r[2] for r in rows}) == 9
# Existing traces are preserved, and failed initialization must preserve errno.
subprocess.run([str(exe)], env=dict(os.environ, DM_HOST_PAUSE='1', DM_HOST_TRACE=str(trace)), check=True)
assert trace.with_suffix('.test.bin').read_bytes() == data
print('trace accounting passed: 8002 published records, nine thread IDs, exclusive creation, errno preserved')
small = a.out / 'pause-overflow'
subprocess.run(['xcrun', 'clang', '-std=gnu11', '-O2', '-DSATCHEL_PAUSE_CAP=64',
                '-I', str(Path(__file__).parent), str(fixture), '-o', str(small)], check=True)
prefix = a.out / 'overflow-trace'
subprocess.run([str(small)], env=dict(os.environ, DM_HOST_PAUSE='1', DM_HOST_TRACE=str(prefix)), check=True)
data = prefix.with_suffix('.test.bin').read_bytes()
magic, epoch, count, capacity = struct.unpack_from('<4Q', data)
assert count == 8002 and capacity == 64 and len(data) == 64 + 64 * 48
assert all(struct.unpack_from('<Q', data, 64 + i * 48 + 40)[0] for i in range(64))
print('trace overflow passed: 8002 events counted, 64 committed, no buffer overrun')
