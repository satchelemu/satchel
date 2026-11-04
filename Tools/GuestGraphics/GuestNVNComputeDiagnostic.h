#ifndef SATCHEL_GUEST_NVN_COMPUTE_DIAGNOSTIC_H
#define SATCHEL_GUEST_NVN_COMPUTE_DIAGNOSTIC_H
#include "GuestPoolDiagnostic.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
 SatchelGuestPoolDiagnostic memory;
 uint64_t instructions,dispatch_events;
 uint32_t dispatch_status,cpu_stop_reason,program_initialized,program_admissions;
 uint32_t pipelines_compiled,fences_completed,cases_completed,commands_recorded;
 uint32_t unflushed_preserved,flushed_changed,changed_code_rebound;
 uint32_t program_finalized,handle_finalized,command_finalized,queue_finalized,device_finalized;
 uint32_t baseline_pipelines,final_pipelines;
} SatchelGuestNVNComputeDiagnostic;
/* Original interpreted NVN-only journey. Actual guest program metadata/code,
 * program admission, immutable recorded resources, real Vulkan pipeline/fence,
 * explicit memory-pool flush/invalidate and guarded readback. Four executions:
 * initial operands, unflushed edits, flushed edits, changed code/new recording.
 * Admission is not pipeline creation. This C compiler does not invoke/warm C#.
 * Serialize entire invocation with game graphics and retirement. On cancel or
 * timeout, bounded dispatch handoff owns all resources before CPU destruction;
 * next invocation reaps or returns BUSY. No borrowed CPU/provider/cancel state.
 * Success=status0/stage10 with four cases/pipelines/fences and matched cleanup.
 * No game input, arbitrary shader compatibility or game rendering claim. */
int satchel_guest_nvn_compute_diagnostic(SatchelGuestNVNComputeDiagnostic *,SatchelGuestPoolCancelled,void *);
#ifdef __cplusplus
}
#endif
#endif
