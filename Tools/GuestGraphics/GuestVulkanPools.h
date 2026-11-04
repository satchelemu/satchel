#ifndef SATCHEL_GUEST_VULKAN_POOLS_H
#define SATCHEL_GUEST_VULKAN_POOLS_H
#include "GuestDispatch.h"
#include "GuestPools.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Explicit trusted opt-in, before any dispatch event, after attaching the exact
 * real Vulkan v2 backend. Wrapped/fake/generic backends reject. No app enablement
 * occurs automatically. Adds only the documented cached-pool prototype profile. */
int satchel_guest_dispatch_enable_vulkan_pools(SatchelGuestDispatch *dispatch);
/* Trusted host bridge for future queue work/original tests. Fully checks guest
 * owner/generation/range and returns host-only tokens; never write them to guest
 * RAM. Serialize resolved-token use with the same stopped dispatch/CPU lifetime.
 * Revalidate each use; this is not a resource lease across guest execution. */
SatchelGuestDispatchStatus satchel_guest_dispatch_vulkan_pool_binding(SatchelGuestDispatch *dispatch,SatchelCPUProbe *cpu,
    uint64_t guest_device,uint64_t guest_gpu,uint64_t size,SatchelGuestPoolBinding *binding,uint64_t *backend_device);
#ifdef __cplusplus
}
#endif
#endif
