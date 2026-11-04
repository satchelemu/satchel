#ifndef SATCHEL_NATIVE_REWRITER_H
#define SATCHEL_NATIVE_REWRITER_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define NR_API __attribute__((visibility("default")))
#define NR_ABI_VERSION 1u
typedef enum {
    NR_OK,
    NR_INVALID_INPUT,
    NR_LIMIT,
    NR_CANCELLED,
    NR_ASSERTION,
    NR_UNSUPPORTED,
    NR_BRANCH_RANGE,
    NR_ALLOCATION,
    NR_INTERNAL
} nr_status;
typedef struct {
    const uint8_t *bytes;
    size_t size;
    uint64_t offset;
} nr_segment;
typedef struct {
    uint64_t offset;
    const uint8_t *original, *replacement;
    size_t size;
    const char *reason;
} nr_cut;
typedef struct {
    uint64_t offset;
    uint32_t number, original_word;
    const char *reason;
} nr_observation;
typedef struct {
    uint64_t size;
    const char *const *fingerprints;
    size_t fingerprint_count;
    const nr_cut *cuts;
    size_t cut_count; /* offsets relative to the function */
} nr_match_function;
typedef struct {
    const char *name;
    const nr_match_function *functions;
    size_t function_count;
} nr_match_rule;
/* Profiles are caller-owned, reviewed data. No runtime script or Python dependency. */
typedef struct {
    const nr_cut *cuts;
    size_t cut_count;
    const nr_observation *observations;
    size_t observation_count;
    const char *const *capabilities;
    size_t capability_count;
    uint32_t recipe; /* 0: none, 1: TOTK 1.4.3, build-checked */
    const nr_match_rule *match_rules;
    size_t match_count; /* Same-name alternatives form one atomic rule. */
} nr_profile;
typedef struct {
    uint32_t abi_version;
    nr_segment segments[3];
    uint64_t bss_size;
    uint8_t build_id[32];
    const char *module_name;
    nr_profile profile;
    size_t max_image_bytes, max_patch_bytes, max_function_bytes, max_records;
    int (*cancelled)(void *context);
    void *context;
} nr_request;
typedef struct {
    nr_status code;
    int64_t offset;
    char message[192];
} nr_error;
typedef struct nr_result nr_result;
typedef struct {
    const uint8_t *data;
    size_t size;
} nr_bytes;
typedef enum { NR_IMAGE, NR_FIXUPS, NR_GATES, NR_SITES, NR_CUTS, NR_OBS, NR_CHANGED } nr_artifact;
/* Transactional: failure leaves *result NULL. Error messages never contain instruction bytes.
   Inputs are immutable and must stay valid until return. Cancellation runs on the calling thread.
   Set every bound explicitly. Limits are per invocation; calls may run independently. */
NR_API nr_status nr_rewrite(const nr_request *request, nr_result **result, nr_error *error);
/* Additive API: preserves nr_request ABI. Byte counters describe the current
   analysis pass, phases 0 through 7. Callback runs synchronously. */
typedef void (*nr_progress)(void *context, unsigned phase, uint64_t completed, uint64_t total);
NR_API nr_status nr_rewrite_progress(const nr_request *request, nr_result **result, nr_error *error, nr_progress progress);
NR_API nr_bytes nr_result_get(const nr_result *result, nr_artifact artifact);
NR_API void nr_result_free(nr_result *result);
#ifdef __cplusplus
}
#endif
#endif
