#ifndef SATCHEL_GUEST_RASTER_RECORD_H
#define SATCHEL_GUEST_RASTER_RECORD_H
#include "GuestRasterRun.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct SatchelGuestRasterRecord SatchelGuestRasterRecord;
enum {SATCHEL_GRR_OK,SATCHEL_GRR_INVALID,SATCHEL_GRR_MEMORY,SATCHEL_GRR_BAD_OBJECT,SATCHEL_GRR_UNSUPPORTED,
 SATCHEL_GRR_LIMIT,SATCHEL_GRR_BUSY,SATCHEL_GRR_BACKEND,SATCHEL_GRR_ALLOCATION,SATCHEL_GRR_INTERNAL};
enum {SATCHEL_GRR_TARGET=1,SATCHEL_GRR_PROGRAM,SATCHEL_GRR_VERTEX_BUFFER,SATCHEL_GRR_VERTEX_ATTRIBUTES,
 SATCHEL_GRR_VERTEX_STREAM,SATCHEL_GRR_VIEWPORT,SATCHEL_GRR_SCISSOR,SATCHEL_GRR_DEPTH_RANGE,SATCHEL_GRR_COLOR,
 SATCHEL_GRR_CHANNEL,SATCHEL_GRR_POLYGON,SATCHEL_GRR_DEPTH_STENCIL,SATCHEL_GRR_MULTISAMPLE,SATCHEL_GRR_DEPTH_CLAMP,
 SATCHEL_GRR_RASTER_DISCARD,SATCHEL_GRR_SAMPLE_MASK,SATCHEL_GRR_RENDER_ENABLE,SATCHEL_GRR_SWIZZLES,
 SATCHEL_GRR_CLEAR,SATCHEL_GRR_DRAW};
typedef struct {uint32_t operation;uint64_t x[6];uint32_t f[2];} SatchelGuestRasterStateRequest;
/* Supplied by the common command owner: actual queue/device identity and owned
 * high-level NVN API invariants, never a read of invented hardware registers.
 * mode fields0 are known NVN initialization defaults, not 'unknown'. This first
 * renderer currently accepts only upper-left/near-zero at draw time. */
typedef struct {uint32_t version,size,known,origin_upper_left,depth_zero_to_one,
 viewport_transform_enabled,y_negate,transform_feedback;uint64_t device,backend,device_generation;} SatchelGuestRasterQueueState;
/* Guard protects command/control/queue/private objects from writable target
 * overlap; it may allow ordinary pool data. Never retains the borrowed CPU. */
typedef int (*SatchelGuestRasterRecordGuard)(void *,SatchelCPUProbe *,uint64_t,uint64_t);
/* Atomic caller-memory record commit: request is a complete immutable host
 * snapshot, not game code. Nonzero status leaves command count/bytes unchanged.
 * The caller has already validated guest return PC and owns its command ranges. */
typedef uint32_t (*SatchelGuestRasterRecordCommit)(void *,SatchelCPUProbe *,uint32_t,const SatchelGuestRasterCommand *);
SatchelGuestRasterRecord *satchel_guest_raster_record_create(SatchelGuestPools *,SatchelGuestTextures *,SatchelGuestRasterPrograms *,const SatchelGuestRasterQueueState *,SatchelGuestRasterRecordGuard,void *);
uint32_t satchel_guest_raster_record_apply(SatchelGuestRasterRecord *,SatchelCPUProbe *,const SatchelGuestRasterStateRequest *,SatchelGuestRasterRecordCommit,void *);
/* Snapshot after successful EndRecording; retained owner survives until handle
 * finalization. No native/backend token enters guest RAM. */
uint32_t satchel_guest_raster_record_snapshot(SatchelGuestRasterRecord *,SatchelCPUProbe *,SatchelGuestRasterRequest *);
uint32_t satchel_guest_raster_record_count(SatchelGuestRasterRecord *);
/* CPU-free, fallible ref/lease releases preserve progress. */
uint32_t satchel_guest_raster_record_destroy(SatchelGuestRasterRecord **);
#ifdef __cplusplus
}
#endif
#endif
