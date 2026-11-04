# Checked guest graphics and NVN execution

Current matched implementation also includes real checked NVN device/pool/transfer/compute ownership and an explicitly opted-in scalar raster slice. `GuestVulkanRaster` preserves the common Program owner and prior compute/transfer routes, with actual paired source capture, copied NVN state, block-linear targets and fence-owned publication. Host original interpreter fixtures pass; iOS is compiled but raster is not yet phone validated. No window/present or game frame is claimed. See [RASTER.md](RASTER.md) and run `zsh scripts/test_nvn_raster.sh --ios` for the matched reproducible gate.

The original symbol-boundary component implements exact named-function lookup in authenticated, decoded NSO dynamic
symbol tables, using guest virtual addresses and a caller-provided checked read
callback. It resolves an export such as `nvnBootstrapLoader` without game-specific
addresses, code patching, GOT rewriting, native thunks, or executing any symbol.
It is a reusable prerequisite for interpreter NVN interception, not rendering.

## Source findings and why a separate boundary is needed

The pinned NX2Apple `dm/gfx/nvn/hle_nvn.c` forcibly replaces
`nvnBootstrapLoader`, looks up names, and returns native assembly thunk pointers.
That works only with its native-execution architecture. Its default unimplemented
thunk logs guest arguments and returns zero; **neither behavior is adopted here**.
An interpreter must keep function addresses in guest space and stop before a
registered guest entry point. Native Metal/Vulkan objects must never become
executable guest pointers.

Actual Castlevania symbol metadata confirms imports of `nvnBootstrapLoader` in
main/subsdk0 and a definition in sdk. This component reproduced that finding
through the real package loader and segment decoder entirely in RAM. No code,
module image, key value, instruction operand, or proprietary address was written
to a report/file. rtld had no matching symbol. This validates lookup, not a call
into that export or an implemented NVN operation.

## Integration contract

`GuestSymbols.h` is plain C and has no external dependency. Root may compile
`GuestSymbols.c` into the app target or link the iOS static archive produced by:

```sh
zsh scripts/test_guest_graphics.sh --ios
```

Artifact: `/tmp/satchel-guest-graphics/libSatchelGuestSymbols.a` (arm64 iOS17+).

After authenticating ExeFS, inspecting/decoding each NSO, and constructing its
process map, call:

```c
SatchelGuestSymbolTable table;
SatchelGuestSymbolStatus status = satchel_guest_symbols_nso_table(
    original_nso_bytes, original_nso_size,
    satchel_switch_process_module_address(process, module_index), &table);
```

This reads the NSO0 header only. It obtains text/rodata geometry and the
rodata-relative dynstr (`0x90/0x94`) and dynsym (`0x98/0x9c`) descriptors,
consistent with `Core/NSOLoader.c`. It does not authenticate or decompress an
NSO and must not replace that loader's checks. Keep the table paired with the
same decoded module/base; do not manufacture bases from proprietary offsets.

Then invoke:

```c
SatchelGuestSymbol symbol;
status = satchel_guest_symbols_find_function(&table, checked_read, cancelled, context,
                                       "nvnBootstrapLoader", 18, &symbol);
```

`checked_read` calls `satchel_cpu_probe_read` on the already-mapped CPU and returns
success only if the complete requested range is readable. Call this outside an
active CPU run, from the process worker. Keep context/mappings stable throughout;
callbacks must not reenter the interpreter. Optional cancellation is checked
before reading and for each symbol. It also invalidates a previously discovered
candidate, so a partial scan never returns a seemingly authoritative address.

On `FOUND`, `symbol.address` is an aligned **guest** text address, and the full
declared function extent fits that module's text. Zero-sized ELF functions are
allowed only where a complete aligned four-byte instruction fits. Root must also
verify the current CPU mapping has executable permission before registering a
breakpoint. Imported `SHN_UNDEF` matches return `UNDEFINED` with zero address;
missing names return `NOT_FOUND`. Multiple definitions in one module return
`AMBIGUOUS`. A process-wide caller must also reject multiple defining modules
unless it implements an explicit, accurate ELF lookup/interposition scope.
This component does not guess strong/weak precedence or perform relocations.

Only global/weak `STT_FUNC`, default/protected visibility, ordinary defined
section indices are accepted. IFUNC, absolute/reserved section indices, hidden
or nonstandard visibility, data/local symbols, bad extents and ambiguous names
are rejected. Limits: 65536 symbols, 4MiB string table, 255-byte requested name;
fixed stack buffers, no allocation, no logging. Every symbol/name read is bounds
checked, and the ELF null entry/string byte zero are validated. Status codes
carry no source, symbol-table contents, or decoder exception text.

## CPU hook and future NVN dispatch contract

The CPU owner implements a bounded stop-before-guest-PC API.
Required behavior, coordinated with that owner:

- Register only aligned, readable/executable guest entry addresses; bounded
  count (initially one bootstrap export; later at most32 reviewed entry/stub slots).
- Stop with a distinct breakpoint reason before executing/counting the target
  instruction, preserving all registers/SP/flags and guest memory. No native
  callback runs inside VIXL and no synthetic SVC is injected into game code.
- Dispatch outside the CPU run. Bootstrap's guest C-string pointer is in X0;
  X30 contains the guest return PC. Read bounded strings through checked guest
  reads; never log arbitrary requested names/arguments as shader/game payload.
- Check the return PC against executable mapping and alignment before changing
  X0/PC; preferably perform a transactional checked return. Never resume a
  terminal memory/internal fault. An unsupported function remains an explicit
  stopped condition; do not supply a generic zero/success stub.
- Future returned NVN function addresses must be registered guest stubs or
  actual guest addresses, never host function pointers. Float/vector arguments
  use the existing Q-register API; stack arguments need checked guest reads.

GuestSymbols itself implements no CPU changes or NVN functions; GuestDispatch
below uses the separately validated CPU API. Kernel/CPU owners retain their
files. A resolved export alone does not establish an NVN renderer.

Resource integration still needs an emulator-owned registry mapping guest NVN
object identities to typed host resources, generations and bounds; a distinct
checked GPU virtual-address space; verified command buffer ranges; explicit
shader/texture/descriptor state; and fence completion tied to actual GPU work.
Do not store raw Metal/Vulkan host pointers in guest object memory, treat CPU
pointers as GPU addresses, or signal fences before submitted commands complete.
Our synthetic Vulkan/Metal components provide execution primitives only.

## Validation and limits

`Test.c`: **2,082 ASan/UBSan checks**, using original synthetic NSO/ELF metadata.
Includes relocated guest bases; imports/exports; zero sizes; global/weak and
visibility rules; duplicate definitions; IFUNC/data/reserved symbols; pointer
wrap/bounds/alignment; malformed tables/null entries; callback failures and
cancellation after discovering a candidate; all eight read boundaries; invalid
API inputs; and 2,000 deterministic hostile-header mutations. The iOS static
archive cross-compiles cleanly. No game instruction executes in these tests.

An additional scratch-only, sanitizer-backed Swift CLI reused the current real
XCI/NCA/ExeFS/NSO loaders and checked this component against all four actual
Castlevania modules in RAM. It reported only the public symbol name and whether
it was absent, imported, or a checked definition. Command retained for root:
`python3 /tmp/satchel-guest-graphics/run_real_lookup.py`.

Neither metadata validation nor named lookup authenticates code, proves an NVN
ABI version, resolves runtime dynamically loaded modules, or makes imported code
safe to execute. Those remain the responsibility of their respective boundaries.

## Implemented guest call dispatcher

`GuestDispatch.h/.c` now implements the checked boundary described above using
the CPU owner's explicit breakpoint/PC APIs. `scripts/test_guest_dispatch.sh`
links the frozen CPU archive; coordinate shared archive builds before running it.
With `--ios`, output is `/tmp/satchel-guest-graphics/libSatchelGuestDispatch.a`.
This is separate from the unchanged GuestSymbols archive.

The immutable registry contains537 unique public NVN function identifiers from
NX2Apple commit `8227f80f9b381c680611303319656c4e0c806a50`, extracted from the
`.asciz` name table in `dm/gfx/nvn/nvn_thunks.S`. No native thunk assembly, SDK
implementation, game instruction, or arbitrary name discovered at runtime is
copied into the registry. Reference license and reconstructed-header MIT notice
are retained in `Reference-LICENSE` and `NVN-Header-LICENSE`; preserve both in app
notices. The reference header identifies its type reconstruction provenance and
older53.105 ABI coverage; it is not evidence of every future SDK's private layout.

`satchel_guest_dispatch_build_stubs(NULL,0,&layout)` reports8192 bytes, trap offset0,
first stub offset16, stride8, and537 slots. Calling it with a sufficiently large
buffer emits original AArch64 **data**, interpreted through guest RX mappings:

- The shared trap and unused bytes contain BRK guards, so failure to register
  the breakpoint stops rather than accidentally executing a native callback.
- Each slot sets the AAPCS64 scratch register X16 to its one-based operation ID
  with MOVZ, then branches relative to the shared trap. X30 remains the guest
  caller's return address. The region is position independent.
- Compile-time assertions bound the registry to the64KiB process-map limit,
 16-bit MOVZ ID and signed26-bit branch displacement. No host executable-memory
  allocation occurs and no game NSO bytes are patched.

Root's process constructor copies these bytes once into its optional dedicated
RX mapping outside the contiguous NSO image, visible to QueryMemory as
GeneratedCode (0x14). It must not be labeled NSO Code (3): the actual loader
treated that earlier category as another module and faulted. Root validated the
corrected visible category on the Duo in stage12. Obtain the actual guest base from
`satchel_switch_process_dispatch_address`; pass it with the symbol-resolved SDK
bootstrap address to `satchel_guest_dispatch_create`. The two addresses returned by
`satchel_guest_dispatch_breakpoints` must be merged with other breakpoint owners and
registered through the CPU API. There are only **two breakpoints**, irrespective
of how many public names resolve to stubs.

Dispatch only a fresh `SATCHEL_CPU_BREAKPOINT` result from that same stopped CPU.
The implementation verifies current PC using a zero-budget CPU run, so a stale
result cannot redirect a CPU that has already moved elsewhere. Direct bootstrap
calls use X0 for the name; `nvnDeviceGetProcAddress` uses X1 and does not dereference
its device argument, matching the reviewed reference lookup ABI. Names are read
through checked guest memory, max127 bytes plus NUL. Unknown/unterminated names
stop explicitly and do not return a fake null/success result. A known name gets
its genuine guest stub address; invoking a known unimplemented function stops
with `UNIMPLEMENTED` and its immutable operation ID, without returning to the game.
Only registry-derived names may be reported; never log arbitrary requested names.

Implemented operations:

| Operation | Verified ABI / behavior implemented |
| --- | --- |
| nvnBootstrapLoader | Guest name pointer in X0; returns guest stub address in X0 |
| nvnDeviceGetProcAddress | Guest name pointer in X1; returns guest stub address in X0 |
| nvnDeviceBuilderSetDefaults | Void; X0 points to an opaque64-byte builder; writes private zero-flags defaults |
| nvnDeviceBuilderSetFlags | Void; X0 builder, low32 bits of X1 flags; updates private flags |
| nvnDeviceInitialize | X0 device, X1 builder; returns1 only after a real backend device and checked guest object commit |
| nvnDeviceFinalize | Void; X0 device; validates ownership, clears guest object and destroys its real backend device |
| nvnDeviceGetInteger | Void; X0 device, W1 selector, X2 int*; seven real compute limits only |

The64-byte reservation follows the reference reconstructed `NVNdeviceBuilder`
type. The reference replacement stores its own4-byte flags representation; this
dispatcher instead uses a **private tagged64-byte representation**, version1 with
flags at offset4. It is not Nintendo's internal layout, and must never be handed
to a nonintercepted SDK builder/device function. SetFlags requires our initialized
tag/version, supports unaligned guest buffers through byte access, and preserves
all other bytes. Builder copies retain the tag without relying on host pointers.
`DeviceBuilderGetFlags` remains unsupported because the pinned prototype table
explicitly lacks a verified signature. Queues, rendering and other unimplemented calls remain unsupported.
DeviceGetInteger supports only the seven verified compute limits below. Device initialization requires the explicit
real backend described below; an unattached backend stops with BACKEND_UNAVAILABLE.

Before committing any result, the dispatcher validates/sets the guest LR through
`satchel_cpu_probe_set_pc`. An invalid LR leaves registers and memory unchanged. A
builder update uses exactly one full-span checked CPU write; if that write fails,
it changes no bytes and the original trap PC is restored. Void calls preserve X0.
Lookup commits X0 only after LR validation. These guarantees depend on the
single-worker, nonreentrant CPU contract and stable mappings during dispatch;
callbacks may not mutate the process. The attached backend owns real host
graphics objects; guest memory contains only an independent generation tag,
never a host pointer or backend token.

Every intercepted attempt consumes an event, even when the trap consumes zero
CPU steps. Context creation requires a finite event limit (<=100000) and monotonic
deadline (<=30000ms). These are independent of CPU instruction budgets; the caller
must enforce its overall run budget too. Cancel/deadline checks occur before
preparation and before commit. Any status other than RETURNED must stop the loop.
The dispatcher does not reset its own budget or silently continue unimplemented
calls.

Validation: **4,638 sanitizer-backed original interpreter checks** passed with
the CPU owner's frozen18,879-check archive. Original BLR callers execute every
one of the537 MOVZ/B slots, hit the common breakpoint in exactly3 instructions,
and preserve guest LR. Tests cover lookup/GetProcAddress, checked returns,
callee-saved/vector preservation, unaligned builder defaults and32-bit flag
truncation, adjacent guards, bad object/name/ID/span, read-only output rollback,
invalid LR, unknown/unimplemented stops, stale results, cancellation, deadline,
and a zero-CPU-step return loop stopped by the dispatch-event budget. No imported
game function is called in these tests. This is an actual guest dispatch boundary;
a game-originated NVN clear/present still requires real operations and resource
handling beyond these builders.

## Real device lifecycle and capability boundary

`GuestDeviceBackend.h` defines a versioned, host-only trusted callback table.
Call `satchel_vulkan_guest_device_backend(&backend)` and attach it with
`satchel_guest_dispatch_set_device_backend(dispatch,&backend)` **before any dispatch
events**. The table is copied, must be serialized/nonreentrant, and cannot be
changed after use. It is not a guest-callable native function table.

DeviceInitialize accepts only builder flags0. Each nonzero debug, deferred,
separate-sampler or unknown bit stops explicitly with UNSUPPORTED_FLAGS. The
0x3000-byte `NVNdevice` reservation follows the pinned reconstructed header; our
private representation holds a tag/version and guest generation, with the rest
zeroed. It must not reach nonintercepted SDK object operations. A bounded
eight-slot host registry stores the actual backend token and capability snapshot
separately; duplicate/overlapping guest device spans are rejected.

Initialization checks builder/object spans and guest LR before creating a real
backend device. A failed atomic guest write or cancellation rolls the new device
back and restores the trap PC. Finalization checks the private tag/generation
and LR, atomically clears the guest object, then releases its owned device;
backend failure restores the original object/PC. Context teardown releases any
still-owned devices. A trusted backend must preserve an existing device on
failure and release valid owned devices on destroy. No submissions exist for
these persistent devices yet; adding queues requires explicit pending-work
ownership rather than weakening these cleanup guarantees.

`satchel_guest_dispatch_device_capabilities` returns actual host Vulkan properties
for a live guest device to trusted emulator code. Supported feature bits and
enabled features are distinct; optional enabled features are currently zero.
These are not fabricated NVN version limits. Only the seven directly mapped
compute limits below are exposed through DeviceGetInteger.

`zsh scripts/test_guest_vulkan_lifecycle.sh` passed **1,677 ASan/UBSan checks**
(the initial lifecycle alone passed559; the current total includes queries)
using original interpreted calls and real Vulkan devices. Coverage includes all32
unsupported flag bits, invalid LR, readonly guest-write rollback, guest guards,
actual caps, duplicate/overlap, stale generations, eight-device exhaustion,
finalize and teardown cleanup. The script writes the iOS dispatch archive to
`/tmp/satchel-guest-dispatch/staging/libSatchelGuestDispatch.a`; promote only while
the parent is not building/using the canonical archive. The validated version
is promoted at `/tmp/satchel-guest-graphics/libSatchelGuestDispatch.a`.

## Separate original phone-callable lifecycle diagnostic

`GuestVulkanDiagnostic.h/.c` exposes `satchel_guest_vulkan_diagnostic` with only a
report and optional cancellation callback. It accepts no game input or game CPU.
A new bounded interpreter executes original BLR callers and generated guest
stubs to resolve builder/defaults/init/finalize and create/destroy a real Vulkan
device. A synthetic SVC return marker is handled only by this test harness,
never installed as a Horizon service or injected into game code.

`zsh scripts/test_guest_vulkan_diagnostic.sh` validates host behavior under
ASan/UBSan and emits `/tmp/satchel-guest-diagnostic/libSatchelGuestVulkanDiagnostic.a`
for arm64 iOS17+. Link that separate component alongside the existing CPUProbe,
GuestDispatch and SatchelVulkan outputs; it does not embed duplicate copies. Call
on a worker outside an active CPU run. It owns its CPU/context, caps dispatch
events at1024 with a10-second deadline and bounded instruction slices. Driver
device creation/destruction are synchronous and cannot be interrupted mid-call.

Initial host validation passed: **18 interpreted instructions, 6 dispatch events**,
actual device create/finalize, adjacent object guards, X19/Q8 preservation, zeroed
finalized object, and live-device count restored to baseline. Null report and
pre-cancellation fail without creating a device. iOS cross-compilation passed. Root then validated the original lifecycle on
the maintainer's iPhone Duo in stage16: status0/stage7,18 instructions/6 dispatch events,
deviceCreated/deviceFinalized, guards, preserved state and cleanup all true.
That integrated phone check used the imported Castlevania runtime and graphics
frameworks in the same app process, but the lifecycle fixture kept its own CPU.
The real game had not yet called NVN. This diagnostic submits no frame or shader
and makes no game rendering claim.

## Compute-limit queries

Validated with original fixtures: `nvnDeviceGetInteger` follows the pinned
`nvn_protos.h` void ABI: device X0, `NVNdeviceInfo` low32 bits of X1, output int*
X2. The exact enum values from the pinned reconstructed `include/nvn.h` are:

| NVN selector | Meaning | Retained actual Vulkan field |
| --- | --- | --- |
| 0x1c / 0x1d / 0x1e | Maximum compute work-group X / Y / Z dimension | maxComputeWorkGroupSize[0 / 1 / 2] |
| 0x1f | Maximum invocations in one work group | maxComputeWorkGroupInvocations |
| 0x20 / 0x21 / 0x22 | Maximum dispatched work-group X / Y / Z count | maxComputeWorkGroupCount[0 / 1 / 2] |

These field meanings also match the official
[Vulkan limits reference](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceLimits.html).
The callback snapshot gets them from the real physical device, with no optional
feature requirement. They are upper bounds on future compute submissions, not
a claim that NVN command-buffer submission is implemented. Future dispatch must
enforce all dimensional and total-invocation limits together.

The getter validates a live device and its guest generation, rejects output
aliases into live device objects, validates guest LR, and performs one atomic
four-byte little-endian write. It preserves X0 and other registers. Readonly,
out-of-bounds and wrapping outputs fail without a partial write or resumed PC.
Values0 or above INT32_MAX are backend failures rather than wrapped/clamped lies.
Every other selector stops with UNSUPPORTED_QUERY and leaves output unchanged,
including NVN API/GLSLC versions. Null devices are not accepted for the supported
queries. The reference's copied Tegra constant table, guessed new enum values,
and zero-on-unknown fallback are not adopted. Texture formats, descriptor
layouts, GPU timestamps and compiler ABI ranges need separate real support.

Query validation used the existing real-Vulkan interpreted lifecycle fixture.
All seven values matched the actual retained capability snapshot; tests covered
W1 truncation, unaligned output/guard preservation, unsupported selectors0..0x67,
invalid return PC, readonly/end-of-map/wrapping/null output, live-object alias,
null/stale-generation device, and full cleanup. The separate dispatcher suite
still passed4,638 sanitizer-backed checks. The new iOS archive is staged at
`/tmp/satchel-guest-dispatch/staging/libSatchelGuestDispatch.a` and was promoted
with parent coordination to `/tmp/satchel-guest-graphics/libSatchelGuestDispatch.a`;
the prior lifecycle archive is preserved as `libSatchelGuestDispatch.pre-compute-queries.a`.
No Vulkan framework or backend ABI changed for these queries.
The stage16 phone evidence above predates this query addition.

The current phone-callable original diagnostic also resolves GetInteger and
checks all seven values against the actual caps, using an unaligned guest int
output with adjacent guards. Its appended `compute_queries` report field is7
on success; expected totals are now **48 interpreted instructions,14 dispatch
events**. Host ASan/UBSan validation and iOS cross-compilation passed. This
extended diagnostic awaits phone validation; stage16's18/6 remains accurate
for the earlier fixture. The diagnostic archive path is unchanged, and it still
uses a completely separate original CPU context with no game input.

## Bounded retirement ownership (implemented, promoted)

Backend attachment now reserves one of **four total active-or-retiring context
slots**, before any device can exist. Admission failure leaves the context/table
unchanged. This reservation guarantees that later teardown can transfer ownership
without encountering a newly full retirement queue. Device limits within a
context remain unchanged. Owner operations and handlers share one process-wide recursive pthread lock;
separate runtime workers cannot race the registry. Each CPU/context still needs
its normal exclusive caller lifetime and must not be freed during its own handler.

`satchel_guest_dispatch_retire(&dispatch)` returns1 only when the caller pointer has
been cleared and the context either freed (no backend/resources) or accepted by
its reserved retirement owner. On0 the caller still owns the pointer. Accepted
contexts are closed to new guest dispatch. `satchel_guest_dispatch_retirement_poll`
performs at most four contexts × eight device attempts, with cancellation checks
and no blocking fence wait. IDLE/PENDING/CANCELLED/INVALID/BUSY are explicit outcomes;
the report records reserved/active/retiring contexts, pending devices, release
counts and backend failure status. Stats alone do not perform cleanup.

A device slot is cleared only after drain and actual backend destruction succeed.
BUSY, device loss, failure and cancellation preserve its token/context. Once all
devices are released the owner frees that context and its reservation. The
legacy void `satchel_guest_dispatch_destroy` safely transfers into the same reserved
owner and polls once; it no longer discards failed destroy ownership. App callers
should still migrate to explicit retire/poll so pending resources remain visible
and subsequent collection is scheduled. This is not a new NVN queue connection.

The separate `SatchelGuestDeviceBackendV2` embeds a device table with version2 and a
nonblocking drain callback. Use `satchel_vulkan_guest_device_backend_v2` followed by
`satchel_guest_dispatch_set_device_backend_v2`. The version1 table layout and factory
are unchanged and are never read as a larger table. Version1 retirement can
retain a failed destroy, but an external job owner must reap its jobs before
destruction can succeed; version2 can drain its owned device itself.

The Vulkan version2 drain consumes an actually completed owned-device job and
unlocks its resources; timeout/error retains it. It must not compete with a
separate active submission owner for that same device. No GPU success is
fabricated. DeviceFinalize's existing failure path still restores guest memory/
PC when destruction cannot complete. Merely transferring a pending job is not
a successful guest finalization.

Retired contexts store no CPU pointer, file provider or cancellation context.
They contain only dispatch metadata, device ownership and trusted host backend
callbacks. A non-null backend context must independently outlive final retirement
and must not depend on the freed CPU/provider; the Vulkan factory uses null.
Cancellation context is borrowed for each poll only. Freeing guest CPU memory
after accepted retirement is valid for the current separately allocated GPU
buffers; future borrowed/imported guest allocations would require their own
retained owner. Callers must not enable guest submission before this migration
and backing-lifetime rule are respected.

Mutating retirement/attachment is rejected while any dispatch handler or
retirement poll is active. Reentrant handle/poll calls are also rejected. This
protects callbacks during initialization, finalization and cleanup from freeing
a context still on the stack. Read-only stats remain available.

Validation: `zsh scripts/test_guest_retirement.sh` passed **349 ASan/UBSan
assertions** with original interpreted device initialization and real Vulkan
pending fills. It verifies explicit and legacy ownership transfer, rejected
reentry from create/finalize/drain/destroy callbacks, CPU destruction before
retirement, actual fence drain, cancellation, retained LIMIT/failure responses,
actual cross-worker teardown handoff,
four active+retiring reservations, fifth attachment rejection, and complete
cleanup. Controlled callback-failure fixtures exercise retention without
pretending those failures occurred in the real driver.

The dispatcher4,638 and lifecycle1,677 assertions passed again against the
promoted shared-context/writev/DMB canonical CPU after the handoff guards. Before the final callback-guard extension,
the same dispatcher/lifecycle plus244 retirement assertions also passed against
the frozen shared-context CPU staging archive. Scripts accept optional
`SATCHEL_CPU_ARCHIVE` for coordinated compatibility runs; never link an archive
being rebuilt by its owner.

Staged iOS dispatcher: `/tmp/satchel-guest-retirement/libSatchelGuestDispatch.a`.
Staged Vulkan framework: `/tmp/satchel-vulkan-backend/staging/ios/SatchelVulkan.framework`
with the20-symbol C ABI, version2 factory and nonblocking device drain. The
validated artifacts were promoted with root coordination. Canonical dispatcher
SHA256 is12f718354540748dd5e35941bafb319b0b00911ddc54bf03d3a4a98b9e68f033;
the prior archive is `libSatchelGuestDispatch.pre-handoff.a`. Root migrated App
attachment to version2 and teardown to worker handoff, then a process-wide serial
cleanup queue polls with null cancellation/context for up to50 attempts at100ms.
Unfinished ownership remains in C and collection restarts on the next begin.
Phone retirement execution is not yet claimed.

### Worker handoff during another handler

For Swift worker teardown, use `satchel_guest_dispatch_handoff(&dispatch)`, then
check its accepted result and cleared pointer. It waits for another worker's
active handler/poll to leave the owner lock, transfers into the already reserved
owner, and performs **no backend cleanup**. A valid exclusively owned pointer
outside a callback does not fail just because another handler is active. This
needs no additional pointer queue or per-game memory retention. An unattached
context with no backend is freed on accepted handoff.

The strict `satchel_guest_dispatch_retire` remains nonblocking and can decline when
another worker owns the lock; it preserves the caller pointer. Poll returns
SATCHEL_GUEST_RETIRE_BUSY when another worker holds the owner lock and may be retried;
stats returns0 if unavailable. Same-thread callback reentry is still rejected
without pointer transfer. Do not call blocking handoff on the UI thread, from
a callback, or as permission to free a CPU executing its own handler. Callbacks
must not wait for another thread attempting an owner API under the held lock.
The synchronous driver call already inside a handler cannot be interrupted by
handoff; this is ownership safety, not a hard wall-clock teardown guarantee.

The installed macOS/iOS pthread headers expose the static recursive mutex
initializer used here; no new dependency or heap-allocated handoff node is
introduced. The original concurrency test pauses an interpreted Initialize
inside its backend callback, destroys the other runtime's inactive CPU, starts
a second-thread handoff, observes strict retire/poll refusal and pending handoff,
then releases the first handler and verifies accepted transfer plus cleanup.
The assertions use an atomic counter and synchronized thread coordination.
This test passed ASan/UBSan; no claim of a ThreadSanitizer run is made.

## Next genuine pool/queue bridge: source audit only

The following is a research handoff, not enabled guest methods. Sources are the
same pinned NX2Apple `include/nvn.h`, `nvn_protos.h` and `hle_nvn.c`, plus the
current installed `Tools/CPUProbe/CPUProbe.h/.cpp`. The reconstructed NVN header
is limited ABI evidence, not a complete Nintendo specification.

| Verified pinned ABI fact | Consequence for a narrow bridge |
| --- | --- |
| NVNbufferAddress and NVNcommandHandle are uint64_t | Guest-visible values must remain checked emulator identifiers/addresses, never native pointers |
| Pool-builder64 bytes, pool256 bytes | Tagged guest objects can point by generation to host-owned backing metadata |
| Buffer-builder64 bytes, buffer48 bytes | Later buffer views need pool ownership plus checked signed offset/size, not unrelated allocations |
| Queue-builder64 bytes, queue8192 bytes; command-buffer160 bytes | Object spans and parent generations must be checked before any state transition |
| ClearBuffer(command, GPU-address, size_t, uint32_t) | A first guest clear needs a pool/GPU-address bridge, not necessarily an NVNbuffer object |
| CopyBufferToBuffer(command, source-address, destination-address, size_t, int flags) | Address lookup must validate both complete spans; only explicitly supported copy flags can run |
| QueueSubmitCommands(queue, int count, const command-handle*) and QueueFinish(queue) | Validate bounded arrays, ordered recorded work and actual completion; no default successful finish |

A useful first independent component is a bounded pool/GPU-address registry:
parent device, guest storage range, reviewed flags, unique generation, disjoint
guest GPU virtual range and actual Vulkan backing token. One backing allocation
per pool preserves aliasing when later NVNbuffer views share it. Map returns a
guest CPU virtual address, subject to access flags; GetBufferAddress returns the
separate checked GPU address. The native reference's host-pointer aliasing is
not an interpreter implementation. Negative ptrdiff_t offsets and overflow
must fail before range lookup. Pool/storage/object overlap and stale generations
need explicit rules, not blind memcpy.

CPU_CACHED storage needs real flush-to-GPU and invalidate-from-GPU copy semantics.
CPU_UNCACHED needs synchronization of guest writes before GPU consumption and
completed GPU writes before guest reads; it cannot be claimed merely because
Apple hardware has unified memory. Shader-code, compressed and physical/virtual
pool flags require separate support or explicit rejection. The pinned source's
measured page-size table does not by itself prove all pool-storage alignment
rules, so those rules remain an ABI verification gap.

The promoted CPU API provides complete checked reads, nonmutating `check_range`
and atomic `writev` across up to8 non-overlapping writable spans totaling2MiB.
Shared-context activity blocks host access. Range checks require the full span
inside one mapping with every requested permission; they are snapshots, not
mapping leases. Consumers serialize shared CPU contexts and revalidate each use.
Never simulate preflight by rewriting unchanged bytes, which clears exclusives.
The staged pool profile below rejects oversized writeback transactions instead
of splitting one promised atomic commit or clamping reported pool size.

Recorded commands need a bounded owned representation and real command/control
memory accounting. The reference writes its private records into supplied guest
command memory and returns the recording start as a handle. It also allocates
host fallback segments and calls memory callbacks as native function pointers.
Those fallback/callback mechanics cannot be copied. A guest callback requires
a checked interpreter call/continuation with bounded depth; until implemented,
insufficient command memory must stop explicitly. Returning an opaque handle
also requires every used handle-consuming API to validate its ownership; do not
assume typedef uint64_t proves that nonintercepted SDK code never inspects it.

The existing backend admits one transfer command per job and one job per device.
A first fully connected original test can therefore record/submit one clear or
copy, wait on its real fence and verify checked guest-memory writeback/guards.
Supporting a multi-command queue requires bounded batch ownership or retained
execution cursors so timeout/resume never replays completed commands. Queue and
command finalization must retain referenced pools while work is pending; device
retirement alone is not a substitute for per-resource command lifetimes.

This route tests actual interpreted NVN calls reaching real GPU work without
requiring a Maxwell shader for buffer clearing. It still does not draw a game
frame. Texture layout/format handling, render targets, descriptors, shader-state
snapshots, draw calls and presentation remain separate requirements. Actual
Castlevania NVN calls should select the next methods, rather than adding a broad
set of unobserved no-op implementations.

## Staged pool backing and GPU address component

Implemented and host-validated: `GuestPools.h/.c` supplies a separate checked
builder/pool registry backed by actual Vulkan buffers. It is **not enabled by
the app**; the separately staged opt-in dispatcher slice is described below.
The standalone registry test calls the host component directly
with original guest-memory fixtures; this is not an interpreted NVN pool call or
an observed Castlevania graphics operation. No canonical dispatcher/framework
artifact changed for this work.

The pinned reference's `include/nvn.h` defines builder64/pool256 byte spans and
CPU_CACHED `0x4` / GPU_CACHED `0x20`; `nvn_protos.h` supplies signed `ptrdiff_t`
offsets for flush/invalidate. Those ABI facts inform the adapter. No native
reference memory layout, host-pointer alias or blanket Initialize success is
copied. The full reference/header licenses remain in `Reference-LICENSE` and
`NVN-Header-LICENSE`. No dependency was added.

Supported profile and deliberately narrower limits:

- Exactly CPU_CACHED|GPU_CACHED `0x24`; all other flag combinations stop. Builder
  defaults leave flags0, requiring an explicitly supported profile before init.
- Nonempty, 64KiB-aligned storage base and size, at most2MiB, entirely inside one
  readable/writable guest mapping. The64KiB rule is an adapter restriction, **not
  a claimed general NVN minimum**. Up to8 pools per owner. Vulkan's global limits
  additionally charge actual allocation padding, not only logical pool sizes.
- Disjoint pool storage spans and no overlap with their tagged objects, builders
  or selected device0x3000-byte span. Shared buffer *views within one pool* can be
  supported by later address consumers; distinct aliased pools are rejected.
- Initial CPU snapshot is uploaded to a real buffer with storage/transfer usage
  before committing the full tagged pool object. Backend tokens remain private.
  Only independently allocated guest GPU ranges and pool generations are public;
  both monotonically advance across owners and are never reused on finalization.
- Map returns a guest CPU address. Resolve validates a complete nonempty GPU span,
  current object/device generation, current CPU permissions and actual buffer
  parent/size. Its buffer token/offset output is **trusted host metadata only**,
  never a guest return value or native pointer.
- Flush copies only the checked CPU span to real backing. Invalidate reads real
  backing then uses one atomic guest write transaction. CPU_CACHED writes are
  intentionally not visible to GPU backing until flush; completed GPU writes are
  not visible to CPU memory until invalidate. Negative/overflowing offsets fail.
  Full validation occurs even for zero-length requests; valid in-range empty
  requests are no-ops. Pending submissions return BUSY for nonempty cache access.

The owner borrows a CPU only for each synchronous operation. Its trusted device
validator/context must have an independent lifetime and return the live backend
device plus guest-device generation. It retains no CPU/provider or cancellation
pointer. All CPU contexts and uses of resolved bindings must remain serialized
through an operation; `check_range` does not pin memory against other threads.
Per-owner reentry rejects BUSY. The component does not alter guest PC/registers;
a future dispatcher must validate the return PC **before** a mutating operation.

`satchel_guest_pools_try_destroy(&owner)` is CPU-free. It clears the pointer only after
every owned buffer releases. BUSY or backend failure retains the owner and every
unreleased slot, blocks new operations, and requires later retry after the real
submission owner reaps completion. Parent devices must outlive this cleanup.
Standalone owners must implement that ownership ordering themselves. The new
opt-in dispatcher slice below attaches pools to the existing retirement owner;
the installed app still uses the previous frozen artifacts.

Initialization failure keeps a provisional slot if its new buffer cannot be
released. Failed finalization restores the guest object and retains the slot.
The normal serialized path checks pending state before clearing an object; an
unexpected backend destroy error triggers rollback and explicit error instead
of successful finalization. A driver flush failure may leave backend bytes
changed, as documented by `VulkanBuffer.h`; no GPU transaction rollback is claimed.

Validation/reproduction:

```sh
zsh scripts/test_guest_pools.sh
```

This passed **440 ASan/UBSan assertions** against the frozen canonical CPU range
API and actual MoltenVK. Original tests verify initial upload, guarded partial
flush and actual GPU fill/fence/invalidate,8 maximum-sized pools, allocation
accounting, single-mapping permissions, profile/overflow/device-overlap rejection,
stale/replayed generations, cross-owner GPU address nonreuse, preserved GPR/SIMD
state, callback reentry, pending cleanup after CPU destruction and baseline
resource recovery. Test-only failure shims around real buffer operations cover
failed upload, failed provisional release, failed finalize rollback and removed
CPU storage before copyback. They never fabricate GPU completion and are absent
from the reusable iOS archive. The prebuilt upstream MoltenVK library itself is
not sanitizer-instrumented.

The same script compiled the arm64 iOS17+ archive:
`/tmp/satchel-guest-pools/libSatchelGuestPools.a`, SHA256
`eed8acab22b30a0dc9ff57578d274204d8d9fbf726e8a7e86b8faf8f589987a2`.
It exports create/execute/resolve/try_destroy and requires the existing CPUProbe
and SatchelVulkan C APIs. This staged archive has **not** been run on the phone.

Remaining integration gates include real guest NVN call evidence; parent/retiring
device attachment; validated guest command memory and callback continuations;
bounded queue/command handles and completion ownership; and explicit resource
bindings. The NativeAOT diagnostic remains resource-free compute only: its
default accessor sizes/features are not legitimate snapshots for game shaders.
Game shader compilation additionally needs real device/state/binding snapshots,
bounded decoder/IR work with cancellation and original semantics tests. Pool
completion alone does not authorize feeding imported shaders to that facade.

### Fixed pool diagnostic for a separate phone validation

`GuestPoolDiagnostic.h/.c` exports a fixed original checked-memory fixture,
`satchel_guest_pool_diagnostic`. It accepts only a numeric report and a borrowed cancel
callback/context, never a game CPU, shader or asset. It creates its own small CPU
memory map but **executes no instructions and dispatches no NVN methods**. It
initializes a real pool, verifies initial upload and explicit partial flush,
submits a real GPU fill, waits for its fence, invalidates into checked guest RAM,
checks value/adjacent guards and preserved GPR/SIMD state, then finalizes the pool.
The result contains booleans/counts/status enums only; no tokens or memory bytes.

Call it on the serialized runtime worker outside CPU execution. Return1 with
status0/stage10 means every check and cleanup passed. Failed/unsupported backend
admission propagates a non-success status; cleanup does not erase the failure
code. Actual driver calls are synchronous. The submitted fence wait is capped
at5000ms and checks cancellation; cancellation between all earlier/later stages
cleans unsubmitted resources immediately.

If a job has been submitted and completion is unknown, the function releases its
synthetic CPU and retains at most one private job/device/pool owner with no CPU,
provider or callback-context dependencies. `resources_retained` reports this
state. The next invocation first polls the same real fence with timeout0, then
destroys backing/parent, or returns BUSY/error while retaining ownership. Only a
successful fence consumes the job; no completion or destroy is forced. A
reentrant/concurrent call returns BUSY without reading mutable state. Other
backend callers' devices, buffers and jobs remain untouched. Irrecoverable
driver failure can retain this bounded private owner until process exit.

```sh
zsh scripts/test_guest_pool_diagnostic.sh
```

The original host fixture passed **107 ASan/UBSan assertions**, including guarded
GPU output, all nine stage cancellation checkpoints, deterministic cancellation
at the first submitted wait, next-call fence reap, reentry, unrelated live job
preservation and full device/buffer registry admission failures. No failure shims
are used by this diagnostic or its host test. Upstream prebuilt MoltenVK remains
outside sanitizer instrumentation. The script also compiled arm64 iOS17+:

- `/tmp/satchel-guest-pool-diagnostic/libSatchelGuestPoolDiagnostic.a`, SHA256
  `6005f40e4e19105276b0209afe471a11d1f0784c188794efe75817ad9f3d63be`
- Link it with staged `libSatchelGuestPools.a`, canonical CPUProbe and the existing
  SatchelVulkan framework. Include `GuestPoolDiagnostic.h` in the App bridge.

These staged archives are frozen for root integration. No device run has been
performed for this new fixture yet. Canonical dispatcher and SatchelVulkan binary
hashes remain respectively `12f71835…` and `07f9f601…`; no framework rebuild or
new dependency is required.

## Staged interpreted NVN pool dispatch

Implemented and host-validated, not enabled in App: `GuestVulkanPools.h/.c`
provides an explicit `satchel_guest_dispatch_enable_vulkan_pools(dispatch)` opt-in.
Call it after attaching the **exact real Vulkan v2 factory table**, before the
first dispatch event. It requires matching create/destroy/capabilities/context
and drain callback identities. Absent, v1, wrapped or fake device backends reject;
merely returning an integer that resembles a Vulkan device token does not enable
this path. The generic dispatcher still links without Vulkan or GuestPools.

The private `GuestPoolServices.h` table separates generic owner machinery from
the real Vulkan-backed component. It is a trusted native implementation table,
never guest data or guest function pointers. The dispatcher supplies its own
device validator: it finds the live private DeviceSlot, checks the full guest
object span and tag/generation, queries the actual backend, and obtains the
backend token from that private slot. It never interprets a guest integer as a
backend token. The table/owner retain only the dispatcher context, which outlives
pool retirement; no CPU, storage provider or cancellation context is retained.

Only these verified pinned `nvn_protos.h` signatures are connected:

| Operation | Guest ABI and behavior |
| --- | --- |
| MemoryPoolBuilderSetDefaults | X0 builder; void, X0 preserved |
| MemoryPoolBuilderSetDevice | X0 builder/X1 device; void |
| MemoryPoolBuilderSetStorage | X0 builder/X1 CPU address/X2 size_t; void |
| MemoryPoolBuilderSetFlags | X0 builder/W1 int; only low32 bits interpreted, void |
| MemoryPoolInitialize | X0 pool/X1 builder; boolean1 only after real backing and tagged object commit |
| MemoryPoolFinalize | X0 pool; void, rejects pending backing |
| MemoryPoolMap | X0 pool; returns checked guest CPU address in X0 |
| MemoryPoolGetBufferAddress | X0 pool; returns independent guest GPU address in X0 |
| MemoryPoolFlushMappedRange | X0 pool/X1 signed ptrdiff_t/X2 size_t; void |
| MemoryPoolInvalidateMappedRange | Same argument ABI; actual completed backing copied through atomic guest write |

Every call validates LR before any pool/output/driver mutation. Pool writes use
the checked atomic transaction; failures restore the original trap PC. Only the
three non-void operations update X0. Other registers, including X1/X2, retain
their stopped-call values. Cancellation/event/deadline checks occur before a
pool operation; bounded allocations/copies and synchronous driver calls are not
preemptible mid-call. Unsupported pool methods remain explicit stops. The same
64KiB/2MiB/8-pool cached profile and general-NVN limitations still apply.

Pool builder/object/storage ranges cannot overlap **any** live device object,
not only their selected parent. Conversely, when pools are enabled, new device
objects, device-builder writes and GetInteger outputs cannot overlap live pool
objects/storage. Direct guest writes may still corrupt a tag; subsequent checked
lookup rejects it while private backing remains owned for cleanup.

DeviceFinalize returns RESOURCE_BUSY while any child pool, including provisional
failed-initialization backing, remains. Retirement first drains actual device
jobs, then destroys pool backing, then parent devices. A BUSY or failed pool
release retains both the pool owner and its parent devices in the reserved
retirement slot. The collector never calls the device validator or touches guest
RAM during this cleanup. The existing four active+retiring owner bound and v1
compatibility are preserved.

`satchel_guest_dispatch_vulkan_pool_binding` is a **trusted host-only** checked bridge
for original tests/future command consumers. It returns backend IDs and a complete
buffer span after current owner/tag/range validation; those IDs must never enter
guest memory or return registers. It grants no lifetime lease across further
guest execution. Future recorded commands must add explicit pool references and
queue ownership before retaining such bindings. No guest queue submission API is
implemented by this slice.

Validation:

```sh
zsh scripts/test_guest_pool_dispatch.sh
```

**1,336 ASan/UBSan assertions passed.** Original BLR callers execute the original
MOVZ/B stubs, stop at the common interpreter breakpoint, invoke all ten methods,
and return through a real guest SVC marker. The test validates real initial
upload and guest-invoked cache flush/invalidate around a host-submitted original
GPU fill/fence. Thus the cache methods are now genuinely interpreted NVN calls;
the GPU fill is still a trusted test submission, **not a guest NVN queue call**.
Other assertions cover exact backend opt-in, all ten invalid-LR cases, void ABI
preservation, profile/range/overlap rejection, pending finalization, stale GPU
addresses, and CPU-free retirement. Test-only service-destroy failures verify
pool-before-parent retention without fabricating GPU completion. No game data is
used and no game frame is produced.

Affected regressions passed:4,638 generic dispatcher assertions;1,677 original
real-device lifecycle/query assertions;349 retirement assertions;440 standalone
pool assertions;107 checked-memory pool-diagnostic assertions. All used frozen
CPU archives and ASan/UBSan; prebuilt MoltenVK is not sanitizer-instrumented.

Staged arm64 iOS17+ artifacts under `/tmp/satchel-guest-pool-dispatch`:

| Archive | SHA256 |
| --- | --- |
| libSatchelGuestDispatch.a | 1b04e0dbb5fd45c34f2e3c41d5bb45d0af2033a4688390b302f1bf8c6c0c5410 |
| libSatchelGuestPools.a | 0a3ab0c4f6cb6253f3205a54c15174a1157931345a9811f6932a379998cfd2f0 |
| libSatchelGuestVulkanPools.a | 9b56cf144fbde587c2a8a05f4a86a0be2a90dd14829602ff17cf7d277b769e4c |

No canonical artifact was replaced. Existing App builds remain compatible with
the frozen public headers; only dispatch status enum values were appended, and
new unused host APIs were added. Promotion and App opt-in require root's next
integration gate. This new interpreted slice has not been validated on the Duo.

Additional shader gate confirmed by the independent CPU-agent source audit:
the pinned translator unconditionally declares subgroup/image capabilities and
its default accessor enables Float64 while the Vulkan device enables no optional
features. Default unresolved-memory lowering can also replace reads with zero or
remove writes after logging. Do not feed game shaders into that facade: actual
enabled-capability snapshots, supported emitted capability checks, fail-closed
memory resolution and bounded translation remain prerequisites. None of those
translator defaults or frozen sources were changed by the pool work.

### Separate interpreted phone fixture, awaiting device validation

`GuestPoolDispatchDiagnostic.h/.c` adds
`satchel_guest_pool_dispatch_diagnostic` without changing or replacing the older
checked-memory `satchel_guest_pool_diagnostic`. Its report embeds that numeric memory
report and adds instruction/event counts, dispatch/CPU stop statuses and a
device-finalized flag. Success is return1, `memory.status==0`, `memory.stage==10`.

The original successful path executes **78 interpreted instructions and26
dispatch events**:13 named bootstrap resolutions and13 real device/pool methods,
using BLR, the original MOVZ/B stubs, the checked common breakpoint and a guest
SVC return marker. The pool Map and GetBufferAddress returns are guest addresses;
only the trusted host resolver obtains private backend bindings for the fixed
original GPU fill. Real upload, explicit flush, fence completion, guest-invoked
invalidate, output/adjacent guards, GPR/SIMD preservation, and explicit pool then
device finalization are checked. There is still no guest NVN queue submission or
game frame in this fixture.

All teardown uses the existing reserved retirement owner. Cancellation after
submission transfers the dispatcher without forcing a fence, destroys the
synthetic CPU and reports retained resources. A later call polls the collector
before admitting another fixture, or returns BUSY with ownership preserved.
An unexpected handoff refusal keeps a bounded private deferred dispatcher pointer
for retry; it has no CPU/provider dependency. Cancel contexts are never retained.
The collector can also drain other already-retired owners; active contexts and
unrelated Vulkan resources remain untouched. Run on the serialized worker, not
inside an interpreter/dispatch callback or on the UI thread.

```sh
zsh scripts/test_guest_pool_dispatch_diagnostic.sh
```

**109 ASan/UBSan assertions passed**, covering exact counts and result/guards,
all stage cancellation points, submitted cancellation followed by next-call
reaping, reentry, unrelated pending GPU work, and full device/buffer/retirement
admission limits. The same script compiled arm64 iOS17+:
`/tmp/satchel-guest-pool-dispatch-diagnostic/libSatchelGuestPoolDispatchDiagnostic.a`
SHA256 `d19f5009739a027d25b84be87ea83dfa6d349d24d061c1eaa495ad78ec5cc199`.
It exports only `satchel_guest_pool_dispatch_diagnostic`.

Integration requires the three staged pool-dispatch archives listed above plus
the existing CPU and SatchelVulkan framework. Replace dispatcher and pool archives
as a matched set; add GuestVulkanPools and this new fixture archive. The existing
host-only fixture archive remains binary-compatible and passed107 regression
assertions against the new pool source. Preserve old artifacts before promotion.
No Vulkan framework change is necessary. These candidate archives and headers
are frozen; canonical artifact replacement/App migration remains root-owned.

### Matched-set promotion and App serialization contract

At root's explicit integration gate, the above validated candidates were promoted
without source changes or redundant test reruns. SHA256 and arm64 architecture
were checked before replacement; exported C symbols and final hashes were checked
afterward. Canonical locations now are:

- `/tmp/satchel-guest-graphics/libSatchelGuestDispatch.a`: `1b04e0db…`
- `/tmp/satchel-guest-pools/libSatchelGuestPools.a`: `0a3ab0c4…`
- `/tmp/satchel-guest-graphics/libSatchelGuestVulkanPools.a`: `9b56cf14…`
- `/tmp/satchel-guest-diagnostic/libSatchelGuestPoolDispatchDiagnostic.a`: `d19f5009…`

Previous canonical dispatcher/pool archives are preserved under
`/tmp/satchel-graphics-before-pool-dispatch-adsy9qii`. The SatchelVulkan framework
remains byte-identical (`07f9f601…`); the older checked-memory diagnostic archive
also remains unchanged. This supersedes the earlier "not promoted" state above;
it still does not establish a device run or a game-originated NVN call.

Reviewed App contract: every **whole** GPU diagnostic, including baseline/final
resource snapshots, and every actual guest dispatch handler must run on the same
global graphics queue as retirement polling. Future direct Vulkan mutations,
submissions and trusted binding use must follow that same serialization rule.
Handoff stays on the runtime worker after its stopped handler returns; it waits
behind the native owner lock if necessary and only transfers ownership, with no
GPU cleanup. The retired owner remains independent of CPU/provider lifetime.
Cancel callbacks must not synchronously reenter the graphics queue; avoid nested
queue.sync calls. This prevents retirement or another runtime worker's graphics
operations from changing a diagnostic's resource-count baselines mid-check.
