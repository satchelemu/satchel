#ifndef SATCHEL_RASTER_ORIGINAL_FIXTURES_H
#define SATCHEL_RASTER_ORIGINAL_FIXTURES_H
#include "RasterCompiler.h"
#include <string.h>
/* Original test encoder, using public field positions; no game shader data.
 * Instruction prefixes come from pinned Ryujinx InstTable, fields from the
 * independent InstDecoders description, SPH masks from NVIDIA cla097sph.h. */
typedef struct {uint8_t sph[80],code[SATCHEL_RASTER_CODE_BYTES];size_t cursor;} RasterFixture;
static void fixture32(uint8_t *p,uint32_t x){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(x>>(i*8));}
static void fixture64(uint8_t *p,uint64_t x){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(x>>(i*8));}
static void fixture_init(RasterFixture *f,unsigned stage){memset(f,0,sizeof(*f));fixture32(f->sph,(stage==1?1u:2u)|(3u<<5)|(stage<<10)|(3u<<17));fixture32(f->sph+16,0xffu<<12);fixture32(f->sph+(stage==1?52:72),stage==1?0xf0000000u:15u);}
static void fixture_op(RasterFixture *f,uint64_t op){if(!(f->cursor%32))f->cursor+=8;fixture64(f->code+f->cursor,op);f->cursor+=8;}
static void fixture_mov(RasterFixture *f,unsigned d,unsigned a){fixture_op(f,(UINT64_C(0x0b93)<<51)|((uint64_t)15<<39)|((uint64_t)a<<20)|(7u<<16)|d);}
static void fixture_imm(RasterFixture *f,unsigned d,uint32_t bits){fixture_op(f,(UINT64_C(0x10)<<52)|((uint64_t)bits<<20)|(7u<<16)|(15u<<12)|d);}
static void fixture_ald(RasterFixture *f,unsigned d,unsigned location,unsigned component,unsigned n){unsigned off=0x80+location*16+component*4;fixture_op(f,(UINT64_C(0x1dfb)<<51)|((uint64_t)(n-1)<<47)|((uint64_t)255<<39)|((uint64_t)off<<20)|(7u<<16)|(255u<<8)|d);for(unsigned i=0;i<n;i++){unsigned bit=location*4+component+i;f->sph[24+bit/8]|=1u<<(bit%8);}}
static void fixture_ast(RasterFixture *f,unsigned src,unsigned component,unsigned n){unsigned off=0x70+component*4;fixture_op(f,(UINT64_C(0x1dfe)<<51)|((uint64_t)(n-1)<<47)|((uint64_t)255<<39)|((uint64_t)off<<20)|(7u<<16)|(255u<<8)|src);}
static void fixture_exit(RasterFixture *f){fixture_op(f,UINT64_C(0xe30000000007000f));}
static SatchelRasterStageInput fixture_input(RasterFixture *f,unsigned stage){SatchelRasterStageInput in={.version=1,.size=sizeof(SatchelRasterStageInput),.stage=stage,.state={.known=255,.single_vertex_program=1,.viewport_transform_enabled=1,.depth_zero_to_one=1,.origin_upper_left=1,.alpha_test_always=1},.sph=f->sph,.sph_size=80,.code=f->code,.code_size=f->cursor};return in;}
#endif
