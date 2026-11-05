// Bounded diagnostic accounting. The caller serializes access to the frame ring.
#ifndef SATCHEL_FRAME_TIMING_H
#define SATCHEL_FRAME_TIMING_H
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { SATCHEL_TIMING_RING = 1024, SATCHEL_TIMING_WINDOW = 300 };
typedef struct {
    uint64_t frame;
    unsigned registered, completed, invalid;
    double gpu_ms, present_ms;
    unsigned presents;
    int used;
} SatchelTimingFrame;
typedef struct {
    double gpu[SATCHEL_TIMING_WINDOW], cpu[SATCHEL_TIMING_WINDOW];
    double wall[SATCHEL_TIMING_WINDOW], present[SATCHEL_TIMING_WINDOW];
    unsigned count, missing, pending, invalid, no_present;
} SatchelTimingWindow;

static inline SatchelTimingFrame *SatchelTimingSlot(SatchelTimingFrame *ring, uint64_t frame) {
    SatchelTimingFrame *r = &ring[frame % SATCHEL_TIMING_RING];
    if (!r->used || r->frame != frame) *r = (SatchelTimingFrame){.frame = frame, .used = 1};
    return r;
}
static inline void SatchelTimingBegin(SatchelTimingFrame *ring, uint64_t frame) {
    SatchelTimingSlot(ring, frame)->registered++;
}
static inline void SatchelTimingDone(SatchelTimingFrame *ring, uint64_t frame,
                                    double start, double end, int error, int present) {
    SatchelTimingFrame *r = &ring[frame % SATCHEL_TIMING_RING];
    // A delayed callback must never reset a newer frame after ring wrap.
    if (!r->used || r->frame != frame || r->completed >= r->registered) return;
    r->completed++;
    if (error || !isfinite(start) || !isfinite(end) || start <= 0 || end <= start) {
        r->invalid++;
        return;
    }
    double ms = (end - start) * 1000;
    r->gpu_ms += ms;
    if (present) { r->present_ms += ms; r->presents++; }
}
static inline void SatchelTimingCollect(SatchelTimingWindow *w, const SatchelTimingFrame *r,
                                       uint64_t frame, double cpu, double wall) {
    if (!r->used || r->frame != frame || !r->registered) { w->missing++; return; }
    if (r->completed != r->registered) { w->pending++; return; }
    if (r->invalid || !isfinite(cpu) || cpu < 0 || !isfinite(wall) || wall <= 0 ||
        !isfinite(r->gpu_ms) || r->gpu_ms <= 0) {
        w->invalid++; return;
    }
    // A dropped or not-yet-submitted display frame is not a complete GPU sample.
    if (!r->presents) { w->no_present++; return; }
    unsigned i = w->count++ % SATCHEL_TIMING_WINDOW;
    w->gpu[i] = r->gpu_ms; w->cpu[i] = cpu; w->wall[i] = wall; w->present[i] = r->present_ms;
}
static inline int SatchelTimingCompare(const void *a, const void *b) {
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}
static inline double SatchelTimingPercentile(const double *samples, unsigned n, unsigned percent) {
    if (!n) return -1;
    double sorted[SATCHEL_TIMING_WINDOW];
    if (n > SATCHEL_TIMING_WINDOW) n = SATCHEL_TIMING_WINDOW;
    memcpy(sorted, samples, n * sizeof *sorted);
    qsort(sorted, n, sizeof *sorted, SatchelTimingCompare);
    unsigned rank = (n * percent + 99) / 100;
    return sorted[rank ? rank - 1 : 0];
}
static inline const char *SatchelTimingLimit(double gpu, double cpu, double wall,
                                           double budget, unsigned valid, unsigned rejected) {
    if (valid < 30 || rejected > valid / 4 || budget <= 0) return "unknown";
    if (wall <= budget * 1.05 && gpu < budget * .9 && cpu < budget * .9) return "paced";
    if (gpu >= budget * .9 && gpu > cpu * 1.2) return "GPU-likely";
    if (cpu >= budget * .9 && cpu > gpu * 1.2) return "CPU-likely";
    if (gpu >= budget * .9 && cpu >= budget * .9) return "mixed";
    return "unknown";
}
#endif
