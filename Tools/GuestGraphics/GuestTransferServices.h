#ifndef SATCHEL_GUEST_TRANSFER_SERVICES_H
#define SATCHEL_GUEST_TRANSFER_SERVICES_H
#include "GuestCheckedPoolServices.h"
#include "GuestTransfers.h"
/* Additive private extension: the original pool table layout stays unchanged.
 * prepare_retirement owns/reaps queue jobs BEFORE generic device draining; only
 * after success may the dispatcher destroy pools and then parent devices. */
typedef struct {
    SatchelGuestCheckedPoolServices checked;
    uint32_t (*prepare_retirement)(void *);
    uint32_t (*execute_transfer)(void *,SatchelCPUProbe *,const SatchelGuestTransferRequest *,SatchelGuestTransferResult *,SatchelCPUProbeCancelled,void *);
} SatchelGuestTransferServices;
int satchel_guest_dispatch_attach_transfer_services(SatchelGuestDispatch *,const SatchelGuestDeviceBackendV2 *,const SatchelGuestTransferServices *);
#endif
