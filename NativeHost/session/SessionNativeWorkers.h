#pragma once
#include <pthread.h>

// Native producers cooperate with stop. They never unwind through native frames.
// A reaper joins actual pthread exit, including TLS destructors, before removal.
typedef struct SatchelNativeWorker SatchelNativeWorker;
typedef struct {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    SatchelNativeWorker *head;
    pthread_t reaper;
    unsigned live;
    int phase, reaper_started, reaper_done, reaper_joined;
} SatchelNativeWorkers;
#define SATCHEL_NATIVE_WORKERS_INIT { .mutex = PTHREAD_MUTEX_INITIALIZER, .changed = PTHREAD_COND_INITIALIZER }
int satchel_native_create(SatchelNativeWorkers *, const pthread_attr_t *, void *(*)(void *), void *);
int satchel_native_stopping(SatchelNativeWorkers *);
// Returns 1 on stop, 0 when the interval expires. Also handles wakeup races.
int satchel_native_wait(SatchelNativeWorkers *, unsigned milliseconds);
void satchel_native_stop(SatchelNativeWorkers *);
int satchel_native_join(SatchelNativeWorkers *, unsigned timeout_ms);
int satchel_native_reset(SatchelNativeWorkers *);
unsigned satchel_native_live(SatchelNativeWorkers *);
