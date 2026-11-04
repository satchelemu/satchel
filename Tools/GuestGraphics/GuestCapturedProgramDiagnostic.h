#ifndef SATCHEL_GUEST_CAPTURED_PROGRAM_DIAGNOSTIC_H
#define SATCHEL_GUEST_CAPTURED_PROGRAM_DIAGNOSTIC_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef int (*SatchelGuestCapturedDiagnosticCancelled)(void *);
enum {SATCHEL_GUEST_CAPTURED_DIAG_OK,SATCHEL_GUEST_CAPTURED_DIAG_FAILED,SATCHEL_GUEST_CAPTURED_DIAG_CANCELLED,SATCHEL_GUEST_CAPTURED_DIAG_BUSY,SATCHEL_GUEST_CAPTURED_DIAG_INVALID};
typedef struct {
 uint32_t version,size,status,stage,owner_status,pool_status,backend_status;
 uint32_t source_admissions,pairs_compiled,pipelines_created,draws_submitted,fences_completed;
 uint32_t pre_submit_cancellations,pending_cancellations,source_rebound,values_matched,guards_matched,state_preserved;
 uint32_t program_finalized,pool_finalized,device_finalized,cpu_destroyed,cleanup_matched,resources_retained,prior_reaped;
 uint32_t baseline_devices,final_devices,baseline_buffers,final_buffers,baseline_jobs,final_jobs;
 uint32_t baseline_targets,final_targets,baseline_pipelines,final_pipelines;
 uint64_t baseline_buffer_bytes,final_buffer_bytes,baseline_target_bytes,final_target_bytes;
 uint64_t source_unused_host,source_unused_state;
} SatchelGuestCapturedProgramDiagnostic;
/* Fixed ORIGINAL checked guest-memory program capture, actual live-device ABI2
 * translation, independent typed Vulkan pipeline,4offscreen draws/readbacks.
 * Explicit ORIGINAL complete raster state; no game/NVN-dispatch/display claim.
 * Success: status0/stage10,2admissions/2compiled pairs/2pipelines/4draws/4fences,
 *4pre-submit and4pending cancellation checks, guards/state/cleanup matched.
 *
 * Returns status, not a boolean. Output is committed once the owned call ends.
 * Invalid/misaligned output or concurrent/reentrant BUSY preserves it unchanged.
 * Run on the same serial graphics worker as every GPU operation/retirement.
 * Cancellation must be pure; reads token/deadline only. Cooperative10s deadline,
 * per-compile<=1s and per-fence<=2.5s; driver creation is not forcibly interruptible.
 *
 * One private bounded pending owner retains tokens/leases on every nonzero-job
 * error/cancel/timeout. The private CPU is destroyed before retained GPU cleanup;
 * callbacks/borrowed context never survive return. A later call first polls the
 * old real fence and drains CPU-free owners, or returns BUSY. It never drops a
 * still-owned device. Pool64KiB, checked guest RAM260KiB,16x16RGBA8 target; all real
 * backend allocations remain charged by existing Vulkan registries. */
uint32_t satchel_guest_captured_program_diagnostic(SatchelGuestCapturedProgramDiagnostic *,SatchelGuestCapturedDiagnosticCancelled,void *);
#ifdef __cplusplus
}
#endif
#endif
