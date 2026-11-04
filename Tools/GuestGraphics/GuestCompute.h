#ifndef SATCHEL_GUEST_COMPUTE_H
#define SATCHEL_GUEST_COMPUTE_H
#include "GuestPrograms.h"
#include "ScalarCompiler.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct SatchelGuestCompute SatchelGuestCompute;
enum {SATCHEL_GC_OK,SATCHEL_GC_INVALID,SATCHEL_GC_MEMORY,SATCHEL_GC_UNSUPPORTED,SATCHEL_GC_BUSY,SATCHEL_GC_CANCELLED,SATCHEL_GC_BACKEND,SATCHEL_GC_ALLOCATION,SATCHEL_GC_CHANGED,SATCHEL_GC_INTERNAL};
typedef struct {uint64_t program,cb_gpu,cb_size,storage_gpu,storage_size;uint32_t uniform_slot,groups_x,groups_y,groups_z;} SatchelGuestComputeBinding;
typedef struct {uint32_t status,stage,compiled,submitted,completed,resources_retained,compiler_status,semantic_ops,stores,unused_memory_mask,bindings_retained;} SatchelGuestComputeReport;
/* Privately owns GuestPrograms; never accepts an externally mutable program
 * owner. Caller serializes composite CPU/pool/device/adapter lifetime. Supplied
 * pools/context outlive this owner. one immutable binding/run per owner;8programs.
 * Other-object guard excludes this owner's program objects (handled internally).
 * Existing pool guard must call pool_guard below to protect these new objects.
 * Program admission has a separate checked transaction; the explicit NVN composite
 * uses it before returning SetShaders success. Actual pipelines remain deferred. */
SatchelGuestCompute *satchel_guest_compute_create(SatchelGuestPools *,SatchelGuestPoolDeviceValidator,SatchelGuestPoolCheckedRangeGuard other_guard,void *);
/* Internal shared Program identity for the checked raster-family composite;
 * does not expose the mutable Program owner or assert compute admission. */
uint32_t satchel_guest_compute_program_identity(SatchelGuestCompute *,SatchelCPUProbe *,uint64_t,SatchelGuestProgramIdentity *);
uint32_t satchel_guest_compute_program_initialize(SatchelGuestCompute *,SatchelCPUProbe *,uint64_t program,uint64_t device);
uint32_t satchel_guest_compute_program_capture(SatchelGuestCompute *,SatchelCPUProbe *,uint64_t program,int32_t count,uint64_t shader_data);
uint32_t satchel_guest_compute_program_admit(SatchelGuestCompute *,SatchelCPUProbe *,uint64_t,int32_t,uint64_t);
uint32_t satchel_guest_compute_program_retain(SatchelGuestCompute *,SatchelCPUProbe *,uint64_t,uint64_t device,SatchelScalarRequirements *);
uint32_t satchel_guest_compute_program_release(SatchelGuestCompute *,uint64_t);
uint32_t satchel_guest_compute_program_finalize(SatchelGuestCompute *,SatchelCPUProbe *,uint64_t program);
/* Bind actual guest pool ranges; derive physical disjointness from CB AND full captured code/C1, never accept an
 * attestation from the caller. Retains both pool generations and program state.
 * Rebinding is allowed only without pending work; full admission precedes swap. */
uint32_t satchel_guest_compute_bind(SatchelGuestCompute *,SatchelCPUProbe *,const SatchelGuestComputeBinding *);
uint32_t satchel_guest_compute_unbind(SatchelGuestCompute *);
/* Capture actualGPU-visible CB and capturedcode, compile bounded scalar profile,
 * revalidate program/pools/CB, then submit. Only returned report.compiled means
 * actual compiler+pipeline acceptance. Nonzero job ownership never escapes owner.
 * Callbacks must not mutate CPU/pool/composite state; none is retained. */
uint32_t satchel_guest_compute_submit(SatchelGuestCompute *,SatchelCPUProbe *,SatchelGuestComputeReport *,SatchelCPUProbeCancelled,void *);
/* CPU-free; actual fence only. Cancellation/timeouts preserve all resources.
 * Completion destroys transient pipeline/UBO; binding stays until unbind.
 * resources_retained describes GPU transients; bindings_retained separately
 * reports retained guest binding leases. Program objects survive until finalize. */
uint32_t satchel_guest_compute_wait(SatchelGuestCompute *,uint32_t timeout_ms,SatchelGuestComputeReport *,SatchelCPUProbeCancelled,void *);
/* Internal reciprocal pool guard: pure bounded scan under composite lifetime
 * serialization, no callbacks or recursive active-lock acquisition. */
int satchel_guest_compute_pool_guard(const SatchelGuestCompute *,SatchelCPUProbe *,uint64_t,uint64_t);
/* Device finalization must reject while any program child remains. */
uint32_t satchel_guest_compute_device_children(SatchelGuestCompute *,uint64_t device,uint32_t *count);
/* CPU-free retirement drains its run, binding leases, then programs. BUSY/error
 * preserves pointer. Caller retains pools/device/context until this succeeds. */
uint32_t satchel_guest_compute_try_destroy(SatchelGuestCompute **);
#ifdef __cplusplus
}
#endif
#endif
