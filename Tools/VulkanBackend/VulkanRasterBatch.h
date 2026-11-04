#ifndef SATCHEL_VULKAN_RASTER_BATCH_H
#define SATCHEL_VULKAN_RASTER_BATCH_H
#include "VulkanRaster.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {SATCHEL_VULKAN_RASTER_BATCH_LIMIT=16,SATCHEL_VULKAN_RASTER_LOAD=1,SATCHEL_VULKAN_RASTER_CLEAR=2,
 SATCHEL_VULKAN_RASTER_OP_DRAW=1,SATCHEL_VULKAN_RASTER_OP_CLEAR=2};
typedef struct {
 uint32_t operation,vertex_count,first_vertex;
 uint64_t pipeline,vertex_buffer,vertex_offset;
 float viewport_x,viewport_y,viewport_width,viewport_height,min_depth,max_depth;
 uint32_t scissor_x,scissor_y,scissor_width,scissor_height;
 float clear_rgba[4];
} SatchelVulkanRasterCommand;
typedef struct {
 uint32_t version,size,load_operation,command_count;
 float initial_clear_rgba[4];
 uint64_t readback_buffer,readback_offset,row_pitch;
 SatchelVulkanRasterCommand commands[SATCHEL_VULKAN_RASTER_BATCH_LIMIT];
} SatchelVulkanRasterBatch;
/* Actual full-image buffer upload; mandatory when a future guest owner needs
 * initial pool contents or explicit CPU-flushed edits to seed a preserved image.
 * Source TRANSFER_SOURCE and whole checked row span required. Nonzero job owns
 * source and target until real fence; layout/initialized state changes on reap. */
SatchelVulkanStatus satchel_vulkan_target_upload(uint64_t device,uint64_t target,uint64_t source,
 uint64_t offset,uint64_t row_pitch,uint64_t *job,SatchelVulkanCancelled,void *);
/* One target, up to16 ordered draw/full-color-clear operations. Explicit LOAD
 * requires a completed initialized target and preserves previous pixels. CLEAR
 * is performed only when requested; no repeated implicit clear between draws.
 * A mid-pass CLEAR explicitly clears all4 channels/full image, independent of
 * a draw's scissor. Guest adapter must prove that matches actual clear state.
 * Pipelines, all unique vertex buffers, target and readback remain retained.
 * Full admission precedes Vulkan recording/submission; any nonzero token owns
 * work even on API error. No callback/context is retained by submitted jobs. */
SatchelVulkanStatus satchel_vulkan_raster_batch_readback(uint64_t device,uint64_t target,
 const SatchelVulkanRasterBatch *,uint64_t *job,SatchelVulkanCancelled,void *);
#ifdef __cplusplus
}
#endif
#endif
