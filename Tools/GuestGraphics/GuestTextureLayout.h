#ifndef SATCHEL_GUEST_TEXTURE_LAYOUT_H
#define SATCHEL_GUEST_TEXTURE_LAYOUT_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
enum {SATCHEL_GTL_OK,SATCHEL_GTL_INVALID,SATCHEL_GTL_UNSUPPORTED,SATCHEL_GTL_LIMIT};
enum {SATCHEL_GTL_RGBA8=0x25,SATCHEL_GTL_2D=1,SATCHEL_GTL_LINEAR_RENDER_TARGET=0x40,
 SATCHEL_GTL_MAX_DIMENSION=2048,SATCHEL_GTL_MAX_STORAGE=16*1024*1024};
typedef struct {
 uint32_t version,size,target,format,flags,width,height,depth,levels,samples;
 uint64_t stride;
} SatchelGuestTextureDescription;
typedef struct {
 SatchelGuestTextureDescription description;
 uint64_t storage_size,row_bytes,row_pitch;
 uint32_t alignment,block_height_log2;
} SatchelGuestTextureLayout;
/* Checked one-mip/layer RGBA8 layout. samples0/1 mean single sample. Ordinary
 * flags0 Tegra block-linear or explicit LINEAR_RENDER_TARGET0x40 only. No
 * sparse/compressed/depth/packaged/multisample/array fallbacks. Failure preserves
 * output. Sizes include actual guest tiling/row padding, not VkImage padding. */
int satchel_guest_texture_layout(const SatchelGuestTextureDescription *,SatchelGuestTextureLayout *);
int satchel_guest_texture_offset(const SatchelGuestTextureLayout *,uint32_t x_byte,uint32_t y,uint64_t *);
/* Complete valid nonoverlapping host spans required. All validation precedes
 * writes; only active texels change, preserving destination row/tile padding.
 * No CPU guest pointers, allocation, logs or data-dependent out-of-bounds work.
 * Layout is re-derived/checked rather than trusting caller-authored offsets. */
int satchel_guest_texture_decode(const SatchelGuestTextureLayout *,const void *storage,size_t storage_capacity,
 void *linear,size_t linear_capacity,uint64_t row_pitch);
int satchel_guest_texture_encode(const SatchelGuestTextureLayout *,const void *linear,size_t linear_capacity,
 uint64_t row_pitch,void *storage,size_t storage_capacity);
#ifdef __cplusplus
}
#endif
#endif
