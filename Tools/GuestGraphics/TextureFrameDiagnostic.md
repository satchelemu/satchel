# Original interpreted texture-frame diagnostic

One separately exported C diagnostic exercises the concrete texture-frame
composite without guest game data. It creates an original interpreter caller and
checked NVN stubs, then interprets real device/pool/texture/queue APIs. One GPU
ClearBuffer writes green pixels into a31×17 block-linear texture's pool mirror.
The snapshot reads that mirror, uploads its actual decoded bytes to the owned
image, waits for its fence and takes a real LOAD readback into immutable bytes.
The original CPU mirror is checked unchanged: no fake readback/invalidate or CPU
replacement supplies the expected green result.

The successful journey verifies cached-submit/texture/device refusal while GPU
snapshot ownership is live, then genuine guest finalization after drain. It keeps
one immutable frame lease while handing off the dispatcher and destroying the
CPU, and verifies the bytes again before releasing the lease and collecting.
There is no Window/VI/nativewindow/acquire/present call, display timestamp, UIKit
pointer, selected game frame or claim of imported-game graphics. Explicit format,
color-space and origin are original-fixture facts. No shader compiler is invoked;
existing cold-first compiler diagnostic ordering is unaffected.

## Result contract

Return is an enum: OK0, FAILED1, CANCELLED2, BUSY3, INVALID4. Invalid/misaligned
output or reentrant BUSY preserves the caller's output. Other calls publish one
complete report. Success requires all of:

- statusOK and stage10;273 interpreted instructions/92 dispatch events in this
  fixed source revision (includes the intentionally refused parent/submit calls);
- one real transfer submitted/QueueFinish, two snapshots started/two completed
  uploads, exactly one completed snapshot and one cancelled/discard-drained snapshot;
- one pre-submit cancellation and one pending-snapshot cancellation;
- width31,height17,row stride124; values/guards/unchanged CPU cache/state preserved;
- actual texture/device finalized, CPU destroyed, immutable lease verified after
  CPU destruction, cleanup matched, and resources_retained false;
- complete baseline/final live-resource equality.

Resource snapshots include devices, public buffers/bytes and jobs, render targets/
bytes, raster and compute pipelines, private provoking-index buffers/bytes,
GuestFrames owners/frames/states/bytes, texture-snapshot owners/GPU owners/staging,
and reserved/active/retiring dispatcher contexts and pending devices. Cumulative
historical release counters are intentionally not treated as live allocations.
Padding/reserved fields are explicitly zeroed before equality comparisons.

A static owner keeps every nonzero ticket, immutable lease and dispatcher-retirement
obligation after error/cancellation. It never stores a CPU or callback/context.
Always-cancel calls perform cleanup-only retries before admitting new work.
BUSY with no resources can occur if a second cleanup succeeds; it is not success.
The serial App worker may poll global dispatcher retirement before this collector:
unleased drained tickets may then be INVALID at destroy, but a genuine UI lease
pins its composite and must remain valid at release. Fault tests exercise that
exact ordering and fail if a live lease is silently discarded.

The callback must only inspect cancellation/deadline state. The diagnostic has a
cooperative10s monotonic deadline and2.5s per snapshot phase, with individual fence
waits at most1ms. Clock failure latches cancellation, including a failed initial
read or phase-start read. Driver creation calls cannot be forcibly interrupted.
Original guest mappings total less than512KiB; there are two64KiB pools,12KiB
queue memory, one small image, at most one selected snapshot, and no native guest
execution. Staging/frame/backend bounds remain enforced by the existing owners.

## Reproduction and integration

```
zsh scripts/test_texture_frame_diagnostic.sh --ios \
  --cpu-archive /path/to/frozen/libSatchelCPUProbe.a \
  --cpu-header /path/to/frozen/CPUProbe.h \
  --cpu-source /path/to/frozen/CPUProbe.cpp \
  --output /tmp/satchel-texture-frame-diagnostic-build
```

The output must be fresh and outside the source tree. `--dependency-root` defaults
to the repository root; a relocated additive overlay can select the canonical
repository dependency tree explicitly. All consumed source/header files are
locked. The existing MoltenVK static archive is hash-pinned. Backend/captured/
scalar artifacts are verified against producer manifests and corresponding
canonical sources. CPU input is privately copied once and its precise provenance
recorded. No canonical files or artifacts are written by the recipe.

The normal host gate validates success, callback reentry/output canaries, early
cancellation and a fresh successful retry. The fault build independently injects
initial/phase clock failure, stub allocation, cancellation after a real begin,
cleanup destroy/release/handoff/retirement failure, and frame-poll failure. Every
failure preserves pending ownership; global-retirement-first retries drain it.
The production iOS17 archive contains only GuestTextureFrameDiagnostic.o and
exports only `satchel_guest_texture_frame_diagnostic`. Fault wrappers are compiled only
into a separate host test object. CPU test-only symbols are absent.

Link the archive with the existing texture-frame composite/owner, GuestFrames,
checked CPU/dispatcher/pool/transfer/compute/raster/captured owners and their
existing frameworks. Do not duplicate GuestPrograms objects. Root owns App report,
serial-worker cleanup integration, project linking and physical-device validation.
Until that validation is performed this is host-tested/iOS-compiled evidence only.
No dependency or upstream code is newly introduced; existing component licenses
and pinned reference notices remain unchanged and required.
