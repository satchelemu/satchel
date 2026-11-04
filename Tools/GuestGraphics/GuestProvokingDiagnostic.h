#ifndef SATCHEL_GUEST_PROVOKING_DIAGNOSTIC_H
#define SATCHEL_GUEST_PROVOKING_DIAGNOSTIC_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef int (*SatchelGuestProvokingCancelled)(void *);
enum {SATCHEL_GUEST_PROVOKING_OK,SATCHEL_GUEST_PROVOKING_FAILED,SATCHEL_GUEST_PROVOKING_CANCELLED,SATCHEL_GUEST_PROVOKING_BUSY,SATCHEL_GUEST_PROVOKING_INVALID};
typedef struct {uint32_t devices,buffers,jobs,targets,pipelines,index_buffers;uint64_t buffer_bytes,target_bytes,index_bytes;} SatchelGuestProvokingResources;
typedef struct {
 uint32_t version,size,status,stage,backend_status,compiler_status;
 uint32_t pairs_compiled,pipelines_created,draws_submitted,fences_completed;
 uint32_t pre_submit_cancellations,pending_cancellations,values_matched,coverage_matched,guards_matched,vertices_unchanged;
 uint32_t native_extension,native_last_supported,native_last_enabled,rotated_last_supported;
 uint32_t device_finalized,cleanup_matched,resources_retained,prior_reaped;
 SatchelGuestProvokingResources baseline,final;
 uint64_t live_index_bytes;
} SatchelGuestProvokingDiagnostic;
/* Original encoded source -> live ABI2 adapter -> independent typed FIRST/LAST
 * pipelines -> two offscreen GPU draws. Compiler input remains explicit ORIGINAL
 * FIRST state; LAST is the separately proven pure-module renderer transform.
 * No NVN enum/default/state normalization, guest game, window or display claim.
 * Success requires statusOK,stage10,pairs1,pipelines2,draws2,fences2,pre/pending2,
 * values/coverage/guards/vertices/cleanup true and resources_retained false.
 *
 * Return is an enum status, not boolean. Invalid/misaligned out and reentrant
 * BUSY preserve output. Run on the serial graphics worker shared by all GPU
 * owners/retirement. Callback must only inspect cancellation/deadline; no guest
 * work, source mutation or GPU operations. It and context never survive return.
 * Cooperative10s deadline (clock failure latches cancellation), compile<=1s,
 * each fence<=2.5s; native driver creation is not forcibly interruptible.
 *
 * One static CPU-free owner retains every nonzero job/resource on error/cancel.
 * Next invocation drains it before new work; an always-cancel invocation is a
 * cleanup-only retry. If still pending it reports BUSY without discarding tokens.
 * All backend registries plus private index allocation are counted. One32x24
 * target,two pipelines(one bounded index allocation),320/3584-byte buffers.
 * BUSY with no resources can result from a second successful cleanup and is not
 * a completed diagnostic. No CPU test exports/fault seams in production archive.
 */
uint32_t satchel_guest_provoking_diagnostic(SatchelGuestProvokingDiagnostic *,SatchelGuestProvokingCancelled,void *);
#ifdef __cplusplus
}
#endif
#endif
