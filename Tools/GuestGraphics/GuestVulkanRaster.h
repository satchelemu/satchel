#ifndef SATCHEL_GUEST_VULKAN_RASTER_H
#define SATCHEL_GUEST_VULKAN_RASTER_H
#include "GuestDispatch.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Explicit trusted real-Vulkan opt-in. Adds bounded original-code scalar raster
 * pairs, checked RGBA8 textures and retained ordered clear/draw queues to the
 * existing compute/transfer owner. Unknown/mixed profiles remain unsupported. */
int satchel_guest_dispatch_enable_vulkan_raster(SatchelGuestDispatch *);
#ifdef __cplusplus
}
#endif
#endif
