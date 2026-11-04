#ifndef SATCHEL_GUEST_POOLS_H
#define SATCHEL_GUEST_POOLS_H
#include "CPUProbe.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {SATCHEL_GUEST_POOL_LIMIT=8,SATCHEL_GUEST_POOL_MAX_SIZE=16*1024*1024,SATCHEL_GUEST_POOL_BACKING_LIMIT=128*1024*1024,
    SATCHEL_GUEST_POOL_ALIGNMENT=65536,SATCHEL_GUEST_POOL_BUILDER_SIZE=64,SATCHEL_GUEST_POOL_OBJECT_SIZE=256,
    SATCHEL_GUEST_POOL_CACHED_FLAGS=0x24};
typedef enum {SATCHEL_GUEST_POOL_OK=0,SATCHEL_GUEST_POOL_INVALID,SATCHEL_GUEST_POOL_MEMORY,
    SATCHEL_GUEST_POOL_BAD_OBJECT,SATCHEL_GUEST_POOL_UNSUPPORTED,SATCHEL_GUEST_POOL_LIMIT_REACHED,
    SATCHEL_GUEST_POOL_BUSY,SATCHEL_GUEST_POOL_BACKEND,SATCHEL_GUEST_POOL_ALLOCATION,SATCHEL_GUEST_POOL_INTERNAL} SatchelGuestPoolStatus;
/* Trusted synchronous callback validates a current tagged guest device against
 * its host owner and returns backend device/generation IDs. These are host-only
 * integers; never write them to guest memory. Context must independently outlive
 * this pool owner. No CPU/provider pointer is retained by the pool component. */
typedef int (*SatchelGuestPoolDeviceValidator)(void *context,SatchelCPUProbe *cpu,uint64_t guest_device,
    uint64_t *backend_device,uint64_t *generation);
typedef struct SatchelGuestPools SatchelGuestPools;
typedef struct {uint32_t live_pools,provisional_pools;uint64_t logical_bytes,backing_bytes,global_backing_bytes,global_backing_limit,transient_bytes,transient_limit;} SatchelGuestPoolBudget;
/* Read-only host budget snapshot; provisional resources remain charged until
 * actual destruction. Global bytes include all VkBuffers, not only this owner. */
SatchelGuestPoolStatus satchel_guest_pools_budget(SatchelGuestPools *,SatchelGuestPoolBudget *);
/* Optional trusted guard rejects overlap with other private owned objects.
 * Called synchronously after checked range validation; context is the same
 * independent owner context, never a retained CPU/provider. */
typedef int (*SatchelGuestPoolRangeGuard)(void *context,uint64_t address,uint64_t size);
/* Additive checked guard: CPU is borrowed synchronously, never retained. */
typedef int (*SatchelGuestPoolCheckedRangeGuard)(void *context,SatchelCPUProbe *cpu,uint64_t address,uint64_t size);
typedef enum {SATCHEL_GUEST_POOL_BUILDER_DEFAULTS=1,SATCHEL_GUEST_POOL_BUILDER_DEVICE,
    SATCHEL_GUEST_POOL_BUILDER_STORAGE,SATCHEL_GUEST_POOL_BUILDER_FLAGS,SATCHEL_GUEST_POOL_INITIALIZE,
    SATCHEL_GUEST_POOL_FINALIZE,SATCHEL_GUEST_POOL_MAP,SATCHEL_GUEST_POOL_GPU_ADDRESS,
    SATCHEL_GUEST_POOL_FLUSH,SATCHEL_GUEST_POOL_INVALIDATE} SatchelGuestPoolOperation;
typedef struct {uint32_t operation;uint64_t object,argument0,argument1;} SatchelGuestPoolRequest;
typedef struct {uint32_t status;uint64_t value;} SatchelGuestPoolResult;
typedef struct {uint64_t buffer,offset,size,pool_generation;} SatchelGuestPoolBinding;
enum {SATCHEL_GUEST_POOL_LEASE_LIMIT=512};
typedef struct {uint64_t object,guest_device,cpu_address,gpu_address,size,generation;} SatchelGuestPoolView;
/* Trusted host-only retained recording references. IDs/views must never enter
 * guest RAM or return registers. Retain validates the current full pool and
 * supplied parent/range before reserving a bounded lease. Each lease releases
 * exactly once; release is CPU-free and allowed during retirement. Finalize/
 * owner destruction cannot release backing while any lease remains. Views and
 * bindings are revalidated on each use; retaining does not pin CPU mappings. */
SatchelGuestPoolStatus satchel_guest_pools_retain_object(SatchelGuestPools *,SatchelCPUProbe *,uint64_t guest_device,uint64_t object,uint64_t *lease,SatchelGuestPoolView *view);
SatchelGuestPoolStatus satchel_guest_pools_retain_range(SatchelGuestPools *,SatchelCPUProbe *,uint64_t guest_device,uint64_t gpu,uint64_t size,uint64_t *lease,SatchelGuestPoolView *view);
SatchelGuestPoolStatus satchel_guest_pools_lease_view(SatchelGuestPools *,SatchelCPUProbe *,uint64_t lease,SatchelGuestPoolView *view);
SatchelGuestPoolStatus satchel_guest_pools_lease_binding(SatchelGuestPools *,SatchelCPUProbe *,uint64_t lease,uint64_t gpu,uint64_t size,SatchelGuestPoolBinding *binding);
SatchelGuestPoolStatus satchel_guest_pools_release(SatchelGuestPools *,uint64_t lease);
/* A checked exclusive GPU/publication hold on one live pool lease. It blocks
 * Flush/Invalidate and prevents releasing that lease until explicit release.
 * Revalidate all bindings before acquisition; pending jobs retain the lease and
 * hold through real fence and successful mirror publication. Release is CPU-free.
 * One hold per pool; deduplicate same-pool resources in a command snapshot.
 * New resolves/retains and bindings through other leases return BUSY. This does
 * not revoke an already copied host binding: the composite must exclude legacy
 * cached-binding submissions while a publication owner is pending. */
SatchelGuestPoolStatus satchel_guest_pools_gpu_acquire(SatchelGuestPools *,SatchelCPUProbe *,uint64_t lease);
SatchelGuestPoolStatus satchel_guest_pools_gpu_release(SatchelGuestPools *,uint64_t lease);
/* Staged host component only: not registered as NVN methods in the app.
 * One backend VkBuffer per pool, explicit CPU_CACHED|GPU_CACHED profile only.
 * Storage base/size must be64KiB aligned, nonempty, <=16MiB in one RW mapping.
 * The backend charges actual padded allocations to a shared128MiB buffer budget
 * across all owners/devices, including provisional buffers, staging and readback.
 * Pool backing explicitly has STORAGE|TRANSFER_SRC|TRANSFER_DST|VERTEX usage.
 * A validated initial CPU snapshot is uploaded; later writes require explicit
 * flush and completed GPU writes require invalidate. Other flags are rejected.
 * Immutable opaque pool IDs and GPU ranges are never reused, even across owners.
 * Methods and external uses of resolved bindings require exclusive owner/CPU
 * lifetime. Per-owner reentry/concurrency is rejected; caller retains pointers. */
SatchelGuestPools *satchel_guest_pools_create(SatchelGuestPoolDeviceValidator validate,void *context);
SatchelGuestPools *satchel_guest_pools_create_protected(SatchelGuestPoolDeviceValidator validate,SatchelGuestPoolRangeGuard guard,void *context);
SatchelGuestPools *satchel_guest_pools_create_checked(SatchelGuestPoolDeviceValidator validate,SatchelGuestPoolCheckedRangeGuard guard,void *context);
/* Host-only ownership queries, including provisional backing. No CPU access.
 * check_unowned_range rejects overlap with any pool object/storage span. */
SatchelGuestPoolStatus satchel_guest_pools_device_children(SatchelGuestPools *owner,uint64_t guest_device,uint32_t *count);
SatchelGuestPoolStatus satchel_guest_pools_check_unowned_range(SatchelGuestPools *owner,uint64_t address,uint64_t size);
/* Checks physical backing overlap, including locked source views. Unknown or
 * unmapped protected spans fail closed; caller separately validates permissions. */
SatchelGuestPoolStatus satchel_guest_pools_check_unowned_range_checked(SatchelGuestPools *,SatchelCPUProbe *,uint64_t address,uint64_t size);
/* Worker teardown without CPU access. ReturnsOK and clears pointer only after
 * all buffers destroy successfully; BUSY/error preserves owner and unreleased
 * slots. Drain pending jobs first. Parent devices must outlive pool teardown. */
SatchelGuestPoolStatus satchel_guest_pools_try_destroy(SatchelGuestPools **owner);
/* Request object is always a guest builder/pool address. Arguments:
 * DEVICE:arg0 guest device; STORAGE:arg0 guest base,arg1 byte size;
 * FLAGS:arg0 low32 flags; INITIALIZE:arg0 builder;
 * FLUSH/INVALIDATE:arg0 nonnegative signed offset,arg1 byte count.
 * MAP/GPU_ADDRESS return a guest address; INITIALIZE returns1 only on commit.
 * No PC/register mutation: a future dispatcher must validate guest LR before
 * invoking a mutating operation, then handle the result without fake success. */
SatchelGuestPoolStatus satchel_guest_pools_execute(SatchelGuestPools *owner,SatchelCPUProbe *cpu,
    const SatchelGuestPoolRequest *request,SatchelGuestPoolResult *result);
/* Resolves a complete nonempty GPU span within a live, generation-checked pool
 * owned by this current guest device. Binding is trusted HOST metadata only;
 * no Vk/native pointers are exposed and backend tokens must not reach guest RAM. */
SatchelGuestPoolStatus satchel_guest_pools_resolve(SatchelGuestPools *owner,SatchelCPUProbe *cpu,uint64_t guest_device,
    uint64_t gpu_address,uint64_t size,SatchelGuestPoolBinding *binding);
#ifdef __cplusplus
}
#endif
#endif
