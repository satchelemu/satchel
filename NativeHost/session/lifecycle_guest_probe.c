#include "SessionBoundary.h"
#include <assert.h>
#include <stdatomic.h>
#include <stdio.h>
#include <unistd.h>
extern void lifecycle_guest(void *), lifecycle_guest_end(void);
extern void lifecycle_native_guest(void *), lifecycle_native_guest_end(void);
extern _Atomic int lifecycle_inside_host, lifecycle_host_returned;
int lifecycle_host_lock_available(void);
static _Atomic unsigned long counter;
static void *run(void *unused) { (void)unused; satchel_session_run_guest(lifecycle_guest, &counter); return NULL; }
static void *native_run(void *unused) { (void)unused; satchel_session_run_guest(lifecycle_native_guest, &counter); return NULL; }
int main(void) {
    assert(!satchel_session_begin());
    assert(!satchel_session_guest_range(lifecycle_guest, (char *)lifecycle_native_guest_end - (char *)lifecycle_guest));
    pthread_t native; assert(!satchel_session_pthread_create(&native, NULL, native_run, NULL));
    while (!atomic_load(&lifecycle_inside_host)) usleep(1000);
    satchel_session_pause(1); usleep(100000);
    assert(atomic_load(&lifecycle_host_returned) && lifecycle_host_lock_available());
    assert(!atomic_load(&counter));
    satchel_session_pause(0); usleep(10000); assert(atomic_load(&counter));
    satchel_session_request_stop(); assert(!satchel_session_join(2000)); assert(!satchel_session_end());
    for (int cycle = 0; cycle < 10; cycle++) {
        assert(!satchel_session_begin());
        assert(!satchel_session_guest_range(lifecycle_guest, (char *)lifecycle_guest_end - (char *)lifecycle_guest));
        pthread_t thread; assert(!satchel_session_pthread_create(&thread, NULL, run, NULL));
        usleep(10000); assert(atomic_load(&counter));
        for (int i = 0; i < 10; i++) {
            satchel_session_pause(1); usleep(10000);
            unsigned long before = atomic_load(&counter); usleep(10000); assert(before == atomic_load(&counter));
            satchel_session_pause(0); usleep(10000); assert(before != atomic_load(&counter));
        }
        satchel_session_pause(1); satchel_session_request_stop(); assert(!satchel_session_join(2000)); assert(!satchel_session_end());
    }
    puts("PASS: native ARM64 guest parks exactly, 100 pause/resume pairs, native locks released before parking, stop while parked, ten fresh sessions");
}
