# Immutable host frame handoff

Implemented and host-validated in this repository; not connected to App, guest VI, an NVN window,
AcquireTexture or QueuePresentTexture. It snapshots an actual initialized Vulkan
render target through the existing zero-command LOAD batch and real fence. It
never substitutes pixels, advances a displayed timestamp or reports presentation.

## API and ownership

`GuestFrames.h` is a UIKit-free C ABI. Create an owner with a nonzero surface epoch.
The graphics worker calls `begin` using live device/target generation tokens and
explicit RGBA8_UNORM, linear-sRGB-primary, top-left metadata from owned renderer
state. UNORM alone does not establish color-space/origin semantics. Unsupported
profiles are rejected; width/height, tight row stride and size come from the real
target. Only initialized targets up to2048×2048 and8MiB per snapshot are admitted.

`begin` reserves CPU capacity before allocation/submission. A zero output ticket
means no new frame ownership. Every nonzero ticket remains owned, including on
error or cancellation; the owner must be polled/collected and retained. The actual
backend job retains its target and readback buffer through fence reap, so a caller
cannot destroy an in-flight target/device. No guest CPU/provider/native texture
pointer or cancellation context is retained. The caller keeps the device owner
alive until backend jobs/buffers are reaped. After READY, the immutable CPU bytes
are independent of the target/device/process.

`poll` waits at most10ms. READY means fence completed, readback copied, and backing
buffer destroyed. Errors preserve ownership for retry. `take` leases the oldest
non-discarded, unleased ticket for that epoch; a pending older frame blocks later
READY frames even across independent devices. Multiple leases can coexist, so
the UI must preserve its intended display ordering. `copy` copies checked byte
ranges into caller storage without exposing a native pointer. `release` relinquishes
the immutable bytes. Failure preserves `take` metadata and `copy` destination.
Stale tokens, duplicate releases and overflow ranges fail.

`attach` requires a strictly increasing epoch. Attach/detach invalidate unleased
frames; pending jobs retire without early destruction. Existing UI leases survive
detach/reattach and remain copyable/releasable under their old epoch/token.
`try_destroy` detaches and marks the owner retiring; BUSY leaves the pointer owned.
Call `collect` on the graphics worker with no expired cancellation context, then
retry destruction. Outstanding UI leases must be explicitly released. `collect`
OK only means no retiring job failed/pended; it does not imply leased frames are
absent. Only successful `try_destroy` clears the owner pointer.

All entry points use a bounded global try-gate. Concurrent/reentrant calls return
BUSY, without blocking another caller. A cancellation callback must be pure and
must not mutate backend resources. Begin/poll/collect/destroy share the App's
existing serial graphics worker with all backend calls and retirement. UI calls
may take/copy/release/detach/stats; retry BUSY later. Owner pointer lifetime must
outlive concurrent UI access, and destruction needs exclusive pointer ownership.
The App must retain owners after detach until all jobs and UI leases are drained.

The global owner budget is4 owners,3 immutable frame allocations, at most8MiB
each /24MiB total. These are **owner-managed CPU bytes only**. Readback Vulkan
allocations remain separately charged by the backend. UI Data/CGImage copies,
texture uploads and any presentation surfaces are outside this24MiB cap and need
separate bounded App accounting. This API deliberately copies rather than lending
a raw pointer; no end-to-end memory-cap or zero-copy claim is made.

## Focused reproducible gate

From the repository:

```sh
zsh scripts/test_guest_frames.sh
```

The default source root is this repository.
Outputs default to`/tmp/satchel-frame-handoff-build`; `--output` accepts only an
external task-owned scratch directory. The source lock binds the new owner,
original tests/build recipe and exact consumed canonical backend/compiler headers,
implementation and original fixture. No current CPU archive, game file or keys are
used. The installed official MoltenVK1.4.2 macOS archive, binary and headers are
verified against the existing dependency pin; no download/new dependency occurs.
Existing upstream notices in Tools/VulkanBackend remain applicable. This original
owner copies no upstream implementation.

The gate rebuilds the actual backend and original raster compiler with ASan/UBSan,
then renders an original shader triangle, snapshots it, and compares actual bytes
and guards. It also covers two-device delivery order, epochs/detach/UI leases,
source destruction after READY, allocation/readback/destroy/submission failures,
pre/post-submit cancellation, reentrant BUSY outputs,3×8MiB allocation limits,
stale tokens and complete cleanup. Assertion counts include per-byte comparisons,
not independent scenarios. The production C owner compiles for arm64 iOS17 with
exactly11 exports and no fault-injection symbols; the build verifies Mach-O minOS.
The manifest records source lock/build hashes, dependency/tool identity, test result
and artifact hashes. iOS compilation is not an on-phone/display validation.

Root reviewed the owner/backend state transitions and caught a sentinel output
bug before freeze: begin/create now clear valid output ownership values before
INVALID/BUSY failure. Source is copied before a possibly overlapping ticket write.
A subsequent ordering review added the pending-oldest delivery rule and its real
two-device regression. No guest display-service success is synthesized.
