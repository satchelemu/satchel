#include "SessionNativeWorkers.h"
#include <errno.h>
#include <stdlib.h>
#include <time.h>

struct SatchelNativeWorker {
    SatchelNativeWorker *next;
    SatchelNativeWorkers *owner;
    pthread_t thread;
    void *(*entry)(void *);
    void *argument;
    int finished;
};

static struct timespec deadline(unsigned ms) {
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    t.tv_sec += ms / 1000;
    t.tv_nsec += (long)(ms % 1000) * 1000000;
    if (t.tv_nsec >= 1000000000) { t.tv_nsec -= 1000000000; t.tv_sec++; }
    return t;
}

static void finished(void *value) {
    SatchelNativeWorker *worker = value;
    SatchelNativeWorkers *owner = worker->owner;
    pthread_mutex_lock(&owner->mutex);
    worker->finished = 1;
    pthread_cond_broadcast(&owner->changed);
    pthread_mutex_unlock(&owner->mutex);
}

static void *run(void *value) {
    SatchelNativeWorker *worker = value;
    void *result;
    pthread_cleanup_push(finished, worker);
    result = worker->entry(worker->argument);
    pthread_cleanup_pop(1);
    return result;
}

static void *reap(void *value) {
    SatchelNativeWorkers *owner = value;
    pthread_mutex_lock(&owner->mutex);
    for (;;) {
        SatchelNativeWorker *worker = owner->head;
        while (worker && !worker->finished) worker = worker->next;
        if (worker) {
            pthread_mutex_unlock(&owner->mutex);
            int result = pthread_join(worker->thread, NULL);
            if (result) abort(); // An owned joinable pthread has exactly one joiner.
            pthread_mutex_lock(&owner->mutex);
            SatchelNativeWorker **link = &owner->head;
            while (*link != worker) link = &(*link)->next;
            *link = worker->next;
            owner->live--;
            free(worker);
            pthread_cond_broadcast(&owner->changed);
        } else if (owner->phase == 2 && !owner->live) break;
        else pthread_cond_wait(&owner->changed, &owner->mutex);
    }
    owner->reaper_done = 1;
    pthread_cond_broadcast(&owner->changed);
    pthread_mutex_unlock(&owner->mutex);
    return NULL;
}

int satchel_native_create(SatchelNativeWorkers *owner, const pthread_attr_t *attr,
                     void *(*entry)(void *), void *argument) {
    int detached = PTHREAD_CREATE_JOINABLE;
    if (attr) pthread_attr_getdetachstate(attr, &detached);
    if (detached != PTHREAD_CREATE_JOINABLE) return EINVAL;
    SatchelNativeWorker *worker = calloc(1, sizeof *worker);
    if (!worker) return ENOMEM;
    worker->owner = owner; worker->entry = entry; worker->argument = argument;
    pthread_mutex_lock(&owner->mutex);
    int result = owner->phase == 2 ? ECANCELED : 0;
    if (!result && !owner->reaper_started) {
        result = pthread_create(&owner->reaper, NULL, reap, owner);
        if (!result) { owner->reaper_started = 1; owner->phase = 1; }
    }
    if (!result) result = pthread_create(&worker->thread, attr, run, worker);
    if (!result) { worker->next = owner->head; owner->head = worker; owner->live++; }
    pthread_mutex_unlock(&owner->mutex);
    if (result) free(worker);
    return result;
}

int satchel_native_stopping(SatchelNativeWorkers *owner) {
    pthread_mutex_lock(&owner->mutex);
    int stop = owner->phase == 2;
    pthread_mutex_unlock(&owner->mutex);
    return stop;
}

int satchel_native_wait(SatchelNativeWorkers *owner, unsigned ms) {
    struct timespec until = deadline(ms);
    pthread_mutex_lock(&owner->mutex);
    while (owner->phase != 2)
        if (pthread_cond_timedwait(&owner->changed, &owner->mutex, &until) == ETIMEDOUT) break;
    int stop = owner->phase == 2;
    pthread_mutex_unlock(&owner->mutex);
    return stop;
}

void satchel_native_stop(SatchelNativeWorkers *owner) {
    pthread_mutex_lock(&owner->mutex);
    owner->phase = 2;
    pthread_cond_broadcast(&owner->changed);
    pthread_mutex_unlock(&owner->mutex);
}

int satchel_native_join(SatchelNativeWorkers *owner, unsigned timeout_ms) {
    struct timespec until = deadline(timeout_ms);
    pthread_mutex_lock(&owner->mutex);
    if (owner->phase != 2) { pthread_mutex_unlock(&owner->mutex); return EBUSY; }
    for (SatchelNativeWorker *worker = owner->head; worker; worker = worker->next)
        if (pthread_equal(pthread_self(), worker->thread)) {
            pthread_mutex_unlock(&owner->mutex); return EDEADLK;
        }
    while (owner->reaper_started && !owner->reaper_done) {
        int result = pthread_cond_timedwait(&owner->changed, &owner->mutex, &until);
        if (result) { pthread_mutex_unlock(&owner->mutex); return result; }
    }
    // Only the coordinator calls join/reset. Keep this lock across the reaper
    // join so no concurrent admission or reset can observe a partial transition.
    if (owner->reaper_started && !owner->reaper_joined) {
        int result = pthread_join(owner->reaper, NULL);
        if (result) { pthread_mutex_unlock(&owner->mutex); return result; }
        owner->reaper_joined = 1;
    }
    pthread_mutex_unlock(&owner->mutex);
    return 0;
}

int satchel_native_reset(SatchelNativeWorkers *owner) {
    pthread_mutex_lock(&owner->mutex);
    int result = owner->phase != 2 || owner->live ||
                 (owner->reaper_started && !owner->reaper_joined) ? EBUSY : 0;
    if (!result) owner->phase = owner->reaper_started = owner->reaper_done = owner->reaper_joined = 0;
    pthread_mutex_unlock(&owner->mutex);
    return result;
}

unsigned satchel_native_live(SatchelNativeWorkers *owner) {
    pthread_mutex_lock(&owner->mutex);
    unsigned live = owner->live;
    pthread_mutex_unlock(&owner->mutex);
    return live;
}
