// Exercise the real helper registry and cancellation paths. These tiny SDK
// adapters replace guest malloc/CreateThread/WaitThread, not the helper owner.
#include "hle.h"
#include "media_probe.h"
#include <assert.h>
static void *helper_symbol(const char *);
#define guest_sym helper_symbol
#define dm_guest_thread_spawn fixture_helper_spawn
#define dm_guest_thread_join fixture_helper_join
#include "hle_guestthread.c"
#undef guest_sym
#undef dm_guest_thread_spawn
#undef dm_guest_thread_join

#define RAW __attribute__((no_instrument_function))
extern void fixture_spin(void *);
static media_fixture waiting;
static void *guest_stack;
static _Atomic int helper_cleaned, join_cleaned;
static void *RAW sdk_malloc(size_t bytes) {
    assert(!guest_stack); guest_stack = calloc(1, bytes); assert(guest_stack); return guest_stack;
}
static int RAW sdk_create(void *tt, void (*fn)(void *), void *arg, void *stack, size_t size, int priority) {
    (void)tt; (void)priority; assert(stack && size);
    media_start_guest(fn, arg); return 0;
}
static void RAW sdk_start(void *tt) { (void)tt; }
static void RAW sdk_wait(void *tt) { (void)tt; fixture_spin(&waiting); }
static void RAW sdk_destroy(void *tt) { (void)tt; abort(); }
static void RAW sdk_free(void *p) { (void)p; abort(); }
static void *helper_symbol(const char *name) {
    if (!strcmp(name, "malloc")) return sdk_malloc;
    if (!strcmp(name, "free")) return sdk_free;
    if (strstr(name, "CreateThread")) return sdk_create;
    if (strstr(name, "StartThread")) return sdk_start;
    if (strstr(name, "WaitThread")) return sdk_wait;
    if (strstr(name, "DestroyThread")) return sdk_destroy;
    abort();
}
static void helper_body(void *unused) {
    (void)unused;
    while (!satchel_session_stopping()) usleep(1000);
    atomic_store(&helper_cleaned, 1);
}
static void create_and_join(void *unused) {
    (void)unused;
    void *t = fixture_helper_spawn(helper_body, NULL, 16, 65536); assert(t);
    fixture_helper_join(t);
    assert(satchel_session_stopping());
    atomic_store(&join_cleaned, 1);
}
void media_helper_start(void) {
    memset(&waiting, 0, sizeof waiting);
    atomic_store(&helper_cleaned, 0); atomic_store(&join_cleaned, 0);
    media_start_guest(create_and_join, NULL); media_await(&waiting.ready);
    assert(helpers && guest_stack);
}
void media_helper_verify(void) {
    assert(atomic_load(&helper_cleaned) && atomic_load(&join_cleaned));
    assert(helpers); // Cancelled SDK join retained ThreadType until pthread join.
    assert(!satchel_guest_helpers_session_reset() && !helpers);
    free(guest_stack); guest_stack = NULL; // Synthetic guest allocator arena teardown.
}
