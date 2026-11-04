#include "SessionNativeWorkers.h"
#include <assert.h>
#include <errno.h>
#include <stdatomic.h>
#include <stdint.h>
#include <unistd.h>

static SatchelNativeWorkers workers = SATCHEL_NATIVE_WORKERS_INIT;
static pthread_key_t key;
static _Atomic int entered, released, ready;
static void destructor(void *unused) {
    (void)unused; atomic_store(&entered, 1);
    while (!atomic_load(&released)) usleep(1000);
}
static void *worker(void *unused) {
    (void)unused; pthread_setspecific(key, (void *)(uintptr_t)1);
    assert(satchel_native_join(&workers, 0) == EBUSY);
    atomic_store(&ready, 1);
    while (!satchel_native_wait(&workers, 3600000)) {}
    assert(satchel_native_join(&workers, 0) == EDEADLK);
    return NULL;
}
void owners_native_verify(void) {
    assert(!pthread_key_create(&key, destructor));
    assert(!satchel_native_create(&workers, NULL, worker, NULL));
    for (int i = 0; !atomic_load(&ready); i++) { assert(i < 2000); usleep(1000); }
    satchel_native_stop(&workers);
    for (int i = 0; !atomic_load(&entered); i++) { assert(i < 2000); usleep(1000); }
    assert(satchel_native_join(&workers, 10) == ETIMEDOUT);
    assert(satchel_native_live(&workers) == 1 && satchel_native_reset(&workers) == EBUSY);
    assert(satchel_native_create(&workers, NULL, worker, NULL) == ECANCELED);
    atomic_store(&released, 1);
    assert(!satchel_native_join(&workers, 2000));
    assert(!satchel_native_live(&workers) && !satchel_native_reset(&workers));
    pthread_key_delete(key);
}
