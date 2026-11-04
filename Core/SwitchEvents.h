#ifndef SATCHEL_SWITCH_EVENTS_H
#define SATCHEL_SWITCH_EVENTS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { SATCHEL_SWITCH_EVENT_LIMIT = 32, SATCHEL_SWITCH_EVENT_REFERENCE_LIMIT = 4096,
       SATCHEL_SWITCH_EVENT_WAIT_LIMIT = 64 };
typedef struct SatchelSwitchEvents SatchelSwitchEvents;
typedef enum {
    SATCHEL_SWITCH_EVENT_OK, SATCHEL_SWITCH_EVENT_INVALID, SATCHEL_SWITCH_EVENT_NOT_FOUND,
    SATCHEL_SWITCH_EVENT_LIMIT_REACHED, SATCHEL_SWITCH_EVENT_INVALID_STATE,
    SATCHEL_SWITCH_EVENT_PENDING
} SatchelSwitchEventResult;
/* Original, bounded virtual event state. IDs belong to this pool only; they are
 * not guest handles or native objects. Process owns handle rights and validates
 * all handle translations. Caller serializes every operation and pool lifetime.
 * No callback, host waiting, clock, scheduler, timeout or fabricated wakeup. */
SatchelSwitchEvents *satchel_switch_events_create(void);
void satchel_switch_events_destroy(SatchelSwitchEvents *events);
/* Creates an unsignaled event with one owned reference. Nonzero IDs increase
 * monotonically and are never reused within a pool, even after slot recycling.
 * On every failure, output arguments and existing state remain unchanged. */
SatchelSwitchEventResult satchel_switch_event_create(SatchelSwitchEvents *events, uint64_t *id);
SatchelSwitchEventResult satchel_switch_event_retain(SatchelSwitchEvents *events, uint64_t id);
SatchelSwitchEventResult satchel_switch_event_release(SatchelSwitchEvents *events, uint64_t id);
SatchelSwitchEventResult satchel_switch_event_signal(SatchelSwitchEvents *events, uint64_t id);
/* Clear is idempotent; reset requires a signaled event, otherwise INVALID_STATE.
 * Waiting observes the level and does not clear it. Process decides which
 * writable/readable handles may invoke these operations. */
SatchelSwitchEventResult satchel_switch_event_clear(SatchelSwitchEvents *events, uint64_t id);
SatchelSwitchEventResult satchel_switch_event_reset(SatchelSwitchEvents *events, uint64_t id);
SatchelSwitchEventResult satchel_switch_event_query(const SatchelSwitchEvents *events,
                                         uint64_t id, int *signaled);
/* Snapshot wait-any: validate ALL IDs first, then return OK and the first
 * signaled input index, or PENDING without modifying the output. Duplicates
 * allowed; count zero with NULL ids is valid and returns PENDING. Maximum 64.
 * No event is consumed and no reference retained. A future scheduler must
 * retain all objects for a suspended wait and release them when it completes;
 * Process handles timeout/cancellation and translation to kernel results. */
SatchelSwitchEventResult satchel_switch_events_wait_any(const SatchelSwitchEvents *events,
    const uint64_t *ids, size_t count, size_t *index);
size_t satchel_switch_events_count(const SatchelSwitchEvents *events);
#ifdef __cplusplus
}
#endif
#endif
