#ifndef SATCHEL_GUEST_VULKAN_COMPUTE_H
#define SATCHEL_GUEST_VULKAN_COMPUTE_H
#include "GuestDispatch.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Experimental explicit opt-in: exact real v2 backend, before first event.
 * Adds checked admitted scalar programs and one immutable compute recording.
 * No generic game-shader admission, mixed batches, or driver-CB substitution. */
int satchel_guest_dispatch_enable_vulkan_compute(SatchelGuestDispatch *);
#ifdef __cplusplus
}
#endif
#endif
