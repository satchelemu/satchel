#ifndef SATCHEL_CAPTURED_FLAT_V2_H
#define SATCHEL_CAPTURED_FLAT_V2_H
#include "CapturedFlat.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Additive ABI; v1 remains unchanged. The exact device/profile adapter resolves
 * 22 facts. Only the two driver-bug facts may be requested as source-unused.
 * The compiler proves this AFTER immutable strict source admission; no caller
 * attestation can bypass it. Unknown/overlapping/unapproved provenance refuses. */
typedef struct {uint32_t version,size;SatchelCFInput input;uint64_t device_known,policy_known,requested_source_unused;} SatchelCFInputV2;
typedef struct {uint32_t version,size;uint64_t proven_source_unused;SatchelCFPair pair;} SatchelCFPairV2;
int satchel_captured_flat_compile_v2(const SatchelCFInputV2 *,SatchelCFPairV2 *,uint32_t output_size);
#ifdef __cplusplus
}
#endif
#endif
