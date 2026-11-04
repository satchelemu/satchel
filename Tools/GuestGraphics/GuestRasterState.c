#include "GuestRasterState.h"
#include <limits.h>
#include <string.h>

static _Thread_local int active;
static uint64_t load(const uint8_t *p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)p[i]<<(i*8);return v;}
static void store(uint8_t *p,unsigned n,uint64_t v){for(unsigned i=0;i<n;i++)p[i]=(uint8_t)(v>>(i*8));}
static void bits(uint64_t *v,unsigned shift,unsigned width,uint64_t x){uint64_t m=((UINT64_C(1)<<width)-1)<<shift;*v=(*v&~m)|((x<<shift)&m);}
static int compare(uint32_t v){return v>=1 && v<=8;}
static int samples(uint32_t v){return v==0 || v==1 || v==2 || v==4 || v==8 || v==16;}
static uint32_t boolean(uint64_t v){return (uint8_t)v!=0;}
uint32_t satchel_guest_nvn_state_size(uint32_t k){switch(k){case SATCHEL_GNS_COLOR:case SATCHEL_GNS_CHANNEL:case SATCHEL_GNS_POLYGON:case SATCHEL_GNS_VERTEX_ATTRIBUTE:return 4;case SATCHEL_GNS_DEPTH_STENCIL:case SATCHEL_GNS_VERTEX_STREAM:return 8;case SATCHEL_GNS_MULTISAMPLE:return 24;default:return 0;}}
SatchelGuestNVNStateStatus satchel_guest_nvn_state_decode(uint32_t k,const void *input,size_t n,SatchelGuestNVNStateDecoded *out){
 if(!input || !out || !satchel_guest_nvn_state_size(k) || n!=satchel_guest_nvn_state_size(k))return SATCHEL_GNS_INVALID;
 const uint8_t *b=input;uint64_t v=load(b,n<8?(unsigned)n:8);SatchelGuestNVNStateDecoded d={.kind=k};uint32_t *f=d.fields;
 switch(k){
 case SATCHEL_GNS_COLOR:if(v>>16)return SATCHEL_GNS_UNSUPPORTED;f[0]=v&255;f[1]=(v>>8)&15;f[2]=(v>>12)&15;if(!compare(f[2]))return SATCHEL_GNS_UNSUPPORTED;break;
 case SATCHEL_GNS_CHANNEL:f[0]=(uint32_t)v;break;
 case SATCHEL_GNS_POLYGON:if(v>>8)return SATCHEL_GNS_UNSUPPORTED;f[0]=v&3;f[1]=(v>>2)&1;f[2]=(v>>3)&3;f[3]=(v>>5)&7;if(f[2]>2)return SATCHEL_GNS_UNSUPPORTED;break;
 case SATCHEL_GNS_DEPTH_STENCIL:if(v>>39)return SATCHEL_GNS_UNSUPPORTED;f[0]=v&1;f[1]=(v>>1)&1;f[2]=(v>>2)&15;f[3]=(v>>6)&1;for(unsigned i=4;i<12;i++)f[i]=(v>>(7+4*(i-4)))&15;for(unsigned i=2;i<12;i++)if(i!=3 && !compare(f[i]))return SATCHEL_GNS_UNSUPPORTED;break;
 case SATCHEL_GNS_MULTISAMPLE:for(unsigned i=0;i<10;i++)f[i]=b[i];for(unsigned i=10;i<24;i++)if(b[i])return SATCHEL_GNS_UNSUPPORTED;
  if(f[0]>1 || !samples(f[1]) || f[2]>1 || f[3]>1 || !samples(f[4]) || f[5]>3 || f[6]>1 || f[7]>7 || f[8]>1 || f[9]>1)return SATCHEL_GNS_UNSUPPORTED;break;
 case SATCHEL_GNS_VERTEX_ATTRIBUTE:f[0]=v&255;f[1]=(v>>8)&0x7ffff;f[2]=(v>>27)&31;if(f[0]>0x9c)return SATCHEL_GNS_UNSUPPORTED;break;
 case SATCHEL_GNS_VERTEX_STREAM:f[0]=(uint32_t)v;f[1]=(uint32_t)(v>>32);if(f[1]>INT32_MAX)return SATCHEL_GNS_UNSUPPORTED;break;
 default:return SATCHEL_GNS_INVALID;
 }
 *out=d;return SATCHEL_GNS_OK;
}
static uint32_t kind(uint32_t op,int *defaults){*defaults=0;switch(op){
 case SATCHEL_GNS_COLOR_DEFAULTS:*defaults=1;return SATCHEL_GNS_COLOR;case SATCHEL_GNS_COLOR_BLEND:case SATCHEL_GNS_COLOR_LOGIC:case SATCHEL_GNS_COLOR_ALPHA:return SATCHEL_GNS_COLOR;
 case SATCHEL_GNS_CHANNEL_DEFAULTS:*defaults=1;return SATCHEL_GNS_CHANNEL;case SATCHEL_GNS_CHANNEL_MASK:return SATCHEL_GNS_CHANNEL;
 case SATCHEL_GNS_POLYGON_DEFAULTS:*defaults=1;return SATCHEL_GNS_POLYGON;case SATCHEL_GNS_POLYGON_CULL:case SATCHEL_GNS_POLYGON_FRONT:case SATCHEL_GNS_POLYGON_MODE:case SATCHEL_GNS_POLYGON_OFFSET:return SATCHEL_GNS_POLYGON;
 case SATCHEL_GNS_DEPTH_DEFAULTS:*defaults=1;return SATCHEL_GNS_DEPTH_STENCIL;case SATCHEL_GNS_DEPTH_TEST:case SATCHEL_GNS_DEPTH_WRITE:case SATCHEL_GNS_DEPTH_FUNC:case SATCHEL_GNS_STENCIL_TEST:case SATCHEL_GNS_STENCIL_FUNC:case SATCHEL_GNS_STENCIL_OP:return SATCHEL_GNS_DEPTH_STENCIL;
 case SATCHEL_GNS_MULTISAMPLE_DEFAULTS:*defaults=1;return SATCHEL_GNS_MULTISAMPLE;case SATCHEL_GNS_MULTISAMPLE_ENABLE:case SATCHEL_GNS_MULTISAMPLE_SAMPLES:case SATCHEL_GNS_MULTISAMPLE_ALPHA_COVERAGE:case SATCHEL_GNS_MULTISAMPLE_ALPHA_DITHER:case SATCHEL_GNS_MULTISAMPLE_RASTER_SAMPLES:case SATCHEL_GNS_MULTISAMPLE_COVERAGE_MODE:case SATCHEL_GNS_MULTISAMPLE_COVERAGE_COLOR:case SATCHEL_GNS_MULTISAMPLE_COVERAGE_OUTPUT:case SATCHEL_GNS_MULTISAMPLE_LOCATIONS_ENABLE:case SATCHEL_GNS_MULTISAMPLE_GRID_ENABLE:return SATCHEL_GNS_MULTISAMPLE;
 case SATCHEL_GNS_ATTRIBUTE_DEFAULTS:*defaults=1;return SATCHEL_GNS_VERTEX_ATTRIBUTE;case SATCHEL_GNS_ATTRIBUTE_FORMAT:case SATCHEL_GNS_ATTRIBUTE_STREAM:return SATCHEL_GNS_VERTEX_ATTRIBUTE;
 case SATCHEL_GNS_STREAM_DEFAULTS:*defaults=1;return SATCHEL_GNS_VERTEX_STREAM;case SATCHEL_GNS_STREAM_STRIDE:case SATCHEL_GNS_STREAM_DIVISOR:return SATCHEL_GNS_VERTEX_STREAM;
 default:return 0;
}}
static SatchelGuestNVNStateResult apply(SatchelCPUProbe *cpu,const SatchelGuestNVNStateRequest *r,SatchelGuestNVNStateGuard guard,void *context){
 SatchelGuestNVNStateResult result={.status=SATCHEL_GNS_INVALID};if(!cpu || !r || !guard)return result;
 const SatchelGuestNVNStateRequest request=*r;r=&request;int defaults;uint32_t k=kind(r->operation,&defaults),n=satchel_guest_nvn_state_size(k);if(!n){result.status=SATCHEL_GNS_UNSUPPORTED;return result;}
 result.status=SATCHEL_GNS_MEMORY;if(!r->x[0] || r->x[0]>UINT64_MAX-n || !satchel_cpu_probe_check_range(cpu,r->x[0],n,defaults?SATCHEL_CPU_WRITE:SATCHEL_CPU_READ|SATCHEL_CPU_WRITE) || !guard(context,cpu,r->x[0],n))return result;
 uint8_t b[24]={0};if(!defaults && !satchel_cpu_probe_read(cpu,r->x[0],b,n))return result;
 uint64_t v=load(b,n<8?n:8);uint32_t a=(uint32_t)r->x[1],a2=(uint32_t)r->x[2];result.status=SATCHEL_GNS_UNSUPPORTED;
 switch(r->operation){
 case SATCHEL_GNS_COLOR_DEFAULTS:v=UINT64_C(0x8300);break;
 case SATCHEL_GNS_COLOR_BLEND:if(a>7)return result;bits(&v,a,1,boolean(r->x[2]));break;
 case SATCHEL_GNS_COLOR_LOGIC:if(a>15)return result;bits(&v,8,4,a);break;
 case SATCHEL_GNS_COLOR_ALPHA:if(!compare(a))return result;bits(&v,12,4,a);break;
 case SATCHEL_GNS_CHANNEL_DEFAULTS:v=UINT32_MAX;break;
 case SATCHEL_GNS_CHANNEL_MASK:if(a>7)return result;bits(&v,a*4,4,boolean(r->x[2])|(boolean(r->x[3])<<1)|(boolean(r->x[4])<<2)|(boolean(r->x[5])<<3));break;
 case SATCHEL_GNS_POLYGON_DEFAULTS:v=20;break;
 case SATCHEL_GNS_POLYGON_CULL:if(a>3)return result;bits(&v,0,2,a);break;
 case SATCHEL_GNS_POLYGON_FRONT:if(a>1)return result;bits(&v,2,1,a);break;
 case SATCHEL_GNS_POLYGON_MODE:if(a>2)return result;bits(&v,3,2,a);break;
 case SATCHEL_GNS_POLYGON_OFFSET:if(a>7)return result;bits(&v,5,3,a);break;
 case SATCHEL_GNS_DEPTH_DEFAULTS:v=2|(UINT64_C(2)<<2)|(UINT64_C(8)<<7)|(UINT64_C(8)<<11);for(unsigned i=0;i<6;i++)v|=UINT64_C(1)<<(15+4*i);break;
 case SATCHEL_GNS_DEPTH_TEST:bits(&v,0,1,boolean(r->x[1]));break;
 case SATCHEL_GNS_DEPTH_WRITE:bits(&v,1,1,boolean(r->x[1]));break;
 case SATCHEL_GNS_DEPTH_FUNC:if(!compare(a))return result;bits(&v,2,4,a);break;
 case SATCHEL_GNS_STENCIL_TEST:bits(&v,6,1,boolean(r->x[1]));break;
 case SATCHEL_GNS_STENCIL_FUNC:if(a>3 || !compare(a2))return result;if(a&1)bits(&v,7,4,a2);if(a&2)bits(&v,11,4,a2);break;
 case SATCHEL_GNS_STENCIL_OP:if(a>3 || !compare(a2) || !compare((uint32_t)r->x[3]) || !compare((uint32_t)r->x[4]))return result;for(unsigned face=0;face<2;face++)if(a&(1u<<face))for(unsigned i=0;i<3;i++)bits(&v,15+12*face+4*i,4,(uint32_t)r->x[2+i]);break;
 case SATCHEL_GNS_MULTISAMPLE_DEFAULTS:b[0]=1;break;
 case SATCHEL_GNS_MULTISAMPLE_ENABLE:b[0]=boolean(r->x[1]);break;
 case SATCHEL_GNS_MULTISAMPLE_SAMPLES:if(!samples(a))return result;b[1]=(uint8_t)a;break;
 case SATCHEL_GNS_MULTISAMPLE_ALPHA_COVERAGE:b[2]=boolean(r->x[1]);break;
 case SATCHEL_GNS_MULTISAMPLE_ALPHA_DITHER:b[3]=boolean(r->x[1]);break;
 case SATCHEL_GNS_MULTISAMPLE_RASTER_SAMPLES:if(!samples(a))return result;b[4]=(uint8_t)a;break;
 case SATCHEL_GNS_MULTISAMPLE_COVERAGE_MODE:if(a>3)return result;b[5]=(uint8_t)a;break;
 case SATCHEL_GNS_MULTISAMPLE_COVERAGE_COLOR:b[6]=boolean(r->x[1]);break;
 case SATCHEL_GNS_MULTISAMPLE_COVERAGE_OUTPUT:if(a>7)return result;b[7]=(uint8_t)a;break;
 case SATCHEL_GNS_MULTISAMPLE_LOCATIONS_ENABLE:b[8]=boolean(r->x[1]);break;
 case SATCHEL_GNS_MULTISAMPLE_GRID_ENABLE:b[9]=boolean(r->x[1]);break;
 case SATCHEL_GNS_ATTRIBUTE_DEFAULTS:case SATCHEL_GNS_STREAM_DEFAULTS:v=0;break;
 case SATCHEL_GNS_ATTRIBUTE_FORMAT:if(a>0x9c || r->x[2]>0x7ffff)return result;bits(&v,0,8,a);bits(&v,8,19,r->x[2]);break;
 case SATCHEL_GNS_ATTRIBUTE_STREAM:if(a>31)return result;bits(&v,27,5,a);break;
 case SATCHEL_GNS_STREAM_STRIDE:if(r->x[1]>UINT32_MAX)return result;bits(&v,0,32,r->x[1]);break;
 case SATCHEL_GNS_STREAM_DIVISOR:if(a>INT32_MAX)return result;bits(&v,32,32,a);break;
 default:return result;
 }
 if(k!=SATCHEL_GNS_MULTISAMPLE)store(b,n,v);SatchelGuestNVNStateDecoded decoded;result.status=satchel_guest_nvn_state_decode(k,b,n,&decoded);if(result.status)return result;
 const SatchelCPUProbeWrite write={r->x[0],b,n};SatchelCPUWriteResult status=satchel_cpu_probe_writev(cpu,&write,1);result.status=status==SATCHEL_CPU_WRITE_OK?SATCHEL_GNS_OK:status==SATCHEL_CPU_WRITE_BUSY?SATCHEL_GNS_BUSY:status==SATCHEL_CPU_WRITE_ALLOCATION?SATCHEL_GNS_ALLOCATION:SATCHEL_GNS_MEMORY;return result;
}
SatchelGuestNVNStateResult satchel_guest_nvn_state_apply(SatchelCPUProbe *cpu,const SatchelGuestNVNStateRequest *request,SatchelGuestNVNStateGuard guard,void *context){
 if(active)return (SatchelGuestNVNStateResult){.status=SATCHEL_GNS_BUSY};active=1;SatchelGuestNVNStateResult result=apply(cpu,request,guard,context);active=0;return result;
}
