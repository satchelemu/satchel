# Queue ordering lease for presentation

This additive host interface prepares, polls and releases a checked real queue.
It does not implement Window/VI, native acquire/present timing, or game display.
The original QueueFinish API still writes its own checked queue tag; the host
lease drains the same actual transfer/compute/raster fence and publication first.
All same-queue guest work is blocked while leased. Cancellation marks only the
preparation, never cancels preceding healthy queue work or fabricates READY.
Error-drained leases may release safely without claiming failed work completed.

No CPU/context pointers are retained. A caller must release the ordering lease
before transfer-owner retirement can finish. `timeout_ms<=10` bounds requested
fence waits, not synchronous driver/readback/repack/publication wall time.

Run `scripts/test_present_queue.sh --output /tmp/satchel-present-queue-build
--cpu-archive <frozen archive> --cpu-header Tools/CPUProbe/CPUProbe.h --ios`.
Optional source/backend/compiler/scalar/MoltenVK paths are explicit arguments.
The existing validated artifact manifests bind implementation sources and binary
hashes; the CPU archive is copied privately and recorded, never a hidden mutable
link. Original transfer, interpreted compute and raster assertions are included
unchanged; added assertions include byte/register comparisons, not unique cases.
The archive exports the existing twelve functions plus three ordering functions.

The production C is byte-identical to the reviewed queue checkpoint. The only
post-review header delta corrects the timeout comment. No dependencies or license
terms changed; existing GuestGraphics, Vulkan, reference and compiler notices apply.
