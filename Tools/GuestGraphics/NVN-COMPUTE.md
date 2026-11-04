# Bounded interpreted NVN compute

Implemented and host-validated; awaiting the matched iPhone integration. This
adds real program admission and guest-recorded scalar compute to the checked
NVN dispatcher. No imported game shader or game frame has been validated.
Existing transfer-only and pool-only opt-ins retain their explicit scope.

`satchel_guest_dispatch_enable_vulkan_compute` is a separate trusted opt-in before
any dispatch event. It requires the exact real Vulkan v2 backend. Every guest
call still crosses interpreted BLR → emulator-owned guest stub → checked CPU
breakpoint; the return PC is validated before any resource or guest-RAM mutation.
No host address enters guest objects, command records or return registers.

## Accepted state and actual execution

ProgramInitialize creates a tagged, owned 0xC0-byte program object. SetShaders
copies the borrowed control data and GPU-visible code/C1 from a retained pool.
The shared bounded scalar decoder admits the actual instruction slice before
replacing the previous program generation. It records immutable IR and deferred
resource requirements, not fabricated constant-buffer values or a pipeline.
Admission checks actual Vulkan1.1/Shader and buffer/descriptor limits. Failed
admission preserves the prior snapshot and IR. Only user CB slots3–17 are
accepted, mapped from NVN uniform slots0–14; driver CB0, CB2 and C1 are not
substituted. Unknown local/shared sizes are acceptable only under the decoder's
complete proof that this admitted instruction set cannot use them.

BindProgram, BindUniformBuffer, BindStorageBuffer and DispatchCompute record
immutable state into caller-provided checked command/control memory. The first
profile allows one live compute recording per owner, one program/uniform/storage
binding and one compute dispatch in that recording. Compute stage5/mask0x20,
one storage slot0, and actual local/dispatch dimensions1x1x1 are required.
Rebinding inside that recording, indirect dispatch, mixed transfer/compute
recordings and multi-handle compute submissions stop explicitly. New bindings
and changed programs are supported after explicit command-handle finalization.

Retained program/pool generations survive recording and submission. QueueSubmit
snapshots actual GPU-visible CB bytes, verifies the CB-loaded storage pointer,
derives physical disjointness against both CB and full captured code/C1, and
revalidates all code/binding generations before invoking the compiler. The
backend independently checks the emitted scalar SPIR-V schema, creates a real
pipeline/descriptor set, consumes that emitted code and submits the GPU work.
It never substitutes an original fixture or precomputed result. A private64KiB
UBO copies the actual declared CB range; only unaccessed remaining bytes are
zero-filled. The live shader does not read those bytes.

QueueSubmit contacts `vkQueueSubmit` synchronously. QueueFlush is a validated,
nonblocking publication operation: no wait, fence consumption, completion claim
or pool-cache change. Empty queues are valid. Retained submission errors remain
errors until actual completion; inconsistent internal queue state stops. A
recording not passed to Submit remains outside the queue. QueueFinish waits the
real fence, then publishes completion. Explicit MemoryPoolFlushMappedRange and
InvalidateMappedRange control CPU/GPU cache copies. QueueFlush does not replace
those operations.

## Failure and retirement

The existing atomic checked writes protect guest objects/command/control
representations, including physical aliases. If command-record publication
fails after a binding is retained, ownership remains for cleanup, but End refuses
unless a Dispatch record was actually committed. No cleanup-only global binding
can become an executable recording. Submission preparation may leave a bounded
queue-RAM snapshot after a zero-token failure; pending counters/handle references
are not committed. Every nonzero job remains owned even when submission reports
an error. Cancellation and timeouts preserve the real resources.

The composite's const reciprocal range guards protect program, device, pool,
queue and recording objects without recursive active-lock bypass. The global
serialized graphics worker owns all callbacks and diagnostics. Retirement drains
compute jobs, releases recordings/program references, destroys program snapshots,
then pools and devices. CPU/provider/cancellation pointers never survive handoff.
Busy/error cleanup retains ownership in the existing bounded collector.

## Validation and reproduction

Run `zsh scripts/test_scalar_compiler.sh`, then
`zsh scripts/test_guest_nvn_compute.sh`. Build the pinned runtime with
`zsh scripts/test_vulkan_backend.sh --ios`; it verifies the existing official
release checksums and keeps debug symbols outside the framework. No new external
dependency or per-game preparation is needed. The optional output-root variables
keep experiments isolated from canonical artifacts.

The staged original interpreted gate passed16,174 ASan/UBSan assertions, including
failed admission preservation, changed code/operands, immutable recordings,
flush visibility, invalid LR, physical aliases, unsupported roles, mixed-operation
refusal, atomic-write allocation failure, pre/post-token submission errors,
fence-cleanup failure, cancellation and CPU-free retirement. These counts include
register/canary assertions; they are not16,174 distinct game scenarios.
The unchanged transfer suite passed6,521 assertions against the new components.

`GuestNVNComputeDiagnostic` is a separate original on-phone fixture. Its host
journey executes438 original CPU instructions/146 NVN dispatch events, admits two
program generations, then creates and completes four real pipelines/fences.
It checks initial operands, unflushed edits, flushed edits and changed code/new
bindings, with guarded readback and zero resource growth.44 host assertions cover
that fixture, cancellation at nine stages, reentry and deferred ownership. It is
C-only and does not warm the existing cold-first NativeAOT/C# shader fixture.

Previously built legacy diagnostic host objects were linked unchanged against
this matched backend/dispatcher: device48 instructions/14 events/7 capability
queries; pool107 assertions; interpreted pool109; transfer111; cold C# Maxwell
fixture90 assertions and14 real operand/fence cases. Their public service table,
request/result and report ABIs are unchanged. Existing operation enum values are
preserved; new methods append values. Public Vulkan/compiler header include-order
permutations also compile.

## ABI source and limits

Pinned reference NX2Apple commit8227f80f9b381c680611303319656c4e0c806a50:
`dm/gfx/nvn/nvn_protos.h` verifies exact program, bind, dispatch and queue
signatures. `include/nvn.h` verifies opaque sizes, shaderData and stage constants.
`hle_nvn.c` copies borrowed shader controls and uses immediate submission with a
no-op QueueFlush; our Flush additionally validates owned state. Control offsets
come from the pinned shader split/build implementation, not a claim of a universal
public Nintendo format specification. Unknown layouts remain unsupported.
Existing NVN/reference notices remain in this directory; pinned MoltenVK and all
transitive notices remain in the framework. Graphics, textures, arbitrary
Maxwell programs and full game compatibility remain outside this profile.
