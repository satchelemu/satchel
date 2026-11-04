#ifndef SATCHEL_GUEST_NVN_RASTER_STATE_H
#define SATCHEL_GUEST_NVN_RASTER_STATE_H
#include "CPUProbe.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef enum {SATCHEL_GNS_OK,SATCHEL_GNS_INVALID,SATCHEL_GNS_MEMORY,SATCHEL_GNS_UNSUPPORTED,
 SATCHEL_GNS_BUSY,SATCHEL_GNS_ALLOCATION} SatchelGuestNVNStateStatus;
typedef enum {SATCHEL_GNS_COLOR=1,SATCHEL_GNS_CHANNEL,SATCHEL_GNS_POLYGON,SATCHEL_GNS_DEPTH_STENCIL,
 SATCHEL_GNS_MULTISAMPLE,SATCHEL_GNS_VERTEX_ATTRIBUTE,SATCHEL_GNS_VERTEX_STREAM} SatchelGuestNVNStateKind;
typedef enum {
 SATCHEL_GNS_COLOR_DEFAULTS=1,SATCHEL_GNS_COLOR_BLEND,SATCHEL_GNS_COLOR_LOGIC,SATCHEL_GNS_COLOR_ALPHA,
 SATCHEL_GNS_CHANNEL_DEFAULTS,SATCHEL_GNS_CHANNEL_MASK,
 SATCHEL_GNS_POLYGON_DEFAULTS,SATCHEL_GNS_POLYGON_CULL,SATCHEL_GNS_POLYGON_FRONT,SATCHEL_GNS_POLYGON_MODE,SATCHEL_GNS_POLYGON_OFFSET,
 SATCHEL_GNS_DEPTH_DEFAULTS,SATCHEL_GNS_DEPTH_TEST,SATCHEL_GNS_DEPTH_WRITE,SATCHEL_GNS_DEPTH_FUNC,SATCHEL_GNS_STENCIL_TEST,SATCHEL_GNS_STENCIL_FUNC,SATCHEL_GNS_STENCIL_OP,
 SATCHEL_GNS_MULTISAMPLE_DEFAULTS,SATCHEL_GNS_MULTISAMPLE_ENABLE,SATCHEL_GNS_MULTISAMPLE_SAMPLES,SATCHEL_GNS_MULTISAMPLE_ALPHA_COVERAGE,
 SATCHEL_GNS_MULTISAMPLE_ALPHA_DITHER,SATCHEL_GNS_MULTISAMPLE_RASTER_SAMPLES,SATCHEL_GNS_MULTISAMPLE_COVERAGE_MODE,
 SATCHEL_GNS_MULTISAMPLE_COVERAGE_COLOR,SATCHEL_GNS_MULTISAMPLE_COVERAGE_OUTPUT,SATCHEL_GNS_MULTISAMPLE_LOCATIONS_ENABLE,SATCHEL_GNS_MULTISAMPLE_GRID_ENABLE,
 SATCHEL_GNS_ATTRIBUTE_DEFAULTS,SATCHEL_GNS_ATTRIBUTE_FORMAT,SATCHEL_GNS_ATTRIBUTE_STREAM,
 SATCHEL_GNS_STREAM_DEFAULTS,SATCHEL_GNS_STREAM_STRIDE,SATCHEL_GNS_STREAM_DIVISOR
} SatchelGuestNVNStateOperation;
/* Actual AAPCS argument snapshot: x0 object address, x1..5 setter arguments.
 * NVNboolean uses the low byte; int/enums use the low32 bits; ptrdiff_t uses64.
 * All current operations are void: callers preserve X0. No CPU register writes. */
typedef struct {uint32_t operation;uint64_t x[6];} SatchelGuestNVNStateRequest;
typedef struct {uint32_t status;uint64_t value;} SatchelGuestNVNStateResult;
/* Pure synchronous protection check, required. Must not mutate/reenter, and must
 * check current PHYSICAL overlap with protected owner ranges, failing on holes.
 * CPU/context are borrowed only for this call. Caller serializes all owners and
 * validates the guest return PC BEFORE calling this mutation API. */
typedef int (*SatchelGuestNVNStateGuard)(void *,SatchelCPUProbe *,uint64_t,uint64_t);
SatchelGuestNVNStateResult satchel_guest_nvn_state_apply(SatchelCPUProbe *,const SatchelGuestNVNStateRequest *,SatchelGuestNVNStateGuard,void *);
/* Pure bounded decoding of the emulator's documented opaque layout, not a
 * hardware/Nintendo object layout. No pointers retained; failure leaves output.
 * Fields: Color[blend,logic,alpha]; Channel[mask]; Polygon[cull,front,mode,offset];
 * DepthStencil[dt,dw,df,st,sf_front,sf_back,fail_front,zfail_front,zpass_front,
 * fail_back,zfail_back,zpass_back]; Multisample[enable,samples,a2c,dither,
 * raster_samples,coverage_mode,coverage_color,coverage_output,locations,grid];
 * Attribute[format,offset,stream]; Stream[stride,divisor]. Unused fields zero.
 * Known non-rasterized settings remain representable; Draw must validate its
 * narrower supported profile. Decoding is not rendering/pipeline support. */
typedef struct {uint32_t kind;uint32_t fields[12];} SatchelGuestNVNStateDecoded;
uint32_t satchel_guest_nvn_state_size(uint32_t kind);
SatchelGuestNVNStateStatus satchel_guest_nvn_state_decode(uint32_t kind,const void *,size_t,SatchelGuestNVNStateDecoded *);
#ifdef __cplusplus
}
#endif
#endif
