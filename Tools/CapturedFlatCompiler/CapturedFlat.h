#ifndef SATCHEL_CAPTURED_FLAT_H
#define SATCHEL_CAPTURED_FLAT_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { SATCHEL_CF_OK=0,SATCHEL_CF_INVALID=1,SATCHEL_CF_UNSUPPORTED=2,SATCHEL_CF_LIMIT=3,SATCHEL_CF_CANCELLED=4,SATCHEL_CF_DEADLINE=5,SATCHEL_CF_BUSY=6,SATCHEL_CF_INTERNAL=7 };
enum { SATCHEL_CF_WORDS=4096,SATCHEL_CF_IDS=256,SATCHEL_CF_SPH_BYTES=80,SATCHEL_CF_CODE_BYTES=1024 };
/* Caller-owned host snapshots, not guest pointers. Fields must describe actual
 * emulator-owned state/caps. No inferred defaults. All storage must remain valid
 * for the synchronous call; output may not overlap input or source spans. */
typedef struct { const uint8_t *sph,*code; uint32_t sph_size,code_size; } SatchelCFSource;
typedef struct {
 uint32_t known,single_vertex_program,viewport_transform_enabled,depth_zero_to_one,y_negate,origin_upper_left,alpha_test_always,alpha_to_coverage,transform_feedback;
 /* extra_known bits0..11 cover the following twelve scalar facts. */
 uint32_t extra_known,topology,early_z,alpha_dither,program_point_size,point_size_bits,alpha_reference_bits,draw_parameters,dual_source_blend,halve_primitive_id,float_vertex_mask,float_output_mask,provoking_mode;
} SatchelCFState;
/* Boolean host-query bits are enumerated below. Unknown is never false. */
#define SATCHEL_CF_HOST_QUERYHOSTREDUCEDPRECISION (UINT64_C(1)<<0)
#define SATCHEL_CF_HOST_QUERYHOSTHASFRONTFACINGBUG (UINT64_C(1)<<1)
#define SATCHEL_CF_HOST_QUERYHOSTHASVECTORINDEXINGBUG (UINT64_C(1)<<2)
#define SATCHEL_CF_HOST_QUERYHOSTSUPPORTSBGRAFORMAT (UINT64_C(1)<<3)
#define SATCHEL_CF_HOST_QUERYHOSTSUPPORTSFRAGMENTSHADERINTERLOCK (UINT64_C(1)<<4)
#define SATCHEL_CF_HOST_QUERYHOSTSUPPORTSFRAGMENTSHADERORDERINGINTEL (UINT64_C(1)<<5)
#define SATCHEL_CF_HOST_QUERYHOSTSUPPORTSGEOMETRYSHADER (UINT64_C(1)<<6)
#define SATCHEL_CF_HOST_QUERYHOSTSUPPORTSGEOMETRYSHADERPASSTHROUGH (UINT64_C(1)<<7)
#define SATCHEL_CF_HOST_QUERYHOSTSUPPORTSIMAGELOADFORMATTED (UINT64_C(1)<<8)
#define SATCHEL_CF_HOST_QUERYHOSTSUPPORTSLAYERVERTEXTESSELLATION (UINT64_C(1)<<9)
#define SATCHEL_CF_HOST_QUERYHOSTSUPPORTSNONCONSTANTTEXTUREOFFSET (UINT64_C(1)<<10)
#define SATCHEL_CF_HOST_QUERYHOSTSUPPORTSSCALEDVERTEXFORMATS (UINT64_C(1)<<11)
#define SATCHEL_CF_HOST_QUERYHOSTSUPPORTSSEPARATESAMPLER (UINT64_C(1)<<12)
#define SATCHEL_CF_HOST_QUERYHOSTSUPPORTSSHADERBALLOT (UINT64_C(1)<<13)
#define SATCHEL_CF_HOST_QUERYHOSTSUPPORTSSHADERBARRIERDIVERGENCE (UINT64_C(1)<<14)
#define SATCHEL_CF_HOST_QUERYHOSTSUPPORTSSHADERFLOAT64 (UINT64_C(1)<<15)
#define SATCHEL_CF_HOST_QUERYHOSTSUPPORTSSHADERNONUNIFORMINDEXING (UINT64_C(1)<<16)
#define SATCHEL_CF_HOST_QUERYHOSTSUPPORTSSNORMBUFFERTEXTUREFORMAT (UINT64_C(1)<<17)
#define SATCHEL_CF_HOST_QUERYHOSTSUPPORTSTEXTUREGATHEROFFSETS (UINT64_C(1)<<18)
#define SATCHEL_CF_HOST_QUERYHOSTSUPPORTSTEXTURESHADOWLOD (UINT64_C(1)<<19)
#define SATCHEL_CF_HOST_QUERYHOSTSUPPORTSTRANSFORMFEEDBACK (UINT64_C(1)<<20)
#define SATCHEL_CF_HOST_QUERYHOSTSUPPORTSVIEWPORTINDEXVERTEXTESSELLATION (UINT64_C(1)<<21)
#define SATCHEL_CF_HOST_QUERYHOSTSUPPORTSVIEWPORTMASK (UINT64_C(1)<<22)
#define SATCHEL_CF_HOST_QUERYHOSTSUPPORTSDEPTHCLIPCONTROL (UINT64_C(1)<<23)
#define SATCHEL_CF_HOST_ALL UINT64_C(16777215)
typedef struct { uint64_t known,values;uint32_t storage_alignment,physical_bgra8_color_attachment,admitted_color_format,current_color_format; } SatchelCFHost;
typedef int (*SatchelCFIsCancelled)(void *context);
typedef struct {
 uint32_t version,size;SatchelCFSource vertex,fragment;SatchelCFState state;SatchelCFHost host;
 uint32_t max_work,timeout_ns;SatchelCFIsCancelled cancelled;void *context;
 uint32_t reserved[4];
} SatchelCFInput;
/* Layout identical to renderer SatchelVulkanFlatModule, but renderer must independently
 * validate types/interfaces/resources. Outputs are actual translator bytes. */
typedef struct {uint32_t version,size,word_count,words[SATCHEL_CF_WORDS];} SatchelCFModule;
typedef struct {uint32_t semantic_ops,attribute_mask,varying_mask,declared_uniforms,active_uniforms,spirv_id_bound;} SatchelCFFacts;
typedef struct {uint32_t version,size,dynamic_code_features,work_used;SatchelCFModule vertex,fragment;SatchelCFFacts vertex_facts,fragment_facts;} SatchelCFPair;
/* Tiny cold-safe transition, nonblocking process gate. Pure borrowed callback is
 * checked cooperatively with the monotonic deadline at work charges; it cannot
 * block/mutate renderer resources or escape this call. Timeout1..1000000000ns,work1..65536.
 * No hard realtime/preemption guarantee. Every failure leaves whole output intact. */
int satchel_captured_flat_compile(const SatchelCFInput *,SatchelCFPair *,uint32_t output_size);
#ifdef __cplusplus
}
#endif
#endif
