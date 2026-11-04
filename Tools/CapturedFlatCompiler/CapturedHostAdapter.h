#ifndef SATCHEL_CAPTURED_HOST_ADAPTER_H
#define SATCHEL_CAPTURED_HOST_ADAPTER_H
#include "CapturedFlatV2.h"
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {uint32_t version,size;SatchelCFSource vertex,fragment;SatchelCFState state;uint32_t max_work,timeout_ns;SatchelCFIsCancelled cancelled;void *context;} SatchelCapturedRequest;
/* Synchronous actual-device/target adapter. No caller-provided host masks/facts.
 * Caller owns device,target,source spans for entire call and serializes their
 * mutations. Pure cancellation must not destroy resources/reenter guest work.
 * No CPU/native resource pointers retained. Failure preserves complete output. */
int satchel_captured_compile_for_target(uint64_t device,uint64_t target,const SatchelCapturedRequest *,SatchelCFPairV2 *,uint32_t output_size);
#ifdef __cplusplus
}
#endif
#endif
