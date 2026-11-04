#ifndef SATCHEL_GUEST_RASTER_PROGRAMS_H
#define SATCHEL_GUEST_RASTER_PROGRAMS_H
#include "GuestPools.h"
#include "RasterCompiler.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {SATCHEL_GRP_PROGRAM_LIMIT=8,SATCHEL_GRP_REFERENCE_LIMIT=32,
      SATCHEL_GRP_CODE_LIMIT=65536,SATCHEL_GRP_CONTROL_LIMIT=65536,
      SATCHEL_GRP_SPH_VERTEX=1,SATCHEL_GRP_SPH_FRAGMENT=5,
      SATCHEL_GRP_NVN_VERTEX_BIT=SATCHEL_RASTER_NVN_VERTEX_BIT,
      SATCHEL_GRP_NVN_FRAGMENT_BIT=SATCHEL_RASTER_NVN_FRAGMENT_BIT};
enum {SATCHEL_GRP_OK,SATCHEL_GRP_INVALID,SATCHEL_GRP_MEMORY,SATCHEL_GRP_BAD_OBJECT,
      SATCHEL_GRP_UNSUPPORTED,SATCHEL_GRP_LIMIT,SATCHEL_GRP_BUSY,SATCHEL_GRP_BACKEND,
      SATCHEL_GRP_ALLOCATION,SATCHEL_GRP_CHANGED,SATCHEL_GRP_INTERNAL};
enum {SATCHEL_GRP_FAMILY_UNASSIGNED,SATCHEL_GRP_FAMILY_RASTER,SATCHEL_GRP_FAMILY_COMPUTE};
typedef struct {
    uint64_t guest_device,backend_device,device_generation,program_generation;
} SatchelGuestRasterIdentity;
/* The composite owns ONE initialized Program/tag plus its family table. This
 * synchronous callback validates the actual common object/device generation,
 * physical range/tag, and reports that family's existing value. It must not
 * reenter this owner or retain the borrowed CPU. False never changes outputs.
 * Common Program identity does not imply compiled shader or pipeline success. */
typedef int (*SatchelGuestRasterIdentityValidator)(void *,SatchelCPUProbe *,uint64_t,
                                             SatchelGuestRasterIdentity *,uint32_t *family);
typedef struct SatchelGuestRasterPrograms SatchelGuestRasterPrograms;
typedef struct {
    uint32_t sph_stage,nvn_stage_bit,code_size,control_size;
    uint32_t sph_offset,instruction_offset,instruction_size,c1_offset,c1_size;
    uint64_t pool_generation;
    SatchelRasterAdmission admission;
} SatchelGuestRasterStage;
typedef struct {
    uint32_t version,size,nvn_stage_mask,admitted,compiled;
    uint64_t program_generation,capture_generation;
    SatchelGuestRasterStage vertex,fragment;
} SatchelGuestRasterProgramInfo;
/* Owns only paired snapshots and recording references, never guest tags or
 * common Program state. Pools/common identity context outlive this owner.
 * Caller serializes all composite/CPU/pool use, including callbacks. */
SatchelGuestRasterPrograms *satchel_guest_raster_programs_create(SatchelGuestPools *,SatchelGuestRasterIdentityValidator,void *);
/* Exactly two original NVNshaderData{gpu,u64 control} entries. Stages are derived
 * from actual SPH and may arrive in either order. Both copied controls and full
 * GPU-visible code/C1 spans are bounded/retained before actual code admission.
 * No graphics state is invented; compiled stays0. Failure preserves previous
 * active capture and its leases. An old successful capture retires separately
 * after atomic replacement, so cleanup failure cannot corrupt active state.
 * COMPUTE→RASTER is unsupported until common Finalize→Initialize; the composite
 * changes its family only after this call commits successfully, without failure. */
uint32_t satchel_guest_raster_programs_capture(SatchelGuestRasterPrograms *,SatchelCPUProbe *,uint64_t program,
                                        int32_t count,uint64_t shader_data,SatchelGuestRasterProgramInfo *);
uint32_t satchel_guest_raster_programs_info(SatchelGuestRasterPrograms *,SatchelCPUProbe *,uint64_t,SatchelGuestRasterProgramInfo *);
/* Each opaque host reference retains one exact capture; no reference token or
 * backend identity may enter guest RAM. Retains block replacement/forget. */
uint32_t satchel_guest_raster_programs_retain(SatchelGuestRasterPrograms *,SatchelCPUProbe *,uint64_t program,
                                       uint64_t *reference,SatchelGuestRasterProgramInfo *);
uint32_t satchel_guest_raster_programs_release(SatchelGuestRasterPrograms *,uint64_t reference);
/* Validates common identity, both live pool generations and all GPU-visible
 * captured bytes, then emits actual code under actual recorded graphics state.
 * No pipeline creation, resources, callbacks or default state. Output pair is
 * unchanged on failure; compiled in returned metadata is1 only on success. */
uint32_t satchel_guest_raster_programs_compile(SatchelGuestRasterPrograms *,SatchelCPUProbe *,uint64_t reference,
                                        const SatchelRasterState *,SatchelRasterModule *,SatchelRasterModule *,
                                        SatchelGuestRasterProgramInfo *);
/* Checks a writable guest span against both full captured code/C1 physical
 * spans. Unknown/locked mappings are not assumed disjoint; CPU range validation
 * belongs to the caller. No native pointers are exposed. */
uint32_t satchel_guest_raster_programs_code_disjoint(SatchelGuestRasterPrograms *,SatchelCPUProbe *,uint64_t reference,
                                              uint64_t address,uint64_t size);
typedef struct {
    uint32_t sph_stage,nvn_stage_bit;
    uint64_t capture_generation,code_gpu,code_cpu,code_size;
    SatchelGuestPoolView pool;
    SatchelGuestPoolBinding binding;
} SatchelGuestRasterCodeBinding;
/* Read-only HOST metadata, canonical vertex/fragment order. Revalidates actual
 * common identity, both pool generations and all GPU-visible captured bytes.
 * Full wrapper/SPH/instructions/C1 ranges are returned, never payload, native
 * pointers, or the owner's private lease tokens. Entire pair unchanged on
 * failure. Caller provides a valid two-element host output span.
 * These are current snapshots, not durable mapping/visibility guarantees.
 * Under composite serialization, retain extra generation-checked pool leases
 * and deduplicate pool holds before later submission/publication. */
uint32_t satchel_guest_raster_programs_code_bindings(SatchelGuestRasterPrograms *,SatchelCPUProbe *,uint64_t reference,
                                              SatchelGuestRasterCodeBinding out[2]);
/* Query before common finalization; mutation belongs to composite transaction.
 * Forget ONLY after common tag removal succeeds. It needs no CPU and retains
 * partial cleanup state on failure. Failure of common removal must not call it. */
uint32_t satchel_guest_raster_programs_references(SatchelGuestRasterPrograms *,uint64_t program,uint64_t generation,uint32_t *);
uint32_t satchel_guest_raster_programs_forget(SatchelGuestRasterPrograms *,uint64_t program,uint64_t generation);
uint32_t satchel_guest_raster_programs_device_children(SatchelGuestRasterPrograms *,uint64_t guest_device,uint32_t *);
uint32_t satchel_guest_raster_programs_collect(SatchelGuestRasterPrograms *);
/* CPU-free teardown. Caller first releases recorded references; BUSY/error
 * preserves owner and every remaining lease. No guest memory is touched. */
uint32_t satchel_guest_raster_programs_try_destroy(SatchelGuestRasterPrograms **);
#ifdef __cplusplus
}
#endif
#endif
