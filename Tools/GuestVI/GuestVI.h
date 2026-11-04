#ifndef SATCHEL_GUEST_VI_H
#define SATCHEL_GUEST_VI_H
#include "CPUProbe.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {SATCHEL_GVI_OWNER_LIMIT=4,SATCHEL_GVI_REFERENCE_LIMIT=32,SATCHEL_GVI_IDENTITY_BYTES=4096,
 SATCHEL_GVI_IDENTITY_STRIDE=16,SATCHEL_GVI_EVENT_LIMIT=100000,SATCHEL_GVI_TIMEOUT_LIMIT_MS=30000};
typedef enum {SATCHEL_GVI_OK=0,SATCHEL_GVI_INVALID,SATCHEL_GVI_BUSY,SATCHEL_GVI_BAD_OBJECT,
 SATCHEL_GVI_MEMORY,SATCHEL_GVI_BAD_RETURN,SATCHEL_GVI_UNSUPPORTED,SATCHEL_GVI_CANCELLED,
 SATCHEL_GVI_LIMIT,SATCHEL_GVI_RESERVATION,SATCHEL_GVI_NO_SURFACE,SATCHEL_GVI_ALLOCATION,
 SATCHEL_GVI_INTERNAL,SATCHEL_GVI_NOT_OURS,SATCHEL_GVI_SYMBOL} SatchelGuestVIStatus;
typedef enum {SATCHEL_GVI_OPEN_DEFAULT=1,SATCHEL_GVI_CREATE_LAYER,SATCHEL_GVI_GET_NATIVE,
 SATCHEL_GVI_DESTROY_LAYER,SATCHEL_GVI_CLOSE_DISPLAY,SATCHEL_GVI_CREATE_SIZED,
 SATCHEL_GVI_CREATE_SETTINGS} SatchelGuestVIOperation;
typedef enum {SATCHEL_GVI_ACTIVE=1,SATCHEL_GVI_DETACHED,SATCHEL_GVI_RETIRING} SatchelGuestVIPhase;
typedef struct {uint32_t version,size;uint64_t process_id,generation,address,length;} SatchelGuestVIReservation;
typedef struct {uint32_t version,size;SatchelGuestVIReservation reservation;
 uint64_t surface_epoch;uint32_t max_events,timeout_ms;} SatchelGuestVIConfig;
typedef struct SatchelGuestVI SatchelGuestVI;
/* Claim is constructor-only and one-way for the Process lifetime. Validate is
 * pure, authoritative SAME-process/stopped-CPU/exclusive exact-NONE checking;
 * failed reads alone are not proof. Both callbacks are borrowed, never stored.
 * Process must forbid ALL guest changes to this RW->NONE reserved backing.
 * CPU itself retains mutable provenance; trusted host mutation must be refused. */
typedef int (*SatchelGuestVIReservationCheck)(void *,SatchelCPUProbe *,const SatchelGuestVIReservation *);
typedef int (*SatchelGuestVIOutputGuard)(void *,SatchelCPUProbe *,uint64_t,size_t);
typedef struct {SatchelCPUProbe *cpu;uint64_t surface_epoch;
 SatchelGuestVIReservationCheck validate;void *validation_context;
 SatchelGuestVIOutputGuard guard;void *guard_context;
 SatchelCPUProbeCancelled cancelled;void *cancel_context;} SatchelGuestVIBorrow;
typedef struct {uint32_t version,size;uint64_t process_id,reservation_generation,
 surface_epoch,display_identity,layer_identity,native_identity,
 display_generation,layer_generation,native_generation;} SatchelGuestVIInfo;
typedef struct {uint32_t phase,display,layer,native,references,issued;
 uint64_t events;} SatchelGuestVIStats;
typedef struct {uint32_t status,operation;uint64_t events;} SatchelGuestVIResult;
/* Host-only creation; valid out is cleared on failure (config copied first).
 * One owner per claimed reservation for the entire Process lifetime. No retail
 * object bytes/layout, dimensions/defaults or host pointers enter guest RAM. */
SatchelGuestVIStatus satchel_guest_vi_create(const SatchelGuestVIConfig *,SatchelCPUProbe *,
 SatchelGuestVIReservationCheck claim,void *,SatchelGuestVI **out);
/* Caller owns stopped CPU exclusively. Pure callbacks cannot mutate CPU/owners.
 * Supported Result functions commit8-byte pointer output and W0=0; void destroys
 * preserve X0. All ordinary refusals preserve guest PC/registers/bytes and all
 * semantic owner state. Lifetime event counters may advance on refusal; the
 * monotonic timeout is PER TRANSACTION, never owner lifetime/idle time.
 * Detached permits supported destruction only; retiring refuses every call.
 * trap/return must be actual guest PCs, never native pointers. */
SatchelGuestVIStatus satchel_guest_vi_execute(SatchelGuestVI *,const SatchelGuestVIBorrow *,
 uint32_t operation,uint64_t x0,uint64_t x1,uint64_t trap,uint64_t lr,SatchelGuestVIResult *);
/* Private host references for future Window owners. No token in guest RAM.
 * Both outputs publish only on success; all generation/address values are opaque
 * emulated identities, not retail SDK fields. Unknown extent stays unknown. */
SatchelGuestVIStatus satchel_guest_vi_retain_native(SatchelGuestVI *,const SatchelGuestVIBorrow *,
 uint64_t native_identity,uint64_t *reference,SatchelGuestVIInfo *);
SatchelGuestVIStatus satchel_guest_vi_release_native(SatchelGuestVI *,uint64_t reference);
SatchelGuestVIStatus satchel_guest_vi_detach(SatchelGuestVI *,uint64_t epoch);
/* CPU-free terminal retirement; preserves pointer until all references drain.
 * Process claim and address reservation stay burned until Process destruction. */
SatchelGuestVIStatus satchel_guest_vi_try_destroy(SatchelGuestVI **);
SatchelGuestVIStatus satchel_guest_vi_stats(SatchelGuestVI *,SatchelGuestVIStats *);
#ifdef __cplusplus
}
#endif
#endif
