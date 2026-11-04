#ifndef SATCHEL_GUEST_TRANSFERS_H
#define SATCHEL_GUEST_TRANSFERS_H
#include "GuestPools.h"
#include "GuestCompute.h"
#include "GuestRasterRecord.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {SATCHEL_GUEST_TRANSFER_QUEUE_LIMIT=4,SATCHEL_GUEST_TRANSFER_COMMAND_BUFFER_LIMIT=8,
    SATCHEL_GUEST_TRANSFER_HANDLE_LIMIT=16,SATCHEL_GUEST_TRANSFER_COMMAND_LIMIT=32,
    SATCHEL_GUEST_TRANSFER_SUBMIT_HANDLE_LIMIT=8,SATCHEL_GUEST_TRANSFER_QUEUE_MEMORY_LIMIT=1024*1024,
    SATCHEL_GUEST_TRANSFER_CANCELLED=10,SATCHEL_GUEST_TRANSFER_SHADER_STATE_UNAVAILABLE=11};
typedef enum {
    SATCHEL_GT_QUEUE_DEFAULTS=1,SATCHEL_GT_QUEUE_DEVICE,SATCHEL_GT_QUEUE_FLAGS,SATCHEL_GT_QUEUE_COMMAND_SIZE,
    SATCHEL_GT_QUEUE_COMPUTE_SIZE,SATCHEL_GT_QUEUE_CONTROL_SIZE,SATCHEL_GT_QUEUE_MEMORY_SIZE,SATCHEL_GT_QUEUE_MEMORY,
    SATCHEL_GT_QUEUE_INITIALIZE,SATCHEL_GT_QUEUE_FINALIZE,SATCHEL_GT_QUEUE_SUBMIT,SATCHEL_GT_QUEUE_FINISH,
    SATCHEL_GT_COMMAND_INITIALIZE,SATCHEL_GT_COMMAND_FINALIZE,SATCHEL_GT_COMMAND_MEMORY,SATCHEL_GT_CONTROL_MEMORY,
    SATCHEL_GT_BEGIN,SATCHEL_GT_END,SATCHEL_GT_CLEAR,SATCHEL_GT_COPY,SATCHEL_GT_HANDLE_FINALIZE,
    SATCHEL_GT_COMMAND_SIZE,SATCHEL_GT_COMMAND_USED,SATCHEL_GT_COMMAND_FREE,
    SATCHEL_GT_CONTROL_SIZE,SATCHEL_GT_CONTROL_USED,SATCHEL_GT_CONTROL_FREE,SATCHEL_GT_IS_RECORDING,
    SATCHEL_GT_PROGRAM_INITIALIZE,SATCHEL_GT_PROGRAM_FINALIZE,SATCHEL_GT_PROGRAM_SHADERS,
    SATCHEL_GT_BIND_PROGRAM,SATCHEL_GT_BIND_UNIFORM,SATCHEL_GT_BIND_STORAGE,SATCHEL_GT_DISPATCH,SATCHEL_GT_QUEUE_FLUSH,
    SATCHEL_GT_RENDER_TARGETS,SATCHEL_GT_CLEAR_COLOR,SATCHEL_GT_DRAW_ARRAYS,SATCHEL_GT_VERTEX_BUFFER,SATCHEL_GT_VERTEX_ATTRIBUTES,SATCHEL_GT_VERTEX_STREAM,
    SATCHEL_GT_VIEWPORT,SATCHEL_GT_SCISSOR,SATCHEL_GT_DEPTH_RANGE,SATCHEL_GT_COLOR,SATCHEL_GT_CHANNEL,SATCHEL_GT_POLYGON,SATCHEL_GT_DEPTH_STENCIL,SATCHEL_GT_MULTISAMPLE,
    SATCHEL_GT_DEPTH_CLAMP,SATCHEL_GT_RASTER_DISCARD,SATCHEL_GT_SAMPLE_MASK,SATCHEL_GT_RENDER_ENABLE,SATCHEL_GT_SWIZZLES
} SatchelGuestTransferOperation;
typedef struct {uint32_t operation;uint64_t x[6];uint32_t f[2];} SatchelGuestTransferRequest;
typedef struct {uint32_t status,set_value;uint64_t value;} SatchelGuestTransferResult;
typedef struct SatchelGuestTransfers SatchelGuestTransfers;
/* Staged trusted host state machine, not automatically enabled in App. Owns
 * caller-provided guest command/control/queue ranges and retained pool leases.
 * One logical queue per real device,32 transfer commands/recording/submission,
 * one live recording per command buffer until explicit handle finalization.
 * Pool owner outlives this component; validator/context independently outlive
 * retirement. No CPU/provider/cancel pointer is retained. Submission prepares
 * caller queue memory before contacting the backend. A zero-token failure can
 * leave that staged snapshot; it does not advance submission state or acquire
 * in-flight handle references. Every nonzero backend job is retained on error.
 * This is not an atomic transaction spanning guest RAM and GPU submission. */
SatchelGuestTransfers *satchel_guest_transfers_create(SatchelGuestPools *pools,SatchelGuestPoolDeviceValidator validate,SatchelGuestPoolRangeGuard guard,void *context);
SatchelGuestTransfers *satchel_guest_transfers_create_checked(SatchelGuestPools *pools,SatchelGuestPoolDeviceValidator validate,SatchelGuestPoolCheckedRangeGuard guard,void *context);
uint32_t satchel_guest_transfers_guard(const SatchelGuestTransfers *,SatchelCPUProbe *,uint64_t,uint64_t);
typedef uint32_t (*SatchelGuestRasterQueueQuery)(void *,SatchelCPUProbe *,uint64_t,SatchelGuestRasterQueueState *);
int satchel_guest_transfers_attach_raster(SatchelGuestTransfers *,SatchelGuestTextures *,SatchelGuestRasterPrograms *,SatchelGuestRasterQueueQuery,void *);
int satchel_guest_transfers_raster_pending(const SatchelGuestTransfers *);
int satchel_guest_transfers_attach_compute(SatchelGuestTransfers *,SatchelGuestCompute *);
uint32_t satchel_guest_transfers_execute(SatchelGuestTransfers *,SatchelCPUProbe *,const SatchelGuestTransferRequest *,SatchelGuestTransferResult *,SatchelCPUProbeCancelled,void *);
/* CPU-free: reap own queue jobs before releasing handle/pool leases. BUSY/error
 * retains owner. Success clears pointer. Parent device cleanup comes later. */
uint32_t satchel_guest_transfers_try_destroy(SatchelGuestTransfers **);
uint32_t satchel_guest_transfers_device_children(SatchelGuestTransfers *,uint64_t device,uint32_t *count);
uint32_t satchel_guest_transfers_check_unowned_range(SatchelGuestTransfers *,uint64_t address,uint64_t size);
uint32_t satchel_guest_transfers_check_unowned_range_checked(SatchelGuestTransfers *,SatchelCPUProbe *,uint64_t address,uint64_t size);
/* Private composite-owner pool guard only. The external dispatch owner lock
 * must serialize all access. May be called synchronously by pool validation
 * inside an active transfer. Pure scan, no callbacks/mutations. Command-storage
 * ranges are excluded because they are intentionally inside owned pools; pool
 * object/storage exclusion already protects them from new pool allocations. */
uint32_t satchel_guest_transfers_check_pool_range_checked(const SatchelGuestTransfers *,SatchelCPUProbe *,uint64_t address,uint64_t size);
/* Host-only presentation order lease. These APIs never execute a guest NVN
 * call and never retain CPU/cancel pointers. Prepare validates the actual queue
 * and device generation; one lease per queue blocks subsequent Submit/Finalize.
 * Both outputs publish only on success. All pending GPU work remains owned by
 * the queue; cancellation of preparation does NOT cancel that preceding work. */
typedef struct {uint32_t version,size;uint64_t queue,queue_generation,guest_device,
 backend_device,device_generation,sequence;} SatchelGuestPresentQueueInfo;
typedef struct {uint32_t status,ready,drained,cancelled;uint64_t sequence;} SatchelGuestPresentQueueProgress;
uint32_t satchel_guest_transfers_present_prepare(SatchelGuestTransfers *,SatchelCPUProbe *,uint64_t queue,
 uint64_t expected_device,uint64_t *lease,SatchelGuestPresentQueueInfo *);
/* timeout_ms <=10 bounds the requested fence wait, not total synchronous
 * readback/repack/publication or driver wall time. Real completion includes
 * raster pool publication. Cancellation
 * is latched; a cancelled lease must still drain before release. No guest writes. */
uint32_t satchel_guest_transfers_present_poll(SatchelGuestTransfers *,uint64_t lease,uint32_t timeout_ms,
 int cancel,SatchelGuestPresentQueueProgress *);
/* CPU-free, release only after drained. Completed queue-tag publication is
 * deferred to the next checked guest queue operation. No stale queue generation
 * may be finalized/reused while the lease exists. */
uint32_t satchel_guest_transfers_present_release(SatchelGuestTransfers *,uint64_t lease);
#ifdef __cplusplus
}
#endif
#endif
