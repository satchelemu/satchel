#ifndef SATCHEL_GUEST_TEXTURES_H
#define SATCHEL_GUEST_TEXTURES_H
#include "GuestTextureLayout.h"
#include "GuestPools.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {SATCHEL_GTEX_LIMIT=8,SATCHEL_GTEX_REFS=64,SATCHEL_GTEX_BUILDER_SIZE=0x80,SATCHEL_GTEX_OBJECT_SIZE=0xc0};
enum {SATCHEL_GTEX_OK,SATCHEL_GTEX_INVALID,SATCHEL_GTEX_MEMORY,SATCHEL_GTEX_BAD_OBJECT,SATCHEL_GTEX_UNSUPPORTED,
 SATCHEL_GTEX_LIMIT_REACHED,SATCHEL_GTEX_BUSY,SATCHEL_GTEX_BACKEND,SATCHEL_GTEX_ALLOCATION,SATCHEL_GTEX_INTERNAL};
enum {SATCHEL_GTEX_DEFAULTS=1,SATCHEL_GTEX_DEVICE,SATCHEL_GTEX_FLAGS,SATCHEL_GTEX_TARGET,SATCHEL_GTEX_SIZE2D,
 SATCHEL_GTEX_LEVELS,SATCHEL_GTEX_FORMAT,SATCHEL_GTEX_SAMPLES,SATCHEL_GTEX_STRIDE,SATCHEL_GTEX_STORAGE,
 SATCHEL_GTEX_STORAGE_SIZE,SATCHEL_GTEX_STORAGE_ALIGNMENT,SATCHEL_GTEX_INITIALIZE,SATCHEL_GTEX_FINALIZE,
 SATCHEL_GTEX_WIDTH,SATCHEL_GTEX_HEIGHT,SATCHEL_GTEX_POOL,SATCHEL_GTEX_OFFSET,SATCHEL_GTEX_GPU_ADDRESS};
typedef struct SatchelGuestTextures SatchelGuestTextures;
typedef struct {uint32_t operation;uint64_t object,arg0,arg1;} SatchelGuestTextureRequest;
typedef struct {uint32_t status;uint64_t value;} SatchelGuestTextureResult;
typedef struct {
 uint64_t object,guest_device,backend_device,device_generation,generation;
 uint64_t target,pool_lease,pool_generation,pool_object,storage_cpu,storage_gpu;
 SatchelGuestPoolBinding binding;SatchelGuestTextureLayout layout;
} SatchelGuestTextureBinding;
/* Host-only side owner, no native pointers in guest tags. Actual opaque sizes
 * from pinned public NVN ABI. Pools/device/context outlive this owner. A texture
 * owns a real VkImage and retained pool range; it is not initialized to fake zero.
 * Adapter uploads actual GPU-visible pool contents before LOAD and publishes
 * completed image bytes back in the exact guest layout. */
SatchelGuestTextures *satchel_guest_textures_create(SatchelGuestPools *,SatchelGuestPoolDeviceValidator,
 SatchelGuestPoolCheckedRangeGuard,void *context);
uint32_t satchel_guest_textures_execute(SatchelGuestTextures *,SatchelCPUProbe *,const SatchelGuestTextureRequest *,SatchelGuestTextureResult *);
/* Retain validates current tag/device/pool, complete physical spans and target
 * properties. Output is HOST metadata. Texture storage aliases are unsupported. */
uint32_t satchel_guest_textures_retain(SatchelGuestTextures *,SatchelCPUProbe *,uint64_t object,uint64_t *reference,SatchelGuestTextureBinding *);
uint32_t satchel_guest_textures_binding(SatchelGuestTextures *,SatchelCPUProbe *,uint64_t reference,SatchelGuestTextureBinding *);
uint32_t satchel_guest_textures_release(SatchelGuestTextures *,uint64_t reference);
/* Pure reciprocal object-range guard, no callbacks/locks/writes. */
uint32_t satchel_guest_textures_pool_guard(const SatchelGuestTextures *,SatchelCPUProbe *,uint64_t,uint64_t);
uint32_t satchel_guest_textures_device_children(SatchelGuestTextures *,uint64_t,uint32_t *);
/* CPU-free teardown: targets then pool leases. Failed releases preserve owner
 * and progress; recording references must release before destruction. */
uint32_t satchel_guest_textures_try_destroy(SatchelGuestTextures **);
#ifdef __cplusplus
}
#endif
#endif
