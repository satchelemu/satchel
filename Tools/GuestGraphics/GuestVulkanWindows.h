#ifndef SATCHEL_GUEST_VULKAN_WINDOWS_H
#define SATCHEL_GUEST_VULKAN_WINDOWS_H
#include "GuestDispatch.h"
#include "GuestVulkanTextureFrames.h"
#include "GuestWindows.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Separate opt-in composite; original captured/legacy objects are unchanged.
 * Explicit Window calls select retained VI-backed textures; the host-only snapshot
 * functions below remain separate and never imply Window/Present success.
 * Complete original captured/scalar/raster behavior is retained. Actual parent
 * DeviceFinalize children/retirement and every cached transfer submission are
 * gated until upload/readback/cleanup resources drain, including post-fence error.
 * Same serial worker as GuestDispatch; no borrowed CPU/context survives calls.
 * Lifetime-safe monotonically increasing host tickets, never guest RAM/pointers.
 */
/* VI owner dependency must outlive the active composite. During teardown hand
 * off the dispatcher first; VI try_destroy then retains live native refs until
 * CPU-free Window retirement releases them. No Process or CPU is retained.
 * A valid ticket out is cleared on failure. All APIs use one serial worker. */
int satchel_guest_dispatch_enable_vulkan_windows(SatchelGuestDispatch *,SatchelGuestVI *,uint64_t *ticket);
/* Only Window calls need this borrowed bridge; old operations can use ordinary
 * dispatch. The exact fresh common-trap/LR/context is checked before set_pc.
 * WAIT is a host park/retry requirement, never guest Acquire success.
 * A naturally aligned report is always published. Pre-dispatch refusals report
 * operation/events zero (not a cumulative event snapshot). Invalid/misaligned
 * report pointers are rejected without dereference. Request/stop are copied
 * before output publication; callbacks are borrowed, pure and non-reentrant. */
SatchelGuestDispatchStatus satchel_guest_dispatch_handle_windows(SatchelGuestDispatch *,SatchelCPUProbe *,const SatchelCPUProbeResult *,const SatchelGuestWindowBorrow *,SatchelGuestDispatchReport *);
SatchelGuestWindowStatus satchel_guest_dispatch_window_poll(uint64_t,uint32_t,int);
SatchelGuestWindowStatus satchel_guest_dispatch_window_take(uint64_t,uint64_t,SatchelFrameInfo *);
SatchelGuestWindowStatus satchel_guest_dispatch_window_copy(uint64_t,uint64_t,uint64_t,void *,size_t);
SatchelGuestWindowStatus satchel_guest_dispatch_window_release(uint64_t,uint64_t);
SatchelGuestWindowStatus satchel_guest_dispatch_window_detach(uint64_t,uint64_t);
SatchelGuestWindowStatus satchel_guest_dispatch_window_stats(uint64_t,SatchelGuestWindowStats *);
SatchelGuestTextureFrameStatus satchel_guest_dispatch_windows_texture_frame_describe(SatchelGuestDispatch *,SatchelCPUProbe *,uint64_t texture,SatchelGuestTextureFrameIdentity *);
SatchelGuestTextureFrameStatus satchel_guest_dispatch_windows_texture_frame_begin(SatchelGuestDispatch *,SatchelCPUProbe *,const SatchelGuestTextureFrameRequest *,uint64_t *ticket,SatchelFrameCancelled,void *);
SatchelGuestTextureFrameStatus satchel_guest_dispatch_windows_texture_frame_poll(uint64_t ticket,uint32_t timeout_ms,int cancel,SatchelGuestTextureFrameReport *);
SatchelGuestTextureFrameStatus satchel_guest_dispatch_windows_texture_frame_take(uint64_t ticket,uint64_t epoch,SatchelFrameInfo *);
SatchelGuestTextureFrameStatus satchel_guest_dispatch_windows_texture_frame_copy(uint64_t ticket,uint64_t frame_token,uint64_t offset,void *,size_t);
SatchelGuestTextureFrameStatus satchel_guest_dispatch_windows_texture_frame_release(uint64_t ticket,uint64_t frame_token);
SatchelGuestTextureFrameStatus satchel_guest_dispatch_windows_texture_frame_detach(uint64_t ticket,uint64_t epoch);
/* Owns cleanup; ticket remains valid on BUSY/error, invalid after OK. */
SatchelGuestTextureFrameStatus satchel_guest_dispatch_windows_texture_frame_destroy(uint64_t ticket);
#ifdef __cplusplus
}
#endif
#endif
