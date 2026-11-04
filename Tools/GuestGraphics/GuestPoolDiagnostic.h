#ifndef SATCHEL_GUEST_POOL_DIAGNOSTIC_H
#define SATCHEL_GUEST_POOL_DIAGNOSTIC_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {SATCHEL_GUEST_POOL_DIAG_OK=0,SATCHEL_GUEST_POOL_DIAG_INVALID,SATCHEL_GUEST_POOL_DIAG_BUSY,
    SATCHEL_GUEST_POOL_DIAG_CANCELLED,SATCHEL_GUEST_POOL_DIAG_FAILED};
typedef struct {
    uint32_t status,stage,pool_status,backend_status;
    uint32_t device_created,pool_created,initial_upload,flush_completed,gpu_completed,invalidate_completed;
    uint32_t values_matched,guards_matched,state_preserved,pool_finalized,cleanup_matched,resources_retained,prior_reaped;
    uint32_t baseline_devices,final_devices,baseline_buffers,final_buffers,baseline_jobs,final_jobs;
    uint64_t baseline_bytes,final_bytes;
} SatchelGuestPoolDiagnostic;
typedef int (*SatchelGuestPoolCancelled)(void *context);
/* Fixed original guest-memory fixture, NOT guest NVN calls or game graphics.
 * Uses a separate checked CPU memory map, real pool upload/flush, actual GPU
 * fill/fence, invalidate and guarded comparisons. No external data/CPU input.
 * Returns1 only on verified success (report.status==OK); numeric report only.
 * Worker-only, outside CPU execution. Cancellation is checked between stages
 * and during the <=5000ms fence wait; driver operations remain synchronous.
 * On timeout/cancel/error with submitted work, releases the synthetic CPU but
 * retains the private pool/device/job owner. A later call polls/reaps that exact
 * job before new work or reports BUSY. No ownership is discarded. Up to one
 * private job/device/pool is retained; irrecoverable driver failure can keep it
 * until process exit. Concurrent/reentrant calls return BUSY without inspecting
 * mutable owner state. Cancel/context pointers are never stored. */
int satchel_guest_pool_diagnostic(SatchelGuestPoolDiagnostic *report,SatchelGuestPoolCancelled cancelled,void *context);
#ifdef __cplusplus
}
#endif
#endif
