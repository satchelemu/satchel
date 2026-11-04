/* TEST ONLY: independent host C bitfield layout from the exact installed pin.
 * Production uses explicit byte encoding and does not include this header. */
#include "GuestRasterState.h"
#include "nvn_obj.h"
_Static_assert(sizeof(NVNcolorState)==4 && sizeof(NVNchannelMaskState)==4 && sizeof(NVNpolygonState)==4,"pinned state ABI");
_Static_assert(sizeof(NVNdepthStencilState)==8 && sizeof(NVNmultisampleState)==24 && sizeof(NVNvertexAttribState)==4 && sizeof(NVNvertexStreamState)==8,"pinned state ABI");
_Static_assert(NVN_LOGIC_OP_COPY==3 && NVN_ALPHA_FUNC_ALWAYS==8 && NVN_FRONT_FACE_CCW==1 && NVN_POLYGON_MODE_FILL==2 && NVN_FORMAT_RGBA32F==46,"pinned values");
void oracle(uint8_t b[24],const SatchelGuestNVNStateRequest *r){uint32_t a=r->x[1],z=r->x[2];
 color_st c;chmask_st cm;poly_st p;ds_st d;ms_st m;va_st va;vs_st vs;
 memcpy(&c,b,sizeof c);memcpy(&cm,b,sizeof cm);memcpy(&p,b,sizeof p);memcpy(&d,b,sizeof d);memcpy(&m,b,sizeof m);memcpy(&va,b,sizeof va);memcpy(&vs,b,sizeof vs);
 switch(r->operation){
 case SATCHEL_GNS_COLOR_DEFAULTS:c=(color_st){0,NVN_LOGIC_OP_COPY,NVN_ALPHA_FUNC_ALWAYS};break;
 case SATCHEL_GNS_COLOR_BLEND:if((uint8_t)z)c.blend_en|=1u<<a;else c.blend_en&=~(1u<<a);break;
 case SATCHEL_GNS_COLOR_LOGIC:c.logic_op=a;break;case SATCHEL_GNS_COLOR_ALPHA:c.alpha_func=a;break;
 case SATCHEL_GNS_CHANNEL_DEFAULTS:cm.mask=UINT32_MAX;break;
 case SATCHEL_GNS_CHANNEL_MASK:{uint32_t mask=((uint8_t)r->x[2]!=0)|(((uint8_t)r->x[3]!=0)<<1)|(((uint8_t)r->x[4]!=0)<<2)|(((uint8_t)r->x[5]!=0)<<3);cm.mask=(cm.mask&~(15u<<(a*4)))|(mask<<(a*4));break;}
 case SATCHEL_GNS_POLYGON_DEFAULTS:p=(poly_st){NVN_FACE_NONE,NVN_FRONT_FACE_CCW,NVN_POLYGON_MODE_FILL,0};break;
 case SATCHEL_GNS_POLYGON_CULL:p.cull=a;break;case SATCHEL_GNS_POLYGON_FRONT:p.front=a;break;case SATCHEL_GNS_POLYGON_MODE:p.mode=a;break;case SATCHEL_GNS_POLYGON_OFFSET:p.offset_en=a;break;
 case SATCHEL_GNS_DEPTH_DEFAULTS:d=(ds_st){0,1,NVN_DEPTH_FUNC_LESS,0,NVN_STENCIL_FUNC_ALWAYS,NVN_STENCIL_FUNC_ALWAYS,NVN_STENCIL_OP_KEEP,NVN_STENCIL_OP_KEEP,NVN_STENCIL_OP_KEEP,NVN_STENCIL_OP_KEEP,NVN_STENCIL_OP_KEEP,NVN_STENCIL_OP_KEEP};break;
 case SATCHEL_GNS_DEPTH_TEST:d.dt=(uint8_t)a!=0;break;case SATCHEL_GNS_DEPTH_WRITE:d.dw=(uint8_t)a!=0;break;case SATCHEL_GNS_DEPTH_FUNC:d.df=a;break;case SATCHEL_GNS_STENCIL_TEST:d.st=(uint8_t)a!=0;break;
 case SATCHEL_GNS_STENCIL_FUNC:if(a&NVN_FACE_FRONT)d.sf_f=z;if(a&NVN_FACE_BACK)d.sf_b=z;break;
 case SATCHEL_GNS_STENCIL_OP:if(a&NVN_FACE_FRONT){d.fail_f=z;d.zfail_f=r->x[3];d.zpass_f=r->x[4];}if(a&NVN_FACE_BACK){d.fail_b=z;d.zfail_b=r->x[3];d.zpass_b=r->x[4];}break;
 case SATCHEL_GNS_MULTISAMPLE_DEFAULTS:memset(b,0,24);memset(&m,0,sizeof m);m.enable=1;break;
 case SATCHEL_GNS_MULTISAMPLE_ENABLE:m.enable=(uint8_t)a!=0;break;case SATCHEL_GNS_MULTISAMPLE_SAMPLES:m.samples=a;break;case SATCHEL_GNS_MULTISAMPLE_ALPHA_COVERAGE:m.a2c=(uint8_t)a!=0;break;case SATCHEL_GNS_MULTISAMPLE_ALPHA_DITHER:m.a2c_dither=(uint8_t)a!=0;break;case SATCHEL_GNS_MULTISAMPLE_RASTER_SAMPLES:m.raster_samples=a;break;case SATCHEL_GNS_MULTISAMPLE_COVERAGE_MODE:m.cov_mod=a;break;case SATCHEL_GNS_MULTISAMPLE_COVERAGE_COLOR:m.cov2color_en=(uint8_t)a!=0;break;case SATCHEL_GNS_MULTISAMPLE_COVERAGE_OUTPUT:m.cov2color_out=a;break;case SATCHEL_GNS_MULTISAMPLE_LOCATIONS_ENABLE:m.sl_en=(uint8_t)a!=0;break;case SATCHEL_GNS_MULTISAMPLE_GRID_ENABLE:m.sl_grid=(uint8_t)a!=0;break;
 case SATCHEL_GNS_ATTRIBUTE_DEFAULTS:va=(va_st){0};break;case SATCHEL_GNS_ATTRIBUTE_FORMAT:va.fmt=a;va.offset=r->x[2];break;case SATCHEL_GNS_ATTRIBUTE_STREAM:va.stream=a;break;
 case SATCHEL_GNS_STREAM_DEFAULTS:vs=(vs_st){0};break;case SATCHEL_GNS_STREAM_STRIDE:vs.stride=r->x[1];break;case SATCHEL_GNS_STREAM_DIVISOR:vs.divisor=a;break;
 }
 unsigned op=r->operation;if(op<=SATCHEL_GNS_COLOR_ALPHA)memcpy(b,&c,sizeof c);else if(op<=SATCHEL_GNS_CHANNEL_MASK)memcpy(b,&cm,sizeof cm);else if(op<=SATCHEL_GNS_POLYGON_OFFSET)memcpy(b,&p,sizeof p);else if(op<=SATCHEL_GNS_STENCIL_OP)memcpy(b,&d,sizeof d);else if(op<=SATCHEL_GNS_MULTISAMPLE_GRID_ENABLE)memcpy(b,&m,sizeof m);else if(op<=SATCHEL_GNS_ATTRIBUTE_STREAM)memcpy(b,&va,sizeof va);else memcpy(b,&vs,sizeof vs);
}
void oracle_decode(unsigned kind,const uint8_t b[24],SatchelGuestNVNStateDecoded *out){SatchelGuestNVNStateDecoded r={.kind=kind};uint32_t *f=r.fields;
 switch(kind){case SATCHEL_GNS_COLOR:{color_st s;memcpy(&s,b,sizeof s);f[0]=s.blend_en;f[1]=s.logic_op;f[2]=s.alpha_func;break;}
 case SATCHEL_GNS_CHANNEL:{chmask_st s;memcpy(&s,b,sizeof s);f[0]=s.mask;break;}
 case SATCHEL_GNS_POLYGON:{poly_st s;memcpy(&s,b,sizeof s);f[0]=s.cull;f[1]=s.front;f[2]=s.mode;f[3]=s.offset_en;break;}
 case SATCHEL_GNS_DEPTH_STENCIL:{ds_st s;memcpy(&s,b,sizeof s);uint32_t values[]={s.dt,s.dw,s.df,s.st,s.sf_f,s.sf_b,s.fail_f,s.zfail_f,s.zpass_f,s.fail_b,s.zfail_b,s.zpass_b};memcpy(f,values,sizeof values);break;}
 case SATCHEL_GNS_MULTISAMPLE:{ms_st s;memcpy(&s,b,sizeof s);uint32_t values[]={s.enable,s.samples,s.a2c,s.a2c_dither,s.raster_samples,s.cov_mod,s.cov2color_en,s.cov2color_out,s.sl_en,s.sl_grid};memcpy(f,values,sizeof values);break;}
 case SATCHEL_GNS_VERTEX_ATTRIBUTE:{va_st s;memcpy(&s,b,sizeof s);f[0]=s.fmt;f[1]=s.offset;f[2]=s.stream;break;}
 case SATCHEL_GNS_VERTEX_STREAM:{vs_st s;memcpy(&s,b,sizeof s);f[0]=s.stride;f[1]=s.divisor;break;}}
 *out=r;
}
