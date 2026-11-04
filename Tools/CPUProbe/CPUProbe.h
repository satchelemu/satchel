#ifndef SATCHEL_CPU_PROBE_H
#define SATCHEL_CPU_PROBE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Experimental, deliberately incomplete AArch64 interpreter; no Horizon/GPU. */
enum { SATCHEL_CPU_READ = 1, SATCHEL_CPU_WRITE = 2, SATCHEL_CPU_EXECUTE = 4 };
enum { SATCHEL_CPU_CONTEXT_LIMIT = 32 };
typedef struct {
    uint64_t address;
    const void *bytes;
    size_t bytes_size;
    size_t memory_size;
    unsigned permissions;
} SatchelCPUProbeRegion;
typedef enum {
    SATCHEL_CPU_BUDGET, SATCHEL_CPU_SVC, SATCHEL_CPU_CANCELLED, SATCHEL_CPU_UNSUPPORTED,
    SATCHEL_CPU_MEMORY_FAULT, SATCHEL_CPU_INTERNAL_FAULT, SATCHEL_CPU_BREAKPOINT
} SatchelCPUProbeStop;
typedef struct {
    SatchelCPUProbeStop reason;
    uint64_t pc;
    uint64_t steps;
    uint32_t instruction;
    uint16_t svc;
    char detail[160];
} SatchelCPUProbeResult;
typedef struct SatchelCPUProbe SatchelCPUProbe;
typedef int (*SatchelCPUProbeCancelled)(void *context);

/* Copies bounded segments; rejects overlap, W+X, >256 MiB, and invalid entry.
 * Contexts must not be used concurrently. Callback must not reenter the probe.
 * All supplied host buffers must be valid for their declared lengths. */
SatchelCPUProbe *satchel_cpu_probe_create(const SatchelCPUProbeRegion *regions, size_t count,
                               uint64_t entry, char *error, size_t error_size);
/* Fresh register/PC/TLS/fault state sharing peer's retained process memory and
 * breakpoint set. Does not clone peer registers or execute/start a thread.
 * Up to 32 contexts share the unchanged 256 MiB/32-region map limits. Initial
 * GPR/SP/TLS defaults match create; caller initializes required SIMD state.
 * All contexts serialize; map changes are immediately visible to siblings.
 * Parent destruction cannot release memory retained by surviving siblings.
 * Rejects active/reentrant/terminal peer or invalid executable entry. */
SatchelCPUProbe *satchel_cpu_probe_create_shared(SatchelCPUProbe *peer, uint64_t entry,
                                      char *error, size_t error_size);
/* Returns 0 while the process is busy or from a run callback; caller retains
 * ownership and must retry outside execution. NULL succeeds. Legacy destroy
 * delegates here and is a no-op on forbidden active/reentrant destruction.
 * Host callers must serialize pointer lifetime; this is not a general-purpose
 * thread-safe reference API and must not race another call with destruction. */
int satchel_cpu_probe_try_destroy(SatchelCPUProbe *probe);
void satchel_cpu_probe_destroy(SatchelCPUProbe *probe);
int satchel_cpu_probe_set_x(SatchelCPUProbe *probe, unsigned reg, uint64_t value);
int satchel_cpu_probe_get_x(SatchelCPUProbe *probe, unsigned reg, uint64_t *value);
int satchel_cpu_probe_set_sp(SatchelCPUProbe *probe, uint64_t value);
/* Guest thread-register values only; never reads or writes host TLS. Values
 * default to zero and are not dereferenced until a checked guest memory access.
 * The scheduler owns TPIDRRO_EL0; guest MSR may change only TPIDR_EL0.
 * Getter requires both non-null outputs and changes neither on failure. */
int satchel_cpu_probe_set_tls(SatchelCPUProbe *probe, uint64_t tpidrro_el0, uint64_t tpidr_el0);
int satchel_cpu_probe_get_tls(SatchelCPUProbe *probe, uint64_t *tpidrro_el0, uint64_t *tpidr_el0);
enum { SATCHEL_CPU_SYSTEM_TICK_FREQUENCY = 19200000 };
/* Same 19.2 MHz virtual counter as guest CNTPCT_EL0/CNTVCT_EL0 (virtual offset
 * zero); CNTFRQ_EL0 reports the constant above. The epoch belongs to the shared
 * process owner, not a thread, and survives parent-context destruction. Uses
 * elapsed non-adjustable host monotonic time including idle/system sleep, never
 * host register contents or wall time. Equal consecutive ticks are valid.
 * Rejects null output, active/sibling reentry or host clock failure/regression;
 * output stays unchanged. No reservation, guest register or memory mutation.
 * No timer interrupt, scheduling, deadline, or wait-service semantics implied. */
int satchel_cpu_probe_get_system_tick(SatchelCPUProbe *probe, uint64_t *ticks);
typedef enum { SATCHEL_CPU_FP_OK, SATCHEL_CPU_FP_INVALID, SATCHEL_CPU_FP_BUSY, SATCHEL_CPU_FP_UNKNOWN } SatchelCPUFPResult;
/* Narrow thread-creation status contract; no general system-register access.
 * Fresh contexts have owned FPCR reset0/FPSR0. Admitted FADD/FMUL D update values
 * and cumulative flags using exact integer arithmetic (FPCR0 or DN only), never
 * the host floating environment. An unmodeled status is UNKNOWN, never guessed
 * zero. Both outputs remain unchanged unless OK.
 * Rejects terminal contexts, aliased/null outputs and active/reentrant use. */
SatchelCPUFPResult satchel_cpu_probe_get_fp_status(SatchelCPUProbe *probe, uint32_t *fpcr, uint32_t *fpsr);
/* Copies actual FPCR and modeled-known FPSR only between distinct, nonterminal
 * contexts sharing the same process memory. Either unknown status rejects.
 * Failure leaves destination entirely unchanged. Does not copy/clear SIMD,
 * GPRs, flags, SP, TLS, PC, memory or reservations; Process initializes a new
 * thread's Q registers explicitly with the existing checked set_q API. */
SatchelCPUFPResult satchel_cpu_probe_copy_fp_status(SatchelCPUProbe *destination, SatchelCPUProbe *source);
int satchel_cpu_probe_set_q(SatchelCPUProbe *probe, unsigned reg, const uint8_t value[16]);
int satchel_cpu_probe_get_q(SatchelCPUProbe *probe, unsigned reg, uint8_t value[16]);
/* Whole-span read preflight preserves the destination on failure. A span may
 * cross adjacent views only when physical offsets are contiguous in the same
 * retained backing and every view grants READ. Zero-size read is a no-op. */
int satchel_cpu_probe_read(SatchelCPUProbe *probe, uint64_t address, void *bytes, size_t size);
/* Nonmutating snapshot check: the nonempty, nonoverflowing span must reside in
 * one retained backing at contiguous physical offsets. Every covered view must
 * contain every requested READ/WRITE/EXECUTE permission. Required
 * permissions must be a nonzero subset of those bits. Rejects busy/reentrant
 * calls. No memory copy, allocation, reservation change or native pointer is
 * exposed. Does not pin a mapping: consumers revalidate on every later use. */
int satchel_cpu_probe_check_range(SatchelCPUProbe *probe, uint64_t address, uint64_t size,
                             unsigned required_permissions);
/* Physical disjointness check for protected guest objects/storage. Both spans
 * must be nonempty, nonoverflowing and completely mapped; permissions are
 * deliberately ignored, so locked source views participate. Returns1 and sets
 * overlap to0/1 only after validating BOTH complete spans. Returns0 on invalid,
 * unmapped or busy/reentrant input, leaving output unchanged. Multi-backing
 * spans are supported. No allocation, reservation change or identities/pointers
 * escape. This is a snapshot, not a mapping lease or permission check. */
int satchel_cpu_probe_physical_overlap(SatchelCPUProbe *probe, uint64_t first, uint64_t first_size,
                                  uint64_t second, uint64_t second_size, int *overlap);
/* Checks the entire nonempty span before writing; a failure changes no bytes.
 * Adjacent views are allowed only with contiguous physical offsets in one
 * retained backing and WRITE permission in every view. Caller buffers valid. */
int satchel_cpu_probe_write(SatchelCPUProbe *probe, uint64_t address, const void *bytes, size_t size);
enum { SATCHEL_CPU_WRITE_SPAN_LIMIT = 8, SATCHEL_CPU_WRITE_BYTE_LIMIT = 2 * 1024 * 1024 };
typedef struct {
    uint64_t address;
    const void *bytes;
    size_t size;
} SatchelCPUProbeWrite;
typedef enum {
    SATCHEL_CPU_WRITE_OK, SATCHEL_CPU_WRITE_INVALID, SATCHEL_CPU_WRITE_LIMIT,
    SATCHEL_CPU_WRITE_MEMORY, SATCHEL_CPU_WRITE_ALLOCATION, SATCHEL_CPU_WRITE_BUSY,
    SATCHEL_CPU_WRITE_INTERNAL
} SatchelCPUWriteResult;
/* Atomic checked guest output transaction: 1–8 nonempty spans, <=2 MiB total.
 * Each destination must fit one retained backing at contiguous physical offsets;
 * every covered view must be writable. Destinations must not overlap virtually
 * or physically. Copies descriptors and snapshots ALL source bytes before any write,
 * so valid source buffers may alias destinations or each other. Sources remain
 * trusted host spans, never guest addresses cast to native pointers. Busy/run
 * callback calls reject. Failure preserves every guest byte and reservation;
 * success commits all spans and clears shared-context reservations once.
 * No callbacks, allocations or recoverable failures occur during final commit.
 * Does not commit kernel handles/state; the caller owns that transaction. */
SatchelCPUWriteResult satchel_cpu_probe_writev(SatchelCPUProbe *probe,
                                   const SatchelCPUProbeWrite *writes, size_t count);
typedef enum {
    SATCHEL_CPU_MAP_OK, SATCHEL_CPU_MAP_INVALID, SATCHEL_CPU_MAP_LIMIT,
    SATCHEL_CPU_MAP_ALLOCATION, SATCHEL_CPU_MAP_BUSY
} SatchelCPUMapResult;
/* Atomic permission-only transaction over an existing page-aligned nonempty
 * span. expected_old and permissions are exactly0,READ,orREAD|WRITE. Every
 * covered view must match expected_old, originate from initially RW storage,
 * and be neither executable, alias-locked, alias-destination nor shared-readonly.
 * Split/coalesce retains the SAME backing/offset/bytes and unique physical charge.
 * Process/kernel state eligibility remains the caller's responsibility.
 * Maximum32 final views; failure preserves all mappings, bytes and reservations.
 * Same-permission validated no-op allocates nothing and preserves reservations;
 * actual success clears all sibling reservations. Reentrant/active calls reject.
 * Allocation-replacing resize/range operations reject fragmented backings until
 * a restore/coalesce makes them whole again; no implicit permission restoration.
 */
SatchelCPUMapResult satchel_cpu_probe_set_permissions(SatchelCPUProbe *, uint64_t address,
                                           uint64_t size, unsigned expected_old,
                                           unsigned permissions);
enum { SATCHEL_CPU_SHARED_READONLY_SIZE = 4096, SATCHEL_CPU_SHARED_READONLY_LIMIT = 32 };
/* Process-owned immutable shared page, not a guest mapping or host pointer.
 * Copies exactly4096 valid host bytes before committing a monotonic token.
 * Owned but unmapped pages count toward the shared256MiB backing budget.
 * Maximum32 live shared objects; mapped aliases also consume the existing
 * 32-view map limit. All failures leave output/state/bytes unchanged.
 * All APIs reject active/sibling reentry; caller serializes CPU lifetime. */
SatchelCPUMapResult satchel_cpu_probe_shared_readonly_create(SatchelCPUProbe *, const void *bytes,
                                                  size_t size, uint64_t *token);
/* Map the full immutable page at an aligned empty guest address, READ only.
 * Multiple mappings retain the SAME bytes and incur no duplicate backing
 * charge. Tokens belong to one shared process, including its sibling contexts.
 * No executable or writable view can be requested. */
SatchelCPUMapResult satchel_cpu_probe_shared_readonly_map(SatchelCPUProbe *, uint64_t token,
                                               uint64_t address, uint64_t size);
/* Exact token/address/full-page correspondence required; no arbitrary memory
 * removal. Maps survive release of host ownership and parent-context teardown.
 * Successful map/unmap clears shared reservations; rejected operations do not. */
SatchelCPUMapResult satchel_cpu_probe_shared_readonly_unmap(SatchelCPUProbe *, uint64_t token,
                                                 uint64_t address, uint64_t size);
/* Drops the single host ownership reference. Existing maps retain the page and
 * can still be unmapped with the token; new maps thereafter reject. Unique bytes
 * cease to count only after the final ownership/mapping reference disappears.
 * No guest mapping/register/reservation changes. Final CPU-owner destruction
 * releases its registry and maps; no CPU pointer or external allocation leaks. */
SatchelCPUMapResult satchel_cpu_probe_shared_readonly_release(SatchelCPUProbe *, uint64_t token);
/* Creates/resizes a dynamic RW mapping at an exact 4096-byte-aligned base.
 * Size must be page-aligned; zero removes an existing dynamic mapping.
 * Initial mappings cannot be replaced. Preserves the resized prefix and zeros
 * new bytes. Rejects overlap, overflow, >32 regions or >256 MiB aggregate.
 * Every failure preserves all mappings and bytes. Mutation during run or its
 * callback is rejected; contexts still must not be used concurrently. */
SatchelCPUMapResult satchel_cpu_probe_resize_rw(SatchelCPUProbe *probe, uint64_t base, uint64_t size);
/* Atomic dynamic-RW range operations. Nonzero page-aligned base/size, base>=4096.
 * Map fills gaps without replacing existing dynamic RW bytes; unmap tolerates
 * gaps and preserves surviving fragments. Any initial/immutable overlap rejects.
 * Map coalesces existing backings intersecting the explicit request, retaining
 * their full extents; adjacent outside mappings remain distinct. Callers can
 * expand a request to include known adjacent spans they own. Final backing
 * remains <=256 MiB and <=32 regions. All allocations precede commit. Output reports
 * actual bytes added/removed on success, including zero for a no-op; failures
 * leave output, all mappings, bytes and reservations unchanged. Actual changes
 * invalidate shared reservations; no-ops do not. Caller owns address policy. */
SatchelCPUMapResult satchel_cpu_probe_map_missing_rw(SatchelCPUProbe *probe, uint64_t base,
                                         uint64_t size, uint64_t *mapped_bytes);
SatchelCPUMapResult satchel_cpu_probe_unmap_rw(SatchelCPUProbe *probe, uint64_t base,
                                    uint64_t size, uint64_t *unmapped_bytes);
/* Genuine retained-backing aliases, not copied memory. Both ranges must be
 * nonoverlapping, nonempty and 4096-byte aligned. Map requires a completely
 * mapped RW source (initial or dynamic, never another alias) and empty target.
 * Source pages become inaccessible; destination pages are RW views of the SAME
 * bytes. Unmap validates source/destination physical correspondence, removes
 * destination views and restores source RW. Partial page-aligned unmap is valid.
 * Multiple adjacent source allocations are supported; whole-span access across
 * views is valid only when their physical bytes are contiguous in one backing.
 * Unique allocation bytes stay unchanged (<=256 MiB); final views remain <=32.
 * Resize/free/range replacement of any allocation with a live alias rejects.
 * After its last alias is removed, original contiguous views coalesce again.
 * All failures preserve mappings, bytes and reservations. Successful commits
 * invalidate shared reservations. Caller owns Horizon states/address policy. */
SatchelCPUMapResult satchel_cpu_probe_map_alias(SatchelCPUProbe *probe, uint64_t destination,
                                    uint64_t source, uint64_t size);
SatchelCPUMapResult satchel_cpu_probe_unmap_alias(SatchelCPUProbe *probe, uint64_t destination,
                                      uint64_t source, uint64_t size);
/* Copies/replaces the shared process's up to 32 distinct executable addresses.
 * Count zero clears the set. A hit stops before fetching/executing/counting the
 * instruction, leaves all registers untouched, and does not invoke a callback.
 * Repeated run stops again until the caller changes PC or removes the point.
 * Budget zero wins first; cancellation wins before breakpoint detection.
 * Registration and PC mutation reject active runs and terminal-fault contexts.
 * Invalid input leaves the previous set or PC unchanged. */
int satchel_cpu_probe_set_breakpoints(SatchelCPUProbe *probe, const uint64_t *addresses, size_t count);
/* Validates a complete aligned 4-byte executable guest fetch. To dispatch a
 * guest return, validate/set the PC first, then set result registers; never put
 * native function pointers into guest PC/LR. This cannot revive a fault. */
int satchel_cpu_probe_set_pc(SatchelCPUProbe *probe, uint64_t address);
SatchelCPUProbeResult satchel_cpu_probe_run(SatchelCPUProbe *probe, uint64_t budget,
                                SatchelCPUProbeCancelled cancelled, void *context);
#ifdef __cplusplus
}
#endif
#endif
