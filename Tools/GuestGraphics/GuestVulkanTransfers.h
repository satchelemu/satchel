#ifndef SATCHEL_GUEST_VULKAN_TRANSFERS_H
#define SATCHEL_GUEST_VULKAN_TRANSFERS_H
#include "GuestDispatch.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Separate explicit opt-in before events, exact real Vulkan v2 backend only.
 * Includes cached pools and the bounded caller-memory transfer-only profile.
 * Existing pool-only opt-in never enables queues. No implicit App enablement. */
int satchel_guest_dispatch_enable_vulkan_transfers(SatchelGuestDispatch *);
#ifdef __cplusplus
}
#endif
#endif
