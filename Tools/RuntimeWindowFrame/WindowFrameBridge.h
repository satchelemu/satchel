#ifndef SATCHEL_WINDOW_FRAME_BRIDGE_H
#define SATCHEL_WINDOW_FRAME_BRIDGE_H
#include <stdint.h>
#include "GuestTextureFrameDiagnostic.h"
#ifdef __cplusplus
extern "C" {
#endif
enum {SATCHEL_WINDOW_FIXTURE_READY=0,SATCHEL_WINDOW_FIXTURE_BUSY=1,SATCHEL_WINDOW_FIXTURE_INVALID=2,SATCHEL_WINDOW_FIXTURE_FAILED=3};
typedef struct {
 uint32_t version,size,status,stage,cpu_destroyed,ready,resources_retained,cleanup_matched;
 uint32_t width,height,row_stride,expected_r,expected_g,expected_b,expected_a,failure_line;
 uint64_t fixture_token,window_ticket,epoch,byte_size,instructions,dispatch_events;
 SatchelGuestTextureFrameResources baseline,current;
} SatchelWindowFrameFixtureReport;
/* ORIGINAL host integration fixture, not game input or vendor surface policy.
 * Caller supplies a real test surface epoch. Mode0 leaves original Process/CPU
 * alive until finish; mode1 destroys them before returning the untaken READY
 * frame. Dispatcher/VI remain owned until finish, preventing an early global
 * retirement poll from discarding the untaken frame. No CPU/provider escapes.
 * Expected31x17 RGBA8/linear-sRGB/top-left red image is actually rasterized,
 * queued through interpreted Window acquire/present and immutable readback.
 * Use window_ticket with production Window take/copy/release on the same serial
 * graphics worker. Root Swift owns/budgets copies. These host-only numeric tokens
 * are test coordination, never guest RAM, runtime telemetry or display proof.
 * Cooperative10s begin budget; synchronous driver work cannot be interrupted.
 * Every nonzero fixture_token requires finish even if begin failed. Invalid
 * arguments and reentry preserve output. No fault injection in this component. */
uint32_t satchel_window_frame_fixture_begin(uint64_t epoch,uint32_t destroy_cpu_before_take,SatchelWindowFrameFixtureReport *);
/* Starts retirement; never run before consumer take/copy/release unless discarding.
 * A held UI lease makes BUSY retryable; release it through the production API,
 * then retry finish. Retains static owners on BUSY/error, no CPU-dependent retry.
 * READY with cleanup_matched1/resources_retained0 means fully drained. */
uint32_t satchel_window_frame_fixture_finish(uint64_t fixture_token,SatchelWindowFrameFixtureReport *);
#ifdef __cplusplus
}
#endif
#endif
