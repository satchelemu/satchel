#ifndef SATCHEL_GUEST_VULKAN_CAPTURED_RASTER_H
#define SATCHEL_GUEST_VULKAN_CAPTURED_RASTER_H
#include "GuestDispatch.h"
#include "GuestVulkanRaster.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Explicit additive opt-in: preserves legacy scalar compute/raster/transfer
 * paths and adds bounded paired-source admission for the captured flat profile.
 * ProgramSetShaders success means source admitted, never compiled. Real queue
 * state is insufficient: binding a captured profile returns the distinct
 * SHADER_STATE_UNAVAILABLE status before recording or compiling. No default
 * provoking/YNegate state is substituted. Profile replacement requires common
 * ProgramFinalize/Initialize; captured-to-captured replacement is transactional.
 * Captures and pending cleanup retire before common objects/pools/devices. */
int satchel_guest_dispatch_enable_vulkan_captured_raster(SatchelGuestDispatch *);
#ifdef __cplusplus
}
#endif
#endif
