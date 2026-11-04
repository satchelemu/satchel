#include "SessionInputGate.h"
#include "SessionBoundary.h"
#include <stdatomic.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

static _Atomic int closed;
static _Atomic unsigned publishers;
int satchel_input_enter(void) {
    atomic_fetch_add(&publishers, 1);
    if (atomic_load(&closed) || !satchel_session_active() || satchel_session_stopping()) {
        atomic_fetch_sub(&publishers, 1); return 0;
    }
    return 1;
}
void satchel_input_leave(int *entered) { if (*entered) atomic_fetch_sub(&publishers, 1); }
void satchel_input_stop(void) { atomic_store(&closed, 1); }
int satchel_input_join(unsigned milliseconds) {
    if (!atomic_load(&closed)) return EBUSY;
    uint64_t until = clock_gettime_nsec_np(CLOCK_MONOTONIC) + (uint64_t)milliseconds * 1000000;
    while (atomic_load(&publishers)) {
        if (clock_gettime_nsec_np(CLOCK_MONOTONIC) >= until) return ETIMEDOUT;
        usleep(1000);
    }
    return 0;
}
void satchel_input_reset(void) { atomic_store(&closed, 0); }
