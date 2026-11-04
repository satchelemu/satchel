#ifndef SATCHEL_HOST_NVN_H
#define SATCHEL_HOST_NVN_H
#include "GuestDispatch.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct SatchelHostNVN SatchelHostNVN;
typedef struct {
 uint32_t reached,created,status,operation_id,window_ms,cleanup_status;
 uint64_t events,returned_calls,elapsed_ms;
 uint32_t retiring_contexts,pending_devices,active_contexts,backend_failures;
 uint32_t live_devices,live_buffers,live_submissions,live_targets,live_pipelines,live_compute_pipelines,live_raster_runs;
 uint64_t buffer_bytes,target_bytes,raster_host_bytes;
} SatchelHostNVNReport;
uint64_t satchel_host_nvn_now_ns(void);
/* Configuration only. Does not create graphics/dispatcher. Absolute outer
 * deadline is never reset; window starts only at first genuine bootstrap.
 * All calls serialized on one exclusive worker. No CPU retained. */
SatchelHostNVN *satchel_host_nvn_create(uint64_t bootstrap,uint64_t stubs,uint64_t deadline_ns,
 uint32_t window_ms,uint32_t events);
SatchelGuestDispatchStatus satchel_host_nvn_handle(SatchelHostNVN *,SatchelCPUProbe *,const SatchelCPUProbeResult *,
 SatchelCPUProbeCancelled,void *);
/* Pure deadline query, safe from the execution cancellation callback. */
int satchel_host_nvn_should_cancel(const SatchelHostNVN *);
int satchel_host_nvn_report(const SatchelHostNVN *,SatchelHostNVNReport *);
/* Handoff before CPU teardown. Drain after CPU teardown, on same worker; no
 * provider, CPU or execution-cancellation context is retained. drain_ms<=5000.
 * A pending collector remains globally owned and truthfully reported. */
int satchel_host_nvn_handoff(SatchelHostNVN *);
int satchel_host_nvn_finish(SatchelHostNVN **,uint32_t drain_ms,SatchelHostNVNReport *);
#ifdef __cplusplus
}
#endif
#endif
