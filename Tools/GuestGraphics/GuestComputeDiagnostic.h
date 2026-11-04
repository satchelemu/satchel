#ifndef SATCHEL_GUEST_COMPUTE_DIAGNOSTIC_H
#define SATCHEL_GUEST_COMPUTE_DIAGNOSTIC_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef int (*SatchelGuestComputeCancelled)(void *);
enum {SATCHEL_GUEST_COMPUTE_OK=0,SATCHEL_GUEST_COMPUTE_FAILED=1,SATCHEL_GUEST_COMPUTE_CANCELLED=2,SATCHEL_GUEST_COMPUTE_BUSY=3};
typedef struct {
    uint32_t status,stage,compiler_status,backend_status,runtime_features;
    uint32_t compiled,spirv_bytes,active_bindings,declared_bindings,precise_math_requested;
    uint32_t pipeline_created,cases_completed,fences_completed,values_matched,guards_matched;
    uint32_t pipeline_finalized,buffers_finalized,device_finalized,cleanup_matched,resources_retained,prior_reaped;
    uint32_t baseline_devices,final_devices,baseline_buffers,final_buffers,baseline_jobs,final_jobs,baseline_pipelines,final_pipelines;
    uint64_t baseline_bytes,final_bytes;
} SatchelGuestComputeDiagnostic;
/* ORIGINAL fixed Maxwell64-byte input only, compiled on this device into SPIR-V.
 * Consumes that actual compiler output in a persistent Vulkan pipeline. Fourteen
 * runtime operand pairs (including NaN/subnormal bit patterns) use the same real
 * pipeline and updated zero-padded64KiB UBO; output is GPU-computed uint32 add.
 * This is not an interpreted NVN compute command or a game shader/frame.
 * Entire call and snapshots share the serial graphics worker. Every pending
 * job/pipeline/descriptor/buffer stays privately owned across cancellation/error;
 * next call reaps or returns BUSY. No CPU/provider/callback context is retained.
 * Checks a10s cooperative deadline, with real fence waits<=5s. Synchronous
 * compiler/driver creation cannot be forcibly interrupted. Safe metadata only. */
int satchel_guest_compute_diagnostic(SatchelGuestComputeDiagnostic *,SatchelGuestComputeCancelled,void *);
#ifdef __cplusplus
}
#endif
#endif
