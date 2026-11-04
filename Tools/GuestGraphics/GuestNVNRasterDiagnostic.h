#ifndef SATCHEL_GUEST_NVN_RASTER_DIAGNOSTIC_H
#define SATCHEL_GUEST_NVN_RASTER_DIAGNOSTIC_H
#include "GuestPoolDiagnostic.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
 SatchelGuestPoolDiagnostic memory;
 uint64_t instructions,dispatch_events;
 uint32_t dispatch_status,cpu_stop_reason,program_admissions,pipelines_compiled;
 uint32_t commands_recorded,fences_completed,cpu_cache_preserved;
 uint32_t programs_finalized,texture_finalized,handle_finalized,command_finalized,queue_finalized,device_finalized;
 uint32_t baseline_targets,final_targets,baseline_pipelines,final_pipelines;
 uint64_t baseline_target_bytes,final_target_bytes;
} SatchelGuestNVNRasterDiagnostic;
/* Fixed ORIGINAL interpreted NVN calls, actual VS/two FS instruction captures,
 * actual state setters, ClearColor+two DrawArrays, real Vulkan queue/fence and
 * block-linear pool publication/invalidation/padding. No game shader/frame.
 * Run on the same serial graphics worker as all GPU calls/retirement; cancellation
 * only reads token/deadline. No borrowed callback/CPU context survives return.
 * Pending resources stay in bounded C handoff ownership and are reaped on the
 * next call, or report BUSY. Success status0/stage8, admissions2/pipelines2,
 * commands3/fences1 with values/guards/cache/state/cleanup true. Does not warm C#.
 */
int satchel_guest_nvn_raster_diagnostic(SatchelGuestNVNRasterDiagnostic *,SatchelGuestPoolCancelled,void *);
#ifdef __cplusplus
}
#endif
#endif
