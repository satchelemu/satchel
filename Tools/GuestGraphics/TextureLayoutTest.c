#include "GuestTextureLayout.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "/tmp/satchel-switch-integration/NX2Apple/dm/gfx/nvn/nvn_tex.h"
#include "/tmp/satchel-switch-integration/NX2Apple/dm/gfx/tex/nvn_deswizzle.h"
static unsigned long long checks;
#define CHECK(x) do{checks++;if(!(x)){fprintf(stderr,"layout assertion line%d\n",__LINE__);exit(1);}}while(0)
static SatchelGuestTextureDescription describe(unsigned w,unsigned h,unsigned flags){return (SatchelGuestTextureDescription){1,sizeof(SatchelGuestTextureDescription),1,0x25,flags,w,h,1,1,1,0};}
static void layout(unsigned w,unsigned h,unsigned flags){
 SatchelGuestTextureDescription d=describe(w,h,flags);SatchelGuestTextureLayout l;CHECK(satchel_guest_texture_layout(&d,&l)==0);
 nvn_texdesc reference={.target=1,.format=0x25,.flags=(int)flags,.samples=1,.width=w,.height=h,.depth=1,.levels=1};nvn_texlayout expected;nvn_tex_layout(&reference,&expected);CHECK(l.storage_size==expected.size && l.alignment==expected.align && l.block_height_log2==expected.tileH);
 uint64_t point=UINT64_MAX;for(unsigned y=0;y<h;y+=(h/9+1))for(unsigned x=0;x<w*4;x+=(w/7+1)){CHECK(satchel_guest_texture_offset(&l,x,y,&point)==0);CHECK(point==(flags?(uint64_t)y*l.row_pitch+x:nvn_bl_offset(x,y,0,w*4,h,l.block_height_log2,0)));}
}
static void roundtrip(unsigned w,unsigned h,unsigned flags){
 SatchelGuestTextureDescription d=describe(w,h,flags);SatchelGuestTextureLayout l;CHECK(!satchel_guest_texture_layout(&d,&l));
 size_t pitch=w*4+16,n=pitch*h,sz=(size_t)l.storage_size;unsigned char *input=malloc(n+32),*output=malloc(n+32),*storage=malloc(sz+32),*touched=calloc(sz,1);CHECK(input && output && storage && touched);memset(input,0xa5,n+32);memset(output,0xc3,n+32);memset(storage,0x7e,sz+32);
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w*4;x++)input[y*pitch+x]=(unsigned char)(x*7+y*11);
 CHECK(!satchel_guest_texture_encode(&l,input,n,pitch,storage,sz));CHECK(!satchel_guest_texture_decode(&l,storage,sz,output,n,pitch));
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w*4;x++){uint64_t at;CHECK(!satchel_guest_texture_offset(&l,x,y,&at));CHECK(at<sz && !touched[at]);touched[at]=1;CHECK(storage[at]==input[y*pitch+x] && output[y*pitch+x]==input[y*pitch+x]);}
 for(size_t i=0;i<sz;i++)if(!touched[i])CHECK(storage[i]==0x7e);
 for(unsigned y=0;y<h;y++)for(size_t x=w*4;x<pitch;x++)CHECK(output[y*pitch+x]==0xc3);
 for(size_t i=0;i<32;i++){CHECK(storage[sz+i]==0x7e);CHECK(output[n+i]==0xc3);}
 unsigned char *old=malloc(sz);CHECK(old);memcpy(old,storage,sz);CHECK(satchel_guest_texture_encode(&l,input,n-17,pitch,storage,sz)==SATCHEL_GTL_INVALID);CHECK(!memcmp(old,storage,sz));CHECK(satchel_guest_texture_decode(&l,storage,sz,storage,sz,pitch)==SATCHEL_GTL_INVALID);CHECK(!memcmp(old,storage,sz));
 free(old);free(input);free(output);free(storage);free(touched);
}
int main(){
 for(unsigned w=1;w<=64;w++)for(unsigned h=1;h<=160;h++){layout(w,h,0);layout(w,h,0x40);}
 const unsigned sizes[][2]={{1,1},{3,7},{17,21},{63,65},{127,129},{1280,720},{1920,1080},{2048,2048}};
 for(unsigned i=0;i<sizeof(sizes)/sizeof(*sizes);i++){layout(sizes[i][0],sizes[i][1],0);roundtrip(sizes[i][0],sizes[i][1],0);if(i<7)roundtrip(sizes[i][0],sizes[i][1],0x40);}
 SatchelGuestTextureDescription d=describe(1920,1080,0);SatchelGuestTextureLayout l,old;CHECK(!satchel_guest_texture_layout(&d,&l));CHECK(l.storage_size==8847360 && l.alignment==512);d=describe(1280,720,0);CHECK(!satchel_guest_texture_layout(&d,&l));CHECK(l.storage_size==3932160);
 for(unsigned kind=0;kind<12;kind++){d=describe(64,64,0);if(kind==0)d.width=0;if(kind==1)d.width=UINT32_MAX;if(kind==2)d.flags=0x20;if(kind==3)d.format=0x38;if(kind==4)d.levels=2;if(kind==5)d.samples=4;if(kind==6)d.stride=256;if(kind==7)d.target=2;if(kind==8)d.depth=2;if(kind==9){d.flags=0x40;d.stride=UINT64_MAX;}if(kind==10){d.flags=0x40;d.stride=128;}if(kind==11){d.flags=0x40;d.stride=257;}memset(&l,0xa5,sizeof(l));old=l;CHECK(satchel_guest_texture_layout(&d,&l)!=0);CHECK(!memcmp(&l,&old,sizeof(l)));}
 d=describe(16,16,0);CHECK(!satchel_guest_texture_layout(&d,&l));l.storage_size++;uint64_t unchanged=17;CHECK(satchel_guest_texture_offset(&l,0,0,&unchanged)==SATCHEL_GTL_INVALID && unchanged==17);
 printf("Texture layout: %llu sanitizer assertions, public pinned layout comparisons,720p/1080p block-linear bijection/roundtrip and padding preservation. No guest draw claim.\n",checks);return 0;
}
