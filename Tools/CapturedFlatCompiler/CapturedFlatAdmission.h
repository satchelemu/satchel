#ifndef SATCHEL_CAPTURED_FLAT_ADMISSION_H
#define SATCHEL_CAPTURED_FLAT_ADMISSION_H
#include "CapturedFlat.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Source-only admission for the current strict flat pair, not shader compilation.
 * No graphics state, host facts, resource defaults or pipeline are invented.
 * Same bounded source format, shared gate and cooperative budget as v1/v2. */
typedef struct {uint32_t version,size;SatchelCFSource vertex,fragment;uint32_t max_work,timeout_ns;SatchelCFIsCancelled cancelled;void *context;} SatchelCFAdmissionInput;
typedef struct {uint32_t semantic_ops,attribute_mask,varying_mask;} SatchelCFAdmissionStage;
typedef struct {uint32_t version,size,profile,nvn_stage_mask;uint64_t source_unused_host,source_unused_state;SatchelCFAdmissionStage vertex,fragment;} SatchelCFAdmission;
/* Natural8-byte input/output alignment. Sources are copied before callbacks.
 * Failure preserves whole output. Successful requirements remain tied to those
 * exact source bytes; callers retain/revalidate immutable snapshots before use.
 * source_unused_state mask0x08 (bit3) proves YNegate unused, not observed false. v1/v2 still
 * require their original explicit state contracts; this entry changes neither. */
int satchel_captured_flat_admit(const SatchelCFAdmissionInput *,SatchelCFAdmission *,uint32_t output_size);
#ifdef __cplusplus
}
#endif
#endif
