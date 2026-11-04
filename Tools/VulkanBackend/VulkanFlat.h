#ifndef SATCHEL_VULKAN_FLAT_H
#define SATCHEL_VULKAN_FLAT_H
#include "VulkanBackend.h"
#include "VulkanRaster.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Capability/preflight only. This API does not validate SPIR-V, create a
 * pipeline or claim a supplied shader implements this interface. A future
 * schema validator must independently derive every field from copied code. */
typedef struct {
 uint32_t version,size,api_version,graphics_queue,float4_vertex;
 uint32_t max_attributes,max_stride,max_attribute_offset;
 uint32_t max_vertex_output_components,max_fragment_input_components;
 uint32_t max_fragment_outputs,first_enabled,last_enabled;
 uint32_t last_supported;
 /* Physical optimal-tiling B8G8R8A8_UNORM COLOR_ATTACHMENT feature, independent
  * of implemented target formats. It does NOT admit BGRA target creation. */
 uint32_t physical_bgra8_color_attachment;
 uint32_t admitted_color_format; /* Only SATCHEL_VULKAN_TARGET_RGBA8_UNORM. */
} SatchelVulkanFlatCaps;
typedef struct {
 uint32_t version,size,vertex_inputs[8];
 uint32_t vertex_outputs[8],fragment_inputs[8],flat_outputs[8],flat_inputs[8];
 uint32_t position_components,color_components;
 uint32_t declared_resources,active_resources,inactive_support_uniform;
 uint32_t provoking_known,provoking_mode; /* FIRST0 only; LAST1 unsupported. */
 uint32_t vertex_stride,attribute_offsets[8];
} SatchelVulkanFlatInterface;
SatchelVulkanStatus satchel_vulkan_flat_capabilities(uint64_t,SatchelVulkanFlatCaps *);
/* Pure fixed-capacity check against a captured capability snapshot. No
 * allocation/native pointer/retained state and no caller output mutation. */
SatchelVulkanStatus satchel_vulkan_flat_preflight(const SatchelVulkanFlatCaps *,const SatchelVulkanFlatInterface *);
/* Separate typed profile; never weakens satchel_vulkan_raster_validate. Fixed copied
 * caller buffers; excess words/IDs and unreviewed operations are unsupported. */
enum {SATCHEL_VULKAN_FLAT_WORD_LIMIT=4096,SATCHEL_VULKAN_FLAT_ID_LIMIT=256};
typedef struct {uint32_t version,size,word_count,words[SATCHEL_VULKAN_FLAT_WORD_LIMIT];} SatchelVulkanFlatModule;
typedef struct {
 uint32_t stage,required_capabilities,inputs[8],outputs[8],read_components[8],written_components[8];
 uint32_t flat_inputs[8],flat_outputs[8];
 uint32_t position_components,point_size_one,color_components;
 uint32_t declared_uniforms,active_uniforms,support_set,support_binding,support_member_extent;
 uint32_t declared_clip_components,declared_cull_components,active_clip_components,active_cull_components;
} SatchelVulkanFlatStage;
typedef struct {uint32_t version,size;SatchelVulkanFlatStage vertex,fragment;} SatchelVulkanFlatPair;
typedef struct {uint32_t version,size;SatchelVulkanRasterDescription raster;uint32_t provoking_known,provoking_mode;} SatchelVulkanFlatDescription;
/* Full pair/type/access-chain/resource validation, no allocation or driver call.
 * Actual interpolation decorations remain visible; VS may omit Flat while FS
 * requires Flat. Failure leaves pair output unchanged. */
SatchelVulkanStatus satchel_vulkan_flat_validate_pair(const SatchelVulkanFlatModule *,const SatchelVulkanFlatModule *,SatchelVulkanFlatPair *);
/* Consumes actual copied compiler output only after complete paired admission.
 * Shared bounded pipeline registry and existing draw/batch/fence/destroy APIs.
 * All pointer arguments must identify disjoint caller-owned storage. Token/info
 * outputs clear before failures; pair reflection publishes only on success. */
SatchelVulkanStatus satchel_vulkan_flat_create(uint64_t,const SatchelVulkanFlatDescription *,const SatchelVulkanFlatModule *,const SatchelVulkanFlatModule *,uint64_t *,SatchelVulkanRasterInfo *,SatchelVulkanFlatPair *);
#ifdef __cplusplus
}
#endif
#endif
