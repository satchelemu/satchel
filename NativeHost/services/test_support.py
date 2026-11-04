"""Test the exact generated kernel-event helpers and pinned DLC scan with synthetic inputs."""
from pathlib import Path
import re
import runpy


def prepare(engine, out):
    overlay = runpy.run_path(str(Path(__file__).with_name('overlay.py')))['prepare_support']
    host = engine / 'dm/host'
    thread = overlay(host / 'hle_thread.c', out).read_text()
    misc = overlay(host / 'hle_misc.c', out).read_text()
    # Lift the real host definitions and helpers, excluding unrelated thread/SDK hooks.
    event = re.search(r'typedef struct event \{[^\n]+event_t;', thread)[0]
    table = thread[thread.index('#define MAX_H'):thread.index('static pthread_key_t g_keythr;')]
    peek = thread[thread.index('static void *h_peek'):thread.index('static void *h_get')]
    peek = re.sub(r'    if \(h == H_CUR_THREAD[^\n]+\n', '', peek)
    scan = misc[misc.index('static int g_aoc['):misc.index('// On the big host stack:')]
    aoc = misc[misc.index('unsigned satchel_host_aoc_list('):]
    events = thread[thread.index('uint32_t satchel_host_event_create('):]
    source = '''#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>
#define LOG(...) do { if (0) printf(__VA_ARGS__); } while (0)
enum { O_FREE, O_EVENT_R };
static pthread_mutex_t G = PTHREAD_MUTEX_INITIALIZER;
static struct { uint64_t program_id; } profile = {0x0100123400000000ull};
#define dm_prof() (&profile)
''' + event + '\n' + table + peek + '\nstatic void wake_sync_waiters(const void *e) { (void)e; }\n' + scan + aoc + events + '''
int satchel_test_event_state(uint32_t h) { event_t *e = h_peek(h, O_EVENT_R); return e ? e->signaled : -1; }
#ifndef SATCHEL_SUPPORT_NO_MAIN
int main(int argc, char **argv) {
    assert(argc == 2); (void)mkdir(argv[1], 0700); assert(!chdir(argv[1]));
    const char *files[] = {"1.img", "7.img", "2000.img", "2001.img", "nonsense"};
    for (unsigned i = 0; i < 5; i++) { FILE *f = fopen(files[i], "w"); assert(f); fclose(f); }
    (void)mkdir("7", 0700); // Old directory and new image count once.
    unsetenv("DM_AOC_OWNED"); setenv("DM_AOC_DIR", argv[1], 1);
    assert(satchel_host_aoc_list(NULL, 0, 0) == 3);
    uint32_t out[4] = {0, 0, 0, 0xdeadbeef};
    assert(satchel_host_aoc_list(out, 0, 4) == 3 && out[0] == 1 && out[1] == 7 && out[2] == 2000 && out[3] == 0xdeadbeef);
    assert(satchel_host_aoc_list(out, UINT32_MAX, UINT32_MAX) == 0);
    assert(satchel_host_program_id() == profile.program_id);
    g_naoc = -1; setenv("DM_AOC_OWNED", "0", 1); assert(!satchel_host_aoc_list(NULL, 0, 0));
    g_naoc = -1; unsetenv("DM_AOC_OWNED"); unsetenv("DM_AOC_DIR"); assert(!satchel_host_aoc_list(NULL, 0, 0));
    for (int signaled = 0; signaled < 2; signaled++) {
        uint32_t owner = satchel_host_event_create(signaled), copy = satchel_host_event_copy(owner);
        assert(owner && copy && owner != copy);
        event_t *e = h_peek(copy, O_EVENT_R); assert(e == h_peek(owner, O_EVENT_R) && e->refs == 2 && e->signaled == signaled);
        satchel_host_event_set(owner, !signaled); assert(e->signaled == !signaled);
        satchel_host_event_set(copy, signaled); assert(e->signaled == signaled);
        satchel_host_event_close(owner); assert(e->refs == 1 && e == h_peek(copy, O_EVENT_R));
        e->signaled = 0; assert(!((event_t *)h_peek(copy, O_EVENT_R))->signaled);
        satchel_host_event_close(copy); assert(!h_peek(copy, O_EVENT_R));
    }
    uint32_t owner = satchel_host_event_create(0), handles[MAX_H]; unsigned count = 0;
    while ((handles[count] = satchel_host_event_copy(owner))) count++;
    assert(count == MAX_H - 2 && !satchel_host_event_create(0));
    for (unsigned i = 0; i < count; i++) satchel_host_event_close(handles[i]);
    satchel_host_event_close(owner);
    for (unsigned i = 1; i < MAX_H; i++) assert(!g_h[i].type);
    puts("PASS: generated engine event helpers, copy lifetime, reset state, kernel-table exhaustion; pinned DLC scan, deduplication, disabled/empty policy");
}
#endif
'''
    target = out / 'test-support.c'; target.write_text(source)
    return target
