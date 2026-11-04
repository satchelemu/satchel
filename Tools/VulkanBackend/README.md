# Vulkan backend on Apple platforms

Implemented: persistent bounded real Vulkan devices, buffers and transfer jobs;
checked scalar compute pipelines; RGBA8 images with ordered clear/load/draw,
readback and fence-owned cleanup. MoltenVK converts supplied, independently
validated SPIR-V on the executing device. The paired NVN adapter lives in
`Tools/GuestGraphics`; shader admission/capture remains separate from pipeline
creation and submission. The original fixed compute diagnostic is retained as
one regression, alongside actual interpreted original compute/transfer/raster
journeys. The matched renderer framework exports49 reviewed C functions.

The new raster slice is host validated and iOS compiled; no installed raster,
game frame, window/acquire/present, arbitrary shader or gameplay claim follows.
No Mac per-game shader preparation or fixture substitution is used. See
`Tools/GuestGraphics/RASTER.md` for exact bounds, unsupported modes and validation.
Reproduce the complete matched slice with `zsh scripts/test_nvn_raster.sh --ios`.

## Suitability and current upstream status

As checked 2026-10-31, [Khronos's portability page](https://www.vulkan.org/porting)
and the [MoltenVK main README](https://github.com/KhronosGroup/MoltenVK/blob/main/README.md)
state that Khronos deprecated its MoltenVK stewardship; community maintenance
continues best-effort. MoltenVK remains their recommended option for iOS and
Intel Macs; KosmicKrisp is recommended for Apple Silicon Macs. This is a material
long-term dependency constraint, not a reason to claim iOS has a native Vulkan
driver. The iOS path still executes through Metal.

A Vulkan backend could reuse Vulkan resource/pipeline code from a suitably
licensed Switch GPU renderer and consume the Maxwell translator's SPIR-V output.
It does not create those emulation semantics. Castlevania's identified interface
is NVN, so it will still need a checked guest call/submission boundary, live GPU
state, resources, synchronization, and accurate shader translation. NX2Apple's
existing NVN backend targets Metal directly; placing Vulkan below it requires
an explicit renderer adapter rather than a library substitution. No performance
advantage has been measured or claimed.

## Pinned dependency and licenses

Official MoltenVK release: [v1.4.2](https://github.com/KhronosGroup/MoltenVK/releases/tag/v1.4.2)
(2026-07-24), source `db66022459ffb663aa2b50f6b018bc2e124f5edf`.
The regular macOS artifact is used, **not** `macos-privateapi`.

| Official release asset | SHA-256 |
| --- | --- |
| MoltenVK-ios.tar | b5d947b1660e6e9fed40b9cd2387e160aaab9e80b775c0cef7e14059405178c1 |
| MoltenVK-macos.tar | f95765a6229cb7b915990a2890ce12ebe36a730b021545d3d52ae69ce4c4024e |

These digests match the official GitHub release asset metadata. Build verifies
both tar hashes and the consumed extracted static archive/header contents.
MoltenVK itself uses its official prebuilt static runtime; the small backend
wrapper and isolated framework are built locally. This is not a claim that the
entire upstream runtime was independently rebuilt or sanitizer-instrumented.

`Notices/` preserves the full license files from pinned MoltenVK, SPIRV-Cross,
SPIRV-Tools, SPIRV-Headers, Vulkan-Headers, and cereal revisions; `manifest.json`
records source URLs, revisions, and notice hashes. These notices are copied
inside the final framework and must remain in distributed app bundles. The
principal runtime license is Apache-2.0; bundled components also contain MIT,
BSD and Khronos notices. The complete collection is retained without truncation.

## Build and integration

```sh
python3 Tools/VulkanBackend/fetch.py
zsh scripts/test_vulkan_backend.sh --ios
```

Requires installed Xcode clang and Python3.14 (the local toolchain), no Vulkan
SDK/package-manager installation. All downloads/build outputs live under
`/tmp/satchel-vulkan-backend`; no game data is used.

Final iOS artifact:
`/tmp/satchel-vulkan-backend/build/ios/SatchelVulkan.framework`

The framework is unsigned, arm64 iOS17+, built with the installed iOS27.2 SDK.
Root app integration must embed/sign it with the app's existing signing setup.
Import `VulkanBackend.h` from its Headers directory, or import the SatchelVulkan
module. Call `satchel_vulkan_compute_diagnostic` off the UI thread, passing a report
and optional cancellation callback/context. No Vulkan types or C++ classes cross
the public C ABI. Null reports fail. Return status and report.status agree.

The custom framework is deliberately built around the official **static**
MoltenVK archive and exports only reviewed `satchel_vulkan_*` C functions: the fixed
compute diagnostic, device lifecycle/factory and bounded buffer API. The canonical
historical retirement version exported20 symbols; the current matched renderer exports49.
The prior lifecycle version exported6.
It avoids
C++ symbol interposition with the separately linked ShaderBridge's different
SPIRV-Cross revision. The official iOS dynamic framework had a non-weak,
Mac-shaped IOKit load path; the custom framework was audited to have no IOKit
load dependency and only expected public Apple framework/system dependencies.
The original fixed compute packaging passed actual app/device validation below;
the persistent lifecycle passed the separate stage16 device check described below.
Debug symbols stay in sibling `SatchelVulkan.framework.dSYM`, never inside the
framework, so Xcode does not attempt to bitcode-strip the dSYM as a runtime binary.

The wrapper enables `VK_KHR_portability_subset` when creating its Vulkan device.
`VK_KHR_portability_enumeration` is a loader extension: it is enabled with its
instance flag only when advertised. Direct-linked MoltenVK enumerates its device
without that loader extension. `VK_EXT_layer_settings` disables MoltenVK payload
logging, debug shader conversion, tracing and automatic capture for this instance.
The exact settings and APIs were checked against the pinned installed source.
See [the pinned runtime guide](https://github.com/KhronosGroup/MoltenVK/blob/db66022459ffb663aa2b50f6b018bc2e124f5edf/Docs/MoltenVK_Runtime_UserGuide.md).

## Resource lifetime and validation

A successful fence is required before GPU results are read. Noncoherent mapped
memory is flushed/invalidate-correct; coherent memory is preferred. The buffer
is 16bytes and allocation requests above16MiB are rejected. Queue/device/extension
counts have explicit limits. No indefinite `vkDeviceWaitIdle` is used.

Fence waits have a five-second deadline and50ms cancellation polls. On timeout,
cancellation, or a non-device-lost error after submission, at most one job is
retained instead of destroying resources still in use. Subsequent calls poll and
clean that job before admitting another; BUSY reports retained resources. This
retention path is implemented but an artificial driver hang was not injected.
Synchronous instance/device/shader/pipeline creation is not interruptible; only
small fixed original inputs are admitted. Do not describe this as a general
cancellable hostile-shader compilation service.

Mac validation passed **11 checks** with ASan/UBSan on our wrapper: null-report,
pre-cancellation, reentrant BUSY guard, cancellation at four pre-submit stages,
and two fresh complete native Vulkan GPU runs with value/guard verification.
The official upstream runtime is not sanitizer-instrumented and a Vulkan
validation-layer package was not installed. iOS framework cross-compilation,
public C export, load-command audit and notice packaging passed; phone execution
belongs to root's integrated device test, not this host evidence.

## Integrated phone evidence (root validation)

Root's stage09 test on the maintainer's iPhone Duo passed with the Vulkan framework,
NativeAOT shader compiler and direct Metal path loaded in the same app process.
Reported Vulkan status0/stage7, VkResult0, device API version4198757,
portabilitySubset=true, commandCompleted=true, valueMatched=true,
guardWordsMatched=true and resourcesRetained=false. Root also verified15
framework notice/manifest files against the built app and no nested dSYM.
Evidence belongs to `/tmp/satchel-runtime-castlevania-09.xcresult` and the paired
runtime JSON retained by root. This establishes on-phone original Vulkan compute
execution and coexistence, not Castlevania rendering or a game-submitted shader.

## Persistent Vulkan devices

`VulkanDevice.h` adds a real instance/device/compute-queue lifecycle.
`satchel_vulkan_device_create(0,&token,&caps)` allocates a real Vulkan device,
`satchel_vulkan_device_capabilities(token,&caps)` reads its retained actual snapshot,
and `satchel_vulkan_device_destroy(token)` releases it. Tokens are monotonically
assigned integers, not addresses. At most eight devices may be live; stale
handles, unsupported nonzero flags, concurrent/reentrant calls and exhaustion
are rejected explicitly. `satchel_vulkan_device_live_count` supports cleanup checks.

The capability snapshot records actual device/API identifiers, queue flags,
portability-subset status, selected supported features, buffer/image/compute
limits and alignments from Vulkan. Enabled optional features are separately
reported as zero, consistent with the actual VkDevice creation request. This
is not an NVN limit table or a claim of complete Switch GPU compatibility.

`satchel_vulkan_guest_device_backend(&table)` supplies the versioned trusted callbacks
for explicit attachment to GuestDispatch. Callback/context pointers and Vulkan
tokens remain host-only. Its supported builder flags are zero; unsupported
semantics are not silently ignored. The separate transfer-submission API below now tracks pending ownership.
Device destruction returns BUSY until callers observe/reap a successful fence;
it never performs an unbounded device-idle wait. GuestDispatch is not wired to
submission yet: its teardown must first retain/reap a failed backend destroy.

`DeviceTest.cpp` passed **115 ASan/UBSan wrapper checks**, including all32 rejected
flag bits, actual capability fields, eight real devices, exhaustion, stale-token
rejection, cleanup, new-token generation and callback adapter behavior. The
separate compute proof still passed its11 checks. `--staging` builds to
`/tmp/satchel-vulkan-backend/staging` for validation before coordinated promotion;
the lifecycle version is now promoted to the canonical framework path above.
The framework export/load-command/notice audits and iOS cross-compilation passed.

Original interpreter lifecycle coverage and the separate phone-callable
diagnostic are described in `Tools/GuestGraphics/README.md`. Host tests establish
the checked call-to-real-device connection. Root's stage16 actual Duo check also passed the separate original interpreted
lifecycle diagnostic: status0/stage7,18 instructions/6 dispatch events, real
device creation/finalization, guest guards, state preservation and cleanup.
The game runtime was loaded in the same app but had not called NVN. Neither
this lifecycle evidence nor stage09 fixed compute establishes game-originated
drawing/presentation.

## Bounded real buffer resources

`VulkanBuffer.h` exposes reusable host-only resource operations. A successful
create allocates a real VkBuffer, queries its memory requirements, selects an
actually compatible host-visible memory type, allocates/binds/maps its entire
VkDeviceMemory and zeroes it before publication. The only supported usage bits
are storage1, transfer-source2 and transfer-destination4; at least one is
required and every other bit is rejected. Storage size also respects the actual
physical-device storage-buffer range. No GPU command is submitted by this API.

```c
uint64_t buffer = 0;
SatchelVulkanBufferInfo info;
SatchelVulkanStatus status = satchel_vulkan_buffer_create(device, 4096,
    SATCHEL_VULKAN_BUFFER_STORAGE | SATCHEL_VULKAN_BUFFER_TRANSFER_DESTINATION,
    &buffer, &info);
// On OK: checked copies from/to caller-owned host memory only.
// satchel_vulkan_buffer_write(device, buffer, offset, source, size);
// satchel_vulkan_buffer_read(device, buffer, offset, destination, size);
// satchel_vulkan_buffer_destroy(device, buffer);
```

Tokens are monotonic integers with a buffer-specific high-bit tag, not native
pointers. Each operation requires the correct live parent-device/token pair.
Reused registry slots get new tokens; stale/wrong-parent/type-confused IDs fail.
No VkBuffer, VkDeviceMemory or mapped host pointer crosses the C ABI. A future
guest adapter must copy through the checked CPU API; interpreting a guest
virtual address as the C source/destination pointer is forbidden.

The resource budget is32 buffers,8MiB maximum logical bytes per buffer and32MiB
aggregate actual Vulkan allocation bytes. It charges
[VkMemoryRequirements.size](https://docs.vulkan.org/refpages/latest/refpages/source/VkMemoryRequirements.html),
including implementation padding, before allocation. This budget covers this
registry's allocations, not all driver/app/CPU memory. Stats report exact live
counts and charged bytes, together with these limits. Size0, unsupported usage,
wraparound and any access outside the logical size fail before copying.
Zero-length access permits a null host pointer only with a valid handle and
in-range offset. Invalid reads preserve the caller's output.

Coherent memory is preferred. Noncoherent writes flush and reads invalidate the
full allocation, which was mapped from offset0 to its end. Using offset0 and
VK_WHOLE_SIZE follows the
[documented atom-alignment rules](https://docs.vulkan.org/refpages/latest/refpages/source/VkMappedMemoryRange.html).
A driver flush failure is reported; copied bytes may already have changed, so
no transactional rollback is claimed for device/API failures. All functions
share the backend's serialization gate; reentry/concurrent calls return BUSY.

Device destruction releases its buffer children before destroying the Vulkan
device. Ordinary buffer destruction returns its actual charge to the budget.
The submission API below explicitly protects pending GPU references.
Vulkan requires
[all referencing commands to finish before buffer destruction](https://docs.vulkan.org/refpages/latest/refpages/source/vkDestroyBuffer.html).
While a job is retained, referenced-buffer host access/destruction and parent
device destruction return BUSY. Only real fence success releases these locks.

`zsh scripts/test_vulkan_backend.sh --ios --staging` passed **64,233 ASan/UBSan
assertions** in `BufferTest.cpp`: real zero-initialized resources, all seven
usage combinations, rejected flags/size0, boundary and unaligned copies with
guards,512 deterministic valid/invalid range pairs, full8MiB roundtrip, readonly
metadata, wrong parent/stale token/type mismatch, reentry,32-slot exhaustion,
actual padded-allocation accounting, budget exhaustion, and child/parent cleanup.
The count includes per-byte unchanged-output assertions, not64,233 distinct
scenarios. The existing11 compute and115 device checks also passed. These checks do not
instrument upstream MoltenVK, force noncoherent memory on coherent hardware,
inject driver allocation/failure errors, or prove game/GPU submission semantics.

The staged iOS framework was promoted with parent coordination to
`/tmp/satchel-vulkan-backend/build/ios/SatchelVulkan.framework`, with `VulkanBuffer.h`
included in the umbrella module. The13-symbol export audit, no-IOKit audit,
sibling dSYM and full bundled notices passed. Prior framework/dSYM artifacts
are preserved as `.pre-buffers` siblings. No new dependency was introduced.
Actual phone buffer execution awaits root integration; stage16 lifecycle
evidence does not cover this addition.

## NVN MemoryPool/Buffer semantics still to connect

The same pinned reconstructed `include/nvn.h`, `nvn_protos.h` and `hle_nvn.c`
show a pool builder receiving a device, guest storage pointer/size and flags; a
buffer builder receives a pool plus signed offset and size. Map returns CPU
access subject to pool flags; buffer-address queries belong to GPU address
space. The reference native path stores host pointers and often aliases CPU and
GPU addresses. That convention cannot be imported into the interpreter.

The reviewed pool flag values are CPU no-access/uncached/cached1/2/4, GPU
no-access/uncached/cached8/16/32, shader-code0x40, compressible0x80, physical0x100
and virtual0x200. Vulkan storage/transfer usage bits are **not** these flags.
The new host buffer API does not silently reinterpret them or implement a guest
MemoryPoolInitialize/BufferInitialize. Those functions still stop unsupported.

A truthful next adapter needs checked pool-object/storage ranges and signed
offset arithmetic; owned pool generations with buffer-view parenting; distinct
bounded guest GPU virtual addresses; CPU cached/uncached and flush/invalidate
copy semantics; and lifetime rules for aliases and submitted work. Shader-code,
compressed, physical/virtual mappings need explicit support or rejection. Pool
backing should preserve buffer-view aliasing instead of allocating unrelated
VkBuffers for each guest view. The current primitives provide actual backing
allocation/copy/ownership for that adapter, while queue/binding/clear/present
remain separate unimplemented operations.

## Fixed original on-device buffer diagnostic

`satchel_vulkan_buffer_diagnostic` takes only a report and optional cancellation
callback, never guest data or native resources. It creates a temporary real
Vulkan device with65-byte and31-byte buffers, verifies zero initialization,
writes/reads an original bounded pattern with adjacent guards, confirms an
out-of-bounds write is rejected, destroys the first buffer explicitly, and
releases the second through parent teardown. Report fields are status/booleans,
counts and allocation totals only; no source/fixture payload or native token
is exposed. It does not submit a shader or GPU command.

Success means status0/stage9, created_buffers2, zero_initialized, write_completed,
read_completed, guards_matched, bounds_rejected, explicit_destroy, parent_cleanup
and cleanup_matched true. Baseline/final device count, buffer count and actual
allocation bytes match. Driver create/map/free operations are synchronous;
cancellation is checked between stages and always cleans owned resources. Use
the existing serialized worker, outside other active backend work.

The buffer assertions above include this complete diagnostic, seven cancellation
checkpoints, full-registry failure with no leaked device, and a second caller's
live device/buffer remaining unchanged across the fixture. The final iOS
framework is frozen for root's stage19 phone validation; no device result is
claimed yet. The resource primitives are reusable for a future checked guest
pool adapter; the original fixture itself is not game-originated graphics.

## Bounded GPU transfer submissions

`VulkanSubmission.h` adds actual GPU buffer fill/copy commands on the retained
Vulkan device/queue. `satchel_vulkan_buffer_fill` requires four-byte-aligned offset
and nonzero size with destination transfer usage; `satchel_vulkan_buffer_copy`
requires distinct source/destination buffers, source/destination transfer usages
and complete valid logical byte ranges. Copy supports byte-unaligned regions,
validated against this pinned MoltenVK implementation. Both operations reject
wrong parents, overflow, stale handles and unsupported usages before recording.
No arbitrary command stream or native resource pointer is accepted.

The command rules were checked against the pinned installed headers/runtime and
the official [fill](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdFillBuffer.html)
and [copy](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdCopyBuffer.html)
contracts. Each job owns one command pool, one command buffer and one fence,
with explicit buffer-token and parent-device references. Barriers order host/
previous transfer writes before transfer access and make destination transfer
writes available for host reads. Noncoherent host reads still invalidate their
complete mapped allocations after successful completion.

At most **four jobs globally and one job per device** may be retained. Before
queue submission is attempted, cancellation/failure destroys unsubmitted objects
and returns token0. Immediately before attempting vkQueueSubmit, the registry
publishes ownership and locks referenced buffers plus their parent device. A
nonzero token therefore owns resources **even if submission returns API_ERROR**.
This deliberately retains uncertain driver-error paths instead of assuming that
nothing could have been submitted. Token namespaces distinguish devices,
buffers and submissions; IDs are monotonic and never native pointers.

`satchel_vulkan_submission_wait(device,token,timeout_ms,cancel,context,&report)`
accepts0–5000ms;0 polls once and positive waits use at most10ms requested driver
waits. Actual timeout precision remains a driver property, as specified by
[vkWaitForFences](https://docs.vulkan.org/refpages/latest/refpages/source/vkWaitForFences.html).
A real successful fence consumes the job/token, destroys its completed command
resources and unlocks the buffers/device. Read/write/destroy remain BUSY until
that observation, even if hardware may already have completed. Zero-size host
access is blocked too. Unrelated resources remain usable.

Cancellation, timeout and all non-success fence errors retain ownership for a
later poll. There is no force-release or invented completion. Unrecoverable
device loss can retain the bounded resources until process termination; native
resource destructors avoid Vulkan destruction during static teardown when
completion is unknown. Callers must preserve every nonzero submission token
and treat only a successful completed report as its consumption. Invalid API
arguments or BUSY responses do not imply that an existing job was released.

The trusted GuestDispatch factory remains lifecycle-only. Do not submit through
a guest-owned device until GuestDispatch teardown is extended to preserve and
reap a backend destroy that returns BUSY; its current teardown is not a pending
job owner. No NVN queue, memory-pool, buffer, clear or present call is enabled
by these standalone resource APIs.

`SubmissionTest.cpp` passed **136 ASan/UBSan assertions**: real GPU fill/copy
values and unchanged guard words; byte-unaligned copy; missing usage/parent/
range/alignment errors; cancellation at both pre-submit checks; retained
post-submit cancellation with protected host access and destruction; actual
fences; stale/wrong-parent tokens; one-job/device and four-job global limits;
and complete buffer/device/job cleanup. Zero-time polls accept either genuine
immediate success or a timeout followed by real completion. Tests do not fake
a slow GPU or inject driver/device-loss failures; those retention paths are
implemented conservatively, not claimed to have been induced on hardware.

The existing64,233 buffer assertions,115 device checks and11 fixed compute
checks still pass. Staged iOS framework and18-export/load-command/notice audits
passed at `/tmp/satchel-vulkan-backend/staging/ios/SatchelVulkan.framework`. The
validated transfer framework was subsequently promoted, then superseded by
the20-export retirement version below.
This validates host GPU transfers, not phone transfer execution or game frames.

## Fixed original GPU transfer diagnostic

`satchel_vulkan_transfer_diagnostic` is an additional standalone original fixture for
phone integration. It owns a temporary device and16-/24-byte buffers, initializes
guards through the host-copy API, submits an actual GPU fill, observes its fence,
then submits an actual GPU copy and observes that fence. Host reads compare the
written values and unchanged surrounding guards. No shader/game input, native
resource handle or fixture byte is accepted or exposed by this facade.

Success is status0/stage9; fill_completed, copy_completed, values_matched,
guards_matched and cleanup_matched true; resources_retained false; and baseline/
final device, buffer and job counts plus allocation bytes equal. The16-byte
source fill and24-byte destination guards are checked separately, so host copy
success alone cannot pass the diagnostic.

The diagnostic has its own nonreentrant gate and private persistent ownership
record. After cancellation, timeout or error with a nonzero submitted token, it
retains the exact device/source/destination/job IDs internally. It does not try
to force-release or misreport cleanup. The next invocation first polls that job: a
real completed fence permits cleanup, while any unresolved completion reports
BUSY/retained. Only after prior cleanup does another fixture start. At most one
fixture's resource set is retained, within the global resource/job limits.

The136 submission assertions above include the new facade: four pre-submit
cancellation checkpoints with cleanup; deterministic cancellation before polling
an already submitted fill; next-invocation BUSY-or-real-reap behavior; successful
GPU fill/copy; full-job-registry refusal without leakage; and preservation of
another caller's live device/buffer. Tests use bounded real fence waits and do
not inject artificial driver completion. The18-export transfer framework was promoted with root coordination; its prior
framework/dSYM are preserved as `.pre-transfers` siblings. Actual Duo transfer
validation remains root-owned; no result is claimed here.

## Retirement backend extension

`satchel_vulkan_device_drain` performs a nonblocking actual fence check for the
device's owned job, reaping it only on success. BUSY/error preserves ownership.
This retirement-owner operation consumes completed submission tokens, so it
must not race a separate active token owner on the same device.
`satchel_vulkan_guest_device_backend_v2` returns an explicit larger callback table
with version2 and this drain callback; the version1 layout/factory are unchanged.

The new GuestDispatch reserved-slot retirement owner is documented and tested
in `Tools/GuestGraphics/README.md`. Guest NVN queues remain disconnected. The
staged20-export iOS framework passed the existing136 GPU submission,64,233 buffer,
115 device and11 compute assertions plus export/load-command/notice checks.
The20-export framework was promoted with root coordination and export/hash
verification, without rerunning unchanged gates. Canonical SHA256 is
07f9f6015dd78344d6356d1a84482a249b8f930029c644730ff1245755b5b2e4.
The prior framework/dSYM are `.pre-retirement` siblings. Root owns App migration
and actual phone validation; the current canonical artifacts are frozen.

## Staged bounded transfer batches

`VulkanTransferBatch.h` adds the trusted host-only `satchel_vulkan_transfer_batch`
primitive for a future genuine guest command-queue adapter. It snapshots1..32
fill/copy descriptors, validates the entire batch before allocating driver command
resources or submitting anything, and retains every unique referenced buffer
under one real command buffer/fence. Up to32 unique existing buffers are allowed;
total work is capped at32MiB, charging fill=size and copy=2×size. Existing global
buffer/allocation limits and four-job/one-job-per-device limits remain in force.
This is not a guest command parser or an enabled NVN queue implementation.

Fill requires nonzero4-byte-aligned size/offset. Copy accepts byte ranges and
permits same-buffer copies only when source/destination spans are disjoint.
Every token must belong to the actual device with required transfer usage, and
every complete range is checked before recording. Fill's unused source fields
and copy's unused value field must be zero. Unknown kinds/oversized batches fail.
These rules follow the checked installed Vulkan1.x APIs and the official
[fill requirements](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdFillBuffer.html)
and [copy requirements](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdCopyBuffer.html).

Real host/transfer→transfer barriers precede execution; transfer-read/write
barriers between commands preserve RAW/WAR/WAW dependencies; the final barrier
makes transfer writes available for subsequent host access after completion.
This uses the existing core barrier API, no new optional feature or dependency.
The ordering follows Khronos' [synchronization guidance](https://docs.vulkan.org/guide/latest/synchronization_examples.html).
The batch publishes one owned submission token and locks all unique resources
before attempting vkQueueSubmit. A submit error with a nonzero token still owns
potentially active work. Cancellation before submission returns token0 and cleans
unsubmitted objects. Cancellation/timeout after submission retain all buffers,
device, command pool and fence until actual success is observed. Ordinary wait
and device-retirement drain both use the same all-buffer completion path.

Validation passed **254 ASan/UBSan assertions** using actual MoltenVK GPU output:
dependent fill→copy→overwrite→copy sequences, guarded disjoint same-buffer and
unaligned copies, complete-batch validation before a valid first command can
execute, malformed/overflow/wrong-parent/usage/overlap checks, copied input despite
later caller mutation, both pre-submit cancellation checkpoints, post-submit
retention,32-command/32-buffer limits,32MiB work boundary, stale tokens, four-job
admission and actual retirement drain. The last additions exercised batch drain
and job limits; only that affected test was rerun afterwards, with no code change
to the already-built framework.

Affected existing gates also passed:11 compute,115 device,64,233 buffer and136
single-command submission assertions. Buffer counts include per-byte comparisons,
not64,233 distinct scenarios. Upstream prebuilt MoltenVK remains outside sanitizer
instrumentation. The staged arm64 iOS framework has21 reviewed C exports and all
existing notices; no native Vulkan/C++ symbols leaked into its public ABI.

Reproduction: `zsh scripts/test_vulkan_backend.sh --ios --staging`.
Candidate `/tmp/satchel-vulkan-backend/staging/ios/SatchelVulkan.framework`, SHA256
`988dabfa601d8ceae205a70eafed168fde28efb72c8d5a0549415ec8c91764fd`.
Canonical framework remains unchanged at `07f9f601…`; no promotion or phone test
of this batch function has occurred. A guest queue adapter still needs actual
command/control-memory accounting, retained recording/pool lifetimes, checked
handle arrays and completion/retirement ownership before it may submit a batch.


## Original runtime-compiled compute pipeline

See [COMPUTE.md](COMPUTE.md) for the staged real pipeline/descriptor machinery,
original Maxwell→SPIR-V→Vulkan execution, bit-preservation checks, ownership and
matched framework requirements. The earlier transfer candidate above was promoted
and validated on the Duo in stage35; the new compute stage is host-validated,
iOS-compiled and promoted with backups. Stage36 passed the14-case compute
diagnostic on the actual Duo alongside all9 GPU checks. No game graphics are claimed.

The additive `VulkanCapturedHost.h` query reports actual same-parent RGBA target
and physical BGRA/alignment facts separately from implemented-profile policy and
two unknown driver-bug facts. `Tools/CapturedFlatCompiler/HOST_FACTS.md` documents
the checked ABI2 compiler adapter; this adds no target format or game-frame claim.
