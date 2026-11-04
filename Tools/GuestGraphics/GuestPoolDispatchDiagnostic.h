#ifndef SATCHEL_GUEST_POOL_DISPATCH_DIAGNOSTIC_H
#define SATCHEL_GUEST_POOL_DISPATCH_DIAGNOSTIC_H
#include "GuestPoolDiagnostic.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    SatchelGuestPoolDiagnostic memory;
    uint64_t instructions,dispatch_events;
    uint32_t dispatch_status,cpu_stop_reason,device_finalized;
} SatchelGuestPoolDispatchDiagnostic;
/* Separate original interpreted NVN device/pool fixture; no game input.
 * Uses real bootstrap/stubs/CPU breakpoints, actual pool upload/cache methods,
 * and a trusted host-submitted original GPU fill. NOT a guest queue/draw call.
 * Return1 means status0/stage10 with counts and all guards/cleanup verified.
 * Worker-only outside active CPU/dispatch callbacks. Calls and resource-count
 * snapshots must be serialized with other runtime diagnostic invocations.
 * Submitted cancellation hands off its dispatcher to the bounded retirement
 * collector, releases the synthetic CPU, and preserves resources_retained.
 * The next call polls retirement before new work, or returns BUSY. Private
 * deferred handoff ownership is retained on an unexpected handoff refusal.
 * No CPU/provider/cancel-context pointers survive return. The collector may
 * also drain other already-retired owners; it never touches active owners.
 * Driver calls are synchronous and the submitted wait is capped at5000ms. */
int satchel_guest_pool_dispatch_diagnostic(SatchelGuestPoolDispatchDiagnostic *report,SatchelGuestPoolCancelled cancelled,void *context);
#ifdef __cplusplus
}
#endif
#endif
