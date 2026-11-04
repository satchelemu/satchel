#pragma once
#include <stddef.h>
#include <stdatomic.h>

// Only trivially copied state has byte snapshots. Resource owners destroy
// allocations first. Objective-C references use typed retirement callbacks.
// Locks, once guards and TLS are never restored. All retirement requires quiescence.
typedef struct SatchelSessionState {
    struct SatchelSessionState *next;
    void *address, *initial;
    size_t size;
    void (*retire)(void *, size_t);
    unsigned owner;
    _Atomic int registered;
} SatchelSessionState;
enum { SATCHEL_STATE_GRAPHICS = 1, SATCHEL_STATE_NVN, SATCHEL_STATE_SERVICES };
void satchel_state_register(SatchelSessionState *, void *, void *, size_t, unsigned);
void satchel_state_register_retirement(SatchelSessionState *, void *, size_t, unsigned, void (*)(void *, size_t));
int satchel_state_restore(unsigned owner);
#define SATCHEL_STATE(owner_, value_) do { \
    static unsigned char initial_[sizeof(value_)]; \
    static SatchelSessionState record_; \
    if (!atomic_load_explicit(&record_.registered, memory_order_acquire)) \
        satchel_state_register(&record_, &(value_), initial_, sizeof(value_), (owner_)); \
} while (0)
