# Bounded VIXL CPU probe

Implemented and host-validated: an isolated, deliberately incomplete AArch64
interpreter with a C interface, virtual guest addresses, copied memory segments,
instruction budgets, cancellation, and a stop at each SVC. It uses no JIT,
executable guest mappings, runtime native callbacks, or per-game compilation.
It is suitable for an experimental **bounded CPU diagnostic**, stopping at the
first unsupported instruction or service. It is not a Switch emulator, game boot,
Horizon implementation, graphics renderer, or evidence of App Store acceptance.

## Source and reproduction

VIXL 8.0.0, BSD 3-Clause, pinned commit:
`7b792dcee6250db405f7f692d3853786b4d2a92d`.

- [Official source at the pinned revision](https://gitlab.arm.com/runtimes/vixl/-/tree/7b792dcee6250db405f7f692d3853786b4d2a92d)
- [Simulator API](https://gitlab.arm.com/runtimes/vixl/-/blob/7b792dcee6250db405f7f692d3853786b4d2a92d/src/aarch64/simulator-aarch64.h)
- [Simulator implementation](https://gitlab.arm.com/runtimes/vixl/-/blob/7b792dcee6250db405f7f692d3853786b4d2a92d/src/aarch64/simulator-aarch64.cc)
- Full retained license: `VIXL-LICENCE`.

The source checkout and compiled outputs remain outside this repository. The
small retained `vixl-memory-guard.patch` adds checked guest-address translation
to `Memory::Read/Write` and disables the host-pipe memory-probing path. No upstream
assertion is removed. The build requires `VIXL_DEBUG` and the upstream
`VIXL_NEGATIVE_TESTING` option so assertions throw and can be caught at the C
execution boundary. `VIXL_CODE_BUFFER_MALLOC` avoids executable allocator paths.

On an Apple Silicon Mac with Xcode and Python 3:

```sh
git clone --depth 1 --branch 8.0.0 https://gitlab.arm.com/runtimes/vixl.git /tmp/satchel-vixl-probe/vixl
zsh scripts/test_cpu_probe.sh --ios
```

The script verifies the exact commit and patch, applies only that patch to a
clean checkout, builds outside the repository, runs native comparison and
ASan/UBSan tests, and optionally compiles an iOS static library. It does not
download dependencies, sign, install, or contact a device. Flags `--source` and
`--output` select other external directories. The iOS archive is:

`/tmp/satchel-vixl-probe/build/ios/libSatchelCPUProbe.a`

Link the archive with libc++ and include `CPUProbe.h`. The header has no VIXL
types and may be included in a Swift bridging header. The archive includes the
adapter and VIXL objects, so do not compile `CPUProbe.cpp` separately into the
same application. All original fixture assembly is in `fixture.S`; there are
no game binaries, keys, or generated game source here.

## Embedding contract

`satchel_cpu_probe_create` takes 1–32 `SatchelCPUProbeRegion` records. Each provides a
virtual guest address, initial bytes, memory size including zero-filled tail,
and permissions (`READ=1`, `WRITE=2`, `EXECUTE=4`). Those permission bits must be
translated from the caller's format, not copied from unrelated flag fields.
The adapter rejects overlap, address overflow, zero lengths, invalid input
lengths, W+X mappings, unaligned executable bases, and aggregate memory above
256 MiB. Code must be readable and executable. It copies every segment; the
caller's byte buffers may be released when creation returns.

Supply a separate RW stack region and set SP using `satchel_cpu_probe_set_sp`.
For example, a zero-filled 128 KiB region has `bytes=NULL`, `bytes_size=0`, and
`memory_size=131072`. No guest register initially contains a host pointer.
Set initial X registers using `satchel_cpu_probe_set_x`. Guest addresses remain
virtual; do not add the host buffer address to guest registers or entry points.

`satchel_cpu_probe_set_tls(context, tpidrro_el0, tpidr_el0)` supplies independent
guest thread-register values; `satchel_cpu_probe_get_tls` returns them through two
required output pointers. They default to zero. The process wrapper validates
its chosen TLS mapping; the CPU accepts all 64-bit register values and checks
memory permissions/bounds only when guest code dereferences one. Exact MRS
reads both registers, and exact MSR writes only TPIDR_EL0. TPIDRRO_EL0 is set
by the external process/scheduler API. These operations never read, expose or
overwrite host TLS, and do not invoke VIXL's system-register visitor. Contexts
remain nonconcurrent; there is no scheduler or automatic context switching.
The pinned VIXL system visitor does not implement these TLS registers; its
existing system visitor override remains fail-closed for everything else.

The clock slice adds exact read-only `CNTPCT_EL0`,
`CNTVCT_EL0`, and `CNTFRQ_EL0` reads. A process-owned epoch survives context
creation/destruction and uses Darwin `CLOCK_MONOTONIC_RAW`: elapsed time is
monotonic, unaffected by wall-clock adjustment, and includes idle/system sleep.
Both counter registers use the same virtual count with a zero virtual offset;
the frequency register is 19,200,000 Hz. Host architectural register values and
host clock epoch are never exposed. The exact conversion is
`(ns / 625) * 12 + ((ns % 625) * 12) / 625`, avoiding the overflow in a direct
`ns * 12` multiplication for every uint64 input. Equal consecutive reads are
valid; this is elapsed time, not an instruction counter.

`satchel_cpu_probe_get_system_tick(context, &tick)` uses that same process clock for
future service integration. It rejects null outputs, active/reentrant/sibling
access, host clock failure, and backwards samples without changing the output.
A failed guest counter read stops with `INTERNAL_FAULT` before changing the
destination or advancing PC. Frequency reads do not need a host time sample.
Counter reads leave flags, SP, SIMD, memory and reservations unchanged. Guest
counter writes, timer control/compare registers, interrupts, scheduling and wait
services remain unsupported. Counter support alone is not a timer service.

The ABI comes from pinned [libnx counter.h](https://github.com/switchbrew/libnx/blob/b156b4774fb2df7b93402169509266da7547fc4c/nx/include/switch/arm/counter.h)
(`CNTPCT`, `CNTFRQ`, and the 12/625 conversion),
[libnx's SVC wrapper](https://github.com/switchbrew/libnx/blob/b156b4774fb2df7b93402169509266da7547fc4c/nx/source/kernel/svc.s)
(GetSystemTick is SVC 0x1e), and
[Arm's Generic Timer guide](https://developer.arm.com/-/media/Arm%20Developer%20Community/PDF/Learn%20the%20Architecture/Generic%20Timer.pdf?revision=c710e7a7-9f52-4901-8c9d-91b19f44f9c7)
(physical/virtual counter relationship). Host clock behavior was checked against
the installed Darwin `clock_gettime(3)` manual. Original assembler fixtures,
independent 128-bit conversion comparisons, and an independently bracketed live
host-clock interval test cover this slice. The host hardware frequency is not
assumed to match the emulated Switch frequency. Deterministic clock/failure
seams exist only in the host test build and are absent from iOS exports.

Build the clock staging gate without replacing canonical artifacts:

```sh
zsh scripts/test_cpu_probe.sh --output /tmp/satchel-vixl-probe/clock-staging --ios
```

That staged gate passed 120,940 total original-fixture ASan/UBSan/native checks
and compiled the iOS arm64 archive. Its public tick getter is exported and all
test-only clock seams are absent from iOS. It was subsequently promoted after
the Process integration gate. On-phone counter/game progress remains separate
evidence in `Validation/phone-runtime-progress.md`.

`satchel_cpu_probe_get_fp_status` and `satchel_cpu_probe_copy_fp_status` are narrow
thread-creation prerequisites. Fresh contexts have actual VIXL FPCR reset0 and
provably known reset FPSR0. Copy requires distinct, nonterminal contexts sharing
one process map and preserves all other state. The pinned simulator's
`FPProcessException` is empty; the first checkpoint therefore marked arithmetic
status unknown rather than inventing zero. The separate integer-FP staging
described below replaces only the two already admitted scalar operations with
accurate owned status. Any unmodeled status still rejects queries/copy without
changing outputs/destination. No arbitrary FPCR/FPSR system-register access is
introduced. Process code must
explicitly zero the new thread's Q registers; generic VIXL SIMD defaults remain
unchanged. This models the public thread-creation distinction between copied
FP status and cleared SIMD data in pinned
[Atmosphere thread context initialization](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libmesosphere/source/arch/arm64/kern_k_thread_context.cpp).
The isolated FP checkpoint passed121,005 checks and compiled for iOS under
`/tmp/satchel-vixl-probe/fp-status-staging`; it does not establish a scheduler.

`Binary64.cpp` is an original dependency-free integer model of **FADD D and
FMUL D only**, staged under `/tmp/satchel-vixl-probe/integer-fp-staging`. It admits
nearest-even, gradual underflow and ordinary NaN propagation or DN: FPCR must
be0 or0x02000000. All traps, alternative FP features, other rounding modes,
FZ/AHP and reserved fields reject before modifying guest state. Multiplication
uses an exact53-by53-bit product. Addition aligns operands exactly for exponent
gaps up to74, keeping the intermediate below128 bits; beyond that gap the
smaller nonzero value cannot change nearest-even rounding of the larger one,
but it still raises inexact. Rounding explicitly covers carries, signed zeros,
subnormals, overflow and tininess **before** rounding. Signaling NaNs precede
quiet NaNs, then operand order chooses the propagated payload; DN produces the
default NaN. Invalid, overflow, underflow and inexact flags accumulate. With
FZ disabled, subnormal inputs do not generate IDC; existing IDC/DZC/QC flags
are preserved, not invented or cleared.

No production instruction reads or writes the host FPCR/FPSR for this model,
and no floating-point operation calculates its results. The private model has
no allocation, callback or unbounded loop. Installed Xcode27.2 `fenv.h` and
Arm's [Cortex-A57 TRM](https://documentation-service.arm.com/static/5e906b9fc8052b1608760b6b)
provide register/exception definitions. The pinned
[QEMU Arm configuration](https://github.com/qemu/qemu/blob/v9.2.0/target/arm/cpu.c)
independently confirms base-Arm pre-rounding tininess and NaN priority; no QEMU
implementation or dependency is reused.

`native-fp-oracle.S` is original **test-only** code, linked only into the host
test executable. It executes fixed native FADD/FMUL while saving/restoring the
host floating environment, allowing independent value/flag comparisons against
manual edge cases, all exponent gaps, random values and cumulative status.
It is absent from both production archives. Its temporary environment is not
safe to present as isolated from arbitrary asynchronous signal handlers:
[XNU's signal path](https://github.com/apple-oss-distributions/xnu/blob/xnu-11417.121.6/bsd/dev/arm/unix_signal.c)
saves/restores NEON state but does not establish a separate FP environment at
handler entry. Production avoids that issue entirely by using integer code.

The integer-FP staging gate passed193,687 original/native/ASan/UBSan checks and
compiled the iOS archive. It includes every finite exponent gap, DN/NaN and
pre-rounding tininess cases, unsupported-control rejection, cumulative flags,
post-arithmetic thread-state copy and guest exclusive-reservation preservation.
Native test-oracle symbols are absent from both libraries; iOS also excludes
all CPU test seams. This gate is staged, not a device or scheduler validation.

The retained-backing alias slice is staged under
`/tmp/satchel-vixl-probe/alias-staging`. `satchel_cpu_probe_map_alias(destination,
source,size)` locks a page-aligned RW source and adds RW destination views of
the same allocated bytes. Initial data segments and dynamic allocations both
qualify; executable/protected sources, existing targets, nested aliases and
overlapping ranges reject. `satchel_cpu_probe_unmap_alias` checks physical identity
and corresponding source offsets before restoring source RW permissions and
removing destination views. Partial page-aligned unmap is supported. The CPU
does not invent Horizon memory states or validate Stack reservations; Process
owns those rules.

Views retain their allocation across context destruction and source splitting.
Aliases add no physical backing charge; the256MiB cap counts each allocation
once. The32-view limit still applies after transactional splitting/coalescing.
Map/unmap plans allocate before committing and preserve all bytes, permissions
and reservations on failure. Successful commits clear shared reservations.
Whole-span reads/writes may cross views only at contiguous physical offsets in
one backing, with the requested permission in every view. A crossing into a
locked view or independent backing fails before any access. Multiwrite also
rejects physical destination overlap. No native memory pointer is exposed.

`satchel_cpu_probe_physical_overlap` checks two completely mapped nonempty spans
against retained backing offsets, including inaccessible source views. It
validates both whole spans before returning an overlap boolean; holes, overflow,
null/busy/reentrant calls fail without changing the output. A caller must treat
failure as unknown, not disjoint. Fixed32-entry fragment arrays support spans
across multiple allocations with no allocation or exposed backing identity.
GPU object/storage guards can therefore reject a protected physical overlap
even when the guest uses different virtual addresses. This neither grants
permissions nor pins mappings; permissions and overlap must be rechecked at use.

Resize/free/range replacement of any allocation with a live alias is rejected,
including a valid Horizon heap growth while one of its pages is locked. That
is an explicit unsupported operation for the Process caller, not permission to
report fabricated kernel failure/success. Once the allocation's final alias is
removed, contiguous source views coalesce and its prior exact-base resize
contract works again. Multiple source allocations can be aliased in one call,
but accesses spanning independent physical allocations remain unsupported.
This original retained-view implementation is informed by the public
[MapMemory/UnmapMemory policy](https://github.com/Atmosphere-NX/Atmosphere/blob/6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8/libraries/libmesosphere/source/kern_k_page_table_base.cpp);
no upstream kernel implementation is copied into this adapter.

The combined alias/FP/physical-overlap gate passed125,155 original-fixture
native/ASan/UBSan checks and compiled the iOS archive. Tests cover both allocation
failure seams, initial and dynamic sources, partial restoration, exact256MiB
physical capacity,32-view limits, cross-page interpreted LDR/STR, independent
page-state transitions, shared-context lifetime, exclusive reservations and
complete-span physical comparisons. New public exports are present and test
seams are absent from iOS. Archives remain staged for Process/GPU integration;
this isolated gate does not establish on-phone MapMemory or secondary threads.

`satchel_cpu_probe_run(context, instruction_budget, cancellation, user_context)`
returns a result with reason, completed steps for this call, PC, SVC immediate,
and a bounded generic diagnostic. SVC stops before any Horizon side effect,
reports the SVC instruction's PC, and internally advances to the next guest
instruction. The process wrapper can set result registers and resume after
handling a supported service. Budget
and cancellation report the next instruction's PC and can also resume.
Unsupported, memory and internal faults are terminal; repeating run returns
the saved terminal result and performs no further instructions. There is no
automatic success for an unsupported service or instruction.

`satchel_cpu_probe_set_breakpoints(context, addresses, count)` copies up to 32
distinct, 4-byte-aligned executable guest addresses shared by all sibling contexts. Count zero clears the set;
invalid input leaves the old set intact. A `SATCHEL_CPU_BREAKPOINT` result reports
the guest PC before its instruction is fetched, executed or counted. The
instruction field is zero, and guest GPRs/SP/NZCV/SIMD and memory are unchanged.
Repeated runs stop again at that address until the caller explicitly moves
PC or removes the point. A zero budget returns `BUDGET` first; cancellation is
checked before a breakpoint on each instruction boundary.

`satchel_cpu_probe_set_pc(context, address)` validates a complete aligned 4-byte
executable fetch before changing the guest PC. Both mutation APIs reject an
active run/callback and terminal-fault contexts. Neither can revive a fault.
For an external dispatcher returning through guest X30, read the guest LR and
call `set_pc` first; only after it succeeds write X0 or other result registers.
An invalid return therefore cannot publish a result before failing validation.
The dispatcher must marshal arguments and implement any actual service itself.
It also needs a bounded event count/deadline: repeated zero-step trap returns
cannot be bounded by guest instruction count alone.
Symbol resolution supplies guest metadata, never a native callback or function
pointer. This CPU API does not resolve NVN symbols, execute native thunks,
invent SVCs, or implement graphics.

`satchel_cpu_probe_write` provides checked guest-memory output for external process
service implementations. It accepts only a nonempty span with a non-null source
that fits within one retained backing at contiguous physical offsets. Every
covered view must be writable. It checks the complete span before copying,
rejects read-only/executable/locked views, and leaves all guest
bytes unchanged when validation fails. It does not expose a host pointer,
choose service behavior or make contexts safe for concurrent calls.
Successful host writes clear all shared-context exclusive reservations so a stale
store-exclusive cannot overwrite a service/dispatcher write.

`satchel_cpu_probe_check_range(context, address, size, required_permissions)` checks
a nonempty, nonoverflowing span against one contiguous retained backing. Every
requested permission must be present in every covered view; zero/unknown
permission masks reject. It acquires
the shared process guard, rejects active/reentrant access, and performs no
allocation, byte copy, reservation change or host-pointer exposure. This is a
snapshot, not a mapping lease: resource users revalidate before each later use.

`satchel_cpu_probe_map_missing_rw` and `satchel_cpu_probe_unmap_rw` atomically add/remove
page-aligned dynamic RW spans. Both require a nonempty range and an output for
the actual number of bytes added/removed. Initial mappings are immutable to
these operations. Map preserves existing bytes and zeroes only new gaps;
unmap tolerates gaps and preserves surviving prefix/suffix fragments. Results
retain the existing 32-region and 256 MiB aggregate limits. Invalid, busy,
limit and allocation failures preserve all mappings, bytes and output values.

Map coalesces existing dynamic backings intersecting its explicit request,
including their complete extents. It does not absorb merely adjacent outside
mappings: the process wrapper expands the range to include adjacent physical
spans it owns when contiguous cross-page accesses are required. A separately
managed adjacent heap retains its exact-base `resize_rw` identity. A wholly
covered map or wholly free unmap is a no-op, reports zero and preserves exclusive
reservations. Actual commits clear every shared-context reservation. All new
backing/fragments are allocated and copied before any published vector moves;
the final commit uses only noexcept moves/swap. Temporary copies can increase
host memory during the transaction; allocation failure rolls back cleanly.
The CPU supplies storage, not Horizon alias policy, accounting or SVC results.

`satchel_cpu_probe_writev(context, writes, count)` adds an atomic multi-span output
transaction for service data plus its TLS reply. `SatchelCPUProbeWrite` contains a
guest destination address, trusted host source pointer and byte count. Require
1–8 nonempty spans and at most 2 MiB aggregate. Each destination must fit one
retained backing at contiguous physical offsets, with write permission in every
view. Independent neighboring backings cannot be crossed by one span. Reject
virtually or physically overlapping destinations (including identical spans);
adjacent disjoint spans are valid.
All arithmetic and mappings are checked before source snapshots are allocated.

The caller keeps all native descriptors/source spans valid and stable throughout
the call. Descriptors are copied first, then every source byte is copied to one bounded
temporary buffer before the first guest write. This permits aliased sources,
including actual guest backing supplied internally, without exposing a guest
host-pointer API. The process guard covers preflight, allocation, snapshots and
commit. The final commit makes no allocation/callback and has no recoverable
failure path. A bad second span, allocation failure or busy process leaves all
guest bytes and exclusive reservations unchanged. Successful commit clears all
shared reservations once. This atomicity is with respect to serialized CPU/API
access and reported failures, not host termination or arbitrary invalid native
pointers supplied by trusted C callers.

Return `SATCHEL_CPU_WRITE_OK` (zero), or distinct `INVALID`, `LIMIT`, `MEMORY`,
`ALLOCATION`, `BUSY`, `INTERNAL` failures. Do not interpret this result like the
single-write API's boolean success. The provider must finish integrity checking
and staging the asset first. The caller commits handle/service state only after
`writev` succeeds; the CPU does not own those objects. Single-write behavior and
its existing limits are unchanged. Mac-only tests inject snapshot allocation
failure and perform a guest-backed swap internally; neither test seam is present
in the iOS library and no native guest-memory pointer escapes them.

`satchel_cpu_probe_resize_rw(context, base, size)` creates or resizes a dynamic
read/write region at an exact page-aligned guest address. Size is a multiple of
4096; zero removes an existing dynamic region. Initial image/stack/TLS mappings
cannot be replaced or removed. The existing 32-region and 256 MiB aggregate
limits apply to committed guest bytes, not virtual reservations. A resize
preserves the retained prefix and zeroes growth; removed/truncated bytes never
return on regrowth. Mapping changes clear old exclusive reservations.

Map results distinguish `OK`, invalid layout/arguments, limits, allocation
failure and active-run `BUSY`. Failure leaves guest bytes and mappings unchanged.
Allocation and copying finish in temporary storage before publication, so a
resize temporarily retains both old and new buffers (each is still bounded by
the aggregate cap). Mutations from cancellation callbacks are rejected, even
for a no-op resize. The caller must serialize scheduling; the process guard
rejects busy operations rather than scheduling or waiting for another thread.
This API supplies no process address-space policy, heap service or success
result on behalf of Horizon.

The instruction field is for synthetic development tests. Do not print game
instruction words or code bytes to logs, reports, or the UI. Contexts are not
safe for concurrent calls, and cancellation callbacks must not reenter a probe.
The callback should perform only a cheap cancellation check. Independent
contexts use thread-local memory routing. Destruction releases context state
and its retained share of process memory; see the ownership contract below.

## CLREX local reservation boundary

The actual main thread reached CLREX after a genuine mutex wait and priority
inheritance. The exact encoding `(instruction & 0xfffff0ff) == 0xd503305f` clears
only the executing context's simulated local exclusive monitor. All sixteen CRm
values are admitted: the architectural field is ignored. No host CLREX executes,
no memory is modified, and no barrier, GPU completion or system register access
is implied. The existing conservative reservation invalidation on guest context
switches remains unchanged.

[Arm's A64 instruction reference, CLREX](https://documentation-service.arm.com/static/67e40f3398aa3c3b6eea6a85)
specifies local-monitor clearing. The pinned VIXL `SimExclusiveLocalMonitor::Clear`
sets only the context-owned reservation address/size to zero; its broad system
visitor stays disabled. Original native assembly exercises all immediate values,
W/X failed exclusive stores and fresh successful retry after clearing. Sanitizer
fixtures also cover repeated clearing, GPR/SIMD/NZCV/SP/TLS/FP and shared-memory
preservation, independent process monitors, fixed-bit mutations, and cancellation,
budget and breakpoint stops before side effects. The new archives are isolated
in `/tmp/satchel-vixl-probe/clrex-staging` until joint Process/thread gates and
explicit promotion. This extends CPU startup coverage, not game rendering.

## DSB completion boundary

The actual game child thread reached `DSB SY`. The adapter now admits the exact
legacy DSB encoding with the twelve named LD/ST/full access options across all
four shareability domains. A fixed compiled host `dsb sy` with a compiler memory
clobber completes prior synchronous ordinary RAM accesses before continuing.
This is stronger than the narrower admitted domains/types. It executes no guest
bytes, invokes no native guest callback, and preserves guest registers, flags,
FP status, TLS and exclusive reservations. No deferred CPU store queue exists.

[Arm's DSB reference](https://support.arm.com/documentation/ddi0602/2022-06/Base-Instructions/DSB--Data-Synchronization-Barrier-)
distinguishes completion from DMB ordering and identifies the SSBB/PSSBB aliases.
Options0/4/8/12, speculation aliases, nXS encodings, ISB and cache maintenance stay
unsupported. Guest device/MMIO mappings remain unavailable. In particular, DSB
does not complete Vulkan submissions, upload dirty GPU buffers, or replace an
NVN queue fence; graphics ownership and completion remain with their backend.
The pinned VIXL broad system visitor remains disabled.

Original native fixtures compare all twelve admitted options, normal stores,
NZCV preservation and exclusive retry loops. Checked interpreter tests cover
GPR/SIMD/SP/owned-FP/TLS preservation, W/X reservation preservation, shared-context
visibility, rejected encoding neighbors and budget/cancel/breakpoint ordering.
Artifacts are staged separately under `/tmp/satchel-vixl-probe/dsb-staging` until
combined Process tests and explicit promotion; prior integer-FP artifacts remain
unchanged. These tests are CPU semantics checks, not a new game-frame claim.

## DMB ordering boundary

Exact AArch64 DMB (`(instruction & 0xfffff0ff) == 0xd50330bf`) executes a real
`std::atomic_thread_fence(std::memory_order_seq_cst)`. It leaves guest registers,
flags, TLS and exclusive reservations unchanged while advancing PC normally.
All 16 CRm options use conservative full ordering. The four reserved options
0/4/8/12 have the full-barrier fallback required by
[Arm’s A64 DMB reference](https://support.arm.com/documentation/ddi0602/2022-09/Base-Instructions/DMB--Data-Memory-Barrier-).

This models only serialized ordinary guest RAM. There are no guest device/MMIO
mappings or deferred CPU stores; graphics queue completion still needs explicit
backend synchronization. It is not a device flush or GPU fence. ISB, cache
maintenance and neighboring system instructions remain unsupported. Pinned
VIXL’s system visitor uses `VIXL_SYNC` for several barrier families together;
this adapter handles exact reviewed barriers before that disabled visitor. Original native
fixtures exercise every option, NZCV preservation and load/store-exclusive retry
loops. Separate tests cover encoding neighbors, register/SIMD/TLS preservation,
shared-context visibility and budget/cancellation/breakpoint precedence. The
combined staged host gate passed 22,832 ASan/UBSan/native checks. Inspection of
the compiled host object confirms the fence emits `dmb ish` for coherent host
RAM. Barrier artifacts live in `/tmp/satchel-vixl-probe/barrier-staging`; prior
shared-context and multiwrite staging outputs remain preserved.

## SIMD128 pair transfers (new staged coverage)

Exact Q STP/LDP offset/pre-index/post-index forms use the existing pinned VIXL
LoadStorePairHelper, restricted to its Q paths. The adapter validates the whole
32-byte access before AddressModeHelper can write back Xn/SP or either element
can load/store. Patched MemRead/MemWrite still check each element. Q/X registers
are distinct banks: equal numeric register indices are valid; Q31 is real SIMD
state. Duplicate load destinations are constrained-unpredictable and reject;
duplicate store sources are valid. Initial SP alignment is16 bytes. Signed
immediates scale by16 (-1024..1008), including valid zero writeback handled by
the existing offset-form override. These are bit transfers, not FP arithmetic.

Arm reference: [ARM Compiler armasm Reference Guide, A64 SIMD/FP pair transfers](https://documentation-service.arm.com/static/5f3fa899428f7a6b3328fd44).

Upstream audit: pinned simulator-aarch64.cc4611–4736 (qreg_t memory operations,
no native callbacks),5465–5496 (unsigned address calculation/writeback), and
instructions-aarch64.h429 (Q element-size calculation). S/D, non-temporal,
reserved opc and tagged-pair families stay outside admission. General memory
is serialized normal guest RAM; no MMIO or multi-copy atomicity claim is made.
Successful Q stores conservatively invalidate the exclusive reservation.

Original native assembly covers all modes, SP push/pop, Q31, duplicate sources,
numeric X/Q matches, zero writeback and both offset endpoints. Synthetic checks
cover every form, whole32-byte fault atomicity and unchanged SP/Q/base state,
protected second halves, overflow, stack alignment, shared-page write denial,
genuine aliases, page crossing and exclusive-store failure. The test-only SP
getter is absent from production iOS. Artifacts remain staged until matched
Process/thread gates and a new device run; device57 itself stopped at STP Q.

## Immutable shared-page backing

Actual device57 mapped the time service's immutable4096-byte page through this
backing API and completed63/63 observed IPC requests. This validates that mapped
page journey, not every clock method or generic shared-memory support.

The `shared_readonly_create/map/unmap/release` C APIs own a copied 4096-byte
snapshot through an opaque process token. Creation is not a guest mapping;
each explicit map adds a READ-only view of the same retained physical bytes.
There is no guest WRITE/EXECUTE mode and no host update or pointer-export API.
This narrow primitive is intended for an immutable time-service page; Process
owns Horizon handle, address-space, memory-state and result-code policy.

A process can own at most 32 page objects and retain at most the existing 32
mapped views in total. Unique backing bytes, including owned but unmapped pages,
count toward the existing 256 MiB cap. Multiple aliases charge the page once.
Tokens are never reused, including across unrelated process owners. Releasing
server ownership prevents new maps but leaves existing maps readable; exact
unmap with the original token remains possible until the last map disappears.
The last CPU context destroys all remaining objects and views. No borrowed CPU
pointer is retained in an object.

Creation snapshots source bytes before publication. Map allocation failures,
invalid overlap/alignment/size, stale or foreign tokens and active-run reentry
leave all output, maps, bytes and reservations unchanged. Successful map/unmap
clears shared exclusive reservations; owner-only create/release does not.
The original fixtures exercise cross-context and parent-destruction lifetime,
wrong-token unmap, both allocation-failure stages, view and physical-budget
limits, alias/resize/writev escape denial and actual interpreted scalar, pair,
SIMD and exclusive-store protection. This component alone does not implement a
Horizon shared-memory handle or establish time-service correctness.

## Shared process memory and serialized contexts

`satchel_cpu_probe_create_shared(peer, entry, error, error_size)` creates a fresh CPU
context retaining the peer's process memory. Each context owns a separate VIXL
Decoder/Simulator, GPRs, SP, SIMD/FP registers, NZCV/FPCR, PC, guest TLS and terminal
fault result. It does not clone registers, create a Horizon thread, allocate a
guest stack/TLS region or run anything. GPRs/SP/TLS begin at zero; SIMD and FP
control use the same pinned VIXL defaults as the existing constructor (SIMD is
VIXL's deterministic NaN pattern, not a host register snapshot). The caller must
set the actual entry arguments, SP, TLS and required SIMD state. A terminal peer
cannot seed a new context; existing siblings retain their own fault state.

At most 32 live contexts retain one process map. The map still has at most 32
regions and 256 MiB total committed guest bytes, counted once, not per context.
Creating a sibling copies no guest image. Checked host writes and dynamic
mapping changes immediately affect all siblings. Breakpoints are process-wide:
setting/clearing them through one sibling applies the same traps to every
context. Register changes and guest PC remain context-local. The process layer
must decide whether a fault should terminate the whole process; this API does
not hide or recover another context's fault.

The map is reference-counted. Destroying the original context does not invalidate
surviving siblings; the final context releases the shared images. A process-wide
nonblocking guard rejects operations while any sibling is running, including
cancellation callback reentry and attempted operations from another host thread.
This is a defensive check, not support for parallel guest execution: the caller
must serialize its scheduler and pointer lifetimes. In particular, do not race
a pointer's destruction against another API call using that pointer.

`satchel_cpu_probe_try_destroy` returns 1 on successful release (including NULL), or
0 while active/busy; failure leaves caller ownership unchanged. Prefer it when
ownership must be checked explicitly. Legacy `satchel_cpu_probe_destroy` delegates to
it and safely does nothing on forbidden active/reentrant destruction; callers
must retry later rather than discard their only pointer. Register/read/write
APIs also reject reentry without changing outputs/state. Dynamic maps cannot
invalidate buffers retained by an executing instruction.

The execution model is one serialized virtual core. Switching from context A
to B with a nonzero instruction budget conservatively clears every reservation.
Repeated slices of the same context retain their reservation unless an ordinary
store, checked host write or map change invalidates it. Host writes/map changes
clear all sibling monitors. Zero-budget inspection does not switch the virtual
core. Destroying a last-running context clears the remembered context pointer.
VIXL's simulated global monitor only introduces spurious failures; it does not
supply cross-context coherence. No parallel ARM weak-memory model is claimed.
An eventual scheduler must allow exclusive retry loops enough execution time;
alternating threads between every load-exclusive/store-exclusive can starve them.

No FPSR/cumulative FP exception model exists in pinned VIXL (`FPProcessException`
is empty); the staged original integer adapter supplies it only for the two
reviewed scalar operations. SVE and unsupported FPCR modes remain rejected; separate simulator
instances preserve the supported FP state without pretending a raw GPR-only
snapshot is a complete architectural context. No new instruction family, kernel
CreateThread/StartThread success, synchronization primitive or scheduling policy
is supplied by this change.

Stage validation with
`zsh scripts/test_cpu_probe.sh --output /tmp/satchel-vixl-probe/shared-context-staging --ios`; coordinate promotion before
replacing canonical archives used by app or other agents.

## Reviewed execution boundary

The stock simulator is **not** a sandbox. In this adapter:

- Every instruction fetch checks guest alignment, mapping and execute permission
  before reading four bytes or invoking the VIXL decoder.
- An explicit encoding allowlist runs before decode. Reserved 32-bit shift,
  move-wide and bitfield encodings are rejected explicitly, not left to VIXL.
  Malformed-encoding tests found an upstream undefined shift for a W-register
  ROR amount above 31; a dedicated regression verifies it now stops before decode.
- Scalar, SIMD and exclusive loads/stores in accepted families use patched
  `Memory::Read/Write`; the complete access range and permission are checked
  before a host pointer is returned. Reads cannot escape the copied mappings,
  writes cannot change code, and tagged addresses are not stripped to bypass
  bounds. Multi-region accesses fail instead of spanning unrelated buffers.
  For admitted pair transfers, the entire8-byte (W),16-byte (X) or32-byte (Q) range is checked before either
  word or the writeback base can change; per-word VIXL checks remain in place.
  Load destination aliases and writeback/base overlaps are rejected, except
  that SP and XZR are distinct despite sharing encoding 31. Zero-offset
  writeback uses the equivalent offset visitor to preserve a valid instruction
  without disabling VIXL's nonzero-writeback assertion.
  Byte/H/W/X unscaled and pre/post-index transfers likewise validate the full
  access before base writeback, reject overlapping writeback registers, and
  distinguish SP from XZR. Unprivileged forms remain blocked; register-offset
  support is limited to the separately reviewed integer transfers below.
- Register-offset integer B/H/W/X stores, unsigned loads, signed B/H loads to
  W/X and LDRSW are separately admitted after validating UXTW/UXTX/SXTW/SXTX
  and the element-size shift. Their full effective range and read/write
  permission are checked using unsigned virtual-address arithmetic before the
  same guarded scalar-memory helper runs. SIMD, prefetch and reserved
  opcode/size/extension combinations remain blocked.
- ADR/ADRP, B/BL, B.cond, CBZ/CBNZ, TBZ/TBNZ and BR/BLR/RET are handled with guest PC and
  guest LR semantics. Indirect targets are never invoked as native functions.
  The next fetch checks the new target. VIXL sees a host PC only for reviewed
  non-branch, non-PC-relative instructions.
- SVC and the three exact guest TLS MRS/MSR forms are intercepted before VIXL
  decode. HLT, BRK, other system/MTE/SVE instructions,
  pointer-authenticated branches and all other unlisted families are rejected.
  Exception/system/register-branch visitor overrides provide another barrier
  against native callbacks, breakpoint traps and HLT pseudo-instructions.
- Tracing, disassembly and debugger entry are disabled. Bounded guest-PC stops
  are exposed, but VIXL branch interception/native-call registration is not.

Accepted VIXL families currently cover ADD/SUB immediate, shifted-register and
extended-register (all eight extensions, shifts 0–4),
logical immediate and shifted-register, MOVN/Z/K, bitfield operations,
MADD/MSUB W/X modular arithmetic (including MUL/MNEG aliases), UMULH/SMULH,
SMADDL/SMSUBL/UMADDL/UMSUBL (including widening multiply/negate aliases), unsigned-immediate
integer B/H/W/X transfers (including signed B/H loads and LDRSW) and the
previously reviewed Q load/store forms,
signed-immediate unscaled and pre/post-index unsigned byte/H/W/X loads/stores,
integer register-offset B/H/W/X transfers (including signed B/H loads and LDRSW),
variable LSLV/LSRV/ASRV/RORV W/X with architectural count masking,
CSEL/CSINC/CSINV/CSNEG W/X (including CSET/CSETM/CNEG aliases),
CCMP/CCMN register/immediate W/X,
UDIV/SDIV W/X with architectural divisor-zero and signed-overflow results,
single W/X LDXR/LDAXR/STXR/STLXR, non-exclusive LDAR/STLR B/H/W/X, ADD V.16B,
double FADD/FMUL, W/X GPR and128-bit Q SIMD STP/LDP in offset/pre/post modes.
Other pair forms (S/D SIMD, signed-word loads and non-temporal) remain unsupported.
Exact RBIT/CLZ W/X use unsigned operations in the adapter, including defined
zero-input CLZ results and W zero-extension. Other one-source/PAC/system forms
remain unsupported. NOP and the guest-address operations above are handled
by the adapter. This is intentionally far smaller than VIXL's instruction set.
The extended-register encoding limits were checked against the
[Arm instruction reference](https://documentation-service.arm.com/static/6245c734b059dc5ff9a8bdab)
and the pinned VIXL helper. Reserved shifts 5–7 and reserved conditional-compare
fields stop before decoding.

## Remaining integration gates

Stock VIXL still contains direct host-memory/native mechanisms. Its HLT
trace/log/printf handlers read inline arguments directly; its runtime-call HLT
handler calls a host function pointer; `DoUnreachable` calls `abort`; register
branch interception can call native callbacks; SVE fault-tolerant access uses
`CanReadMemory`; debugger and trace routines assume host memory; MTE metadata
has host-oriented assumptions. These paths are not exposed by the current
allowlist. **Do not broaden the allowlist or enable trace/debug/native helpers
without auditing the newly reachable paths.** The retained patch alone does
not turn arbitrary VIXL execution into a sandbox.

This CPU library has no architectural exception delivery, system registers
beyond explicit guest TLS and read-only timer values, literal loads, broad SIMD/FP,
scheduler, or Horizon service policy.
Exclusive accesses require natural alignment (and 16-byte SP alignment), and
the complete access is checked before reservation/status mutations. Status
register aliases with source or non-SP base are rejected as constrained
unpredictable (including status/source both ZR). Exclusive byte/halfword, pair,
CAS, limited-order and RCpc variants remain outside the allowlist. Exact
non-exclusive LDAR/STLR forms cover B/H/W/X and require the same full-span,
natural-alignment and SP checks; loads never establish an exclusive reservation
and stores require none. Acquire/release use
the pinned visitor's host barrier. Shared contexts use conservative
reservation invalidation for one serialized virtual core, not a parallel
multicore Switch memory model. FP is limited to FADD/FMUL D with FPCR0/DN;
other rounding modes and FP instructions remain unsupported. No throughput or
game compatibility target has been established.
The adapter conservatively clears reservations after successful ordinary guest
stores and checked host writes; upstream ordinary stores otherwise clear only
probabilistically. Exclusive stores still use VIXL's address/width reservation
and simulated global-monitor failure, clear the reservation on each attempt,
and can legitimately require retry. They never report unconditional success.

The app integration now maps all decoded NSOs into a full process through
[`App/SwitchRuntimeSession.swift`](../../App/SwitchRuntimeSession.swift) and
[`Core/SwitchProcess.c`](../../Core/SwitchProcess.c). The process wrapper owns
the map-backed QueryMemory policy and uses the checked CPU memory API for
guest output; this CPU library remains independent of Horizon policy.
Original `rtld` startup and QueryMemory calls have been verified on the physical
phone. Current counts and limits belong to
[`Validation/runtime-core.md`](../../Validation/runtime-core.md).
This establishes startup and service-boundary progress, not completed game boot,
rendering, update compatibility or playable speed. Future instruction work must
extend checked memory/PC semantics and independent native comparison tests
before extending coverage.

## Validation recorded 2026-10-31

- 22,832 checks passed with AddressSanitizer and UndefinedBehaviorSanitizer,
  both configured to halt on errors. Includes original native ARM64 integer,
  flags, branches, scalar/vector memory, SIMD, FP and exclusive-loop comparisons;
  SVC stop/resume; virtual ADR/ADRP and cross-segment BLR/RET; permissions and
  bounds; budgets/cancellation; and 2,944 deterministic malformed-encoding and
  hostile-address cases. W/X pair coverage additionally compares all three address
  modes, zero writeback, W truncation/zero-extension and a native stack frame, rejects overlaps and other
  pair formats, and verifies crossing-boundary faults preserve both destination
  registers and memory. Byte transfer coverage verifies truncation, load zero
  extension, unchanged neighboring bytes, the maximum 4095-byte unsigned
  offset, boundary faults, and rejection of signed unscaled/unprivileged
  families. The unscaled/pre/post suite covers all four supported widths,
  signed immediate limits, unaligned X accesses, zero writeback, stack accesses,
  and preservation of the entire access/base/destination on bounded faults.
  Halfword offset/pre/post fixtures additionally compare complete native guarded
  buffers across truncation and zero-extension cases, signed offsets -256/255,
  unaligned two-byte accesses, SP/WZR distinction, unchanged flags/FP status,
  failed-access atomicity and ordinary-store reservation invalidation. The
  actual phone's STURH boundary motivated this extension; it does not broaden
  signed loads, unprivileged forms, or SIMD memory operations.
  MADD/MSUB/MUL compares unsigned modular overflow, zero extension of W results,
  register aliases and unchanged NZCV with independently assembled native code;
  High and widening multiply compare native signed extremes, accumulator wrap,
  zero registers, aliases, unchanged flags and 128 randomized operand sets.
  High multiply rejects accumulator fields other than ZR, W forms and reserved bits.
  Guest TLS fixtures compare against a native GPR value model; the native test
  deliberately does not modify host TLS. Tests cover zero initialization,
  interleaved context isolation, SVC resume, all MSR GPR/ZR operands, unchanged
  flags, invalid API outputs, forbidden system-register encodings and checked
  memory faults when an invalid guest TLS value is used as a pointer.
  Dynamic mapping tests cover zero initialization/growth, shrink/regrow,
  prefix preservation, checked guest loads/stores, no execution permission,
  overlap and initial-region protection, region/aggregate caps, removal/reuse,
  callback mutation rejection and clearing old exclusive reservations. A private
  `SATCHEL_CPU_TESTING` seam injects `bad_alloc` before replacement allocation and
  region insertion; both failures preserve existing maps/bytes. That seam is
  compiled into the Mac sanitizer build only and is absent from the iOS archive.
  Breakpoint fixtures compare native BL/BLR calls with an original synthetic
  stop/checked-return dispatcher, and retain the ordinary no-breakpoint path.
  Guards cover every GPR/SIMD register, SP/NZCV, memory side effects, copied
  registration sets, capacity/duplicates, complete executable spans, callback
  mutation rejection, terminal faults and exact budget/cancellation ordering.
  W/X acquire/release exclusive loops compare native full memory, overflow,
  W zero extension and flags. Reservation tests require failed stores after
  checked host writes and each ordinary guest-store family, absent/consumed
  reservations and mismatched widths. Faults preflight alignment, permissions,
  complete spans and status/destination preservation. Reserved forms and
  constrained-unpredictable register aliases are rejected before execution.
  Non-exclusive LDAR/STLR B/H/W/X compare native full-buffer results, narrow
  zero extension/truncation, SP/ZR and flags. Tests prove acquire loads create no
  reservation, release stores need none and invalidate old reservations, and
  malformed fields/alignment/permissions fail before register or memory changes.
  Register-offset loads compare all four extension types, element scaling,
  LDRSW and signed B/H destination-width extension against native code. Stores
  additionally compare complete memory, truncation, SP accesses and unchanged
  neighbors; bounds/permission failures must not alter any destination byte.
  Variable shifts/rotates compare W/X results and unchanged flags against
  native code for zero, width-boundary and very large masked counts. TBZ/TBNZ
  tests cover every bit position, forward/backward and zero-register behavior,
  unchanged NZCV and instruction budgets. Checked memory-write tests verify
  bounds, R/RX protection, null/overflow rejection and no partial writes.
  RBIT/CLZ W/X compare native flags, zero/extreme inputs, register aliases and
  width semantics, plus 128 deterministic hostile bit-pattern cases.
  Conditional selection compares all 16 conditions and all four operations in
  W/X widths, aliases, modular overflow and preserved NZCV against native code.
  UDIV/SDIV W/X compare native divisor-zero and MIN/-1 results, truncation toward
  zero, zero-extension, aliases/ZR and unchanged flags. Another 128 deterministic
  signed/unsigned operand cases run under both sanitizers.
  Unsigned-immediate halfword and signed B/H/W loads compare native truncation,
  destination-width extension, SP/ZR behavior, flags and the maximum 4095 scaled
  offset. New width/permission/boundary guards preserve all bytes on failure.
  CCMP/CCMN native matrices cover all conditions, every fallback NZCV literal,
  immediate/register forms, W/X widths, zero and signed-overflow cases. Extended
  ADD/SUB native matrices cover all eight extensions, shifts 0/4, W/X widths,
  flag-setting/preserving forms and SP. Reserved shifts 5–7 fail before decode;
  WSP truncation is tested only in the interpreter to preserve the host stack.
  This is a regression sweep, not an exhaustive proof.
- iOS arm64 static compilation, including W/X STP/LDP, unsigned STRB/LDRB,
  unscaled/pre/post unsigned byte/H/W/X transfers, MADD/MSUB W/X, reviewed
  register-offset loads, TBZ/TBNZ, RBIT/CLZ and checked guest-memory writes,
  passed after host checks. `vtool` reports platform IOS, minimum
  17.0, SDK 27.2. The archive has no mmap/mprotect/pthread_jit imports.
- No device execution, playable title or graphics was tested by this isolated
  CPU work. Original-module startup diagnostics belong to the separate app
  integration evidence; native fixture results do not establish game boot.

Shared-context validation adds instruction-by-instruction interleaving against
native arithmetic/NZCV, SIMD, FP and TLS reference fixtures; independent SP and
fresh defaults; map creation/growth/removal/failure visibility; parent destruction;
process-wide traps; terminal-context isolation; context cap/slot reuse; rejected
reentrant and cross-host busy operations; and W/X exclusive reservation behavior
for switches, checked sibling writes and map changes. The staged host build passed
all 19,659 checks; the iOS archive compiled, without a device test. Canonical
archive promotion is separate from this gate;
no Horizon thread creation or game-thread execution is established by these tests.

Atomic multi-span host validation passed 19,792 total ASan/UBSan/native checks,
including all shared-context regressions. Added cases verify whole-buffer
canaries, bad second spans, protected/unmapped/cross-mapping output, overlapping
destinations, source alias swaps, one-shot allocation failure, exact eight-span
and 2 MiB limits, busy callbacks/siblings/host callers, and reservation retention
on failure versus invalidation after successful commit. Artifacts for this gate
are staged separately under `/tmp/satchel-vixl-probe/multiwrite-staging`.

### General-register DUP (staged)

This slice admits only the Advanced SIMD general-register DUP family: 8B,
16B, 4H, 8H, 2S, 4S and 2D arrangements. The least set imm5 bit selects
the lane width; higher bits are architecturally ignored. Zero/bit4-only imm5
and the Q=0 D form stop before VIXL. Vector-element/scalar DUP, INS, UMOV,
SMOV and SVE remain outside this new admission. Rn31 is ZR, destination Q31
is real vector state, and 64-bit destinations clear their upper 64 bits.
There are no memory accesses, FP arithmetic, native callbacks or monitor changes.

Reviewed pinned VIXL `VisitNEONCopy` (simulator-aarch64.cc:8645),
`dup_immediate` (logic-aarch64.cc:2989), and `TriangularFormatMap`
(instructions-aarch64.h:993): valid formats mask the source, clear the vector
destination and repeat a bounded number of lanes. The guard excludes invalid
formats before the upstream decoder. Arm's [Compiler armasm Reference Guide,
section 9.28](https://documentation-service.arm.com/static/5f3fa899428f7a6b3328fd44)
defines the general-register vector arrangements. Original native assembly
compares every arrangement, ignored high imm5 bits, ZR, matching GPR/Q numbers,
Q31, upper-zeroing and NZCV. Sanitizer tests exhaust all imm5/Q combinations,
rejected related families, exclusive preservation, owned cumulative FP status
and independent shared-memory contexts. No imported bytes enter fixtures.

The actual device58 report progressed past the previously added Q-pair family
and stopped at SDK offset0x126dd4, safely classified as general-register
DUP16B. That observation motivates this slice; these host tests and iOS
compilation do not establish that the next installed build passes the boundary.

### Retained-backing permission transactions (staged)

`satchel_cpu_probe_set_permissions(cpu,address,size,expected_old,permissions)` changes
only permissions over a page-aligned complete span. Both masks are exactly
NONE0, READ1 or READ|WRITE3. Every covered view must have the expected old mask
and immutable initial-RW provenance. Initial RO/RX/write-only regions, immutable
shared pages, alias destinations and locked source views reject even a no-op.
Horizon state/attribute eligibility is independently checked by Process; this
CPU primitive never manufactures a kernel policy result or executable mapping.

The transaction splits/coalesces retained views of the SAME physical allocation;
no backing byte copy, duplicate charge or replacement occurs. It supports spans
across distinct eligible backings without merging their physical identity.
Final32-view capacity and both allocation-failure points are checked before
commit. Failure preserves views, bytes and reservations; validated no-op does
not allocate or clear monitors; an actual commit clears shared-context monitors.
All siblings see the changed map and survive parent-context destruction.

Existing dynamic allocation replacement operations now reject fragmented
backings: resizing/copying a single permission view would otherwise leave the
original allocation retained elsewhere and violate accounting. Restoring a
whole allocation toRW coalesces its views and restores existing reshape support.
Process must stop otherwise valid unsupported reshape requests explicitly;
this limitation must not become a fabricated Horizon allocation error.

Original sanitizer fixtures include the observed public76-of95-page CodeData
prefix shape, middle permission boundaries, R/NONE/RW restoration,
whole-span writev and interpreted Q-pair store atomicity, actual LDR/LDP-Q
reads across contiguous RW/R views, permission-independent physical overlap
through RW/R/NONE and locked alias views, original RO/RX/shared
and alias denials, shared contexts, full32-view capacity, both metadata failure
seams, unchanged unique charge, and real exclusive reservation outcomes. Only
original bytes are used. The source/API are staged pending matched Process
integration and actual phone validation.

## D-register unscaled transfers (staged)

Exact64-bit SIMD/FP D single load/store unscaled/pre/post forms use the pinned
integer bit-copy path with full8-byte permission preflight before any base/SP
writeback. D and X/SP are separate register banks; D31 is a real vector. Loads
copy low64 bits and zero upper64; stores copy low64 and preserve source state.
Zero pre/post displacement is a valid unchanged-base update. Other scalar
widths, Q unscaled, unsigned/register/literal forms and mode2 stay unsupported.
Transfers perform no FP arithmetic or FPSR changes; native fixtures explicitly
compare signaling/quiet NaNs, infinities, subnormals, upper-lane clearing, host
FP environment and owned cumulative flags. Ordinary stores clear reservations;
loads preserve them. Bounds, permissions, alias/time-page, SP, shared-context
and failure-atomicity cases are independently tested with original data.

The first new load-monitor assertion found VIXL LoadStoreHelper unconditionally
calls MaybeClear even on loads. The exact D family therefore uses explicit
checked uint64 copies and WriteDRegisterBits, without altering the assertion
or upstream source. This also keeps signaling-NaN bits outside host FP values.

## D-register unsigned immediate transfers (staged)

Exact64-bit D load/store unsigned imm12 forms use the same checked integer
bit-transfer path. Offset is imm12×8 (0..32760), never signed and never
writeback; immediate low bits1/2/3 cannot select post/pre-index. The entire
8-byte access and original SP alignment are checked before state mutation.
Loads clear upper64 and preserve reservations; stores preserve the full source
register and clear the local reservation after successful commit. Native and
sanitizer cases cover signed-zero/NaN/subnormal bits, zero/max offsets, D31,
separate D/X registers, SP, RW/R/NONE fragments, retained aliases/shared-RO,
unchanged nonzero FPSR and full-span failure atomicity. Other unsigned scalar
SIMD widths, reserved opc and register/literal forms remain excluded. Prior
unsigned-D exclusions are replaced by explicit positive execution assertions.

### Integer MOVI checkpoint (staged)

The reviewed modified-immediate subset admits integer MOVI only: byte, halfword,
word shifted/MSL, and64-bit byte-mask arrangements with Q=0/1. All imm8 values
are handled generically. The exact fixed bits and cmode/op combinations reject
MVNI, ORR/BIC, floating immediate forms and reserved encodings before VIXL. The
pinned helper uses bounded unsigned integer expansion and register-only MOVI;
Q=0 clears upper64. It never accesses guest RAM, changes FP status, or touches
exclusive reservations. A fixed original native program exhausts all256
immediates across20 arrangements; negative and owned-state checks remain separate.
No actual phone validation of this new family is claimed by the host gate.

### SIMD single-width checkpoint (staged)

B/H/S/Q unscaled/pre/post transfers now join the reviewed D forms in one checked
integer register-copy path. Complete width1/2/4/8/16 memory preflight precedes
copy, destination change and writeback; narrow loads zero every higherQ bit,
and stores preserve the whole source. Original SP alignment and separate SIMD/X
register banks are explicit. Only a successful store clears the local reservation.
Mode2, unallocated size/opc, register/literal and unsigned B/H/S remain rejected.
Existing unsignedD uses the same equivalent bit-copy route; unsignedQ is unchanged.
Native original fixtures compare every new width/mode and signed endpoints,
with permission-fragment, retained-alias, immutable-page and FP-state tests.
No actual phone validation of the new widths is claimed by the host gate.

### SIMD register-offset checkpoint (staged)

Allocated B/H/S/D/Q register-offset loads/stores use the same checked integer
copy path. Only UXTW/UXTX/SXTW/SXTX and optional element-size shift are admitted.
Rm31 denotes zero, Rn31 denotes the original aligned SP, and SIMD31 remains a
real register. Unsigned modular address arithmetic precedes a whole-width
permission check; no writeback exists. Narrow loads clear all higher bits;
only successful stores clear the current reservation. Reserved size/opc/options
and other addressing families remain stopped. Native original fixtures cover
every width/extension/shift, with boundary/alias/permissions/FP-state checks.
No physical-device validation of this new family is claimed by host tests.

### Integer vector ADD/SUB checkpoint

The staged extension admits integer vector ADD/SUB for 8B/16B,4H/8H,2S/4S,2D.
Q=0,size3, scalar, saturating, pairwise and floating-point forms remain excluded.
Pinned VIXL `VisitNEON3Same` selects only `add`/`sub`; their arithmetic is unsigned
modulo64 on left-justified lanes, followed by bounded lane insertion. Temporary
helper saturation bookkeeping does not update FPSR. Q=0 clears the upper64.
Original native assembly compares every arrangement, wraparound and overlapping
source/destination registers; fixtures also preserve flags, FP state, sibling
registers and exclusive reservations. This is not a game-frame claim. The actual
host register02 diagnostic reached ADD2D after67,453,390 instructions; no phone
validation of this new family has occurred.

### Integer vector comparison checkpoint

The staged extension admits three-register CMEQ, CMGE, CMGT, CMHI, CMHS and
CMTST for 8B/16B, 4H/8H, 2S/4S and 2D. Q=0,size3, scalar, immediate-zero
and FP comparisons remain excluded. Pinned VIXL `cmp` performs typed signed or
unsigned comparisons without arithmetic; `cmptst` checks an unsigned bitwise
AND. Every destination lane becomes all ones or zero; upper64 clears for Q=0.
The helpers have no guest memory, FP-state, NZCV or exclusive-monitor effects.
Original native tests compare all 42 operations/arrangements and overlapping
registers against independent byte-lexicographic and bit-test expectations.
The actual host ADD/SUB run reached CMHI2D after 67,462,783 instructions; new
comparison support is not yet an original-game or phone validation.

### Three-register vector logical checkpoint

The staged extension admits AND, BIC, ORR (including its register MOV alias),
ORN, EOR, BSL, BIT and BIF in 8B/16B arrangements. Exact fixed encodings exclude
immediate, scalar, FP and SVE paths. Reviewed helpers use only unsigned bitwise
operations and owned register lanes. BSL uses the old destination as a mask;
BIT/BIF preserve the selected old-destination bits. Every active lane is read
before it is written, including all destructive overlaps, and Q=0 clears upper64.
Original native comparisons and an independent per-bit truth-table oracle cover
all16 operation/arrangement combinations, all destination/source overlaps and
zero/ones/alternating masks. NZCV, FP state, reservations and sibling contexts
remain unchanged. Actual host comparison run reached BIF after 67,462,784
instructions; this new support has not yet been validated by a game or phone run.

### Integer horizontal reductions checkpoint

The staged family admits ADDV, SMAXV, SMINV, UMAXV and UMINV for 8B/16B,
4H/8H and 4S. D and 2S, widening sums, scalar pairwise, and floating reductions
remain excluded. Pinned VIXL finishes reading all source lanes before writing
the scalar destination and clearing every other bit. ADDV accumulates at most
four signed32 values into int64 before truncation, so its admitted accumulator
cannot overflow. MIN/MAX use comparisons only. No memory, flags, FP state or
reservation mutation is introduced. Native originals cover all25 arrangements,
overlap and Q31; an independent byte-carry/lexicographic oracle also covers
unallocated shapes. This is proactive source coverage, not measured game or
phone progress. Validation is recorded separately after the staged gate.

The reviewed integer permutation slice admits EXT with every allocated byte
index and UZP/ZIP/TRN1/2 across seven allocated B/H/S/D arrangements. Native
fixtures and an independent byte-position oracle cover overlapping registers,
Q31, upper-zero behavior and exclusions. No memory or floating operation added.

## Register-only GPR/SIMD bit moves

Six exact FMOV forms copy W↔S, X↔D and X↔the upper64-bit SIMD lane.
They use integer bit accessors, preserving signaling NaN payloads and every
other bit pattern without a host floating conversion. W-to-S clears upper96
vector bits; X-to-D clears upper64; X-to-upper-D preserves the low64 bits.
GPR31 is ZR (never SP), while SIMD31 is an ordinary vector register.
Guest FPCR/FPSR, NZCV and exclusive reservations remain unchanged.

This does not admit FP16, immediate moves, scalar FP-to-FP moves, conversions
or additional arithmetic. Original native and independent full-bank tests
cover special bit patterns, upper-lane rules, ZR, same-number register banks,
preexisting cumulative floating status, host floating-environment preservation,
local reservations and separate thread registers.

## Integer vector bit counts

Exact CNT counts set bits independently in each byte (8B/16B). CLS and CLZ
count leading sign/zero bits in B/H/S lanes of either vector width; CLS excludes
the sign bit itself. All source lanes are read before any destination change,
so overlapping registers work; short vectors clear upper64. No D-lane, scalar,
FP, saturation or neighboring miscellaneous SIMD operation is admitted.
Original native and independent per-bit oracles cover all256 byte values,
leading-bit boundaries, aliases/Q31, full register preservation, FP status,
local reservations and zero upper lanes.

## Widening integer vector reductions

SADDLV and UADDLV reduce allocated 8B/16B, 4H/8H and 4S vectors into
H/S/D scalar results respectively. The accumulator fits signed/unsigned64,
and the entire source is read before any destination write. Scalar result
writes clear every higher vector bit; Q31 is an ordinary SIMD register.
D lanes, Q0/2S, scalar-pairwise and FP reductions remain unsupported. These
helpers do not use a predicate register; the simulator's unrelated GetPTrue
object is initialized by its constructor and remains process-owned.
Original native comparisons and independent bytewise sign-extension/carry
checks cover extrema, negative sums, destructive aliases and upper zeroing.
This is an ahead-of-runtime candidate observed in a bounded forward decode,
not evidence that the actual game has executed the widened reduction yet.

## Architectural prefetch hints

Exact baseline PRFM unsigned-immediate, literal and register forms plus PRFUM
unscaled use an explicit no-cache-action policy. They do not dereference any
guest/host address, fault on target permissions or SP data alignment, modify
registers/FP status or clear the guest exclusive monitor. Normal code fetch,
CPU-context checks, cancellation, breakpoints and instruction budgets still
apply. All32 hint values are accepted, including unnamed/unallocated hints,
which need not perform cache work. This does not implement cache maintenance,
GPU visibility, MMIO, SVE or RPRFM/new architectural extensions.

Arm's baseline pseudocode skips CheckSPAlignment for prefetch and specifies
implementation-defined prefetch effects. Pinned VIXL also ignores the hint,
but its general load helper subsequently clears the monitor opportunistically;
this explicit branch avoids that unrelated side effect. Original native fixtures
and bit/state tests cover the four forms, allowed extensions, offset endpoints,
invalid/unmapped target addresses, FP/flags, reservations and rejected encodings.
Primary references: [Arm PRFM immediate](https://support.arm.com/documentation/ddi0602/2022-06/Base-Instructions/PRFM--immediate---Prefetch-Memory--immediate--)
and [Arm instruction reference](https://documentation-service.arm.com/static/6245c734b059dc5ff9a8bdab).

## Vector element replication and narrowing

Vector-element DUP admits allocated B/H/S arrangements and2D, reading any
element of the full128-bit source before destination clearing. SHRN/SHRN2
admit only nonrounding nonsaturating H-to-B, S-to-H and D-to-S with shift1..
destination width. SHRN zeroes upper64; SHRN2 preserves lower64. Both support
destructive source/destination aliases and Q31, without memory/FP/NZCV/monitor
effects. Scalar copies, rounding/saturation and other shift families stay stopped.
Actual phone72 stopped at vector-element DUP4S; SHRN16-to8 was only safely
decoded in the nearby forward window, not yet observed executing.

## Unsigned integer to binary32

Exact UCVTF W-to-S and X-to-S use a private integer-only direct24-significand-bit
nearest-even model. No intermediate double or host FP conversion is used. Zero
produces positive zero; all nonzero unsigned64 inputs produce finite normal
values, with only cumulative IXC newly possible. Existing FPCR0/DN and modeled
FPSR masks remain unchanged; unsupported controls/status reject before writing
the destination. W inputs zero-extend, source31 is ZR, and S writes clear upper96
bits. Signed, D/H, fixed-point and SIMD-source conversion families stay stopped.
Actual phone73 stopped at unsigned64 GPR-to-S; the W form has original test
coverage only until future actual runtime evidence. Native FP-state-changing
reference instructions live only in the test oracle, never production archives.

## Indexed vector floating multiply

Exact indexed FMUL2S/4S/2D uses integer-only binary32 multiplication and the
existing integer binary64 multiply model. The D form requires Q=1 and L=0;
S indexes any of all four source lanes even with a2S destination. Full128-bit
source snapshots preserve destructive aliases, and2S clears upper64. One final
whole-vector/FPSR commit follows successful computation of every lane. FPCR0/DN
and the existing modeled FPSR mask remain unchanged; signed zeros, NaN priority,
gradual underflow, nearest-even ties and cumulative flags are modeled explicitly.
Scalar indexed, half, FMULX, accumulation and ordinary vector FMUL remain stopped.
The actual phone74b boundary is indexed2S;4S/2D have original fixture coverage.
Native bit/status references and an independent exact-distance finite-product
oracle are test-only. No host FP arithmetic or environment change is used by the
production path, and no public ABI or memory/reservation behavior changes.

## Binary32 to binary64 vector widening

Exact FCVTL/FCVTL2 S-to-D admits only the single-precision source encoding. Q
selects source lanes0/1 or2/3, while both forms replace the complete128-bit
result. A full source snapshot protects same-register aliases and real Q31.
The private integer model widens finite values exactly, including normalizing
binary32 subnormals into normal binary64 values. It preserves signed zero and
infinity; NaN sign/payload widen and quiet, with signaling NaNs adding only IOC,
or DN supplying the positive default NaN. Unselected-half NaNs have no effects.
The existing FPCR0/DN and modeled cumulative FPSR profile remains unchanged;
unknown or unsupported controls/status refuse before destination/status commit.
Half-to-single, scalar and narrowing conversions remain stopped. Actual phone75
stopped at the lower-half S-to-D form; upper-half FCVTL2 is original-fixture
coverage. Native references are test-only; production uses no host FP arithmetic.

## Vector binary64 to unsigned integer

Exact FCVTZU2D performs integer-only toward-zero conversion followed by unsigned
saturation. It snapshots both source lanes, including self-alias/Q31, and commits
both integer lanes and cumulative status together. Negative fractions between
minus one and zero yield zero plus IXC. Negative values at or below minus one
saturate to zero plus IOC; positive values at/above2^64 saturate to UINT64_MAX
plus IOC. Either NaN yields zero plus IOC. Exact finite integers and signed zeros
add no flags. Valid nonintegral finite values, including gradual subnormals, add
IXC. Invalid lanes add no new IXC but preserve cumulative flags from prior lanes.
FPCR0/DN and the existing modeled FPSR mask remain required. No host floating
arithmetic/state, scalar/single/half/signed/fixed-point conversion is introduced.
This2D form was identified only by bounded static lookahead after the observed
phone75 FCVTL stop; it has not yet been executed in the original game runtime.

### ISB SY checkpoint

Exact ISB SY (option15) executes one fixed compiled host instruction barrier.
The serialized interpreter completes prior instruction effects before each fresh
checked fetch and has no guest speculative pipeline or decoded instruction
cache. This does not complete GPU work, implement cache maintenance, expose
privileged context updates, or invalidate guest exclusive reservations. Other
ISB options remain unsupported in this bounded profile. Original native tests
compare flags, register/memory bit patterns and exclusive progress; synthetic
tests cover all reserved options, adjacent system forms, TLS state, cancellation
and breakpoint ordering. The prior ISB SY exclusion inputs remain as explicit
positive assertions; all other old barrier exclusions remain.

The reviewed UCVTF integer profile additionally admits W/X→D using integer-only nearest-even binary64 rounding, FPCR0/DN and modeled cumulative FPSR. W inputs are exact; X may add IXC. Other rounding/merging/trap controls, signed/fixed/half/SIMD-source forms remain unsupported. Original native/raw-bit/rational-oracle tests cover both precisions.

Staged exact scalar FDIV D uses integer quotient/remainder rounding with FPCR0/DN and modeled cumulative FPSR; original native/rational fixtures cover exceptional and gradual-underflow behavior. S/H/vector division and unsupported controls remain stopped. This slice is static-lookahead support, not runtime-observed game progress.
