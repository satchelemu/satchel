#include "FrameTiming.h"
#include <assert.h>
#include <stdio.h>

static SatchelTimingFrame ring[SATCHEL_TIMING_RING];
static void complete(uint64_t frame, double gpu, double present) {
    SatchelTimingBegin(ring, frame); SatchelTimingBegin(ring, frame);
    SatchelTimingDone(ring, frame, 1, 1 + gpu / 1000, 0, 0);
    SatchelTimingDone(ring, frame, 2, 2 + present / 1000, 0, 1);
}
int main(void) {
    SatchelTimingWindow w = {0};
    for (unsigned f = 0; f < 300; f++) {
        complete(f, f + 1, 1);
        SatchelTimingCollect(&w, &ring[f], f, f + 2, f + 3);
    }
    assert(w.count == 300);
    assert(fabs(SatchelTimingPercentile(w.gpu, w.count, 50) - 151) < .00001);
    assert(fabs(SatchelTimingPercentile(w.gpu, w.count, 95) - 286) < .00001);
    SatchelTimingCollect(&w, &ring[500], 500, 1, 2); assert(w.missing == 1);
    SatchelTimingBegin(ring, 501);
    SatchelTimingCollect(&w, &ring[501], 501, 1, 2); assert(w.pending == 1);
    SatchelTimingDone(ring, 501, 0, 2, 0, 1);
    SatchelTimingCollect(&w, &ring[501], 501, 1, 2); assert(w.invalid == 1);
    double bad[][2] = {{2, 1}, {2, 2}, {NAN, 3}, {2, INFINITY}, {1, 2}};
    for (unsigned i = 0; i < 5; i++) {
        unsigned f = 502 + i; SatchelTimingBegin(ring, f);
        SatchelTimingDone(ring, f, bad[i][0], bad[i][1], i == 4, 1);
        SatchelTimingCollect(&w, &ring[f], f, 1, 2);
    }
    assert(w.invalid == 6);
    complete(507, 1, 1); SatchelTimingCollect(&w, &ring[507], 507, -1, 2); assert(w.invalid == 7);
    SatchelTimingBegin(ring, 508); SatchelTimingDone(ring, 508, 1, 2, 0, 0);
    SatchelTimingCollect(&w, &ring[508], 508, 1, 2); assert(w.no_present == 1);
    SatchelTimingBegin(ring, 1024); SatchelTimingDone(ring, 0, 1, 2, 0, 1);
    assert(ring[0].frame == 1024 && ring[0].completed == 0);
    SatchelTimingDone(ring, 1024, 1, 2, 0, 1);
    SatchelTimingDone(ring, 1024, 1, 2, 0, 1); assert(ring[0].completed == 1);
    assert(!strcmp(SatchelTimingLimit(40, 12, 45, 33.333, 100, 0), "GPU-likely"));
    assert(!strcmp(SatchelTimingLimit(10, 40, 45, 33.333, 100, 0), "CPU-likely"));
    assert(!strcmp(SatchelTimingLimit(40, 40, 45, 33.333, 100, 0), "mixed"));
    assert(!strcmp(SatchelTimingLimit(10, 10, 33.333, 33.333, 100, 0), "paced"));
    assert(!strcmp(SatchelTimingLimit(10, 10, 45, 33.333, 100, 0), "unknown"));
    assert(!strcmp(SatchelTimingLimit(40, 10, 45, 33.333, 29, 0), "unknown"));
    assert(!strcmp(SatchelTimingLimit(40, 10, 45, 33.333, 100, 26), "unknown"));
    puts("PASS: frame timing pairing, percentiles, invalid/error/pending/dropped rejection, wrap and inference");
}
