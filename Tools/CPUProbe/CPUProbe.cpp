#include "CPUProbe.h"
#include "Binary64.h"
#include "aarch64/simulator-aarch64.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <time.h>
#include <type_traits>
#include <vector>

#if !defined(VIXL_DEBUG) || !defined(VIXL_NEGATIVE_TESTING) || !defined(SATCHEL_VIXL_MEMORY_GUARD)
#error Probe requires checked, catchable VIXL assertions and its memory guard patch.
#endif

using namespace vixl::aarch64;
namespace {
struct Fault : std::runtime_error {
    SatchelCPUProbeStop reason;
    Fault(SatchelCPUProbeStop r, const char *s) : std::runtime_error(s), reason(r) {}
};
// Views retain one allocation. Splitting a permission interval never copies its
// bytes; aliases share this allocation and are charged only once.
struct BackingView {
    std::shared_ptr<std::vector<uint8_t>> backing;
    size_t offset = 0, length = 0;
    BackingView() = default;
    BackingView(std::vector<uint8_t> bytes)
        : backing(std::make_shared<std::vector<uint8_t>>(std::move(bytes))), length(backing->size()) {}
    size_t size() const { return length; }
    uint8_t *data() const { return backing ? backing->data() + offset : nullptr; }
    void resize(size_t size) { *this = BackingView(std::vector<uint8_t>(size)); }
    void swap(BackingView &other) noexcept {
        backing.swap(other.backing); std::swap(offset, other.offset); std::swap(length, other.length);
    }
};
struct Region {
    uint64_t address;
    unsigned permissions;
    BackingView bytes;
    bool dynamic = false;
    bool alias_locked = false;
    uint64_t alias_source = 0; // Nonzero only on a destination alias view.
    uint64_t shared_readonly = 0; // Process-owned immutable shared-page token.
    bool permission_mutable = false; // Immutable provenance: this storage began RW.
};
static uint64_t physical_bytes(const std::vector<Region> &regions) {
    uint64_t total = 0;
    for (size_t i = 0; i < regions.size(); ++i) {
        bool seen = false;
        for (size_t j = 0; j < i; ++j)
            if (regions[i].bytes.backing == regions[j].bytes.backing) { seen = true; break; }
        if (!seen) total += regions[i].bytes.backing->size();
    }
    return total;
}
static bool backing_is_aliased(const std::vector<Region> &regions, const Region &region) {
    for (const auto &r : regions)
        if (r.bytes.backing == region.bytes.backing && (r.alias_locked || r.alias_source)) return true;
    return false;
}
// Replacement APIs require a whole physical allocation, not a permission view.
// Copying one fragment would leave the original allocation retained elsewhere
// and invalidate the old unique-backing accounting/prefix-preservation model.
static bool backing_is_fragmented(const std::vector<Region> &regions, const Region &region) {
    if (region.bytes.offset || region.bytes.size() != region.bytes.backing->size()) return true;
    for (const auto &other : regions)
        if (&other != &region && other.bytes.backing == region.bytes.backing) return true;
    return false;
}
static const Region *region_at(const std::vector<Region> &regions, uint64_t address) {
    for (const auto &r : regions)
        if (address >= r.address && address - r.address < r.bytes.size()) return &r;
    return nullptr;
}
class CheckedSimulator : public Simulator {
public:
    explicit CheckedSimulator(Decoder *decoder) : Simulator(decoder) {
        SilenceExclusiveAccessWarning();
    }
    void VisitException(const Instruction *) override {
        throw Fault(SATCHEL_CPU_UNSUPPORTED, "Exception handler is disabled");
    }
    void VisitUnconditionalBranchToRegister(const Instruction *) override {
        throw Fault(SATCHEL_CPU_UNSUPPORTED, "Native branch callbacks are disabled");
    }
    void VisitSystem(const Instruction *) override {
        throw Fault(SATCHEL_CPU_UNSUPPORTED, "System instruction is not implemented");
    }
    void VisitUnimplemented(const Instruction *) override {
        throw Fault(SATCHEL_CPU_UNSUPPORTED, "VIXL instruction is not implemented");
    }
    void VisitUnallocated(const Instruction *) override {
        throw Fault(SATCHEL_CPU_UNSUPPORTED, "Unallocated instruction");
    }
    void VisitLoadStorePairPreIndex(const Instruction *instruction) override {
        // Zero writeback is valid AArch64, but VIXL's address helper asserts a
        // nonzero writeback offset. The equivalent offset form keeps all checks.
        if (instruction->GetImmLSPair() == 0) Simulator::VisitLoadStorePairOffset(instruction);
        else Simulator::VisitLoadStorePairPreIndex(instruction);
    }
    void VisitLoadStorePairPostIndex(const Instruction *instruction) override {
        if (instruction->GetImmLSPair() == 0) Simulator::VisitLoadStorePairOffset(instruction);
        else Simulator::VisitLoadStorePairPostIndex(instruction);
    }
    void VisitLoadStorePreIndex(const Instruction *instruction) override {
        if (instruction->GetImmLS() == 0) Simulator::VisitLoadStoreUnscaledOffset(instruction);
        else Simulator::VisitLoadStorePreIndex(instruction);
    }
    void VisitLoadStorePostIndex(const Instruction *instruction) override {
        if (instruction->GetImmLS() == 0) Simulator::VisitLoadStoreUnscaledOffset(instruction);
        else Simulator::VisitLoadStorePostIndex(instruction);
    }
};
void copy_error(char *out, size_t size, const char *text) {
    if (out && size) std::snprintf(out, size, "%s", text);
}
int64_t sign_extend(uint64_t value, unsigned bits) {
    const uint64_t sign = uint64_t{1} << (bits - 1);
    return static_cast<int64_t>((value ^ sign) - sign);
}
bool is_gpr_pair(uint32_t op) {
    // W/X GPR offset/pre/post forms only; excludes SIMD, LDPSW and LDNP/STNP.
    const uint32_t form = op & 0x7f800000;
    return form == 0x29000000 || form == 0x29800000 || form == 0x28800000;
}
bool is_prefetch_hint(uint32_t op) {
    // Exact Armv8 baseline prefetch forms. Register option<1> must be one;
    // this excludes unallocated extensions and later RPRFM encodings.
    return (op & 0xffc00000) == 0xf9800000 || // PRFM unsigned immediate
           (op & 0xffe00c00) == 0xf8800000 || // PRFUM unscaled, no writeback
           (op & 0xff000000) == 0xd8000000 || // PRFM PC-relative literal
           ((op & 0xffe00c00) == 0xf8a00800 && (op & 0x4000));
}

bool is_simd_q_pair(uint32_t op) {
    // Exact128-bit SIMD/FP offset/pre/post pairs only. S/D, non-temporal,
    // newer tagged-pair and reserved opc=3 encodings remain excluded.
    const uint32_t form = op & 0xff800000;
    return form == 0xad000000 || form == 0xad800000 || form == 0xac800000;
}
bool is_gpr_unscaled(uint32_t op) {
    const uint32_t form = op & 0xffe00000;
    // Byte/H/W/X unsigned loads and stores only. Bit21=0 excludes register
    // offsets; mode2 is the unprivileged family and is never accepted here.
    return ((op >> 10) & 3) != 2 &&
           (form == 0x38000000 || form == 0x38400000 ||
            form == 0x78000000 || form == 0x78400000 ||
            form == 0xb8000000 || form == 0xb8400000 ||
            form == 0xf8000000 || form == 0xf8400000);
}
bool is_simd_d_unscaled(uint32_t op) {
    // Exact D-register64-bit unscaled/pre/post only; mode2, other SIMD
    // widths, unsigned/register offsets and reserved opc remain excluded.
    const uint32_t form = op & 0xffe00000;
    return ((op >> 10) & 3) != 2 && (form == 0xfc000000 || form == 0xfc400000);
}
bool is_simd_other_unscaled(uint32_t op) {
    // B/H/S/Q unscaled/pre/post transfers only. Opc2/3 means Q only when
    // size==0; all other combinations, unprivileged and register offsets reject.
    const uint32_t form = op & 0xffe00000;
    return ((op >> 10) & 3) != 2 &&
           (form == 0x3c000000 || form == 0x3c400000 ||
            form == 0x7c000000 || form == 0x7c400000 ||
            form == 0xbc000000 || form == 0xbc400000 ||
            form == 0x3c800000 || form == 0x3cc00000);
}
bool is_simd_register(uint32_t op) {
    // Exact B/H/S/D/Q register-offset transfers, allocated extension modes
    // UXTW/UXTX/SXTW/SXTX only. Rm31 is ZR; no base writeback exists.
    const unsigned extend = (op >> 13) & 7;
    if (extend != 2 && extend != 3 && extend != 6 && extend != 7) return false;
    const uint32_t form = op & 0xffe00c00;
    return form == 0x3c200800 || form == 0x3c600800 ||
           form == 0x7c200800 || form == 0x7c600800 ||
           form == 0xbc200800 || form == 0xbc600800 ||
           form == 0xfc200800 || form == 0xfc600800 ||
           form == 0x3ca00800 || form == 0x3ce00800;
}
bool is_simd_d_unsigned(uint32_t op) {
    // Exact D-register64-bit unsigned imm12, scaled by8. Other SIMD widths,
    // opc variants and register/literal addressing are not added here.
    const uint32_t form = op & 0xffc00000;
    return form == 0xfd000000 || form == 0xfd400000;
}
bool is_register_memory(uint32_t op) {
    const uint32_t form = op & 0xffe00c00;
    // Enumerate the integer transfers; reserved opc/size combinations,
    // prefetch and SIMD/FP remain outside this memory path.
    return form == 0x38200800 || form == 0x78200800 || form == 0xb8200800 || form == 0xf8200800 ||
           form == 0x38600800 || form == 0x78600800 || form == 0xb8600800 || form == 0xf8600800 ||
           form == 0x38a00800 || form == 0x38e00800 || form == 0x78a00800 || form == 0x78e00800 ||
           form == 0xb8a00800;
}
bool is_gpr_unsigned(uint32_t op) {
    const uint32_t form = op & 0xffc00000;
    return form == 0x39000000 || form == 0x79000000 || form == 0xb9000000 || form == 0xf9000000 ||
           form == 0x39400000 || form == 0x79400000 || form == 0xb9400000 || form == 0xf9400000 ||
           form == 0x39800000 || form == 0x39c00000 || form == 0x79800000 || form == 0x79c00000 ||
           form == 0xb9800000;
}
bool is_gpr_exclusive(uint32_t op) {
    // Exact single W/X LDXR/LDAXR and STXR/STLXR. No pairs, byte/halfword,
    // non-exclusive acquire/release, CAS or other atomic operations.
    return (op & 0xbfff7c00) == 0x885f7c00 || (op & 0xbfe07c00) == 0x88007c00;
}
bool is_gpr_acquire_release(uint32_t op) {
    // Exact non-exclusive LDAR/STLR B/H/W/X, excluding limited-order and
    // newer RCpc operations. Rs/Rt2 are fixed architectural reserved fields.
    return (op & 0x3ffffc00) == 0x08dffc00 || (op & 0x3ffffc00) == 0x089ffc00;
}
bool is_ordinary_store(uint32_t op) {
    return ((is_gpr_pair(op) || is_simd_q_pair(op)) && !(op & 0x00400000)) ||
           ((is_gpr_unscaled(op) || is_simd_d_unscaled(op) || is_simd_other_unscaled(op) || is_simd_register(op) || is_simd_d_unsigned(op)) && !(op & 0x00400000)) ||
           ((is_register_memory(op) || is_gpr_unsigned(op)) && !(op & 0x00c00000)) ||
           (op & 0xffc00000) == 0x3d800000 ||
           (is_gpr_acquire_release(op) && !(op & 0x00400000));
}
bool is_fmov_gpr(uint32_t op) {
    // Exact GPR/SIMD bit transfers only. No FP16, conversion, immediate,
    // scalar FP-to-FP move or arithmetic is admitted by this predicate.
    switch (op & 0xfffffc00) {
    case 0x1e260000: case 0x1e270000: case 0x9e660000:
    case 0x9e670000: case 0x9eae0000: case 0x9eaf0000: return true;
    default: return false;
    }
}
bool is_binary64_arithmetic(uint32_t op) {
    // These are the only admitted FP arithmetic families. VIXL produces their
    // values but does not model FPSR cumulative exception bits. New FP or
    // saturating SIMD coverage must explicitly audit this status boundary too.
    return (op & 0xffe0fc00) == 0x1e602800 || (op & 0xffe0fc00) == 0x1e600800;
}
/* Every accepted family must be reviewed for direct host-memory/native-call
 * paths. This is intentionally not "all instructions supported by VIXL". */
bool reviewed_instruction(uint32_t op) {
    const bool wide = (op >> 31) != 0;
    const unsigned shift = (op >> 10) & 63;
    if ((op & 0x1f800000) == 0x11000000) return true; // ADD/SUB immediate (not tag operations).
    if ((op & 0x1f200000) == 0x0b000000)
        return ((op >> 22) & 3) != 3 && (wide || shift < 32); // ADD/SUB shifted register.
    if ((op & 0x1fe00000) == 0x0b200000)
        return ((op >> 10) & 7) <= 4; // ADD/SUB extended register; imm3 5..7 is unallocated.
    if ((op & 0x1f000000) == 0x0a000000)
        return wide || shift < 32; // Logical shifted register; invalid W rotates cause upstream UB.
    if ((op & 0x1f800000) == 0x12800000)
        return ((op >> 29) & 3) != 1 && (wide || ((op >> 21) & 3) < 2); // MOVN/Z/K.
    if ((op & 0x1f800000) == 0x13000000)
        return ((op >> 29) & 3) != 3 && wide == bool(op & 0x00400000) &&
               (wide || (((op >> 16) & 63) < 32 && shift < 32)); // Bitfield.
    if ((op & 0x1f800000) == 0x12000000)
        return wide || !(op & 0x00400000); // Logical immediate.
    // Only MADD/MSUB W/X (including MUL/MNEG with Ra==ZR). VIXL uses
    // uint64_t multiply/add/subtract and truncates W results; NZCV is untouched.
    if ((op & 0x7fe00000) == 0x1b000000) return true;
    // Exact high products require Ra==ZR. The audited MultiplyHigh helper
    // splits unsigned limbs; signed widening multiplies fit in int64_t before
    // the accumulator addition/subtraction is performed modulo uint64_t.
    if ((op & 0xffe0fc00) == 0x9b407c00 || (op & 0xffe0fc00) == 0x9bc07c00) return true;
    if ((op & 0xffe00000) == 0x9b200000 || (op & 0xffe00000) == 0x9ba00000) return true;
    // Exact LSLV/LSRV/ASRV/RORV W/X. The audited handler masks counts to
    // 5/6 bits before unsigned ShiftOperand, including safe zero-count rotates.
    if ((op & 0x7fe0f000) == 0x1ac02000) return true;
    // Exact UDIV/SDIV W/X. The audited handler explicitly returns zero for
    // divisor zero and the signed minimum for MIN/-1 before native division.
    if ((op & 0x7fe0fc00) == 0x1ac00800 || (op & 0x7fe0fc00) == 0x1ac00c00) return true;
    // CSEL/CSINC/CSINV/CSNEG W/X, including CSET/CSETM aliases. Bit11 and
    // the S/reserved fields remain fixed; the handler uses unsigned results.
    if ((op & 0x3fe00800) == 0x1a800000) return true;
    // CCMP/CCMN register or immediate W/X. S must be set, bits10/4 must
    // remain zero; bit11 selects immediate. Flags arithmetic is unsigned.
    if ((op & 0x3fe00410) == 0x3a400000) return true;
    if (is_gpr_pair(op)) {
        const unsigned rt = op & 31, rn = (op >> 5) & 31, rt2 = (op >> 10) & 31;
        const bool load = op & 0x00400000;
        const bool writeback = ((op >> 23) & 3) != 2;
        // Reject constrained-unpredictable destination and writeback aliases.
        // Rn==31 denotes SP; Rt/Rt2==31 denote XZR and do not alias SP.
        return !(load && rt == rt2) && !(writeback && rn != 31 && (rn == rt || rn == rt2));
    }
    if (is_simd_q_pair(op)) {
        // Q and X/SP are distinct register files: matching register numbers do
        // NOT create a writeback alias. Q31 is real SIMD state, never XZR/SP.
        // Equal load destinations are constrained-unpredictable; stores may
        // duplicate a source. Transfer only copies bits, not FP arithmetic.
        return !(op & 0x00400000) || (op & 31) != ((op >> 10) & 31);
    }
    if (is_gpr_unscaled(op)) {
        const unsigned rt = op & 31, rn = (op >> 5) & 31;
        const bool writeback = ((op >> 10) & 3) != 0;
        return !(writeback && rn != 31 && rn == rt);
    }
    // D and X/SP register banks are distinct; identical register numbers are
    // valid even with writeback. D31 is a real SIMD register, never ZR.
    if (is_simd_d_unscaled(op) || is_simd_other_unscaled(op) || is_simd_register(op) || is_simd_d_unsigned(op)) return true;
    if (is_register_memory(op)) {
        const unsigned extend = (op >> 13) & 7;
        return extend == 2 || extend == 3 || extend == 6 || extend == 7;
    }
    if (is_gpr_unsigned(op)) return true;
    if ((op & 0xffc00000) == 0x3d800000 ||
        (op & 0xffc00000) == 0x3dc00000) return true; // Existing Q unsigned immediate forms only.
    if (is_gpr_exclusive(op)) {
        if (op & 0x00400000) return true;
        const unsigned rs = (op >> 16) & 31, rt = op & 31, rn = (op >> 5) & 31;
        // Constrained-unpredictable status/source or status/base aliases.
        return rs != rt && (rn == 31 || rs != rn);
    }
    if (is_gpr_acquire_release(op)) return true;
    if ((op & 0x9ff80c00) == 0x0f000400) {
        // Integer MOVI only. Cmode15/FMOV and bit11/FP16 remain excluded;
        // odd modes below12 are ORR/BIC, op1 except cmode14 is MVNI.
        // Audited VisitNEONModifiedImmediate uses only unsigned expansion and
        // register-only movi; Q=0 clears upper64, including scalar D.
        const unsigned cmode = (op >> 12) & 15;
        if (op & 0x20000000) return cmode == 14;
        return cmode == 12 || cmode == 13 || cmode == 14 || (cmode < 12 && !(cmode & 1));
    }
    if ((op & 0xbfe0fc00) == 0x0e000400) {
        // Vector-element DUP only. The least imm5 bit selects lane width;
        // remaining bits select an element from the full128-bit source.
        // Snapshot-before-clear helper supports Rd=Rn, Q31 is a real vector.
        const unsigned size_bits = (op >> 16) & 15;
        return size_bits && (size_bits != 8 || (op & 0x40000000));
    }
    if ((op & 0xbf80fc00) == 0x0f008400) {
        // SHRN/SHRN2 only: H->B, S->H, D->S; immh selects a nonzero
        // destination width <=32 and right shift1..width. Exclude rounding,
        // saturation, scalar forms and immh[3]. Owned temporary snapshots
        // source before narrowing; SHRN2 preserves the old low64 bits.
        const unsigned immh = (op >> 19) & 15;
        return immh && immh < 8; // immh=0 MOVI was handled above.
    }
    if ((op & 0xbfe0fc00) == 0x0e000c00) {
        // DUP from an owned general register (Rn31 is ZR, not SP). The
        // least set imm5 bit chooses B/H/S/D; higher bits are ignored by ARM.
        // Zero/bit4-only and Q=0 D arrangements are unallocated. Exclude all
        // element/scalar/insert/SVE forms before VIXL's format decoder.
        const unsigned size_bits = (op >> 16) & 15;
        return size_bits && (size_bits != 8 || (op & 0x40000000));
    }
    if ((op & 0x9f20fc00) == 0x0e208400) {
        // Integer vector ADD/SUB only. U selects subtraction; size selects
        // B/H/S/D lanes. A 64-bit vector containing one D lane is unallocated.
        // Reviewed VIXL helpers use unsigned modulo arithmetic, not FP or
        // saturating arithmetic, and clear upper64 for Q=0 destinations.
        return ((op >> 22) & 3) != 3 || (op & 0x40000000);
    }
    switch (op & 0xbfe0fc00) {
    case 0x0e201c00: // AND
    case 0x0e601c00: // BIC
    case 0x0ea01c00: // ORR (including the register MOV alias)
    case 0x0ee01c00: // ORN
    case 0x2e201c00: // EOR
    case 0x2e601c00: // BSL: old destination is the mask
    case 0x2ea01c00: // BIT: insert when mask bits are one
    case 0x2ee01c00: // BIF: insert when mask bits are zero
        // Three-register vector bitwise operations, 8B/16B only. Reviewed
        // helpers read each old destination/source lane before writing it;
        // Q=0 clears upper64. No immediate/scalar/FP/SVE form is admitted.
        return true;
    }
    switch (op & 0xbf20fc00) {
    case 0x2e208c00: // CMEQ
    case 0x0e203c00: // CMGE (signed)
    case 0x0e203400: // CMGT (signed)
    case 0x2e203400: // CMHI (unsigned)
    case 0x2e203c00: // CMHS (unsigned)
    case 0x0e208c00: // CMTST
        // Three-register integer vector comparisons only. Q=0 D is
        // unallocated; scalar, compare-with-zero and FP variants stay stopped.
        // Helpers write a whole all-ones/zero lane without modifying NZCV/FP.
        return ((op >> 22) & 3) != 3 || (op & 0x40000000);
    }
    if ((op & 0xbfe08400) == 0x2e000000) {
        // EXT uses byte lanes and an immediate strictly inside its vector.
        // Read all source bytes into the bounded temporary before any writes.
        return (op & 0x40000000) || ((op >> 11) & 15) < 8;
    }
    switch (op & 0xbf20fc00) {
    case 0x0e001800: case 0x0e005800: // UZP1/UZP2
    case 0x0e002800: case 0x0e006800: // TRN1/TRN2
    case 0x0e003800: case 0x0e007800: // ZIP1/ZIP2
        // Exact integer lane permutations; one-lane D arrangements reject.
        // Audited helpers snapshot all active input lanes before clearing and
        // replacing owned destination lanes, including overlapping registers.
        return ((op >> 22) & 3) != 3 || (op & 0x40000000);
    }
    switch (op & 0xbf3ffc00) {
    case 0x0e205800: // CNT: population count, one result per byte.
        return ((op >> 22) & 3) == 0;
    case 0x0e204800: // CLS: leading sign bits, excluding sign bit itself.
    case 0x2e204800: // CLZ: leading zero bits.
        // Exact integer vector counts, B/H/S in either vector width. D,
        // scalar, FP, saturating and other two-register misc forms reject.
        // Pinned helpers snapshot all lane counts before clearing/writing
        // the destination, including Rd==Rn; Q=0 clears upper64.
        return ((op >> 22) & 3) < 3;
    }
    switch (op & 0xbf3ffc00) {
    case 0x0e303800: // SADDLV
    case 0x2e303800: // UADDLV
        // Widening integer reductions: B/H in either vector width, S only
        // in 128 bits. No D, scalar-pairwise or floating reduction forms.
        // All lanes are read before the widened H/S/D scalar write. Signed
        // sums fit int64 (at most four int32 lanes); unsigned sums fit uint64.
        return ((op >> 22) & 3) < 2 ||
               (((op >> 22) & 3) == 2 && (op & 0x40000000));
    }
    switch (op & 0xbf3ffc00) {
    case 0x0e31b800: // ADDV
    case 0x0e30a800: // SMAXV
    case 0x0e31a800: // SMINV
    case 0x2e30a800: // UMAXV
    case 0x2e31a800: // UMINV
        // Integer across-lane reductions: B/H in either vector width, S only
        // in 128 bits. No D, widening, scalar-pairwise or FP reductions.
        // Pinned helpers finish reading before clearing/writing the scalar
        // destination. ADDV accumulates at most four signed32 lanes in int64.
        return ((op >> 22) & 3) < 2 ||
               (((op >> 22) & 3) == 2 && (op & 0x40000000));
    }
    if ((op & 0xffe0fc00) == 0x1e602800 ||
        (op & 0xffe0fc00) == 0x1e600800) return true; // FADD/FMUL D.
    return false;
}
bool condition_holds(unsigned cond, uint32_t flags) {
    bool n = flags >> 31, z = (flags >> 30) & 1;
    bool c = (flags >> 29) & 1, v = (flags >> 28) & 1;
    bool result = false;
    switch (cond >> 1) {
        case 0: result = z; break;
        case 1: result = c; break;
        case 2: result = n; break;
        case 3: result = v; break;
        case 4: result = c && !z; break;
        case 5: result = n == v; break;
        case 6: result = !z && n == v; break;
        case 7: result = true; break;
    }
    return (cond & 1) && cond != 15 ? !result : result;
}
uint64_t monotonic_nanoseconds() {
    // Darwin's non-adjustable monotonic clock includes system sleep. Never read
    // host CNT* registers or expose the host uptime/clock epoch to guest code.
    timespec value{};
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &value) || value.tv_sec < 0 ||
        value.tv_nsec < 0 || value.tv_nsec >= 1000000000 ||
        uint64_t(value.tv_sec) > (UINT64_MAX - uint64_t(value.tv_nsec)) / 1000000000)
        throw Fault(SATCHEL_CPU_INTERNAL_FAULT, "Host monotonic clock is unavailable");
    return uint64_t(value.tv_sec) * 1000000000 + uint64_t(value.tv_nsec);
}
uint64_t ticks_from_nanoseconds(uint64_t ns) {
    // 19.2 MHz = 12/625 ticks/ns. Multiplying the full ns value first can
    // overflow; both products below are bounded for every uint64_t input.
    return (ns / 625) * 12 + ((ns % 625) * 12) / 625;
}
}

struct SharedReadOnly {
    uint64_t token = 0;
    std::shared_ptr<std::vector<uint8_t>> owned;
    std::weak_ptr<std::vector<uint8_t>> storage;
};
static std::atomic<uint64_t> next_shared_readonly_token{1};
struct SharedMemory {
    std::vector<Region> regions;
    std::array<SharedReadOnly, SATCHEL_CPU_SHARED_READONLY_LIMIT> readonly_pages{};
    std::array<SatchelCPUProbe *, SATCHEL_CPU_CONTEXT_LIMIT> contexts{};
    std::array<uint64_t, 32> breakpoints{};
    size_t breakpoint_count = 0;
    SatchelCPUProbe *last_context = nullptr;
    std::atomic_flag busy = ATOMIC_FLAG_INIT;
    const uint64_t clock_origin_ns = monotonic_nanoseconds();
    uint64_t last_clock_elapsed_ns = 0;
#if defined(SATCHEL_CPU_TESTING)
    bool test_clock = false, test_clock_failure = false;
    uint64_t test_clock_elapsed_ns = 0;
#endif
};
static uint64_t physical_bytes(const SharedMemory &memory) {
    uint64_t total = physical_bytes(memory.regions);
    for (size_t i = 0; i < memory.readonly_pages.size(); ++i) {
        auto backing = memory.readonly_pages[i].storage.lock();
        if (!backing) continue;
        bool seen = false;
        for (const auto &region : memory.regions)
            if (region.bytes.backing == backing) { seen = true; break; }
        for (size_t j = 0; j < i && !seen; ++j)
            if (memory.readonly_pages[j].storage.lock() == backing) seen = true;
        if (!seen) total += backing->size();
    }
    return total;
}
static uint64_t system_tick(SharedMemory &memory) {
    uint64_t elapsed;
#if defined(SATCHEL_CPU_TESTING)
    if (memory.test_clock) {
        if (memory.test_clock_failure) throw Fault(SATCHEL_CPU_INTERNAL_FAULT, "Host monotonic clock is unavailable");
        elapsed = memory.test_clock_elapsed_ns;
    } else
#endif
    {
        const uint64_t now = monotonic_nanoseconds();
        if (now < memory.clock_origin_ns) throw Fault(SATCHEL_CPU_INTERNAL_FAULT, "Host monotonic clock moved backwards");
        elapsed = now - memory.clock_origin_ns;
    }
    if (elapsed < memory.last_clock_elapsed_ns)
        throw Fault(SATCHEL_CPU_INTERNAL_FAULT, "Host monotonic clock moved backwards");
    memory.last_clock_elapsed_ns = elapsed;
    return ticks_from_nanoseconds(elapsed);
}
struct SatchelCPUProbe {
    explicit SatchelCPUProbe(std::shared_ptr<SharedMemory> shared = std::make_shared<SharedMemory>())
        : memory(std::move(shared)) {}
    std::shared_ptr<SharedMemory> memory;
    Decoder decoder;
    CheckedSimulator simulator{&decoder};
    uint64_t pc = 0;
    uint64_t tpidrro_el0 = 0, tpidr_el0 = 0;
    // Owned cumulative status; scalar FADD/FMUL update it through exact integer
    // arithmetic. No host floating environment participates.
    uint32_t modeled_fpsr = 0;
    bool fp_status_known = true;
    bool terminal = false;
    SatchelCPUProbeResult last{};

    void *translate(uint64_t address, size_t size, unsigned permission) {
        if (!size || address > UINT64_MAX - size)
            throw Fault(SATCHEL_CPU_MEMORY_FAULT, "Invalid guest memory span");
        const Region *first = region_at(memory->regions, address);
        if (!first) throw Fault(SATCHEL_CPU_MEMORY_FAULT, "Guest memory address is outside mapped segments");
        const uint64_t physical_offset = first->bytes.offset + address - first->address;
        if ((first->permissions & permission) != permission)
            throw Fault(SATCHEL_CPU_MEMORY_FAULT, "Guest memory permission denied");
        if (size <= first->bytes.size() - (address - first->address))
            return first->bytes.backing->data() + physical_offset;
        uint64_t cursor = first->address + first->bytes.size();
        while (cursor < address + size) {
            const Region *region = region_at(memory->regions, cursor);
            if (!region || region->bytes.backing != first->bytes.backing ||
                region->bytes.offset + (cursor - region->address) != physical_offset + (cursor - address))
                throw Fault(SATCHEL_CPU_MEMORY_FAULT, "Guest span crosses unrelated memory backings");
            if ((region->permissions & permission) != permission)
                throw Fault(SATCHEL_CPU_MEMORY_FAULT, "Guest memory permission denied");
            cursor = std::min(address + size, region->address + region->bytes.size());
        }
        return first->bytes.backing->data() + physical_offset;
    }
};

static thread_local SatchelCPUProbe *active_probe = nullptr;
// All contexts sharing a process map serialize through one nonblocking guard.
// Host callers still own pointer lifetime and must not race destruction.
struct Access {
    std::shared_ptr<SharedMemory> memory;
    explicit Access(SatchelCPUProbe *p) {
        if (p && !active_probe && !p->memory->busy.test_and_set(std::memory_order_acquire))
            memory = p->memory;
    }
    ~Access() { if (memory) memory->busy.clear(std::memory_order_release); }
    explicit operator bool() const { return bool(memory); }
};
static void clear_reservations(SharedMemory &memory) {
    for (auto *context : memory.contexts)
        if (context) context->simulator.ClearLocalMonitor();
}
static void initialize_context(SatchelCPUProbe &probe, uint64_t entry) {
    if (entry & 3) throw std::runtime_error("Invalid entry alignment");
    probe.translate(entry, 4, SATCHEL_CPU_EXECUTE);
    probe.pc = entry;
    for (unsigned r = 0; r < 31; ++r) probe.simulator.WriteXRegister(r, 0);
    probe.simulator.WriteSp(uint64_t{0});
}

#if defined(SATCHEL_CPU_TESTING)
// Mac-only deterministic bad_alloc seam. Not public and absent from iOS.
static thread_local unsigned mapping_allocation_failure = 0;
static thread_local bool write_allocation_failure = false;
extern "C" void satchel_cpu_probe_test_mapping_allocation_failure(unsigned stage) {
    mapping_allocation_failure = stage;
}
extern "C" void satchel_cpu_probe_test_write_allocation_failure(int fail) {
    write_allocation_failure = fail != 0;
}
extern "C" uint64_t satchel_cpu_probe_test_ticks_from_ns(uint64_t ns) {
    return ticks_from_nanoseconds(ns);
}
extern "C" int satchel_cpu_probe_test_set_clock(SatchelCPUProbe *p, uint64_t elapsed_ns, int fail) {
    Access access(p); if (!access) return 0;
    p->memory->test_clock = true;
    p->memory->test_clock_elapsed_ns = elapsed_ns;
    p->memory->test_clock_failure = fail != 0;
    return 1;
}
extern "C" int satchel_cpu_probe_test_set_known_fpcr(SatchelCPUProbe *p, uint32_t value) {
    Access access(p);
    // Test only the supported DN bit, not arbitrary/reserved FPCR modes.
    if (!access || p->terminal || !p->fp_status_known || (value & ~UINT32_C(0x02000000))) return 0;
    p->simulator.ReadFpcr().SetRawValue(value); return 1;
}
// Mac-only owned-state fixtures: no host FP state and no production export.
extern "C" int satchel_cpu_probe_test_conversion_state(SatchelCPUProbe *p, unsigned mode) {
    Access access(p); if (!access || p->terminal || mode > 3) return 0;
    p->simulator.ReadFpcr().SetRawValue(mode == 2 ? UINT32_C(0x00400000) : UINT32_C(0x02000000));
    p->modeled_fpsr = mode == 3 ? UINT32_C(0x20) : UINT32_C(0x0800008f);
    p->fp_status_known = mode != 1;
    return 1;
}

#endif
static void mapping_allocation_point(unsigned stage) {
#if defined(SATCHEL_CPU_TESTING)
    if (mapping_allocation_failure == stage) {
        mapping_allocation_failure = 0;
        throw std::bad_alloc();
    }
#else
    (void)stage;
#endif
}
extern "C" void *satchel_vixl_translate(uint64_t address, size_t size, bool write) {
    if (!active_probe) throw Fault(SATCHEL_CPU_INTERNAL_FAULT, "No active memory context");
    return active_probe->translate(address, size, write ? SATCHEL_CPU_WRITE : SATCHEL_CPU_READ);
}

extern "C" SatchelCPUProbe *satchel_cpu_probe_create(const SatchelCPUProbeRegion *regions, size_t count,
                                           uint64_t entry, char *error, size_t error_size) {
    try {
        if (active_probe || !regions || count == 0 || count > 32 || (entry & 3))
            throw std::runtime_error("Invalid region count or entry alignment");
        auto probe = std::make_unique<SatchelCPUProbe>();
        size_t total = 0;
        for (size_t i = 0; i < count; ++i) {
            const auto &r = regions[i];
            if (r.address < 4096 || r.memory_size == 0 || r.bytes_size > r.memory_size ||
                (r.bytes_size && !r.bytes) || r.address > UINT64_MAX - r.memory_size ||
                r.permissions == 0 || (r.permissions & ~7u) ||
                ((r.permissions & SATCHEL_CPU_EXECUTE) &&
                 ((r.address & 3) || !(r.permissions & SATCHEL_CPU_READ) || (r.permissions & SATCHEL_CPU_WRITE))))
                throw std::runtime_error("Invalid segment bounds or permissions");
            if (r.memory_size > 256 * 1024 * 1024 - total)
                throw std::runtime_error("Probe mapping exceeds 256 MiB limit");
            total += r.memory_size;
            for (const auto &old : probe->memory->regions)
                if (r.address < old.address + old.bytes.size() && old.address < r.address + r.memory_size)
                    throw std::runtime_error("Overlapping guest segments");
            Region copy{r.address, r.permissions, std::vector<uint8_t>(r.memory_size)};
            copy.permission_mutable = r.permissions == (SATCHEL_CPU_READ | SATCHEL_CPU_WRITE);
            if (r.bytes_size) std::memcpy(copy.bytes.data(), r.bytes, r.bytes_size);
            probe->memory->regions.push_back(std::move(copy));
        }
        initialize_context(*probe, entry);
        probe->memory->contexts[0] = probe.get();
        copy_error(error, error_size, "");
        return probe.release();
    } catch (const std::exception &e) { copy_error(error, error_size, e.what()); }
    catch (...) { copy_error(error, error_size, "Unknown probe construction failure"); }
    return nullptr;
}
extern "C" SatchelCPUProbe *satchel_cpu_probe_create_shared(SatchelCPUProbe *peer, uint64_t entry,
        char *error, size_t error_size) {
    Access access(peer);
    if (!access) { copy_error(error, error_size, "Invalid or active shared context"); return nullptr; }
    if (peer->terminal) { copy_error(error, error_size, "Cannot create from a terminal context"); return nullptr; }
    auto &contexts = peer->memory->contexts;
    auto slot = std::find(contexts.begin(), contexts.end(), nullptr);
    if (slot == contexts.end()) { copy_error(error, error_size, "Shared context limit exceeded"); return nullptr; }
    try {
        auto probe = std::make_unique<SatchelCPUProbe>(peer->memory);
        initialize_context(*probe, entry);
        *slot = probe.get();
        copy_error(error, error_size, "");
        return probe.release();
    } catch (const std::exception &e) { copy_error(error, error_size, e.what()); }
    catch (...) { copy_error(error, error_size, "Shared context construction failed"); }
    return nullptr;
}
extern "C" int satchel_cpu_probe_try_destroy(SatchelCPUProbe *p) {
    if (!p) return 1;
    Access access(p);
    if (!access) return 0;
    for (auto &context : p->memory->contexts) if (context == p) context = nullptr;
    if (p->memory->last_context == p) p->memory->last_context = nullptr;
    delete p;
    return 1;
}
extern "C" void satchel_cpu_probe_destroy(SatchelCPUProbe *p) { (void)satchel_cpu_probe_try_destroy(p); }
extern "C" int satchel_cpu_probe_set_x(SatchelCPUProbe *p, unsigned r, uint64_t v) {
    Access access(p);
    if (!access || r >= 31) return 0;
    p->simulator.WriteXRegister(r, v); return 1;
}
extern "C" int satchel_cpu_probe_get_x(SatchelCPUProbe *p, unsigned r, uint64_t *v) {
    Access access(p);
    if (!access || r >= 31 || !v) return 0;
    *v = p->simulator.ReadXRegister(r); return 1;
}
extern "C" int satchel_cpu_probe_set_sp(SatchelCPUProbe *p, uint64_t v) {
    Access access(p);
    if (!access) return 0;
    p->simulator.WriteSp(v); return 1;
}
extern "C" int satchel_cpu_probe_set_tls(SatchelCPUProbe *p, uint64_t ro, uint64_t rw) {
    Access access(p);
    if (!access) return 0;
    p->tpidrro_el0 = ro; p->tpidr_el0 = rw; return 1;
}
extern "C" int satchel_cpu_probe_get_tls(SatchelCPUProbe *p, uint64_t *ro, uint64_t *rw) {
    Access access(p);
    if (!access || !ro || !rw) return 0;
    *ro = p->tpidrro_el0; *rw = p->tpidr_el0; return 1;
}
extern "C" int satchel_cpu_probe_get_system_tick(SatchelCPUProbe *p, uint64_t *ticks) {
    Access access(p); if (!access || !ticks) return 0;
    try { const uint64_t value = system_tick(*p->memory); *ticks = value; return 1; }
    catch (...) { return 0; }
}
extern "C" SatchelCPUFPResult satchel_cpu_probe_get_fp_status(SatchelCPUProbe *p, uint32_t *fpcr, uint32_t *fpsr) {
    if (!p || !fpcr || !fpsr || fpcr == fpsr) return SATCHEL_CPU_FP_INVALID;
    Access access(p); if (!access) return SATCHEL_CPU_FP_BUSY;
    if (p->terminal) return SATCHEL_CPU_FP_INVALID;
    if (!p->fp_status_known) return SATCHEL_CPU_FP_UNKNOWN;
    const uint32_t control = p->simulator.ReadFpcr().GetRawValue(), status = p->modeled_fpsr;
    *fpcr = control; *fpsr = status; return SATCHEL_CPU_FP_OK;
}
extern "C" SatchelCPUFPResult satchel_cpu_probe_copy_fp_status(SatchelCPUProbe *destination, SatchelCPUProbe *source) {
    if (!destination || !source || destination == source || destination->memory != source->memory)
        return SATCHEL_CPU_FP_INVALID;
    Access access(destination); if (!access) return SATCHEL_CPU_FP_BUSY;
    if (destination->terminal || source->terminal) return SATCHEL_CPU_FP_INVALID;
    if (!destination->fp_status_known || !source->fp_status_known) return SATCHEL_CPU_FP_UNKNOWN;
    const uint32_t control = source->simulator.ReadFpcr().GetRawValue(), status = source->modeled_fpsr;
    destination->simulator.ReadFpcr().SetRawValue(control);
    destination->modeled_fpsr = status;
    return SATCHEL_CPU_FP_OK;
}
extern "C" int satchel_cpu_probe_set_q(SatchelCPUProbe *p, unsigned r, const uint8_t value[16]) {
    Access access(p);
    if (!access || r >= 32 || !value) return 0;
    Simulator::qreg_t q; std::memcpy(q.val, value, 16);
    p->simulator.WriteQRegister(r, q); return 1;
}
extern "C" int satchel_cpu_probe_get_q(SatchelCPUProbe *p, unsigned r, uint8_t value[16]) {
    Access access(p);
    if (!access || r >= 32 || !value) return 0;
    auto q = p->simulator.ReadQRegister(r); std::memcpy(value, q.val, 16); return 1;
}
extern "C" int satchel_cpu_probe_read(SatchelCPUProbe *p, uint64_t address, void *bytes, size_t size) {
    Access access(p);
    if (!access || (!bytes && size)) return 0;
    try { if (size) std::memcpy(bytes, p->translate(address, size, SATCHEL_CPU_READ), size); return 1; }
    catch (...) { return 0; }
}
extern "C" int satchel_cpu_probe_check_range(SatchelCPUProbe *p, uint64_t address, uint64_t size,
                                         unsigned required_permissions) {
    Access access(p);
    if (!access || !size || address > UINT64_MAX - size ||
        !required_permissions || (required_permissions & ~7u)) return 0;
    try { p->translate(address, size, required_permissions); return 1; }
    catch (...) { return 0; }
}
extern "C" int satchel_cpu_probe_write(SatchelCPUProbe *p, uint64_t address, const void *bytes, size_t size) {
    Access access(p);
    if (!access || !bytes || size == 0) return 0;
    try {
        void *destination = p->translate(address, size, SATCHEL_CPU_WRITE);
        std::memcpy(destination, bytes, size);
        // A service/dispatcher write must not leave a stale reservation that
        // could overwrite its result. Conservative spurious failure is allowed.
        clear_reservations(*p->memory);
        return 1;
    } catch (...) { return 0; }
}
extern "C" int satchel_cpu_probe_physical_overlap(SatchelCPUProbe *p, uint64_t first, uint64_t first_size,
                                             uint64_t second, uint64_t second_size, int *overlap) {
    Access access(p);
    if (!access || !overlap || !first_size || !second_size ||
        first > UINT64_MAX - first_size || second > UINT64_MAX - second_size) return 0;
    struct Span { const std::vector<uint8_t> *backing; uint64_t offset, size; };
    std::array<Span,32> a{}, b{}; size_t a_count=0,b_count=0;
    auto collect=[&](uint64_t start,uint64_t size,std::array<Span,32>&spans,size_t&count) {
        const uint64_t end=start+size;uint64_t cursor=start;
        while(cursor<end) {
            const Region *r=region_at(p->memory->regions,cursor);
            if(!r || count==spans.size())return false;
            const uint64_t length=std::min(end-cursor,r->bytes.size()-(cursor-r->address));
            spans[count++]={r->bytes.backing.get(),r->bytes.offset+(cursor-r->address),length};
            cursor+=length;
        }
        return true;
    };
    if(!collect(first,first_size,a,a_count) || !collect(second,second_size,b,b_count))return 0;
    int result=0;
    for(size_t i=0;i<a_count;i++)for(size_t j=0;j<b_count;j++)
        if(a[i].backing==b[j].backing && a[i].offset<b[j].offset+b[j].size && b[j].offset<a[i].offset+a[i].size)
            result=1;
    *overlap=result;return 1;
}

// Caller holds the process Access guard. Temporary snapshots make overlapping
// source/destination aliasing safe without exposing a host guest-memory pointer.
static SatchelCPUWriteResult writev_locked(SatchelCPUProbe *p, const SatchelCPUProbeWrite *writes, size_t count) {
    if (!writes || count == 0) return SATCHEL_CPU_WRITE_INVALID;
    if (count > SATCHEL_CPU_WRITE_SPAN_LIMIT) return SATCHEL_CPU_WRITE_LIMIT;
    std::array<SatchelCPUProbeWrite, SATCHEL_CPU_WRITE_SPAN_LIMIT> spans{};
    std::memcpy(spans.data(), writes, count * sizeof(*writes));
    std::array<void *, SATCHEL_CPU_WRITE_SPAN_LIMIT> destinations{};
    std::array<size_t, SATCHEL_CPU_WRITE_SPAN_LIMIT> offsets{};
    size_t total = 0;
    try {
        for (size_t i = 0; i < count; ++i) {
            const auto &span = spans[i];
            if (!span.bytes || span.size == 0 || span.address > UINT64_MAX - span.size)
                return SATCHEL_CPU_WRITE_INVALID;
            if (span.size > SATCHEL_CPU_WRITE_BYTE_LIMIT - total) return SATCHEL_CPU_WRITE_LIMIT;
            for (size_t j = 0; j < i; ++j)
                if (span.address < spans[j].address + spans[j].size &&
                    spans[j].address < span.address + span.size) return SATCHEL_CPU_WRITE_INVALID;
            destinations[i] = p->translate(span.address, span.size, SATCHEL_CPU_WRITE);
            // Retained guest aliases can name the same physical bytes through
            // different virtual addresses. Reject that overlap as well.
            const uintptr_t physical = reinterpret_cast<uintptr_t>(destinations[i]);
            for (size_t j = 0; j < i; ++j) {
                const uintptr_t prior = reinterpret_cast<uintptr_t>(destinations[j]);
                if (physical < prior + spans[j].size && prior < physical + span.size)
                    return SATCHEL_CPU_WRITE_INVALID;
            }
            offsets[i] = total; total += span.size;
        }
#if defined(SATCHEL_CPU_TESTING)
        if (write_allocation_failure) { write_allocation_failure = false; throw std::bad_alloc(); }
#endif
        std::vector<uint8_t> snapshot(total);
        for (size_t i = 0; i < count; ++i)
            std::memcpy(snapshot.data() + offsets[i], spans[i].bytes, spans[i].size);
        // The map cannot change under this guard. All remaining operations are
        // nonthrowing; no second-span failure can follow the first mutation.
        for (size_t i = 0; i < count; ++i)
            std::memcpy(destinations[i], snapshot.data() + offsets[i], spans[i].size);
        clear_reservations(*p->memory);
        return SATCHEL_CPU_WRITE_OK;
    } catch (const Fault &) { return SATCHEL_CPU_WRITE_MEMORY; }
    catch (const std::bad_alloc &) { return SATCHEL_CPU_WRITE_ALLOCATION; }
    catch (...) { return SATCHEL_CPU_WRITE_INTERNAL; }
}
extern "C" SatchelCPUWriteResult satchel_cpu_probe_writev(SatchelCPUProbe *p,
        const SatchelCPUProbeWrite *writes, size_t count) {
    if (!p) return SATCHEL_CPU_WRITE_INVALID;
    Access access(p);
    if (!access) return SATCHEL_CPU_WRITE_BUSY;
    return writev_locked(p, writes, count);
}
#if defined(SATCHEL_CPU_TESTING)
// Original-fixture alias test seam; no native guest-memory pointer escapes this
// private helper, and neither this helper nor the failure seam exists on iOS.
extern "C" SatchelCPUWriteResult satchel_cpu_probe_test_swap(SatchelCPUProbe *p,
        uint64_t first, uint64_t second, size_t size) {
    Access access(p);
    if (!access) return SATCHEL_CPU_WRITE_BUSY;
    try {
        const SatchelCPUProbeWrite writes[] = {
            {first, p->translate(second, size, SATCHEL_CPU_READ), size},
            {second, p->translate(first, size, SATCHEL_CPU_READ), size}};
        return writev_locked(p, writes, 2);
    } catch (...) { return SATCHEL_CPU_WRITE_MEMORY; }
}
extern "C" int satchel_cpu_probe_test_same_backing(SatchelCPUProbe *p, uint64_t first, uint64_t second) {
    Access access(p); if (!access) return 0;
    const auto *a = region_at(p->memory->regions, first), *b = region_at(p->memory->regions, second);
    return a && b && a->bytes.backing == b->bytes.backing &&
        a->bytes.offset + (first - a->address) == b->bytes.offset + (second - b->address);
}
extern "C" uint64_t satchel_cpu_probe_test_stack_pointer(SatchelCPUProbe *p) {
    Access access(p); return access ? p->simulator.ReadXRegister(31, Reg31IsStackPointer) : 0;
}
extern "C" uint64_t satchel_cpu_probe_test_physical_bytes(SatchelCPUProbe *p) {
    Access access(p); return access ? physical_bytes(*p->memory) : 0;
}
#endif

static SharedReadOnly *readonly_page(SharedMemory &memory, uint64_t token) {
    for (auto &page : memory.readonly_pages)
        if (token && page.token == token) return &page;
    return nullptr;
}
extern "C" SatchelCPUMapResult satchel_cpu_probe_shared_readonly_create(SatchelCPUProbe *p,
        const void *bytes, size_t size, uint64_t *out) {
    if (active_probe) return SATCHEL_CPU_MAP_BUSY;
    if (!p || !bytes || !out || size != SATCHEL_CPU_SHARED_READONLY_SIZE) return SATCHEL_CPU_MAP_INVALID;
    Access access(p); if (!access) return SATCHEL_CPU_MAP_BUSY;
    if (size > 256 * 1024 * 1024 - physical_bytes(*p->memory)) return SATCHEL_CPU_MAP_LIMIT;
    SharedReadOnly *slot = nullptr;
    for (auto &page : p->memory->readonly_pages)
        if (page.storage.expired()) { slot = &page; break; }
    if (!slot) return SATCHEL_CPU_MAP_LIMIT;
    try {
        mapping_allocation_point(1);
        std::vector<uint8_t> snapshot(size);
        std::memcpy(snapshot.data(), bytes, size);
        mapping_allocation_point(2);
        auto backing = std::make_shared<std::vector<uint8_t>>(std::move(snapshot));
        uint64_t token = next_shared_readonly_token.load();
        do { if (token == UINT64_MAX) return SATCHEL_CPU_MAP_LIMIT; }
        while (!next_shared_readonly_token.compare_exchange_weak(token, token + 1));
        // Shared/weak assignments and token publication cannot allocate/fail.
        slot->owned = std::move(backing); slot->storage = slot->owned; slot->token = token;
        *out = token; return SATCHEL_CPU_MAP_OK;
    } catch (const std::bad_alloc &) { return SATCHEL_CPU_MAP_ALLOCATION; }
    catch (...) { return SATCHEL_CPU_MAP_ALLOCATION; }
}
extern "C" SatchelCPUMapResult satchel_cpu_probe_shared_readonly_map(SatchelCPUProbe *p,
        uint64_t token, uint64_t base, uint64_t size) {
    if (active_probe) return SATCHEL_CPU_MAP_BUSY;
    if (!p) return SATCHEL_CPU_MAP_INVALID;
    Access access(p); if (!access) return SATCHEL_CPU_MAP_BUSY;
    if (size != SATCHEL_CPU_SHARED_READONLY_SIZE || base < 4096 || (base & 4095) || base > UINT64_MAX-size)
        return SATCHEL_CPU_MAP_INVALID;
    auto *page = readonly_page(*p->memory, token);
    if (!page || !page->owned) return SATCHEL_CPU_MAP_INVALID;
    for (const auto &region : p->memory->regions)
        if (base < region.address + region.bytes.size() && region.address < base + size)
            return SATCHEL_CPU_MAP_INVALID;
    if (p->memory->regions.size() >= 32) return SATCHEL_CPU_MAP_LIMIT;
    try {
        mapping_allocation_point(1);
        Region mapped{}; mapped.address = base; mapped.permissions = SATCHEL_CPU_READ;
        mapped.bytes.backing = page->owned; mapped.bytes.length = size; mapped.shared_readonly = token;
        mapping_allocation_point(2);
        p->memory->regions.push_back(std::move(mapped));
        clear_reservations(*p->memory); return SATCHEL_CPU_MAP_OK;
    } catch (const std::bad_alloc &) { return SATCHEL_CPU_MAP_ALLOCATION; }
    catch (...) { return SATCHEL_CPU_MAP_ALLOCATION; }
}
extern "C" SatchelCPUMapResult satchel_cpu_probe_shared_readonly_unmap(SatchelCPUProbe *p,
        uint64_t token, uint64_t base, uint64_t size) {
    if (active_probe) return SATCHEL_CPU_MAP_BUSY;
    if (!p) return SATCHEL_CPU_MAP_INVALID;
    Access access(p); if (!access) return SATCHEL_CPU_MAP_BUSY;
    if (size != SATCHEL_CPU_SHARED_READONLY_SIZE || base < 4096 || (base & 4095) || base > UINT64_MAX-size)
        return SATCHEL_CPU_MAP_INVALID;
    auto *page = readonly_page(*p->memory, token);
    if (!page) return SATCHEL_CPU_MAP_INVALID;
    auto backing = page->storage.lock(); if (!backing) return SATCHEL_CPU_MAP_INVALID;
    for (auto i = p->memory->regions.begin(); i != p->memory->regions.end(); ++i)
        if (i->address == base && i->shared_readonly == token && i->bytes.backing == backing &&
            i->bytes.offset == 0 && i->bytes.size() == size && i->permissions == SATCHEL_CPU_READ) {
            p->memory->regions.erase(i); clear_reservations(*p->memory); return SATCHEL_CPU_MAP_OK;
        }
    return SATCHEL_CPU_MAP_INVALID;
}
extern "C" SatchelCPUMapResult satchel_cpu_probe_shared_readonly_release(SatchelCPUProbe *p, uint64_t token) {
    if (active_probe) return SATCHEL_CPU_MAP_BUSY;
    if (!p) return SATCHEL_CPU_MAP_INVALID;
    Access access(p); if (!access) return SATCHEL_CPU_MAP_BUSY;
    auto *page = readonly_page(*p->memory, token);
    if (!page || !page->owned) return SATCHEL_CPU_MAP_INVALID;
    page->owned.reset(); return SATCHEL_CPU_MAP_OK;
}

extern "C" SatchelCPUMapResult satchel_cpu_probe_resize_rw(SatchelCPUProbe *p, uint64_t base, uint64_t size) {
    if (active_probe) return SATCHEL_CPU_MAP_BUSY;
    if (!p) return SATCHEL_CPU_MAP_INVALID;
    Access access(p);
    if (!access) return SATCHEL_CPU_MAP_BUSY;
    if (base < 4096 || (base & 4095) || (size & 4095) || base > UINT64_MAX - size)
        return SATCHEL_CPU_MAP_INVALID;
    size_t existing = p->memory->regions.size();
    uint64_t total = physical_bytes(*p->memory);
    for (size_t i = 0; i < p->memory->regions.size(); ++i) {
        const auto &r = p->memory->regions[i];
        if (r.address == base) existing = i;
    }
    const bool replacing = existing < p->memory->regions.size();
    if (replacing && (!p->memory->regions[existing].dynamic ||
                      p->memory->regions[existing].permissions != (SATCHEL_CPU_READ | SATCHEL_CPU_WRITE) ||
                      backing_is_aliased(p->memory->regions, p->memory->regions[existing]) ||
                      backing_is_fragmented(p->memory->regions, p->memory->regions[existing])))
        return SATCHEL_CPU_MAP_INVALID;
    if (!size) {
        if (!replacing) return SATCHEL_CPU_MAP_INVALID;
        // Region/vector moves are noexcept; removal allocates nothing.
        p->memory->regions.erase(p->memory->regions.begin() + existing);
        clear_reservations(*p->memory);
        return SATCHEL_CPU_MAP_OK;
    }
    if (replacing) total -= p->memory->regions[existing].bytes.size();
    if (size > 256 * 1024 * 1024 - total || (!replacing && p->memory->regions.size() >= 32))
        return SATCHEL_CPU_MAP_LIMIT;
    for (size_t i = 0; i < p->memory->regions.size(); ++i) {
        if (i == existing) continue;
        const auto &r = p->memory->regions[i];
        if (base < r.address + r.bytes.size() && r.address < base + size) return SATCHEL_CPU_MAP_INVALID;
    }
    if (replacing && size == p->memory->regions[existing].bytes.size()) return SATCHEL_CPU_MAP_OK;
    try {
        mapping_allocation_point(1);
        Region replacement{base, SATCHEL_CPU_READ | SATCHEL_CPU_WRITE, std::vector<uint8_t>(size), true};
        replacement.permission_mutable = true;
        if (replacing) {
            const auto &old = p->memory->regions[existing].bytes;
            std::memcpy(replacement.bytes.data(), old.data(), std::min(old.size(), replacement.bytes.size()));
            p->memory->regions[existing].bytes.swap(replacement.bytes);
        } else {
            mapping_allocation_point(2);
            p->memory->regions.push_back(std::move(replacement));
        }
        clear_reservations(*p->memory);
        return SATCHEL_CPU_MAP_OK;
    } catch (const std::bad_alloc &) { return SATCHEL_CPU_MAP_ALLOCATION; }
    catch (...) { return SATCHEL_CPU_MAP_ALLOCATION; }
}

// Plan with fixed descriptors; retain untouched vectors by noexcept move only
// after every new fragment/gap allocation succeeds. No guest pointer escapes.
static SatchelCPUMapResult change_rw_range(SatchelCPUProbe *p, uint64_t base, uint64_t size,
                                      uint64_t *changed_bytes, bool map) {
    if (active_probe) return SATCHEL_CPU_MAP_BUSY;
    if (!p || !changed_bytes) return SATCHEL_CPU_MAP_INVALID;
    Access access(p);
    if (!access) return SATCHEL_CPU_MAP_BUSY;
    if (base < 4096 || !size || (base & 4095) || (size & 4095) || base > UINT64_MAX-size)
        return SATCHEL_CPU_MAP_INVALID;
    const uint64_t end=base+size;
    auto &regions=p->memory->regions;
    uint64_t total=physical_bytes(*p->memory), overlap=0;
    for(const auto&r:regions) {
        const uint64_t r_end=r.address+r.bytes.size();
        if(base<r_end && r.address<end) {
            if(!r.dynamic || r.permissions!=(SATCHEL_CPU_READ|SATCHEL_CPU_WRITE) || backing_is_aliased(regions,r) || backing_is_fragmented(regions,r))
                return SATCHEL_CPU_MAP_INVALID;
            overlap+=std::min(end,r_end)-std::max(base,r.address);
        }
    }
    const uint64_t changed=map?size-overlap:overlap;
    if(!changed) { *changed_bytes=0; return SATCHEL_CPU_MAP_OK; }
    if(map && changed>256*1024*1024-total) return SATCHEL_CPU_MAP_LIMIT;
    struct Plan { uint64_t address; size_t size, old_index, offset; bool retain; };
    std::array<Plan,32> plan{};size_t count=0;
    auto append=[&](uint64_t address,size_t bytes,size_t old_index,size_t offset,bool retain) {
        if(count==plan.size()) return false;
        plan[count++]={address,bytes,old_index,offset,retain};return true;
    };
    uint64_t merged_base=base, merged_end=end;
    if(map) for(const auto&r:regions) {
        const uint64_t r_end=r.address+r.bytes.size();
        if(base<r_end && r.address<end) {
            merged_base=std::min(merged_base,r.address);
            merged_end=std::max(merged_end,r_end);
        }
    }
    for(size_t i=0;i<regions.size();i++) {
        const auto&r=regions[i];const uint64_t r_end=r.address+r.bytes.size();
        if(base>=r_end || r.address>=end) {
            if(!append(r.address,r.bytes.size(),i,0,true)) return SATCHEL_CPU_MAP_LIMIT;
        } else if(!map) {
            if(r.address<base && !append(r.address,size_t(base-r.address),i,0,false)) return SATCHEL_CPU_MAP_LIMIT;
            if(r_end>end && !append(end,size_t(r_end-end),i,size_t(end-r.address),false)) return SATCHEL_CPU_MAP_LIMIT;
        }
    }
    if(map && !append(merged_base,size_t(merged_end-merged_base),regions.size(),0,false)) return SATCHEL_CPU_MAP_LIMIT;
    try {
        mapping_allocation_point(1);
        std::vector<Region> replacement(count);
        for(size_t i=0;i<count;i++) {
            const auto&item=plan[i];if(item.retain)continue;
            auto&r=replacement[i];r.address=item.address;r.permissions=SATCHEL_CPU_READ|SATCHEL_CPU_WRITE;r.dynamic=true;r.permission_mutable=true;
            r.bytes.resize(item.size); // New gaps zeroed; surviving fragments copied.
            if(item.old_index<regions.size()) {
                std::memcpy(r.bytes.data(),regions[item.old_index].bytes.data()+item.offset,item.size);
            } else if(map) {
                for(const auto&old:regions) if(base<old.address+old.bytes.size() && old.address<end)
                    std::memcpy(r.bytes.data()+size_t(old.address-merged_base),old.bytes.data(),old.bytes.size());
            }
        }
        mapping_allocation_point(2);
        static_assert(std::is_nothrow_move_assignable<Region>::value,"Atomic map commit requires noexcept region moves");
        for(size_t i=0;i<count;i++)if(plan[i].retain)replacement[i]=std::move(regions[plan[i].old_index]);
        regions.swap(replacement);
        clear_reservations(*p->memory);
        *changed_bytes=changed;
        return SATCHEL_CPU_MAP_OK;
    } catch(...) { return SATCHEL_CPU_MAP_ALLOCATION; }
}
extern "C" SatchelCPUMapResult satchel_cpu_probe_map_missing_rw(SatchelCPUProbe *p,uint64_t base,uint64_t size,uint64_t *mapped_bytes) {
    return change_rw_range(p,base,size,mapped_bytes,true);
}
extern "C" SatchelCPUMapResult satchel_cpu_probe_unmap_rw(SatchelCPUProbe *p,uint64_t base,uint64_t size,uint64_t *unmapped_bytes) {
    return change_rw_range(p,base,size,unmapped_bytes,false);
}

static Region slice_region(const Region &region, uint64_t start, uint64_t end) {
    Region result = region;
    const size_t offset = size_t(start - region.address);
    result.address = start;
    result.bytes.offset += offset;
    result.bytes.length = size_t(end - start);
    if (result.alias_source) result.alias_source += offset;
    return result;
}
static void coalesce_views(std::vector<Region> &regions) {
    std::sort(regions.begin(), regions.end(), [](const Region &a, const Region &b) { return a.address < b.address; });
    size_t count = 0;
    for (size_t i = 0; i < regions.size(); ++i) {
        auto &r = regions[i];
        if (count) {
            auto &old = regions[count - 1];
            const bool alias_contiguous = (!old.alias_source && !r.alias_source) ||
                (old.alias_source && r.alias_source == old.alias_source + old.bytes.size());
            if (old.address + old.bytes.size() == r.address && old.permissions == r.permissions &&
                old.dynamic == r.dynamic && old.alias_locked == r.alias_locked && alias_contiguous &&
                old.shared_readonly == r.shared_readonly && old.permission_mutable == r.permission_mutable &&
                old.bytes.backing == r.bytes.backing && old.bytes.offset + old.bytes.size() == r.bytes.offset) {
                old.bytes.length += r.bytes.length;
                continue;
            }
        }
        if (count != i) regions[count] = std::move(r);
        ++count;
    }
    regions.resize(count);
}
extern "C" SatchelCPUMapResult satchel_cpu_probe_set_permissions(SatchelCPUProbe *p, uint64_t base,
        uint64_t size, unsigned expected, unsigned permissions) {
    if (active_probe) return SATCHEL_CPU_MAP_BUSY;
    if (!p) return SATCHEL_CPU_MAP_INVALID;
    Access access(p); if (!access) return SATCHEL_CPU_MAP_BUSY;
    auto valid = [](unsigned value) { return value == 0 || value == SATCHEL_CPU_READ ||
                                            value == (SATCHEL_CPU_READ | SATCHEL_CPU_WRITE); };
    if (!valid(expected) || !valid(permissions) || !size || base < 4096 ||
        ((base | size) & 4095) || base > UINT64_MAX - size) return SATCHEL_CPU_MAP_INVALID;
    const uint64_t end = base + size;
    auto &regions = p->memory->regions;
    for (uint64_t cursor = base; cursor < end;) {
        const Region *r = region_at(regions, cursor);
        if (!r || r->permissions != expected || !r->permission_mutable ||
            r->alias_locked || r->alias_source || r->shared_readonly || (r->permissions & SATCHEL_CPU_EXECUTE))
            return SATCHEL_CPU_MAP_INVALID;
        cursor = std::min(end, r->address + r->bytes.size());
    }
    if (expected == permissions) return SATCHEL_CPU_MAP_OK;
    try {
        mapping_allocation_point(1);
        std::vector<Region> replacement;
        replacement.reserve(96); //32 original views, at most two boundary cuts.
        for (const auto &r : regions) {
            const uint64_t r_end = r.address + r.bytes.size();
            if (base >= r_end || r.address >= end) { replacement.push_back(r); continue; }
            const uint64_t first = std::max(base, r.address), last = std::min(end, r_end);
            if (r.address < first) replacement.push_back(slice_region(r, r.address, first));
            Region middle = slice_region(r, first, last); middle.permissions = permissions;
            replacement.push_back(std::move(middle));
            if (last < r_end) replacement.push_back(slice_region(r, last, r_end));
        }
        coalesce_views(replacement);
        if (replacement.size() > 32) return SATCHEL_CPU_MAP_LIMIT;
        mapping_allocation_point(2);
        regions.swap(replacement);
        clear_reservations(*p->memory);
        return SATCHEL_CPU_MAP_OK;
    } catch (...) { return SATCHEL_CPU_MAP_ALLOCATION; }
}
static SatchelCPUMapResult change_alias(SatchelCPUProbe *p, uint64_t destination,
                                   uint64_t source, uint64_t size, bool map) {
    if (active_probe) return SATCHEL_CPU_MAP_BUSY;
    if (!p) return SATCHEL_CPU_MAP_INVALID;
    Access access(p); if (!access) return SATCHEL_CPU_MAP_BUSY;
    if (!size || source < 4096 || destination < 4096 ||
        ((source | destination | size) & 4095) ||
        source > UINT64_MAX - size || destination > UINT64_MAX - size)
        return SATCHEL_CPU_MAP_INVALID;
    const uint64_t source_end = source + size, destination_end = destination + size;
    if (source < destination_end && destination < source_end) return SATCHEL_CPU_MAP_INVALID;
    auto &regions = p->memory->regions;
    if (map) for (const auto &r : regions)
        if (destination < r.address + r.bytes.size() && r.address < destination_end)
            return SATCHEL_CPU_MAP_INVALID;
    uint64_t cursor = source;
    while (cursor < source_end) {
        const Region *s = region_at(regions, cursor);
        if (!s || s->alias_source) return SATCHEL_CPU_MAP_INVALID;
        uint64_t next = std::min(source_end, s->address + s->bytes.size());
        if (map) {
            if (s->permissions != (SATCHEL_CPU_READ | SATCHEL_CPU_WRITE) || s->alias_locked)
                return SATCHEL_CPU_MAP_INVALID;
        } else {
            const uint64_t target = destination + (cursor - source);
            const Region *d = region_at(regions, target);
            if (!s->alias_locked || s->permissions || !d || !d->alias_source || d->alias_locked ||
                d->permissions != (SATCHEL_CPU_READ | SATCHEL_CPU_WRITE) ||
                d->alias_source + (target - d->address) != cursor ||
                d->bytes.backing != s->bytes.backing ||
                d->bytes.offset + (target - d->address) != s->bytes.offset + (cursor - s->address))
                return SATCHEL_CPU_MAP_INVALID;
            next = std::min(next, cursor + std::min(source_end - cursor,
                                                   d->bytes.size() - (target - d->address)));
        }
        cursor = next;
    }
    try {
        mapping_allocation_point(1);
        // At most two source boundary cuts plus one alias per original view;
        // unmap has only four cuts. Never allocate backing bytes here.
        std::vector<Region> replacement;
        replacement.reserve(96);
        for (const auto &r : regions) {
            const uint64_t end = r.address + r.bytes.size();
            if (source < end && r.address < source_end) {
                const uint64_t first = std::max(source, r.address), last = std::min(source_end, end);
                if (r.address < first) replacement.push_back(slice_region(r, r.address, first));
                Region middle = slice_region(r, first, last);
                middle.permissions = map ? 0 : (SATCHEL_CPU_READ | SATCHEL_CPU_WRITE);
                middle.alias_locked = map;
                replacement.push_back(middle);
                if (map) {
                    middle.address = destination + (first - source);
                    middle.permissions = SATCHEL_CPU_READ | SATCHEL_CPU_WRITE;
                    middle.dynamic = false; middle.alias_locked = false; middle.alias_source = first;
                    replacement.push_back(std::move(middle));
                }
                if (last < end) replacement.push_back(slice_region(r, last, end));
            } else if (!map && destination < end && r.address < destination_end) {
                if (r.address < destination) replacement.push_back(slice_region(r, r.address, destination));
                if (destination_end < end) replacement.push_back(slice_region(r, destination_end, end));
            } else replacement.push_back(r);
        }
        coalesce_views(replacement);
        if (replacement.size() > 32) return SATCHEL_CPU_MAP_LIMIT;
        mapping_allocation_point(2);
        regions.swap(replacement);
        clear_reservations(*p->memory);
        return SATCHEL_CPU_MAP_OK;
    } catch (...) { return SATCHEL_CPU_MAP_ALLOCATION; }
}
extern "C" SatchelCPUMapResult satchel_cpu_probe_map_alias(SatchelCPUProbe *p, uint64_t destination,
                                                uint64_t source, uint64_t size) {
    return change_alias(p, destination, source, size, true);
}
extern "C" SatchelCPUMapResult satchel_cpu_probe_unmap_alias(SatchelCPUProbe *p, uint64_t destination,
                                                  uint64_t source, uint64_t size) {
    return change_alias(p, destination, source, size, false);
}

extern "C" int satchel_cpu_probe_set_breakpoints(SatchelCPUProbe *p, const uint64_t *addresses, size_t count) {
    Access access(p);
    if (!access || p->terminal || count > p->memory->breakpoints.size() || (count && !addresses)) return 0;
    try {
        std::array<uint64_t, 32> checked{};
        for (size_t i = 0; i < count; ++i) {
            if (addresses[i] & 3) return 0;
            p->translate(addresses[i], 4, SATCHEL_CPU_EXECUTE);
            checked[i] = addresses[i];
        }
        std::sort(checked.begin(), checked.begin() + count);
        if (std::adjacent_find(checked.begin(), checked.begin() + count) != checked.begin() + count) return 0;
        p->memory->breakpoints = checked; p->memory->breakpoint_count = count;
        return 1;
    } catch (...) { return 0; }
}
extern "C" int satchel_cpu_probe_set_pc(SatchelCPUProbe *p, uint64_t address) {
    Access access(p);
    if (!access || p->terminal || (address & 3)) return 0;
    try {
        p->translate(address, 4, SATCHEL_CPU_EXECUTE);
        p->pc = address;
        return 1;
    } catch (...) { return 0; }
}

extern "C" SatchelCPUProbeResult satchel_cpu_probe_run(SatchelCPUProbe *p, uint64_t budget,
                                             SatchelCPUProbeCancelled cancelled, void *context) {
    SatchelCPUProbeResult result{};
    result.reason = SATCHEL_CPU_INTERNAL_FAULT;
    if (!p || active_probe) { copy_error(result.detail, sizeof(result.detail), "Invalid or reentrant context"); return result; }
    Access access(p);
    if (!access) { copy_error(result.detail, sizeof(result.detail), "Shared process is active"); return result; }
    if (p->terminal) return p->last;
    // One serialized virtual core: changing guest threads clears reservations.
    // A zero-budget inspection does not schedule a different thread.
    if (budget && p->memory->last_context != p) {
        clear_reservations(*p->memory);
        p->memory->last_context = p;
    }
    struct Active {
        Active(SatchelCPUProbe *p) { active_probe = p; }
        ~Active() { active_probe = nullptr; }
    } active(p);
    try {
        result.reason = SATCHEL_CPU_BUDGET;
        while (result.steps < budget) {
            result.pc = p->pc;
            if (cancelled && cancelled(context)) { result.reason = SATCHEL_CPU_CANCELLED; break; }
            if (p->pc & 3) throw Fault(SATCHEL_CPU_MEMORY_FAULT, "Misaligned guest PC");
            auto code = static_cast<const Instruction *>(p->translate(p->pc, 4, SATCHEL_CPU_EXECUTE));
            if (p->memory->breakpoint_count && std::binary_search(p->memory->breakpoints.begin(),
                    p->memory->breakpoints.begin() + p->memory->breakpoint_count, p->pc)) {
                result.reason = SATCHEL_CPU_BREAKPOINT; result.instruction = 0;
                copy_error(result.detail, sizeof(result.detail), "Guest execution breakpoint");
                break;
            }
            uint32_t op; std::memcpy(&op, code, 4); result.instruction = op;
            uint64_t next = p->pc + 4;
            if ((op & 0xffe0001f) == 0xd4000001) { // SVC returns control; no HLE side effects.
                result.svc = (op >> 5) & 0xffff;
                result.reason = SATCHEL_CPU_SVC; ++result.steps; p->pc = next; break;
            } else if ((op & 0x1f000000) == 0x10000000) { // ADR/ADRP use guest PC, never host PC.
                int64_t delta = sign_extend(((op >> 5) & 0x7ffff) * 4 + ((op >> 29) & 3), 21);
                uint64_t value = (op >> 31) ? (p->pc & ~uint64_t{4095}) + uint64_t(delta) * 4096
                                                  : p->pc + uint64_t(delta);
                p->simulator.WriteXRegister(op & 31, value);
            } else if ((op & 0x7c000000) == 0x14000000) { // B/BL.
                if (op >> 31) p->simulator.WriteXRegister(30, next);
                next = p->pc + uint64_t(sign_extend(op & 0x3ffffff, 26)) * 4;
            } else if ((op & 0xff000010) == 0x54000000) { // B.cond.
                if (condition_holds(op & 15, p->simulator.ReadNzcv().GetRawValue()))
                    next = p->pc + uint64_t(sign_extend((op >> 5) & 0x7ffff, 19)) * 4;
            } else if ((op & 0x7e000000) == 0x34000000) { // CBZ/CBNZ.
                uint64_t v = p->simulator.ReadXRegister(op & 31);
                if (!(op >> 31)) v = uint32_t(v);
                if ((v != 0) == bool(op & 0x01000000))
                    next = p->pc + uint64_t(sign_extend((op >> 5) & 0x7ffff, 19)) * 4;
            } else if ((op & 0x7e000000) == 0x36000000) { // TBZ/TBNZ; bit31 is bit-position[5].
                const unsigned bit = ((op >> 19) & 31) | ((op >> 26) & 32);
                const bool set = (uint64_t(p->simulator.ReadXRegister(op & 31)) >> bit) & 1;
                if (set == bool(op & 0x01000000))
                    next = p->pc + uint64_t(sign_extend((op >> 5) & 0x3fff, 14)) * 4;
            } else if ((op & 0xfffffc1f) == 0xd61f0000 ||
                       (op & 0xfffffc1f) == 0xd63f0000 ||
                       (op & 0xfffffc1f) == 0xd65f0000) { // BR/BLR/RET, never native callbacks.
                uint64_t target = p->simulator.ReadXRegister((op >> 5) & 31);
                if ((op & 0xfffffc1f) == 0xd63f0000) p->simulator.WriteXRegister(30, next);
                next = target;
            } else if ((op & 0xffffffe0) == 0xd53bd040 || (op & 0xffffffe0) == 0xd53bd060) {
                // Exact MRS TPIDR_EL0/TPIDRRO_EL0. These context values never
                // enter VIXL's system visitor or access the host thread state.
                const uint64_t value = (op & 0x20) ? p->tpidrro_el0 : p->tpidr_el0;
                p->simulator.WriteXRegister(op & 31, value);
            } else if ((op & 0xffffffe0) == 0xd51bd040) { // Only TPIDR_EL0 is writable at guest EL0.
                p->tpidr_el0 = p->simulator.ReadXRegister(op & 31);
            } else if ((op & 0xffffffe0) == 0xd53be000 ||
                       (op & 0xffffffe0) == 0xd53be020 ||
                       (op & 0xffffffe0) == 0xd53be040) {
                // Exact CNTFRQ_EL0 / CNTPCT_EL0 / CNTVCT_EL0 reads. A single
                // serialized process counter serves every context. The virtual
                // offset is zero; no writable timer/control registers are added.
                const uint64_t value = (op & 0xffffffe0) == 0xd53be000
                    ? SATCHEL_CPU_SYSTEM_TICK_FREQUENCY : system_tick(*p->memory);
                p->simulator.WriteXRegister(op & 31, value);
            } else if ((op & 0xfffff0ff) == 0xd503305f) { // Exact CLREX; CRm is architecturally ignored.
                // Clear only this context's simulated local monitor. Do not
                // touch host exclusives, another process/context, or memory.
                p->simulator.ClearLocalMonitor();
            } else if ((op & 0xfffff0ff) == 0xd50330bf) { // Exact DMB, all CRm options.
                // All guest mappings are ordinary synchronous RAM. Conservatively
                // order reads/writes with a real host fence for every domain/type.
                // Arm specifies full-system fallback for reserved options0/4/8/12.
                // This neither clears reservations nor admits device MMIO.
                std::atomic_thread_fence(std::memory_order_seq_cst);
            } else if ((op & 0xfffff0ff) == 0xd503309f && ((op >> 8) & 3) != 0) {
                // Exact named legacy DSB LD/ST/full options in every domain.
                // CPU memory accesses are synchronous checked ordinary RAM. A
                // fixed compiled host full-system completion barrier is stronger
                // than each admitted domain/type; no guest encoding executes.
                // This does NOT complete GPU jobs, flush device buffers, admit
                // MMIO/cache maintenance, or clear any guest reservation.
                // SSBB/PSSBB, reserved options and nXS forms remain rejected.
#if defined(__aarch64__)
                __asm__ __volatile__("dsb sy" ::: "memory");
#else
#error Satchel CPU DSB requires the reviewed ARM64 host completion barrier
#endif
            } else if (op == UINT32_C(0xd5033fdf)) { // Exact ISB SY only.
                // The serialized interpreter completes each instruction before
                // a fresh checked fetch; it keeps no guest pipeline or decoded
                // instruction cache. A fixed compiled host ISB also synchronizes
                // the host instruction stream. No guest encoding executes here.
                // This is not data/GPU completion, cache maintenance, privileged
                // context support, or exclusive-monitor invalidation.
#if defined(__aarch64__)
                __asm__ __volatile__("isb" ::: "memory");
#else
#error Satchel CPU ISB requires the reviewed ARM64 host instruction barrier
#endif
            } else if (is_prefetch_hint(op)) {
                // Explicit implementation-defined no-cache-action policy.
                // A hint neither reads/writes guest memory nor checks SP data
                // alignment, and never forms/dereferences a host address. Keep
                // local reservations and all architectural registers unchanged.
                // Do not call VIXL's load helper: it unconditionally MaybeClear()s.
            } else if (is_fmov_gpr(op)) {
                // Transport raw integer bits, without host FP conversions or
                // guest FP control/status changes. GPR31 is ZR, SIMD31 is real.
                const unsigned rd = op & 31, rn = (op >> 5) & 31;
                switch (op & 0xfffffc00) {
                case 0x1e260000: p->simulator.WriteWRegister(rd, p->simulator.ReadSRegisterBits(rn)); break;
                case 0x1e270000: p->simulator.WriteSRegisterBits(rd, p->simulator.ReadWRegister(rn)); break;
                case 0x9e660000: p->simulator.WriteXRegister(rd, p->simulator.ReadDRegisterBits(rn)); break;
                case 0x9e670000: p->simulator.WriteDRegisterBits(rd, p->simulator.ReadXRegister(rn)); break;
                case 0x9eae0000: {
                    const auto bits = p->simulator.ReadQRegister(rn);
                    uint64_t upper; std::memcpy(&upper, bits.val + 8, sizeof(upper));
                    p->simulator.WriteXRegister(rd, upper); break;
                }
                case 0x9eaf0000: {
                    auto bits = p->simulator.ReadQRegister(rd);
                    const uint64_t upper = p->simulator.ReadXRegister(rn);
                    std::memcpy(bits.val + 8, &upper, sizeof(upper));
                    p->simulator.WriteQRegister(rd, bits); break;
                }
                }
            } else if ((op & 0x7ffffc00) == 0x1e230000) {
                // Exact UCVTF S,W and S,X only. Source31 is ZR; equal numeric
                // GPR/SIMD indices are distinct banks. No host FP conversion.
                const unsigned rn=(op>>5)&31;
                const uint64_t value=(op&0x80000000)?uint64_t(p->simulator.ReadXRegister(rn)):
                    uint64_t(uint32_t(p->simulator.ReadWRegister(rn)));
                satchel_cpu::Binary32Result floating{};
                if(!p->fp_status_known || !satchel_cpu::uint_to_binary32(value,
                        p->simulator.ReadFpcr().GetRawValue(),p->modeled_fpsr,floating))
                    throw Fault(SATCHEL_CPU_UNSUPPORTED,"Floating-point control or status is outside reviewed coverage");
                p->simulator.WriteSRegisterBits(op&31,floating.bits);
                p->modeled_fpsr=floating.fpsr;
            } else if ((op & 0xffe0fc00) == 0x1e601800) {
                // Exact scalar FDIV D. Snapshot both raw sources before any
                // destination/status commit; register31 is SIMD, never ZR.
                const uint64_t a=p->simulator.ReadDRegisterBits((op>>5)&31);
                const uint64_t b=p->simulator.ReadDRegisterBits((op>>16)&31);
                satchel_cpu::Binary64Result floating{};
                if(!p->fp_status_known || !satchel_cpu::divide_binary64(a,b,
                        p->simulator.ReadFpcr().GetRawValue(),p->modeled_fpsr,floating))
                    throw Fault(SATCHEL_CPU_UNSUPPORTED,"Floating-point control or status is outside reviewed coverage");
                p->simulator.WriteDRegisterBits(op&31,floating.bits);
                p->modeled_fpsr=floating.fpsr;
            } else if ((op & 0x7ffffc00) == 0x1e630000) {
                // Exact UCVTF D,W and D,X. Read GPR/ZR before the independent
                // SIMD destination write; W is explicitly zero-extended.
                const unsigned rn=(op>>5)&31;
                const uint64_t value=(op&0x80000000)?uint64_t(p->simulator.ReadXRegister(rn)):
                    uint64_t(uint32_t(p->simulator.ReadWRegister(rn)));
                satchel_cpu::Binary64Result floating{};
                if(!p->fp_status_known || !satchel_cpu::uint_to_binary64(value,
                        p->simulator.ReadFpcr().GetRawValue(),p->modeled_fpsr,floating))
                    throw Fault(SATCHEL_CPU_UNSUPPORTED,"Floating-point control or status is outside reviewed coverage");
                p->simulator.WriteDRegisterBits(op&31,floating.bits);
                p->modeled_fpsr=floating.fpsr;
            } else if ((op & 0xfffffc00) == 0x6ee1b800) {
                // Exact vector FCVTZU2D, not scalar/S/half/fixed-point forms.
                // Snapshot both binary64 lanes before any destination writes.
                const auto input=p->simulator.ReadQRegister((op>>5)&31);
                Simulator::qreg_t result{};uint32_t status=p->modeled_fpsr;
                if(!p->fp_status_known)
                    throw Fault(SATCHEL_CPU_UNSUPPORTED,"Floating-point status is outside reviewed coverage");
                for(unsigned lane=0;lane<2;lane++) {
                    uint64_t bits;std::memcpy(&bits,input.val+lane*8,8);
                    satchel_cpu::UInt64Result value{};
                    if(!satchel_cpu::binary64_to_uint64(bits,p->simulator.ReadFpcr().GetRawValue(),status,value))
                        throw Fault(SATCHEL_CPU_UNSUPPORTED,"Floating-point control or status is outside reviewed coverage");
                    std::memcpy(result.val+lane*8,&value.bits,8);status=value.fpsr;
                }
                p->simulator.WriteQRegister(op&31,result);p->modeled_fpsr=status;
            } else if ((op & 0xbffffc00) == 0x0e617800) {
                // Exact FCVTL/FCVTL2 S->D only. Q selects the low/upper64
                // source half; the destination always receives full128 bits.
                const auto input=p->simulator.ReadQRegister((op>>5)&31);
                const unsigned first=(op&0x40000000)?2:0;
                Simulator::qreg_t result{};uint32_t status=p->modeled_fpsr;
                if(!p->fp_status_known)
                    throw Fault(SATCHEL_CPU_UNSUPPORTED,"Floating-point status is outside reviewed coverage");
                for(unsigned lane=0;lane<2;lane++) {
                    uint32_t bits;std::memcpy(&bits,input.val+(first+lane)*4,4);
                    satchel_cpu::Binary64Result value{};
                    if(!satchel_cpu::binary32_to_binary64(bits,p->simulator.ReadFpcr().GetRawValue(),status,value))
                        throw Fault(SATCHEL_CPU_UNSUPPORTED,"Floating-point control or status is outside reviewed coverage");
                    std::memcpy(result.val+lane*8,&value.bits,8);status=value.fpsr;
                }
                // Source snapshot protects Rd==Rn; ignored-half NaNs are not
                // processed. Publish the whole destination and status together.
                p->simulator.WriteQRegister(op&31,result);p->modeled_fpsr=status;
            } else if ((op & 0xbf80f400) == 0x0f809000 &&
                       (!(op & 0x00400000) || ((op & 0x40200000) == 0x40000000))) {
                // Exact indexed FMUL2S/4S/2D. D requires Q=1,L=0; H/scalar,
                // FMULX and multiply-accumulate remain outside this branch.
                const bool wide=(op&0x00400000)!=0;
                const unsigned bytes=wide?8:4,lanes=((op&0x40000000)?16:8)/bytes;
                const unsigned index=wide?((op>>11)&1):(((op>>10)&2)|((op>>21)&1));
                const auto left=p->simulator.ReadQRegister((op>>5)&31);
                const auto right=p->simulator.ReadQRegister((op>>16)&31);
                Simulator::qreg_t result{};uint32_t status=p->modeled_fpsr;
                if(!p->fp_status_known)
                    throw Fault(SATCHEL_CPU_UNSUPPORTED,"Floating-point status is outside reviewed coverage");
                for(unsigned lane=0;lane<lanes;lane++) {
                    bool accepted;
                    if(wide) {
                        uint64_t a,b;std::memcpy(&a,left.val+lane*8,8);std::memcpy(&b,right.val+index*8,8);
                        satchel_cpu::Binary64Result value{};
                        accepted=satchel_cpu::binary64(true,a,b,p->simulator.ReadFpcr().GetRawValue(),status,value);
                        std::memcpy(result.val+lane*8,&value.bits,8);status=value.fpsr;
                    } else {
                        uint32_t a,b;std::memcpy(&a,left.val+lane*4,4);std::memcpy(&b,right.val+index*4,4);
                        satchel_cpu::Binary32Result value{};
                        accepted=satchel_cpu::multiply_binary32(a,b,p->simulator.ReadFpcr().GetRawValue(),status,value);
                        std::memcpy(result.val+lane*4,&value.bits,4);status=value.fpsr;
                    }
                    if(!accepted)throw Fault(SATCHEL_CPU_UNSUPPORTED,"Floating-point control or status is outside reviewed coverage");
                }
                // Snapshot all128 source bits first, including indexed upper
                // lanes for Q0. Publish whole destination and flags together.
                p->simulator.WriteQRegister(op&31,result);p->modeled_fpsr=status;
            } else if (is_binary64_arithmetic(op)) {
                satchel_cpu::Binary64Result floating{};
                if (!p->fp_status_known || !satchel_cpu::binary64((op & 0x2000) == 0,
                        p->simulator.ReadDRegisterBits((op >> 5) & 31),
                        p->simulator.ReadDRegisterBits((op >> 16) & 31),
                        p->simulator.ReadFpcr().GetRawValue(), p->modeled_fpsr, floating))
                    throw Fault(SATCHEL_CPU_UNSUPPORTED, "Floating-point control or status is outside reviewed coverage");
                p->simulator.WriteDRegisterBits(op & 31, floating.bits);
                p->modeled_fpsr = floating.fpsr;
            } else if (is_simd_d_unscaled(op) || is_simd_other_unscaled(op) || is_simd_register(op) || is_simd_d_unsigned(op)) {
                // Integer bit copies avoid host FP transport and VIXL's
                // unconditional MaybeClear() after ordinary loads. SIMD and
                // X/SP are separate banks; equal numbers never alias.
                const bool unsigned_offset = is_simd_d_unsigned(op), register_offset = is_simd_register(op);
                const unsigned rn = (op >> 5) & 31, rt = op & 31, mode = (unsigned_offset || register_offset) ? 0 : (op >> 10) & 3;
                const uint64_t base = p->simulator.ReadXRegister(rn, Reg31IsStackPointer);
                if (rn == 31 && (base & 15)) throw Fault(SATCHEL_CPU_MEMORY_FAULT, "Misaligned guest stack pointer");
                uint64_t offset;
                if (register_offset) {
                    offset = p->simulator.ReadXRegister((op >> 16) & 31);
                    const unsigned extend = (op >> 13) & 7;
                    if (extend == 2) offset = uint32_t(offset);
                    if (extend == 6) offset = uint64_t(sign_extend(uint32_t(offset), 32));
                    // UXTX/SXTX retain identical modulo64 bits; shift and
                    // base addition are unsigned, including negative offsets.
                    const unsigned scale = (op & 0x00800000) ? 4 : op >> 30;
                    offset <<= ((op >> 12) & 1) * scale;
                } else {
                    offset = unsigned_offset ? uint64_t((op >> 10) & 4095) * 8
                                             : uint64_t(sign_extend((op >> 12) & 511, 9));
                }
                const uint64_t address = mode == 1 ? base : base + offset;
                const bool load = op & 0x00400000;
                const size_t width = unsigned_offset ? 8 : (op & 0x00800000) ? 16 : size_t{1} << (op >> 30);
                auto *memory = p->translate(address, width, load ? SATCHEL_CPU_READ : SATCHEL_CPU_WRITE);
                if (load) {
                    Simulator::qreg_t bits{};
                    std::memcpy(bits.val, memory, width);
                    p->simulator.WriteQRegister(rt, bits); // Clears all bits above the transfer width.
                } else {
                    const auto bits = p->simulator.ReadQRegister(rt);
                    std::memcpy(memory, bits.val, width);
                    p->simulator.ClearLocalMonitor();
                }
                if (mode) {
                    if (rn == 31) p->simulator.WriteSp(base + offset);
                    else p->simulator.WriteXRegister(rn, base + offset);
                }
            } else if (op == 0xd503201f) { // NOP only; other system instructions rejected.
            } else if ((op & 0x7ffffc00) == 0x5ac00000 || (op & 0x7ffffc00) == 0x5ac01000) {
                // Exact RBIT/CLZ W/X forms. Keep all shifts unsigned and avoid
                // undefined count-leading-zero intrinsics for a zero operand.
                const unsigned width = (op >> 31) ? 64 : 32;
                uint64_t value = p->simulator.ReadXRegister((op >> 5) & 31);
                if (width == 32) value = uint32_t(value);
                uint64_t result = 0;
                if (op & 0x1000) {
                    for (unsigned bit = width; bit > 0 && !((value >> (bit - 1)) & 1); --bit) ++result;
                } else {
                    for (unsigned bit = 0; bit < width; ++bit) {
                        result = (result << 1) | (value & 1);
                        value >>= 1;
                    }
                }
                p->simulator.WriteXRegister(op & 31, result);
            } else {
                if (!reviewed_instruction(op)) throw Fault(SATCHEL_CPU_UNSUPPORTED, "Instruction is outside reviewed CPU coverage");
                if (is_gpr_exclusive(op) || is_gpr_acquire_release(op)) {
                    const unsigned rn = (op >> 5) & 31;
                    const uint64_t address = p->simulator.ReadXRegister(rn, Reg31IsStackPointer);
                    const size_t width = size_t{1} << (op >> 30);
                    if ((address & (width - 1)) || (rn == 31 && (address & 15)))
                        throw Fault(SATCHEL_CPU_MEMORY_FAULT, "Misaligned guest atomic access");
                    // Validate before VIXL marks a reservation or writes the
                    // status register, including failed-reservation stores.
                    p->translate(address, width, (op & 0x00400000) ? SATCHEL_CPU_READ : SATCHEL_CPU_WRITE);
                }
                if (is_gpr_pair(op) || is_simd_q_pair(op)) {
                    const unsigned rn = (op >> 5) & 31;
                    const uint64_t base = p->simulator.ReadXRegister(rn, Reg31IsStackPointer);
                    if (rn == 31 && (base & 15)) throw Fault(SATCHEL_CPU_MEMORY_FAULT, "Misaligned guest stack pointer");
                    const size_t width = is_simd_q_pair(op) ? 16 : (op & 0x80000000) ? 8 : 4;
                    const uint64_t offset = uint64_t(sign_extend((op >> 15) & 127, 7)) * width;
                    const bool postindex = ((op >> 23) & 3) == 1;
                    const uint64_t address = postindex ? base : base + offset;
                    // Validate both accesses before VIXL can write back a base
                    // or store the first word. Patched per-word checks remain.
                    p->translate(address, 2 * width, (op & 0x00400000) ? SATCHEL_CPU_READ : SATCHEL_CPU_WRITE);
                }
                if (is_gpr_unscaled(op)) {
                    const unsigned rn = (op >> 5) & 31;
                    const uint64_t base = p->simulator.ReadXRegister(rn, Reg31IsStackPointer);
                    if (rn == 31 && (base & 15)) throw Fault(SATCHEL_CPU_MEMORY_FAULT, "Misaligned guest stack pointer");
                    const uint64_t offset = uint64_t(sign_extend((op >> 12) & 511, 9));
                    const bool postindex = ((op >> 10) & 3) == 1;
                    const uint64_t address = postindex ? base : base + offset;
                    const size_t width = size_t{1} << (op >> 30);
                    // AddressModeHelper writes the base before loading/storing.
                    // Keep that writeback unreachable until the whole access is checked.
                    p->translate(address, width, (op & 0x00400000) ? SATCHEL_CPU_READ : SATCHEL_CPU_WRITE);
                }
                if (is_register_memory(op)) {
                    const unsigned rn = (op >> 5) & 31;
                    const uint64_t base = p->simulator.ReadXRegister(rn, Reg31IsStackPointer);
                    if (rn == 31 && (base & 15)) throw Fault(SATCHEL_CPU_MEMORY_FAULT, "Misaligned guest stack pointer");
                    uint64_t offset = p->simulator.ReadXRegister((op >> 16) & 31);
                    const unsigned extend = (op >> 13) & 7;
                    if (extend == 2) offset = uint32_t(offset);
                    if (extend == 6) offset = uint64_t(sign_extend(uint32_t(offset), 32));
                    // UXTX/SXTX have identical modulo-64 address bits. All
                    // shifts here are unsigned and only 0 or the element size.
                    const unsigned size_log2 = op >> 30;
                    offset <<= ((op >> 12) & 1) * size_log2;
                    p->translate(base + offset, size_t{1} << size_log2,
                                 (op & 0x00c00000) ? SATCHEL_CPU_READ : SATCHEL_CPU_WRITE);
                }
                if (is_gpr_unsigned(op)) {
                    const unsigned rn = (op >> 5) & 31;
                    const uint64_t base = p->simulator.ReadXRegister(rn, Reg31IsStackPointer);
                    if (rn == 31 && (base & 15)) throw Fault(SATCHEL_CPU_MEMORY_FAULT, "Misaligned guest stack pointer");
                    const unsigned size_log2 = op >> 30;
                    const uint64_t offset = uint64_t((op >> 10) & 4095) << size_log2;
                    p->translate(base + offset, size_t{1} << size_log2,
                                 (op & 0x00c00000) ? SATCHEL_CPU_READ : SATCHEL_CPU_WRITE);
                }
                p->simulator.WritePc(code);
                p->simulator.ExecuteInstruction();
                // Upstream only MaybeClear()s for ordinary stores. Always
                // invalidate after a successful checked guest write instead.
                if (is_ordinary_store(op)) p->simulator.ClearLocalMonitor();
            }
            p->pc = next;
            ++result.steps;
        }
        if (result.reason == SATCHEL_CPU_BUDGET || result.reason == SATCHEL_CPU_CANCELLED) result.pc = p->pc;
    } catch (const Fault &e) { result.reason = e.reason; copy_error(result.detail, sizeof(result.detail), e.what()); }
    catch (const std::exception &) {
        result.reason = SATCHEL_CPU_UNSUPPORTED;
        copy_error(result.detail, sizeof(result.detail), "VIXL rejected this instruction or architectural state");
    } catch (...) {
        result.reason = SATCHEL_CPU_INTERNAL_FAULT;
        copy_error(result.detail, sizeof(result.detail), "Unknown CPU interpreter failure");
    }
    p->terminal = result.reason == SATCHEL_CPU_UNSUPPORTED || result.reason == SATCHEL_CPU_MEMORY_FAULT || result.reason == SATCHEL_CPU_INTERNAL_FAULT;
    p->last = result;
    return result;
}
