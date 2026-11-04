# Checked texture snapshots (staged resource primitive)

`GuestTextureFrames` takes one explicitly selected, checked guest texture and
produces one immutable CPU frame. `GuestVulkanTextureFrames` supplies a separate
trusted opt-in composite that enforces the existing dispatcher/device/queue
ownership rules. This is not an implementation of NVN Window, VI, acquire,
present, a display timestamp, or a game-frame selection policy. No App linkage or
physical-device validation is included in this checkpoint.

The Vulkan image cannot be assumed current after a previous raster run: a later
MemoryPoolFlushMappedRange or GPU transfer may change the pool mirror. Begin
retains the actual texture generation and pool lease, validates the current
checked guest tags/ranges, acquires a GPU-access hold, reads the GPU-visible tiled
pool bytes, detiles them and submits a real image upload. After its fence, a real
zero-command LOAD readback produces the immutable frame through GuestFrames.
Unflushed guest CPU writes remain invisible. Snapshot does not invalidate or
rewrite guest CPU memory. There is no stale-image or zero-image fallback.

## Contract

- Format/color-space/origin are explicit trusted inputs: only RGBA8 UNORM,
  linear-sRGB primaries and top-left row order are admitted. These are not inferred
  display facts from an arbitrary imported texture. Dimensions and generation
  derive from the retained texture; width/height are at most2048, linear bytes8MiB.
- The pool/tag/device checks and final identity revalidation precede the hold.
  No callback interleaves that final check and hold. Begin's cancellation callback
  is pure, borrowed and never retained. It must not execute guest work or mutate
  graphics ownership. Both owners reject reentry through bounded try-gates.
- Every nonnull low-level owner/nonzero composite ticket remains owned even on
  error. A valid output owner/ticket is cleared on all early failures. The request
  is copied before clearing a possibly overlapping output. New typed inputs and
  outputs reject natural-alignment violations before dereference.
- Poll waits at most10ms on one fence per invocation. Upload, snapshot and cleanup
  are distinct phases. A discarded snapshot can drain without setting
  `snapshot_completed`. READY means immutable bytes, never displayed bytes.
- Nonzero upload/readback jobs and their texture/pool/device ownership survive
  backend errors, timeout and cancellation. In particular, a real completed
  fence does not release parent ownership when a later readback/destroy/release
  fails. Retry keeps all unfreed resources tracked.
- Detach matches the exact epoch and latches retirement. Already leased frame
  bytes remain immutable and readable until release, even after guest CPU/process
  destruction. No CPU or callback context is retained by the snapshot owner.
- Composite tickets are monotonic host values, never guest pointers. DestroyOK
  invalidates the ticket; BUSY/error preserves it. Dispatcher retirement may
  invalidate a drained, unleased ticket. A UI lease keeps the composite owner alive.
- GPU begin/poll/destroy and dispatcher work share the existing serial graphics
  worker. UI take/copy/release may use the try-gate and retry BUSY; no UIKit types
  enter this C API. The caller separately budgets copied Data/CGImage storage.

The concrete composite blocks cached transfer/compute/raster submission and pool
operations while a snapshot still owns GPU resources; previously resolved command
handles do not bypass this gate. Its real device-child callback includes snapshot
ownership, and retirement drains snapshots before textures, pools and devices.
The old `GuestVulkanRaster.c` and `GuestVulkanCapturedRaster.c` remain unchanged:
legacy opt-ins keep their old imports and behavior. The new separate composite
inherits the checked scalar/legacy/captured program routes, including the explicit
captured BindProgram state-unavailable stop. It supplies no guessed provoking or
YNegate state. Its derivation is bound in TextureFramesDerivation.json.

## Accounting and limits

There are at most3 snapshot owners globally, and3 per composite. Each stages its
actual tile bytes plus tight linear bytes before upload; the charge is committed
before either allocation. The independent defensive caps are24MiB per owner and
48MiB globally, with texture storage≤16MiB and linear size≤8MiB. Current admitted
2048×1024 fixtures use16MiB each and reach48MiB across three pending owners.
After snapshot completion, all staging is released. GuestFrames separately caps
three immutable frames at8MiB each/24MiB total. Neither limit includes caller-owned
UI copies. Actual padded Vulkan buffers/images/jobs remain charged by their
existing backend registries; this layer does not enlarge those budgets.

## Focused reproduction

No new dependencies. Requires the existing pinned MoltenVK static build, Apple
arm64 clang/Metal, validated52-export backend object, captured compiler artifact,
scalar compiler and a matching CPU source/header/archive checkpoint. All output
must be outside the source tree. Preserve the CPU input as a private copy if
another task may promote it while this gate runs.

```
zsh scripts/test_texture_frames.sh --ios \
  --cpu-archive /path/to/frozen/libSatchelCPUProbe.a \
  --cpu-header /path/to/frozen/CPUProbe.h \
  --cpu-source /path/to/frozen/CPUProbe.cpp \
  --output /tmp/satchel-texture-frames-build
```

`--dependency-root` defaults to the repository root; an additive relocated overlay
can explicitly select the unchanged repository dependency tree. Backend/compiler/
scalar build paths are configurable. The recipe verifies their artifact hashes
against their producer manifests and matches relevant source/header hashes to
the dependency tree. The static MoltenVK archive is pinned by full hash. The CPU
archive/header/source are copied once to the output and recorded, not silently
read again from a mutable build path. No canonical source or archive is written.

The recipe rebuilds the actual canonical C owner/dispatcher dependencies plus
both unchanged old composites. The interpreted test reuses RasterDispatchTest.c
by a locked mechanical rename of its entrypoint and opt-in; no original assertion
is deleted. Fault seams exist only in separate test objects, never iOS production.
The two new iOS17 archives export only11 low-level and9 composite symbols.
GuestFrames and the existing compute/raster/captured owner/compiler dependencies
must be linked separately; the new archives do not duplicate those objects.

The original tests cover prior raster→unflushed CPU mutation→flushed mirror,
GPU ClearBuffer→mirror snapshot without CPU rewrite, both cached raster and
transfer handle exclusion, actual TextureFinalize/DeviceFinalize before/after
cleanup, alias views, read-only backing, bad tags/generations, request alignment,
reentrant callback outputs, wrong epochs/stale tickets, immutable UI leases after
CPU-free handoff, three8MiB frame/staging limits, and guarded copies. Fault builds
cover host allocation, success-with-zero-job rejection, nonzero-job error,
snapshot submit/read/destroy, staging destroy, pool/texture release, discard after
readback failure, and post-fence parent/queue protection with CPU-free recovery.
Assertion counts include byte comparisons and bounded polling checks; they are
not counts of independent test scenarios. Host evidence is not device evidence.

The focused recipe also routes the unchanged captured-NVN admission/state-stop
fixture (2,452 assertions), real-reference/finalize-retry fixture (998) and scalar
compute fixture (16,174) through this new opt-in. Only header/opt-in references are
mechanically redirected in generated test copies; existing failure assertions,
compute fault seams and source/state admission limits remain unchanged. Both old
composite objects also link alongside the new object with distinct exports.

Original component source is project-authored; the composite derives from the
existing checked captured/raster implementation. Existing NVN header/reference
MIT notices, VIXL license, scalar/captured compiler notices and MoltenVK component
notices remain mandatory at their established paths. This additive owner introduces
no third-party code, dependency, vendored binaries or runtime guest native code.
IndependentReview binds the four unchanged production source/header hashes; its
producer run is historical, while a new reproduction manifest records later runs.
