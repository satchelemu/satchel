#ifndef SATCHEL_VULKAN_RASTER_H
#define SATCHEL_VULKAN_RASTER_H
#include "VulkanBackend.h"
#include "RasterCompiler.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {SATCHEL_VULKAN_RASTER_PIPELINE_LIMIT=8,SATCHEL_VULKAN_RASTER_VERTEX_LIMIT=4096,
 SATCHEL_VULKAN_RASTER_TRIANGLE_LIST=3};
typedef struct {
 uint32_t version,size;SatchelRasterState state;
 uint32_t format,samples,topology,primitive_restart,cull_mode,front_face;
 uint32_t depth_test,depth_write,stencil_test,blend_enable,color_mask;
 uint32_t polygon_mode,rasterizer_discard,depth_clamp,depth_bias,logic_op_enable;
 uint32_t sample_mask,sample_shading,alpha_to_one; /* fill0, all enables0, sample_mask1 */
 uint32_t vertex_stride,attribute_offsets[8]; /* UINT32_MAX for unused locations. */
} SatchelVulkanRasterDescription;
typedef struct {uint64_t device;uint32_t references,attribute_mask,vertex_stride;} SatchelVulkanRasterInfo;
typedef struct {uint32_t live_pipelines,pipeline_limit;} SatchelVulkanRasterStats;
typedef struct {uint32_t version,size,graphics_queue,rgba8_target,float4_vertex,max_attributes,max_stride,max_attribute_offset;} SatchelVulkanRasterCaps;
SatchelVulkanStatus satchel_vulkan_raster_capabilities(uint64_t,SatchelVulkanRasterCaps *);
typedef struct {
 uint32_t version,size,vertex_count,first_vertex;
 uint64_t vertex_buffer,vertex_offset,readback_buffer,readback_offset,row_pitch;
 float viewport_x,viewport_y,viewport_width,viewport_height,min_depth,max_depth;
 uint32_t scissor_x,scissor_y,scissor_width,scissor_height;
 float clear_rgba[4];
} SatchelVulkanRasterDraw;
/* Only the independently validated dynamic scalar raster schema is admitted:
 * caller-provided SPIR-V from actual VS/FS compilation, no replacement modules.
 * Shader-only, float32 vec4 inputs and Position/RGBA0 output, no descriptors,
 * interstage varyings, arithmetic, loops or optional features. Known explicit
 * state must match the compiler's documented profile. Pipeline creation copies
 * inputs; no caller pointer is retained. Each active location fetches all16 bytes.
 * Actual GRAPHICS queue/format/device limits are mandatory. */
SatchelVulkanStatus satchel_vulkan_raster_validate(const SatchelRasterModule *,uint32_t stage);
SatchelVulkanStatus satchel_vulkan_raster_create(uint64_t device,const SatchelVulkanRasterDescription *,
 const SatchelRasterModule *vertex,const SatchelRasterModule *fragment,uint64_t *pipeline,SatchelVulkanRasterInfo *);
SatchelVulkanStatus satchel_vulkan_raster_info(uint64_t device,uint64_t pipeline,SatchelVulkanRasterInfo *);
SatchelVulkanStatus satchel_vulkan_raster_destroy(uint64_t device,uint64_t pipeline);
SatchelVulkanStatus satchel_vulkan_raster_stats(SatchelVulkanRasterStats *);
/* Actual triangle-list draw with full-target render-pass clear, explicit dynamic
 * viewport/scissor and image-to-buffer readback. One vertex buffer, no indexing,
 * instancing or vertex-base offset. Same-parent typed resources remain retained
 * until actual fence completion. Nonzero job owns every resource even on error.
 * Cancellation never fabricates completion or drops uncertain ownership. */
SatchelVulkanStatus satchel_vulkan_raster_draw_readback(uint64_t device,uint64_t pipeline,uint64_t target,
 const SatchelVulkanRasterDraw *,uint64_t *job,SatchelVulkanCancelled,void *);
#ifdef __cplusplus
}
#endif
#endif
