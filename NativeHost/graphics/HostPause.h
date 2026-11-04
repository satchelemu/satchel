// Optional timing of host reads, memory operations and guest GC suspension.
// Only operation numbers, counts and times are logged, never guest payloads.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include <stdatomic.h>
#include <time.h>
#include <pthread.h>

typedef struct { _Atomic uint64_t calls, ns, cpu, max_ns, next; } satchel_pause_stat;
typedef struct { satchel_pause_stat *stat; uint64_t start, cpu; const char *name; int code; uint64_t bytes; } satchel_pause_scope;
static int satchel_pause_enabled, satchel_pause_log;
static pthread_once_t satchel_pause_once = PTHREAD_ONCE_INIT;
// Bounded, opt-in trace. Records contain timing and IDs only. A release-store
// publishes each completed record; readers ignore unpublished slots. No trace
// mutex or file syscall occurs in the measured scope. Never overwrite a trial.
#ifndef SATCHEL_PAUSE_UNIT
#define SATCHEL_PAUSE_UNIT "test"
#endif
#ifndef SATCHEL_PAUSE_EPOCH
#define SATCHEL_PAUSE_EPOCH 0
#endif
#ifndef SATCHEL_PAUSE_CAP
#define SATCHEL_PAUSE_CAP 524288u
#endif
typedef struct {
    uint64_t start, cpu, thread, code, bytes;
    _Atomic uint64_t end;
} satchel_pause_event;
typedef struct {
    uint64_t magic, epoch;
    _Atomic uint64_t count;
    uint64_t capacity, reserved[4];
    satchel_pause_event events[SATCHEL_PAUSE_CAP];
} satchel_pause_trace;
_Static_assert(sizeof(satchel_pause_event) == 48, "pause trace record ABI");
static satchel_pause_trace *satchel_trace;
static void satchel_pause_init(void) {
    const char *e = getenv("DM_HOST_PAUSE"), *path = getenv("DM_HOST_TRACE");
    satchel_pause_log = e && atoi(e) > 0;
    satchel_pause_enabled = satchel_pause_log || (path && *path);
    if (!path || !*path) return;
    char file[1024];
    int n = snprintf(file, sizeof file, "%s.%s.bin", path, SATCHEL_PAUSE_UNIT);
    if (n < 0 || n >= (int)sizeof file) return;
    int fd = open(file, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0) { LOG("[hostpause] trace create failed errno=%d", errno); return; }
    void *map = MAP_FAILED;
    if (!ftruncate(fd, sizeof(satchel_pause_trace))) map = mmap(NULL, sizeof(satchel_pause_trace), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (map == MAP_FAILED) { LOG("[hostpause] trace mapping failed errno=%d", errno); return; }
    satchel_trace = map;
    satchel_trace->magic = 0x31524354534c50ull; satchel_trace->epoch = SATCHEL_PAUSE_EPOCH;
    satchel_trace->capacity = SATCHEL_PAUSE_CAP;
}
static uint64_t satchel_pause_cpu(void) { struct timespec t; clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t); return (uint64_t)t.tv_sec * 1000000000ull + t.tv_nsec; }
static satchel_pause_scope satchel_pause_begin(satchel_pause_stat *s, const char *name, int code) {
    int saved_errno = errno;
    pthread_once(&satchel_pause_once, satchel_pause_init);
    satchel_pause_scope scope = satchel_pause_enabled ? (satchel_pause_scope){s, now_ns(), satchel_pause_cpu(), name, code, 0} : (satchel_pause_scope){0};
    errno = saved_errno; return scope;
}
static void satchel_pause_end(satchel_pause_scope *p) {
    if (!p->stat) return;
    int saved_errno = errno;
    uint64_t cpu = satchel_pause_cpu() - p->cpu, now = now_ns(), ns = now - p->start;
    satchel_pause_stat *s = p->stat;
    atomic_fetch_add(&s->calls, 1); atomic_fetch_add(&s->ns, ns); atomic_fetch_add(&s->cpu, cpu);
    uint64_t peak = atomic_load(&s->max_ns);
    while (ns > peak && !atomic_compare_exchange_weak(&s->max_ns, &peak, ns)) {}
    // Cumulative counters avoid losing calls while another thread reports.
    uint64_t next = atomic_load(&s->next);
    if (satchel_pause_log && now >= next && atomic_compare_exchange_strong(&s->next, &next, now + 1000000000ull)) {
        LOG("[hostpause] %s/%#x cumulative calls %llu wall %.3f ms CPU %.3f ms max %.3f ms", p->name, p->code,
            (unsigned long long)atomic_load(&s->calls), atomic_load(&s->ns) / 1e6, atomic_load(&s->cpu) / 1e6, atomic_load(&s->max_ns) / 1e6);
    }
    if (satchel_pause_log && ns >= 10000000ull) LOG("[hostpause] slow %s/%#x wall %.3f ms CPU %.3f ms", p->name, p->code, ns / 1e6, cpu / 1e6);
    int record = 1;
#ifdef SATCHEL_PAUSE_WAIT_FILTER
    // Fast successful polls are counted above. Keep long blocking waits in the
    // bounded event stream so a loading interval does not exhaust it at boot.
    if ((p->code == 0x18 || p->code == 0x1c || p->code == 0x1d || p->code == 0x34 || p->code == 0x35) && ns < 1000000ull) record = 0;
#endif
    if (satchel_trace && record) {
        uint64_t i = atomic_fetch_add_explicit(&satchel_trace->count, 1, memory_order_relaxed);
        if (i < SATCHEL_PAUSE_CAP) {
            static _Thread_local uint64_t tid;
            if (!tid) pthread_threadid_np(NULL, &tid);
            satchel_pause_event *v = &satchel_trace->events[i];
            v->start = p->start; v->cpu = cpu; v->thread = tid;
            v->code = (uint64_t)p->code; v->bytes = p->bytes;
            atomic_store_explicit(&v->end, now, memory_order_release);
        }
    }
    errno = saved_errno;
}
#define SATCHEL_PAUSE_SCOPE(stat, name, code) satchel_pause_scope satchel_scope __attribute__((cleanup(satchel_pause_end))) = satchel_pause_begin(stat, name, code)
