#ifndef SATCHEL_GUEST_RASTER_RUN_H
#define SATCHEL_GUEST_RASTER_RUN_H
#include "GuestTextures.h"
#include "GuestRasterPrograms.h"
#include "VulkanRasterBatch.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {SATCHEL_GRUN_OK,SATCHEL_GRUN_INVALID,SATCHEL_GRUN_UNSUPPORTED,SATCHEL_GRUN_CHANGED,SATCHEL_GRUN_BUSY,
 SATCHEL_GRUN_MEMORY,SATCHEL_GRUN_BACKEND,SATCHEL_GRUN_ALLOCATION,SATCHEL_GRUN_CANCELLED,SATCHEL_GRUN_INTERNAL};
enum {SATCHEL_GRUN_LIMIT=4,SATCHEL_GRUN_HOST_LIMIT=64*1024*1024,SATCHEL_GRUN_DRAW_LIMIT=8};
typedef struct SatchelGuestRasterRun SatchelGuestRasterRun;
typedef struct {
 uint32_t operation;
 uint64_t program,program_generation,capture_generation,vertex_gpu,vertex_size;
 SatchelVulkanRasterDescription pipeline;
 SatchelVulkanRasterCommand draw; /* pipeline/buffer/offset must0: derived from owners. */
} SatchelGuestRasterCommand;
typedef struct {
 uint32_t version,size,load_operation,command_count;
 uint64_t texture,texture_generation;
 float initial_clear_rgba[4];
 SatchelGuestRasterCommand commands[SATCHEL_VULKAN_RASTER_BATCH_LIMIT];
} SatchelGuestRasterRequest;
typedef struct {uint32_t status,phase,upload_completed,draw_completed,published,cancelled,
 compiled_pipelines,resources_retained;uint64_t host_bytes;} SatchelGuestRasterRunReport;
typedef struct {uint32_t live_runs,run_limit;uint64_t host_bytes,host_limit;} SatchelGuestRasterRunStats;
/* Trusted composite entry. Actual texture/pair/code/pool generations and full
 * physical spans are checked, actual SPH/code is compiled under recorded state.
 * No fixed shader, default raster state or caller-provided host GPU token.
 * At most8 draws/16 ordered commands. Initial/midpass clear requires the caller
 * to prove NVN's actual clear state matches full-image/all-channel semantics.
 * All composite GPU submit/flush operations must be serialized AND excluded
 * while this run owns pending publication, including previously cached bindings.
 * Owners outlive this run. CPU/callback/context are borrowed only during call.
 * Cancellation callbacks may only read cancellation/deadline state: no guest
 * execution, owner mutation or GPU reentry. The composite gate enforces this.
 * Any nonnull output run retains ownership EVEN ON ERROR; poll it to completion.
 * Begin always uploads current GPU-visible tiled backing, never assumes zero. */
uint32_t satchel_guest_raster_run_begin(SatchelGuestPools *,SatchelGuestTextures *,SatchelGuestRasterPrograms *,
 SatchelCPUProbe *,const SatchelGuestRasterRequest *,SatchelGuestRasterRun **,SatchelVulkanCancelled,void *);
/* CPU-free progress. Cancel before draw may abandon it after upload fence;
 * after draw submission, fence and exact pool publication still complete before
 * releasing holds. Repack/read/write failures retain all resources for retry.
 * A successful publication changes GPU mirror only: guest CPU must invalidate.
 * Never free a nonnull run. On full cleanup pointer is cleared; report survives. */
uint32_t satchel_guest_raster_run_poll(SatchelGuestRasterRun **,uint32_t timeout_ms,int cancel,
 SatchelGuestRasterRunReport *);
/* Host metadata only; phase1/2 means actual upload/draw submission attempted.
 * Used to distinguish retained cleanup-only admission from GPU submission. */
uint32_t satchel_guest_raster_run_peek(SatchelGuestRasterRun *,SatchelGuestRasterRunReport *);
uint32_t satchel_guest_raster_run_stats(SatchelGuestRasterRunStats *);
#ifdef __cplusplus
}
#endif
#endif
