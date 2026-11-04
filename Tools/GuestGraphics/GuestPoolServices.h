#ifndef SATCHEL_GUEST_POOL_SERVICES_H
#define SATCHEL_GUEST_POOL_SERVICES_H
/* Private trusted host extension contract; never a guest callback table.
 * Generic GuestDispatch links no Vulkan or pool implementation directly. */
#include "GuestDispatch.h"
#include "GuestPools.h"
typedef struct {
    uint32_t version;
    void *(*create)(SatchelGuestPoolDeviceValidator,SatchelGuestPoolRangeGuard,void *);
    SatchelGuestPoolStatus (*destroy)(void **);
    SatchelGuestPoolStatus (*execute)(void *,SatchelCPUProbe *,const SatchelGuestPoolRequest *,SatchelGuestPoolResult *);
    SatchelGuestPoolStatus (*resolve)(void *,SatchelCPUProbe *,uint64_t,uint64_t,uint64_t,SatchelGuestPoolBinding *);
    SatchelGuestPoolStatus (*children)(void *,uint64_t,uint32_t *);
    SatchelGuestPoolStatus (*unowned_range)(void *,uint64_t,uint64_t);
} SatchelGuestPoolServices;
/* Compares the attached v2 device callbacks/context/drain against the supplied
 * expected real factory. Only the dedicated Vulkan opt-in helper calls this.
 * Copies the table; service owner context is this dispatch, with no CPU borrow.
 * Attach before the first event; failure leaves existing owner unchanged. */
int satchel_guest_dispatch_attach_pool_services(SatchelGuestDispatch *,const SatchelGuestDeviceBackendV2 *,const SatchelGuestPoolServices *);
SatchelGuestDispatchStatus satchel_guest_dispatch_resolve_pool(SatchelGuestDispatch *,SatchelCPUProbe *,uint64_t,uint64_t,uint64_t,
    SatchelGuestPoolBinding *,uint64_t *backend_device);
#endif
