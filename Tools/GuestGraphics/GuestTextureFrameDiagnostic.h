#ifndef SATCHEL_GUEST_TEXTURE_FRAME_DIAGNOSTIC_H
#define SATCHEL_GUEST_TEXTURE_FRAME_DIAGNOSTIC_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef int (*SatchelGuestTextureFrameDiagnosticCancelled)(void *);
enum {SATCHEL_GUEST_TFD_OK,SATCHEL_GUEST_TFD_FAILED,SATCHEL_GUEST_TFD_CANCELLED,SATCHEL_GUEST_TFD_BUSY,SATCHEL_GUEST_TFD_INVALID};
typedef struct {
 uint32_t devices,buffers,jobs,targets,pipelines,compute_pipelines,index_buffers;
 uint32_t frame_owners,frames,frame_snapshotting,frame_ready,frame_leased,frame_retiring;
 uint32_t snapshot_owners,snapshot_gpu_owners,reserved_contexts,active_contexts,retiring_contexts,pending_devices,reserved;
 uint64_t buffer_bytes,target_bytes,index_bytes,frame_bytes,staging_bytes;
} SatchelGuestTextureFrameResources;
typedef struct {
 uint32_t version,size,status,stage,dispatch_status,frame_status,cpu_stop_reason,retirement_status;
 uint64_t instructions,dispatch_events;
 uint32_t transfers_submitted,transfer_finishes,snapshots_started,uploads_completed,snapshots_completed;
 uint32_t pre_submit_cancellations,pending_cancellations,cancelled_snapshots_drained;
 uint32_t width,height,row_stride,values_matched,guards_matched,cpu_mirror_unchanged,state_preserved;
 uint32_t parent_protected,texture_finalized,device_finalized,cpu_destroyed,cpu_free_lease,cleanup_matched,resources_retained,prior_reaped;
 SatchelGuestTextureFrameResources baseline,final;
} SatchelGuestTextureFrameDiagnostic;
/* Original interpreted NVN device/pools/texture/queue -> real GPU ClearBuffer ->
 * current tiled mirror -> actual upload/fence -> immutable frame. One31x17 RGBA8
 * texture,2x64KiB pools,12KiB queue memory; original caller/stubs only. Explicit
 * fixture color/origin state, not imported game/window/acquire/display support.
 * Success: enumOK/stage10,transfer1/finish1,snapshots_started2,uploads2,
 * snapshots_completed1,pre_cancel1,pending_cancel1,discard_drained1; all evidence
 * booleans true and full baseline/final equality with no retained resources.
 *
 * Serial graphics worker; pure callback/context borrowed only. Cooperative10s
 * latched deadline, each frame phase<=2.5s with individual waits<=1ms; native
 * driver calls cannot be forcibly interrupted. No raw tickets in reports.
 * CPU is always destroyed before return. A static CPU-free owner retains any
 * nonzero snapshot/lease/dispatch retirement until drain; always-cancel calls
 * retry cleanup only. BUSY with resources_retained0 is not diagnostic success.
 * Invalid/misaligned out and reentry BUSY preserve output. Enum return, not bool.
 * No fault seams/test CPU exports in the production archive.
 */
uint32_t satchel_guest_texture_frame_diagnostic(SatchelGuestTextureFrameDiagnostic *,SatchelGuestTextureFrameDiagnosticCancelled,void *);
#ifdef __cplusplus
}
#endif
#endif
