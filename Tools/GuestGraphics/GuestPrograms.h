#ifndef SATCHEL_GUEST_PROGRAMS_H
#define SATCHEL_GUEST_PROGRAMS_H
#include "GuestPools.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {SATCHEL_GP_LIMIT=8,SATCHEL_GP_OBJECT_SIZE=0xc0,SATCHEL_GP_CODE_LIMIT=65536,SATCHEL_GP_CONTROL_LIMIT=65536};
typedef enum {SATCHEL_GP_OK=0,SATCHEL_GP_INVALID,SATCHEL_GP_MEMORY,SATCHEL_GP_BAD_OBJECT,SATCHEL_GP_UNSUPPORTED,SATCHEL_GP_LIMIT_REACHED,SATCHEL_GP_BUSY,SATCHEL_GP_BACKEND,SATCHEL_GP_ALLOCATION,SATCHEL_GP_INTERNAL,SATCHEL_GP_CHANGED} SatchelGuestProgramStatus;
typedef struct SatchelGuestPrograms SatchelGuestPrograms;
typedef struct {
 uint32_t version,struct_size,stage,code_size,control_size,instruction_offset,instruction_size;
 uint32_t c1_offset,c1_size,local_x,local_y,local_z,shared_memory_size;
 uint32_t local_memory_known,local_memory_size,capture_only,compiler_ready;
 uint64_t generation,pool_generation;
} SatchelGuestProgramMetadata;
/* Host-only checked metadata capture, NOT a compiler/dispatch-success ABI.
 * Owns at most8 programs and one temporary retirement snapshot. NVN opaque
 * object size0xC0 and shaderData {GPU address,u64 guest control pointer} verified
 * against pinned reference. Initial capture profile supports one compute stage;
 * count0, graphics/multiple stages and unknown layouts explicitly unsupported.
 * Local-memory size remains UNKNOWN and compiler_ready is ALWAYS0. Do not turn
 * SATCHEL_GP_OK from capture into an NVN compiler/launch result.
 * Caller serializes CPU/owner/pool lifetime. Pools and validator context outlive
 * this owner. No CPU or callback context is borrowed across an operation. */
SatchelGuestPrograms *satchel_guest_programs_create(SatchelGuestPools *,SatchelGuestPoolDeviceValidator,SatchelGuestPoolCheckedRangeGuard,void *);
/* Initialized common object identity, independent of compute/raster capture.
 * Checked read-only query; output unchanged on failure. Host-only IDs. */
typedef struct {uint64_t guest_device,backend_device,device_generation,program_generation;} SatchelGuestProgramIdentity;
SatchelGuestProgramStatus satchel_guest_programs_identity(SatchelGuestPrograms *,SatchelCPUProbe *,uint64_t program,SatchelGuestProgramIdentity *);
SatchelGuestProgramStatus satchel_guest_programs_initialize(SatchelGuestPrograms *,SatchelCPUProbe *,uint64_t program,uint64_t guest_device);
/* Copies borrowed control while valid and snapshots actual GPU-visible code
 * through a retained pool lease. Never substitutes an original fixture. All
 * invalid/unsupported captures leave the previous active snapshot unchanged.
 * Code writes after capture are detected before metadata/copy access; callers
 * must explicitly recapture. Device/pool identity and full ranges revalidated. */
typedef SatchelGuestProgramStatus (*SatchelGuestProgramAdmission)(void *,const uint8_t *,const SatchelGuestProgramMetadata *);
/* Callback validates copied candidate before old capture is replaced. Output
 * generation is written only on committed success; no callback pointer retained. */
SatchelGuestProgramStatus satchel_guest_programs_set_shaders_admitted(SatchelGuestPrograms *,SatchelCPUProbe *,uint64_t,int32_t,uint64_t,SatchelGuestProgramAdmission,void *,uint64_t *);
SatchelGuestProgramStatus satchel_guest_programs_set_shaders(SatchelGuestPrograms *,SatchelCPUProbe *,uint64_t program,int32_t count,uint64_t shader_data);
SatchelGuestProgramStatus satchel_guest_programs_metadata(SatchelGuestPrograms *,SatchelCPUProbe *,uint64_t program,SatchelGuestProgramMetadata *);
/* Trusted host compiler handoff only; copies to caller RAM, never writes files
 * or logs. Caller buffers must be valid/nonoverlapping. No output changes unless
 * full validation succeeds. The compiler must still reject unknown metadata. */
SatchelGuestProgramStatus satchel_guest_programs_copy(SatchelGuestPrograms *,SatchelCPUProbe *,uint64_t program,void *code,size_t code_capacity,void *control,size_t control_capacity,SatchelGuestProgramMetadata *);
/* Finalize clears the guest tag atomically before releasing owned leases. If a
 * release is BUSY/error, the slot stays in retiring state and the call reports
 * failure; retry finalize or CPU-free try_destroy. No owner may be discarded. */
SatchelGuestProgramStatus satchel_guest_programs_finalize(SatchelGuestPrograms *,SatchelCPUProbe *,uint64_t program);
SatchelGuestProgramStatus satchel_guest_programs_device_children(SatchelGuestPrograms *,uint64_t guest_device,uint32_t *);
SatchelGuestProgramStatus satchel_guest_programs_check_unowned_range(SatchelGuestPrograms *,SatchelCPUProbe *,uint64_t address,uint64_t size);
/* Internal reciprocal pool guard. Composite owner must serialize lifetime.
 * Pure bounded physical-range scan, no active-lock acquisition/callbacks/writes;
 * valid during program operations that synchronously validate pool storage. */
SatchelGuestProgramStatus satchel_guest_programs_pool_guard(const SatchelGuestPrograms *,SatchelCPUProbe *,uint64_t address,uint64_t size);
/* Validate current captured code/C1 lease and reject physical overlap with a
 * writable guest span; unknown backing fails closed. No shader bytes exposed. */
SatchelGuestProgramStatus satchel_guest_programs_code_disjoint(SatchelGuestPrograms *,SatchelCPUProbe *,uint64_t program,uint64_t address,uint64_t size);
/* CPU-free teardown: releases program snapshots BEFORE pools/devices; any
 * failure preserves owner pointer and unreleased resources. */
SatchelGuestProgramStatus satchel_guest_programs_try_destroy(SatchelGuestPrograms **);
#ifdef __cplusplus
}
#endif
#endif
