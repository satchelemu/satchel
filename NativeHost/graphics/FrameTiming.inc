// Opt-in CPU/GPU frame pairing. No payloads, extra GPU waits or per-frame log writes.
#include "FrameTiming.h"
static _Atomic int satchel_timing_enabled = -1;
static pthread_mutex_t satchel_timing_mutex = PTHREAD_MUTEX_INITIALIZER;
static SatchelTimingFrame satchel_timing_frames[SATCHEL_TIMING_RING];
static SatchelTimingWindow satchel_timing_window;
static struct { uint64_t frame; double cpu, wall; int used; } satchel_timing_cpu[16];

static void satchel_timing_reset(void) {
    // mtl_init runs after the previous session's GPU callbacks have drained.
    pthread_mutex_lock(&satchel_timing_mutex);
    memset(satchel_timing_frames, 0, sizeof satchel_timing_frames);
    memset(&satchel_timing_window, 0, sizeof satchel_timing_window);
    memset(satchel_timing_cpu, 0, sizeof satchel_timing_cpu);
    atomic_store(&satchel_timing_enabled, -1);
    pthread_mutex_unlock(&satchel_timing_mutex);
}

static int satchel_timing_on(void) {
    int value = atomic_load_explicit(&satchel_timing_enabled, memory_order_relaxed);
    if (value < 0) {
        const char *e = getenv("DM_SATCHEL_FRAME_TIMING"); value = e && !strcmp(e, "1");
        atomic_store_explicit(&satchel_timing_enabled, value, memory_order_relaxed);
    }
    return value;
}
static void satchel_timing_begin(uint64_t frame) {
    if (!satchel_timing_on()) return;
    pthread_mutex_lock(&satchel_timing_mutex);
    SatchelTimingBegin(satchel_timing_frames, frame);
    pthread_mutex_unlock(&satchel_timing_mutex);
}
static void satchel_timing_done(uint64_t frame, double start, double end, int error, int present) {
    if (!satchel_timing_on()) return;
    pthread_mutex_lock(&satchel_timing_mutex);
    SatchelTimingDone(satchel_timing_frames, frame, start, end, error, present);
    pthread_mutex_unlock(&satchel_timing_mutex);
}
void satchel_timing_cpu_frame(uint64_t frame, double cpu, double wall) {
    if (!satchel_timing_on()) return;
    pthread_mutex_lock(&satchel_timing_mutex);
    unsigned k = frame % 16;
    satchel_timing_cpu[k].frame = frame; satchel_timing_cpu[k].cpu = cpu;
    satchel_timing_cpu[k].wall = wall; satchel_timing_cpu[k].used = 1;
    if (frame >= 8) {
        uint64_t f = frame - 8; k = f % 16;
        if (satchel_timing_cpu[k].used && satchel_timing_cpu[k].frame == f)
            SatchelTimingCollect(&satchel_timing_window, &satchel_timing_frames[f % SATCHEL_TIMING_RING],
                                 f, satchel_timing_cpu[k].cpu, satchel_timing_cpu[k].wall);
    }
    pthread_mutex_unlock(&satchel_timing_mutex);
}
void satchel_timing_summary(uint64_t frame, double budget) {
    if (!satchel_timing_on()) return;
    pthread_mutex_lock(&satchel_timing_mutex);
    SatchelTimingWindow w = satchel_timing_window;
    memset(&satchel_timing_window, 0, sizeof satchel_timing_window);
    pthread_mutex_unlock(&satchel_timing_mutex);
    double gpu = SatchelTimingPercentile(w.gpu, w.count, 50), cpu = SatchelTimingPercentile(w.cpu, w.count, 50);
    double wall = SatchelTimingPercentile(w.wall, w.count, 50);
    unsigned rejected = w.missing + w.pending + w.invalid + w.no_present;
    MLOG("[frame-gpu] up to frame %llu: samples %u missing %u pending %u invalid %u no-present %u; GPU submit ms/frame median %.3f p95 %.3f; CPU present-thread ms/frame median %.3f p95 %.3f; frame wall median %.3f p95 %.3f; present GPU median %.3f; budget %.3f; limit %s (inferred)",
         (unsigned long long)frame, w.count, w.missing, w.pending, w.invalid, w.no_present,
         gpu, SatchelTimingPercentile(w.gpu, w.count, 95), cpu, SatchelTimingPercentile(w.cpu, w.count, 95),
         wall, SatchelTimingPercentile(w.wall, w.count, 95), SatchelTimingPercentile(w.present, w.count, 50), budget,
         SatchelTimingLimit(gpu, cpu, wall, budget, w.count, rejected));
}
