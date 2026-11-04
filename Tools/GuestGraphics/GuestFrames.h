#ifndef SATCHEL_GUEST_FRAMES_H
#define SATCHEL_GUEST_FRAMES_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Host-only immutable image ownership. No guest native-window/VI implementation,
 * UIKit pointers, borrowed CPU mappings or displayed timestamp claims. */
enum {SATCHEL_FRAME_OWNER_LIMIT=4,SATCHEL_FRAME_CAPACITY=3,SATCHEL_FRAME_MAX_BYTES=8*1024*1024,
 SATCHEL_FRAME_TOTAL_BYTES=24*1024*1024,SATCHEL_FRAME_POLL_LIMIT_MS=10,
 SATCHEL_FRAME_RGBA8_UNORM=1,SATCHEL_FRAME_LINEAR_SRGB=1,SATCHEL_FRAME_TOP_LEFT=1};
typedef enum {SATCHEL_FRAME_OK=0,SATCHEL_FRAME_INVALID,SATCHEL_FRAME_BUSY,SATCHEL_FRAME_LIMIT,
 SATCHEL_FRAME_CANCELLED,SATCHEL_FRAME_BACKEND,SATCHEL_FRAME_ALLOCATION,SATCHEL_FRAME_PENDING,
 SATCHEL_FRAME_NOT_READY,SATCHEL_FRAME_NO_SURFACE,SATCHEL_FRAME_UNSUPPORTED} SatchelFrameStatus;
typedef enum {SATCHEL_FRAME_SNAPSHOTTING=1,SATCHEL_FRAME_READY,SATCHEL_FRAME_LEASED,SATCHEL_FRAME_RETIRING} SatchelFrameState;
typedef struct SatchelGuestFrames SatchelGuestFrames;
typedef int (*SatchelFrameCancelled)(void *);
typedef struct {uint32_t version,size;uint64_t epoch,device,target;
 uint32_t format,color_space,origin;} SatchelFrameSource;
typedef struct {uint32_t version,size,state,width,height,row_stride,format,color_space,origin;
 uint64_t epoch,token,byte_size;} SatchelFrameInfo;
typedef struct {uint32_t owners,frames,snapshotting,ready,leased,retiring;
 uint64_t bytes,byte_limit;} SatchelFrameStats;
/* All functions use a bounded global try-gate: concurrent/reentrant calls return
 * BUSY, never wait for another caller. Caller owns each owner pointer exclusively
 * at destruction. Main/UI calls may take/copy/release/detach/stats; only graphics
 * worker calls begin/poll/collect/try_destroy. Pure cancellation callbacks only. */
/* A valid out pointer is cleared on every failure, including BUSY. */
SatchelFrameStatus satchel_guest_frames_create(uint64_t epoch,SatchelGuestFrames **);
/* New epoch must strictly increase; old pending frames retire, leased frames
 * remain valid until release. An epoch never identifies a native host pointer. */
SatchelFrameStatus satchel_guest_frames_attach(SatchelGuestFrames *,uint64_t new_epoch);
SatchelFrameStatus satchel_guest_frames_detach(SatchelGuestFrames *,uint64_t epoch);
/* Explicit full-target linear-sRGB/top-left profile only. The trusted caller
 * must derive color/origin from its owned renderer state, not assume UNORM means
 * sRGB. Dimensions/stride/size derive from the real initialized target. Source
 * target generation is checked by backend token lookup and retained by the real
 * snapshot job until fence reap; no caller texture pointer is retained.
 * Reserve CPU bytes and frame capacity before allocation/submission. Every
 * nonzero output ticket remains owned EVEN ON ERROR; poll/collect must retain it.
 * Valid ticket output is cleared before every failure, including INVALID/BUSY;
 * the source value is copied before touching a possibly overlapping ticket.
 * Parent device must outlive all this owner's backend resources. */
SatchelFrameStatus satchel_guest_frames_begin(SatchelGuestFrames *,const SatchelFrameSource *,uint64_t *ticket,SatchelFrameCancelled,void *);
/* Wait <=10ms. Cancellation, timeout, readback/destroy error retain resources.
 * READY means immutable bytes copied after a real fence, never displayed. */
SatchelFrameStatus satchel_guest_frames_poll(SatchelGuestFrames *,uint64_t ticket,uint32_t timeout_ms,SatchelFrameCancelled,void *);
/* Take in submission-ticket order, never skip an earlier pending snapshot.
 * Multiple leases may coexist; the UI owns its display/copy ordering. Failure
 * preserves out. Copies allocate no owner memory: destination storage and any
 * UI image copies are caller-budgeted, outside the owner's 24 MiB cap. */
SatchelFrameStatus satchel_guest_frames_take(SatchelGuestFrames *,uint64_t epoch,SatchelFrameInfo *);
SatchelFrameStatus satchel_guest_frames_copy(SatchelGuestFrames *,uint64_t ticket,uint64_t offset,void *destination,size_t size);
SatchelFrameStatus satchel_guest_frames_release(SatchelGuestFrames *,uint64_t ticket);
/* Detach/destroy do not discard GPU jobs or leased UI bytes. Collect polls only
 * retiring jobs (zero timeout/no expired callback); retry while PENDING/BUSY.
 * try_destroy marks retiring and clears the pointer only when completely empty. */
SatchelFrameStatus satchel_guest_frames_collect(SatchelGuestFrames *);
SatchelFrameStatus satchel_guest_frames_try_destroy(SatchelGuestFrames **);
SatchelFrameStatus satchel_guest_frames_stats(SatchelFrameStats *);
#ifdef __cplusplus
}
#endif
#endif
