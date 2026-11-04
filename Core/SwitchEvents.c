#include "SwitchEvents.h"
#include <stdlib.h>
#include <string.h>

/* Original state implementation. Public ABI references: pinned libnx event.c/
 * wait.c (b156b4774fb2df7b93402169509266da7547fc4c); Atmosphere readable-event
 * and synchronization-object contracts (6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8).
 * No reference kernel implementation is copied or linked; this helper has no
 * scheduler/listeners, native handles, kernel result codes or guest memory. */

typedef struct {
    uint64_t id;
    uint32_t references;
    int signaled;
} EventSlot;
struct SatchelSwitchEvents {
    EventSlot slots[SATCHEL_SWITCH_EVENT_LIMIT];
    uint64_t last_id;
    size_t count;
};

#if defined(SATCHEL_SWITCH_EVENTS_TESTING)
static int fail_allocation;
void satchel_switch_events_test_fail_allocation(void) { fail_allocation = 1; }
int satchel_switch_events_test_last_id(SatchelSwitchEvents *events, uint64_t last_id) {
    if (!events || events->count || last_id < events->last_id) return 0;
    events->last_id = last_id;
    return 1;
}
#endif
SatchelSwitchEvents *satchel_switch_events_create(void) {
#if defined(SATCHEL_SWITCH_EVENTS_TESTING)
    if (fail_allocation) { fail_allocation = 0; return NULL; }
#endif
    return calloc(1, sizeof(SatchelSwitchEvents));
}
void satchel_switch_events_destroy(SatchelSwitchEvents *events) { free(events); }
static const EventSlot *find_event(const SatchelSwitchEvents *events, uint64_t id) {
    if (!events || !id) return NULL;
    for (size_t i = 0; i < SATCHEL_SWITCH_EVENT_LIMIT; ++i)
        if (events->slots[i].id == id) return &events->slots[i];
    return NULL;
}
static SatchelSwitchEventResult find_mutable(SatchelSwitchEvents *events, uint64_t id,
                                       EventSlot **slot) {
    if (!events || !id) return SATCHEL_SWITCH_EVENT_INVALID;
    *slot = (EventSlot *)find_event(events, id);
    return *slot ? SATCHEL_SWITCH_EVENT_OK : SATCHEL_SWITCH_EVENT_NOT_FOUND;
}
SatchelSwitchEventResult satchel_switch_event_create(SatchelSwitchEvents *events, uint64_t *id) {
    if (!events || !id) return SATCHEL_SWITCH_EVENT_INVALID;
    if (events->count == SATCHEL_SWITCH_EVENT_LIMIT || events->last_id == UINT64_MAX)
        return SATCHEL_SWITCH_EVENT_LIMIT_REACHED;
    for (size_t i = 0; i < SATCHEL_SWITCH_EVENT_LIMIT; ++i) {
        EventSlot *slot = &events->slots[i];
        if (slot->id) continue;
        *slot = (EventSlot){.id = ++events->last_id, .references = 1};
        ++events->count;
        *id = slot->id;
        return SATCHEL_SWITCH_EVENT_OK;
    }
    return SATCHEL_SWITCH_EVENT_LIMIT_REACHED;
}
SatchelSwitchEventResult satchel_switch_event_retain(SatchelSwitchEvents *events, uint64_t id) {
    EventSlot *slot;
    SatchelSwitchEventResult result = find_mutable(events, id, &slot);
    if (result != SATCHEL_SWITCH_EVENT_OK) return result;
    if (slot->references == SATCHEL_SWITCH_EVENT_REFERENCE_LIMIT)
        return SATCHEL_SWITCH_EVENT_LIMIT_REACHED;
    ++slot->references;
    return SATCHEL_SWITCH_EVENT_OK;
}
SatchelSwitchEventResult satchel_switch_event_release(SatchelSwitchEvents *events, uint64_t id) {
    EventSlot *slot;
    SatchelSwitchEventResult result = find_mutable(events, id, &slot);
    if (result != SATCHEL_SWITCH_EVENT_OK) return result;
    if (--slot->references == 0) {
        memset(slot, 0, sizeof(*slot));
        --events->count;
    }
    return SATCHEL_SWITCH_EVENT_OK;
}
SatchelSwitchEventResult satchel_switch_event_signal(SatchelSwitchEvents *events, uint64_t id) {
    EventSlot *slot;
    SatchelSwitchEventResult result = find_mutable(events, id, &slot);
    if (result == SATCHEL_SWITCH_EVENT_OK) slot->signaled = 1;
    return result;
}
SatchelSwitchEventResult satchel_switch_event_clear(SatchelSwitchEvents *events, uint64_t id) {
    EventSlot *slot;
    SatchelSwitchEventResult result = find_mutable(events, id, &slot);
    if (result == SATCHEL_SWITCH_EVENT_OK) slot->signaled = 0;
    return result;
}
SatchelSwitchEventResult satchel_switch_event_reset(SatchelSwitchEvents *events, uint64_t id) {
    EventSlot *slot;
    SatchelSwitchEventResult result = find_mutable(events, id, &slot);
    if (result != SATCHEL_SWITCH_EVENT_OK) return result;
    if (!slot->signaled) return SATCHEL_SWITCH_EVENT_INVALID_STATE;
    slot->signaled = 0;
    return SATCHEL_SWITCH_EVENT_OK;
}
SatchelSwitchEventResult satchel_switch_event_query(const SatchelSwitchEvents *events,
                                         uint64_t id, int *signaled) {
    if (!events || !id || !signaled) return SATCHEL_SWITCH_EVENT_INVALID;
    const EventSlot *slot = find_event(events, id);
    if (!slot) return SATCHEL_SWITCH_EVENT_NOT_FOUND;
    *signaled = slot->signaled;
    return SATCHEL_SWITCH_EVENT_OK;
}
SatchelSwitchEventResult satchel_switch_events_wait_any(const SatchelSwitchEvents *events,
    const uint64_t *ids, size_t count, size_t *index) {
    if (!events || !index || (!ids && count)) return SATCHEL_SWITCH_EVENT_INVALID;
    if (count > SATCHEL_SWITCH_EVENT_WAIT_LIMIT) return SATCHEL_SWITCH_EVENT_LIMIT_REACHED;
    const EventSlot *slots[SATCHEL_SWITCH_EVENT_WAIT_LIMIT];
    for (size_t i = 0; i < count; ++i) {
        if (!ids[i]) return SATCHEL_SWITCH_EVENT_INVALID;
        slots[i] = find_event(events, ids[i]);
        if (!slots[i]) return SATCHEL_SWITCH_EVENT_NOT_FOUND;
    }
    for (size_t i = 0; i < count; ++i) {
        if (!slots[i]->signaled) continue;
        *index = i;
        return SATCHEL_SWITCH_EVENT_OK;
    }
    return SATCHEL_SWITCH_EVENT_PENDING;
}
size_t satchel_switch_events_count(const SatchelSwitchEvents *events) {
    return events ? events->count : 0;
}
