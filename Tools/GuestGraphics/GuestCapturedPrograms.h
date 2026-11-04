#ifndef SATCHEL_GUEST_CAPTURED_PROGRAMS_H
#define SATCHEL_GUEST_CAPTURED_PROGRAMS_H
#include "GuestPools.h"
#include "GuestRasterPrograms.h"
#include "CapturedFlatAdmission.h"
#include "CapturedHostAdapter.h"
#include "VulkanBackend.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {SATCHEL_GCAP_PROGRAM_LIMIT=8,SATCHEL_GCAP_REFERENCE_LIMIT=32,
      SATCHEL_GCAP_CODE_LIMIT=65536,SATCHEL_GCAP_CONTROL_LIMIT=65536,
      SATCHEL_GCAP_SPH_VERTEX=1,SATCHEL_GCAP_SPH_FRAGMENT=5,
      SATCHEL_GCAP_NVN_VERTEX_BIT=1,
      SATCHEL_GCAP_NVN_FRAGMENT_BIT=2};
enum {SATCHEL_GCAP_OK,SATCHEL_GCAP_INVALID,SATCHEL_GCAP_MEMORY,SATCHEL_GCAP_BAD_OBJECT,
      SATCHEL_GCAP_UNSUPPORTED,SATCHEL_GCAP_LIMIT,SATCHEL_GCAP_BUSY,SATCHEL_GCAP_BACKEND,
      SATCHEL_GCAP_ALLOCATION,SATCHEL_GCAP_CHANGED,SATCHEL_GCAP_INTERNAL,SATCHEL_GCAP_STATE_UNAVAILABLE,SATCHEL_GCAP_CANCELLED};
enum {SATCHEL_GCAP_FAMILY_UNASSIGNED,SATCHEL_GCAP_FAMILY_RASTER,SATCHEL_GCAP_FAMILY_COMPUTE};
typedef struct SatchelGuestCapturedPrograms SatchelGuestCapturedPrograms;
typedef struct {
    uint32_t sph_stage,nvn_stage_bit,code_size,control_size;
    uint32_t sph_offset,instruction_offset,instruction_size,c1_offset,c1_size;
    uint64_t pool_generation;
    SatchelCFAdmissionStage admission;
} SatchelGuestCapturedStage;
typedef struct {
    uint32_t version,size,nvn_stage_mask,admitted,compiled;
    uint64_t program_generation,capture_generation;
    SatchelGuestCapturedStage vertex,fragment;
    SatchelCFAdmission source_requirements;
} SatchelGuestCapturedProgramInfo;
/* Owns only paired snapshots and recording references, never guest tags or
 * common Program state. Pools/common identity context outlive this owner.
 * Caller serializes all composite/CPU/pool use, including callbacks. */
SatchelGuestCapturedPrograms *satchel_guest_captured_programs_create(SatchelGuestPools *,SatchelGuestRasterIdentityValidator,void *);
/* Exactly two original NVNshaderData{gpu,u64 control} entries. Stages are derived
 * from actual SPH and may arrive in either order. Both copied controls and full
 * GPU-visible code/C1 spans are bounded/retained before actual code admission.
 * Source admission has a fixed1s/65,536-work-unit limit.
 * No graphics state is invented; compiled stays0. Failure preserves previous
 * active capture and its leases. An old successful capture retires separately
 * after atomic replacement, so cleanup failure cannot corrupt active state.
 * COMPUTE→RASTER is unsupported until common Finalize→Initialize; the composite
 * changes its family only after this call commits successfully, without failure. */
uint32_t satchel_guest_captured_programs_capture(SatchelGuestCapturedPrograms *,SatchelCPUProbe *,uint64_t program,
                                        int32_t count,uint64_t shader_data,SatchelGuestCapturedProgramInfo *);
uint32_t satchel_guest_captured_programs_info(SatchelGuestCapturedPrograms *,SatchelCPUProbe *,uint64_t,SatchelGuestCapturedProgramInfo *);
/* Each opaque host reference retains one exact capture; no reference token or
 * backend identity may enter guest RAM. Retains block replacement/forget. */
uint32_t satchel_guest_captured_programs_retain(SatchelGuestCapturedPrograms *,SatchelCPUProbe *,uint64_t program,
                                       uint64_t *reference,SatchelGuestCapturedProgramInfo *);
uint32_t satchel_guest_captured_programs_release(SatchelGuestCapturedPrograms *,uint64_t reference);
/* Validates common identity, both live pool generations and all GPU-visible
 * captured bytes, then calls the actual ABI2 compiler under complete recorded state.
 * No pipeline creation or default state. The optional cancellation callback must
 * be pure and cannot mutate owners, guest memory or resources. Output pair is
 * unchanged on failure; compiled in returned metadata is1 only after actual ABI2 success. */
/* Complete actual state only. Missing known state (including unknown provoking)
 * returns STATE_UNAVAILABLE; source_unused_state is not an observed default and
 * does not relax v1/v2. Target is a live same-device backend-owned token supplied
 * by the checked texture owner, not read from guest bytes. No pipeline is made. */
uint32_t satchel_guest_captured_programs_compile(SatchelGuestCapturedPrograms *,SatchelCPUProbe *,uint64_t reference,
 const SatchelCFState *,uint64_t target,SatchelCFPairV2 *,SatchelGuestCapturedProgramInfo *,SatchelVulkanCancelled,void *);
/* Checks a writable guest span against both full captured code/C1 physical
 * spans. Unknown/locked mappings are not assumed disjoint; CPU range validation
 * belongs to the caller. No native pointers are exposed. */
uint32_t satchel_guest_captured_programs_code_disjoint(SatchelGuestCapturedPrograms *,SatchelCPUProbe *,uint64_t reference,
                                              uint64_t address,uint64_t size);
typedef struct {
    uint32_t sph_stage,nvn_stage_bit;
    uint64_t capture_generation,code_gpu,code_cpu,code_size;
    SatchelGuestPoolView pool;
    SatchelGuestPoolBinding binding;
} SatchelGuestCapturedCodeBinding;
/* Read-only HOST metadata, canonical vertex/fragment order. Revalidates actual
 * common identity, both pool generations and all GPU-visible captured bytes.
 * Full wrapper/SPH/instructions/C1 ranges are returned, never payload, native
 * pointers, or the owner's private lease tokens. Entire pair unchanged on
 * failure. Caller provides a valid two-element host output span.
 * These are current snapshots, not durable mapping/visibility guarantees.
 * Under composite serialization, retain extra generation-checked pool leases
 * and deduplicate pool holds before later submission/publication. */
uint32_t satchel_guest_captured_programs_code_bindings(SatchelGuestCapturedPrograms *,SatchelCPUProbe *,uint64_t reference,
                                              SatchelGuestCapturedCodeBinding out[2]);
/* Query before common finalization; mutation belongs to composite transaction.
 * Forget ONLY after common tag removal succeeds. It needs no CPU and retains
 * partial cleanup state on failure. Failure of common removal must not call it. */
uint32_t satchel_guest_captured_programs_references(SatchelGuestCapturedPrograms *,uint64_t program,uint64_t generation,uint32_t *);
uint32_t satchel_guest_captured_programs_forget(SatchelGuestCapturedPrograms *,uint64_t program,uint64_t generation);
uint32_t satchel_guest_captured_programs_device_children(SatchelGuestCapturedPrograms *,uint64_t guest_device,uint32_t *);
uint32_t satchel_guest_captured_programs_collect(SatchelGuestCapturedPrograms *);
/* CPU-free teardown. Caller first releases recorded references; BUSY/error
 * preserves owner and every remaining lease. No guest memory is touched. */
uint32_t satchel_guest_captured_programs_try_destroy(SatchelGuestCapturedPrograms **);
#ifdef __cplusplus
}
#endif
#endif
