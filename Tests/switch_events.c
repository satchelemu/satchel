#include "SwitchEvents.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void satchel_switch_events_test_fail_allocation(void);
int satchel_switch_events_test_last_id(SatchelSwitchEvents *, uint64_t);
static unsigned checks;
static void check(int okay, const char *message) {
    ++checks;
    if (!okay) { fprintf(stderr, "FAIL: %s\n", message); exit(1); }
}
static uint64_t create(SatchelSwitchEvents *events) {
    uint64_t id = 0;
    check(satchel_switch_event_create(events, &id) == SATCHEL_SWITCH_EVENT_OK && id,
          "create owned original event");
    return id;
}
static void state(SatchelSwitchEvents *events, uint64_t id, int expected) {
    int value = 99;
    check(satchel_switch_event_query(events, id, &value) == SATCHEL_SWITCH_EVENT_OK && value == expected,
          "actual event signal state");
}
static void lifetime_tests(void) {
    satchel_switch_events_destroy(NULL);
    check(satchel_switch_events_count(NULL) == 0, "null count");
    for (unsigned i = 0; i < 32; ++i) {
        satchel_switch_events_test_fail_allocation();
        check(!satchel_switch_events_create(), "allocation failure leaves no event pool");
        SatchelSwitchEvents *retry = satchel_switch_events_create();
        check(retry && !satchel_switch_events_count(retry), "allocation failure is one shot");
        satchel_switch_events_destroy(retry);
    }
    SatchelSwitchEvents *events = satchel_switch_events_create();
    check(events != NULL, "pool allocation");
    uint64_t context = create(events);
    state(events, context, 0);
    check(satchel_switch_event_reset(events, context) == SATCHEL_SWITCH_EVENT_INVALID_STATE,
          "reset unsignaled event fails instead of inventing a signal");
    check(satchel_switch_event_clear(events, context) == SATCHEL_SWITCH_EVENT_OK,
          "clear unsignaled event is idempotent");
    for (unsigned i = 0; i < 3; ++i)
        check(satchel_switch_event_retain(events, context) == SATCHEL_SWITCH_EVENT_OK,
              "readable copies retain persistent event");
    check(satchel_switch_event_signal(events, context) == SATCHEL_SWITCH_EVENT_OK &&
          satchel_switch_event_signal(events, context) == SATCHEL_SWITCH_EVENT_OK,
          "signal is level state, not accumulating fake wakeups");
    for (unsigned i = 0; i < 3; ++i) {
        check(satchel_switch_event_release(events, context) == SATCHEL_SWITCH_EVENT_OK,
              "closing readable copies releases exactly one reference");
        state(events, context, 1);
    }
    // A reserved copy that cannot be published in the TLS reply is rolled back.
    check(satchel_switch_event_retain(events, context) == SATCHEL_SWITCH_EVENT_OK &&
          satchel_switch_event_release(events, context) == SATCHEL_SWITCH_EVENT_OK,
          "failed publication leaves persistent applet reference intact");
    size_t index = 99;
    for (unsigned i = 0; i < 5; ++i)
        check(satchel_switch_events_wait_any(events, &context, 1, &index) == SATCHEL_SWITCH_EVENT_OK && index == 0,
              "successful wait does not consume manual event");
    state(events, context, 1);
    check(satchel_switch_event_reset(events, context) == SATCHEL_SWITCH_EVENT_OK, "reset actual signal");
    index = 99;
    check(satchel_switch_events_wait_any(events, &context, 1, &index) == SATCHEL_SWITCH_EVENT_PENDING && index == 99,
          "unsignaled wait is pending, never fabricated success or timeout");
    check(satchel_switch_event_release(events, context) == SATCHEL_SWITCH_EVENT_OK && !satchel_switch_events_count(events),
          "last owned reference destroys event");
    int value = 99;
    check(satchel_switch_event_query(events, context, &value) == SATCHEL_SWITCH_EVENT_NOT_FOUND && value == 99,
          "closed event query preserves output");
    uint64_t newer = create(events);
    check(newer > context && satchel_switch_event_signal(events, context) == SATCHEL_SWITCH_EVENT_NOT_FOUND,
          "slot reuse never resurrects stale ID");
    state(events, newer, 0);
    for (unsigned i = 1; i < SATCHEL_SWITCH_EVENT_REFERENCE_LIMIT; ++i)
        check(satchel_switch_event_retain(events, newer) == SATCHEL_SWITCH_EVENT_OK, "bounded references accepted");
    check(satchel_switch_event_retain(events, newer) == SATCHEL_SWITCH_EVENT_LIMIT_REACHED,
          "reference cap cannot overflow or partially increment");
    for (unsigned i = SATCHEL_SWITCH_EVENT_REFERENCE_LIMIT; i; --i) {
        check(satchel_switch_event_release(events, newer) == SATCHEL_SWITCH_EVENT_OK, "owned reference release");
        if (i > 1) state(events, newer, 0);
    }
    check(satchel_switch_event_release(events, newer) == SATCHEL_SWITCH_EVENT_NOT_FOUND,
          "double release cannot underflow refs");
    uint64_t last = newer, stale[SATCHEL_SWITCH_EVENT_LIMIT];
    for (unsigned round = 0; round < 16; ++round) {
        for (size_t i = 0; i < SATCHEL_SWITCH_EVENT_LIMIT; ++i) {
            stale[i] = create(events);
            check(stale[i] > last, "IDs monotonic across many slot-recycling cycles");
            last = stale[i];
        }
        uint64_t output = 99;
        check(satchel_switch_events_count(events) == SATCHEL_SWITCH_EVENT_LIMIT &&
              satchel_switch_event_create(events, &output) == SATCHEL_SWITCH_EVENT_LIMIT_REACHED && output == 99,
              "slot cap fails atomically");
        for (size_t i = 0; i < SATCHEL_SWITCH_EVENT_LIMIT; ++i)
            check(satchel_switch_event_release(events, stale[i]) == SATCHEL_SWITCH_EVENT_OK, "release full pool");
        check(satchel_switch_events_count(events) == 0, "all slots reclaimed");
    }
    check(satchel_switch_events_test_last_id(events, UINT64_MAX - 1), "private exhaustion seam on empty pool");
    uint64_t maximum = create(events);
    check(maximum == UINT64_MAX, "last valid ID does not wrap");
    check(satchel_switch_event_release(events, maximum) == SATCHEL_SWITCH_EVENT_OK, "release maximum ID");
    uint64_t output = 99;
    check(satchel_switch_event_create(events, &output) == SATCHEL_SWITCH_EVENT_LIMIT_REACHED && output == 99 &&
          !satchel_switch_events_count(events), "ID exhaustion never reuses an old identity");
    satchel_switch_events_destroy(events);
}
static void wait_tests(void) {
    SatchelSwitchEvents *events = satchel_switch_events_create();
    check(events != NULL, "wait fixture pool");
    uint64_t ids[8];
    for (size_t i = 0; i < 8; ++i) ids[i] = create(events);
    for (unsigned mask = 0; mask < 256; ++mask) {
        for (unsigned i = 0; i < 8; ++i) {
            SatchelSwitchEventResult result = (mask & (1u << i)) ?
                satchel_switch_event_signal(events, ids[i]) : satchel_switch_event_clear(events, ids[i]);
            check(result == SATCHEL_SWITCH_EVENT_OK, "set independent signal matrix");
        }
        for (size_t count = 0; count <= SATCHEL_SWITCH_EVENT_WAIT_LIMIT; ++count) {
            uint64_t list[SATCHEL_SWITCH_EVENT_WAIT_LIMIT];
            size_t expected = SIZE_MAX;
            for (size_t i = 0; i < count; ++i) {
                unsigned source = (unsigned)((i * 3 + count) % 8);
                list[i] = ids[source];
                if (expected == SIZE_MAX && (mask & (1u << source))) expected = i;
            }
            size_t index = 99;
            SatchelSwitchEventResult result = satchel_switch_events_wait_any(events, count ? list : NULL, count, &index);
            check(result == (expected == SIZE_MAX ? SATCHEL_SWITCH_EVENT_PENDING : SATCHEL_SWITCH_EVENT_OK) &&
                  index == (expected == SIZE_MAX ? 99 : expected),
                  "independent wait-any matrix selects first signal and handles duplicate IDs");
            for (unsigned i = 0; i < 8; ++i) state(events, ids[i], !!(mask & (1u << i)));
        }
    }
    check(satchel_switch_event_signal(events, ids[0]) == SATCHEL_SWITCH_EVENT_OK, "ready object before invalid trailing handle");
    uint64_t list[] = {ids[0], UINT64_MAX};
    size_t index = 99;
    check(satchel_switch_events_wait_any(events, list, 2, &index) == SATCHEL_SWITCH_EVENT_NOT_FOUND && index == 99,
          "valid ready prefix cannot bypass invalid trailing object");
    list[1] = 0;
    check(satchel_switch_events_wait_any(events, list, 2, &index) == SATCHEL_SWITCH_EVENT_INVALID && index == 99,
          "zero trailing object validates before readiness");
    state(events, ids[0], 1);
    check(satchel_switch_events_wait_any(events, ids, 65, &index) == SATCHEL_SWITCH_EVENT_LIMIT_REACHED && index == 99 &&
          satchel_switch_events_wait_any(events, ids, SIZE_MAX, &index) == SATCHEL_SWITCH_EVENT_LIMIT_REACHED && index == 99,
          "oversize counts reject before list access");
    check(satchel_switch_events_wait_any(NULL, ids, 1, &index) == SATCHEL_SWITCH_EVENT_INVALID &&
          satchel_switch_events_wait_any(events, NULL, 1, &index) == SATCHEL_SWITCH_EVENT_INVALID &&
          satchel_switch_events_wait_any(events, ids, 1, NULL) == SATCHEL_SWITCH_EVENT_INVALID && index == 99,
          "invalid wait pointers preserve caller output");
    // A suspended wait's retained object can outlive the original guest handle.
    check(satchel_switch_event_retain(events, ids[0]) == SATCHEL_SWITCH_EVENT_OK &&
          satchel_switch_event_release(events, ids[0]) == SATCHEL_SWITCH_EVENT_OK,
          "wait-owned reference outlives handle closure");
    check(satchel_switch_events_wait_any(events, ids, 1, &index) == SATCHEL_SWITCH_EVENT_OK && index == 0,
          "retained wait observes real signal");
    check(satchel_switch_event_release(events, ids[0]) == SATCHEL_SWITCH_EVENT_OK,
          "completed wait releases retained object");
    index = 99;
    check(satchel_switch_events_wait_any(events, ids, 1, &index) == SATCHEL_SWITCH_EVENT_NOT_FOUND && index == 99,
          "completed wait retains no invisible helper reference");
    satchel_switch_events_destroy(events);
}
static void invalid_tests(void) {
    SatchelSwitchEvents *events = satchel_switch_events_create();
    check(events != NULL, "invalid fixture pool");
    uint64_t id = create(events), output = 99;
    check(satchel_switch_event_create(NULL, &output) == SATCHEL_SWITCH_EVENT_INVALID && output == 99 &&
          satchel_switch_event_create(events, NULL) == SATCHEL_SWITCH_EVENT_INVALID && satchel_switch_events_count(events) == 1,
          "invalid create is atomic");
    SatchelSwitchEventResult (*operations[])(SatchelSwitchEvents *, uint64_t) = {
        satchel_switch_event_retain, satchel_switch_event_release, satchel_switch_event_signal,
        satchel_switch_event_clear, satchel_switch_event_reset};
    for (size_t i = 0; i < sizeof(operations)/sizeof(operations[0]); ++i) {
        check(operations[i](NULL, id) == SATCHEL_SWITCH_EVENT_INVALID &&
              operations[i](events, 0) == SATCHEL_SWITCH_EVENT_INVALID &&
              operations[i](events, UINT64_MAX) == SATCHEL_SWITCH_EVENT_NOT_FOUND,
              "invalid event operation does not modify live event");
        state(events, id, 0);
    }
    int signaled = 99;
    check(satchel_switch_event_query(NULL, id, &signaled) == SATCHEL_SWITCH_EVENT_INVALID &&
          satchel_switch_event_query(events, 0, &signaled) == SATCHEL_SWITCH_EVENT_INVALID &&
          satchel_switch_event_query(events, id, NULL) == SATCHEL_SWITCH_EVENT_INVALID && signaled == 99,
          "invalid query preserves output");
    SatchelSwitchEvents *other = satchel_switch_events_create();
    check(other != NULL, "separate process event pool");
    uint64_t other_id = create(other);
    check(satchel_switch_event_signal(other, other_id) == SATCHEL_SWITCH_EVENT_OK, "signal other process event");
    state(events, id, 0);
    satchel_switch_events_destroy(events);
    state(other, other_id, 1);
    satchel_switch_events_destroy(other);
}
int main(void) {
    lifetime_tests(); wait_tests(); invalid_tests();
    printf("Switch events: %u original ownership/state/wait assertions passed.\n", checks);
    return 0;
}
