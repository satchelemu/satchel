#ifndef SATCHEL_GUEST_DISPATCH_H
#define SATCHEL_GUEST_DISPATCH_H
#include "CPUProbe.h"
#include "GuestDeviceBackend.h"
#ifdef __cplusplus
extern "C" {
#endif
enum { SATCHEL_GUEST_DISPATCH_NAME_LIMIT=127, SATCHEL_GUEST_DISPATCH_BUILDER_SIZE=64, SATCHEL_GUEST_DISPATCH_DEVICE_SIZE=0x3000,
       SATCHEL_GUEST_DISPATCH_EVENT_LIMIT=100000, SATCHEL_GUEST_DISPATCH_TIMEOUT_LIMIT_MS=30000 };
typedef enum { SATCHEL_GUEST_DISPATCH_RETURNED=0, SATCHEL_GUEST_DISPATCH_INVALID,
    SATCHEL_GUEST_DISPATCH_NOT_OURS, SATCHEL_GUEST_DISPATCH_UNKNOWN_NAME, SATCHEL_GUEST_DISPATCH_UNIMPLEMENTED,
    SATCHEL_GUEST_DISPATCH_MEMORY_FAULT, SATCHEL_GUEST_DISPATCH_BAD_OBJECT, SATCHEL_GUEST_DISPATCH_BAD_RETURN,
    SATCHEL_GUEST_DISPATCH_LIMIT, SATCHEL_GUEST_DISPATCH_CANCELLED, SATCHEL_GUEST_DISPATCH_INTERNAL,SATCHEL_GUEST_DISPATCH_BACKEND_UNAVAILABLE,
    SATCHEL_GUEST_DISPATCH_UNSUPPORTED_FLAGS,SATCHEL_GUEST_DISPATCH_DEVICE_LIMIT,SATCHEL_GUEST_DISPATCH_BACKEND_FAILURE,SATCHEL_GUEST_DISPATCH_UNSUPPORTED_QUERY,
    SATCHEL_GUEST_DISPATCH_RESOURCE_BUSY,SATCHEL_GUEST_DISPATCH_UNSUPPORTED_POOL_PROFILE,SATCHEL_GUEST_DISPATCH_RESOURCE_LIMIT,SATCHEL_GUEST_DISPATCH_SHADER_STATE_UNAVAILABLE,SATCHEL_GUEST_DISPATCH_WINDOW_WAIT,SATCHEL_GUEST_DISPATCH_SURFACE_UNAVAILABLE,SATCHEL_GUEST_DISPATCH_PRESENT_TIMING_UNAVAILABLE } SatchelGuestDispatchStatus;
typedef struct { uint32_t region_size,trap_offset,first_stub_offset,stub_stride,stub_count; } SatchelGuestDispatchLayout;
typedef struct { uint32_t status,operation_id;uint64_t events; } SatchelGuestDispatchReport;
typedef struct SatchelGuestDispatch SatchelGuestDispatch;
/* Position-independent original guest ARM64 data: common BRK guard plus
 * MOVZ X16,#id; B commonTrap slots. No host executable allocation. Caller maps
 * these bytes RX once through the process constructor; never patch game NSOs.
 * Query layout with bytes=NULL/capacity=0. Failure leaves bytes unchanged. */
int satchel_guest_dispatch_build_stubs(void *bytes,size_t capacity,SatchelGuestDispatchLayout *layout);
/* Guest addresses must match actual RX mappings; registration checks them.
 * Deadlines use monotonic elapsed time, including time spent running guest CPU.
 * Each intercepted attempt consumes an event, even when zero CPU steps pass.
 * Each CPU/context must have exclusive caller lifetime. Owner APIs serialize
 * across workers; callbacks remain non-reentrant and must not wait for another
 * thread entering an owner API while this handler holds its lock. */
SatchelGuestDispatch *satchel_guest_dispatch_create(uint64_t bootstrap_address,uint64_t region_address,
    uint32_t max_events,uint32_t timeout_ms);
/* Compatibility wrapper: transfers into a pre-reserved retirement owner and
 * polls once; never drops a device whose backend destroy returned failure. */
void satchel_guest_dispatch_destroy(SatchelGuestDispatch *dispatch);
enum { SATCHEL_GUEST_RETIREMENT_LIMIT=4 };
typedef struct {uint32_t reserved_contexts,active_contexts,retiring_contexts,pending_devices;
    uint32_t released_contexts,released_devices,backend_failures,last_backend_status;} SatchelGuestRetirementReport;
typedef enum {SATCHEL_GUEST_RETIRE_IDLE=0,SATCHEL_GUEST_RETIRE_PENDING,SATCHEL_GUEST_RETIRE_CANCELLED,SATCHEL_GUEST_RETIRE_INVALID,SATCHEL_GUEST_RETIRE_BUSY} SatchelGuestRetirementStatus;
/* Explicit ownership transfer. On1, caller pointer is cleared and either freed
 * or held by the bounded retirement owner. On0 it remains caller-owned.
 * Backend attachment reserves a slot before creating devices, so retire cannot
 * fail because the retirement queue filled afterward. No guest RAM is touched. */
int satchel_guest_dispatch_retire(SatchelGuestDispatch **owner);
/* Worker teardown handoff: waits for another worker's active handler/poll to
 * leave the process-wide owner lock, then transfers without running backend
 * cleanup. A valid exclusively owned pointer outside a callback is cleared and
 * accepted; concurrent activity does not cause a busy refusal. Invalid or
 * same-thread callback reentry returns0 and preserves the caller pointer.
 * Never call on UI thread or use this to free a CPU still executing its own
 * handler. The accepted retired context has no CPU/provider dependency. */
int satchel_guest_dispatch_handoff(SatchelGuestDispatch **owner);

SatchelGuestRetirementStatus satchel_guest_dispatch_retirement_poll(SatchelCPUProbeCancelled cancelled,
    void *context,SatchelGuestRetirementReport *report);
int satchel_guest_dispatch_retirement_stats(SatchelGuestRetirementReport *report);

/* Optional trusted host backend, attach once before any dispatch event. Copies
 * callback table; context must independently outlive final retirement and must
 * not borrow a CPU/provider/cancellation object. Devices remain owned here and
 * are retained until teardown succeeds. Attachment reserves one of4 global
 * active/retiring owner slots; failure leaves the table/context unchanged.
 * Version1 supports flags0 only. Never guest-callable. Serialized worker only. */
int satchel_guest_dispatch_set_device_backend(SatchelGuestDispatch *dispatch,const SatchelGuestDeviceBackend *backend);
int satchel_guest_dispatch_set_device_backend_v2(SatchelGuestDispatch *dispatch,const SatchelGuestDeviceBackendV2 *backend);
uint32_t satchel_guest_dispatch_device_count(const SatchelGuestDispatch *dispatch);
int satchel_guest_dispatch_device_capabilities(const SatchelGuestDispatch *dispatch,uint64_t guest_address,SatchelGuestDeviceCaps *caps);
/* Returns two addresses to MERGE with other breakpoint owners, not replace
 * their registrations blindly. Check both with CPU set_breakpoints. */
int satchel_guest_dispatch_breakpoints(const SatchelGuestDispatch *dispatch,uint64_t addresses[2]);
/* Requires the fresh CPU BREAKPOINT result from this same stopped CPU.
 * No native callbacks or default-success NVN operations. Unknown names and
 * known-but-unimplemented operations leave PC/registers/memory unchanged.
 * Implements name lookup/GetProcAddress,64-byte private tagged builders, and
 * optional real backend DeviceInitialize/Finalize with0x3000-byte private device
 * objects. DeviceGetInteger supports only the seven real compute-limit queries
 * 0x1c..0x22; all others stop UNSUPPORTED_QUERY without changing output.
 * Only opaque generation IDs enter guest memory, never host pointers.
 * On commit, checked LR validation precedes output changes; failed atomic
 * guest write restores original trap PC. Reads/writes use CPU checked RAM API.
 * Stop immediately on any status other than RETURNED; never loop it as success. */
SatchelGuestDispatchStatus satchel_guest_dispatch_handle(SatchelGuestDispatch *dispatch,SatchelCPUProbe *cpu,
    const SatchelCPUProbeResult *stop,SatchelCPUProbeCancelled cancelled,void *cancel_context,
    SatchelGuestDispatchReport *report);
/* Borrowed checked output guard for other HLE owners on the same stopped
 * process CPU. Requires a live exclusively owned dispatch, not a retired/stale
 * pointer. Full writable span must be physically disjoint from live/provisional
 * device objects and all resources in the attached checked pool service.
 * Legacy services without physical-range checking refuse. No guest write,
 * event/deadline change, allocation or retained CPU/callback. This is an instant
 * validation, not a lease; serialize subsequent output commit with dispatch.
 * Call only outside dispatch/backend callbacks; reentry/concurrent work refuses.
 * Private transient builder bytes have no registry ownership to attest here. */
int satchel_guest_dispatch_check_output_range(SatchelGuestDispatch *,SatchelCPUProbe *,uint64_t,size_t);
/* Public immutable name metadata only. ID0 means direct bootstrap lookup. */
const char *satchel_guest_dispatch_name(uint32_t operation_id);
#ifdef __cplusplus
}
#endif
#endif
