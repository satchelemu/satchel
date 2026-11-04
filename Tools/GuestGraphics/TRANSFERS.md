# Staged interpreted NVN transfer queues

This component connects original interpreted NVN calls to actual Vulkan buffer
transfers. It is not a renderer: no game call/frame, texture, Maxwell command
stream, shader, draw, presentation, queue callback, or QueueFlush is implemented.
The application must explicitly select `satchel_guest_dispatch_enable_vulkan_transfers`
after attaching the exact real Vulkan v2 device factory and before any event.
The previous pool-only opt-in still enables only pools. Unimplemented operations
stop; they do not return fabricated success.

## Verified ABI and bounded profile

Signatures and opaque object sizes were checked in pinned NX2Apple commit
`8227f80f9b381c680611303319656c4e0c806a50`,
`dm/gfx/nvn/nvn_protos.h` and `dm/gfx/nvn/include/nvn.h`. These are reconstructed
reference contracts, not an assertion of full proprietary API compatibility.
QueueBuilder is64 bytes, Queue8192, CommandBuffer160. ClearBuffer takes a GPU
address, size_t and uint32 value; CopyBufferToBuffer takes two GPU addresses,
size_t and int flags (only0 accepted). SubmitCommands takes an int count and a
guest pointer to64-bit command handles. The actual public
`nvnDeviceFinalizeCommandHandle(device, handle)` prototype provides explicit
handle retirement. The reference implementation's no-op body is not adopted as
a lifetime guarantee.

The supported profile requires real caller-provided memory. A queue has command,
compute-reservation and control areas,4KiB aligned, at most1MiB total, with no
fallback allocation or native guest callback. Only transfer work is supported;
a reserved compute area does not enable compute commands. There are at most4
logical queues, one per real device,8 command buffers,16 closed recording handles,
32 commands per recording/submission and8 handles per submission. Each command
buffer accepts one command grant from a dedicated cached pool and one raw control
grant; its closed recording must be explicitly finalized before memory reuse.
Command/control capacity is charged for actual original emulator records and END
metadata. Refill callbacks/multiple-grant recording and other flags stop explicitly.

These original records are emulator-owned representations of API calls, not
Nintendo GPU command bytes. They contain guest GPU addresses, never backend
resource tokens or pointers. Submit rechecks their complete contents and exact
parent/generation, snapshots bounded commands, resolves private bindings, and
submits one real Vulkan batch. Real barriers preserve dependent fill/copy order.
The backend limits total work to32MiB,32 unique buffers,4 global pending jobs and
one job per device. Source/destination same-buffer copies require disjoint ranges.

The retained pool lease table has512 entries and nonreused IDs. Closed handles
independently retain their command-storage pool and referenced data pools; a
CommandBuffer may finalize while a closed handle survives. In-flight handles
cannot finalize. Pools cannot finalize while referenced. A device cannot finalize
while any pool, queue, command buffer or recording remains.

## Submission, cancellation and cleanup

Guest queue command/control memory is preparation storage, atomically populated
before calling the backend. A zero-token backend failure may leave that bounded
prepared snapshot; submission counters and in-flight handle references do not
advance. This is explicitly not an atomic transaction spanning RAM and the GPU.
There is no fallible allocating rollback. The opaque queue object is updated by
Finish after real completion.

Every nonzero backend token is owned immediately, even on a backend error.
Finish calls the actual fence wait, bounded to5 seconds and the dispatch deadline.
Cancellation/timeout/error retains the token and all referenced buffers. If the
fence succeeds but the later guest status write fails, Finish retains pending
handle references and can retry that write without waiting a consumed token.
A valid queue with no submitted work is legitimately idle; Finish is not a
blanket no-op.

The additive transfer service table has a CPU-free pre-retirement hook. It reaps
its own queue jobs first, then releases handles/command leases. Only afterward
may the existing dispatcher drain other device jobs, destroy pool backing and
release devices. This prevents generic drain from consuming an opaque queue-owned
token behind its owner. BUSY and failures retain the bounded owner. No CPU,
provider, cancellation context or guest callback survives handoff. Legacy pool
service-table layout remains unchanged.

All application GPU operations, complete diagnostic baseline/final snapshots,
and collector polls must share the existing serial graphics worker. Handoff is
outside callbacks on a stopped, exclusively owned CPU. The generic owner lock
also protects handler/retirement ownership and rejects callback reentry.

## Physical alias guards

The additive checked pool service/constructor APIs pass a CPU argument only for
the duration of synchronous validation. No CPU pointer is stored in a graphics
owner. Real Vulkan pool and transfer opt-ins select these checked services.
`satchel_cpu_probe_physical_overlap` compares retained backing intervals, including
locked source mappings; a hole, overflow or unknown protected span rejects.
Candidate permissions are checked separately. Pool/object/storage, queue memory,
command/control and all live device spans are protected by physical disjointness.
Disjoint offsets within the same backing are allowed; aliases are not banned.
Current pool backing/generation/layout is rechecked at later use, not only import.
Pure legacy host-only range queries still compare virtual spans and must not be
used to authorize guest writes in the real alias-capable adapters.

## Original diagnostic and reproduction

`GuestTransferDiagnostic.h` exposes `satchel_guest_transfer_diagnostic` with safe numeric
report fields. Its fixed original guest executes bootstrap/BLR/stubs, creates
three pools and caller-memory queue/command buffers, flushes changed CPU data,
records fill + two dependent copies, submits, finishes, invalidates and compares
values/guards. It explicitly finalizes the handle, command buffer, queue, pools
and device. Success is status0/stage10,282 instructions,94 dispatch events,
3 commands and all state/cleanup flags. This is a synthetic interpreted guest,
not an observed Castlevania graphics call. Parent-verified Duo stage35 passed
this fixture with282 instructions/94 events/3 commands, every queue/handle/command/
device finalizer, upload/flush/GPU/fence/invalidate/value/guard/state/cleanup flag
true, resources0→0 and none retained. Actual Castlevania progressed through
MapMemory to CreateThread; it has not reached a game NVN call or frame.

Cancellation after Submit hands the owner to the bounded collector before CPU
release. A next invocation reaps or reports BUSY. Unexpected handoff refusal is
retained privately. Tests also preserve an unrelated live device/buffer/job and
exercise resource/collector admission failures. No fixture or shader payload is
returned in the report.

Use the frozen CPU archive with physical-overlap support (currently staging):

```sh
export SATCHEL_CPU_ARCHIVE=/tmp/satchel-vixl-probe/alias-staging/macos/libSatchelCPUProbe.a
zsh scripts/test_guest_transfers.sh
zsh scripts/test_guest_transfer_dispatch.sh
zsh scripts/test_guest_transfer_diagnostic.sh
```

All three scripts stage outputs under `/tmp/satchel-guest-transfer-queue`; no
canonical artifact is overwritten. Backend batch reproduction is
`zsh scripts/test_vulkan_backend.sh --ios --staging`. Existing dependencies and
full license notices remain unchanged. MoltenVK itself is a prebuilt dependency;
caller/core sanitizer checks do not instrument its internals.
