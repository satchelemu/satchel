#ifndef SATCHEL_GUEST_TRANSFER_DIAGNOSTIC_H
#define SATCHEL_GUEST_TRANSFER_DIAGNOSTIC_H
#include "GuestPoolDiagnostic.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
    SatchelGuestPoolDiagnostic memory;
    uint64_t instructions,dispatch_events;
    uint32_t dispatch_status,cpu_stop_reason,queue_created,commands_recorded;
    uint32_t queue_submitted,queue_finished,handle_finalized,command_finalized,queue_finalized,device_finalized;
} SatchelGuestTransferDiagnostic;
/* Fixed ORIGINAL interpreted guest only: bootstrap/BLR/stubs record fill+copies
 * in caller-supplied command/control RAM, submit a real Vulkan batch, wait its
 * fence and invalidate/read guarded values. No game input or host submission.
 * Success=status0/stage10 with guards/state/cleanup checked. Entire invocation
 * must share the serial graphics worker with game dispatch and retirement.
 * Cancellation/timeout transfers owner to the bounded collector BEFORE freeing
 * CPU. Nonzero jobs are never discarded; deferred handoff is privately retained
 * on refusal. Next invocation reaps or reports BUSY. No CPU/provider/cancellation
 * context survives. Safe numeric report only; no token/payload/driver text. */
int satchel_guest_transfer_diagnostic(SatchelGuestTransferDiagnostic *,SatchelGuestPoolCancelled,void *);
#ifdef __cplusplus
}
#endif
#endif
