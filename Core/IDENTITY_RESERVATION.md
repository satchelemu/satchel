# Process-owned opaque identity reservation

This opt-in Process allocation supplies numeric identity addresses to a future
checked HLE registry. It does not create an nn::vi display/layer/window, prove an
NVN enum/default, or implement a presentation service.

`satchel_switch_process_create_with_dispatch_and_identity` preserves the existing NSO
layout and dispatch configuration. One 4 KiB page immediately follows rounded
original dispatch code, or starts at the 2 MiB-rounded NSO end when no dispatch code
is supplied. Constructor checks kernel containment and separation from all four
possible TLS pages, main stack, physical Alias and Heap. It charges actual guest
backing, retains the fixed 256 MiB aggregate limit and publishes a distinct
Reserved (0x10), NONE QueryMemory entry. This backing counts as used user memory,
unlike unbacked virtual address-region reservations. Eight three-segment modules
plus dispatch/identity/stack/TLS use 28 initial CPU views, within the unchanged
32-view CPU limit; later mappings must still fit that limit.

CPU construction currently rejects initial NONE regions. The unpublished page
therefore begins as zeroed RW and must successfully reprotect RW→NONE before the
Process returns. A protection failure destroys the CPU and system-resource arena
and returns NULL. Existing constructors allocate no page and retain their old
behavior. Identity generations and Process IDs never wrap or reuse; failed
construction can burn a generation without publishing it.

The 40-byte descriptor contains version 1, its own byte size, Process ID,
reservation generation, guest address and length 4096. It contains no host pointer.
The query copies metadata only. Every later use must validate the exact descriptor
and a currently retained Process CPU context. An unrelated directly-created CPU
sibling is not a Process-owned thread and is refused. An expired thread context
is refused before dereferencing it. Caller must keep the Process alive, serialize
all calls and never manually destroy borrowed Process contexts.

The validator calls the reviewed frozen CPU `set_permissions(page,4096,0,0)`
preflight. In the pinned CPU implementation this checks the entire mapping,
expected NONE, began-RW provenance, and absence of alias source/destination locks,
shared-readonly pages and execution. The exact no-op then returns before memory
allocation, coalescing or monitor invalidation. Tests retain an LDXR reservation
through validation and claim, and show both allocation-failure seams remain
unconsumed. This is stronger than failed reads: a host-created alias can leave its
source NONE but the validator rejects its alias lock.

This proof also relies on existing CPU APIs never removing or replacing an
initial allocation. It does not claim CPU-intrinsic immutability or detect past
host mutations that were fully reversed. Trusted host reprotection remains
possible; current exposure or aliasing causes validation failure. A no-op
validation success is a borrowed instantaneous check, not a lease across future
CPU/mapping operations. Busy/active/reentrant calls fail closed.

A consumer constructor must allocate and validate all private state first, then
call the one-way claim, then publish without fallible work. A rejected claim does
not mutate Process state. Success permanently burns the reservation's namespace:
there is no release/reset when the consumer is destroyed. Recreating an address
cursor in the same Process is therefore refused, preventing old opaque addresses
from becoming new objects. Pure validators are separate from this constructor-only
state-changing operation; no CPU, callback, context or external descriptor is
stored by the reservation APIs.

Guest protection is Process policy: permission changes require CodeData/Normal;
MapMemory source requires those same allowed states and destination must be free;
Shared mapping needs free space and unmap needs exact Shared state; physical
mapping is confined to Alias; Heap resize stays in its separate reservation.
PermissionLocked/Uncached requests cannot admit Reserved, and a zero attribute
mask is a genuine harmless no-op. Unsupported mapping SVCs remain stopped. None
of these paths can replace or expose this page. Trusted host CPU APIs are a
separate authority and are detected at each later validation as described above.

Original tests cover both 36/39-bit profiles, optional dispatch lengths 0/4100,
full metadata/accounting, every implemented memory SVC route with a real time
shared-memory handle, foreign/expired/owned-child contexts, active callback
refusal, allocation/protection failures, max-module construction, exact budget
refusal, irreversible claims, and host permission/alias mutation. They use only
original synthetic modules and arguments. ASan on this macOS toolchain does not
support `detect_leaks`; its attempted unsupported invocation is retained in the
stage history and is not a successful leak check. Failure cleanup additionally
has explicit source review and successful subsequent-construction checks.

Reproduce using the stage's immutable dependency-lock.json and private CPU/IPC
archives, or after reviewed promotion provide the corresponding frozen archive
through `SATCHEL_CPU_PROBE_ARCHIVE`/`SATCHEL_HORIZON_IPC_ARCHIVE` to the existing
Process/thread scripts. No new dependency or canonical artifact is introduced.
