#ifndef SATCHEL_GUEST_CHECKED_POOL_SERVICES_H
#define SATCHEL_GUEST_CHECKED_POOL_SERVICES_H
#include "GuestPoolServices.h"
/* Additive extension; original v1 table layout/callers remain valid. Real Vulkan
 * adapters use synchronous CPU arguments for physical-backing overlap checks. */
typedef struct {
    SatchelGuestPoolServices pools;
    void *(*create_checked)(SatchelGuestPoolDeviceValidator,SatchelGuestPoolCheckedRangeGuard,void *);
    SatchelGuestPoolStatus (*unowned_range_checked)(void *,SatchelCPUProbe *,uint64_t,uint64_t);
} SatchelGuestCheckedPoolServices;
int satchel_guest_dispatch_attach_checked_pool_services(SatchelGuestDispatch *,const SatchelGuestDeviceBackendV2 *,const SatchelGuestCheckedPoolServices *);
#endif
