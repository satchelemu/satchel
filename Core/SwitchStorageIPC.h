#ifndef SATCHEL_SWITCH_STORAGE_IPC_H
#define SATCHEL_SWITCH_STORAGE_IPC_H
#include "SwitchStorage.h"
#include "HorizonIPC.h"
typedef enum {
    SATCHEL_STORAGE_PLAN_OK, SATCHEL_STORAGE_PLAN_INVALID, SATCHEL_STORAGE_PLAN_UNSUPPORTED,
    SATCHEL_STORAGE_PLAN_RANGE, SATCHEL_STORAGE_PLAN_LIMIT
} SatchelStoragePlanStatus;
typedef struct {
    uint32_t command_id;
    uint64_t offset, count, output_address, output_capacity;
} SatchelStoragePlan;
/* Pure schema validation after a successful bounded HIPC parse. Only read-only
 * IStorage Read0/GetSize4, session or domain CMIF, with no transferred handles.
 * The kernel must resolve the session/domain object and validate the entire
 * advertised output mapping's WRITE permissions before calling a provider.
 * Reply and data then require one atomic checked guest-write transaction; this
 * helper performs no guest-memory access, replies or object/handle mutations.
 * NonSecure/normal mapped outputs are accepted; no pointer/NonDevice buffers.
 * Failure leaves plan unchanged. Provider size must fit Horizon's signed size. */
SatchelStoragePlanStatus satchel_switch_storage_plan(const SatchelHIPCRequest *request,
    uint64_t storage_size, SatchelStoragePlan *plan);
/* Host staging only, never pass a guest address as output. Calls the verified
 * provider into private staging and copies only on success. Invalid plans,
 * capacity failures, provider failures and allocation failure preserve output.
 * Zero-byte reads still consult the provider for cancellation/closed backing.
 * Returns storage status, not a fabricated Horizon filesystem result. */
SatchelSwitchStorageStatus satchel_switch_storage_fetch(const SatchelSwitchStorage *storage,
    const SatchelStoragePlan *plan, void *output, size_t capacity);
int satchel_switch_storage_size_bytes(uint64_t storage_size, uint8_t output[8]);
#endif
