#ifndef SATCHEL_GUEST_TEXTURE_FRAMES_H
#define SATCHEL_GUEST_TEXTURE_FRAMES_H
#include "GuestTextures.h"
#include "GuestFrames.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {SATCHEL_GTF_LIMIT=3,SATCHEL_GTF_STAGING_PER_OWNER=24*1024*1024,SATCHEL_GTF_STAGING_TOTAL=48*1024*1024};
typedef enum {SATCHEL_GTF_OK=0,SATCHEL_GTF_INVALID,SATCHEL_GTF_UNSUPPORTED,SATCHEL_GTF_CHANGED,SATCHEL_GTF_BUSY,SATCHEL_GTF_LIMIT_REACHED,SATCHEL_GTF_MEMORY,SATCHEL_GTF_ALLOCATION,SATCHEL_GTF_BACKEND,SATCHEL_GTF_CANCELLED,SATCHEL_GTF_NOT_READY} SatchelGuestTextureFrameStatus;
typedef enum {SATCHEL_GTF_PREPARING=1,SATCHEL_GTF_UPLOADING,SATCHEL_GTF_SNAPSHOTTING,SATCHEL_GTF_READY,SATCHEL_GTF_RETIRING} SatchelGuestTextureFramePhase;
typedef struct SatchelGuestTextureFrames SatchelGuestTextureFrames;
typedef struct {uint32_t version,size;uint64_t texture,texture_generation,epoch;uint32_t format,color_space,origin;} SatchelGuestTextureFrameRequest;
typedef struct {uint32_t status,phase,upload_completed,snapshot_completed,gpu_resources_retained,frame_leased,retiring;uint64_t epoch,staging_bytes;} SatchelGuestTextureFrameReport;
typedef struct {uint32_t owners,owner_limit,gpu_owners;uint64_t staging_bytes,staging_limit;} SatchelGuestTextureFrameStats;
/* One explicitly selected texture -> one immutable host frame, NOT Window/VI or
 * presentation. Actual checked texture/pool generation and current GPU-visible
 * bytes are retained/copied, detiled and uploaded before the frame LOAD snapshot.
 * No zero/stale VkImage fallback. Color/origin facts are trusted explicit inputs;
 * callers must derive them from their owned state, not guess from UNORM.
 *
 * Serial composite/graphics worker contract: pools,textures and parent device
 * outlive ALL retained GPU resources. check_submit must gate cached legacy
 * bindings too; pool holds alone cannot revoke a previously copied host binding.
 * DeviceFinalize must consult device_children in addition to its existing owners.
 * CPU and pure cancellation callback/context are borrowed only during begin.
 * No guest execution/owner mutation/GPU work from callbacks. Reentry returns BUSY.
 * Every nonnull output owner remains caller-owned EVEN ON ERROR; poll/try_destroy
 * drain it. Valid out is cleared on all failures, including BUSY. Request is
 * copied before clearing possibly overlapping out. Maximum frame8MiB, tile16MiB,
 * separately charged staging<=24MiB each/48MiB global before allocations.
 */
SatchelGuestTextureFrameStatus satchel_guest_texture_frames_begin(SatchelGuestPools *,SatchelGuestTextures *,SatchelCPUProbe *,const SatchelGuestTextureFrameRequest *,SatchelGuestTextureFrames **,SatchelFrameCancelled,void *);
/* CPU-free; timeout<=10ms. cancel latches retirement, never fake completion.
 * Upload/readback errors keep nonzero jobs/resources until real fence/cleanup.
 * On success READY means bytes, not display. Poll never frees the owner pointer. */
SatchelGuestTextureFrameStatus satchel_guest_texture_frames_poll(SatchelGuestTextureFrames *,uint32_t timeout_ms,int cancel,SatchelGuestTextureFrameReport *);
SatchelGuestTextureFrameStatus satchel_guest_texture_frames_peek(SatchelGuestTextureFrames *,SatchelGuestTextureFrameReport *);
SatchelGuestTextureFrameStatus satchel_guest_texture_frames_take(SatchelGuestTextureFrames *,uint64_t epoch,SatchelFrameInfo *);
SatchelGuestTextureFrameStatus satchel_guest_texture_frames_copy(SatchelGuestTextureFrames *,uint64_t token,uint64_t offset,void *,size_t);
SatchelGuestTextureFrameStatus satchel_guest_texture_frames_release(SatchelGuestTextureFrames *,uint64_t token);
/* Exact current epoch only. Retires upload/snapshot, preserves already leased
 * immutable bytes. New surface needs a new owner; epochs are never rewritten. */
SatchelGuestTextureFrameStatus satchel_guest_texture_frames_detach(SatchelGuestTextureFrames *,uint64_t epoch);
/* Marks retiring, progresses zero-time CPU-free cleanup and clears *owner only
 * after all GPU resources and UI leases are gone. Repeat on BUSY/error. */
SatchelGuestTextureFrameStatus satchel_guest_texture_frames_try_destroy(SatchelGuestTextureFrames **);
SatchelGuestTextureFrameStatus satchel_guest_texture_frames_device_children(uint64_t guest_device,uint64_t backend_device,uint32_t *);
SatchelGuestTextureFrameStatus satchel_guest_texture_frames_check_submit(uint64_t backend_device);
SatchelGuestTextureFrameStatus satchel_guest_texture_frames_stats(SatchelGuestTextureFrameStats *);
#ifdef __cplusplus
}
#endif
#endif
