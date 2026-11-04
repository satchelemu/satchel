#ifndef SATCHEL_GUEST_WINDOWS_H
#define SATCHEL_GUEST_WINDOWS_H
#include "GuestTransfers.h"
#include "GuestTextureFrames.h"
#include "GuestVI.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {SATCHEL_GWIN_BUILDER_BYTES=64,SATCHEL_GWIN_OBJECT_BYTES=384,SATCHEL_GWIN_TEXTURES=3,SATCHEL_GWIN_OWNERS=4};
typedef enum {SATCHEL_GWIN_OK=0,SATCHEL_GWIN_INVALID,SATCHEL_GWIN_MEMORY,SATCHEL_GWIN_BAD_OBJECT,SATCHEL_GWIN_BUSY,
 SATCHEL_GWIN_WAIT,SATCHEL_GWIN_UNSUPPORTED,SATCHEL_GWIN_UNSUPPORTED_INTERVAL,SATCHEL_GWIN_CANCELLED,
 SATCHEL_GWIN_LIMIT,SATCHEL_GWIN_ALLOCATION,SATCHEL_GWIN_BACKEND,SATCHEL_GWIN_NO_SURFACE} SatchelGuestWindowStatus;
typedef enum {SATCHEL_GWIN_DEFAULTS=1,SATCHEL_GWIN_DEVICE,SATCHEL_GWIN_NATIVE,SATCHEL_GWIN_TEXTURE_LIST,
 SATCHEL_GWIN_INTERVAL,SATCHEL_GWIN_BUILDER_GET_NATIVE,SATCHEL_GWIN_BUILDER_GET_INTERVAL,
 SATCHEL_GWIN_INITIALIZE,SATCHEL_GWIN_FINALIZE,SATCHEL_GWIN_GET_NATIVE,SATCHEL_GWIN_GET_INTERVAL,
 SATCHEL_GWIN_SET_INTERVAL,SATCHEL_GWIN_SET_CROP,SATCHEL_GWIN_GET_CROP,SATCHEL_GWIN_QUEUE_ACQUIRE,
 SATCHEL_GWIN_QUEUE_PRESENT,SATCHEL_GWIN_SYNC_ACQUIRE} SatchelGuestWindowOperation;
typedef struct {uint32_t operation;uint64_t x[6];} SatchelGuestWindowRequest;
typedef struct {uint32_t status,set_value,waiting,accepted;uint64_t value;} SatchelGuestWindowResult;
/* Explicit emulator surface policy, not proof of a vendor NVN interval/default.
 * Only a caller-reviewed mapping may supply this. A missing/refused mapping is
 * UNSUPPORTED_INTERVAL. The callback is pure/borrowed and cannot execute guests. */
typedef struct {uint32_t version,size,format,color_space,origin;uint64_t epoch,policy_generation;} SatchelGuestWindowPresentation;
typedef int (*SatchelGuestWindowPolicy)(void *,const SatchelGuestVIInfo *,int32_t,uint32_t,uint32_t,SatchelGuestWindowPresentation *);
typedef int (*SatchelGuestWindowContextCheck)(void *,SatchelCPUProbe *,uint64_t process_id,uint64_t context_generation);
typedef struct {SatchelCPUProbe *cpu;SatchelGuestVIBorrow vi;uint64_t process_id,context_generation,trap_pc,return_pc;
 SatchelGuestWindowContextCheck validate_context;void *context;
 SatchelGuestWindowPolicy policy;void *policy_context;} SatchelGuestWindowBorrow;
typedef struct SatchelGuestWindows SatchelGuestWindows;
/* Dependencies and trusted device/range context independently outlive this owner.
 * No CPU/Process/cancellation borrow is retained. One Window and three snapshots
 * per owner; shared texture/frame budget remains authoritative. */
SatchelGuestWindowStatus satchel_guest_windows_create(SatchelGuestPools *,SatchelGuestTextures *,SatchelGuestTransfers *,SatchelGuestVI *,
 SatchelGuestPoolDeviceValidator,SatchelGuestPoolCheckedRangeGuard,void *,SatchelGuestWindows **);
/* Internal model called only after adapter LR validation, under one serial
 * composite transaction. It writes no result register. Every host refusal is
 * explicit, never fabricated native-error. WAIT retains only copied numeric
 * request/context and actual owned ordering lease; caller parks/retries exact
 * context, revalidates LR/output, or cancels. Frame count is not display count. */
SatchelGuestWindowStatus satchel_guest_windows_execute(SatchelGuestWindows *,const SatchelGuestWindowBorrow *,const SatchelGuestWindowRequest *,SatchelGuestWindowResult *);
/* timeout_ms caps requested fence wait, not synchronous driver/publication time.
 * The context validator attests exact current live identity only. The adapter
 * independently checks a fresh stop/PC/LR; the pure cancellation callback must
 * also report asynchronous surface-epoch invalidation. */
SatchelGuestWindowStatus satchel_guest_windows_poll(SatchelGuestWindows *,uint32_t timeout_ms,int cancel);
SatchelGuestWindowStatus satchel_guest_windows_take(SatchelGuestWindows *,uint64_t epoch,SatchelFrameInfo *);
SatchelGuestWindowStatus satchel_guest_windows_copy(SatchelGuestWindows *,uint64_t frame_token,uint64_t offset,void *,size_t);
SatchelGuestWindowStatus satchel_guest_windows_release(SatchelGuestWindows *,uint64_t frame_token);
SatchelGuestWindowStatus satchel_guest_windows_detach(SatchelGuestWindows *,uint64_t epoch);
SatchelGuestWindowStatus satchel_guest_windows_try_destroy(SatchelGuestWindows **);
/* Pure range callback for reciprocal guards; returns boolean success/disjoint. */
int satchel_guest_windows_guard(const SatchelGuestWindows *,SatchelCPUProbe *,uint64_t,uint64_t);
SatchelGuestWindowStatus satchel_guest_windows_device_children(SatchelGuestWindows *,uint64_t,uint32_t *);
typedef struct {uint32_t windows,textures,pending_calls,snapshots,leased,retiring,gpu_snapshots,pending_orders;
 uint64_t acquires,presents,snapshots_completed,cancelled; } SatchelGuestWindowStats;
SatchelGuestWindowStatus satchel_guest_windows_stats(SatchelGuestWindows *,SatchelGuestWindowStats *);
#ifdef __cplusplus
}
#endif
#endif
