#ifndef SATCHEL_GUEST_VULKAN_TEXTURE_FRAMES_H
#define SATCHEL_GUEST_VULKAN_TEXTURE_FRAMES_H
#include "GuestDispatch.h"
#include "GuestTextureFrames.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {uint32_t version,size;uint64_t texture,texture_generation,guest_device;} SatchelGuestTextureFrameIdentity;
/* Separate opt-in composite; original captured/legacy objects are unchanged.
 * Snapshot selection is trusted host-only, never a guest Window/Present success.
 * Complete original captured/scalar/raster behavior is retained. Actual parent
 * DeviceFinalize children/retirement and every cached transfer submission are
 * gated until upload/readback/cleanup resources drain, including post-fence error.
 * Same serial worker as GuestDispatch; no borrowed CPU/context survives calls.
 * Lifetime-safe monotonically increasing host tickets, never guest RAM/pointers.
 */
int satchel_guest_dispatch_enable_vulkan_texture_frames(SatchelGuestDispatch *);
SatchelGuestTextureFrameStatus satchel_guest_dispatch_texture_frame_describe(SatchelGuestDispatch *,SatchelCPUProbe *,uint64_t texture,SatchelGuestTextureFrameIdentity *);
SatchelGuestTextureFrameStatus satchel_guest_dispatch_texture_frame_begin(SatchelGuestDispatch *,SatchelCPUProbe *,const SatchelGuestTextureFrameRequest *,uint64_t *ticket,SatchelFrameCancelled,void *);
SatchelGuestTextureFrameStatus satchel_guest_dispatch_texture_frame_poll(uint64_t ticket,uint32_t timeout_ms,int cancel,SatchelGuestTextureFrameReport *);
SatchelGuestTextureFrameStatus satchel_guest_dispatch_texture_frame_take(uint64_t ticket,uint64_t epoch,SatchelFrameInfo *);
SatchelGuestTextureFrameStatus satchel_guest_dispatch_texture_frame_copy(uint64_t ticket,uint64_t frame_token,uint64_t offset,void *,size_t);
SatchelGuestTextureFrameStatus satchel_guest_dispatch_texture_frame_release(uint64_t ticket,uint64_t frame_token);
SatchelGuestTextureFrameStatus satchel_guest_dispatch_texture_frame_detach(uint64_t ticket,uint64_t epoch);
/* Owns cleanup; ticket remains valid on BUSY/error, invalid after OK. */
SatchelGuestTextureFrameStatus satchel_guest_dispatch_texture_frame_destroy(uint64_t ticket);
#ifdef __cplusplus
}
#endif
#endif
