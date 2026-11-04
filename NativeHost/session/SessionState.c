#include "SessionState.h"
#include "SessionBoundary.h"
#include <errno.h>
#include <pthread.h>
#include <string.h>

static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static SatchelSessionState *states;

void satchel_state_register(SatchelSessionState *record, void *address, void *initial,
                       size_t size, unsigned owner) {
    pthread_mutex_lock(&mutex);
    if (!atomic_load_explicit(&record->registered, memory_order_relaxed)) {
        record->address = address; record->initial = initial;
        record->size = size; record->owner = owner;
        memcpy(initial, address, size);
        record->next = states; states = record;
        atomic_store_explicit(&record->registered, 1, memory_order_release);
    }
    pthread_mutex_unlock(&mutex);
}

void satchel_state_register_retirement(SatchelSessionState *record, void *address, size_t size,
                                  unsigned owner, void (*retire)(void *, size_t)) {
    pthread_mutex_lock(&mutex);
    if (!atomic_load_explicit(&record->registered, memory_order_relaxed)) {
        record->address = address; record->size = size; record->owner = owner; record->retire = retire;
        record->next = states; states = record;
        atomic_store_explicit(&record->registered, 1, memory_order_release);
    }
    pthread_mutex_unlock(&mutex);
}
int satchel_state_restore(unsigned owner) {
    if (!satchel_session_quiescent()) return EBUSY;
    pthread_mutex_lock(&mutex);
    for (SatchelSessionState *record = states; record; record = record->next)
        if (record->owner == owner) {
            if (record->retire) record->retire(record->address, record->size);
            else memcpy(record->address, record->initial, record->size);
        }
    pthread_mutex_unlock(&mutex);
    return 0;
}

__attribute__((weak, noinline)) void satchel_retire_checkpoint(unsigned checkpoint) { (void)checkpoint; }
