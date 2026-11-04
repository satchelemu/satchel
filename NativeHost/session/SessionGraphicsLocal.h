#pragma once
#include "SessionState.h"
// Each registered address is a static strong object or an array of strong
// objects. ARC stores, not byte copies, release its contents after quiescence.
static void satchel_arc_retire(void *address, size_t bytes) {
    __strong id *objects = (__strong id *)address;
    for (size_t i = 0; i < bytes / sizeof(id); i++) objects[i] = nil;
}
#define SATCHEL_ARC(value) do { \
    static SatchelSessionState record; \
    if (!atomic_load_explicit(&record.registered, memory_order_acquire)) \
        satchel_state_register_retirement(&record, (void *)&value, sizeof(value), SATCHEL_STATE_GRAPHICS, satchel_arc_retire); \
} while (0)
