#include "GuestTextureLayout.h"
#include <string.h>
int satchel_guest_texture_layout(const SatchelGuestTextureDescription *input,SatchelGuestTextureLayout *out){
 if(!input || !out)return SATCHEL_GTL_INVALID;const SatchelGuestTextureDescription d=*input;
 if(d.version!=1 || d.size!=sizeof(d) || !d.width || !d.height)return SATCHEL_GTL_INVALID;
 if(d.target!=SATCHEL_GTL_2D || d.format!=SATCHEL_GTL_RGBA8 || (d.flags && d.flags!=SATCHEL_GTL_LINEAR_RENDER_TARGET) || d.depth!=1 || d.levels!=1 || d.samples>1)return SATCHEL_GTL_UNSUPPORTED;
 if(d.width>SATCHEL_GTL_MAX_DIMENSION || d.height>SATCHEL_GTL_MAX_DIMENSION)return SATCHEL_GTL_LIMIT;
 SatchelGuestTextureLayout c={0};c.description=d;c.row_bytes=(uint64_t)d.width*4;
 if(d.flags==SATCHEL_GTL_LINEAR_RENDER_TARGET){
  c.alignment=128;c.row_pitch=d.stride?d.stride:(c.row_bytes+127)&~UINT64_C(127);
  if(c.row_pitch<c.row_bytes || (c.row_pitch&127))return SATCHEL_GTL_INVALID;
  if(c.row_pitch>SATCHEL_GTL_MAX_STORAGE/d.height)return SATCHEL_GTL_LIMIT;c.storage_size=c.row_pitch*d.height;
 }else{
  if(d.stride)return SATCHEL_GTL_UNSUPPORTED;c.alignment=512;
  const uint32_t shape=d.height*3/16;unsigned shift=shape>=16?4:shape>=8?3:shape>=4?2:shape>=2?1:0;
  while(shift && d.height<=(8u<<(shift-1)))--shift;c.block_height_log2=shift;
  const uint64_t columns=(c.row_bytes+63)/64,block_rows=((uint64_t)d.height+(8u<<shift)-1)/(8u<<shift);
  c.storage_size=columns*block_rows*(UINT64_C(512)<<shift);
 }
 if(!c.storage_size || c.storage_size>SATCHEL_GTL_MAX_STORAGE)return SATCHEL_GTL_LIMIT;*out=c;return SATCHEL_GTL_OK;
}
static int valid(const SatchelGuestTextureLayout *layout){
 if(!layout)return 0;SatchelGuestTextureLayout c;
 return satchel_guest_texture_layout(&layout->description,&c)==SATCHEL_GTL_OK && c.storage_size==layout->storage_size && c.row_bytes==layout->row_bytes && c.row_pitch==layout->row_pitch && c.alignment==layout->alignment && c.block_height_log2==layout->block_height_log2;
}
static uint64_t offset(const SatchelGuestTextureLayout *l,uint32_t x,uint32_t y){
 if(l->row_pitch)return (uint64_t)y*l->row_pitch+x;
 // Tegra GOB byte-address permutation, expressed as individual bit positions.
 const uint32_t within=(x&15u)|((y&1u)<<4)|((x&16u)<<1)|((y&6u)<<5)|((x&32u)<<3);
 const unsigned shift=l->block_height_log2;const uint64_t block_size=UINT64_C(512)<<shift;
 const uint64_t column=x>>6,block_row=y>>(3+shift),columns=(l->row_bytes+63)/64;
 const uint64_t gob=(y>>3)&((1u<<shift)-1);
 return (block_row*columns+column)*block_size+gob*512+within;
}
int satchel_guest_texture_offset(const SatchelGuestTextureLayout *l,uint32_t x,uint32_t y,uint64_t *out){
 if(!out || !valid(l) || x>=l->row_bytes || y>=l->description.height)return SATCHEL_GTL_INVALID;
 const uint64_t n=offset(l,x,y);if(n>=l->storage_size)return SATCHEL_GTL_INVALID;*out=n;return SATCHEL_GTL_OK;
}
static int disjoint(const void *a,size_t n,const void *b,size_t m){
 const uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
 return a && b && n && m && x<=UINTPTR_MAX-n && y<=UINTPTR_MAX-m && (x+n<=y || y+m<=x);
}
static int move(const SatchelGuestTextureLayout *l,const void *source,size_t source_size,void *dest,size_t dest_size,uint64_t pitch,int encode){
 if(!valid(l) || pitch<l->row_bytes)return SATCHEL_GTL_INVALID;
 const uint64_t rows=l->description.height-1;
 if(rows && pitch>(SIZE_MAX-l->row_bytes)/rows)return SATCHEL_GTL_INVALID;
 const uint64_t span=rows*pitch+l->row_bytes;if(span>SIZE_MAX || l->storage_size>SIZE_MAX)return SATCHEL_GTL_INVALID;
 const uint64_t need_source=encode?span:l->storage_size,need_dest=encode?l->storage_size:span;
 if(need_source>source_size || need_dest>dest_size || !disjoint(source,(size_t)need_source,dest,(size_t)need_dest))return SATCHEL_GTL_INVALID;
 const uint8_t *src=source;uint8_t *dst=dest;
 for(uint32_t y=0;y<l->description.height;y++)for(uint32_t x=0;x<l->row_bytes;x++){
  const uint64_t tiled=offset(l,x,y),linear=(uint64_t)y*pitch+x;
  if(encode)dst[tiled]=src[linear];else dst[linear]=src[tiled];
 }
 return SATCHEL_GTL_OK;
}
int satchel_guest_texture_decode(const SatchelGuestTextureLayout *l,const void *storage,size_t sc,void *linear,size_t lc,uint64_t pitch){return move(l,storage,sc,linear,lc,pitch,0);}
int satchel_guest_texture_encode(const SatchelGuestTextureLayout *l,const void *linear,size_t lc,uint64_t pitch,void *storage,size_t sc){return move(l,linear,lc,storage,sc,pitch,1);}
