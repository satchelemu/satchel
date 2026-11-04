#include "CPUProbe.h"
#include "Binary64.h"
#include <fenv.h>
#include <algorithm>
#include <array>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <time.h>
#include <vector>
extern "C" uint64_t probe_arithmetic(void *, uint64_t);
extern "C" void probe_native_ucvtf_status(uint64_t *, uint64_t, uint64_t, uint64_t);
extern "C" uint64_t probe_ucvtf(void *);
extern "C" void probe_native_ucvtf64_status(uint64_t *, uint64_t, uint64_t, uint64_t);
extern "C" uint64_t probe_ucvtf64(void *);
extern "C" uint64_t probe_fdiv(void *);
extern "C" void probe_native_fdiv_status(uint64_t *,uint64_t,uint64_t,uint64_t);
extern "C" uint64_t probe_fmulindexed(void *);
extern "C" uint64_t probe_fcvtl(void *);
extern "C" uint64_t probe_fcvtzu2d(void *);
extern "C" void probe_native_fcvtzu2d_status(uint64_t *, uint64_t, uint64_t, uint64_t);
extern "C" void probe_native_widen32_status(uint64_t *, uint64_t, uint64_t, uint64_t);
extern "C" void probe_native_mul32_status(uint64_t *, uint64_t, uint64_t, uint64_t);
extern "C" int satchel_cpu_probe_test_conversion_state(SatchelCPUProbe *, unsigned);
extern "C" void probe_native_fp_status(uint64_t *, uint64_t, uint64_t, uint64_t);
extern "C" void probe_vector(void *);
extern "C" void probe_floating(void *);
extern "C" uint64_t probe_barrier(void *);
extern "C" uint64_t probe_dsb(void *);
extern "C" uint64_t probe_isb(void *);
extern "C" uint64_t probe_clrex(void *);
extern "C" uint64_t probe_exclusive(void *);
extern "C" void probe_acquire(void *);
extern "C" void probe_ordered(void *, uint64_t);
extern "C" uint64_t probe_pairs(void *);
extern "C" uint64_t probe_wpairs(void *);
extern "C" uint64_t probe_qpairs(void *);
extern "C" uint64_t probe_dsingle(void *);
extern "C" uint64_t probe_dunsigned(void *);
extern "C" uint64_t probe_simdregister(void *);
extern "C" uint64_t probe_simdaddsub(void *);
extern "C" uint64_t probe_simdcompare(void *);
extern "C" uint64_t probe_simdlogical(void *);
extern "C" uint64_t probe_simdreduce(void *);
extern "C" uint64_t probe_fmovgpr(void *);
extern "C" uint64_t probe_simdcount(void *);
extern "C" uint64_t probe_prefetch(void *);
extern "C" uint64_t probe_elementnarrow(void *);
extern "C" uint64_t probe_simdwidenreduce(void *);
extern "C" uint64_t probe_simdpermute(void *);
extern "C" uint64_t probe_simdsinglewidths(void *);
extern "C" uint64_t probe_movinteger(void *);
extern "C" uint64_t probe_dupgeneral(void *, uint64_t);
extern "C" uint64_t probe_bytes(void *);
extern "C" uint64_t probe_unscaled(void *);
extern "C" void probe_halfunscaled(void *, uint64_t);
extern "C" void probe_multiply(void *, uint64_t, uint64_t, uint64_t);
extern "C" void probe_highmultiply(void *, uint64_t, uint64_t, uint64_t);
extern "C" void probe_tls(void *, uint64_t, uint64_t, uint64_t);
extern "C" uint64_t probe_breakpoints(uint64_t);
extern "C" void satchel_cpu_probe_test_mapping_allocation_failure(unsigned stage);
extern "C" int satchel_cpu_probe_test_same_backing(SatchelCPUProbe *, uint64_t, uint64_t);
extern "C" uint64_t satchel_cpu_probe_test_physical_bytes(SatchelCPUProbe *);
extern "C" void satchel_cpu_probe_test_write_allocation_failure(int fail);
extern "C" uint64_t satchel_cpu_probe_test_ticks_from_ns(uint64_t ns);
extern "C" int satchel_cpu_probe_test_set_clock(SatchelCPUProbe *, uint64_t elapsed_ns, int fail);
extern "C" int satchel_cpu_probe_test_set_known_fpcr(SatchelCPUProbe *, uint32_t value);
extern "C" SatchelCPUWriteResult satchel_cpu_probe_test_swap(SatchelCPUProbe *, uint64_t, uint64_t, size_t);
extern "C" void probe_registerloads(void *);
extern "C" void probe_testbranch(void *, uint64_t);
extern "C" void probe_bitcount(void *, uint64_t);
extern "C" void probe_variableshift(void *, uint64_t, uint64_t);
extern "C" void probe_registermemory(void *, uint64_t);
extern "C" void probe_conditional(void *, uint64_t, uint64_t, uint64_t, uint64_t);
extern "C" void probe_division(void *, uint64_t, uint64_t);
extern "C" void probe_immediates(void *, uint64_t);
extern "C" void probe_conditionalcompare(void *, uint64_t, uint64_t, uint64_t, uint64_t);
extern "C" void probe_extended(void *, uint64_t, uint64_t, uint64_t, uint64_t);
using Probe = std::unique_ptr<SatchelCPUProbe, decltype(&satchel_cpu_probe_destroy)>;
constexpr uint64_t code_address = 0x7100000000;
constexpr uint64_t data_address = 0x7200000000;
constexpr uint64_t stack_address = 0x7300000000;
static unsigned checks = 0;
static void check(bool ok, const char *message) {
    ++checks;
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
static Probe make(const std::vector<uint8_t> &code, const void *data = nullptr, size_t size = 64) {
    SatchelCPUProbeRegion regions[] = {
        {code_address, code.data(), code.size(), code.size(), SATCHEL_CPU_READ | SATCHEL_CPU_EXECUTE},
        {data_address, data, data ? size : 0, size, SATCHEL_CPU_READ | SATCHEL_CPU_WRITE},
        {stack_address, nullptr, 0, 128, SATCHEL_CPU_READ | SATCHEL_CPU_WRITE}};
    char error[160];
    Probe p(satchel_cpu_probe_create(regions, 3, code_address, error, sizeof(error)), satchel_cpu_probe_destroy);
    check(bool(p), error);
    check(satchel_cpu_probe_set_x(p.get(), 0, data_address), "set argument");
    check(satchel_cpu_probe_set_sp(p.get(), stack_address + 128), "set stack");
    return p;
}
static std::vector<uint8_t> words(std::initializer_list<uint32_t> ops) {
    std::vector<uint8_t> bytes(ops.size() * 4);
    std::memcpy(bytes.data(), ops.begin(), bytes.size()); return bytes;
}
static uint32_t pair_op(uint32_t form, unsigned rt, unsigned rt2, unsigned rn, int offset) {
    const int scale = (form & 0x04000000) ? 16 : (form & 0x80000000) ? 8 : 4;
    assert(offset % scale == 0 && offset >= -64 * scale && offset <= 63 * scale);
    return form | ((uint32_t(offset / scale) & 127) << 15) | (rt2 << 10) | (rn << 5) | rt;
}
static uint32_t scalar_op(uint32_t form, unsigned rt, unsigned rn, int offset) {
    assert(offset >= -256 && offset <= 255);
    return form | ((uint32_t(offset) & 511) << 12) | (rn << 5) | rt;
}
static uint32_t register_load(uint32_t form, unsigned rt, unsigned rn, unsigned rm, unsigned extend, bool scaled) {
    assert(extend < 8 && rt < 32 && rn < 32 && rm < 32);
    return form | (rm << 16) | (extend << 13) | (uint32_t(scaled) << 12) | (rn << 5) | rt;
}
static std::vector<uint8_t> fixture(const char *root, const char *name) {
    std::ifstream f(std::string(root) + "/" + name + ".bin", std::ios::binary);
    check(bool(f), "fixture exists");
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(f), {});
}
static uint64_t x(SatchelCPUProbe *p, unsigned r) {
    uint64_t value = 0; check(satchel_cpu_probe_get_x(p, r, &value), "read register"); return value;
}
static void expect_stop(SatchelCPUProbe *p, SatchelCPUProbeStop expected) {
    auto r = satchel_cpu_probe_run(p, 1000, nullptr, nullptr);
    if (r.reason != expected) std::cerr << "reason=" << r.reason << " op=" << std::hex << r.instruction << " " << r.detail << '\n';
    check(r.reason == expected, "expected stop");
}
static void breakpoint_tests(const char *fixtures) {
    const auto call_code = fixture(fixtures, "breakpoints");
    // Offset belongs to this original fixed eight-instruction fixture only.
    const uint64_t target = code_address + 24;
    for (uint64_t input : {uint64_t{0}, uint64_t{19}, UINT64_MAX - 2}) {
        auto ordinary = make(call_code), trapped = make(call_code);
        satchel_cpu_probe_set_x(ordinary.get(), 0, input); satchel_cpu_probe_set_x(trapped.get(), 0, input);
        expect_stop(ordinary.get(), SATCHEL_CPU_SVC);
        const uint64_t expected = probe_breakpoints(input);
        check(x(ordinary.get(), 0) == expected, "no breakpoint retains native BL/BLR fixture behavior");
        uint64_t registered = target;
        check(satchel_cpu_probe_set_breakpoints(trapped.get(), &registered, 1), "register original guest stub");
        registered = data_address; // The probe must own its copied address set.
        for (unsigned call = 0; call < 2; ++call) {
            const auto result = satchel_cpu_probe_run(trapped.get(), 100, nullptr, nullptr);
            check(result.reason == SATCHEL_CPU_BREAKPOINT && result.pc == target && result.steps == 2 && result.instruction == 0,
                  "BL and BLR stop before original stub executes or counts");
            const uint64_t old_result = x(trapped.get(), 0), lr = x(trapped.get(), 30);
            check(!satchel_cpu_probe_set_pc(trapped.get(), data_address) && x(trapped.get(), 0) == old_result,
                  "invalid dispatcher return cannot change return value");
            check(satchel_cpu_probe_set_pc(trapped.get(), lr), "validate guest LR before dispatcher result");
            check(satchel_cpu_probe_set_x(trapped.get(), 0, old_result + 3), "original synthetic dispatcher result");
        }
        expect_stop(trapped.get(), SATCHEL_CPU_SVC);
        check(x(trapped.get(), 0) == expected && x(trapped.get(), 30) == 0,
              "manual validated guest returns match native call results and restore LR");
    }
    auto p = make(words({0xeb00001f, 0xf9000001, 0x9a9f17e2, 0x910003e3, 0xd4000001}));
    uint64_t breakpoint = code_address + 4;
    check(satchel_cpu_probe_set_breakpoints(p.get(), &breakpoint, 1), "register state-preservation breakpoint");
    for (unsigned r = 0; r < 31; ++r) satchel_cpu_probe_set_x(p.get(), r, 0x12340000 + r);
    satchel_cpu_probe_set_x(p.get(), 0, data_address);
    std::array<uint8_t, 16> vector{};
    for (unsigned r = 0; r < 32; ++r) {
        vector.fill(uint8_t(r * 7)); check(satchel_cpu_probe_set_q(p.get(), r, vector.data()), "seed SIMD state");
    }
    struct Mutations { SatchelCPUProbe *probe; bool rejected = false; } mutations{p.get()};
    auto result = satchel_cpu_probe_run(p.get(), 10, [](void *opaque) -> int {
        auto &m = *static_cast<Mutations *>(opaque);
        m.rejected = !satchel_cpu_probe_set_pc(m.probe, code_address + 8) &&
                     !satchel_cpu_probe_set_breakpoints(m.probe, nullptr, 0);
        return 1;
    }, &mutations);
    check(result.reason == SATCHEL_CPU_CANCELLED && result.steps == 0 && mutations.rejected,
          "active callback cannot redirect PC or replace breakpoint set");
    result = satchel_cpu_probe_run(p.get(), 1, nullptr, nullptr);
    check(result.reason == SATCHEL_CPU_BUDGET && result.pc == breakpoint && result.steps == 1,
          "instruction budget wins before next breakpoint check");
    int callbacks = 0;
    result = satchel_cpu_probe_run(p.get(), 0, [](void *v) -> int { ++*static_cast<int *>(v); return 1; }, &callbacks);
    check(result.reason == SATCHEL_CPU_BUDGET && result.pc == breakpoint && callbacks == 0, "zero budget performs no breakpoint/callback work");
    result = satchel_cpu_probe_run(p.get(), 1, [](void *) -> int { return 1; }, nullptr);
    check(result.reason == SATCHEL_CPU_CANCELLED && result.steps == 0 && result.pc == breakpoint, "cancellation wins over breakpoint");
    for (unsigned repeat = 0; repeat < 2; ++repeat) {
        result = satchel_cpu_probe_run(p.get(), 10, nullptr, nullptr);
        check(result.reason == SATCHEL_CPU_BREAKPOINT && result.steps == 0 && result.pc == breakpoint && result.instruction == 0,
              "repeat run does not automatically execute trapped instruction");
    }
    for (unsigned r = 0; r < 31; ++r)
        check(x(p.get(), r) == (r == 0 ? data_address : 0x12340000 + r), "breakpoint preserves every GPR");
    for (unsigned r = 0; r < 32; ++r) {
        check(satchel_cpu_probe_get_q(p.get(), r, vector.data()) &&
              std::all_of(vector.begin(), vector.end(), [r](uint8_t b) { return b == uint8_t(r * 7); }),
              "breakpoint preserves every SIMD register");
    }
    uint64_t stored = 1;
    check(satchel_cpu_probe_read(p.get(), data_address, &stored, 8) && stored == 0, "breakpoint prevents pending memory store");
    for (uint64_t invalid : {uint64_t{0}, breakpoint + 1, data_address, code_address + 20, UINT64_MAX}) {
        check(!satchel_cpu_probe_set_breakpoints(p.get(), &invalid, 1) && !satchel_cpu_probe_set_pc(p.get(), invalid),
              "breakpoint and PC reject non-executable/unaligned/outside addresses");
    }
    const uint64_t duplicate[] = {breakpoint, breakpoint};
    check(!satchel_cpu_probe_set_breakpoints(p.get(), duplicate, 2), "duplicate addresses rejected atomically");
    check(!satchel_cpu_probe_set_breakpoints(p.get(), nullptr, 1), "null nonempty breakpoint input rejected");
    check(!satchel_cpu_probe_set_breakpoints(nullptr, nullptr, 0) && !satchel_cpu_probe_set_pc(nullptr, code_address), "null mutation contexts rejected");
    result = satchel_cpu_probe_run(p.get(), 1, nullptr, nullptr);
    check(result.reason == SATCHEL_CPU_BREAKPOINT && result.pc == breakpoint, "rejected registration/PC changes preserve previous stop");
    check(satchel_cpu_probe_set_pc(p.get(), code_address + 8), "skip synthetic intercepted store with checked PC");
    expect_stop(p.get(), SATCHEL_CPU_SVC);
    check(x(p.get(), 2) == 1 && x(p.get(), 3) == stack_address + 128, "breakpoint and PC setting preserve NZCV and SP");
    check(satchel_cpu_probe_set_pc(p.get(), breakpoint) && satchel_cpu_probe_set_breakpoints(p.get(), nullptr, 0), "clear set and resume existing code");
    expect_stop(p.get(), SATCHEL_CPU_SVC);
    check(satchel_cpu_probe_read(p.get(), data_address, &stored, 8) && stored == 0x12340001, "cleared breakpoint executes ordinary guest store");
    const auto large_code = words({0xd503201f});
    std::vector<uint8_t> many_code(33 * 4);
    for (size_t i = 0; i < 33; ++i) std::copy(large_code.begin(), large_code.end(), many_code.begin() + i * 4);
    auto many = make(many_code);
    std::array<uint64_t, 33> points{};
    for (size_t i = 0; i < points.size(); ++i) points[i] = code_address + (32 - i) * 4;
    check(satchel_cpu_probe_set_breakpoints(many.get(), points.data() + 1, 32), "32 unsorted breakpoints accepted and copied");
    check(!satchel_cpu_probe_set_breakpoints(many.get(), points.data(), 33), "breakpoint capacity checked before input traversal");
    result = satchel_cpu_probe_run(many.get(), 1, nullptr, nullptr);
    check(result.reason == SATCHEL_CPU_BREAKPOINT && result.steps == 0, "full set survives rejected replacement");
    auto bad = make(words({0xffffffff, 0xd503201f})); expect_stop(bad.get(), SATCHEL_CPU_UNSUPPORTED);
    const auto fault = satchel_cpu_probe_run(bad.get(), 1, nullptr, nullptr);
    uint64_t valid = code_address + 4;
    check(!satchel_cpu_probe_set_pc(bad.get(), valid) && !satchel_cpu_probe_set_breakpoints(bad.get(), &valid, 1),
          "PC and breakpoint changes cannot revive terminal fault");
    result = satchel_cpu_probe_run(bad.get(), 1, nullptr, nullptr);
    check(result.reason == fault.reason && result.pc == fault.pc && result.steps == fault.steps, "terminal fault result remains unchanged");
    auto svc = make(words({0xd4000001})); valid = code_address;
    check(satchel_cpu_probe_set_breakpoints(svc.get(), &valid, 1), "register SVC boundary");
    result = satchel_cpu_probe_run(svc.get(), 1, nullptr, nullptr);
    check(result.reason == SATCHEL_CPU_BREAKPOINT && result.steps == 0 && result.svc == 0, "breakpoint precedes SVC and fabricates no syscall");
    auto partial_code = words({0xd503201f, 0xd503201f}); partial_code.resize(6);
    auto partial = make(partial_code); valid = code_address + 4;
    check(!satchel_cpu_probe_set_pc(partial.get(), valid) && !satchel_cpu_probe_set_breakpoints(partial.get(), &valid, 1),
          "PC and breakpoint require complete four-byte executable span");
    const auto nop = words({0xd503201f});
    SatchelCPUProbeRegion read_regions[] = {{code_address, nop.data(), 4, 4, SATCHEL_CPU_READ | SATCHEL_CPU_EXECUTE},
                                      {data_address, nullptr, 0, 4096, SATCHEL_CPU_READ}};
    char error[160];
    Probe read_only(satchel_cpu_probe_create(read_regions, 2, code_address, error, sizeof(error)), satchel_cpu_probe_destroy);
    check(bool(read_only), "create read-only address test context"); valid = data_address;
    check(!satchel_cpu_probe_set_pc(read_only.get(), valid) && !satchel_cpu_probe_set_breakpoints(read_only.get(), &valid, 1),
          "readable memory alone cannot become executable dispatcher target");
}
static void acquire_tests(const char *fixtures) {
    const auto code = fixture(fixtures, "acquire");
    for (uint64_t value : {uint64_t{0}, uint64_t{1}, UINT64_MAX, UINT64_C(0x8000000000000000),
                           UINT64_C(0xffffffff), UINT64_C(0x80000000), UINT64_C(0x7fffffff)}) {
        std::array<uint8_t, 64> native{}, interpreted{}; native.fill(0xa5);
        for (size_t offset : {size_t{0}, size_t{8}, size_t{16}}) std::memcpy(native.data() + offset, &value, 8);
        auto p = make(code, native.data());
        probe_acquire(native.data()); expect_stop(p.get(), SATCHEL_CPU_SVC);
        check(satchel_cpu_probe_read(p.get(), data_address, interpreted.data(), interpreted.size()) && interpreted == native,
              "native W/X acquire-release/plain exclusive retry loops match full memory");
        uint64_t wide, narrow, flags;
        std::memcpy(&wide, interpreted.data(), 8); std::memcpy(&narrow, interpreted.data() + 32, 8);
        std::memcpy(&flags, interpreted.data() + 48, 8);
        check(wide == value + 1 && narrow == uint32_t(value + 1) && flags == 1,
              "exclusive stores really succeed, W loads zero-extend and NZCV is preserved");
    }
    for (bool wide : {false, true}) {
        const uint32_t load = (wide ? 0xc85ffc01u : 0x885ffc01u);
        const uint32_t store = (wide ? 0xc802fc01u : 0x8802fc01u);
        const uint64_t initial = 10, replacement = 77;
        const size_t width = wide ? 8 : 4;
        auto no_reservation = make(words({store, 0xd4000001}), &initial, 8);
        satchel_cpu_probe_set_x(no_reservation.get(), 1, replacement); expect_stop(no_reservation.get(), SATCHEL_CPU_SVC);
        uint64_t observed = 0;
        check(x(no_reservation.get(), 2) == 1 && satchel_cpu_probe_read(no_reservation.get(), data_address, &observed, 8) && observed == initial,
              "STLXR without reservation fails and preserves memory");
        auto changed = make(words({load, 0xd4000001, store, 0xd4000001}), &initial, 8);
        expect_stop(changed.get(), SATCHEL_CPU_SVC);
        check(satchel_cpu_probe_write(changed.get(), data_address, &replacement, width), "checked service writes reservation target");
        expect_stop(changed.get(), SATCHEL_CPU_SVC);
        check(x(changed.get(), 2) == 1 && satchel_cpu_probe_read(changed.get(), data_address, &observed, 8) && observed == replacement,
              "service write invalidates reservation; stale guest value cannot overwrite it");
        auto consumed = make(words({load, store, (store & ~0x001f0000u) | 0x00030000u, 0xd4000001}), &initial, 8);
        expect_stop(consumed.get(), SATCHEL_CPU_SVC);
        check(x(consumed.get(), 3) == 1, "each store-exclusive consumes reservation even if it fails");
        auto mismatch = make(words({load, store ^ 0x40000000u, 0xd4000001}), &initial, 8);
        expect_stop(mismatch.get(), SATCHEL_CPU_SVC);
        check(x(mismatch.get(), 2) == 1, "exclusive width mismatch fails");
        const uint32_t normal_stores[] = {0xf9000003, 0xf8000003, 0xf83f6803, 0xa9001003, 0x3d800000};
        for (uint32_t normal_store : normal_stores) {
            std::array<uint8_t, 64> bytes{}; std::memcpy(bytes.data(), &initial, 8);
            auto intervening = make(words({load, normal_store, store, 0xd4000001}), bytes.data());
            satchel_cpu_probe_set_x(intervening.get(), 3, replacement);
            satchel_cpu_probe_set_x(intervening.get(), 4, 0);
            std::array<uint8_t, 16> q{}; q[0] = uint8_t(replacement);
            satchel_cpu_probe_set_q(intervening.get(), 0, q.data());
            expect_stop(intervening.get(), SATCHEL_CPU_SVC);
            check(x(intervening.get(), 2) == 1 && satchel_cpu_probe_read(intervening.get(), data_address, &observed, 8) && observed == replacement,
                  "every reviewed ordinary guest store invalidates stale reservation");
        }
        for (uint32_t instruction : {load, store}) {
            for (uint64_t address : {data_address + 1, data_address + 8, UINT64_MAX - 7}) {
                auto invalid = make(words({instruction}), &initial, 8);
                satchel_cpu_probe_set_x(invalid.get(), 0, address); satchel_cpu_probe_set_x(invalid.get(), 1, replacement);
                satchel_cpu_probe_set_x(invalid.get(), 2, UINT64_MAX);
                expect_stop(invalid.get(), SATCHEL_CPU_MEMORY_FAULT);
                check(x(invalid.get(), 1) == replacement && x(invalid.get(), 2) == UINT64_MAX &&
                      satchel_cpu_probe_read(invalid.get(), data_address, &observed, 8) && observed == initial,
                      "exclusive fault preserves destination/status/memory before side effects");
            }
            auto stack = make(words({(instruction & ~0x3e0u) | 0x3e0u}));
            satchel_cpu_probe_set_sp(stack.get(), stack_address + 8); expect_stop(stack.get(), SATCHEL_CPU_MEMORY_FAULT);
        }
        const auto store_code = words({load, 0xd4000001, store, 0xd4000001});
        SatchelCPUProbeRegion ro_regions[] = {{code_address, store_code.data(), store_code.size(), store_code.size(), 5},
                                        {data_address, &initial, 8, 8, SATCHEL_CPU_READ}};
        char error[160]; Probe ro(satchel_cpu_probe_create(ro_regions, 2, code_address, error, sizeof(error)), satchel_cpu_probe_destroy);
        check(bool(ro), "read-only exclusive target context"); satchel_cpu_probe_set_x(ro.get(), 0, data_address);
        satchel_cpu_probe_set_x(ro.get(), 2, UINT64_MAX); expect_stop(ro.get(), SATCHEL_CPU_SVC); expect_stop(ro.get(), SATCHEL_CPU_MEMORY_FAULT);
        check(x(ro.get(), 2) == UINT64_MAX && satchel_cpu_probe_read(ro.get(), data_address, &observed, 8) && observed == initial,
              "exclusive permission failure happens before status update");
        for (uint32_t alias : {(store & ~0x001f0000u) | 0x00010000u, store & ~0x001f0000u,
                               (store & ~0x001f001fu) | 0x001f001fu}) {
            auto invalid = make(words({alias})); expect_stop(invalid.get(), SATCHEL_CPU_UNSUPPORTED);
        }
    }
    for (uint32_t unsupported : {0x085ffc01u, 0x485ffc01u, 0xc87ffc01u, 0x88df7c01u,
                                 0x889f7c01u, 0x88a17c02u, 0x885ff801u}) {
        auto p = make(words({unsupported})); expect_stop(p.get(), SATCHEL_CPU_UNSUPPORTED);
    }
}
static void ordered_tests(const char *fixtures) {
    const auto code = fixture(fixtures, "ordered");
    for (uint64_t value : {uint64_t{0}, UINT64_MAX, UINT64_C(0x8000000000000000),
                           UINT64_C(0x80000000800080ff), UINT64_C(0x0123456789abcdef)}) {
        std::array<uint8_t, 128> native{}, interpreted{};
        for (size_t i = 0; i < native.size(); ++i) native[i] = uint8_t(i * 13 + 0x81);
        auto p = make(code, native.data(), native.size()); satchel_cpu_probe_set_x(p.get(), 1, value);
        probe_ordered(native.data(), value); expect_stop(p.get(), SATCHEL_CPU_SVC);
        check(satchel_cpu_probe_read(p.get(), data_address, interpreted.data(), interpreted.size()) && interpreted == native,
              "native LDAR/STLR all widths match full memory, zero extension, truncation, SP and flags");
        uint64_t flags, stack_value; std::memcpy(&flags, interpreted.data() + 64, 8);
        std::memcpy(&stack_value, interpreted.data() + 72, 8);
        check(flags == 1 && stack_value == value, "ordered transfers preserve NZCV and stack values");
    }
    for (unsigned size = 0; size < 4; ++size) {
        const size_t width = size_t{1} << size;
        const uint32_t load = 0x08dffc01u | (size << 30), store = 0x089ffc01u | (size << 30);
        const uint64_t initial = UINT64_C(0xfedcba9876543210), replacement = UINT64_C(0x123456789abcdef0);
        uint64_t observed = 0, expected = initial;
        auto loaded = make(words({load, 0xd4000001}), &initial, 8);
        satchel_cpu_probe_set_x(loaded.get(), 1, UINT64_MAX); expect_stop(loaded.get(), SATCHEL_CPU_SVC);
        const uint64_t mask = width == 8 ? UINT64_MAX : (UINT64_C(1) << (width * 8)) - 1;
        check(x(loaded.get(), 1) == (initial & mask), "LDAR zero-extends narrow destination");
        auto stored = make(words({store, 0xd4000001}), &initial, 8);
        satchel_cpu_probe_set_x(stored.get(), 1, replacement); expect_stop(stored.get(), SATCHEL_CPU_SVC);
        std::memcpy(&expected, &replacement, width);
        check(satchel_cpu_probe_read(stored.get(), data_address, &observed, 8) && observed == expected,
              "STLR needs no reservation and changes only its transfer width");
        auto not_exclusive = make(words({load, 0xc802fc01, 0xd4000001}), &initial, 8);
        expect_stop(not_exclusive.get(), SATCHEL_CPU_SVC);
        check(x(not_exclusive.get(), 2) == 1 && satchel_cpu_probe_read(not_exclusive.get(), data_address, &observed, 8) && observed == initial,
              "LDAR does not create a store-exclusive reservation");
        auto invalidate = make(words({0xc85ffc01, (store & ~31u) | 3u, 0xc802fc01, 0xd4000001}), &initial, 8);
        satchel_cpu_probe_set_x(invalidate.get(), 3, replacement); expect_stop(invalidate.get(), SATCHEL_CPU_SVC);
        check(x(invalidate.get(), 2) == 1 && satchel_cpu_probe_read(invalidate.get(), data_address, &observed, 8) && observed == expected,
              "STLR invalidates prior reservation before stale exclusive store");
        auto zero = make(words({(load & ~31u) | 31u, (store & ~31u) | 31u, 0x910003e3, 0xd4000001}), &initial, 8);
        expect_stop(zero.get(), SATCHEL_CPU_SVC); expected = initial & ~mask;
        check(satchel_cpu_probe_read(zero.get(), data_address, &observed, 8) && observed == expected &&
              x(zero.get(), 3) == stack_address + 128, "ordered ZR transfers discard/load zero without changing SP");
        auto stack = make(words({(store & ~0x3e0u) | 0x3e0u, (load & ~0x3e0u) | 0x3e0u, 0xd4000001}));
        satchel_cpu_probe_set_sp(stack.get(), stack_address + 112); satchel_cpu_probe_set_x(stack.get(), 1, replacement);
        expect_stop(stack.get(), SATCHEL_CPU_SVC);
        check(x(stack.get(), 1) == (replacement & mask), "all ordered widths support aligned guest SP");
        for (uint32_t instruction : {load, store}) {
            std::vector<uint64_t> invalid_addresses{data_address + 8, UINT64_MAX};
            if (width > 1) invalid_addresses.push_back(data_address + 1);
            for (uint64_t address : invalid_addresses) {
                auto invalid = make(words({instruction}), &initial, 8);
                satchel_cpu_probe_set_x(invalid.get(), 0, address); satchel_cpu_probe_set_x(invalid.get(), 1, replacement);
                expect_stop(invalid.get(), SATCHEL_CPU_MEMORY_FAULT);
                check(x(invalid.get(), 1) == replacement && satchel_cpu_probe_read(invalid.get(), data_address, &observed, 8) && observed == initial,
                      "ordered fault preserves destination and memory");
            }
            auto bad_sp = make(words({(instruction & ~0x3e0u) | 0x3e0u}));
            satchel_cpu_probe_set_sp(bad_sp.get(), stack_address + 8); expect_stop(bad_sp.get(), SATCHEL_CPU_MEMORY_FAULT);
            for (uint32_t bit : {0x10000u, 0x400u, 0x8000u}) {
                auto reserved = make(words({instruction ^ bit})); expect_stop(reserved.get(), SATCHEL_CPU_UNSUPPORTED);
            }
            const auto protected_code = words({instruction});
            SatchelCPUProbeRegion regions[] = {{code_address, protected_code.data(), 4, 4, 5},
                {data_address, &initial, 8, 8, unsigned(instruction == load ? SATCHEL_CPU_WRITE : SATCHEL_CPU_READ)}};
            char error[160]; Probe protected_memory(satchel_cpu_probe_create(regions, 2, code_address, error, sizeof(error)), satchel_cpu_probe_destroy);
            check(bool(protected_memory), "create ordered permission test");
            satchel_cpu_probe_set_x(protected_memory.get(), 0, data_address); satchel_cpu_probe_set_x(protected_memory.get(), 1, replacement);
            expect_stop(protected_memory.get(), SATCHEL_CPU_MEMORY_FAULT);
            check(x(protected_memory.get(), 1) == replacement, "ordered permission failure preserves transfer register");
        }
    }
}
static void mapping_tests() {
    constexpr uint64_t heap = 0x7400000000;
    auto p = make(words({0xf9400001, 0xf9000401, 0xd4000001})); // LDR X1; STR X1; SVC.
    std::array<uint8_t, 4096> initial{}, readback{};
    for (size_t i = 0; i < initial.size(); ++i) initial[i] = uint8_t(i * 37 + 11);
    check(satchel_cpu_probe_resize_rw(p.get(), heap, 4096) == SATCHEL_CPU_MAP_OK, "create bounded dynamic page");
    check(satchel_cpu_probe_read(p.get(), heap, readback.data(), readback.size()) &&
          std::all_of(readback.begin(), readback.end(), [](uint8_t b) { return b == 0; }), "new mapping is zero-filled");
    check(satchel_cpu_probe_write(p.get(), heap, initial.data(), initial.size()), "write mapped page");
    check(satchel_cpu_probe_resize_rw(p.get(), heap, 8192) == SATCHEL_CPU_MAP_OK, "grow dynamic mapping");
    check(satchel_cpu_probe_read(p.get(), heap, readback.data(), readback.size()) && readback == initial, "resize preserves complete prefix");
    check(satchel_cpu_probe_read(p.get(), heap + 4096, readback.data(), readback.size()) &&
          std::all_of(readback.begin(), readback.end(), [](uint8_t b) { return b == 0; }), "growth bytes are zero-filled");
    check(satchel_cpu_probe_write(p.get(), heap + 4096, initial.data(), initial.size()), "write grown page");
    check(satchel_cpu_probe_resize_rw(p.get(), heap, 4096) == SATCHEL_CPU_MAP_OK, "shrink dynamic mapping");
    check(!satchel_cpu_probe_read(p.get(), heap + 4096, readback.data(), 1), "shrink unmaps tail");
    check(satchel_cpu_probe_resize_rw(p.get(), heap, 8192) == SATCHEL_CPU_MAP_OK, "regrow dynamic mapping");
    check(satchel_cpu_probe_read(p.get(), heap + 4096, readback.data(), readback.size()) &&
          std::all_of(readback.begin(), readback.end(), [](uint8_t b) { return b == 0; }), "regrowth never exposes truncated bytes");
    for (uint64_t base : {uint64_t{0}, heap + 1, UINT64_MAX - 4095, code_address, data_address, stack_address}) {
        check(satchel_cpu_probe_resize_rw(p.get(), base, 8192) == SATCHEL_CPU_MAP_INVALID,
              "invalid/overflow/initial-region replacement rejected");
    }
    check(satchel_cpu_probe_resize_rw(nullptr, heap, 4096) == SATCHEL_CPU_MAP_INVALID, "null mapping context rejected");
    check(satchel_cpu_probe_resize_rw(p.get(), heap, 1) == SATCHEL_CPU_MAP_INVALID, "nonpage size rejected");
    check(satchel_cpu_probe_resize_rw(p.get(), heap + 4096, 4096) == SATCHEL_CPU_MAP_INVALID, "interior overlap rejected");
    check(satchel_cpu_probe_resize_rw(p.get(), heap - 4096, 8192) == SATCHEL_CPU_MAP_INVALID, "preceding overlap rejected");
    check(satchel_cpu_probe_resize_rw(p.get(), heap, 256 * 1024 * 1024) == SATCHEL_CPU_MAP_LIMIT,
          "aggregate cap includes initial image/stack/data before any allocation");
    check(satchel_cpu_probe_resize_rw(p.get(), heap, UINT64_C(8) * 1024 * 1024 * 1024) == SATCHEL_CPU_MAP_LIMIT,
          "large virtual reservation cannot become unbounded committed memory");
    check(satchel_cpu_probe_resize_rw(p.get(), code_address, 0) == SATCHEL_CPU_MAP_INVALID, "cannot remove executable image");
    check(satchel_cpu_probe_resize_rw(p.get(), data_address, 0) == SATCHEL_CPU_MAP_INVALID, "cannot remove initial RW region");
    for (unsigned stage : {1u, 2u}) {
        satchel_cpu_probe_test_mapping_allocation_failure(stage);
        check(satchel_cpu_probe_resize_rw(p.get(), heap + 16384, 4096) == SATCHEL_CPU_MAP_ALLOCATION,
              "new mapping allocation failure is reported");
        check(!satchel_cpu_probe_read(p.get(), heap + 16384, readback.data(), 1), "allocation failure publishes no partial mapping");
        check(satchel_cpu_probe_read(p.get(), heap, readback.data(), readback.size()) && readback == initial,
              "allocation failure preserves existing bytes");
    }
    satchel_cpu_probe_test_mapping_allocation_failure(1);
    check(satchel_cpu_probe_resize_rw(p.get(), heap, 12288) == SATCHEL_CPU_MAP_ALLOCATION, "resize allocation failure reported");
    check(!satchel_cpu_probe_read(p.get(), heap + 8192, readback.data(), 1), "resize failure preserves old extent");
    check(satchel_cpu_probe_read(p.get(), heap, readback.data(), readback.size()) && readback == initial, "resize failure preserves old prefix");
    check(satchel_cpu_probe_resize_rw(p.get(), heap + 8192, 4096) == SATCHEL_CPU_MAP_OK, "adjacent mappings are allowed");
    check(satchel_cpu_probe_resize_rw(p.get(), heap, 12288) == SATCHEL_CPU_MAP_INVALID, "growth into neighbor rejected");
    struct Mutation { SatchelCPUProbe *probe; unsigned rejected = 0; } mutation{p.get()};
    auto result = satchel_cpu_probe_run(p.get(), 10, [](void *opaque) -> int {
        auto &state = *static_cast<Mutation *>(opaque);
        for (uint64_t size : {uint64_t{0}, uint64_t{8192}, uint64_t{16384}})
            state.rejected += satchel_cpu_probe_resize_rw(state.probe, heap, size) == SATCHEL_CPU_MAP_BUSY;
        state.rejected += satchel_cpu_probe_resize_rw(state.probe, heap + 16384, 4096) == SATCHEL_CPU_MAP_BUSY;
        return 1;
    }, &mutation);
    check(result.reason == SATCHEL_CPU_CANCELLED && result.steps == 0 && mutation.rejected == 4,
          "callback cannot resize/remove/create/no-op mappings during active run");
    check(satchel_cpu_probe_read(p.get(), heap, readback.data(), readback.size()) && readback == initial,
          "reentrant mutation leaves bytes intact");
    satchel_cpu_probe_set_x(p.get(), 0, heap);
    expect_stop(p.get(), SATCHEL_CPU_SVC);
    uint64_t value = 0, original = 0; std::memcpy(&original, initial.data(), 8);
    check(satchel_cpu_probe_read(p.get(), heap + 8, &value, 8) && value == original,
          "guest interpreter reads and writes the newly mapped page");
    check(satchel_cpu_probe_resize_rw(p.get(), heap, 0) == SATCHEL_CPU_MAP_OK, "remove dynamic mapping");
    check(!satchel_cpu_probe_read(p.get(), heap, &value, 8) && !satchel_cpu_probe_write(p.get(), heap, &value, 8), "removed memory inaccessible");
    check(satchel_cpu_probe_resize_rw(p.get(), heap, 0) == SATCHEL_CPU_MAP_INVALID, "cannot remove missing dynamic mapping");
    check(satchel_cpu_probe_resize_rw(p.get(), heap, 4096) == SATCHEL_CPU_MAP_OK &&
          satchel_cpu_probe_read(p.get(), heap, &value, 8) && value == 0, "remove/remap starts clean");
    auto executable = make(words({0xd61f0000}));
    check(satchel_cpu_probe_resize_rw(executable.get(), heap, 4096) == SATCHEL_CPU_MAP_OK, "map non-executable heap");
    satchel_cpu_probe_set_x(executable.get(), 0, heap); expect_stop(executable.get(), SATCHEL_CPU_MEMORY_FAULT);
    auto full = make(words({0xd4000001}));
    for (unsigned i = 0; i < 29; ++i)
        check(satchel_cpu_probe_resize_rw(full.get(), heap + i * 8192, 4096) == SATCHEL_CPU_MAP_OK, "fill bounded region slots");
    check(satchel_cpu_probe_resize_rw(full.get(), heap + 29 * 8192, 4096) == SATCHEL_CPU_MAP_LIMIT, "32-region cap enforced");
    check(satchel_cpu_probe_resize_rw(full.get(), heap, 8192) == SATCHEL_CPU_MAP_OK, "resize uses existing region slot");
    check(satchel_cpu_probe_resize_rw(full.get(), heap, 0) == SATCHEL_CPU_MAP_OK &&
          satchel_cpu_probe_resize_rw(full.get(), heap + 29 * 8192, 4096) == SATCHEL_CPU_MAP_OK, "removal releases region slot");
    // Mapping changes invalidate an old exclusive reservation, never a host pointer.
    auto reservation = make(words({0xc85f7c01, 0xd4000001, 0xc8027c01, 0xd4000001}));
    check(satchel_cpu_probe_resize_rw(reservation.get(), heap, 4096) == SATCHEL_CPU_MAP_OK, "map exclusive target");
    satchel_cpu_probe_set_x(reservation.get(), 0, heap); expect_stop(reservation.get(), SATCHEL_CPU_SVC);
    check(satchel_cpu_probe_resize_rw(reservation.get(), heap, 8192) == SATCHEL_CPU_MAP_OK, "resize exclusive target");
    expect_stop(reservation.get(), SATCHEL_CPU_SVC);
    check(x(reservation.get(), 2) == 1, "mapping change clears old exclusive reservation");
}
static uint64_t reference_reverse(uint64_t value, unsigned width) {
    uint64_t result = 0;
    for (unsigned bit = 0; bit < width; ++bit) result |= ((value >> bit) & 1) << (width - bit - 1);
    return result;
}
static uint64_t reference_leading(uint64_t value, unsigned width) {
    uint64_t result = width;
    for (unsigned bit = 0; bit < width; ++bit) if ((value >> bit) & 1) result = width - bit - 1;
    return result;
}
static uint64_t reference_variable_shift(uint64_t value, uint64_t count, unsigned width, unsigned operation) {
    const uint64_t mask = width == 64 ? UINT64_MAX : UINT32_MAX;
    value &= mask;
    for (uint64_t i = 0; i < (count & (width - 1)); ++i) {
        switch (operation) {
            case 0: value = (value << 1) & mask; break;
            case 1: value >>= 1; break;
            case 2: value = (value >> 1) | (value & (uint64_t{1} << (width - 1))); break;
            case 3: value = (value >> 1) | ((value & 1) << (width - 1)); break;
        }
    }
    return value;
}
static bool reference_condition(unsigned condition, bool n, bool z, bool c, bool v) {
    switch (condition) {
        case 0: return z;
        case 1: return !z;
        case 2: return c;
        case 3: return !c;
        case 4: return n;
        case 5: return !n;
        case 6: return v;
        case 7: return !v;
        case 8: return c && !z;
        case 9: return !c || z;
        case 10: return n == v;
        case 11: return n != v;
        case 12: return !z && n == v;
        case 13: return z || n != v;
        default: return true;
    }
}
static uint64_t reference_division(uint64_t n, uint64_t d, bool wide, bool is_signed) {
    if (!wide) { n = uint32_t(n); d = uint32_t(d); }
    if (d == 0) return 0;
    if (!is_signed) return n / d;
    const int64_t numerator = wide ? int64_t(n) : int64_t(int32_t(n));
    const int64_t denominator = wide ? int64_t(d) : int64_t(int32_t(d));
    if (numerator == INT64_MIN && denominator == -1) return uint64_t(numerator);
    // W operands are widened here, so INT32_MIN/-1 is representable before
    // truncating to the architecturally specified W result.
    const uint64_t result = uint64_t(numerator / denominator);
    return wide ? result : uint32_t(result);
}
static uint64_t reference_arithmetic_flags(uint64_t a, uint64_t b, unsigned width, bool subtract) {
    const uint64_t mask = width == 64 ? UINT64_MAX : UINT32_MAX;
    const uint64_t sign = uint64_t{1} << (width - 1);
    a &= mask; b &= mask;
    const unsigned __int128 sum = static_cast<unsigned __int128>(a) + b;
    const uint64_t result = (subtract ? a - b : uint64_t(sum)) & mask;
    const bool carry = subtract ? a >= b : bool(sum >> width);
    const bool overflow = (subtract ? (a ^ b) & (a ^ result) : ~(a ^ b) & (a ^ result)) & sign;
    return ((result & sign) ? 8 : 0) | (result == 0 ? 4 : 0) | (carry ? 2 : 0) | (overflow ? 1 : 0);
}
static uint64_t reference_extend(uint64_t value, unsigned extension, unsigned width, unsigned amount) {
    const unsigned source_width = std::min(width, 8u << (extension & 3));
    const uint64_t source_mask = source_width == 64 ? UINT64_MAX : (uint64_t{1} << source_width) - 1;
    value &= source_mask;
    if (extension >= 4 && (value & (uint64_t{1} << (source_width - 1)))) value |= ~source_mask;
    value <<= amount;
    return width == 64 ? value : uint32_t(value);
}
static Probe shared(SatchelCPUProbe *peer, uint64_t entry = code_address) {
    char error[160];
    Probe result(satchel_cpu_probe_create_shared(peer, entry, error, sizeof(error)), satchel_cpu_probe_destroy);
    check(bool(result), error); return result;
}
static void shared_context_tests(const char *fixtures) {
    // Alternate after every instruction, including between flag producers and
    // consumers and between SIMD/TLS operations. Each result matches native.
    for (const char *name : {"arithmetic", "vector", "floating", "tls"}) {
        std::array<uint8_t, 128> initial{}, expected{}, actual{};
        for (unsigned i = 0; i < initial.size(); ++i) initial[i] = uint8_t(i * 3 + 1);
        if (!std::strcmp(name, "floating")) {
            for (unsigned i = 0; i < 2; ++i) {
                double a = 1.5 + i, b = 2.25 + i;
                std::memcpy(initial.data() + i * 64, &a, 8);
                std::memcpy(initial.data() + i * 64 + 16, &b, 8);
            }
        }
        expected = initial;
        auto a = make(fixture(fixtures, name), initial.data(), initial.size());
        auto b = shared(a.get());
        SatchelCPUProbe *contexts[] = {a.get(), b.get()};
        uint64_t results[2]{};
        for (unsigned i = 0; i < 2; ++i) {
            auto *p = contexts[i];
            check(x(p, 29) == 0, "shared context fresh GPRs");
            check(satchel_cpu_probe_set_x(p, 0, data_address + i * 64) &&
                  satchel_cpu_probe_set_sp(p, stack_address + 128 - i * 64), "initialize per-context arguments/stack");
            const uint64_t input = i ? 20 : UINT64_MAX - 6;
            satchel_cpu_probe_set_x(p, 1, input);
            if (!std::strcmp(name, "arithmetic")) results[i] = probe_arithmetic(expected.data() + i * 64, input);
            if (!std::strcmp(name, "vector")) probe_vector(expected.data() + i * 64);
            if (!std::strcmp(name, "floating")) probe_floating(expected.data() + i * 64);
            if (!std::strcmp(name, "tls")) {
                satchel_cpu_probe_set_tls(p, 0x100 + i, 0x200 + i);
                satchel_cpu_probe_set_x(p, 3, 0x300 + i);
                probe_tls(expected.data() + i * 64, 0x100 + i, 0x200 + i, 0x300 + i);
            }
        }
        bool done[2]{};
        for (unsigned turn = 0; turn < 512 && (!done[0] || !done[1]); ++turn) {
            const unsigned i = turn & 1; if (done[i]) continue;
            auto r = satchel_cpu_probe_run(contexts[i], 1, nullptr, nullptr);
            check(r.steps == 1 && (r.reason == SATCHEL_CPU_BUDGET || r.reason == SATCHEL_CPU_SVC), "serialized one-instruction context slice");
            done[i] = r.reason == SATCHEL_CPU_SVC;
        }
        check(done[0] && done[1], "both independent contexts reach SVC without fabricated scheduling");
        check(satchel_cpu_probe_read(b.get(), data_address, actual.data(), actual.size()) && actual == expected,
              "shared memory and independent flags/SIMD/FP/TLS match native complete buffers");
        if (!std::strcmp(name, "arithmetic"))for(unsigned i=0;i<2;i++)
            check(x(contexts[i],0)==results[i], "arithmetic return and NZCV remain context-local");
        if (!std::strcmp(name, "tls"))for(unsigned i=0;i<2;i++) {
            uint64_t ro=0,rw=1;
            check(satchel_cpu_probe_get_tls(contexts[i],&ro,&rw) && ro==0x100+i && !rw, "guest TLS writes never cross contexts");
        }
    }
    constexpr uint64_t heap=0x7400000000;
    auto a=make(words({0x910003e1,0xf9000001,0xd4000001,0xffffffff})); // ADD X1,SP,#0; STR; SVC; invalid.
    satchel_cpu_probe_set_x(a.get(),29,0x55);satchel_cpu_probe_set_tls(a.get(),0x66,0x77);
    std::array<uint8_t,16> q{},got{};q.fill(0x5a);satchel_cpu_probe_set_q(a.get(),31,q.data());
    auto b=shared(a.get());uint64_t ro=1,rw=1;
    check(x(b.get(),29)==0 && satchel_cpu_probe_get_tls(b.get(),&ro,&rw) && !ro && !rw,"new sibling does not clone peer registers/TLS");
    check(satchel_cpu_probe_get_q(b.get(),31,got.data()) && got!=q,"new sibling SIMD is independent default state");
    satchel_cpu_probe_set_x(b.get(),0,data_address+8);
    expect_stop(b.get(),SATCHEL_CPU_SVC);uint64_t sp=1;
    check(satchel_cpu_probe_read(a.get(),data_address+8,&sp,8) && sp==0,"fresh sibling SP defaults zero, never host stack");
    expect_stop(a.get(),SATCHEL_CPU_SVC);
    check(satchel_cpu_probe_read(b.get(),data_address,&sp,8) && sp==stack_address+128,"peer SP remains independent");
    check(satchel_cpu_probe_get_q(a.get(),31,got.data()) && got==q,"peer SIMD preserved across sibling execution");
    check(satchel_cpu_probe_resize_rw(a.get(),heap,4096)==SATCHEL_CPU_MAP_OK,"shared map creation");
    uint64_t value=0x1234,read=0;
    check(satchel_cpu_probe_write(b.get(),heap,&value,8) && satchel_cpu_probe_read(a.get(),heap,&read,8) && read==value,"sibling writes share one backing");
    check(satchel_cpu_probe_resize_rw(b.get(),heap,8192)==SATCHEL_CPU_MAP_OK && satchel_cpu_probe_read(a.get(),heap+4096,&read,8) && !read,"growth visible and zeroed in peer");
    satchel_cpu_probe_test_mapping_allocation_failure(1);
    check(satchel_cpu_probe_resize_rw(b.get(),heap,12288)==SATCHEL_CPU_MAP_ALLOCATION && !satchel_cpu_probe_read(a.get(),heap+8192,&read,8),"shared resize allocation failure preserves extents");
    check(satchel_cpu_probe_resize_rw(a.get(),heap,256*1024*1024)==SATCHEL_CPU_MAP_LIMIT,"shared context cannot bypass process aggregate limit");
    check(satchel_cpu_probe_resize_rw(a.get(),heap,0)==SATCHEL_CPU_MAP_OK && !satchel_cpu_probe_read(b.get(),heap,&read,8),"shared removal visible to peer");
    char error[160];
    for(uint64_t entry:{uint64_t{0},code_address+1,data_address,code_address+16})
        check(!satchel_cpu_probe_create_shared(a.get(),entry,error,sizeof(error)),"shared entry bounds/permissions checked before publication");
    check(!satchel_cpu_probe_create_shared(nullptr,code_address,error,sizeof(error)),"null shared peer rejected");
    uint64_t point=code_address;
    check(satchel_cpu_probe_set_pc(b.get(),point) && satchel_cpu_probe_set_breakpoints(a.get(),&point,1),"shared process trap registration");
    auto stop=satchel_cpu_probe_run(b.get(),10,nullptr,nullptr);
    check(stop.reason==SATCHEL_CPU_BREAKPOINT && !stop.steps,"peer observes process-wide trap");
    a.reset(); // Last-running peer may later disappear; memory/traps must survive.
    stop=satchel_cpu_probe_run(b.get(),10,nullptr,nullptr);
    check(stop.reason==SATCHEL_CPU_BREAKPOINT && satchel_cpu_probe_read(b.get(),data_address,&read,8),"parent destruction retains map and trap set");
    check(satchel_cpu_probe_set_breakpoints(b.get(),nullptr,0),"surviving context can clear process traps");
    auto c=shared(b.get(),code_address+12);expect_stop(c.get(),SATCHEL_CPU_UNSUPPORTED);
    check(!satchel_cpu_probe_create_shared(c.get(),point,error,sizeof(error)),"terminal context cannot seed a new context");
    satchel_cpu_probe_set_pc(b.get(),code_address+8);expect_stop(b.get(),SATCHEL_CPU_SVC);
    check(satchel_cpu_probe_run(c.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_UNSUPPORTED,"terminal fault remains context-local and persistent");
    c.reset(); // It was last run before b; no dangling reservation registry entry.
    satchel_cpu_probe_set_pc(b.get(),code_address+8);expect_stop(b.get(),SATCHEL_CPU_SVC);
    std::vector<Probe> siblings;
    for(unsigned i=1;i<SATCHEL_CPU_CONTEXT_LIMIT;i++)siblings.push_back(shared(b.get(),code_address+8));
    check(!satchel_cpu_probe_create_shared(b.get(),code_address,error,sizeof(error)),"bounded per-map context cap");
    expect_stop(siblings.back().get(),SATCHEL_CPU_SVC);siblings.pop_back();
    auto replacement=shared(b.get(),code_address+8);expect_stop(replacement.get(),SATCHEL_CPU_SVC);
    check(satchel_cpu_probe_try_destroy(nullptr),"null checked destruction succeeds");

    struct Reentry {SatchelCPUProbe *a,*b;unsigned rejected=0;bool threaded=false;};
    Reentry reentry{b.get(),replacement.get()};
    satchel_cpu_probe_set_pc(b.get(),code_address+8);
    stop=satchel_cpu_probe_run(b.get(),1,[](void *opaque)->int {
        auto &r=*static_cast<Reentry*>(opaque);uint64_t value=0x42,ro=0,rw=0;uint8_t q[16]{};char error[160];
        for(auto *p:{r.a,r.b}) {
            r.rejected+=!satchel_cpu_probe_set_x(p,0,0x42);r.rejected+=!satchel_cpu_probe_set_sp(p,0x42);
            r.rejected+=!satchel_cpu_probe_set_tls(p,0x42,0x42);r.rejected+=!satchel_cpu_probe_set_q(p,0,q);
            r.rejected+=!satchel_cpu_probe_write(p,data_address,&value,8);
            r.rejected+=!satchel_cpu_probe_get_x(p,0,&value);r.rejected+=!satchel_cpu_probe_get_tls(p,&ro,&rw);
            r.rejected+=!satchel_cpu_probe_get_q(p,0,q);r.rejected+=!satchel_cpu_probe_read(p,data_address,&value,8);
            r.rejected+=!satchel_cpu_probe_set_pc(p,code_address);r.rejected+=!satchel_cpu_probe_set_breakpoints(p,nullptr,0);
            r.rejected+=satchel_cpu_probe_resize_rw(p,heap,4096)==SATCHEL_CPU_MAP_BUSY;
            r.rejected+=!satchel_cpu_probe_create_shared(p,code_address,error,sizeof(error));
            r.rejected+=!satchel_cpu_probe_try_destroy(p);satchel_cpu_probe_destroy(p); // Must be a safe no-op.
            r.rejected+=satchel_cpu_probe_run(p,1,nullptr,nullptr).reason==SATCHEL_CPU_INTERNAL_FAULT;
        }
        // Host misuse, not parallel guest scheduling: a second caller must be
        // rejected by the process guard even though its thread_local is empty.
        std::thread worker([&r]{r.threaded=!satchel_cpu_probe_set_x(r.b,0,0x99) &&
            satchel_cpu_probe_run(r.b,1,nullptr,nullptr).reason==SATCHEL_CPU_INTERNAL_FAULT && !satchel_cpu_probe_try_destroy(r.b);});worker.join();
        return 1;
    },&reentry);
    check(stop.reason==SATCHEL_CPU_CANCELLED && !stop.steps && reentry.rejected==30 && reentry.threaded,"active process rejects all same/sibling/cross-host mutations and destruction");
    check(x(b.get(),0)==data_address+8 && x(replacement.get(),0)==0,"rejected reentry preserves register ownership");
    satchel_cpu_probe_set_pc(b.get(),code_address+8);expect_stop(b.get(),SATCHEL_CPU_SVC);
    satchel_cpu_probe_set_pc(replacement.get(),code_address+8);expect_stop(replacement.get(),SATCHEL_CPU_SVC);

    // A local reservation may survive slicing the same guest context, but not
    // an actual context switch, host write through any sibling, or map mutation.
    for(bool wide:{false,true})for(unsigned intervening=0;intervening<5;intervening++) {
        const uint32_t load=wide?0xc85ffc01:0x885ffc01,store=wide?0xc802fc01:0x8802fc01;
        uint64_t initial=7;
        auto p=make(words({load,store,0xd4000001,0xd503201f,0xd4000001}),&initial,8);
        auto other=shared(p.get(),code_address+12);
        check(satchel_cpu_probe_run(p.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET,"shared exclusive reservation setup");
        satchel_cpu_probe_set_x(p.get(),1,9);
        if(intervening==0)check(satchel_cpu_probe_run(other.get(),0,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET,"zero-budget inspection does not switch virtual context");
        if(intervening==1)check(satchel_cpu_probe_run(other.get(),1,nullptr,nullptr).steps==1,"actual sibling execution switches virtual context");
        if(intervening==2){uint64_t changed=33;check(satchel_cpu_probe_write(other.get(),data_address,&changed,8),"host write through sibling");}
        if(intervening==3)check(satchel_cpu_probe_resize_rw(other.get(),heap,4096)==SATCHEL_CPU_MAP_OK,"mapping change through sibling");
        if(intervening==4){check(satchel_cpu_probe_set_pc(other.get(),code_address+8),"sibling service boundary");expect_stop(other.get(),SATCHEL_CPU_SVC);other.reset();}
        expect_stop(p.get(),SATCHEL_CPU_SVC);uint64_t observed=0;
        check(satchel_cpu_probe_read(p.get(),data_address,&observed,8),"shared exclusive readback");
        if(intervening)check(x(p.get(),2)==1 && observed==(intervening==2?33:7),"context switch/write/map change invalidates reservation without false success");
        else check(x(p.get(),2)==0 && observed==9,"same-context slice and zero-budget sibling inspection preserve real exclusive success");
    }
}
static void multiwrite_tests() {
    std::array<uint8_t,64> original{},actual{};original.fill(0xa5);
    auto p=make(words({0xd503201f,0xd4000001}),original.data());auto sibling=shared(p.get());
    std::array<uint8_t,16> input{};for(size_t i=0;i<input.size();++i)input[i]=uint8_t(i+1);
    SatchelCPUProbeWrite writes[]={{data_address+8,input.data(),8},{data_address+32,input.data()+8,8}};
    check(satchel_cpu_probe_writev(sibling.get(),writes,2)==SATCHEL_CPU_WRITE_OK,"multi-span commit through sibling");
    auto expected=original;std::copy_n(input.begin(),8,expected.begin()+8);std::copy_n(input.begin()+8,8,expected.begin()+32);
    check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),64) && actual==expected,"all multi-write spans committed and canaries preserved");
    writes[1]={data_address+16,input.data(),8};
    check(satchel_cpu_probe_writev(p.get(),writes,2)==SATCHEL_CPU_WRITE_OK,"adjacent destinations and shared source accepted");
    for(unsigned scenario=0;scenario<9;++scenario) {
        check(satchel_cpu_probe_write(p.get(),data_address,original.data(),64),"reset original multi-write canaries");
        writes[0]={data_address+8,input.data(),8};writes[1]={data_address+32,input.data()+8,8};
        SatchelCPUWriteResult expected_status=SATCHEL_CPU_WRITE_INVALID;
        switch(scenario) {
            case 0:writes[1].address=code_address;expected_status=SATCHEL_CPU_WRITE_MEMORY;break;
            case 1:writes[1].address=data_address+64;expected_status=SATCHEL_CPU_WRITE_MEMORY;break;
            case 2:writes[1].address=data_address+60;expected_status=SATCHEL_CPU_WRITE_MEMORY;break;
            case 3:writes[1].bytes=nullptr;break;
            case 4:writes[1].size=0;break;
            case 5:writes[1].address=UINT64_MAX-3;break;
            case 6:writes[1].address=data_address+8;break;
            case 7:writes[1].address=data_address+12;break;
            case 8:writes[1].address=data_address+4;break;
        }
        check(satchel_cpu_probe_writev(p.get(),writes,2)==expected_status,"invalid second span returns explicit failure");
        check(satchel_cpu_probe_read(sibling.get(),data_address,actual.data(),64) && actual==original,"bad second span never partially commits first span");
    }
    writes[0]={data_address+8,input.data(),8};writes[1]={data_address+32,input.data()+8,8};
    check(satchel_cpu_probe_writev(nullptr,writes,2)==SATCHEL_CPU_WRITE_INVALID,"null multi-write context");
    check(satchel_cpu_probe_writev(p.get(),nullptr,1)==SATCHEL_CPU_WRITE_INVALID,"null descriptor array");
    check(satchel_cpu_probe_writev(p.get(),writes,0)==SATCHEL_CPU_WRITE_INVALID,"empty transaction rejected explicitly");
    check(satchel_cpu_probe_writev(p.get(),writes,9)==SATCHEL_CPU_WRITE_LIMIT &&
          satchel_cpu_probe_writev(p.get(),writes,SIZE_MAX)==SATCHEL_CPU_WRITE_LIMIT,"span count bounded before descriptor copy");
    satchel_cpu_probe_test_write_allocation_failure(1);
    check(satchel_cpu_probe_writev(p.get(),writes,2)==SATCHEL_CPU_WRITE_ALLOCATION,"snapshot allocation failure reported");
    check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),64) && actual==original,"allocation failure preserves all destinations");
    check(satchel_cpu_probe_writev(p.get(),writes,2)==SATCHEL_CPU_WRITE_OK,"transaction succeeds after one-shot allocation failure");

    // Sources are actual shared guest backing, without exposing native pointers
    // through any API. A sequential write/read implementation would fail swap.
    for(size_t i=0;i<original.size();++i)original[i]=uint8_t(i*3+5);
    satchel_cpu_probe_write(p.get(),data_address,original.data(),64);expected=original;
    for(unsigned i=0;i<16;i++)std::swap(expected[8+i],expected[40+i]);
    check(satchel_cpu_probe_test_swap(sibling.get(),data_address+8,data_address+40,16)==SATCHEL_CPU_WRITE_OK &&
          satchel_cpu_probe_read(p.get(),data_address,actual.data(),64) && actual==expected,"all aliased sources snapshotted before first destination commit");
    const auto snapshot=actual;satchel_cpu_probe_test_write_allocation_failure(1);
    check(satchel_cpu_probe_test_swap(p.get(),data_address+8,data_address+40,16)==SATCHEL_CPU_WRITE_ALLOCATION &&
          satchel_cpu_probe_read(sibling.get(),data_address,actual.data(),64) && actual==snapshot,"aliased source allocation failure remains atomic");

    constexpr uint64_t heap=0x7400000000;
    check(satchel_cpu_probe_resize_rw(p.get(),heap,SATCHEL_CPU_WRITE_BYTE_LIMIT+4096)==SATCHEL_CPU_MAP_OK,"bounded transaction backing");
    std::vector<uint8_t> large(SATCHEL_CPU_WRITE_BYTE_LIMIT+1,0x5a), readback(SATCHEL_CPU_WRITE_BYTE_LIMIT+1);
    std::array<SatchelCPUProbeWrite,8> maximum{};
    for(size_t i=0;i<maximum.size();++i)maximum[i]={heap+i*(SATCHEL_CPU_WRITE_BYTE_LIMIT/8),large.data()+i*(SATCHEL_CPU_WRITE_BYTE_LIMIT/8),SATCHEL_CPU_WRITE_BYTE_LIMIT/8};
    check(satchel_cpu_probe_writev(p.get(),maximum.data(),maximum.size())==SATCHEL_CPU_WRITE_OK,"exact eight-span two-MiB limit accepted");
    check(satchel_cpu_probe_read(sibling.get(),heap,readback.data(),readback.size()) &&
          std::equal(readback.begin(),readback.end()-1,large.begin()) && readback.back()==0,"full limit commit and trailing guard");
    maximum.back().size++;
    check(satchel_cpu_probe_writev(p.get(),maximum.data(),maximum.size())==SATCHEL_CPU_WRITE_LIMIT,"aggregate limit checked across all spans");
    SatchelCPUProbeWrite too_large={heap,large.data(),large.size()};
    check(satchel_cpu_probe_writev(p.get(),&too_large,1)==SATCHEL_CPU_WRITE_LIMIT,"single span transaction limit");
    check(satchel_cpu_probe_read(p.get(),heap+SATCHEL_CPU_WRITE_BYTE_LIMIT,readback.data(),1) && readback[0]==0,"rejected oversize leaves trailing guard untouched");
    // A transfer cannot cross mapping boundaries, even contiguous RW mappings.
    check(satchel_cpu_probe_resize_rw(p.get(),heap+SATCHEL_CPU_WRITE_BYTE_LIMIT+4096,4096)==SATCHEL_CPU_MAP_OK,"adjacent transaction mapping");
    SatchelCPUProbeWrite crossing={heap+SATCHEL_CPU_WRITE_BYTE_LIMIT+4096-4,input.data(),8};
    check(satchel_cpu_probe_writev(p.get(),&crossing,1)==SATCHEL_CPU_WRITE_MEMORY,"each span must fit one mapping");
    struct Busy {SatchelCPUProbe *a,*b;SatchelCPUProbeWrite *writes;bool okay=false;};Busy busy{p.get(),sibling.get(),writes};
    auto stop=satchel_cpu_probe_run(p.get(),1,[](void *opaque)->int {
        auto &b=*static_cast<Busy*>(opaque);
        b.okay=satchel_cpu_probe_writev(b.a,b.writes,2)==SATCHEL_CPU_WRITE_BUSY && satchel_cpu_probe_writev(b.b,b.writes,2)==SATCHEL_CPU_WRITE_BUSY;
        std::thread worker([&b]{b.okay=b.okay && satchel_cpu_probe_writev(b.b,b.writes,2)==SATCHEL_CPU_WRITE_BUSY;});worker.join();
        return 1;
    },&busy);
    check(stop.reason==SATCHEL_CPU_CANCELLED && busy.okay,"multi-write rejects same/sibling/cross-host active run");
    check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),64) && actual==snapshot,"busy transaction preserves canaries");

    for(bool wide:{false,true})for(unsigned event=0;event<4;event++) {
        const uint32_t load=wide?0xc85ffc01:0x885ffc01,store=wide?0xc802fc01:0x8802fc01;
        std::array<uint8_t,64> bytes{};uint64_t initial=7;std::memcpy(bytes.data(),&initial,8);
        auto a=make(words({load,store,0xd4000001}),bytes.data());auto b=shared(a.get());
        check(satchel_cpu_probe_run(a.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET,"multi-write exclusive precondition");
        satchel_cpu_probe_set_x(a.get(),1,9);uint64_t update=33;
        SatchelCPUProbeWrite parts[]={{data_address,&update,8},{data_address+16,&update,8}};
        if(event==1)parts[1].address=code_address;
        if(event==2)satchel_cpu_probe_test_write_allocation_failure(1);
        if(event==3)parts[1].address=data_address+4;
        SatchelCPUWriteResult status=satchel_cpu_probe_writev(b.get(),parts,2);
        check(status==(event==0?SATCHEL_CPU_WRITE_OK:event==1?SATCHEL_CPU_WRITE_MEMORY:event==2?SATCHEL_CPU_WRITE_ALLOCATION:SATCHEL_CPU_WRITE_INVALID),"transaction exclusive-path result");
        expect_stop(a.get(),SATCHEL_CPU_SVC);uint64_t value=0;
        check(satchel_cpu_probe_read(a.get(),data_address,&value,8),"transaction exclusive readback");
        check(x(a.get(),2)==(event==0?1:0) && value==(event==0?33:9),"successful transaction clears reservations; all rejected transactions preserve them");
    }
}
static void barrier_tests(const char *fixtures) {
    std::array<uint8_t,160> native{},actual{};native.fill(0xa5);
    auto p=make(fixture(fixtures,"barrier"),native.data(),native.size());
    const uint64_t reference=probe_barrier(native.data());expect_stop(p.get(),SATCHEL_CPU_SVC);
    check(reference==17 && x(p.get(),0)==reference && satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size()) && actual==native,
          "native all sixteen DMB options preserve flags/order/exclusives and full buffer guards");
    for(unsigned option=0;option<16;option++) {
        const uint32_t dmb=0xd50330bf|(option<<8);
        auto context=make(words({dmb,0xd4000001}));
        for(unsigned r=0;r<31;r++)satchel_cpu_probe_set_x(context.get(),r,UINT64_C(0x1234567800000000)+r);
        std::array<std::array<uint8_t,16>,32> q{};
        for(unsigned r=0;r<32;r++){q[r].fill(uint8_t(r*7+3));satchel_cpu_probe_set_q(context.get(),r,q[r].data());}
        satchel_cpu_probe_set_tls(context.get(),0x111,0x222);
        const auto result=satchel_cpu_probe_run(context.get(),1,nullptr,nullptr);
        check(result.reason==SATCHEL_CPU_BUDGET && result.steps==1 && result.pc==code_address+4,"DMB consumes exactly one instruction and advances guest PC");
        for(unsigned r=0;r<31;r++)check(x(context.get(),r)==UINT64_C(0x1234567800000000)+r,"DMB leaves every GPR unchanged");
        for(unsigned r=0;r<32;r++){std::array<uint8_t,16> got{};check(satchel_cpu_probe_get_q(context.get(),r,got.data()) && got==q[r],"DMB leaves SIMD unchanged");}
        uint64_t ro=0,rw=0;check(satchel_cpu_probe_get_tls(context.get(),&ro,&rw) && ro==0x111 && rw==0x222,"DMB leaves guest TLS unchanged");
        for(bool wide:{false,true}) {
            const uint32_t load=wide?0xc85ffc01:0x885ffc01,store=wide?0xc802fc01:0x8802fc01;
            uint64_t value=7;auto atomic=make(words({load,dmb,store,0xd4000001}),&value,8);
            check(satchel_cpu_probe_run(atomic.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET,"DMB exclusive setup");
            satchel_cpu_probe_set_x(atomic.get(),1,9);expect_stop(atomic.get(),SATCHEL_CPU_SVC);
            check(x(atomic.get(),2)==0 && satchel_cpu_probe_read(atomic.get(),data_address,&value,8) && value==9,"DMB does not clear W/X local reservation");
        }
        for(uint32_t neighbor:{0xd503309fu,0xd50330dfu,0xd50330ffu}) {
            if(neighbor==0xd503309f && (option&3))continue; // Reviewed named DSB now has dedicated native tests.
            const uint32_t op=neighbor|(option<<8);auto denied=make(words({op}));
            if(op==0xd5033fdf) {const auto step=satchel_cpu_probe_run(denied.get(),1,nullptr,nullptr);check(step.reason==SATCHEL_CPU_BUDGET&&step.steps==1&&step.pc==code_address+4,"formerly excluded exact ISB SY now has explicit native/state coverage");}
            else expect_stop(denied.get(),SATCHEL_CPU_UNSUPPORTED);
        }
    }
    // All low-byte neighbors stay in the system-instruction region; the exact
    // Exact DMB, reviewed named DSB and CLREX have dedicated semantics tests.
    for(unsigned low=0;low<256;low++)if(low!=0xbf && low!=0x9f && low!=0x5f) {
        auto denied=make(words({0xd5033f00u|low}));
        if(low==0xdf) {const auto step=satchel_cpu_probe_run(denied.get(),1,nullptr,nullptr);check(step.reason==SATCHEL_CPU_BUDGET&&step.steps==1&&step.pc==code_address+4,"exact ISB SY fixed bits admitted without widening system neighbors");}
        else expect_stop(denied.get(),SATCHEL_CPU_UNSUPPORTED);
    }
    auto shared_memory=make(words({0xf9000001,0xd5033fbf,0xd4000001,0xd5033bbf,0xf9400002,0xd4000001}));
    auto reader=shared(shared_memory.get(),code_address+12);satchel_cpu_probe_set_x(reader.get(),0,data_address);
    satchel_cpu_probe_set_x(shared_memory.get(),1,0x7788);expect_stop(shared_memory.get(),SATCHEL_CPU_SVC);expect_stop(reader.get(),SATCHEL_CPU_SVC);
    check(x(reader.get(),2)==0x7788,"serialized sibling observes prior committed normal RAM across DMB boundaries");
    auto stopped=make(words({0xd5033fbf,0xd4000001}));uint64_t address=code_address;
    satchel_cpu_probe_set_breakpoints(stopped.get(),&address,1);
    auto result=satchel_cpu_probe_run(stopped.get(),1,nullptr,nullptr);
    check(result.reason==SATCHEL_CPU_BREAKPOINT && !result.steps,"breakpoint precedes DMB execution");
    result=satchel_cpu_probe_run(stopped.get(),1,[](void*)->int{return 1;},nullptr);
    check(result.reason==SATCHEL_CPU_CANCELLED && !result.steps,"cancellation precedes DMB execution");
}
static void dsb_tests(const char *fixtures) {
    std::array<uint8_t,160> native{},actual{};native.fill(0xa5);
    auto p=make(fixture(fixtures,"dsb"),native.data(),native.size());
    const uint64_t reference=probe_dsb(native.data());expect_stop(p.get(),SATCHEL_CPU_SVC);
    check(reference==13 && x(p.get(),0)==reference && satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size()) && actual==native,
          "native all twelve named DSB options preserve flags/order/exclusives and full buffer guards");
    for(unsigned option=0;option<16;option++) {
        if(!(option&3))continue;
        const uint32_t dsb=0xd503309f|(option<<8);
        auto context=make(words({dsb,0x910003e9,0xd4000001}));
        for(unsigned r=0;r<31;r++)satchel_cpu_probe_set_x(context.get(),r,UINT64_C(0x1234567800000000)+r);
        std::array<std::array<uint8_t,16>,32> q{};
        for(unsigned r=0;r<32;r++){q[r].fill(uint8_t(r*7+3));satchel_cpu_probe_set_q(context.get(),r,q[r].data());}
        satchel_cpu_probe_set_tls(context.get(),0x111,0x222);
        uint32_t before_fpcr=0,before_fpsr=0;
        check(satchel_cpu_probe_get_fp_status(context.get(),&before_fpcr,&before_fpsr)==SATCHEL_CPU_FP_OK,"DSB known FP before");
        check(satchel_cpu_probe_set_sp(context.get(),0x50000),"DSB SP setup");
        const auto result=satchel_cpu_probe_run(context.get(),1,nullptr,nullptr);
        check(result.reason==SATCHEL_CPU_BUDGET && result.steps==1 && result.pc==code_address+4,"DSB consumes exactly one instruction and advances guest PC");
        uint32_t after_fpcr=0,after_fpsr=0;
        check(satchel_cpu_probe_get_fp_status(context.get(),&after_fpcr,&after_fpsr)==SATCHEL_CPU_FP_OK && before_fpcr==after_fpcr && before_fpsr==after_fpsr,"DSB preserves owned FP state");
        for(unsigned r=0;r<31;r++)check(x(context.get(),r)==UINT64_C(0x1234567800000000)+r,"DSB leaves every GPR unchanged");
        for(unsigned r=0;r<32;r++){std::array<uint8_t,16> got{};check(satchel_cpu_probe_get_q(context.get(),r,got.data()) && got==q[r],"DSB leaves SIMD unchanged");}
        uint64_t ro=0,rw=0;check(satchel_cpu_probe_get_tls(context.get(),&ro,&rw) && ro==0x111 && rw==0x222,"DSB leaves guest TLS unchanged");
        check(satchel_cpu_probe_run(context.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET && x(context.get(),9)==0x50000,"DSB preserves SP read by original MOV");
        for(bool wide:{false,true}) {
            const uint32_t load=wide?0xc85ffc01:0x885ffc01,store=wide?0xc802fc01:0x8802fc01;
            uint64_t value=7;auto atomic=make(words({load,dsb,store,0xd4000001}),&value,8);
            check(satchel_cpu_probe_run(atomic.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET,"DSB exclusive setup");
            satchel_cpu_probe_set_x(atomic.get(),1,9);expect_stop(atomic.get(),SATCHEL_CPU_SVC);
            check(x(atomic.get(),2)==0 && satchel_cpu_probe_read(atomic.get(),data_address,&value,8) && value==9,"DSB does not clear W/X local reservation");
        }
    }
    for(unsigned option:{0u,4u,8u,12u}) {
        auto denied=make(words({0xd503309fu|(option<<8)}));expect_stop(denied.get(),SATCHEL_CPU_UNSUPPORTED);
    }
    // nXS/speculation/altered fixed bits stay stopped; exact ISB SY is now positive.
    for(uint32_t denied_op:{0xd503323fu,0xd503363fu,0xd5033a3fu,0xd5033e3fu,0xd5033fdfu,0xd5033f9eu,0xd5032f9fu}) {
        auto denied=make(words({denied_op}));
        if(denied_op==0xd5033fdf) {const auto step=satchel_cpu_probe_run(denied.get(),1,nullptr,nullptr);check(step.reason==SATCHEL_CPU_BUDGET&&step.steps==1&&step.pc==code_address+4,"DSB neighbor exact ISB SY now separately implemented");}
        else expect_stop(denied.get(),SATCHEL_CPU_UNSUPPORTED);
    }
    auto shared_memory=make(words({0xf9000001,0xd5033f9f,0xd4000001,0xd5033b9f,0xf9400002,0xd4000001}));
    auto reader=shared(shared_memory.get(),code_address+12);satchel_cpu_probe_set_x(reader.get(),0,data_address);
    satchel_cpu_probe_set_x(shared_memory.get(),1,0x7788);expect_stop(shared_memory.get(),SATCHEL_CPU_SVC);expect_stop(reader.get(),SATCHEL_CPU_SVC);
    check(x(reader.get(),2)==0x7788,"serialized sibling observes prior committed normal RAM across DSB boundaries");
    auto stopped=make(words({0xd5033f9f,0xd4000001}));uint64_t address=code_address;
    satchel_cpu_probe_set_breakpoints(stopped.get(),&address,1);
    auto result=satchel_cpu_probe_run(stopped.get(),1,nullptr,nullptr);
    check(result.reason==SATCHEL_CPU_BREAKPOINT && !result.steps,"breakpoint precedes DSB execution");
    result=satchel_cpu_probe_run(stopped.get(),1,[](void*)->int{return 1;},nullptr);
    check(result.reason==SATCHEL_CPU_CANCELLED && !result.steps,"cancellation precedes DSB execution");
}
static void isb_tests(const char *fixtures) {
    std::array<uint8_t,96> native{},actual{};native.fill(0xa5);
    const uint64_t patterns[2]={UINT64_C(0x7ff0000000000001),UINT64_C(0x8000000000000000)};
    std::memcpy(native.data()+32,patterns,16);
    auto p=make(fixture(fixtures,"isb"),native.data(),native.size());
    const int host_round=fegetround(),host_exceptions=fetestexcept(FE_ALL_EXCEPT);
    const uint64_t reference=probe_isb(native.data());expect_stop(p.get(),SATCHEL_CPU_SVC);
    check(reference==2&&x(p.get(),0)==2&&satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==native,
          "native ISB SY/default preserves order/flags/Q bits/exclusive progress and complete guarded buffer");
    check(fegetround()==host_round&&fetestexcept(FE_ALL_EXCEPT)==host_exceptions,"ISB does not change host floating environment");
    for(unsigned mode=0;mode<4;mode++) {
        auto context=make(words({0xd5033fdf,0x910003e9,0xd4000001}));
        check(satchel_cpu_probe_test_conversion_state(context.get(),mode),"ISB fixture owns actual known or unknown FP status");
        uint32_t before_fpcr=0xa7a7a7a7,before_fpsr=0xb8b8b8b8;
        const auto before_status=satchel_cpu_probe_get_fp_status(context.get(),&before_fpcr,&before_fpsr);
        check(before_status==(mode==1?SATCHEL_CPU_FP_UNKNOWN:SATCHEL_CPU_FP_OK),"ISB FP fixture status is explicit");
        for(unsigned r=0;r<31;r++)check(satchel_cpu_probe_set_x(context.get(),r,UINT64_C(0x1234567800000000)+r),"ISB GPR fixture");
        std::array<std::array<uint8_t,16>,32> q{};
        for(unsigned r=0;r<32;r++){q[r].fill(uint8_t(r*7+3));check(satchel_cpu_probe_set_q(context.get(),r,q[r].data()),"ISB SIMD fixture");}
        check(satchel_cpu_probe_set_tls(context.get(),0x111,0x222)&&satchel_cpu_probe_set_sp(context.get(),0x50000),"ISB thread register fixture");
        const auto result=satchel_cpu_probe_run(context.get(),1,nullptr,nullptr);
        check(result.reason==SATCHEL_CPU_BUDGET&&result.steps==1&&result.pc==code_address+4,"exact ISB SY consumes one instruction and advances PC");
        for(unsigned r=0;r<31;r++)check(x(context.get(),r)==UINT64_C(0x1234567800000000)+r,"ISB preserves every GPR");
        for(unsigned r=0;r<32;r++){std::array<uint8_t,16> got{};check(satchel_cpu_probe_get_q(context.get(),r,got.data())&&got==q[r],"ISB preserves every SIMD bit");}
        uint64_t ro=0,rw=0;check(satchel_cpu_probe_get_tls(context.get(),&ro,&rw)&&ro==0x111&&rw==0x222,"ISB preserves both TLS registers");
        uint32_t after_fpcr=0xa7a7a7a7,after_fpsr=0xb8b8b8b8;
        check(satchel_cpu_probe_get_fp_status(context.get(),&after_fpcr,&after_fpsr)==before_status&&after_fpcr==before_fpcr&&after_fpsr==before_fpsr,
              "ISB preserves FPCR/FPSR and unknown-status state without inventing conversion support");
        check(satchel_cpu_probe_run(context.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET&&x(context.get(),9)==0x50000,"ISB preserves SP read by original MOV");
    }
    for(unsigned option=0;option<15;option++) {
        auto refused=make(words({0xd50330dfu|(option<<8)}));satchel_cpu_probe_set_x(refused.get(),3,0x1234);
        const auto stopped=satchel_cpu_probe_run(refused.get(),1,nullptr,nullptr);
        check(stopped.reason==SATCHEL_CPU_UNSUPPORTED&&stopped.steps==0&&stopped.pc==code_address&&x(refused.get(),3)==0x1234,
              "reserved ISB options remain unsupported atomically in the exact SY profile");
    }
    // Public system encodings neighboring ISB, not original game instructions.
    for(uint32_t op:{0xd5033fdeu,0xd5033fceu,0xd5032fdfu,0xd5033fffu,0xd5233fdfu,0xd5033fefu}) {
        auto refused=make(words({op}));expect_stop(refused.get(),SATCHEL_CPU_UNSUPPORTED);
    }
    for(bool wide:{false,true}) {
        uint64_t memory=7;const uint32_t load=wide?0xc85ffc01u:0x885ffc01u,store=wide?0xc802fc01u:0x8802fc01u;
        auto exclusive=make(words({load,0xd5033fdf,store,0xd4000001}),&memory,8);
        check(satchel_cpu_probe_run(exclusive.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET,"ISB original exclusive setup");
        check(satchel_cpu_probe_set_x(exclusive.get(),1,9),"ISB replacement value");expect_stop(exclusive.get(),SATCHEL_CPU_SVC);
        check(x(exclusive.get(),2)==0&&satchel_cpu_probe_read(exclusive.get(),data_address,&memory,8)&&memory==9,"ISB preserves W/X local exclusive reservation");
        auto absent=make(words({0xd5033fdf,store,0xd4000001}),&memory,8);satchel_cpu_probe_set_x(absent.get(),1,11);expect_stop(absent.get(),SATCHEL_CPU_SVC);
        check(x(absent.get(),2)==1&&satchel_cpu_probe_read(absent.get(),data_address,&memory,8)&&memory==9,"ISB never creates an exclusive reservation");
    }
    // Existing admitted context state is already committed before the next
    // checked fetch; ISB does not enable new privileged system operations.
    auto tls=make(words({0xd51bd041,0xd5033fdf,0xd53bd042,0xd4000001}));satchel_cpu_probe_set_x(tls.get(),1,0x9876);expect_stop(tls.get(),SATCHEL_CPU_SVC);
    check(x(tls.get(),2)==0x9876,"MSR TPIDR_EL0 precedes ISB and a fresh MRS observes committed owned TLS");
    auto siblings=make(words({0xf9000001,0xd5033fdf,0xd4000001,0xd5033fdf,0xf9400002,0xd4000001}));
    auto reader=shared(siblings.get(),code_address+12);satchel_cpu_probe_set_x(reader.get(),0,data_address);satchel_cpu_probe_set_x(siblings.get(),1,0x7788);
    expect_stop(siblings.get(),SATCHEL_CPU_SVC);expect_stop(reader.get(),SATCHEL_CPU_SVC);
    check(x(reader.get(),2)==0x7788,"serialized sibling sees already committed normal RAM without inventing GPU completion");
    auto guarded=make(words({0xd5033fdf,0xd4000001}));uint64_t address=code_address;
    check(satchel_cpu_probe_set_breakpoints(guarded.get(),&address,1),"ISB breakpoint setup");
    auto result=satchel_cpu_probe_run(guarded.get(),1,nullptr,nullptr);check(result.reason==SATCHEL_CPU_BREAKPOINT&&!result.steps&&result.pc==code_address,"breakpoint stops before ISB");
    result=satchel_cpu_probe_run(guarded.get(),1,[](void*)->int{return 1;},nullptr);check(result.reason==SATCHEL_CPU_CANCELLED&&!result.steps&&result.pc==code_address,"cancellation stops before ISB");
    check(satchel_cpu_probe_set_breakpoints(guarded.get(),nullptr,0),"ISB breakpoint clear");
    result=satchel_cpu_probe_run(guarded.get(),0,nullptr,nullptr);check(result.reason==SATCHEL_CPU_BUDGET&&!result.steps&&result.pc==code_address,"zero budget leaves ISB pending");
    result=satchel_cpu_probe_run(guarded.get(),1,nullptr,nullptr);check(result.reason==SATCHEL_CPU_BUDGET&&result.steps==1&&result.pc==code_address+4,"resumed ISB executes exactly once");
}

static void clrex_tests(const char *fixtures) {
    std::array<uint64_t,8> native{},actual{};native.fill(UINT64_C(0xa5a5a5a500000007));
    auto p=make(fixture(fixtures,"clrex"),native.data(),sizeof(native));
    const uint64_t reference=probe_clrex(native.data());expect_stop(p.get(),SATCHEL_CPU_SVC);
    check(reference==1 && x(p.get(),0)==reference &&
          satchel_cpu_probe_read(p.get(),data_address,actual.data(),sizeof(actual)) && actual==native &&
          actual[0]==UINT64_C(0xa5a5a5a500000008),
          "native all sixteen CLREX immediates invalidate W/X reservations, preserve NZCV/guards, and allow fresh retry");
    for(unsigned option=0;option<16;option++) {
        const uint32_t clrex=0xd503305f|(option<<8);
        auto context=make(words({clrex,0x910003e9,0xd4000001}));
        for(unsigned r=0;r<31;r++)satchel_cpu_probe_set_x(context.get(),r,UINT64_C(0x1234567800000000)+r);
        std::array<std::array<uint8_t,16>,32> q{};
        for(unsigned r=0;r<32;r++){q[r].fill(uint8_t(r*7+3));satchel_cpu_probe_set_q(context.get(),r,q[r].data());}
        satchel_cpu_probe_set_tls(context.get(),0x111,0x222);
        uint32_t fpcr=0,fpsr=0;
        check(satchel_cpu_probe_get_fp_status(context.get(),&fpcr,&fpsr)==SATCHEL_CPU_FP_OK,"CLREX owned FP before");
        check(satchel_cpu_probe_set_sp(context.get(),0x50000),"CLREX SP setup");
        auto result=satchel_cpu_probe_run(context.get(),1,nullptr,nullptr);
        check(result.reason==SATCHEL_CPU_BUDGET && result.steps==1 && result.pc==code_address+4,"CLREX without reservation consumes one instruction");
        uint32_t after_fpcr=0,after_fpsr=0;
        check(satchel_cpu_probe_get_fp_status(context.get(),&after_fpcr,&after_fpsr)==SATCHEL_CPU_FP_OK && fpcr==after_fpcr && fpsr==after_fpsr,"CLREX preserves owned FP state");
        for(unsigned r=0;r<31;r++)check(x(context.get(),r)==UINT64_C(0x1234567800000000)+r,"CLREX preserves every GPR");
        for(unsigned r=0;r<32;r++){std::array<uint8_t,16> got{};check(satchel_cpu_probe_get_q(context.get(),r,got.data()) && got==q[r],"CLREX preserves SIMD");}
        uint64_t ro=0,rw=0;check(satchel_cpu_probe_get_tls(context.get(),&ro,&rw) && ro==0x111 && rw==0x222,"CLREX preserves TLS");
        check(satchel_cpu_probe_run(context.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET && x(context.get(),9)==0x50000,"CLREX preserves SP");
        for(bool wide:{false,true})for(bool acquire:{false,true}) {
            const uint32_t load=(wide?0xc85f7c01u:0x885f7c01u)|(acquire?0x8000:0);
            const uint32_t store=(wide?0xc8027c03u:0x88027c03u)|(acquire?0x8000:0);
            uint64_t value=UINT64_C(0xabcdef1200000007);
            auto atomic=make(words({load,clrex,clrex,store,load,store,0xd4000001}),&value,8);
            satchel_cpu_probe_set_x(atomic.get(),3,9);
            check(satchel_cpu_probe_run(atomic.get(),4,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET,"CLREX repeated-clear sequence");
            uint64_t observed=0;
            check(x(atomic.get(),2)==1 && satchel_cpu_probe_read(atomic.get(),data_address,&observed,8) && observed==value,"CLREX forces failed STXR/STLXR with no write");
            expect_stop(atomic.get(),SATCHEL_CPU_SVC);
            check(x(atomic.get(),2)==0 && satchel_cpu_probe_read(atomic.get(),data_address,&observed,8) && observed==(wide?9:UINT64_C(0xabcdef1200000009)),"fresh LDXR/LDAXR establishes reservation after CLREX");
        }
    }
    // A different process retains its independent monitor while CLREX executes.
    uint64_t initial=7;
    auto independent=make(words({0xc85f7c01,0xc8027c03,0xd4000001}),&initial,8);
    auto clearer=make(words({0xd5033f5f,0xd4000001}),&initial,8);
    satchel_cpu_probe_set_x(independent.get(),3,9);
    check(satchel_cpu_probe_run(independent.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET,"independent process exclusive setup");
    expect_stop(clearer.get(),SATCHEL_CPU_SVC);expect_stop(independent.get(),SATCHEL_CPU_SVC);
    check(x(independent.get(),2)==0,"CLREX never clears another process monitor");
    auto parent=make(words({0xd5033f5f,0xd4000001}),&initial,8);auto sibling=shared(parent.get());
    satchel_cpu_probe_set_x(sibling.get(),7,0x1234);satchel_cpu_probe_set_tls(sibling.get(),0x111,0x222);
    expect_stop(parent.get(),SATCHEL_CPU_SVC);
    uint64_t observed=0,ro=0,rw=0;
    check(x(sibling.get(),7)==0x1234 && satchel_cpu_probe_get_tls(sibling.get(),&ro,&rw) && ro==0x111 && rw==0x222 &&
          satchel_cpu_probe_read(sibling.get(),data_address,&observed,8) && observed==initial,"CLREX leaves sibling register/TLS and shared bytes unchanged");
    // Scheduling a sibling already invalidates reservations conservatively;
    // CLREX itself must not add process-wide invalidation or memory writes.
    for(unsigned bit=0;bit<32;bit++)if(bit<8 || bit>=12) {
        if(bit==7) {
            const uint32_t instruction=0xd5033f5fu^(1u<<bit);
            check(instruction==0xd5033fdf,"CLREX bit7 neighbor is exact public ISB SY");
            auto barrier=make(words({0xc85f7c01,instruction,0xc8027c03,0xd4000001}),&initial,8);
            check(satchel_cpu_probe_set_x(barrier.get(),3,9)&&satchel_cpu_probe_set_x(barrier.get(),7,0x1234),"derived ISB fixture state");
            check(satchel_cpu_probe_run(barrier.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET,"derived ISB has original live exclusive reservation");
            const auto result=satchel_cpu_probe_run(barrier.get(),1,nullptr,nullptr);
            check(result.reason==SATCHEL_CPU_BUDGET&&result.steps==1&&result.pc==code_address+8,"derived exact ISB executes once and advances correct PC");
            uint64_t word=0;
            check(x(barrier.get(),0)==data_address&&x(barrier.get(),1)==initial&&x(barrier.get(),3)==9&&x(barrier.get(),7)==0x1234&&
                  satchel_cpu_probe_read(barrier.get(),data_address,&word,8)&&word==initial,"derived ISB leaves loaded/register/memory state unchanged");
            expect_stop(barrier.get(),SATCHEL_CPU_SVC);
            check(x(barrier.get(),2)==0&&satchel_cpu_probe_read(barrier.get(),data_address,&word,8)&&word==9,"derived ISB does not masquerade as CLREX reservation clearing");
            continue;
        }
        if(bit==26 || bit==30) {
            // Flipping these major decode bits creates already-supported SUB
            // immediate or BL, not a reserved CLREX. Verify normal dispatch and
            // that neither is accidentally handled as reservation clearing.
            auto ordinary=make(words({0xc85f7c01,0xd5033f5fu^(1u<<bit),0xc8027c03,0xd4000001}),&initial,8);
            satchel_cpu_probe_set_x(ordinary.get(),3,9);
            auto result=satchel_cpu_probe_run(ordinary.get(),2,nullptr,nullptr);
            check(result.reason==SATCHEL_CPU_BUDGET && result.steps==2,"major CLREX mutation follows existing SUB/BL decoder");
            check(satchel_cpu_probe_set_pc(ordinary.get(),code_address+8),"restore original store PC after tested BL");
            expect_stop(ordinary.get(),SATCHEL_CPU_SVC);
            check(x(ordinary.get(),2)==0,"SUB/BL encoding mutation does not clear reservation");
            continue;
        }
        auto denied=make(words({0xd5033f5fu^(1u<<bit)}));
        auto result=satchel_cpu_probe_run(denied.get(),1,nullptr,nullptr);
        check(result.reason==SATCHEL_CPU_UNSUPPORTED && !result.steps,"CLREX altered fixed bits rejected before side effects");
    }
    for(unsigned event=0;event<3;event++) {
        auto stopped=make(words({0xc85f7c01,0xd5033f5f,0xc8027c03,0xd4000001}),&initial,8);
        satchel_cpu_probe_set_x(stopped.get(),3,9);satchel_cpu_probe_run(stopped.get(),1,nullptr,nullptr);
        uint64_t breakpoint=code_address+4;
        if(event==1)satchel_cpu_probe_set_breakpoints(stopped.get(),&breakpoint,1);
        auto result=satchel_cpu_probe_run(stopped.get(),event==0?0:1,event==2?+[](void*)->int{return 1;}:nullptr,nullptr);
        check(!result.steps && result.reason==(event==0?SATCHEL_CPU_BUDGET:event==1?SATCHEL_CPU_BREAKPOINT:SATCHEL_CPU_CANCELLED),"budget/breakpoint/cancel precede CLREX side effects");
        satchel_cpu_probe_set_breakpoints(stopped.get(),nullptr,0);satchel_cpu_probe_set_pc(stopped.get(),code_address+8);
        expect_stop(stopped.get(),SATCHEL_CPU_SVC);
        check(x(stopped.get(),2)==0,"stopped-before-CLREX preserves reservation");
    }
}
static void range_mapping_tests() {
    constexpr uint64_t page=4096, base=0x7500000000;
    auto p=make(words({0xd503201f,0xd4000001}));auto sibling=shared(p.get());
    uint64_t changed=99;
    auto map=[&](uint64_t at,uint64_t size){return satchel_cpu_probe_map_missing_rw(p.get(),at,size,&changed);};
    auto unmap=[&](uint64_t at,uint64_t size){return satchel_cpu_probe_unmap_rw(sibling.get(),at,size,&changed);};
    check(map(base+page,2*page)==SATCHEL_CPU_MAP_OK && changed==2*page,"range map creates actual missing pages");
    std::vector<uint8_t> data(2*page,0x51),actual(2*page);
    check(satchel_cpu_probe_read(p.get(),base+page,actual.data(),actual.size()) && std::all_of(actual.begin(),actual.end(),[](uint8_t b){return b==0;}),"new physical pages zeroed");
    check(satchel_cpu_probe_write(p.get(),base+page,data.data(),data.size()),"write initial dynamic page canaries");
    check(map(base+2*page,2*page)==SATCHEL_CPU_MAP_OK && changed==page,"overlap adds only missing page");
    check(satchel_cpu_probe_check_range(sibling.get(),base+page,3*page,SATCHEL_CPU_READ|SATCHEL_CPU_WRITE) &&
          satchel_cpu_probe_read(p.get(),base+page,actual.data(),actual.size()) && actual==data,"intersected backing coalesced including old prefix");
    uint64_t zero=7;check(satchel_cpu_probe_read(p.get(),base+3*page,&zero,8) && !zero,"coalesced new tail zeroed");
    check(map(base,4*page)==SATCHEL_CPU_MAP_OK && changed==page && satchel_cpu_probe_check_range(p.get(),base,4*page,SATCHEL_CPU_WRITE),"explicit expanded request joins prefix for cross-page access");
    uint64_t crossing=0x1122334455667788,readback=0;
    check(satchel_cpu_probe_write(p.get(),base+page-3,&crossing,8) && satchel_cpu_probe_read(sibling.get(),base+page-3,&readback,8) && readback==crossing,"coalesced backing permits genuine cross-page load/store span");
    for(unsigned stage:{1u,2u}) {
        changed=99;satchel_cpu_probe_test_mapping_allocation_failure(stage);
        check(unmap(base+page,page)==SATCHEL_CPU_MAP_ALLOCATION && changed==99,"split allocation failure preserves output");
        check(satchel_cpu_probe_check_range(p.get(),base,4*page,SATCHEL_CPU_WRITE) &&
              satchel_cpu_probe_read(p.get(),base+page-3,&readback,8) && readback==crossing,"failed split preserves map and byte canaries");
        satchel_cpu_probe_test_mapping_allocation_failure(stage);
        check(map(base,5*page)==SATCHEL_CPU_MAP_ALLOCATION && changed==99,"growth failure atomic before and after staged copies");
        check(!satchel_cpu_probe_check_range(p.get(),base+4*page,1,SATCHEL_CPU_READ) &&
              satchel_cpu_probe_read(p.get(),base+page-3,&readback,8) && readback==crossing,"failed merged map publishes no added page");
    }
    check(unmap(base+page,page)==SATCHEL_CPU_MAP_OK && changed==page &&
          satchel_cpu_probe_check_range(p.get(),base,page,SATCHEL_CPU_READ) &&
          !satchel_cpu_probe_check_range(p.get(),base+page,1,SATCHEL_CPU_READ) &&
          satchel_cpu_probe_check_range(p.get(),base+2*page,2*page,SATCHEL_CPU_READ),"middle unmap creates exact surviving fragments");
    check(map(base,4*page)==SATCHEL_CPU_MAP_OK && changed==page && satchel_cpu_probe_check_range(p.get(),base,4*page,SATCHEL_CPU_WRITE),"remapping gap reunites fragments");
    check(satchel_cpu_probe_read(p.get(),base+page,&zero,8) && !zero,"remapped page zeroed instead of resurrecting old contents");
    check(satchel_cpu_probe_read(p.get(),base+2*page,&zero,8) && zero==0x5151515151515151,"surviving suffix retains contents");
    check(map(base,4*page)==SATCHEL_CPU_MAP_OK && changed==0,"fully mapped range is exact no-op");
    check(unmap(base-page,6*page)==SATCHEL_CPU_MAP_OK && changed==4*page,"unmap skips free gaps and counts only actual backing");
    check(unmap(base,4*page)==SATCHEL_CPU_MAP_OK && changed==0,"unmapped range removal is no-op");
    auto cross=make(words({0xf9400001,0xf9000401,0xd4000001}));
    check(satchel_cpu_probe_map_missing_rw(cross.get(),base,page,&changed)==SATCHEL_CPU_MAP_OK &&
          satchel_cpu_probe_map_missing_rw(cross.get(),base,2*page,&changed)==SATCHEL_CPU_MAP_OK,"interpreted cross-page map setup");
    satchel_cpu_probe_set_x(cross.get(),0,base+page-3);
    check(satchel_cpu_probe_write(cross.get(),base+page-3,&crossing,8),"interpreted crossing input");
    expect_stop(cross.get(),SATCHEL_CPU_SVC);
    check(x(cross.get(),1)==crossing && satchel_cpu_probe_read(cross.get(),base+page+5,&readback,8) && readback==crossing,
          "actual interpreted LDR/STR spans coalesced page boundary");
    // Adjacent independently managed heap keeps its exact-base resize identity.
    check(satchel_cpu_probe_resize_rw(p.get(),base+8*page,2*page)==SATCHEL_CPU_MAP_OK,"separate heap created");
    check(map(base+7*page,page)==SATCHEL_CPU_MAP_OK && changed==page,"adjacent outside heap not absorbed");
    check(!satchel_cpu_probe_check_range(p.get(),base+7*page,2*page,SATCHEL_CPU_READ) &&
          satchel_cpu_probe_resize_rw(p.get(),base+8*page,3*page)==SATCHEL_CPU_MAP_OK,"separate heap base still resizable");
    check(unmap(base+7*page,page)==SATCHEL_CPU_MAP_OK && satchel_cpu_probe_resize_rw(p.get(),base+8*page,0)==SATCHEL_CPU_MAP_OK,"range removal leaves adjacent heap ownership intact");
    for(bool add:{false,true}) {
        auto operation=add?satchel_cpu_probe_map_missing_rw:satchel_cpu_probe_unmap_rw;
        for(auto range:std::array<std::pair<uint64_t,uint64_t>,7>{{{0,page},{base+1,page},{base,page+1},{base,0},{UINT64_MAX-4095,page},{base,UINT64_MAX},{code_address,page}}}) {
            changed=99;check(operation(p.get(),range.first,range.second,&changed)==SATCHEL_CPU_MAP_INVALID && changed==99,"invalid/immutable range preserves changed output");
        }
        check(operation(nullptr,base,page,&changed)==SATCHEL_CPU_MAP_INVALID &&
              operation(p.get(),base,page,nullptr)==SATCHEL_CPU_MAP_INVALID,"null range API arguments rejected");
    }
    changed=99;check(map(base,256*1024*1024)==SATCHEL_CPU_MAP_LIMIT && changed==99,"aggregate backing limit preflight preserves output");
    // Independent page model; arbitrary maps/removals, holes and partial overlap.
    std::array<uint8_t,20> tags{};uint32_t random=0x12345678;
    for(unsigned turn=0;turn<160;turn++) {
        random=random*1664525u+1013904223u;unsigned first=(random>>16)%tags.size();
        random=random*1664525u+1013904223u;unsigned count=1+(random>>16)%(tags.size()-first);
        bool add=(random>>31)!=0;uint64_t expected=0;
        for(unsigned i=first;i<first+count;i++)expected+=(add?!tags[i]:bool(tags[i]))?page:0;
        auto operation=add?satchel_cpu_probe_map_missing_rw:satchel_cpu_probe_unmap_rw;
        if(expected && turn%3==0)for(unsigned stage:{1u,2u}) {
            changed=99;satchel_cpu_probe_test_mapping_allocation_failure(stage);
            check(operation(p.get(),base+first*page,count*page,&changed)==SATCHEL_CPU_MAP_ALLOCATION && changed==99,"modeled map transaction allocation failure atomic");
            for(unsigned i=0;i<tags.size();i++) {
                std::array<uint8_t,4096> bytes{};
                bool present=satchel_cpu_probe_read(p.get(),base+i*page,bytes.data(),bytes.size());
                check(present==bool(tags[i]) && (!present || std::all_of(bytes.begin(),bytes.end(),[&](uint8_t b){return b==tags[i];})),"failure leaves modeled full pages unchanged");
            }
        }
        check(operation(p.get(),base+first*page,count*page,&changed)==SATCHEL_CPU_MAP_OK && changed==expected,"range result equals independent changed-page count");
        for(unsigned i=0;i<tags.size();i++) {
            bool affected=i>=first && i<first+count;bool present=affected?add:bool(tags[i]);
            std::array<uint8_t,4096> bytes{};
            bool readable=satchel_cpu_probe_read(sibling.get(),base+i*page,bytes.data(),bytes.size());
            uint8_t expected_tag=(affected && !tags[i])?0:tags[i];
            check(readable==present && (!present || std::all_of(bytes.begin(),bytes.end(),[&](uint8_t b){return b==expected_tag;})),"page model checks all surviving/new bytes through sibling");
            if(present) {tags[i]=uint8_t(i+1);bytes.fill(tags[i]);check(satchel_cpu_probe_write(p.get(),base+i*page,bytes.data(),bytes.size()),"update original per-page canary");}
            else tags[i]=0;
        }
    }
    // Region-count exhaustion on a split, while merging can reduce the count.
    auto limited=make(words({0xd4000001}));
    check(satchel_cpu_probe_map_missing_rw(limited.get(),base,3*page,&changed)==SATCHEL_CPU_MAP_OK,"region cap setup wide map");
    for(unsigned i=0;i<28;i++)check(satchel_cpu_probe_map_missing_rw(limited.get(),base+(5+2*i)*page,page,&changed)==SATCHEL_CPU_MAP_OK,"region cap setup isolated map");
    changed=99;check(satchel_cpu_probe_unmap_rw(limited.get(),base+page,page,&changed)==SATCHEL_CPU_MAP_LIMIT && changed==99 &&
                    satchel_cpu_probe_check_range(limited.get(),base,3*page,SATCHEL_CPU_READ),"split beyond32 regions fails atomically");
    check(satchel_cpu_probe_map_missing_rw(limited.get(),base+5*page,3*page,&changed)==SATCHEL_CPU_MAP_OK && changed==page &&
          satchel_cpu_probe_check_range(limited.get(),base+5*page,3*page,SATCHEL_CPU_READ),"coalescing reduces fragmented map count");
    check(satchel_cpu_probe_unmap_rw(limited.get(),base+page,page,&changed)==SATCHEL_CPU_MAP_OK && changed==page,"split succeeds after actual backing count reduced");
    struct Busy {SatchelCPUProbe*a,*b;bool okay=false;} busy{p.get(),sibling.get()};
    auto cancelled=[](void*context)->int {
        auto&b=*static_cast<Busy*>(context);uint64_t out=99;
        b.okay=satchel_cpu_probe_map_missing_rw(b.a,base,4096,&out)==SATCHEL_CPU_MAP_BUSY &&
               satchel_cpu_probe_unmap_rw(b.b,base,4096,&out)==SATCHEL_CPU_MAP_BUSY && out==99;
        std::thread thread([&b]{uint64_t out=99;b.okay=b.okay && satchel_cpu_probe_map_missing_rw(b.b,base,4096,&out)==SATCHEL_CPU_MAP_BUSY &&
            satchel_cpu_probe_unmap_rw(b.b,base,4096,&out)==SATCHEL_CPU_MAP_BUSY && out==99;});thread.join();return 1;
    };
    check(satchel_cpu_probe_run(p.get(),1,cancelled,&busy).reason==SATCHEL_CPU_CANCELLED && busy.okay,"range mutations reject shared active and callback reentry");
    for(unsigned variant=0;variant<6;variant++) {
        auto exclusive=make(words({0xc85f7c01,0xc8027c03,0xd4000001}));auto peer=shared(exclusive.get());
        check(satchel_cpu_probe_map_missing_rw(exclusive.get(),base,page,&changed)==SATCHEL_CPU_MAP_OK,"exclusive range setup");
        satchel_cpu_probe_set_x(exclusive.get(),3,9);satchel_cpu_probe_run(exclusive.get(),1,nullptr,nullptr);
        if(variant==0)check(satchel_cpu_probe_map_missing_rw(peer.get(),base,page,&changed)==SATCHEL_CPU_MAP_OK && !changed,"existing map does not mutate reservation");
        if(variant==1)check(satchel_cpu_probe_unmap_rw(peer.get(),base+page,page,&changed)==SATCHEL_CPU_MAP_OK && !changed,"free unmap does not mutate reservation");
        if(variant==2) {satchel_cpu_probe_test_mapping_allocation_failure(2);check(satchel_cpu_probe_map_missing_rw(peer.get(),base,2*page,&changed)==SATCHEL_CPU_MAP_ALLOCATION,"failed map does not mutate reservation");}
        if(variant==3) {satchel_cpu_probe_test_mapping_allocation_failure(2);check(satchel_cpu_probe_unmap_rw(peer.get(),base,page,&changed)==SATCHEL_CPU_MAP_ALLOCATION,"failed unmap does not mutate reservation");}
        if(variant==4)check(satchel_cpu_probe_map_missing_rw(peer.get(),base,2*page,&changed)==SATCHEL_CPU_MAP_OK,"actual range growth clears sibling reservation");
        if(variant==5)check(satchel_cpu_probe_unmap_rw(peer.get(),base,page,&changed)==SATCHEL_CPU_MAP_OK,"actual range removal clears sibling reservation");
        expect_stop(exclusive.get(),SATCHEL_CPU_SVC);check(x(exclusive.get(),2)==(variant>=4?1u:0u),"range transaction reservation semantics");
    }
}

static void range_tests() {
    auto code=words({0xd503201f,0xd4000001});
    const uint64_t base=0x7400000000, heap=base+0x10000;
    std::array<uint8_t,64> initial{};
    for(size_t i=0;i<initial.size();i++)initial[i]=uint8_t(i+1);
    SatchelCPUProbeRegion regions[]={
        {code_address,code.data(),code.size(),code.size(),SATCHEL_CPU_READ|SATCHEL_CPU_EXECUTE},
        {base,initial.data(),16,16,SATCHEL_CPU_READ},
        {base+16,initial.data()+16,16,16,SATCHEL_CPU_READ|SATCHEL_CPU_WRITE},
        {base+32,initial.data()+32,16,16,SATCHEL_CPU_WRITE},
        {base+48,initial.data()+48,16,16,SATCHEL_CPU_READ|SATCHEL_CPU_WRITE}};
    char error[160];Probe p(satchel_cpu_probe_create(regions,5,code_address,error,sizeof(error)),satchel_cpu_probe_destroy);
    check(bool(p),"range fixture constructed");auto sibling=shared(p.get());
    for(uint64_t offset=0;offset<=65;offset++)for(uint64_t size=0;size<=66;size++)for(unsigned mask=0;mask<16;mask++) {
        bool expected=false;
        for(const auto&r:regions)if(base+offset>=r.address && base+offset-r.address<r.memory_size && size &&
            size<=r.memory_size-(base+offset-r.address) && mask && !(mask&~7u) && (r.permissions&mask)==mask)expected=true;
        check(bool(satchel_cpu_probe_check_range(sibling.get(),base+offset,size,mask))==expected,
              "range permissions match independent complete-span model including adjacent mappings");
    }
    check(satchel_cpu_probe_check_range(p.get(),code_address,8,SATCHEL_CPU_READ|SATCHEL_CPU_EXECUTE) &&
          !satchel_cpu_probe_check_range(p.get(),code_address,8,SATCHEL_CPU_WRITE),"RX map permission check does not imply write");
    for(uint64_t size:{uint64_t(0),uint64_t(1),UINT64_MAX})
        check(!satchel_cpu_probe_check_range(p.get(),UINT64_MAX,size,SATCHEL_CPU_READ),"overflow and unmapped extreme ranges rejected");
    check(!satchel_cpu_probe_check_range(nullptr,base,1,SATCHEL_CPU_READ) &&
          !satchel_cpu_probe_check_range(p.get(),base,1,UINT32_MAX),"null and malformed permission masks rejected");
    std::array<uint8_t,16> after{};
    check(satchel_cpu_probe_read(p.get(),base,after.data(),after.size()) && std::equal(after.begin(),after.end(),initial.begin()),
          "successful and failing permission checks preserve guest bytes");
    check(satchel_cpu_probe_resize_rw(p.get(),heap,4096)==SATCHEL_CPU_MAP_OK &&
          satchel_cpu_probe_check_range(sibling.get(),heap,4096,SATCHEL_CPU_READ|SATCHEL_CPU_WRITE),"shared dynamic creation visible to check");
    check(satchel_cpu_probe_resize_rw(sibling.get(),heap,8192)==SATCHEL_CPU_MAP_OK &&
          satchel_cpu_probe_check_range(p.get(),heap,8192,SATCHEL_CPU_WRITE),"shared growth visible to check");
    check(satchel_cpu_probe_resize_rw(p.get(),heap,4096)==SATCHEL_CPU_MAP_OK &&
          !satchel_cpu_probe_check_range(sibling.get(),heap,8192,SATCHEL_CPU_READ),"range check is not a stale mapping lease");
    check(satchel_cpu_probe_resize_rw(p.get(),heap,0)==SATCHEL_CPU_MAP_OK &&
          !satchel_cpu_probe_check_range(sibling.get(),heap,1,SATCHEL_CPU_READ),"removed mapping cannot pass check");
    struct Busy {SatchelCPUProbe*a,*b;bool okay=false;} busy{p.get(),sibling.get()};
    auto cancel=[](void*context)->int {
        auto&b=*static_cast<Busy*>(context);
        b.okay=!satchel_cpu_probe_check_range(b.a,code_address,4,SATCHEL_CPU_EXECUTE) &&
               !satchel_cpu_probe_check_range(b.b,code_address,4,SATCHEL_CPU_EXECUTE);
        std::thread worker([&b]{b.okay=b.okay && !satchel_cpu_probe_check_range(b.b,code_address,4,SATCHEL_CPU_EXECUTE);});worker.join();
        return 1;
    };
    auto result=satchel_cpu_probe_run(p.get(),1,cancel,&busy);
    check(result.reason==SATCHEL_CPU_CANCELLED && !result.steps && busy.okay,"range checks reject current/sibling callback and cross-host busy access");
    check(satchel_cpu_probe_check_range(p.get(),code_address,4,SATCHEL_CPU_EXECUTE),"range check recovers after run guard released");
    for(unsigned width:{4u,8u}) {
        auto exclusive=make(words({width==4?0x885f7c01u:0xc85f7c01u,width==4?0x88027c03u:0xc8027c03u,0xd4000001}));
        satchel_cpu_probe_set_x(exclusive.get(),3,9);
        check(satchel_cpu_probe_run(exclusive.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET,"reservation before permission checks");
        check(satchel_cpu_probe_check_range(exclusive.get(),data_address,width,SATCHEL_CPU_READ|SATCHEL_CPU_WRITE) &&
              !satchel_cpu_probe_check_range(exclusive.get(),data_address,width,SATCHEL_CPU_EXECUTE),"successful and failing checks during reservation");
        expect_stop(exclusive.get(),SATCHEL_CPU_SVC);
        check(x(exclusive.get(),2)==0,"permission checks preserve exclusive reservation");
    }
    auto fault=make(words({0}));expect_stop(fault.get(),SATCHEL_CPU_UNSUPPORTED);
    check(satchel_cpu_probe_check_range(fault.get(),data_address,64,SATCHEL_CPU_READ),"faulted-context memory remains inspectable without reviving execution");
    check(satchel_cpu_probe_run(fault.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_UNSUPPORTED,"permission check cannot revive terminal fault");
    p.reset();check(satchel_cpu_probe_check_range(sibling.get(),base,16,SATCHEL_CPU_READ),"range check retains shared map after parent destruction");
}
static void clock_tests(const char *fixtures) {
    const auto code=fixture(fixtures,"clock");
    uint32_t encoded[4]{};std::memcpy(encoded,code.data(),sizeof(encoded));
    check(encoded[1]==0xd53be021 && encoded[2]==0xd53be042 && encoded[3]==0xd53be003,
          "independent original assembler agrees with exact CNTPCT/CNTVCT/CNTFRQ masks");
    const auto oracle=[](uint64_t ns){return uint64_t((__uint128_t(ns)*19200000)/1000000000);};
    for(uint64_t ns:{uint64_t{0},uint64_t{1},uint64_t{52},uint64_t{53},uint64_t{624},uint64_t{625},
        uint64_t{626},uint64_t{999999999},uint64_t{1000000000},UINT64_MAX/12,UINT64_MAX-625,UINT64_MAX})
        check(satchel_cpu_probe_test_ticks_from_ns(ns)==oracle(ns),"tick conversion exact at floor and overflow boundaries");
    uint64_t random=0x54d63ac274c8e291;
    for(unsigned i=0;i<20000;i++){
        random=random*6364136223846793005ULL+1442695040888963407ULL;
        check(satchel_cpu_probe_test_ticks_from_ns(random)==oracle(random),"tick conversion matches independent 128-bit oracle");
    }
    for(uint64_t elapsed:{uint64_t{0},uint64_t{52},uint64_t{53},uint64_t{625},uint64_t{1000000000},UINT64_MAX}){
        auto p=make(code);
        check(satchel_cpu_probe_test_set_clock(p.get(),elapsed,0),"select deterministic original clock fixture");
        uint8_t q[16];std::memset(q,0x6b,sizeof(q));satchel_cpu_probe_set_q(p.get(),0,q);
        expect_stop(p.get(),SATCHEL_CPU_SVC);
        std::array<uint64_t,5> actual{};
        check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),sizeof(actual)),"read original timer fixture output");
        check(actual[0]==oracle(elapsed) && actual[1]==oracle(elapsed) &&
            actual[2]==SATCHEL_CPU_SYSTEM_TICK_FREQUENCY && actual[3]==1 && actual[4]==stack_address+128,
            "counter/frequency/XZR reads preserve NZCV/SP and report virtual time");
        uint8_t after[16];check(satchel_cpu_probe_get_q(p.get(),0,after) && !std::memcmp(q,after,16),"timer reads preserve SIMD state");
    }
    auto parent=make(words({0xd53be020,0xd4000001,0xd53be041,0xd4000001,0xd53be002,0xd4000001}));
    auto peer=shared(parent.get(),code_address+8);
    check(satchel_cpu_probe_test_set_clock(parent.get(),625,0),"initialize process-shared clock");
    uint64_t a=99,b=99;
    check(satchel_cpu_probe_get_system_tick(parent.get(),&a) && satchel_cpu_probe_get_system_tick(peer.get(),&b) && a==12 && b==12,
          "siblings observe exact common epoch and time sample");
    expect_stop(parent.get(),SATCHEL_CPU_SVC);expect_stop(peer.get(),SATCHEL_CPU_SVC);
    check(x(parent.get(),0)==12 && x(peer.get(),1)==12,"physical and virtual counters share zero offset");
    check(satchel_cpu_probe_test_set_clock(peer.get(),1250,0),"advance shared clock without executing guest instructions");
    check(satchel_cpu_probe_get_system_tick(parent.get(),&a) && a==24,"host tick getter matches process clock advancement");
    parent.reset();
    check(satchel_cpu_probe_get_system_tick(peer.get(),&b) && b==24,"surviving context retains clock after parent destruction");
    auto another=shared(peer.get(),code_address);
    expect_stop(another.get(),SATCHEL_CPU_SVC);check(x(another.get(),0)==24,"new contexts do not reset shared clock epoch");
    auto independent=make(words({0xd53be020,0xd4000001}));
    check(satchel_cpu_probe_test_set_clock(independent.get(),0,0) && satchel_cpu_probe_get_system_tick(independent.get(),&a) && a==0,
          "independent processes own independent virtual clock epochs");
    // A failed host sample or regression must neither fabricate a value nor
    // mutate the destination register. A sibling can still run after recovery.
    check(satchel_cpu_probe_test_set_clock(peer.get(),1249,0),"inject backwards host sample");
    a=0xfeed;check(!satchel_cpu_probe_get_system_tick(peer.get(),&a) && a==0xfeed,"clock regression leaves API output untouched");
    check(satchel_cpu_probe_test_set_clock(peer.get(),1250,1),"inject host clock failure");
    check(!satchel_cpu_probe_get_system_tick(peer.get(),&a) && a==0xfeed,"clock failure leaves API output untouched");
    satchel_cpu_probe_set_pc(another.get(),code_address);satchel_cpu_probe_set_x(another.get(),0,0x1234);
    auto failed=satchel_cpu_probe_run(another.get(),1,nullptr,nullptr);
    check(failed.reason==SATCHEL_CPU_INTERNAL_FAULT && failed.steps==0 && failed.pc==code_address && x(another.get(),0)==0x1234,
          "failed counter instruction is terminal without register/PC side effects");
    satchel_cpu_probe_set_pc(peer.get(),code_address+16);expect_stop(peer.get(),SATCHEL_CPU_SVC);
    check(x(peer.get(),2)==19200000,"frequency read does not require or fabricate a host time sample");
    check(satchel_cpu_probe_test_set_clock(peer.get(),1251,0) && satchel_cpu_probe_get_system_tick(peer.get(),&a) && a==24,
          "valid nondecreasing clock recovers on a live sibling");
    check(satchel_cpu_probe_run(another.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_INTERNAL_FAULT,"clock recovery does not revive terminal contexts");
    a=0xfeed;check(!satchel_cpu_probe_get_system_tick(nullptr,&a) && a==0xfeed && !satchel_cpu_probe_get_system_tick(peer.get(),nullptr),
          "tick getter rejects null arguments atomically");
    for(uint32_t base:{0xd53be000u,0xd53be020u,0xd53be040u})for(unsigned reg=0;reg<32;reg++){
        auto p=make(words({base|reg,0xd4000001}));satchel_cpu_probe_test_set_clock(p.get(),625,0);
        expect_stop(p.get(),SATCHEL_CPU_SVC);
        check(reg==31?x(p.get(),0)==data_address:x(p.get(),reg)==(base==0xd53be000u?19200000:12),
              "every Rt encoding uses X registers including discarded XZR result");
        auto denied=make(words({(base&~0x200000u)|reg})); // Corresponding MSR always forbidden.
        auto r=satchel_cpu_probe_run(denied.get(),1,nullptr,nullptr);
        check(r.reason==SATCHEL_CPU_UNSUPPORTED && !r.steps,"guest cannot write any counter or frequency register");
    }
    for(uint32_t neighbor:{0xd53be060u,0xd53be080u,0xd53be100u,0xd53be120u,0xd53be200u,0xd53fe020u}){
        auto p=make(words({neighbor}));auto r=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
        check(r.reason==SATCHEL_CPU_UNSUPPORTED && !r.steps,"timer control/compare/EL2 neighbors remain unsupported");
    }
    auto priority=make(words({0xd53be020,0xd4000001}));satchel_cpu_probe_test_set_clock(priority.get(),0,1);
    check(satchel_cpu_probe_run(priority.get(),0,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET,"zero budget precedes failing clock access");
    auto cancelled=satchel_cpu_probe_run(priority.get(),1,[](void*)->int{return 1;},nullptr);
    check(cancelled.reason==SATCHEL_CPU_CANCELLED && !cancelled.steps,"cancellation precedes failing clock access");
    uint64_t point=code_address;satchel_cpu_probe_set_breakpoints(priority.get(),&point,1);
    auto trap=satchel_cpu_probe_run(priority.get(),1,nullptr,nullptr);
    check(trap.reason==SATCHEL_CPU_BREAKPOINT && !trap.steps,"breakpoint precedes failing clock access");
    satchel_cpu_probe_set_breakpoints(priority.get(),nullptr,0);
    auto sibling=shared(priority.get(),code_address);
    struct Reentry {SatchelCPUProbe*a,*b;unsigned rejected=0;bool threaded=false;} reentry{priority.get(),sibling.get()};
    cancelled=satchel_cpu_probe_run(priority.get(),1,[](void *opaque)->int{
        auto &r=*static_cast<Reentry*>(opaque);
        for(auto*p:{r.a,r.b}){uint64_t tick=0xabcd;
            r.rejected+=!satchel_cpu_probe_get_system_tick(p,&tick) && tick==0xabcd;
            r.rejected+=!satchel_cpu_probe_test_set_clock(p,625,0);
        }
        std::thread thread([&r]{uint64_t tick=0x1234;r.threaded=!satchel_cpu_probe_get_system_tick(r.b,&tick)&&tick==0x1234;});thread.join();return 1;
    },&reentry);
    check(cancelled.reason==SATCHEL_CPU_CANCELLED && !cancelled.steps && reentry.rejected==4 && reentry.threaded,
          "clock API rejects running/sibling/reentrant/cross-host access without side effects");
    for(unsigned width:{4u,8u}){
        auto p=make(words({width==4?0x885f7c01u:0xc85f7c01u,0xd53be024u,width==4?0x88027c03u:0xc8027c03u,0xd4000001}));
        satchel_cpu_probe_test_set_clock(p.get(),625,0);satchel_cpu_probe_set_x(p.get(),3,9);
        check(satchel_cpu_probe_run(p.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET,"reserve ordinary guest memory before clock reads");
        check(satchel_cpu_probe_get_system_tick(p.get(),&a) && a==12,"host getter during suspended exclusive sequence");
        expect_stop(p.get(),SATCHEL_CPU_SVC);
        check(x(p.get(),2)==0 && x(p.get(),4)==12,"host and guest clock reads preserve existing reservation");
    }
    // Native host clock comparison brackets the API without assuming the host
    // hardware counter frequency equals the emulated Switch frequency.
    auto live=make(words({0xd53be020,0xd4000001}));
    const auto host=[](){timespec t{};check(clock_gettime(CLOCK_MONOTONIC_RAW,&t)==0,"read native monotonic clock");return uint64_t(t.tv_sec)*1000000000+uint64_t(t.tv_nsec);};
    uint64_t outer0=host();check(satchel_cpu_probe_get_system_tick(live.get(),&a),"sample live virtual clock");uint64_t inner0=host();
    timespec delay{0,2000000};while(nanosleep(&delay,&delay)!=0){}
    uint64_t inner1=host();check(satchel_cpu_probe_get_system_tick(live.get(),&b),"sample clock after host idle interval");uint64_t outer1=host();
    check(b>a && b-a+1>=oracle(inner1-inner0) && b-a<=oracle(outer1-outer0)+1,
          "live virtual counter advances at 19.2MHz across an independently measured idle interval");
}
static void fp_status_tests() {
    auto parent=make(words({0xd503201f,0xd4000001,0x1e602802,0xd4000001,0x1e600802,0xd4000001,0}));
    auto child=shared(parent.get(),code_address);
    uint32_t control=99,status=88;
    check(satchel_cpu_probe_get_fp_status(parent.get(),&control,&status)==SATCHEL_CPU_FP_OK && control==0 && status==0,
          "new thread status is provable reset FPCR/FPSR");
    check(satchel_cpu_probe_copy_fp_status(child.get(),parent.get())==SATCHEL_CPU_FP_OK,"copy default status across same-process distinct contexts");
    check(satchel_cpu_probe_test_set_known_fpcr(parent.get(),0x02000000),"original fixture sets actual supported parent FPCR DN");
    uint8_t marker[16],after[16];std::memset(marker,0x29,sizeof(marker));
    satchel_cpu_probe_set_q(child.get(),4,marker);satchel_cpu_probe_set_x(child.get(),4,0x1234);satchel_cpu_probe_set_tls(child.get(),0x3344,0x5566);
    const auto before=satchel_cpu_probe_run(child.get(),0,nullptr,nullptr);
    check(satchel_cpu_probe_copy_fp_status(child.get(),parent.get())==SATCHEL_CPU_FP_OK,"copy actual nonzero FPCR rather than assumed default");
    check(satchel_cpu_probe_get_fp_status(child.get(),&control,&status)==SATCHEL_CPU_FP_OK && control==0x02000000 && status==0,
          "destination owns copied FPCR and known FPSR");
    uint64_t ro=0,rw=0;
    check(satchel_cpu_probe_get_q(child.get(),4,after) && !std::memcmp(marker,after,16) && x(child.get(),4)==0x1234 &&
        satchel_cpu_probe_get_tls(child.get(),&ro,&rw) && ro==0x3344 && rw==0x5566 &&
        satchel_cpu_probe_run(child.get(),0,nullptr,nullptr).pc==before.pc,"status clone does not copy parent vector/GPR/TLS/PC state");
    check(satchel_cpu_probe_test_set_known_fpcr(parent.get(),0) &&
        satchel_cpu_probe_get_fp_status(child.get(),&control,&status)==SATCHEL_CPU_FP_OK && control==0x02000000,
        "copied FP status is context-local after parent changes");
    auto independent=make(words({0xd4000001}));
    check(satchel_cpu_probe_copy_fp_status(child.get(),independent.get())==SATCHEL_CPU_FP_INVALID &&
        satchel_cpu_probe_copy_fp_status(child.get(),child.get())==SATCHEL_CPU_FP_INVALID &&
        satchel_cpu_probe_copy_fp_status(nullptr,parent.get())==SATCHEL_CPU_FP_INVALID &&
        satchel_cpu_probe_copy_fp_status(child.get(),nullptr)==SATCHEL_CPU_FP_INVALID,"copy rejects self/null/foreign ownership");
    control=0x55;status=0x66;
    check(satchel_cpu_probe_get_fp_status(nullptr,&control,&status)==SATCHEL_CPU_FP_INVALID && control==0x55 && status==0x66,
          "null status query preserves outputs");
    check(satchel_cpu_probe_get_fp_status(child.get(),nullptr,&status)==SATCHEL_CPU_FP_INVALID && status==0x66 &&
          satchel_cpu_probe_get_fp_status(child.get(),&control,nullptr)==SATCHEL_CPU_FP_INVALID && control==0x55 &&
          satchel_cpu_probe_get_fp_status(child.get(),&control,&control)==SATCHEL_CPU_FP_INVALID && control==0x55,
          "status outputs must be distinct and present");
    auto fault=shared(parent.get(),code_address+24);expect_stop(fault.get(),SATCHEL_CPU_UNSUPPORTED);
    check(satchel_cpu_probe_get_fp_status(fault.get(),&control,&status)==SATCHEL_CPU_FP_INVALID && control==0x55 && status==0x66 &&
          satchel_cpu_probe_copy_fp_status(child.get(),fault.get())==SATCHEL_CPU_FP_INVALID &&
          satchel_cpu_probe_copy_fp_status(fault.get(),parent.get())==SATCHEL_CPU_FP_INVALID,"terminal contexts reject status reads/copy");
    struct Reentry {SatchelCPUProbe*a,*b;unsigned rejected=0;bool threaded=false;} reentry{parent.get(),child.get()};
    auto stopped=satchel_cpu_probe_run(parent.get(),1,[](void *opaque)->int{
        auto&r=*static_cast<Reentry*>(opaque);uint32_t c=0x55,s=0x66;
        for(auto*p:{r.a,r.b})r.rejected+=satchel_cpu_probe_get_fp_status(p,&c,&s)==SATCHEL_CPU_FP_BUSY && c==0x55 && s==0x66;
        r.rejected+=satchel_cpu_probe_copy_fp_status(r.b,r.a)==SATCHEL_CPU_FP_BUSY;
        r.rejected+=satchel_cpu_probe_copy_fp_status(r.a,r.b)==SATCHEL_CPU_FP_BUSY;
        std::thread t([&r]{uint32_t c=1,s=2;r.threaded=satchel_cpu_probe_get_fp_status(r.b,&c,&s)==SATCHEL_CPU_FP_BUSY && c==1 && s==2 &&
            satchel_cpu_probe_copy_fp_status(r.b,r.a)==SATCHEL_CPU_FP_BUSY;});t.join();return 1;
    },&reentry);
    check(stopped.reason==SATCHEL_CPU_CANCELLED && !stopped.steps && reentry.rejected==4 && reentry.threaded,
          "same/sibling/cross-host status calls reject active process atomically");
    check(satchel_cpu_probe_get_fp_status(child.get(),&control,&status)==SATCHEL_CPU_FP_OK && control==0x02000000 && status==0,
          "rejected copies preserve destination FP status");
    for(uint64_t entry:{code_address+8,code_address+16}){
        auto arithmetic=shared(parent.get(),entry);
        uint8_t zero[16]{};satchel_cpu_probe_set_q(arithmetic.get(),0,zero);satchel_cpu_probe_set_q(arithmetic.get(),1,zero);
        check(satchel_cpu_probe_get_fp_status(arithmetic.get(),&control,&status)==SATCHEL_CPU_FP_OK,"status known before original FP instruction");
        expect_stop(arithmetic.get(),SATCHEL_CPU_SVC);
        control=0x55;status=0x66;
        check(satchel_cpu_probe_get_fp_status(arithmetic.get(),&control,&status)==SATCHEL_CPU_FP_OK && control==0 && status==0,
              "exact zero FADD/FMUL preserves provably known zero cumulative status");
        check(satchel_cpu_probe_copy_fp_status(child.get(),arithmetic.get())==SATCHEL_CPU_FP_OK &&
              satchel_cpu_probe_copy_fp_status(arithmetic.get(),parent.get())==SATCHEL_CPU_FP_OK,
              "accurate scalar arithmetic status remains eligible for thread creation");
        check(satchel_cpu_probe_get_fp_status(child.get(),&control,&status)==SATCHEL_CPU_FP_OK && control==0 && status==0,
              "child receives actual parent floating status after arithmetic");
        satchel_cpu_probe_set_q(arithmetic.get(),0,zero);
        check(satchel_cpu_probe_get_fp_status(arithmetic.get(),&control,&status)==SATCHEL_CPU_FP_OK && status==0,
              "SIMD data mutation leaves owned cumulative exception status unchanged");
    }
    auto integer=shared(parent.get(),code_address);expect_stop(integer.get(),SATCHEL_CPU_SVC);
    check(satchel_cpu_probe_get_fp_status(integer.get(),&control,&status)==SATCHEL_CPU_FP_OK && !control && !status,
          "integer-only execution retains known reset floating status");
    for(uint32_t op:{0xd53b4400u,0xd51b4400u,0xd53b4420u,0xd51b4420u}){
        auto p=make(words({op}));expect_stop(p.get(),SATCHEL_CPU_UNSUPPORTED);
    }
    uint64_t initial=7;
    auto reservation=make(words({0xc85f7c01,0xc8027c03,0xd4000001}),&initial,8);
    auto target=shared(reservation.get(),code_address);satchel_cpu_probe_set_x(reservation.get(),3,9);
    check(satchel_cpu_probe_run(reservation.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET,"reservation before host FP query/copy");
    check(satchel_cpu_probe_get_fp_status(reservation.get(),&control,&status)==SATCHEL_CPU_FP_OK &&
          satchel_cpu_probe_copy_fp_status(target.get(),reservation.get())==SATCHEL_CPU_FP_OK,"status calls do not schedule a sibling");
    expect_stop(reservation.get(),SATCHEL_CPU_SVC);check(x(reservation.get(),2)==0,"host FP query/copy preserves exclusive reservation");
}
static void alias_tests() {
    constexpr uint64_t page=4096, target=0x7600000000, heap=0x7500000000;
    const auto code=words({0xf9400001,0xf9000401,0xd4000001});
    std::vector<uint8_t> original(6*page);
    for(size_t i=0;i<original.size();i++) original[i]=uint8_t(i/4096+1);
    auto p=make(code,original.data(),original.size());auto sibling=shared(p.get());
    const uint64_t total=satchel_cpu_probe_test_physical_bytes(p.get());
    auto map=[&](uint64_t d,uint64_t s,uint64_t n){return satchel_cpu_probe_map_alias(p.get(),d,s,n);};
    auto unmap=[&](uint64_t d,uint64_t s,uint64_t n){return satchel_cpu_probe_unmap_alias(sibling.get(),d,s,n);};
    uint64_t value=0x8877665544332211,got=0;std::vector<uint8_t> after(original.size());
    for(unsigned stage:{1u,2u}) {
        satchel_cpu_probe_test_mapping_allocation_failure(stage);
        check(map(target,data_address+page,4*page)==SATCHEL_CPU_MAP_ALLOCATION,"alias allocation failure before and after plan");
        check(satchel_cpu_probe_read(p.get(),data_address,after.data(),after.size()) && after==original &&
              !satchel_cpu_probe_check_range(p.get(),target,1,SATCHEL_CPU_READ),"failed alias leaves original bytes and permissions unchanged");
    }
    check(map(target,data_address+page,4*page)==SATCHEL_CPU_MAP_OK,"alias initial NSO-like data subset");
    check(satchel_cpu_probe_test_same_backing(p.get(),data_address+page,target) &&
          satchel_cpu_probe_test_same_backing(sibling.get(),data_address+5*page-1,target+4*page-1) &&
          satchel_cpu_probe_test_physical_bytes(p.get())==total,"alias retains same physical allocation and offsets with no capacity charge");
    check(!satchel_cpu_probe_read(sibling.get(),data_address+page,&got,8) &&
          !satchel_cpu_probe_write(p.get(),data_address+page,&value,8) &&
          !satchel_cpu_probe_check_range(p.get(),data_address+page,1,SATCHEL_CPU_READ) &&
          !satchel_cpu_probe_check_range(p.get(),target,4,SATCHEL_CPU_EXECUTE),"locked source denied read/write and alias never executable");
    check(satchel_cpu_probe_check_range(sibling.get(),data_address,page,SATCHEL_CPU_WRITE) &&
          satchel_cpu_probe_check_range(sibling.get(),data_address+5*page,page,SATCHEL_CPU_WRITE),"untouched source prefix/suffix remain accessible");
    got=0xabcdef;
    check(!satchel_cpu_probe_check_range(p.get(),data_address+page-3,8,SATCHEL_CPU_READ) &&
          !satchel_cpu_probe_read(p.get(),data_address+page-3,&got,8) && got==0xabcdef &&
          !satchel_cpu_probe_write(p.get(),data_address+page-3,&value,8),
          "same-backing span entering locked view fails complete preflight and preserves output");
    std::array<uint8_t,3> prefix{};
    check(satchel_cpu_probe_read(p.get(),data_address+page-3,prefix.data(),prefix.size()) &&
          std::all_of(prefix.begin(),prefix.end(),[](uint8_t b){return b==1;}),"failed cross-permission write preserves accessible prefix bytes");
    check(satchel_cpu_probe_write(sibling.get(),target+page-3,&value,8),"write true alias across page boundary");
    std::memcpy(original.data()+2*page-3,&value,8);
    satchel_cpu_probe_set_x(p.get(),0,target+page-3);expect_stop(p.get(),SATCHEL_CPU_SVC);
    std::memcpy(original.data()+2*page+5,&value,8);
    check(x(p.get(),1)==value && satchel_cpu_probe_read(sibling.get(),target+page+5,&got,8) && got==value,
          "guest LDR/STR accesses actual alias backing across page boundary");
    for(unsigned stage:{1u,2u}) {
        satchel_cpu_probe_test_mapping_allocation_failure(stage);
        check(unmap(target+page,data_address+2*page,2*page)==SATCHEL_CPU_MAP_ALLOCATION,"partial alias unmap allocation failure atomic");
        check(!satchel_cpu_probe_check_range(p.get(),data_address+2*page,1,SATCHEL_CPU_READ) &&
              satchel_cpu_probe_check_range(p.get(),target,4*page,SATCHEL_CPU_WRITE) &&
              satchel_cpu_probe_read(p.get(),target+page+5,&got,8) && got==value,"failed unmap retains alias bytes and source locks");
    }
    check(unmap(target+page,data_address+2*page,2*page)==SATCHEL_CPU_MAP_OK,"partial middle alias unmap restores exact source pages");
    check(satchel_cpu_probe_check_range(p.get(),data_address+2*page,2*page,SATCHEL_CPU_WRITE) &&
          !satchel_cpu_probe_check_range(p.get(),target+page,1,SATCHEL_CPU_READ) &&
          !satchel_cpu_probe_check_range(p.get(),data_address+page,1,SATCHEL_CPU_READ) &&
          !satchel_cpu_probe_check_range(p.get(),data_address+4*page,1,SATCHEL_CPU_READ),"partial restore leaves independent outer aliases locked");
    check(unmap(target,data_address+page,page)==SATCHEL_CPU_MAP_OK &&
          unmap(target+3*page,data_address+4*page,page)==SATCHEL_CPU_MAP_OK,"remove remaining alias views in separate transactions");
    check(satchel_cpu_probe_check_range(p.get(),data_address,6*page,SATCHEL_CPU_WRITE) &&
          satchel_cpu_probe_read(p.get(),data_address,after.data(),after.size()) && after==original &&
          satchel_cpu_probe_test_physical_bytes(p.get())==total,"restored original allocation coalesces with exact alias writes intact");
    check(unmap(target,data_address+page,page)==SATCHEL_CPU_MAP_INVALID,"double alias unmap rejects");
    // Adjacent alias requests can become one compatible physical view. Partial
    // restoration at different boundaries still keeps the source correspondence.
    check(map(target,data_address,page)==SATCHEL_CPU_MAP_OK && map(target+page,data_address+page,page)==SATCHEL_CPU_MAP_OK,
          "adjacent aliases of one backing admitted independently");
    check(satchel_cpu_probe_check_range(p.get(),target,2*page,SATCHEL_CPU_WRITE) &&
          satchel_cpu_probe_write(p.get(),target+page-3,&value,8),"same-backing neighboring aliases permit whole cross-view access");
    check(unmap(target,data_address,2*page)==SATCHEL_CPU_MAP_OK,"one matching unmap can span adjacent original alias requests");
    // Distinct allocations stay distinct; both are aliased truthfully, but an
    // unaligned access cannot pretend two host vectors are contiguous.
    check(satchel_cpu_probe_resize_rw(p.get(),heap,page)==SATCHEL_CPU_MAP_OK &&
          satchel_cpu_probe_resize_rw(p.get(),heap+page,page)==SATCHEL_CPU_MAP_OK,"two independent neighboring dynamic allocations");
    check(map(target,heap,2*page)==SATCHEL_CPU_MAP_OK && satchel_cpu_probe_test_same_backing(p.get(),heap,target) &&
          satchel_cpu_probe_test_same_backing(p.get(),heap+page,target+page),"multi-backing source becomes corresponding alias views");
    check(!satchel_cpu_probe_read(p.get(),target+page-3,&got,8) && !satchel_cpu_probe_write(p.get(),target+page-3,&value,8),
          "cross-allocation alias span rejected before partial access");
    check(unmap(target+page,heap,page)==SATCHEL_CPU_MAP_INVALID && unmap(target,heap+page,page)==SATCHEL_CPU_MAP_INVALID,
          "unmap rejects mismatched source and physical offset correspondence");
    check(unmap(target,heap,2*page)==SATCHEL_CPU_MAP_OK &&
          satchel_cpu_probe_resize_rw(p.get(),heap+page,0)==SATCHEL_CPU_MAP_OK && satchel_cpu_probe_resize_rw(p.get(),heap,0)==SATCHEL_CPU_MAP_OK,
          "multi-backing restore preserves separate resize ownership");
    check(satchel_cpu_probe_resize_rw(p.get(),heap,4*page)==SATCHEL_CPU_MAP_OK,"dynamic heap for alias locking");
    check(map(target,heap+page,page)==SATCHEL_CPU_MAP_OK,"middle of dynamic allocation aliases without copy");
    uint64_t changed=99;
    for(uint64_t size:{uint64_t{0},page,4*page,5*page})
        check(satchel_cpu_probe_resize_rw(p.get(),heap,size)==SATCHEL_CPU_MAP_INVALID,"live alias rejects resize/free even outside locked subrange");
    check(satchel_cpu_probe_map_missing_rw(p.get(),heap,5*page,&changed)==SATCHEL_CPU_MAP_INVALID && changed==99 &&
          satchel_cpu_probe_unmap_rw(p.get(),heap,page,&changed)==SATCHEL_CPU_MAP_INVALID && changed==99 &&
          satchel_cpu_probe_resize_rw(p.get(),target,0)==SATCHEL_CPU_MAP_INVALID,"range mutation cannot detach aliased backing or free target");
    check(unmap(target,heap+page,page)==SATCHEL_CPU_MAP_OK && satchel_cpu_probe_resize_rw(p.get(),heap,5*page)==SATCHEL_CPU_MAP_OK,
          "last alias removal restores exact-base dynamic resize");
    // Invalid flags are not accepted indirectly through relaxed map parameters.
    for(auto op:{satchel_cpu_probe_map_alias,satchel_cpu_probe_unmap_alias}) {
        for(auto tuple:std::array<std::array<uint64_t,3>,10>{{
            {0,data_address,page},{target,0,page},{target+1,data_address,page},{target,data_address+1,page},
            {target,data_address,0},{target,data_address,page+1},{target,data_address,UINT64_MAX},
            {UINT64_MAX-4095,data_address,page},{target,UINT64_MAX-4095,page},{data_address,data_address,page}}})
            check(op(p.get(),tuple[0],tuple[1],tuple[2])==SATCHEL_CPU_MAP_INVALID,"invalid alias range rejected without mutation");
        check(op(nullptr,target,data_address,page)==SATCHEL_CPU_MAP_INVALID,"null alias context rejected");
    }
    check(map(code_address,data_address,page)==SATCHEL_CPU_MAP_INVALID && map(target,code_address,page)==SATCHEL_CPU_MAP_INVALID &&
          map(target,heap+5*page,page)==SATCHEL_CPU_MAP_INVALID,"occupied destination, executable source and missing source rejected");
    check(map(target,data_address,page)==SATCHEL_CPU_MAP_OK &&
          map(target+8*page,target,page)==SATCHEL_CPU_MAP_INVALID && map(target+8*page,data_address,page)==SATCHEL_CPU_MAP_INVALID,
          "nested aliases and repeat aliases of locked source rejected");
    const uint64_t marker=0xabcdef;
    SatchelCPUProbeWrite transaction[]={{target,&marker,8},{data_address,&marker,8}};
    got=0;check(satchel_cpu_probe_read(p.get(),target,&got,8),"snapshot alias before failed output transaction");
    check(satchel_cpu_probe_writev(p.get(),transaction,2)==SATCHEL_CPU_WRITE_MEMORY,"second locked output fails whole multiwrite");
    uint64_t retained=0;check(satchel_cpu_probe_read(p.get(),target,&retained,8) && retained==got,"failed transaction leaves first alias destination unchanged");
    struct Busy {SatchelCPUProbe*a,*b;bool okay=false;} busy{p.get(),sibling.get()};
    satchel_cpu_probe_set_pc(p.get(),code_address);
    auto stopped=satchel_cpu_probe_run(p.get(),1,[](void*opaque)->int{
        auto&b=*static_cast<Busy*>(opaque);
        b.okay=satchel_cpu_probe_map_alias(b.a,target+8*page,data_address+page,page)==SATCHEL_CPU_MAP_BUSY &&
            satchel_cpu_probe_unmap_alias(b.b,target,data_address,page)==SATCHEL_CPU_MAP_BUSY;
        std::thread t([&b]{b.okay=b.okay && satchel_cpu_probe_unmap_alias(b.a,target,data_address,page)==SATCHEL_CPU_MAP_BUSY;});t.join();return 1;
    },&busy);
    check(stopped.reason==SATCHEL_CPU_CANCELLED && busy.okay,"alias mutation rejects callback and cross-host sibling reentry");
    p.reset();
    check(satchel_cpu_probe_test_same_backing(sibling.get(),data_address,target) &&
          satchel_cpu_probe_unmap_alias(sibling.get(),target,data_address,page)==SATCHEL_CPU_MAP_OK &&
          satchel_cpu_probe_read(sibling.get(),data_address,&retained,8) && retained==got,
          "retained map/backing outlives original CPU context and restores through sibling");
    // Failure and successful commit reservation semantics on an unrelated page.
    for(unsigned variant=0;variant<4;variant++) {
        auto exclusive=make(words({0xc85f7c01,0xc8027c03,0xd4000001}),nullptr,2*page);auto peer=shared(exclusive.get());
        if(variant>=2)check(satchel_cpu_probe_map_alias(peer.get(),target,data_address+page,page)==SATCHEL_CPU_MAP_OK,"reservation unmap setup");
        satchel_cpu_probe_set_x(exclusive.get(),3,9);satchel_cpu_probe_run(exclusive.get(),1,nullptr,nullptr);
        if(!(variant&1))satchel_cpu_probe_test_mapping_allocation_failure(2);
        const auto result=variant<2?satchel_cpu_probe_map_alias(peer.get(),target,data_address+page,page):
            satchel_cpu_probe_unmap_alias(peer.get(),target,data_address+page,page);
        check(result==((variant&1)?SATCHEL_CPU_MAP_OK:SATCHEL_CPU_MAP_ALLOCATION),"alias reservation transaction result");
        expect_stop(exclusive.get(),SATCHEL_CPU_SVC);check(x(exclusive.get(),2)==(variant&1),"alias commit clears reservations; failure preserves them");
    }
    // View cap, not byte cap, constrains excessive aliases and partial splits.
    auto limited=make(words({0xd4000001}),nullptr,3*page);
    for(unsigned i=0;i<28;i++)check(satchel_cpu_probe_resize_rw(limited.get(),heap+i*2*page,page)==SATCHEL_CPU_MAP_OK,"fill alias view budget");
    check(satchel_cpu_probe_map_alias(limited.get(),target,data_address+page,page)==SATCHEL_CPU_MAP_LIMIT &&
          satchel_cpu_probe_check_range(limited.get(),data_address,3*page,SATCHEL_CPU_WRITE),"split-and-alias beyond32 views rejects atomically");
    check(satchel_cpu_probe_map_alias(limited.get(),target,data_address,3*page)==SATCHEL_CPU_MAP_OK,"whole alias fits final32 views");
    check(satchel_cpu_probe_unmap_alias(limited.get(),target+page,data_address+page,page)==SATCHEL_CPU_MAP_LIMIT &&
          !satchel_cpu_probe_check_range(limited.get(),data_address,1,SATCHEL_CPU_READ) &&
          satchel_cpu_probe_check_range(limited.get(),target,3*page,SATCHEL_CPU_READ),"partial restore beyond32 views preserves complete existing alias");
    check(satchel_cpu_probe_unmap_alias(limited.get(),target,data_address,3*page)==SATCHEL_CPU_MAP_OK,"whole restore reduces view count");
    {
        constexpr uint64_t cap=256*1024*1024;
        SatchelCPUProbeRegion regions[]={
            {code_address,code.data(),code.size(),page,SATCHEL_CPU_READ|SATCHEL_CPU_EXECUTE},
            {data_address,nullptr,0,cap-page,SATCHEL_CPU_READ|SATCHEL_CPU_WRITE}};
        char error[160];Probe full(satchel_cpu_probe_create(regions,2,code_address,error,sizeof(error)),satchel_cpu_probe_destroy);
        check(bool(full),"allocate exact256MiB physical limit");
        check(satchel_cpu_probe_map_alias(full.get(),target,data_address,cap-page)==SATCHEL_CPU_MAP_OK &&
              satchel_cpu_probe_test_physical_bytes(full.get())==cap,"full-capacity alias does not allocate or charge duplicate physical backing");
        check(satchel_cpu_probe_resize_rw(full.get(),heap,page)==SATCHEL_CPU_MAP_LIMIT &&
              satchel_cpu_probe_unmap_alias(full.get(),target,data_address,cap-page)==SATCHEL_CPU_MAP_OK &&
              satchel_cpu_probe_resize_rw(full.get(),heap,page)==SATCHEL_CPU_MAP_LIMIT,
              "alias removal restores permissions without freeing real source capacity");
    }
    // Independent repeated page-state oracle checks source denial, physical
    // identity and bytes over varied alias/partial-unmap transition orders.
    auto model=make(words({0xd4000001}),nullptr,12*page);std::array<bool,12> aliases{};
    std::array<uint64_t,12> tags{};uint32_t random=0x654321;
    for(unsigned turn=0;turn<180;turn++) {
        random=random*1664525u+1013904223u;unsigned i=(random>>16)%aliases.size();
        auto result=aliases[i]?satchel_cpu_probe_unmap_alias(model.get(),target+i*page,data_address+i*page,page):
            satchel_cpu_probe_map_alias(model.get(),target+i*page,data_address+i*page,page);
        check(result==SATCHEL_CPU_MAP_OK,"independent page-alias state transition");aliases[i]=!aliases[i];tags[i]=turn+1;
        check(satchel_cpu_probe_write(model.get(),(aliases[i]?target:data_address)+i*page,&tags[i],8),"write active original page location");
        for(unsigned j=0;j<aliases.size();j++) {
            uint64_t observed=UINT64_MAX;
            check(satchel_cpu_probe_read(model.get(),(aliases[j]?target:data_address)+j*page,&observed,8) && observed==tags[j] &&
                  !satchel_cpu_probe_check_range(model.get(),(aliases[j]?data_address:target)+j*page,1,SATCHEL_CPU_READ),
                  "independent model preserves each page bytes and exact access rights");
            if(aliases[j])check(satchel_cpu_probe_test_same_backing(model.get(),target+j*page,data_address+j*page),"each model alias retains identical physical bytes");
        }
    }
}
static void physical_overlap_tests() {
    constexpr uint64_t page=4096,target=0x7600000000;
    auto p=make(words({0xd503201f,0xd4000001}),nullptr,6*page);auto sibling=shared(p.get());
    int overlap=99;
    auto query=[&](uint64_t a,uint64_t an,uint64_t b,uint64_t bn,int expected) {
        overlap=99;
        check(satchel_cpu_probe_physical_overlap(sibling.get(),a,an,b,bn,&overlap) && overlap==expected,
              "checked physical intersection agrees with independent expected relation");
    };
    query(data_address,page,data_address,page,1);
    query(data_address,page,data_address+page,page,0);
    query(data_address,page,data_address+page-1,2,1);
    query(data_address,page,code_address,4,0);
    check(satchel_cpu_probe_map_alias(p.get(),target,data_address+page,2*page)==SATCHEL_CPU_MAP_OK,"physical overlap fixture locks source subset");
    query(target,2*page,data_address+page,2*page,1);
    query(target,page,data_address,page,0);
    query(target,page,data_address+2*page,page,0);
    query(target+page-1,2,data_address+2*page,1,1);
    query(target,2*page,data_address,6*page,1); // Crosses RW/locked/RW source views.
    query(data_address,6*page,data_address,6*page,1);
    query(data_address,6*page,stack_address,128,0);
    query(target,2*page,code_address,4,0);
    check(satchel_cpu_probe_map_alias(p.get(),target+2*page,data_address+3*page,page)==SATCHEL_CPU_MAP_OK,
          "adjacent same-backing alias comparison fixture");
    query(target+page-3,page+6,data_address+2*page-2,page+4,1);
    // Both spans must be validated before reporting any early intersection.
    for(auto ranges:std::array<std::array<uint64_t,4>,9>{{
        {target,4*page,target,page},{target,page,target,4*page},
        {target-1,2,target,page},{target,page,target-1,2},
        {target,0,target,page},{target,page,target,0},
        {UINT64_MAX-1,4,target,page},{target,page,UINT64_MAX-1,4},
        {target+8*page,page,data_address,page}}}) {
        overlap=99;
        check(!satchel_cpu_probe_physical_overlap(p.get(),ranges[0],ranges[1],ranges[2],ranges[3],&overlap) && overlap==99,
              "holes, empty and overflow queries fail without guessing disjoint or altering output");
    }
    overlap=99;
    check(!satchel_cpu_probe_physical_overlap(nullptr,target,page,data_address,page,&overlap) && overlap==99 &&
          !satchel_cpu_probe_physical_overlap(p.get(),target,page,data_address,page,nullptr),"null physical query arguments rejected");
    // A multi-backing request is permitted, unlike a single contiguous memory
    // copy. Test equal/different allocations and intersection in only one part.
    constexpr uint64_t dynamic=0x7500000000;
    check(satchel_cpu_probe_resize_rw(p.get(),dynamic,page)==SATCHEL_CPU_MAP_OK &&
          satchel_cpu_probe_resize_rw(p.get(),dynamic+page,page)==SATCHEL_CPU_MAP_OK &&
          satchel_cpu_probe_map_alias(p.get(),target+8*page,dynamic,2*page)==SATCHEL_CPU_MAP_OK,"multi-allocation physical overlap fixture");
    query(dynamic,2*page,target+8*page,2*page,1);
    query(dynamic,2*page,target+9*page,page,1);
    query(dynamic,page,target+9*page,page,0);
    query(dynamic,2*page,target,3*page,0);
    check(satchel_cpu_probe_unmap_alias(p.get(),target+page,data_address+2*page,page)==SATCHEL_CPU_MAP_OK,
          "physical query after matched partial restoration");
    query(data_address+2*page,page,data_address+2*page,page,1);
    overlap=99;
    check(!satchel_cpu_probe_physical_overlap(p.get(),target,3*page,data_address+page,3*page,&overlap) && overlap==99,
          "removed middle alias is a hole even though outer views overlap physically");
    // Query is nonmutating even for reservations and through shared contexts.
    auto exclusive=make(words({0xc85f7c01,0xc8027c03,0xd4000001}));auto peer=shared(exclusive.get());
    satchel_cpu_probe_set_x(exclusive.get(),3,9);satchel_cpu_probe_run(exclusive.get(),1,nullptr,nullptr);
    overlap=99;check(satchel_cpu_probe_physical_overlap(peer.get(),data_address,8,data_address+8,8,&overlap) && !overlap,
                     "sibling physical query while exclusive sequence suspended");
    expect_stop(exclusive.get(),SATCHEL_CPU_SVC);check(x(exclusive.get(),2)==0,"physical query preserves reservations");
    struct Busy {SatchelCPUProbe*a,*b;bool okay=false;} busy{p.get(),sibling.get()};
    auto stopped=satchel_cpu_probe_run(p.get(),1,[](void*opaque)->int{
        auto&b=*static_cast<Busy*>(opaque);int out=99;
        b.okay=!satchel_cpu_probe_physical_overlap(b.a,data_address,1,target,1,&out) && out==99 &&
            !satchel_cpu_probe_physical_overlap(b.b,data_address,1,target,1,&out) && out==99;
        std::thread t([&b]{int out=99;b.okay=b.okay && !satchel_cpu_probe_physical_overlap(b.b,data_address,1,target,1,&out) && out==99;});
        t.join();return 1;
    },&busy);
    check(stopped.reason==SATCHEL_CPU_CANCELLED && busy.okay,"physical query rejects running/sibling/reentrant/cross-host access");
    // Dense original byte-range comparison through locked source and aliases.
    uint32_t random=0x11223344;
    for(unsigned i=0;i<300;i++) {
        random=random*1664525u+1013904223u;uint64_t a=(random>>8)%page;
        random=random*1664525u+1013904223u;uint64_t b=(random>>8)%page;
        random=random*1664525u+1013904223u;uint64_t an=1+(random>>8)%(page-a);
        random=random*1664525u+1013904223u;uint64_t bn=1+(random>>8)%(page-b);
        query(data_address+page+a,an,target+b,bn,a<b+bn && b<a+an);
    }
    p.reset();query(data_address+page,page,target,page,1);
}
// Independent oracle searches ordered positive float bit patterns by exact
// rational distance, rather than reproducing the implementation's bit cut.
static satchel_cpu::Binary32Result uint_float_oracle(uint64_t value,uint32_t prior) {
    using Wide=unsigned __int128;
    if(!value)return {0,prior};
    auto scaled=[](uint32_t bits)->Wide {
        return Wide((bits&0x7fffff)|0x800000)<<((bits>>23)-127);
    };
    const Wide target=Wide(value)<<23;
    uint32_t low=0x3f800000,high=0x5f800000;
    while(low<high) {
        const uint32_t middle=low+(high-low+1)/2;
        if(scaled(middle)<=target)low=middle;else high=middle-1;
    }
    const Wide floor=scaled(low);
    if(floor==target)return {low,prior};
    const Wide down=target-floor,up=scaled(low+1)-target;
    return {low+uint32_t(up<down || (up==down && (low&1))),prior|16u};
}
// Independent binary-rational oracle: visit each significand bit at its
// absolute power of two, partitioning integral/fractional/overflow bits.
// No production exponent branch or truncated-significand calculation is reused.
static satchel_cpu::UInt64Result fcvtzu_bit_oracle(uint64_t bits,uint32_t prior) {
    const unsigned exponent=unsigned((bits>>52)&2047);const bool negative=bool(bits>>63);
    if(exponent==2047)return {(bits&UINT64_C(0xfffffffffffff))||negative?0:UINT64_MAX,prior|1u};
    const uint64_t numerator=(bits&UINT64_C(0xfffffffffffff))|(exponent?UINT64_C(1)<<52:0);
    const int scale=exponent?int(exponent)-1075:-1074;
    uint64_t integer=0;bool fraction=false,overflow=false;
    for(unsigned bit=0;bit<53;bit++)if(numerator&(UINT64_C(1)<<bit)) {
        const int position=scale+int(bit);
        if(position<0)fraction=true;else if(position>=64)overflow=true;else integer|=UINT64_C(1)<<unsigned(position);
    }
    if(negative&&(integer||overflow))return {0,prior|1u};
    if(overflow)return {UINT64_MAX,prior|1u};
    return {integer,prior|(fraction?16u:0u)};
}
static void fcvtzu2d_tests(const char *fixtures) {
    using satchel_cpu::UInt64Result;
    struct Edge {uint64_t bits,value;uint32_t flags;};
    const Edge edges[]={
        {0,0,0},{UINT64_C(0x8000000000000000),0,0},
        {1,0,16},{UINT64_C(0x8000000000000001),0,16},
        {UINT64_C(0x000fffffffffffff),0,16},{UINT64_C(0x0010000000000000),0,16},
        {UINT64_C(0x3fe0000000000000),0,16},{UINT64_C(0xbfe0000000000000),0,16},
        {UINT64_C(0x3fefffffffffffff),0,16},{UINT64_C(0xbfefffffffffffff),0,16},
        {UINT64_C(0x3ff0000000000000),1,0},{UINT64_C(0xbff0000000000000),0,1},
        {UINT64_C(0x3ff0000000000001),1,16},{UINT64_C(0xbff0000000000001),0,1},
        {UINT64_C(0x3ff8000000000000),1,16},{UINT64_C(0xbff8000000000000),0,1},
        {UINT64_C(0x433fffffffffffff),UINT64_C(0x1fffffffffffff),0},
        {UINT64_C(0x43dfffffffffffff),UINT64_C(0x7ffffffffffffc00),0},
        {UINT64_C(0x43e0000000000000),UINT64_C(0x8000000000000000),0},
        {UINT64_C(0x43efffffffffffff),UINT64_C(0xfffffffffffff800),0},
        {UINT64_C(0x43f0000000000000),UINT64_MAX,1},
        {UINT64_C(0x43f0000000000001),UINT64_MAX,1},
        {UINT64_C(0xc3efffffffffffff),0,1},{UINT64_C(0x7fefffffffffffff),UINT64_MAX,1},
        {UINT64_C(0x7ff0000000000000),UINT64_MAX,1},{UINT64_C(0xfff0000000000000),0,1},
        {UINT64_C(0x7ff8000000000000),0,1},{UINT64_C(0xfff8000000001234),0,1},
        {UINT64_C(0x7ff0000000000001),0,1},{UINT64_C(0xfff0000000000001),0,1}};
    std::vector<uint64_t> values;
    for(const auto&e:edges){UInt64Result r{};check(satchel_cpu::binary64_to_uint64(e.bits,0,0,r)&&r.bits==e.value&&r.fpsr==e.flags,"FCVTZU explicit edge exact value/flag");values.push_back(e.bits);}
    for(unsigned exponent=0;exponent<2048;exponent++)for(uint64_t fraction:{UINT64_C(0),UINT64_C(1),UINT64_C(0x8000000000000),UINT64_C(0xfffffffffffff)})for(uint64_t sign:{UINT64_C(0),UINT64_C(0x8000000000000000)})values.push_back(sign|(uint64_t(exponent)<<52)|fraction);
    uint64_t random=UINT64_C(0xad46bc9f98765e21);auto next=[&](){random^=random<<13;random^=random>>7;random^=random<<17;return random;};
    for(unsigned i=0;i<2048;i++)values.push_back(next());
    for(uint64_t input:values)for(uint32_t control:{0u,0x02000000u})for(uint32_t prior:{0u,0x0800008eu,0x0800009fu}) {
        UInt64Result r{};const auto expected=fcvtzu_bit_oracle(input,prior);uint64_t native[9]={input,0};probe_native_fcvtzu2d_status(native,0,control,prior);
        check(satchel_cpu::binary64_to_uint64(input,control,prior,r)&&r.bits==expected.bits&&r.fpsr==expected.fpsr,"FCVTZU exact bit-position oracle and cumulative flags");
        check(native[2]==r.bits&&native[3]==0&&native[8]==r.fpsr,"FCVTZU native vector value and full FPSR agree");
        check(native[4]==native[6]&&native[5]==native[7],"FCVTZU native oracle restores host environment");
    }
    // Lane-order independence: an invalid lane must not erase inexact raised by
    // the other lane, including when invalid appears second.
    for(uint64_t invalid:{UINT64_C(0xbff8000000000000),UINT64_C(0x43f0000000000000),UINT64_C(0x7ff8000000000000)})for(bool reverse:{false,true}) {
        uint64_t native[9]={reverse?invalid:UINT64_C(0xbfe0000000000000),reverse?UINT64_C(0xbfe0000000000000):invalid};probe_native_fcvtzu2d_status(native,0,0,0);
        check(native[8]==17,"FCVTZU native lane status IOC and IXC both accumulate");
        auto p=make(words({0x6ee1b820}));check(satchel_cpu_probe_set_q(p.get(),1,reinterpret_cast<const uint8_t*>(native)),"seed FCVTZU lane exception pair");auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
        std::array<uint8_t,16> got{};uint32_t control=7,status=9;
        check(stop.reason==SATCHEL_CPU_BUDGET&&stop.steps==1&&satchel_cpu_probe_get_q(p.get(),0,got.data())&&!std::memcmp(got.data(),native+2,16),"FCVTZU two lanes match native exception pair");
        check(satchel_cpu_probe_get_fp_status(p.get(),&control,&status)==SATCHEL_CPU_FP_OK&&status==17,"FCVTZU instruction cumulative IOC/IXC independent of order");
    }
    for(unsigned bit=0;bit<32;bit++) {
        UInt64Result out{UINT64_C(0x123456789abcdef0),0x76543210};
        if(bit!=25)check(!satchel_cpu::binary64_to_uint64(1,1u<<bit,0,out)&&out.bits==UINT64_C(0x123456789abcdef0)&&out.fpsr==0x76543210,"FCVTZU invalid FPCR preserves output");
        if(!(0x0800009fu&(1u<<bit)))check(!satchel_cpu::binary64_to_uint64(1,0,1u<<bit,out)&&out.bits==UINT64_C(0x123456789abcdef0)&&out.fpsr==0x76543210,"FCVTZU invalid FPSR preserves output");
    }
    fenv_t saved{},altered{},after{};check(fegetenv(&saved)==0,"save FCVTZU host environment");
    for(uint32_t mode:{0x00400000u,0x00800000u,0x00c00000u}) {
        altered=saved;altered.__fpcr=mode|0x01000000;altered.__fpsr=0x0800009f;check(fesetenv(&altered)==0,"set hostile FCVTZU host environment");
        for(const auto&e:edges){UInt64Result out{};check(satchel_cpu::binary64_to_uint64(e.bits,0,0,out)&&out.bits==e.value&&out.fpsr==e.flags,"FCVTZU independent of host rounding/flush");}
        check(fegetenv(&after)==0&&after.__fpcr==altered.__fpcr&&after.__fpsr==altered.__fpsr,"FCVTZU leaves host FP state unchanged");
    }
    check(fesetenv(&saved)==0,"restore FCVTZU host environment");
    const auto code=fixture(fixtures,"fcvtzu2d");
    for(unsigned seed=0;seed<64;seed++) {
        std::array<uint8_t,112> initial{},expected{},actual{};initial.fill(0xa5);uint64_t inputs[2]={edges[seed%30].bits,seed<30?edges[(seed+1)%30].bits:next()};std::memcpy(initial.data(),inputs,16);expected=initial;
        fenv_t env{},profile{};check(fegetenv(&env)==0,"save FCVTZU fixture native environment");profile=env;profile.__fpcr=seed&1?0x02000000:0;profile.__fpsr=0;check(fesetenv(&profile)==0,"set native FCVTZU fixture environment");
        uint64_t ok=probe_fcvtzu2d(expected.data());check(fegetenv(&after)==0,"read native FCVTZU fixture flags");check(fesetenv(&env)==0,"restore native FCVTZU fixture environment");
        auto p=make(code,initial.data(),initial.size());check(satchel_cpu_probe_test_set_known_fpcr(p.get(),profile.__fpcr),"set guest FCVTZU controls");expect_stop(p.get(),SATCHEL_CPU_SVC);
        check(ok==1&&x(p.get(),0)==1,"FCVTZU preserves full NZCV");check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==expected,"five assembled FCVTZU alias cases and guards equal native");
        uint32_t control=7,status=9;check(satchel_cpu_probe_get_fp_status(p.get(),&control,&status)==SATCHEL_CPU_FP_OK&&control==profile.__fpcr&&status==after.__fpsr,"FCVTZU fixture FPSR equals native");
    }
    for(unsigned rd=0;rd<32;rd++)for(unsigned mapping=0;mapping<4;mapping++) {
        unsigned rn=mapping==0?rd:mapping==1?(rd+1)%32:mapping==2?31:0;auto p=make(words({0x6ee1b800|(rn<<5)|rd}));auto child=shared(p.get());
        std::array<std::array<uint8_t,16>,32> vectors{};std::array<uint64_t,31> gprs{};
        for(unsigned reg=0;reg<32;reg++){uint64_t v[2]={edges[reg%30].bits,edges[(reg+13)%30].bits};std::memcpy(vectors[reg].data(),v,16);check(satchel_cpu_probe_set_q(p.get(),reg,vectors[reg].data())&&satchel_cpu_probe_set_q(child.get(),reg,vectors[reg].data()),"seed FCVTZU vector and sibling bank");}
        for(unsigned reg=0;reg<31;reg++){gprs[reg]=UINT64_C(0x76543210abcd0000)+reg;check(satchel_cpu_probe_set_x(p.get(),reg,gprs[reg]),"seed FCVTZU GPR bank");}
        check(satchel_cpu_probe_test_conversion_state(p.get(),0),"seed FCVTZU DN/cumulative status");uint64_t native[9]{};std::memcpy(native,vectors[rn].data(),16);probe_native_fcvtzu2d_status(native,0,0x02000000,0x0800008f);
        std::array<uint8_t,16> expected{};std::memcpy(expected.data(),native+2,16);auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(stop.reason==SATCHEL_CPU_BUDGET&&stop.steps==1&&stop.pc==code_address+4,"FCVTZU checked single step");
        for(unsigned reg=0;reg<31;reg++)check(x(p.get(),reg)==gprs[reg],"FCVTZU preserves GPRs");
        for(unsigned reg=0;reg<32;reg++){std::array<uint8_t,16> got{};check(satchel_cpu_probe_get_q(p.get(),reg,got.data())&&got==(reg==rd?expected:vectors[reg]),"FCVTZU full Q alias destination only");check(satchel_cpu_probe_get_q(child.get(),reg,got.data())&&got==vectors[reg],"FCVTZU sibling vectors unchanged");}
        uint32_t control=7,status=9;check(satchel_cpu_probe_get_fp_status(p.get(),&control,&status)==SATCHEL_CPU_FP_OK&&control==0x02000000&&status==native[8],"FCVTZU full cumulative status");check(satchel_cpu_probe_get_fp_status(child.get(),&control,&status)==SATCHEL_CPU_FP_OK&&control==0&&status==0,"FCVTZU sibling status unchanged");
    }
    for(unsigned mode:{1u,2u,3u}) {
        auto p=make(words({0x6ee1b821}));std::array<uint8_t,16> before{},got{};before.fill(0xbe);satchel_cpu_probe_set_q(p.get(),1,before.data());check(satchel_cpu_probe_test_conversion_state(p.get(),mode),"seed FCVTZU invalid profile");
        auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(stop.reason==SATCHEL_CPU_UNSUPPORTED&&!stop.steps&&stop.pc==code_address&&satchel_cpu_probe_get_q(p.get(),1,got.data())&&got==before,"FCVTZU invalid profile atomic refusal");uint32_t control=7,status=9;check(satchel_cpu_probe_get_fp_status(p.get(),&control,&status)==SATCHEL_CPU_FP_INVALID&&control==7&&status==9,"terminal FCVTZU query preserves output");
    }
    // Public excluded FCVTZU2S/4S/scalarD, FCVTZS2D, FCVTPU2D,
    // half-vector and fixed-point forms. No broad bit-flipped neighbor guesses.
    for(uint32_t op:{0x2ea1b820u,0x6ea1b820u,0x7ee1b820u,0x4ee1b820u,0x6ee1a820u,0x6ef9b820u,0x6f40fc20u,0x2ee1b820u}) {
        auto p=make(words({op}));std::array<uint8_t,16> before{},got{};before.fill(0x71);satchel_cpu_probe_set_q(p.get(),0,before.data());auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(stop.reason==SATCHEL_CPU_UNSUPPORTED&&!stop.steps&&satchel_cpu_probe_get_q(p.get(),0,got.data())&&got==before,"excluded conversion profiles refuse unchanged");
    }
    auto reservation=make(words({0xc85f7c01,0x6ee1b820,0xc8027c03,0xd4000001}));satchel_cpu_probe_set_x(reservation.get(),3,0x76543210);expect_stop(reservation.get(),SATCHEL_CPU_SVC);uint64_t memory=0;check(x(reservation.get(),2)==0&&satchel_cpu_probe_read(reservation.get(),data_address,&memory,8)&&memory==0x76543210,"FCVTZU preserves exclusive monitor");
    auto cancel=make(words({0x6ee1b820}));std::array<uint8_t,16> before{},got{};before.fill(0x8a);satchel_cpu_probe_set_q(cancel.get(),0,before.data());auto stop=satchel_cpu_probe_run(cancel.get(),1,[](void*){return 1;},nullptr);check(stop.reason==SATCHEL_CPU_CANCELLED&&!stop.steps&&satchel_cpu_probe_get_q(cancel.get(),0,got.data())&&got==before,"FCVTZU cancellation before mutation");uint32_t control=7,status=9;check(satchel_cpu_probe_get_fp_status(cancel.get(),&control,&status)==SATCHEL_CPU_FP_OK&&control==0&&status==0,"FCVTZU cancellation preserves FP state");
}

// Independent exact rational equality: strip trailing powers of two from each
// finite format's integer numerator, then compare normalized numerator/exponent.
// This does not reproduce production leading-bit placement or bias adjustment.
static bool widen_exact_equal(uint32_t input,uint64_t output) {
    const uint32_t inMagnitude=input&0x7fffffff;const uint64_t outMagnitude=output&UINT64_C(0x7fffffffffffffff);
    if((uint64_t(input>>31))!=(output>>63))return false;
    if(!inMagnitude)return !outMagnitude;
    const unsigned ie=inMagnitude>>23,oe=unsigned(outMagnitude>>52);
    if(ie==255||!oe||oe==2047)return false;
    uint64_t inNumerator=(input&0x7fffff)|(ie?0x800000:0),outNumerator=(output&UINT64_C(0xfffffffffffff))|UINT64_C(0x10000000000000);
    int inExponent=ie?int(ie)-150:-149,outExponent=int(oe)-1075;
    while(!(inNumerator&1)){inNumerator>>=1;inExponent++;}
    while(!(outNumerator&1)){outNumerator>>=1;outExponent++;}
    return inNumerator==outNumerator&&inExponent==outExponent;
}
static uint32_t fcvtl_encoding(bool upper,unsigned rd,unsigned rn) {
    return 0x0e617800|(upper?0x40000000:0)|(rn<<5)|rd;
}
static void fcvtl_tests(const char *fixtures) {
    using satchel_cpu::Binary64Result;
    struct Exact {uint32_t input;uint64_t bits;uint32_t flags;};
    const Exact edges[]={
        {0,0,0},{0x80000000,UINT64_C(0x8000000000000000),0},
        {1,UINT64_C(0x36a0000000000000),0},{2,UINT64_C(0x36b0000000000000),0},
        {0x007fffff,UINT64_C(0x380fffffc0000000),0},{0x00800000,UINT64_C(0x3810000000000000),0},
        {0x00800001,UINT64_C(0x3810000020000000),0},{0x3f800000,UINT64_C(0x3ff0000000000000),0},
        {0x3f800001,UINT64_C(0x3ff0000020000000),0},{0x7f7fffff,UINT64_C(0x47efffffe0000000),0},
        {0xff7fffff,UINT64_C(0xc7efffffe0000000),0},{0x7f800000,UINT64_C(0x7ff0000000000000),0},
        {0xff800000,UINT64_C(0xfff0000000000000),0},{0x7f800001,UINT64_C(0x7ff8000020000000),1},
        {0xff800001,UINT64_C(0xfff8000020000000),1},{0x7fc00123,UINT64_C(0x7ff8002460000000),0},
        {0xffc00123,UINT64_C(0xfff8002460000000),0},{0x7fffffff,UINT64_C(0x7fffffffe0000000),0}};
    std::vector<uint32_t> inputs;
    for(const auto&e:edges) {
        Binary64Result result{};uint64_t native[8]={e.input};probe_native_widen32_status(native,0,0,0);
        check(satchel_cpu::binary32_to_binary64(e.input,0,0,result)&&result.bits==e.bits&&result.fpsr==e.flags,"integer widening matches explicit bit-pattern edge");
        check(native[2]==e.bits&&native[3]==e.flags,"native S-to-D validates exact subnormal normal infinity and NaN edges");
        inputs.push_back(e.input);
    }
    for(unsigned exp=0;exp<256;exp++)for(uint32_t fraction:{0u,1u,2u,3u,0x1fffffu,0x3fffffu,0x400000u,0x400001u,0x7ffffeu,0x7fffffu})for(uint32_t sign:{0u,0x80000000u})inputs.push_back(sign|(exp<<23)|fraction);
    for(unsigned bit=0;bit<23;bit++)for(uint32_t sign:{0u,0x80000000u}){inputs.push_back(sign|(1u<<bit));inputs.push_back(sign|0x7f800000|(1u<<bit));}
    uint64_t random=UINT64_C(0x78ab5612dead731f);auto next=[&](){random^=random<<13;random^=random>>7;random^=random<<17;return uint32_t(random);};
    for(unsigned i=0;i<4096;i++)inputs.push_back(next());
    for(uint32_t input:inputs)for(uint32_t control:{0u,0x02000000u})for(uint32_t prior:{0u,0x0800008fu,0x0800009fu}) {
        Binary64Result result{7,9};uint64_t native[8]={input};probe_native_widen32_status(native,0,control,prior);
        check(satchel_cpu::binary32_to_binary64(input,control,prior,result)&&result.bits==native[2]&&result.fpsr==native[3],"integer widening matches native raw bits and complete cumulative FPSR");
        check(native[4]==native[6]&&native[5]==native[7],"native widening oracle restores entire host FP environment");
        const uint32_t mag=input&0x7fffffff;
        if(mag<0x7f800000)check(widen_exact_equal(input,result.bits)&&result.fpsr==prior,"finite widening preserves exact independent rational value with no new flags");
        else if(mag==0x7f800000)check(result.bits==((uint64_t(input>>31)<<63)|UINT64_C(0x7ff0000000000000))&&result.fpsr==prior,"infinity preserves sign and flags");
        else {
            const bool signaling=!(input&0x400000);check(result.fpsr==(prior|(signaling?1u:0u)),"only signaling NaN adds IOC during widening");
            check(control?result.bits==UINT64_C(0x7ff8000000000000):((result.bits>>63)==(input>>31)&&((result.bits>>29)&0x7fffff)==((input&0x7fffff)|0x400000)&&(result.bits&0x1fffffff)==0),"NaN sign and payload widen and quiet exactly unless DN overrides");
        }
    }
    for(unsigned bit=0;bit<32;bit++) {
        Binary64Result out{UINT64_C(0xabcdef0123456789),0x98765432};
        if(bit!=25)check(!satchel_cpu::binary32_to_binary64(1,1u<<bit,0,out)&&out.bits==UINT64_C(0xabcdef0123456789)&&out.fpsr==0x98765432,"unsupported widening FPCR rejects without output changes");
        if(!(0x0800009fu&(1u<<bit)))check(!satchel_cpu::binary32_to_binary64(1,0,1u<<bit,out)&&out.bits==UINT64_C(0xabcdef0123456789)&&out.fpsr==0x98765432,"unsupported widening FPSR rejects without output changes");
    }
    fenv_t saved{},altered{},after{};check(fegetenv(&saved)==0,"save widening host environment");
    for(uint32_t mode:{0x00400000u,0x00800000u,0x00c00000u}) {
        altered=saved;altered.__fpcr=mode|0x01000000;altered.__fpsr=0x0800009f;check(fesetenv(&altered)==0,"set hostile host FP rounding flush and status");
        for(const auto&e:edges){Binary64Result out{};check(satchel_cpu::binary32_to_binary64(e.input,0,0,out)&&out.bits==e.bits&&out.fpsr==e.flags,"integer widening independent of host FP state");}
        check(fegetenv(&after)==0&&after.__fpcr==altered.__fpcr&&after.__fpsr==altered.__fpsr,"integer widening never changes host FP environment");
    }
    check(fesetenv(&saved)==0,"restore widening host environment");
    const auto code=fixture(fixtures,"fcvtl");
    for(unsigned seed=0;seed<128;seed++) {
        std::array<uint8_t,192> initial{},expected{},actual{};initial.fill(0xa5);
        for(unsigned lane=0;lane<4;lane++){uint32_t value=seed<18?edges[(seed+lane)%18].input:next();std::memcpy(initial.data()+4*lane,&value,4);}expected=initial;
        fenv_t nativeEnv{},profile{};check(fegetenv(&nativeEnv)==0,"save native vector widening environment");profile=nativeEnv;profile.__fpcr=seed&1?0x02000000:0;profile.__fpsr=0;check(fesetenv(&profile)==0,"set native vector widening0/DN");
        const uint64_t ok=probe_fcvtl(expected.data());check(fegetenv(&after)==0,"read native vector widening flags");check(fesetenv(&nativeEnv)==0,"restore native vector widening environment");
        auto p=make(code,initial.data(),initial.size());check(satchel_cpu_probe_test_set_known_fpcr(p.get(),profile.__fpcr),"set guest matching widening controls");expect_stop(p.get(),SATCHEL_CPU_SVC);
        check(ok==1&&x(p.get(),0)==1,"FCVTL and FCVTL2 preserve all NZCV bits");
        check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==expected,"all10 assembled widening alias cases match native complete output and guards");
        uint32_t control=1,status=2;check(satchel_cpu_probe_get_fp_status(p.get(),&control,&status)==SATCHEL_CPU_FP_OK&&control==profile.__fpcr&&status==after.__fpsr,"native/interpreted vector widening cumulative flags match");
    }
    for(bool upper:{false,true})for(unsigned rd=0;rd<32;rd++)for(unsigned alias=0;alias<4;alias++) {
        const unsigned rn=alias==0?rd:alias==1?(rd+1)%32:alias==2?31:0;const uint32_t op=fcvtl_encoding(upper,rd,rn);
        auto p=make(words({op,0xd4000001}));auto child=shared(p.get());
        std::array<std::array<uint8_t,16>,32> vectors{};std::array<uint64_t,31> gpr{};
        for(unsigned reg=0;reg<32;reg++){for(unsigned lane=0;lane<4;lane++){uint32_t v=edges[(reg+lane)%18].input;std::memcpy(vectors[reg].data()+4*lane,&v,4);}check(satchel_cpu_probe_set_q(p.get(),reg,vectors[reg].data())&&satchel_cpu_probe_set_q(child.get(),reg,vectors[reg].data()),"seed all widening and sibling Q registers");}
        for(unsigned reg=0;reg<31;reg++){gpr[reg]=UINT64_C(0xfedcba9876540000)+reg;check(satchel_cpu_probe_set_x(p.get(),reg,gpr[reg]),"seed all widening GPRs");}
        check(satchel_cpu_probe_test_conversion_state(p.get(),0),"set DN/cumulative flags for widening instruction");
        std::array<uint8_t,16> expected{};uint32_t status=0x0800008f;
        for(unsigned lane=0;lane<2;lane++){uint32_t input;std::memcpy(&input,vectors[rn].data()+4*(lane+(upper?2:0)),4);uint64_t native[8]={input};probe_native_widen32_status(native,0,0x02000000,status);std::memcpy(expected.data()+8*lane,&native[2],8);status=uint32_t(native[3]);}
        auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(stop.reason==SATCHEL_CPU_BUDGET&&stop.steps==1&&stop.pc==code_address+4,"widening commits one checked instruction");
        for(unsigned reg=0;reg<31;reg++)check(x(p.get(),reg)==gpr[reg],"widening preserves full GPR bank");
        for(unsigned reg=0;reg<32;reg++){std::array<uint8_t,16> got{};check(satchel_cpu_probe_get_q(p.get(),reg,got.data())&&got==(reg==rd?expected:vectors[reg]),"widening changes only full Q destination including same-register and Q31");check(satchel_cpu_probe_get_q(child.get(),reg,got.data())&&got==vectors[reg],"widening does not touch sibling Q bank");}
        uint32_t control=1,flags=2;check(satchel_cpu_probe_get_fp_status(p.get(),&control,&flags)==SATCHEL_CPU_FP_OK&&control==0x02000000&&flags==status,"widening status accumulates selected lanes only");check(satchel_cpu_probe_get_fp_status(child.get(),&control,&flags)==SATCHEL_CPU_FP_OK&&control==0&&flags==0,"widening preserves sibling controls/status");
    }
    for(bool upper:{false,true})for(bool selectedNaN:{false,true}) {
        auto p=make(words({fcvtl_encoding(upper,0,1)}));std::array<uint32_t,4> input{};
        for(unsigned lane=0;lane<4;lane++){bool selected=(lane>=2)==upper;input[lane]=(selected==selectedNaN)?0x7f800123:0x3f800000;}
        check(satchel_cpu_probe_set_q(p.get(),1,reinterpret_cast<const uint8_t*>(input.data())),"seed selected/discarded signaling NaN halves");
        auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);uint32_t control=1,status=2;
        check(stop.reason==SATCHEL_CPU_BUDGET&&satchel_cpu_probe_get_fp_status(p.get(),&control,&status)==SATCHEL_CPU_FP_OK&&status==(selectedNaN?1u:0u),"discarded half signaling NaN never raises IOC; selected half does");
        std::array<uint8_t,16> got{},expected{};uint64_t bits=selectedNaN?UINT64_C(0x7ff8002460000000):UINT64_C(0x3ff0000000000000);std::memcpy(expected.data(),&bits,8);std::memcpy(expected.data()+8,&bits,8);check(satchel_cpu_probe_get_q(p.get(),0,got.data())&&got==expected,"selected half alone determines both full destination lanes");
    }
    for(bool upper:{false,true})for(unsigned mode:{1u,2u,3u}) {
        auto p=make(words({fcvtl_encoding(upper,1,1)}));std::array<uint8_t,16> before{},got{};before.fill(0xbd);satchel_cpu_probe_set_q(p.get(),1,before.data());check(satchel_cpu_probe_test_conversion_state(p.get(),mode),"seed widening invalid mode");
        auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(stop.reason==SATCHEL_CPU_UNSUPPORTED&&!stop.steps&&stop.pc==code_address&&satchel_cpu_probe_get_q(p.get(),1,got.data())&&got==before,"unknown status and unsupported controls refuse before any lane writes");
        uint32_t control=7,status=9;check(satchel_cpu_probe_get_fp_status(p.get(),&control,&status)==SATCHEL_CPU_FP_INVALID&&control==7&&status==9,"terminal widening refusal preserves FP query outputs");
    }
    for(unsigned size=0;size<4;size++)for(bool upper:{false,true}) {
        const uint32_t op=0x0e217820|(size<<22)|(upper?0x40000000:0);auto p=make(words({op}));std::array<uint8_t,16> before{},zero{},got{};before.fill(0xa5);satchel_cpu_probe_set_q(p.get(),0,before.data());satchel_cpu_probe_set_q(p.get(),1,zero.data());
        auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(stop.reason==(size==1?SATCHEL_CPU_BUDGET:SATCHEL_CPU_UNSUPPORTED)&&stop.steps==unsigned(size==1),"only S-to-D allocation admitted; half and reserved sizes stopped");check(satchel_cpu_probe_get_q(p.get(),0,got.data())&&got==(size==1?zero:before),"allocation refusal preserves entire destination");
    }
    // Narrowing FCVTN/FCVTXN, signed conversions and scalar widening remain
    // outside this exact family. All words are original public fixture values.
    for(uint32_t op:{0x0e616820u,0x4e616820u,0x2e616820u,0x6e616820u,0x0e617c20u,0x2e617820u,0x5e617820u,0x1e22c020u}) {
        auto p=make(words({op}));std::array<uint8_t,16> before{},got{};before.fill(0xa3);satchel_cpu_probe_set_q(p.get(),0,before.data());auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
        check(stop.reason==SATCHEL_CPU_UNSUPPORTED&&!stop.steps&&satchel_cpu_probe_get_q(p.get(),0,got.data())&&got==before,"unreviewed narrowing/scalar/signed/reserved neighbors remain stopped");
    }
    for(bool upper:{false,true}) {
        const uint32_t op=fcvtl_encoding(upper,0,1);auto p=make(words({0xc85f7c01,op,0xc8027c03,0xd4000001}));satchel_cpu_probe_set_x(p.get(),3,0x12345678);expect_stop(p.get(),SATCHEL_CPU_SVC);uint64_t memory=0;
        check(x(p.get(),2)==0&&satchel_cpu_probe_read(p.get(),data_address,&memory,8)&&memory==0x12345678,"register-only widening retains exclusive reservation and does not touch memory");
        auto cancel=make(words({op}));std::array<uint8_t,16> before{},got{};before.fill(0x34);satchel_cpu_probe_set_q(cancel.get(),0,before.data());auto stop=satchel_cpu_probe_run(cancel.get(),1,[](void*){return 1;},nullptr);
        check(stop.reason==SATCHEL_CPU_CANCELLED&&!stop.steps&&satchel_cpu_probe_get_q(cancel.get(),0,got.data())&&got==before,"cancellation precedes widening destination/status commit");uint32_t control=7,status=9;check(satchel_cpu_probe_get_fp_status(cancel.get(),&control,&status)==SATCHEL_CPU_FP_OK&&control==0&&status==0,"cancellation preserves known widening status");
    }
}

// Original exact-distance reference: represent every finite product as an
// unsigned integer in units2^-298 (9 limbs). No production rounding shifts or
// host floating arithmetic. Binary-search adjacent binary32 values, then
// compare full rational distances, with explicit even-tie selection.
struct MulExact {
    std::array<uint64_t,9> limb{};
    static MulExact scaled(uint64_t significand,unsigned shift) {
        MulExact r;const unsigned word=shift/64,bit=shift%64;
        check(word<9,"reference rational fits576 bits");r.limb[word]=significand<<bit;
        if(bit&&word+1<9)r.limb[word+1]=significand>>(64-bit);
        return r;
    }
    int compare(const MulExact &b)const {
        for(int i=8;i>=0;i--)if(limb[i]!=b.limb[i])return limb[i]<b.limb[i]?-1:1;
        return 0;
    }
    MulExact minus(const MulExact &b)const {
        MulExact r;uint64_t borrow=0;
        for(unsigned i=0;i<9;i++) {
            const uint64_t x=limb[i],y=b.limb[i],difference=x-y;
            r.limb[i]=difference-borrow;borrow=(x<y)||(difference<borrow);
        }
        check(!borrow,"reference subtraction ordered");return r;
    }
};
static MulExact float_rational(uint32_t bits) {
    const unsigned e=bits>>23;return MulExact::scaled((bits&0x7fffff)|(e?0x800000:0),unsigned((e?int(e)-150:-149)+298));
}
static satchel_cpu::Binary32Result mul32_oracle(uint32_t a,uint32_t b,uint32_t fpcr,uint32_t prior) {
    constexpr uint32_t infinity=0x7f800000,quiet=0x400000;
    const uint32_t ax=a&0x7fffffff,bx=b&0x7fffffff,sign=(a^b)&0x80000000;
    const bool an=ax>infinity,bn=bx>infinity,as=an&&!(a&quiet),bs=bn&&!(b&quiet);
    if(an||bn)return {(fpcr&0x02000000)?0x7fc00000:(as?a:bs?b:an?a:b)|quiet,prior|((as||bs)?1u:0u)};
    if(ax==infinity||bx==infinity)return (!ax||!bx)?satchel_cpu::Binary32Result{0x7fc00000,prior|1}:satchel_cpu::Binary32Result{sign|infinity,prior};
    const unsigned ae=ax>>23,be=bx>>23;
    const uint64_t product=uint64_t((ax&0x7fffff)|(ae?0x800000:0))*((bx&0x7fffff)|(be?0x800000:0));
    if(!product)return {sign,prior};
    const int exponent=(ae?int(ae)-150:-149)+(be?int(be)-150:-149);
    const auto exact=MulExact::scaled(product,unsigned(exponent+298));
    // Overflow midpoint = 2^128 - 2^103. Exact tie rounds to infinity.
    const auto overflowMid=MulExact::scaled(1,426).minus(MulExact::scaled(1,401));
    if(exact.compare(overflowMid)>=0)return {sign|infinity,prior|20};
    uint32_t lo=0,hi=0x7f7fffff;
    while(lo<hi) {
        const uint32_t mid=lo+(hi-lo+1)/2;
        if(float_rational(mid).compare(exact)<=0)lo=mid;else hi=mid-1;
    }
    const auto lower=float_rational(lo);uint32_t rounded=lo;
    const bool lost=lower.compare(exact)!=0;
    if(lost&&lo<0x7f7fffff) {
        const auto down=exact.minus(lower),up=float_rational(lo+1).minus(exact);
        const int order=down.compare(up);if(order>0||(order==0&&(lo&1)))rounded=lo+1;
    }
    const bool tiny=exact.compare(float_rational(0x00800000))<0;
    return {sign|rounded,prior|(lost?16u:0u)|((lost&&tiny)?8u:0u)};
}
static uint32_t indexed_mul_encoding(bool wide,bool q,unsigned index,unsigned rd,unsigned rn,unsigned rm) {
    return 0x0f809000|(q?0x40000000:0)|(wide?0x00400000:0)|
        ((wide?index:index>>1)<<11)|(!wide?(index&1)<<21:0)|(rm<<16)|(rn<<5)|rd;
}
static void fmulindexed_tests(const char *fixtures) {
    using satchel_cpu::Binary32Result;
    struct Exact {uint32_t a,b,bits,flags;};
    const Exact edges[]={
        {0x3f800000,0x40000000,0x40000000,0},{0x80000000,0x3f800000,0x80000000,0},
        {0x80000000,0xbf800000,0,0},{0,0x7f800000,0x7fc00000,1},
        {0xff800000,0x80000000,0x7fc00000,1},{0xff800000,0x3f800000,0xff800000,0},
        {0x7f800001,0x7fc00123,0x7fc00001,1},{0x7fc00123,0xff800456,0xffc00456,1},
        {0xffc00123,0x7fc00456,0xffc00123,0},{0xff800123,0x7f800456,0xffc00123,1},
        {1,0x3f800000,1,0},{1,0x3f000000,0,24},{3,0x3f000000,2,24},
        {0x80000001,0x3f000000,0x80000000,24},{0x00800000,0x3f000000,0x00400000,0},
        {0x00800000,0x3f7fffff,0x00800000,24},{1,1,0,24},
        {0x7f7fffff,0x40000000,0x7f800000,20},{0x7f7fff00,0x3f800000,0x7f7fff00,0},
        {0x3f800001,0x3f800001,0x3f800002,16},{0x3f800001,0x3fc00000,0x3fc00002,16},
        {0x3f800003,0x3fc00000,0x3fc00004,16}};
    std::vector<std::array<uint32_t,2>> pairs;
    for(const auto &e:edges) {
        auto expected=mul32_oracle(e.a,e.b,0,0);Binary32Result actual{};
        check(expected.bits==e.bits&&expected.fpsr==e.flags,"distance reference matches explicit FMUL edge/tie/tininess");
        check(satchel_cpu::multiply_binary32(e.a,e.b,0,0,actual)&&actual.bits==e.bits&&actual.fpsr==e.flags,"integer multiply matches explicit IEEE edge");
        pairs.push_back({e.a,e.b});
    }
    const uint32_t points[]={0,1,2,3,0x007fffff,0x00800000,0x00800001,0x3effffff,0x3f000000,0x3f7fffff,0x3f800000,0x3f800001,0x3fc00000,0x40000000,0x7f7fffff,0x7f800000,0x7f800001,0x7fc00123,0x7fffffff};
    for(auto a:points)for(auto b:points)for(uint32_t sign:{0u,0x80000000u})pairs.push_back({a^sign,b});
    uint64_t random=UINT64_C(0x123cbaa985712357);auto next=[&](){random^=random<<13;random^=random>>7;random^=random<<17;return uint32_t(random);};
    for(unsigned i=0;i<4096;i++)pairs.push_back({next(),next()});
    for(unsigned exponent=0;exponent<255;exponent++)for(uint32_t frac:{0u,1u,0x3fffffu,0x7fffffu})
        pairs.push_back({(exponent<<23)|frac,((254-exponent)<<23)|(next()&0x7fffff)});
    for(const auto &pair:pairs)for(uint32_t fpcr:{0u,0x02000000u})for(uint32_t prior:{0u,0x0800008fu}) {
        auto expected=mul32_oracle(pair[0],pair[1],fpcr,prior);Binary32Result actual{7,9};
        uint64_t native[8]={pair[0],pair[1]};probe_native_mul32_status(native,0,fpcr,prior);
        check(satchel_cpu::multiply_binary32(pair[0],pair[1],fpcr,prior,actual)&&actual.bits==expected.bits&&actual.fpsr==expected.fpsr,"FMUL32 equals independent exact-distance reference");
        check(native[2]==actual.bits&&native[3]==actual.fpsr,"FMUL32 matches native raw bits and cumulative FPSR");
        check(native[4]==native[6]&&native[5]==native[7],"native test oracle restores complete host FP environment");
    }
    for(unsigned bit=0;bit<32;bit++) {
        Binary32Result out{0xaabbccdd,0x11223344};
        if(bit!=25)check(!satchel_cpu::multiply_binary32(1,1,1u<<bit,0,out)&&out.bits==0xaabbccdd&&out.fpsr==0x11223344,"unmodeled FPCR refuses model atomically");
        if(!(0x0800009fu&(1u<<bit)))check(!satchel_cpu::multiply_binary32(1,1,0,1u<<bit,out)&&out.bits==0xaabbccdd&&out.fpsr==0x11223344,"unmodeled FPSR refuses model atomically");
    }
    fenv_t saved{},altered{},after{};check(fegetenv(&saved)==0,"snapshot FMUL host environment");
    for(uint32_t mode:{0x00400000u,0x00800000u,0x00c00000u}) {
        altered=saved;altered.__fpcr=mode|0x01000000;altered.__fpsr=0x0800009f;check(fesetenv(&altered)==0,"change host rounding/FZ/flags for isolation");
        for(const auto&e:edges){Binary32Result got{};check(satchel_cpu::multiply_binary32(e.a,e.b,0,0,got)&&got.bits==e.bits&&got.fpsr==e.flags,"integer FMUL ignores hostile host FP environment");}
        check(fegetenv(&after)==0&&after.__fpcr==altered.__fpcr&&after.__fpsr==altered.__fpsr,"integer FMUL leaves host floating state unchanged");
    }
    check(fesetenv(&saved)==0,"restore FMUL host environment");
    // The assembled native reference covers every allowed arrangement/index,
    // destructive alias and real register31. Compare whole guarded output.
    const auto code=fixture(fixtures,"fmulindexed");
    for(unsigned seed=0;seed<128;seed++) {
        std::array<uint8_t,1024> initial{},expected{},actual{};initial.fill(0xa5);
        for(unsigned i=0;i<8;i++){uint32_t v=seed<19?points[(seed+i)%19]:next();std::memcpy(initial.data()+4*i,&v,4);}expected=initial;
        fenv_t nativeEnv{},nearest{};check(fegetenv(&nativeEnv)==0,"save indexed native environment");nearest=nativeEnv;nearest.__fpcr=seed&1?0x02000000:0;nearest.__fpsr=0;
        check(fesetenv(&nearest)==0,"native indexed profile0/DN");const uint64_t ok=probe_fmulindexed(expected.data());
        check(fegetenv(&after)==0,"read native indexed flags");check(fesetenv(&nativeEnv)==0,"restore indexed reference environment");
        auto p=make(code,initial.data(),initial.size());check(satchel_cpu_probe_test_set_known_fpcr(p.get(),nearest.__fpcr),"set guest matching native control");expect_stop(p.get(),SATCHEL_CPU_SVC);
        check(ok==1&&x(p.get(),0)==1,"native/interpreted indexed FMUL preserves NZCV");
        check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==expected,"all60 assembled indexed cases match raw native buffers and guards");
        uint32_t control=0,status=0;check(satchel_cpu_probe_get_fp_status(p.get(),&control,&status)==SATCHEL_CPU_FP_OK&&control==nearest.__fpcr&&status==after.__fpsr,"all indexed lanes preserve cumulative native flags");
    }
    // Full32-register banks, all indices, Q31 and original all-lane oracle.
    for(unsigned arrangement=0;arrangement<3;arrangement++) {
        const bool wide=arrangement==2,q=arrangement!=0;const unsigned width=wide?8:4,lanes=(q?16:8)/width;
        for(unsigned index=0;index<(wide?2u:4u);index++)for(unsigned rd=0;rd<32;rd++)for(unsigned alias=0;alias<4;alias++) {
            const unsigned rn=alias==1||alias==3?rd:(rd+1)%32,rm=alias==2||alias==3?rd:(rd+17)%32;
            const uint32_t op=indexed_mul_encoding(wide,q,index,rd,rn,rm);auto p=make(words({op,0xd4000001}));auto child=shared(p.get());
            std::array<std::array<uint8_t,16>,32> vectors{};std::array<uint64_t,31> gprs{};
            for(unsigned r=0;r<32;r++){for(unsigned j=0;j<4;j++){uint32_t v=points[(r+j+index)%19]^(r&1?0x80000000u:0);std::memcpy(vectors[r].data()+4*j,&v,4);}check(satchel_cpu_probe_set_q(p.get(),r,vectors[r].data())&&satchel_cpu_probe_set_q(child.get(),r,vectors[r].data()),"seed full FMUL and sibling SIMD banks");}
            for(unsigned r=0;r<31;r++){gprs[r]=UINT64_C(0xff00fe0000000000)+r;check(satchel_cpu_probe_set_x(p.get(),r,gprs[r]),"seed full indexed GPR bank");}
            check(satchel_cpu_probe_test_conversion_state(p.get(),0),"seed DN cumulative flags for indexed multiply");
            std::array<uint8_t,16> want{};uint32_t status=0x0800008f;
            for(unsigned lane=0;lane<lanes;lane++) {
                if(wide) {uint64_t a,b;std::memcpy(&a,vectors[rn].data()+8*lane,8);std::memcpy(&b,vectors[rm].data()+8*index,8);uint64_t native[8]={a,b};probe_native_fp_status(native,1,0x02000000,status);std::memcpy(want.data()+8*lane,&native[2],8);status=uint32_t(native[3]);}
                else {uint32_t a,b;std::memcpy(&a,vectors[rn].data()+4*lane,4);std::memcpy(&b,vectors[rm].data()+4*index,4);auto v=mul32_oracle(a,b,0x02000000,status);std::memcpy(want.data()+4*lane,&v.bits,4);status=v.fpsr;}
            }
            const auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(stop.reason==SATCHEL_CPU_BUDGET&&stop.steps==1&&stop.pc==code_address+4,"indexed FMUL commits exactly one instruction");
            for(unsigned r=0;r<31;r++)check(x(p.get(),r)==gprs[r],"indexed multiply preserves all GPRs");
            for(unsigned r=0;r<32;r++){std::array<uint8_t,16> got{};check(satchel_cpu_probe_get_q(p.get(),r,got.data())&&got==(r==rd?want:vectors[r]),"only whole SIMD destination changes including Q0 upperzero");check(satchel_cpu_probe_get_q(child.get(),r,got.data())&&got==vectors[r],"indexed multiply preserves sibling SIMD bank");}
            uint32_t fpcr=1,fpsr=2;check(satchel_cpu_probe_get_fp_status(p.get(),&fpcr,&fpsr)==SATCHEL_CPU_FP_OK&&fpcr==0x02000000&&fpsr==status,"indexed flags combine every lane and prior cumulative flags");
            check(satchel_cpu_probe_get_fp_status(child.get(),&fpcr,&fpsr)==SATCHEL_CPU_FP_OK&&fpcr==0&&fpsr==0,"sibling status untouched");
        }
    }
    for(unsigned arrangement=0;arrangement<3;arrangement++)for(unsigned mode:{1u,2u,3u}) {
        const uint32_t op=indexed_mul_encoding(arrangement==2,arrangement!=0,1,1,1,1);auto p=make(words({op}));std::array<uint8_t,16> before{},got{};before.fill(0xbe);
        check(satchel_cpu_probe_set_q(p.get(),1,before.data())&&satchel_cpu_probe_test_conversion_state(p.get(),mode),"seed indexed control/status refusal");
        auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(stop.reason==SATCHEL_CPU_UNSUPPORTED&&!stop.steps&&stop.pc==code_address&&satchel_cpu_probe_get_q(p.get(),1,got.data())&&got==before,"invalid control or unknown status has no partial lane write");
        uint32_t fpcr=7,fpsr=9;check(satchel_cpu_probe_get_fp_status(p.get(),&fpcr,&fpsr)==SATCHEL_CPU_FP_INVALID&&fpcr==7&&fpsr==9,"terminal refusal leaves FP query output unchanged");
    }
    // Every size/Q/L/H combination in exact vector family, including both
    // reserved D encodings; half, scalar, FMULX and FMLA remain excluded.
    for(unsigned sz=0;sz<2;sz++)for(unsigned q=0;q<2;q++)for(unsigned l=0;l<2;l++)for(unsigned h=0;h<2;h++) {
        uint32_t op=0x0f809020|(sz<<22)|(q<<30)|(l<<21)|(h<<11);auto p=make(words({op}));
        std::array<uint8_t,16> zero{},before{};before.fill(0x45);satchel_cpu_probe_set_q(p.get(),0,before.data());satchel_cpu_probe_set_q(p.get(),1,zero.data());
        auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);const bool allowed=!sz||(q&&!l);
        check(stop.reason==(allowed?SATCHEL_CPU_BUDGET:SATCHEL_CPU_UNSUPPORTED)&&stop.steps==unsigned(allowed),"exact Q/size/index allocation matrix");
        std::array<uint8_t,16> got{};check(satchel_cpu_probe_get_q(p.get(),0,got.data())&&got==(allowed?zero:before),"allocation matrix full destination preservation or zero product");
    }
    for(uint32_t op:{0x0f009020u,0x4f009020u,0x5f809020u,0x5fc09020u,0x2f809020u,0x6f809020u,0x0f801020u,0x4f801020u,0x2e20dc20u,0x6e20dc20u,0x1e200820u}) {
        auto p=make(words({op}));std::array<uint8_t,16> before{},got{};before.fill(0x57);satchel_cpu_probe_set_q(p.get(),0,before.data());auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
        check(stop.reason==SATCHEL_CPU_UNSUPPORTED&&!stop.steps&&satchel_cpu_probe_get_q(p.get(),0,got.data())&&got==before,"unreviewed half scalar FMULX accumulate and ordinary-vector neighbors remain excluded");
    }
    for(unsigned arrangement=0;arrangement<3;arrangement++) {
        const uint32_t op=indexed_mul_encoding(arrangement==2,arrangement!=0,1,0,1,2);
        auto p=make(words({0xc85f7c01,op,0xc8027c03,0xd4000001}));satchel_cpu_probe_set_x(p.get(),3,0x12345678);expect_stop(p.get(),SATCHEL_CPU_SVC);uint64_t memory=0;
        check(x(p.get(),2)==0&&satchel_cpu_probe_read(p.get(),data_address,&memory,8)&&memory==0x12345678,"indexed FMUL never changes memory/exclusive reservation");
        auto cancel=make(words({op}));std::array<uint8_t,16> before{},got{};before.fill(0x49);satchel_cpu_probe_set_q(cancel.get(),0,before.data());
        auto stop=satchel_cpu_probe_run(cancel.get(),1,[](void*){return 1;},nullptr);
        check(stop.reason==SATCHEL_CPU_CANCELLED&&!stop.steps&&satchel_cpu_probe_get_q(cancel.get(),0,got.data())&&got==before,"cancellation wins before indexed multiply destination/status commit");
        uint32_t fpcr=7,fpsr=9;check(satchel_cpu_probe_get_fp_status(cancel.get(),&fpcr,&fpsr)==SATCHEL_CPU_FP_OK&&fpcr==0&&fpsr==0,"cancellation leaves known FP state intact");
    }
}

// Independent positive-double oracle: binary search adjacent encoding values,
// compare exact distances in units2^-52 (at most117bits). No rounding shifts,
// significand truncation or host FP from the production algorithm is reused.
static satchel_cpu::Binary64Result uint_double_oracle(uint64_t value,uint32_t prior) {
    if(!value)return {0,prior};
    using W=unsigned __int128;
    auto exact=[](uint64_t bits)->W {unsigned exponent=unsigned((bits>>52)&2047)-1023;
        return W((UINT64_C(1)<<52)|(bits&UINT64_C(0xfffffffffffff)))<<exponent;};
    const W target=W(value)<<52;
    uint64_t lo=UINT64_C(0x3ff0000000000000),hi=UINT64_C(0x43f0000000000000);
    while(lo<hi){const uint64_t middle=lo+(hi-lo+1)/2;if(exact(middle)<=target)lo=middle;else hi=middle-1;}
    if(exact(lo)==target)return {lo,prior};
    const W below=target-exact(lo),above=exact(lo+1)-target;
    return {(above<below||(above==below&&(lo&1)))?lo+1:lo,prior|16};
}
// Independent exact-rational oracle: binary search positive binary64 encodings
// and compare cross-products/exponent positions. No quotient/remainder rounding.
static satchel_cpu::Binary64Result finite_divide_oracle(uint64_t a,uint64_t b,uint32_t prior) {
    using W=unsigned __int128;
    struct Rational {W m;int e;};
    auto decode=[](uint64_t bits)->Rational{const unsigned e=unsigned((bits>>52)&2047);return {W((bits&UINT64_C(0xfffffffffffff))|(e?UINT64_C(1)<<52:0)),e?int(e)-1075:-1074};};
    auto compare=[](W a,int ae,W b,int be)->int {
        if(!a||!b)return a?1:b?-1:0;
        auto top=[](W x){unsigned n=0;while(x>>=1)++n;return int(n);};
        const int at=top(a)+ae,bt=top(b)+be;
        if(at!=bt)return at<bt?-1:1;
        if(ae>be)a<<=unsigned(ae-be);else b<<=unsigned(be-ae);
        return a<b?-1:a>b?1:0;
    };
    const auto x=decode(a),y=decode(b);check(x.m&&y.m&&((a>>52)&2047)!=2047&&((b>>52)&2047)!=2047,"finite oracle precondition");
    auto against=[&](uint64_t bits){const auto z=decode(bits);return compare(x.m,x.e,y.m*z.m,y.e+z.e);};
    uint64_t lo=0,hi=UINT64_C(0x7fefffffffffffff);
    while(lo<hi){const uint64_t mid=lo+(hi-lo+1)/2;if(against(mid)>=0)lo=mid;else hi=mid-1;}
    const uint64_t sign=(a^b)&UINT64_C(0x8000000000000000);
    if(against(lo)==0)return {sign|lo,prior};
    const auto lower=decode(lo);
    const Rational upper=lo==UINT64_C(0x7fefffffffffffff)?Rational{1,1024}:decode(lo+1);
    const int base=std::min(lower.e,upper.e);
    const W sum=(lower.m<<unsigned(lower.e-base))+(upper.m<<unsigned(upper.e-base));
    const int half=compare(x.m,x.e+1,y.m*sum,y.e+base);
    uint64_t rounded=lo+(half>0||(half==0&&(lo&1)));
    uint32_t flags=16;
    if(rounded==UINT64_C(0x7ff0000000000000))flags|=4;
    if(compare(x.m,x.e,y.m,y.e-1022)<0)flags|=8;
    return {sign|rounded,prior|flags};
}
static void fdiv_tests(const char *fixtures) {
    using satchel_cpu::Binary64Result;
    struct Exact{uint64_t a,b,bits;uint32_t flags;};
    const Exact cases[]={
        {0,UINT64_C(0x3ff0000000000000),0,0},
        {UINT64_C(0x8000000000000000),UINT64_C(0x3ff0000000000000),UINT64_C(0x8000000000000000),0},
        {UINT64_C(0x3ff0000000000000),UINT64_C(0x4000000000000000),UINT64_C(0x3fe0000000000000),0},
        {UINT64_C(0x3ff0000000000000),UINT64_C(0x4008000000000000),UINT64_C(0x3fd5555555555555),16},
        {0,0,UINT64_C(0x7ff8000000000000),1},
        {UINT64_C(0x7ff0000000000000),UINT64_C(0x7ff0000000000000),UINT64_C(0x7ff8000000000000),1},
        {UINT64_C(0x7ff0000000000000),0,UINT64_C(0x7ff0000000000000),0},
        {UINT64_C(0x3ff0000000000000),0,UINT64_C(0x7ff0000000000000),2},
        {UINT64_C(0x3ff0000000000000),UINT64_C(0x8000000000000000),UINT64_C(0xfff0000000000000),2},
        {UINT64_C(0x3ff0000000000000),UINT64_C(0xfff0000000000000),UINT64_C(0x8000000000000000),0},
        {1,UINT64_C(0x4000000000000000),0,24},
        {3,UINT64_C(0x4000000000000000),2,24},
        {5,UINT64_C(0x4000000000000000),2,24},
        {UINT64_C(0x10000000000000),UINT64_C(0x4000000000000000),UINT64_C(0x8000000000000),0},
        {UINT64_C(0x1fffffffffffff),UINT64_C(0x4000000000000000),UINT64_C(0x10000000000000),24},
        {UINT64_C(0x7fefffffffffffff),UINT64_C(0x3fe0000000000000),UINT64_C(0x7ff0000000000000),20},
        {UINT64_C(0x7ff8000000000123),UINT64_C(0x7ff0000000000456),UINT64_C(0x7ff8000000000456),1},
        {UINT64_C(0xfff0000000000123),UINT64_C(0x7ff8000000000456),UINT64_C(0xfff8000000000123),1},
        {UINT64_C(0xfff8000000000123),UINT64_C(0x7ff8000000000456),UINT64_C(0xfff8000000000123),0},
    };
    for(const auto&c:cases){Binary64Result out{};uint64_t native[8]={c.a,c.b};probe_native_fdiv_status(native,0,0,0);
        check(satchel_cpu::divide_binary64(c.a,c.b,0,0,out)&&out.bits==c.bits&&out.fpsr==c.flags,"division explicit bit/status endpoints");
        check(native[2]==c.bits&&native[3]==c.flags&&native[4]==native[6]&&native[5]==native[7],"native division explicit endpoint and host FP restore");}
    const uint64_t classes[]={0,UINT64_C(0x8000000000000000),1,2,3,UINT64_C(0xfffffffffffff),UINT64_C(0x10000000000000),UINT64_C(0x10000000000001),UINT64_C(0x3fefffffffffffff),UINT64_C(0x3ff0000000000000),UINT64_C(0x3ff0000000000001),UINT64_C(0x4008000000000000),UINT64_C(0x7fefffffffffffff),UINT64_C(0x8000000000000001),UINT64_C(0xffefffffffffffff),UINT64_C(0x7ff0000000000000),UINT64_C(0xfff0000000000000),UINT64_C(0x7ff8000000000123),UINT64_C(0xfff8000000000456),UINT64_C(0x7ff0000000000789),UINT64_C(0xfff0000000000345)};
    for(uint64_t a:classes)for(uint64_t b:classes)for(uint32_t control:{0u,0x02000000u})for(uint32_t prior:{0u,0x08000080u,0x0800009fu}) {
        Binary64Result out{};uint64_t native[8]={a,b};probe_native_fdiv_status(native,0,control,prior);
        check(satchel_cpu::divide_binary64(a,b,control,prior,out)&&out.bits==native[2]&&out.fpsr==native[3],"native class/sign/NaN priority/DN/cumulative flags matrix");
        check(native[4]==native[6]&&native[5]==native[7],"native oracle restores complete host FP environment");
        if((a&UINT64_C(0x7fffffffffffffff))&&(b&UINT64_C(0x7fffffffffffffff))&&((a>>52)&2047)!=2047&&((b>>52)&2047)!=2047){auto expected=finite_divide_oracle(a,b,prior);check(out.bits==expected.bits&&out.fpsr==expected.fpsr,"independent finite rational cross-product oracle matches");}
    }
    uint64_t random=UINT64_C(0xae895a9fc1723d61);
    for(unsigned i=0;i<4096;i++) {
        random^=random<<13;random^=random>>7;random^=random<<17;uint64_t a=random;
        random^=random<<13;random^=random>>7;random^=random<<17;uint64_t b=random;
        if(((a>>52)&2047)==2047)a^=UINT64_C(1)<<52;if(((b>>52)&2047)==2047)b^=UINT64_C(1)<<52;
        if(!(a&UINT64_C(0x7fffffffffffffff)))a|=1;if(!(b&UINT64_C(0x7fffffffffffffff)))b|=1;
        const auto expected=finite_divide_oracle(a,b,0x08000080);Binary64Result out{};uint64_t native[8]={a,b};probe_native_fdiv_status(native,0,0,0x08000080);
        check(satchel_cpu::divide_binary64(a,b,0,0x08000080,out)&&out.bits==expected.bits&&out.fpsr==expected.fpsr,"random finite division exact rational comparison");
        check(out.bits==native[2]&&out.fpsr==native[3]&&native[4]==native[6]&&native[5]==native[7],"random native division/status/host-state reference");
    }
    for(unsigned bit=0;bit<32;bit++) {
        if(bit!=25){Binary64Result out{7,9};check(!satchel_cpu::divide_binary64(1,3,uint32_t{1}<<bit,0,out)&&out.bits==7&&out.fpsr==9,"unsupported FPCR has unchanged output");}
        if(!(0x0800009f&(uint32_t{1}<<bit))){Binary64Result out{7,9};check(!satchel_cpu::divide_binary64(1,3,0,uint32_t{1}<<bit,out)&&out.bits==7&&out.fpsr==9,"unsupported FPSR has unchanged output");}
    }
    fenv_t saved{},altered{},after{};check(fegetenv(&saved)==0,"save host environment for division");
    for(uint32_t rounding:{0x00400000u,0x00800000u,0x00c00000u}){altered=saved;altered.__fpcr=rounding|0x01000000;altered.__fpsr=0x0800009f;check(fesetenv(&altered)==0,"alter host rounding/FZ/status");
        for(const auto&c:cases){Binary64Result out{};check(satchel_cpu::divide_binary64(c.a,c.b,0,0,out)&&out.bits==c.bits&&out.fpsr==c.flags,"production division independent of host FP environment");}
        check(fegetenv(&after)==0&&after.__fpcr==altered.__fpcr&&after.__fpsr==altered.__fpsr,"division leaves entire host FP environment unchanged");}
    check(fesetenv(&saved)==0,"restore host FP after division isolation test");
    const auto code=fixture(fixtures,"fdiv");
    for(const auto&c:cases) {
        std::array<uint8_t,128> initial{},expected{},actual{};initial.fill(0xa5);std::memcpy(initial.data(),&c.a,8);std::memcpy(initial.data()+8,&c.b,8);expected=initial;
        fenv_t env{},nearest{};check(fegetenv(&env)==0,"save native fixture environment");nearest=env;nearest.__fpcr=0;nearest.__fpsr=0;check(fesetenv(&nearest)==0,"set native nearest-even");
        uint64_t ok=probe_fdiv(expected.data());check(fesetenv(&env)==0,"restore native fixture environment");auto p=make(code,initial.data(),initial.size());expect_stop(p.get(),SATCHEL_CPU_SVC);
        check(ok==1&&x(p.get(),0)==1,"assembled native and interpreter FDIV preserve NZCV");
        check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==expected,"assembled all destructive aliases and Q31 match full native bytes");
        for(unsigned i=0;i<6;i++){std::array<uint8_t,16>want{};std::memcpy(want.data(),&c.bits,8);check(!std::memcmp(actual.data()+32+16*i,want.data(),16),"scalar division clears upper64");}
    }
    for(auto regs:{std::array<unsigned,3>{0,1,2},{1,1,2},{2,1,2},{31,1,2},{1,31,2},{1,2,31},{31,31,31}}) {
        auto p=make(words({0x1e601800u|(regs[2]<<16)|(regs[1]<<5)|regs[0],0xd4000001}));auto sibling=shared(p.get());
        std::array<std::array<uint8_t,16>,32> q{};for(unsigned i=0;i<32;i++){q[i].fill(uint8_t(0xa0+i));const uint64_t bits=UINT64_C(0x3ff0000000000000)+i;std::memcpy(q[i].data(),&bits,8);check(satchel_cpu_probe_set_q(p.get(),i,q[i].data())&&satchel_cpu_probe_set_q(sibling.get(),i,q[i].data()),"seed full source/destination/sibling SIMD");}
        std::array<uint64_t,31>xreg{};for(unsigned i=0;i<31;i++){xreg[i]=0x1234567800000000ull+i;check(satchel_cpu_probe_set_x(p.get(),i,xreg[i]),"seed GPR preservation");}
        check(satchel_cpu_probe_test_conversion_state(p.get(),0),"known cumulative flags");uint64_t a,b;std::memcpy(&a,q[regs[1]].data(),8);std::memcpy(&b,q[regs[2]].data(),8);auto expected=finite_divide_oracle(a,b,0x0800008f);
        auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(stop.reason==SATCHEL_CPU_BUDGET&&stop.steps==1&&stop.pc==code_address+4,"division single atomic step");
        for(unsigned i=0;i<32;i++){auto want=q[i];if(i==regs[0]){want.fill(0);std::memcpy(want.data(),&expected.bits,8);}std::array<uint8_t,16>got{};check(satchel_cpu_probe_get_q(p.get(),i,got.data())&&got==want,"division only changes target Q");check(satchel_cpu_probe_get_q(sibling.get(),i,got.data())&&got==q[i],"division leaves sibling Q unchanged");}
        for(unsigned i=0;i<31;i++)check(x(p.get(),i)==xreg[i],"division leaves all GPRs unchanged");uint32_t cr=7,sr=9;check(satchel_cpu_probe_get_fp_status(p.get(),&cr,&sr)==SATCHEL_CPU_FP_OK&&cr==0x02000000&&sr==expected.fpsr,"division status accumulates exact exceptions");check(satchel_cpu_probe_get_fp_status(sibling.get(),&cr,&sr)==SATCHEL_CPU_FP_OK&&cr==0&&sr==0,"sibling status unaffected");
    }
    for(unsigned mode:{1u,2u,3u}){auto p=make(words({0x1e621820}));std::array<uint8_t,16>before{},got{};before.fill(0xa5);satchel_cpu_probe_set_q(p.get(),0,before.data());check(satchel_cpu_probe_test_conversion_state(p.get(),mode),"seed unknown/control/status refusal");auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(stop.reason==SATCHEL_CPU_UNSUPPORTED&&!stop.steps&&stop.pc==code_address&&satchel_cpu_probe_get_q(p.get(),0,got.data())&&got==before,"division refusal atomic destination/PC");uint32_t cr=7,sr=9;check(satchel_cpu_probe_get_fp_status(p.get(),&cr,&sr)==SATCHEL_CPU_FP_INVALID&&cr==7&&sr==9,"terminal refusal preserves FP query outputs");}
    auto p=make(words({0xc85f7c01,0x1e621820,0xc8027c03,0xd4000001}));satchel_cpu_probe_set_x(p.get(),3,0x12345678);expect_stop(p.get(),SATCHEL_CPU_SVC);uint64_t mem=0;check(x(p.get(),2)==0&&satchel_cpu_probe_read(p.get(),data_address,&mem,8)&&mem==0x12345678,"FDIV preserves real exclusive reservation even invalid data result");
    auto cancel=make(words({0x1e621820}));std::array<uint8_t,16>before{},got{};before.fill(0xa5);satchel_cpu_probe_set_q(cancel.get(),0,before.data());auto stop=satchel_cpu_probe_run(cancel.get(),1,[](void*){return 1;},nullptr);check(stop.reason==SATCHEL_CPU_CANCELLED&&!stop.steps&&stop.pc==code_address&&satchel_cpu_probe_get_q(cancel.get(),0,got.data())&&got==before,"cancel before FDIV state mutation");
    for(uint32_t op:{0x1e221820u,0x1ee21820u,0x1ea21820u,0x6e62fc20u,0x2e22fc20u,0x6e22fc20u,0x1e621c20u}){auto n=make(words({op}));satchel_cpu_probe_set_q(n.get(),0,before.data());auto result=satchel_cpu_probe_run(n.get(),1,nullptr,nullptr);check(result.reason==SATCHEL_CPU_UNSUPPORTED&&!result.steps&&result.pc==code_address&&satchel_cpu_probe_get_q(n.get(),0,got.data())&&got==before,"non-D/vector/reserved FDIV neighbor remains atomic refusal");}
}

static void ucvtf64_tests(const char *fixtures) {
    using satchel_cpu::Binary64Result;
    struct Exact {uint64_t input,bits;uint32_t flags;};
    const Exact exact[]={
        {0,0,0},{1,UINT64_C(0x3ff0000000000000),0},
        {UINT32_MAX,UINT64_C(0x41efffffffe00000),0},
        {(UINT64_C(1)<<53)-1,UINT64_C(0x433fffffffffffff),0},
        {UINT64_C(1)<<53,UINT64_C(0x4340000000000000),0},
        {(UINT64_C(1)<<53)+1,UINT64_C(0x4340000000000000),16},
        {(UINT64_C(1)<<53)+2,UINT64_C(0x4340000000000001),0},
        {(UINT64_C(1)<<53)+3,UINT64_C(0x4340000000000002),16},
        {UINT64_MAX,UINT64_C(0x43f0000000000000),16},
        {UINT64_MAX-1024,UINT64_C(0x43efffffffffffff),16},
        {UINT64_MAX-1023,UINT64_C(0x43f0000000000000),16},
        {UINT64_MAX-1022,UINT64_C(0x43f0000000000000),16}};
    std::vector<uint64_t> inputs;
    for(const auto &c:exact){const auto expected=uint_double_oracle(c.input,0);Binary64Result out{};uint64_t native[8]={c.input};probe_native_ucvtf64_status(native,1,0,0);
        check(expected.bits==c.bits&&expected.fpsr==c.flags,"independent distance oracle agrees with explicit double boundary");
        check(satchel_cpu::uint_to_binary64(c.input,0,0,out)&&out.bits==c.bits&&out.fpsr==c.flags,"production conversion matches exact double tie carry endpoint");
        check(native[2]==c.bits&&native[3]==c.flags&&native[4]==native[6]&&native[5]==native[7],"native double conversion matches explicit bits/status and restores host state");inputs.push_back(c.input);}
    for(unsigned exponent=0;exponent<64;exponent++) {
        const uint64_t power=UINT64_C(1)<<exponent;inputs.push_back(power);inputs.push_back(power-1);inputs.push_back(power+1);
        if(exponent>52)for(uint64_t sig:{UINT64_C(0x10000000000000),UINT64_C(0x10000000000001),UINT64_C(0x1ffffffffffffe),UINT64_C(0x1fffffffffffff)}) {
            const unsigned shift=exponent-52;const uint64_t midpoint=(sig<<shift)+(UINT64_C(1)<<(shift-1));
            inputs.push_back(midpoint-1);inputs.push_back(midpoint);inputs.push_back(midpoint+1);
        }
    }
    uint64_t random=UINT64_C(0x731befaac9d54321);
    for(unsigned i=0;i<4096;i++){random^=random<<13;random^=random>>7;random^=random<<17;inputs.push_back(random);}
    for(auto original:inputs)for(bool wide:{false,true})for(uint32_t control:{0u,0x02000000u})for(uint32_t prior:{0u,0x0800008fu,0x0800009fu}) {
        const uint64_t value=wide?original:uint64_t(uint32_t(original));auto expected=uint_double_oracle(value,prior);Binary64Result out{7,9};
        uint64_t native[8]={original};probe_native_ucvtf64_status(native,wide,control,prior);
        check(satchel_cpu::uint_to_binary64(value,control,prior,out)&&out.bits==expected.bits&&out.fpsr==expected.fpsr,"W/X double model equals independent exact-distance oracle");
        check(native[2]==out.bits&&native[3]==out.fpsr,"native W/X double raw bits and cumulative FPSR match");
        check(native[4]==native[6]&&native[5]==native[7],"native oracle restores entire host FP environment");
        if(!wide)check(out.fpsr==prior,"every zero-extended W input is exact binary64 with no added flag");
    }
    for(unsigned bit=0;bit<32;bit++)if(bit!=25) {
        Binary64Result out{0xaabbccdd,0x11223344};
        check(!satchel_cpu::uint_to_binary64(UINT64_MAX,uint32_t{1}<<bit,0,out)&&out.bits==0xaabbccdd&&out.fpsr==0x11223344,
              "unsupported rounding/traps/merging/control refuses without output mutation");
    }
    for(unsigned bit=0;bit<32;bit++)if(!(0x0800009fu&(uint32_t{1}<<bit))) {
        Binary64Result out{0xaabbccdd,0x11223344};
        check(!satchel_cpu::uint_to_binary64(UINT64_MAX,0,uint32_t{1}<<bit,out)&&out.bits==0xaabbccdd&&out.fpsr==0x11223344,
              "unmodeled FPSR rejects without writing output");
    }
    fenv_t saved{},altered{},after{};check(fegetenv(&saved)==0,"snapshot host FP environment for UCVTF64");
    for(uint32_t rounding:{0x00400000u,0x00800000u,0x00c00000u}) {
        altered=saved;altered.__fpcr=rounding|0x01000000;altered.__fpsr=0x0800009f;
        check(fesetenv(&altered)==0,"set different host rounding/FZ and exception flags");
        for(const auto &c:exact) {Binary64Result out{};check(satchel_cpu::uint_to_binary64(c.input,0,0,out)&&out.bits==c.bits&&out.fpsr==c.flags,
             "production integer conversion ignores host rounding/FZ/exception state");}
        check(fegetenv(&after)==0&&after.__fpcr==altered.__fpcr&&after.__fpsr==altered.__fpsr,"integer conversion never mutates host FP environment");
    }
    check(fesetenv(&saved)==0,"restore host environment after isolation fixture");
    // Original assembled native/interpreted program covers numeric register
    // bank aliases, W/X/ZR sources, real Q31 and all four NZCV preservation bits.
    const auto code=fixture(fixtures,"ucvtf64");
    for(const auto &c:exact) {
        std::array<uint8_t,128> initial{},expected{},actual{};initial.fill(0xa5);std::memcpy(initial.data(),&c.input,8);expected=initial;
        fenv_t nativeEnvironment{};check(fegetenv(&nativeEnvironment)==0,"snapshot assembled reference host state");
        fenv_t nearest=nativeEnvironment;nearest.__fpcr=0;nearest.__fpsr=0;check(fesetenv(&nearest)==0,"native reference uses explicit nearest-even state");
        uint64_t ok=probe_ucvtf64(expected.data());check(fesetenv(&nativeEnvironment)==0,"restore assembled reference host state");
        auto p=make(code,initial.data(),initial.size());expect_stop(p.get(),SATCHEL_CPU_SVC);
        check(ok==1&&x(p.get(),0)==1,"native/interpreted UCVTF64 preserves N/Z/C/V flags");
        check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==expected,"assembled UCVTF64 W/X/ZR/bank aliases match complete native output and guards");
        const std::array<uint64_t,6> results={uint_double_oracle(uint32_t(c.input),0).bits,uint_double_oracle(c.input,0).bits,
            uint_double_oracle(uint32_t(c.input),0).bits,uint_double_oracle(c.input,0).bits,0,0};
        for(unsigned i=0;i<6;i++){std::array<uint8_t,16> want{};std::memcpy(want.data(),&results[i],8);check(!std::memcmp(actual.data()+32+i*16,want.data(),16),"all scalar destinations clear upper64 including Q31");}
    }
    // All state-preservation checks use one instruction, with no destination
    // memory store that could mask a reservation or upper-lane bug.
    for(bool wide:{false,true})for(auto registers:{std::array<unsigned,2>{0,0},{1,1},{31,31},{31,1},{1,31}}) {
        const unsigned rd=registers[0],rn=registers[1];const uint32_t op=(wide?0x9e630000u:0x1e630000u)|(rn<<5)|rd;
        auto p=make(words({op,0xd4000001}));auto child=shared(p.get());
        std::array<uint64_t,31> gpr{};std::array<std::array<uint8_t,16>,32> vectors{};
        for(unsigned i=0;i<31;i++){gpr[i]=UINT64_MAX-(UINT64_C(1)<<39)+i;check(satchel_cpu_probe_set_x(p.get(),i,gpr[i]),"seed UCVTF64 GPRs");}
        for(unsigned i=0;i<32;i++){vectors[i].fill(uint8_t(0xa0+i));check(satchel_cpu_probe_set_q(p.get(),i,vectors[i].data())&&satchel_cpu_probe_set_q(child.get(),i,vectors[i].data()),"seed UCVTF64 and sibling SIMD state");}
        check(satchel_cpu_probe_test_conversion_state(p.get(),0),"seed owned cumulative QC/IDC/exception fixture");
        const auto expected=uint_double_oracle(rn==31?0:wide?gpr[rn]:uint64_t(uint32_t(gpr[rn])),0x0800008f);
        const auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(stop.reason==SATCHEL_CPU_BUDGET&&stop.steps==1&&stop.pc==code_address+4,"UCVTF64 commits exactly one instruction");
        for(unsigned i=0;i<31;i++)check(x(p.get(),i)==gpr[i],"conversion preserves every GPR including same numeric destination index");
        for(unsigned i=0;i<32;i++) {auto want=vectors[i];if(i==rd){want.fill(0);std::memcpy(want.data(),&expected.bits,8);}std::array<uint8_t,16> got{};
            check(satchel_cpu_probe_get_q(p.get(),i,got.data())&&got==want,"UCVTF64 changes only complete scalar destination");
            check(satchel_cpu_probe_get_q(child.get(),i,got.data())&&got==vectors[i],"UCVTF64 does not change sibling SIMD state");}
        uint32_t fpcr=1,fpsr=2;check(satchel_cpu_probe_get_fp_status(p.get(),&fpcr,&fpsr)==SATCHEL_CPU_FP_OK&&fpcr==0x02000000&&fpsr==expected.fpsr,"UCVTF64 preserves controls and cumulatively ORs only IXC");
        check(satchel_cpu_probe_get_fp_status(child.get(),&fpcr,&fpsr)==SATCHEL_CPU_FP_OK&&fpcr==0&&fpsr==0,"sibling FP status remains independent");
    }
    for(unsigned mode:{1u,2u,3u}) {
        auto p=make(words({0x9e630021,0xd4000001}));std::array<uint8_t,16> before{},afterQ{};before.fill(0xbd);
        check(satchel_cpu_probe_set_q(p.get(),1,before.data())&&satchel_cpu_probe_set_x(p.get(),1,UINT64_MAX)&&satchel_cpu_probe_test_conversion_state(p.get(),mode),"seed CPU conversion refusal fixture");
        auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
        check(stop.reason==SATCHEL_CPU_UNSUPPORTED&&!stop.steps&&stop.pc==code_address&&x(p.get(),1)==UINT64_MAX&&satchel_cpu_probe_get_q(p.get(),1,afterQ.data())&&afterQ==before,
              "unknown FP status, unsupported control and invalid modeled flags refuse before mutation");
        uint32_t fpcr=0x12345678,fpsr=0x9abcdef0;check(satchel_cpu_probe_get_fp_status(p.get(),&fpcr,&fpsr)==SATCHEL_CPU_FP_INVALID&&fpcr==0x12345678&&fpsr==0x9abcdef0,"terminal conversion refusal preserves FP query outputs");
    }

    for(bool wide:{false,true}) {
        const uint32_t op=(wide?0x9e630080u:0x1e630080u);
        auto p=make(words({0xc85f7c01,op,0xc8027c03,0xd4000001}));satchel_cpu_probe_set_x(p.get(),3,0x12345678);satchel_cpu_probe_set_x(p.get(),4,UINT64_MAX);expect_stop(p.get(),SATCHEL_CPU_SVC);
        uint64_t memory=0;check(x(p.get(),2)==0&&satchel_cpu_probe_read(p.get(),data_address,&memory,8)&&memory==0x12345678,"UCVTF64 preserves exclusive reservation");
        auto cancelled=make(words({op}));std::array<uint8_t,16> before{},got{};before.fill(0xd5);satchel_cpu_probe_set_q(cancelled.get(),0,before.data());
        auto stop=satchel_cpu_probe_run(cancelled.get(),1,[](void*){return 1;},nullptr);uint32_t cr=7,sr=9;
        check(stop.reason==SATCHEL_CPU_CANCELLED&&!stop.steps&&stop.pc==code_address&&satchel_cpu_probe_get_q(cancelled.get(),0,got.data())&&got==before&&satchel_cpu_probe_get_fp_status(cancelled.get(),&cr,&sr)==SATCHEL_CPU_FP_OK&&cr==0&&sr==0,"cancel precedes double destination/status/PC commit");
    }
    // Exact decode guards: signed, half, fixed, SIMD-source, and reserved forms.
    for(uint32_t op:{0x1e620020u,0x9e620020u,0x1ee30020u,0x9ee30020u,0x1e43fc20u,0x9e43fc20u,0x7e61d820u,0x6e61d820u,0x1ea30020u,0x9ea30020u}) {
        check((op&0x7ffffc00)!=0x1e630000,"excluded conversion does not collide with W/X D guard");
        auto p=make(words({op}));std::array<uint8_t,16> before{},got{};before.fill(0x71);satchel_cpu_probe_set_q(p.get(),0,before.data());auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
        check(stop.reason==SATCHEL_CPU_UNSUPPORTED&&!stop.steps&&stop.pc==code_address&&satchel_cpu_probe_get_q(p.get(),0,got.data())&&got==before,"unsupported neighbor has atomic destination/PC refusal");
    }
}

static void ucvtf_tests(const char *fixtures) {
    using satchel_cpu::Binary32Result;
    struct Exact {uint64_t input;uint32_t bits,flags;};
    const Exact exact[]={
        {0,0,0},{1,0x3f800000,0},{(UINT64_C(1)<<24)-1,0x4b7fffff,0},
        {UINT64_C(1)<<24,0x4b800000,0},{(UINT64_C(1)<<24)+1,0x4b800000,16},
        {(UINT64_C(1)<<24)+2,0x4b800001,0},{(UINT64_C(1)<<24)+3,0x4b800002,16},
        {(UINT64_C(1)<<25)-1,0x4c000000,16},{UINT32_MAX,0x4f800000,16},
        {UINT64_MAX,0x5f800000,16},
        // Direct uint64->float must not double-round via binary64 at this tie.
        {UINT64_MAX-(UINT64_C(1)<<39),0x5f7fffff,16},
        {UINT64_MAX-(UINT64_C(1)<<39)+1,0x5f800000,16},
        {UINT64_MAX-(UINT64_C(1)<<39)+2,0x5f800000,16}};
    std::vector<uint64_t> inputs;
    for(const auto &c:exact) {
        Binary32Result actual{};auto independent=uint_float_oracle(c.input,0);
        uint64_t native[8]={c.input};probe_native_ucvtf_status(native,1,0,0);
        check(independent.bits==c.bits&&independent.fpsr==c.flags,"independent distance oracle matches explicit UCVTF boundary");
        check(satchel_cpu::uint_to_binary32(c.input,0,0,actual)&&actual.bits==c.bits&&actual.fpsr==c.flags,
              "integer conversion matches explicit edge/tie/carry/double-rounding trap");
        check(native[2]==c.bits&&native[3]==c.flags&&native[4]==native[6]&&native[5]==native[7],
              "native ARM validates explicit direct-rounding result and restores FP environment");
        inputs.push_back(c.input);
    }
    for(unsigned exponent=0;exponent<64;exponent++) {
        const uint64_t power=UINT64_C(1)<<exponent;
        inputs.push_back(power);inputs.push_back(power-1);inputs.push_back(power+1);
        if(exponent>=24)for(uint64_t significand:{UINT64_C(0x800000),UINT64_C(0x800001),UINT64_C(0xfffffe),UINT64_C(0xffffff)}) {
            const unsigned shift=exponent-23;
            const uint64_t half=(significand<<shift)+(UINT64_C(1)<<(shift-1));
            inputs.push_back(half-1);inputs.push_back(half);inputs.push_back(half+1);
        }
    }
    uint64_t random=UINT64_C(0x97531badc0ffee13);
    for(unsigned i=0;i<4096;i++){random^=random<<13;random^=random>>7;random^=random<<17;inputs.push_back(random);}
    for(auto original:inputs)for(bool wide:{false,true})for(uint32_t control:{0u,0x02000000u})
        for(uint32_t prior:{0u,0x0800008fu,0x0800009fu}) {
            const uint64_t input=wide?original:uint64_t(uint32_t(original));
            const auto expected=uint_float_oracle(input,prior);Binary32Result actual{7,9};
            uint64_t native[8]={original};probe_native_ucvtf_status(native,wide,control,prior);
            check(satchel_cpu::uint_to_binary32(input,control,prior,actual)&&actual.bits==expected.bits&&actual.fpsr==expected.fpsr,
                  "integer UCVTF equals independent rational-distance oracle");
            check(native[2]==actual.bits&&native[3]==actual.fpsr,"W/X native conversion matches direct model and cumulative FPSR");
            check(native[4]==native[6]&&native[5]==native[7],"test-only UCVTF oracle restores entire host FPCR/FPSR");
        }
    for(unsigned bit=0;bit<32;bit++)if(bit!=25) {
        Binary32Result out{0xaabbccdd,0x11223344};
        check(!satchel_cpu::uint_to_binary32(UINT64_MAX,uint32_t{1}<<bit,0,out)&&out.bits==0xaabbccdd&&out.fpsr==0x11223344,
              "unsupported rounding/traps/merging/control refuses without output mutation");
    }
    for(unsigned bit=0;bit<32;bit++)if(!(0x0800009fu&(uint32_t{1}<<bit))) {
        Binary32Result out{0xaabbccdd,0x11223344};
        check(!satchel_cpu::uint_to_binary32(UINT64_MAX,0,uint32_t{1}<<bit,out)&&out.bits==0xaabbccdd&&out.fpsr==0x11223344,
              "unmodeled FPSR rejects without writing output");
    }
    fenv_t saved{},altered{},after{};check(fegetenv(&saved)==0,"snapshot host FP environment for UCVTF");
    for(uint32_t rounding:{0x00400000u,0x00800000u,0x00c00000u}) {
        altered=saved;altered.__fpcr=rounding|0x01000000;altered.__fpsr=0x0800009f;
        check(fesetenv(&altered)==0,"set different host rounding/FZ and exception flags");
        for(const auto &c:exact) {Binary32Result out{};check(satchel_cpu::uint_to_binary32(c.input,0,0,out)&&out.bits==c.bits&&out.fpsr==c.flags,
             "production integer conversion ignores host rounding/FZ/exception state");}
        check(fegetenv(&after)==0&&after.__fpcr==altered.__fpcr&&after.__fpsr==altered.__fpsr,"integer conversion never mutates host FP environment");
    }
    check(fesetenv(&saved)==0,"restore host environment after isolation fixture");
    // Original assembled native/interpreted program covers numeric register
    // bank aliases, W/X/ZR sources, real Q31 and all four NZCV preservation bits.
    const auto code=fixture(fixtures,"ucvtf");
    for(const auto &c:exact) {
        std::array<uint8_t,128> initial{},expected{},actual{};initial.fill(0xa5);std::memcpy(initial.data(),&c.input,8);expected=initial;
        fenv_t nativeEnvironment{};check(fegetenv(&nativeEnvironment)==0,"snapshot assembled reference host state");
        fenv_t nearest=nativeEnvironment;nearest.__fpcr=0;nearest.__fpsr=0;check(fesetenv(&nearest)==0,"native reference uses explicit nearest-even state");
        uint64_t ok=probe_ucvtf(expected.data());check(fesetenv(&nativeEnvironment)==0,"restore assembled reference host state");
        auto p=make(code,initial.data(),initial.size());expect_stop(p.get(),SATCHEL_CPU_SVC);
        check(ok==1&&x(p.get(),0)==1,"native/interpreted UCVTF preserves N/Z/C/V flags");
        check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==expected,"assembled UCVTF W/X/ZR/bank aliases match complete native output and guards");
        const std::array<uint32_t,6> results={uint_float_oracle(uint32_t(c.input),0).bits,uint_float_oracle(c.input,0).bits,
            uint_float_oracle(uint32_t(c.input),0).bits,uint_float_oracle(c.input,0).bits,0,0};
        for(unsigned i=0;i<6;i++){std::array<uint8_t,16> want{};std::memcpy(want.data(),&results[i],4);check(!std::memcmp(actual.data()+32+i*16,want.data(),16),"all scalar destinations clear upper96 including Q31");}
    }
    // All state-preservation checks use one instruction, with no destination
    // memory store that could mask a reservation or upper-lane bug.
    for(bool wide:{false,true})for(auto registers:{std::array<unsigned,2>{0,0},{1,1},{31,31},{31,1},{1,31}}) {
        const unsigned rd=registers[0],rn=registers[1];const uint32_t op=(wide?0x9e230000u:0x1e230000u)|(rn<<5)|rd;
        auto p=make(words({op,0xd4000001}));auto child=shared(p.get());
        std::array<uint64_t,31> gpr{};std::array<std::array<uint8_t,16>,32> vectors{};
        for(unsigned i=0;i<31;i++){gpr[i]=UINT64_MAX-(UINT64_C(1)<<39)+i;check(satchel_cpu_probe_set_x(p.get(),i,gpr[i]),"seed UCVTF GPRs");}
        for(unsigned i=0;i<32;i++){vectors[i].fill(uint8_t(0xa0+i));check(satchel_cpu_probe_set_q(p.get(),i,vectors[i].data())&&satchel_cpu_probe_set_q(child.get(),i,vectors[i].data()),"seed UCVTF and sibling SIMD state");}
        check(satchel_cpu_probe_test_conversion_state(p.get(),0),"seed owned cumulative QC/IDC/exception fixture");
        const auto expected=uint_float_oracle(rn==31?0:wide?gpr[rn]:uint64_t(uint32_t(gpr[rn])),0x0800008f);
        const auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(stop.reason==SATCHEL_CPU_BUDGET&&stop.steps==1&&stop.pc==code_address+4,"UCVTF commits exactly one instruction");
        for(unsigned i=0;i<31;i++)check(x(p.get(),i)==gpr[i],"conversion preserves every GPR including same numeric destination index");
        for(unsigned i=0;i<32;i++) {auto want=vectors[i];if(i==rd){want.fill(0);std::memcpy(want.data(),&expected.bits,4);}std::array<uint8_t,16> got{};
            check(satchel_cpu_probe_get_q(p.get(),i,got.data())&&got==want,"UCVTF changes only complete scalar destination");
            check(satchel_cpu_probe_get_q(child.get(),i,got.data())&&got==vectors[i],"UCVTF does not change sibling SIMD state");}
        uint32_t fpcr=1,fpsr=2;check(satchel_cpu_probe_get_fp_status(p.get(),&fpcr,&fpsr)==SATCHEL_CPU_FP_OK&&fpcr==0x02000000&&fpsr==expected.fpsr,"UCVTF preserves controls and cumulatively ORs only IXC");
        check(satchel_cpu_probe_get_fp_status(child.get(),&fpcr,&fpsr)==SATCHEL_CPU_FP_OK&&fpcr==0&&fpsr==0,"sibling FP status remains independent");
    }
    for(unsigned mode:{1u,2u,3u}) {
        auto p=make(words({0x9e230021,0xd4000001}));std::array<uint8_t,16> before{},afterQ{};before.fill(0xbd);
        check(satchel_cpu_probe_set_q(p.get(),1,before.data())&&satchel_cpu_probe_set_x(p.get(),1,UINT64_MAX)&&satchel_cpu_probe_test_conversion_state(p.get(),mode),"seed CPU conversion refusal fixture");
        auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
        check(stop.reason==SATCHEL_CPU_UNSUPPORTED&&!stop.steps&&stop.pc==code_address&&x(p.get(),1)==UINT64_MAX&&satchel_cpu_probe_get_q(p.get(),1,afterQ.data())&&afterQ==before,
              "unknown FP status, unsupported control and invalid modeled flags refuse before mutation");
        uint32_t fpcr=0x12345678,fpsr=0x9abcdef0;check(satchel_cpu_probe_get_fp_status(p.get(),&fpcr,&fpsr)==SATCHEL_CPU_FP_INVALID&&fpcr==0x12345678&&fpsr==0x9abcdef0,"terminal conversion refusal preserves FP query outputs");
    }
    // Public encoding neighbors: exact D forms now positive; SCVTF S,W/X; UCVTF H; fixed-point UCVTF;
    // scalar/vector SIMD-source conversions. None matches the direct mask.
    for(uint32_t op:{0x1e220020u,0x9e220020u,0x1e630020u,0x9e630020u,0x1ee30020u,0x9ee30020u,
                     0x1e03fc20u,0x9e03fc20u,0x7e21d820u,0x2e21d820u,0x6e21d820u,0x1ea30020u}) {
        check((op&0x7ffffc00)!=0x1e230000,"excluded public/reserved conversion does not match exact GPR S mask");
        if((op&0x7ffffc00)==0x1e630000) {
            auto p=make(words({op,0xd4000001}));std::array<uint8_t,16> before{},got{},expected{};before.fill(0xc7);
            satchel_cpu_probe_set_q(p.get(),0,before.data());satchel_cpu_probe_set_x(p.get(),1,UINT64_MAX);
            const uint64_t bits=(op&0x80000000)?UINT64_C(0x43f0000000000000):UINT64_C(0x41efffffffe00000);
            std::memcpy(expected.data(),&bits,8);auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
            uint32_t cr=7,sr=9;check(stop.reason==SATCHEL_CPU_BUDGET&&stop.steps==1&&stop.pc==code_address+4&&x(p.get(),1)==UINT64_MAX&&satchel_cpu_probe_get_q(p.get(),0,got.data())&&got==expected&&satchel_cpu_probe_get_fp_status(p.get(),&cr,&sr)==SATCHEL_CPU_FP_OK&&cr==0&&sr==((op&0x80000000)?16u:0u),"former exact D neighbor now has explicit native-matched result upper clearing PC GPR and status");
            auto reservation=make(words({0xc85f7c04,op,0xc8027c03,0xd4000001}));satchel_cpu_probe_set_x(reservation.get(),1,UINT64_MAX);satchel_cpu_probe_set_x(reservation.get(),3,0x12345678);expect_stop(reservation.get(),SATCHEL_CPU_SVC);
            uint64_t memory=0;check(x(reservation.get(),2)==0&&satchel_cpu_probe_read(reservation.get(),data_address,&memory,8)&&memory==0x12345678,"same former D neighbor preserves a real exclusive reservation");
            continue;
        }
        auto p=make(words({op,0xd4000001}));std::array<uint8_t,16> before{},got{};before.fill(0xc7);satchel_cpu_probe_set_q(p.get(),0,before.data());
        auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(stop.reason==SATCHEL_CPU_UNSUPPORTED&&!stop.steps&&satchel_cpu_probe_get_q(p.get(),0,got.data())&&got==before,"unreviewed conversion neighbor remains stopped without destination write");
    }
    for(bool wide:{false,true}) {
        auto p=make(words({0xc85f7c01,(wide?0x9e230080u:0x1e230080u),0xc8027c03,0xd4000001}));
        satchel_cpu_probe_set_x(p.get(),3,UINT64_C(0x12345678));satchel_cpu_probe_set_x(p.get(),4,UINT64_MAX);expect_stop(p.get(),SATCHEL_CPU_SVC);
        uint64_t memory=0;check(x(p.get(),2)==0&&satchel_cpu_probe_read(p.get(),data_address,&memory,8)&&memory==0x12345678,"register-only UCVTF retains exclusive reservation through successful STXR");
    }
}

static void binary64_tests() {
    using satchel_cpu::Binary64Result;
    constexpr uint64_t pos_inf=UINT64_C(0x7ff0000000000000),neg_inf=UINT64_C(0xfff0000000000000);
    constexpr uint64_t one=UINT64_C(0x3ff0000000000000),two=UINT64_C(0x4000000000000000);
    constexpr uint64_t min_normal=UINT64_C(0x0010000000000000),max_normal=UINT64_C(0x7fefffffffffffff);
    constexpr uint64_t qnan=UINT64_C(0x7ff8000000000000),snan=UINT64_C(0x7ff0000000000001);
    constexpr uint64_t minus=UINT64_C(1)<<63;
    struct Case {bool multiply;uint64_t a,b,bits;uint32_t flags;};
    // Exact binary identities and manually specified IEEE/Arm edge cases. These
    // expectations do not call the implementation or a host rounding routine.
    const Case cases[]={
        {false,one,one,two,0},{true,one,two,two,0},
        {false,one,UINT64_C(0x3ca0000000000000),one,16}, // 1 + half an ULP, even.
        {false,one+1,UINT64_C(0x3ca0000000000000),one+2,16}, // odd tie rounds up.
        {false,one|minus,UINT64_C(0x3ca0000000000000),UINT64_C(0xbfefffffffffffff),0},
        {false,one,1,one,16},{false,one,minus|1,one,16},
        {false,max_normal,max_normal,pos_inf,20},{true,max_normal,two,pos_inf,20},
        {true,max_normal|minus,two,neg_inf,20},
        {false,1,1,2,0},{false,min_normal,minus|1,min_normal-1,0},
        {true,min_normal,UINT64_C(0x3fe0000000000000),UINT64_C(0x0008000000000000),0},
        {true,1,UINT64_C(0x3fe0000000000000),0,24},
        {true,3,UINT64_C(0x3fe0000000000000),2,24},
        {true,minus|1,UINT64_C(0x3fe0000000000000),minus,24},
        {true,1,1,0,24},
        {true,min_normal,one-1,min_normal,24}, // tiny before rounding, normal after.
        {false,minus,minus,minus,0},{false,minus,0,0,0},{false,one,one|minus,0,0},
        {true,minus,one,minus,0},{true,minus,one|minus,0,0},
        {false,pos_inf,neg_inf,qnan,1},{true,pos_inf,0,qnan,1},
        {true,minus,neg_inf,qnan,1},{false,pos_inf,one,pos_inf,0},
        {true,neg_inf,one,neg_inf,0},{false,qnan|0x23,one,qnan|0x23,0},
        {false,qnan|0x23,snan|minus,(qnan|minus|1),1},
        {true,snan,snan|minus,qnan|1,1},
        {true,qnan|minus|9,qnan|11,qnan|minus|9,0}};
    for(const auto&c:cases) {
        Binary64Result result{0xabcd,0x1234};
        check(satchel_cpu::binary64(c.multiply,c.a,c.b,0,0,result) && result.bits==c.bits && result.fpsr==c.flags,
              "integer binary64 matches independent exact edge-case bits and flags");
        uint64_t native[8]={c.a,c.b};probe_native_fp_status(native,c.multiply,0,0);
        check(native[2]==c.bits && native[3]==c.flags && native[4]==native[6] && native[5]==native[7],
              "native ARM agrees with independent edge case and restores host FP state");
    }
    // Every unsupported control bit, including all traps, AH/FIZ/NEP, rounding
    // and FZ, fails without writing output or changing host floating state.
    for(unsigned bit=0;bit<32;bit++)if(bit!=25) {
        Binary64Result result{0xabcd,0x1234};
        check(!satchel_cpu::binary64(false,one,two,uint32_t{1}<<bit,0,result) &&
              result.bits==0xabcd && result.fpsr==0x1234,"unsupported FPCR feature rejects atomically");
    }
    for(unsigned bit=0;bit<32;bit++)if(!((UINT32_C(0x0800009f)>>bit)&1)) {
        Binary64Result result{0xabcd,0x1234};
        check(!satchel_cpu::binary64(true,one,two,0,uint32_t{1}<<bit,result) &&
              result.bits==0xabcd && result.fpsr==0x1234,"reserved FPSR status rejects atomically");
    }
    const std::array<uint64_t,18> special={0,minus,1,minus|1,min_normal-1,min_normal,min_normal+1,
        one-1,one,one+1,two,max_normal,max_normal|minus,pos_inf,neg_inf,qnan|0x23,snan,snan|minus};
    auto compare=[&](bool multiply,uint64_t a,uint64_t b,uint32_t fpcr,uint32_t status) {
        Binary64Result result{};
        check(satchel_cpu::binary64(multiply,a,b,fpcr,status,result),"admitted scalar mode accepted");
        uint64_t native[8]={a,b};probe_native_fp_status(native,multiply,fpcr,status);
        check(result.bits==native[2] && result.fpsr==native[3],"integer binary64 equals independent native ARM values and cumulative flags");
        check(native[4]==native[6] && native[5]==native[7],"test-only reference restores entire host FPCR/FPSR");
    };
    for(bool multiply:{false,true})for(uint32_t fpcr:{0u,0x02000000u})
        for(auto a:special)for(auto b:special)for(uint32_t prior:{0u,0x0800009fu})compare(multiply,a,b,fpcr,prior);
    uint64_t random=UINT64_C(0x123456789abcdef);
    auto next=[&](){random^=random<<13;random^=random>>7;random^=random<<17;return random;};
    for(unsigned i=0;i<12000;i++) {
        uint64_t a=next(),b=next();
        if(i%4==0)b=(a^minus)+(next()%3); // cancellation near an exact operand.
        if(i%4==1){a=(a&((UINT64_C(1)<<52)-1))|((next()%4)<<52);b=(b&((UINT64_C(1)<<52)-1))|((UINT64_C(1020)+next()%8)<<52);}
        compare(bool(i&1),a,b,(i&2)?0x02000000:0,(i%5==0)?0x08000082:0);
    }
    for(unsigned gap=0;gap<=2046;gap++)for(uint64_t fraction:{uint64_t{0},UINT64_C(0x000fffffffffffff)})
        for(uint64_t sign:{uint64_t{0},minus}) {
            const uint64_t a=(UINT64_C(2046)<<52)|fraction;
            const uint64_t b=(uint64_t(2046-gap)<<52)|UINT64_C(0x000fffffffffffff)|sign;
            compare(false,a,b,0,0); // Includes both sides of the exact128-bit/gap74 boundary.
        }
    // Production arithmetic must be independent of a caller's rounding/flags;
    // unlike the test oracle it never installs a temporary host environment.
    fenv_t saved{},altered{},after{};check(fegetenv(&saved)==0,"snapshot native floating environment");
    altered=saved;altered.__fpcr=0x01c00000;altered.__fpsr=0x0800009f;
    check(fesetenv(&altered)==0,"set host rounding/FZ/flags for isolation check");
    Binary64Result isolation{};
    check(satchel_cpu::binary64(false,one,UINT64_C(0x3ca0000000000000),0,0,isolation) &&
          isolation.bits==one && isolation.fpsr==16,"guest arithmetic ignores nondefault host rounding and flags");
    check(fegetenv(&after)==0 && after.__fpcr==altered.__fpcr && after.__fpsr==altered.__fpsr,
          "production integer operation preserves host FP state bit-for-bit");
    check(fesetenv(&saved)==0,"restore test host environment");
    // Actual interpreter path, Q upper-zeroing, register31 as vector register,
    // cumulative exception status and child inheritance after arithmetic.
    for(unsigned destination:{0u,2u,31u})for(bool multiply:{false,true}) {
        const uint32_t op=(multiply?0x1e600800u:0x1e602800u)|(1u<<16)|destination;
        auto p=make(words({op,0xd4000001}));auto child=shared(p.get());
        std::array<uint64_t,2> a={multiply?max_normal:one,UINT64_MAX},b={multiply?two:UINT64_C(0x3ca0000000000000),UINT64_MAX};
        satchel_cpu_probe_set_q(p.get(),0,reinterpret_cast<uint8_t*>(a.data()));satchel_cpu_probe_set_q(p.get(),1,reinterpret_cast<uint8_t*>(b.data()));
        expect_stop(p.get(),SATCHEL_CPU_SVC);
        std::array<uint64_t,2> actual{};uint32_t control=99,status=88;
        check(satchel_cpu_probe_get_q(p.get(),destination,reinterpret_cast<uint8_t*>(actual.data())) &&
              actual[0]==(multiply?pos_inf:one) && !actual[1],"interpreter commits exact result and clears upper scalar destination bits");
        check(satchel_cpu_probe_get_fp_status(p.get(),&control,&status)==SATCHEL_CPU_FP_OK && !control && status==(multiply?20u:16u) &&
              satchel_cpu_probe_copy_fp_status(child.get(),p.get())==SATCHEL_CPU_FP_OK,"thread creation can copy accurate nonzero post-arithmetic status");
        check(satchel_cpu_probe_get_fp_status(child.get(),&control,&status)==SATCHEL_CPU_FP_OK && status==(multiply?20u:16u),
              "child owns actual cumulative FPSR flags rather than reset zero");
        uint8_t zeros[16]{};satchel_cpu_probe_set_q(p.get(),0,zeros);
        check(satchel_cpu_probe_get_fp_status(p.get(),&control,&status)==SATCHEL_CPU_FP_OK && status==(multiply?20u:16u),
              "overwriting SIMD data cannot erase cumulative arithmetic exceptions");
    }
    auto cumulative=make(words({0x1e610802,0x1e6128c7,0x1e6528a4,0xd4000001}));
    const std::array<std::array<uint64_t,2>,4> registers={{{max_normal,0},{two,0},{0,0},{snan,0}}};
    for(unsigned i=0;i<4;i++)satchel_cpu_probe_set_q(cumulative.get(),std::array<unsigned,4>{0,1,5,6}[i],
        reinterpret_cast<const uint8_t*>(registers[i].data()));
    expect_stop(cumulative.get(),SATCHEL_CPU_SVC);
    uint32_t control=99,status=88;
    check(satchel_cpu_probe_get_fp_status(cumulative.get(),&control,&status)==SATCHEL_CPU_FP_OK && status==21,
          "actual interpreter accumulates overflow/inexact/invalid and retains them after exact arithmetic");
    auto clone=shared(cumulative.get());
    check(satchel_cpu_probe_copy_fp_status(clone.get(),cumulative.get())==SATCHEL_CPU_FP_OK &&
          satchel_cpu_probe_get_fp_status(clone.get(),&control,&status)==SATCHEL_CPU_FP_OK && status==21,
          "thread clone receives full cumulative status after several operations");
    auto reserved=make(words({0xc85f7c01,0x1e610802,0xc8027c03,0xd4000001}));
    std::array<uint64_t,2> operand={one,0};
    satchel_cpu_probe_set_q(reserved.get(),0,reinterpret_cast<uint8_t*>(operand.data()));
    satchel_cpu_probe_set_q(reserved.get(),1,reinterpret_cast<uint8_t*>(operand.data()));
    satchel_cpu_probe_set_x(reserved.get(),3,9);expect_stop(reserved.get(),SATCHEL_CPU_SVC);
    check(x(reserved.get(),2)==0,"register-only floating arithmetic preserves guest exclusive reservation");
}
extern "C" uint64_t satchel_cpu_probe_test_stack_pointer(SatchelCPUProbe *);
static std::vector<uint8_t> prefetch_code(const std::vector<uint32_t> &ops) {
    std::vector<uint8_t> bytes(ops.size()*4);std::memcpy(bytes.data(),ops.data(),bytes.size());return bytes;
}
static void prefetch_unchanged(const std::vector<uint32_t> &ops,uint64_t address,uint64_t sp) {
    std::array<uint8_t,128> initial{},observed{};for(unsigned i=0;i<initial.size();i++)initial[i]=uint8_t(i*13+7);
    auto p=make(prefetch_code(ops),initial.data(),initial.size());
    std::array<uint64_t,31> gprs{};std::array<std::array<uint8_t,16>,32> vectors{};
    for(unsigned i=0;i<31;i++){gprs[i]=address+i;check(satchel_cpu_probe_set_x(p.get(),i,gprs[i]),"prefetch GPR sentinel");}
    for(unsigned i=0;i<32;i++){for(unsigned b=0;b<16;b++)vectors[i][b]=uint8_t(i*19+b*37);check(satchel_cpu_probe_set_q(p.get(),i,vectors[i].data()),"prefetch SIMD sentinel");}
    check(satchel_cpu_probe_set_sp(p.get(),sp)&&satchel_cpu_probe_test_set_known_fpcr(p.get(),0x02000000),"prefetch SP and owned FP sentinel");
    for(unsigned i=0;i<ops.size();i++) {
        const auto result=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
        check(result.reason==SATCHEL_CPU_BUDGET&&result.steps==1&&result.pc==code_address+uint64_t(i+1)*4,"prefetch exact one-step accounting without target fault");
        for(unsigned reg=0;reg<31;reg++)check(x(p.get(),reg)==gprs[reg],"prefetch preserves every GPR including hint-index register numbers");
        for(unsigned reg=0;reg<32;reg++){std::array<uint8_t,16> out{};check(satchel_cpu_probe_get_q(p.get(),reg,out.data())&&out==vectors[reg],"prefetch preserves complete SIMD bank");}
        check(satchel_cpu_probe_test_stack_pointer(p.get())==sp,"prefetch has no SP alignment check or writeback");
        uint32_t control=99,status=99;check(satchel_cpu_probe_get_fp_status(p.get(),&control,&status)==SATCHEL_CPU_FP_OK&&control==0x02000000&&status==0,"prefetch preserves FP controls/status");
    }
    check(satchel_cpu_probe_read(p.get(),data_address,observed.data(),observed.size())&&observed==initial,"prefetch never changes mapped bytes");
}
static void prefetch_tests(const char *fixtures) {
    std::array<uint8_t,128> memory{},copy{};for(unsigned i=0;i<memory.size();i++)memory[i]=uint8_t(i*29+3);copy=memory;
    for(uint64_t address:{uint64_t(reinterpret_cast<uintptr_t>(memory.data())),UINT64_C(0),UINT64_C(1),UINT64_MAX}) {
        auto p=make(fixture(fixtures,"prefetch"),memory.data(),memory.size());check(satchel_cpu_probe_set_x(p.get(),0,address),"native hint target fixture");
        fenv_t before{},after{};check(fegetenv(&before)==0,"prefetch native FP before");
        const auto native=probe_prefetch(reinterpret_cast<void*>(uintptr_t(address)));expect_stop(p.get(),SATCHEL_CPU_SVC);
        check(native==1&&x(p.get(),0)==1,"independent assembler all32 hints/fourforms/extensions preserve native NZCV without target faults");
        check(memory==copy,"native prefetch has no host data write");std::array<uint8_t,128> out{};check(satchel_cpu_probe_read(p.get(),data_address,out.data(),out.size())&&out==copy,"native/interpreted prefetch leaves original bytes intact");
        check(fegetenv(&after)==0&&before.__fpcr==after.__fpcr&&before.__fpsr==after.__fpsr,"prefetch preserves host floating environment");
    }
    std::vector<uint32_t> ops;
    for(unsigned hint=0;hint<32;hint++) {
        for(unsigned immediate:{0u,1u,4095u})ops.push_back(0xf9800000|(immediate<<10)|hint);
        ops.push_back(0xf98003e0|hint); // SP base, no stack alignment check.
        for(int immediate:{-256,-1,0,255})ops.push_back(0xf8800000|((uint32_t(immediate)&511)<<12)|hint);
        for(int immediate:{-262144,0,262143})ops.push_back(0xd8000000|((uint32_t(immediate)&0x7ffff)<<5)|hint);
        for(unsigned option:{2u,3u,6u,7u})for(unsigned shift:{0u,1u})ops.push_back(0xf8a00800|(1u<<16)|(option<<13)|(shift<<12)|hint);
        ops.push_back(0xf8a06800|(31u<<16)|(31u<<5)|hint); // Rm31 is ZR; base31 is SP.
    }
    for(uint64_t address:{UINT64_C(0),UINT64_C(1),UINT64_MAX,data_address,code_address})prefetch_unchanged(ops,address,address);
    // Valid target permission NONE and guest-writable mappings are irrelevant
    // to a hint; actual loads from the same inaccessible span still fault.
    std::array<uint8_t,4096> data{};SatchelCPUProbeRegion regions[]={{code_address,ops.data(),ops.size()*4,ops.size()*4,5},{data_address,data.data(),data.size(),data.size(),3}};char error[160];Probe none(satchel_cpu_probe_create(regions,2,code_address,error,sizeof(error)),satchel_cpu_probe_destroy);check(bool(none),"prefetch protected fixture");check(satchel_cpu_probe_set_permissions(none.get(),data_address,4096,3,0)==SATCHEL_CPU_MAP_OK&&satchel_cpu_probe_set_x(none.get(),0,data_address),"prefetch NONE target fixture");check(satchel_cpu_probe_run(none.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET,"prefetch ignores target NONE permission");
    for(uint32_t hint:{0xf9800000u,0xf8800000u,0xd8000000u,0xf8a06800u}) {
        auto p=make(words({0xc85f7c01,hint,0xc8027c03,0xd4000001}));satchel_cpu_probe_set_x(p.get(),3,9);expect_stop(p.get(),SATCHEL_CPU_SVC);check(x(p.get(),2)==0,"all prefetch forms preserve local exclusive reservation");
        auto fp=make(words({0x1e610802,hint,0xd4000001}));std::array<uint64_t,2>a={UINT64_C(0x7fefffffffffffff),0},b={UINT64_C(0x4000000000000000),0};satchel_cpu_probe_set_q(fp.get(),0,reinterpret_cast<uint8_t*>(a.data()));satchel_cpu_probe_set_q(fp.get(),1,reinterpret_cast<uint8_t*>(b.data()));expect_stop(fp.get(),SATCHEL_CPU_SVC);uint32_t control=99,status=99;check(satchel_cpu_probe_get_fp_status(fp.get(),&control,&status)==SATCHEL_CPU_FP_OK&&status==20,"prefetch preserves actual cumulative FP exceptions");
        auto cancelled=make(words({hint}));auto result=satchel_cpu_probe_run(cancelled.get(),1,[](void*){return 1;},nullptr);check(result.reason==SATCHEL_CPU_CANCELLED&&!result.steps&&result.pc==code_address,"prefetch cannot bypass cancellation");check(satchel_cpu_probe_run(cancelled.get(),0,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET,"prefetch cannot bypass zero budget");
        uint64_t bp=code_address;check(satchel_cpu_probe_set_breakpoints(cancelled.get(),&bp,1),"prefetch breakpoint fixture");result=satchel_cpu_probe_run(cancelled.get(),1,nullptr,nullptr);check(result.reason==SATCHEL_CPU_BREAKPOINT&&!result.steps&&result.pc==code_address,"prefetch cannot bypass breakpoint before effects");
        auto fault=make(words({hint,0xf9400001}));satchel_cpu_probe_set_x(fault.get(),0,1);result=satchel_cpu_probe_run(fault.get(),2,nullptr,nullptr);check(result.reason==SATCHEL_CPU_MEMORY_FAULT&&result.steps==1&&result.pc==code_address+4,"prefetch does not weaken following actual load fault");
    }
    std::vector<uint32_t> refused={0xf8800400,0xf8800c00,0xf8800800,0xdc000000};
    for(unsigned option:{0u,1u,4u,5u})for(unsigned scaled:{0u,1u})refused.push_back(0xf8a00800|(option<<13)|(scaled<<12));
    for(uint32_t op:refused){auto p=make(words({op}));const uint64_t sentinel=UINT64_C(0xa55a123487654321);satchel_cpu_probe_set_x(p.get(),0,sentinel);const auto result=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(result.reason==SATCHEL_CPU_UNSUPPORTED&&!result.steps&&x(p.get(),0)==sentinel,"prefetch rejects pre/post/unprivileged/reserved/RPRFM neighbors");}
    // Fetch remains an ordinary checked executable access after a final hint.
    auto end=make(words({0xf9800000}));const auto fault=satchel_cpu_probe_run(end.get(),2,nullptr,nullptr);check(fault.reason==SATCHEL_CPU_MEMORY_FAULT&&fault.steps==1&&fault.pc==code_address+4,"hint does not bypass subsequent code fetch guard");
    auto parent=make(words({0xf9800000,0xd4000001}));auto child=shared(parent.get());satchel_cpu_probe_set_x(parent.get(),0,1);satchel_cpu_probe_set_x(child.get(),0,UINT64_MAX);expect_stop(parent.get(),SATCHEL_CPU_SVC);expect_stop(child.get(),SATCHEL_CPU_SVC);check(x(parent.get(),0)==1&&x(child.get(),0)==UINT64_MAX,"prefetch preserves separate context register banks");
}

static std::array<uint8_t,16> element_narrow_oracle(uint32_t op,const std::array<uint8_t,16>&src,const std::array<uint8_t,16>&old) {
    std::array<uint8_t,16> expected{};const bool q=op&0x40000000;
    if((op&0xbfe0fc00)==0x0e000400){unsigned imm5=(op>>16)&31,size=0;while(!(imm5&(1u<<size)))size++;const unsigned width=1u<<size,index=imm5>>(size+1);for(unsigned b=0;b<(q?16u:8u);b++)expected[b]=src[index*width+b%width];}
    else {const unsigned immediate=(op>>16)&127;unsigned width=1;while(width*16<=immediate)width*=2;const unsigned shift=width*16-immediate,offset=q?8:0;if(q)expected=old;
        // Independent bit-position oracle, not VIXL shifts/lane accessors.
        for(unsigned lane=0;lane<8/width;lane++)for(unsigned byte=0;byte<width;byte++){uint8_t value=0;for(unsigned bit=0;bit<8;bit++){unsigned source_bit=shift+byte*8+bit;value|=((src[lane*width*2+source_bit/8]>>(source_bit%8))&1)<<bit;}expected[offset+lane*width+byte]=value;}
    }return expected;
}
static std::vector<uint32_t> element_narrow_forms(){std::vector<uint32_t> ops;
    for(unsigned size=0;size<4;size++)for(unsigned q=0;q<2;q++){if(size==3&&!q)continue;for(unsigned lane=0;lane<16/(1u<<size);lane++)ops.push_back(0x0e000400|(q<<30)|(((lane<<(size+1))|(1u<<size))<<16));}
    for(unsigned size=0;size<3;size++)for(unsigned q=0;q<2;q++)for(unsigned shift=1;shift<=(8u<<size);shift++)ops.push_back(0x0f008400|(q<<30)|(((16u<<size)-shift)<<16));return ops;
}
static void element_narrow_positive(uint32_t op){std::array<uint8_t,16> src{},old{},actual{};for(unsigned i=0;i<16;i++){src[i]=uint8_t(i*19+7);old[i]=uint8_t(255-i*11);}const unsigned rd=op&31,rn=(op>>5)&31;auto p=make(words({op}));satchel_cpu_probe_set_q(p.get(),rd,old.data());satchel_cpu_probe_set_q(p.get(),rn,src.data());if(rd==rn)old=src;const auto r=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(r.reason==SATCHEL_CPU_BUDGET&&r.steps==1,"formerly excluded element/narrow exact instruction executes");check(satchel_cpu_probe_get_q(p.get(),rd,actual.data())&&actual==element_narrow_oracle(op,src,old),"formerly excluded element/narrow exact full result preserved as positive");}
static void element_narrow_tests(const char*fixtures){
    auto forms=element_narrow_forms();check(forms.size()==170,"complete original element/narrow form matrix");
    for(unsigned seed=0;seed<12;seed++){std::array<uint8_t,8192> bytes{},actual{};for(size_t i=0;i<bytes.size();i++)bytes[i]=uint8_t(i*37+seed*71);if(seed==0)std::fill(bytes.begin(),bytes.begin()+16,0);if(seed==1)std::fill(bytes.begin(),bytes.begin()+16,255);
        auto p=make(fixture(fixtures,"elementnarrow"),bytes.data(),bytes.size());fenv_t before{},after{};check(fegetenv(&before)==0,"native element/narrow host FP before");const auto native=probe_elementnarrow(bytes.data());const auto r=satchel_cpu_probe_run(p.get(),10000,nullptr,nullptr);check(r.reason==SATCHEL_CPU_SVC&&native==1&&x(p.get(),0)==1,"native170forms with Q31 destructive aliases and NZCV branches");check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==bytes,"native element/narrow entire output and surrounding canaries");check(fegetenv(&after)==0&&before.__fpcr==after.__fpcr&&before.__fpsr==after.__fpsr,"native element/narrow host FP environment unchanged");}
    for(unsigned seed=0;seed<8;seed++)for(auto registers:{std::array<unsigned,2>{0,1},{1,1},{31,31},{31,1},{1,31}}){const unsigned rd=registers[0],rn=registers[1];std::vector<uint32_t> ops;for(auto op:forms)ops.push_back(op|(rn<<5)|rd);auto p=make(prefetch_code(ops));std::array<uint64_t,31> gpr{};for(unsigned i=0;i<31;i++){gpr[i]=UINT64_C(0xfedcba9876543210)+i;satchel_cpu_probe_set_x(p.get(),i,gpr[i]);}satchel_cpu_probe_test_set_known_fpcr(p.get(),0x02000000);
        for(auto op:ops){std::array<std::array<uint8_t,16>,32> bank{};for(unsigned reg=0;reg<32;reg++){for(unsigned b=0;b<16;b++)bank[reg][b]=uint8_t(reg*19+b*37+seed*61);if(seed==0)bank[reg].fill(0);if(seed==1)bank[reg].fill(255);if(seed==2)for(unsigned b=0;b<16;b++)bank[reg][b]=(b&1)?128:0;if(seed==3)for(unsigned b=0;b<16;b++)bank[reg][b]=(b&1)?127:255;satchel_cpu_probe_set_q(p.get(),reg,bank[reg].data());}
            const auto expected=element_narrow_oracle(op,bank[rn],bank[rd]);auto result=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(result.reason==SATCHEL_CPU_BUDGET&&result.steps==1,"element/narrow exact step accounting");for(unsigned reg=0;reg<32;reg++){std::array<uint8_t,16> actual{};check(satchel_cpu_probe_get_q(p.get(),reg,actual.data())&&actual==(reg==rd?expected:bank[reg]),"bit oracle, all lanes, destructive aliases and unrelated Q bank");}for(unsigned reg=0;reg<31;reg++)check(x(p.get(),reg)==gpr[reg],"element/narrow has no GPR bank effects");check(satchel_cpu_probe_test_stack_pointer(p.get())==stack_address+128,"element/narrow preserves SP");uint32_t control=99,status=99;check(satchel_cpu_probe_get_fp_status(p.get(),&control,&status)==SATCHEL_CPU_FP_OK&&control==0x02000000&&status==0,"element/narrow preserves owned FP controls/status");
        }std::array<uint8_t,64> memory{};check(satchel_cpu_probe_read(p.get(),data_address,memory.data(),memory.size())&&std::all_of(memory.begin(),memory.end(),[](uint8_t x){return !x;}),"element/narrow never writes guest memory");}
    // Exact unallocated vector-element widths and unrelated copy/shift families.
    std::vector<uint32_t> refused={0x0e000400,0x4e000400,0x0e100400,0x4e100400,0x0e080400,0x0e180400,0x5e010420,0x4e011c20,0x6e010420,0x0f088c20,0x2f089420,0x0f089420,0x2f088420,0x5f088420};
    for(unsigned immh=8;immh<16;immh++)for(unsigned q=0;q<2;q++)refused.push_back(0x0f008400|(q<<30)|(immh<<19)|0x20);
    for(auto op:refused){std::array<uint8_t,16> old{},actual{};old.fill(0xa7);auto p=make(words({op}));satchel_cpu_probe_set_q(p.get(),op&31,old.data());auto r=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(r.reason==SATCHEL_CPU_UNSUPPORTED&&!r.steps,"element/narrow excludes scalar/round/saturating/invalid-width forms");check(satchel_cpu_probe_get_q(p.get(),op&31,actual.data())&&actual==old,"refused element/narrow preserves destination");}
    // immh zero is allocated MOVI, already supported, not a SHRN negative.
    for(uint32_t op:{0x0f008400u,0x4f008400u}){auto p=make(words({op}));std::array<uint8_t,16> actual{},old{};old.fill(255);satchel_cpu_probe_set_q(p.get(),0,old.data());auto r=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(r.reason==SATCHEL_CPU_BUDGET&&r.steps==1&&satchel_cpu_probe_get_q(p.get(),0,actual.data())&&std::all_of(actual.begin(),actual.end(),[](uint8_t x){return !x;}),"immh0 remains actual MOVI zero, not misclassified SHRN");}
    for(uint32_t op:{0x0e1f043fu,0x4e18043fu,0x0f08843fu,0x4f20843fu}){
        auto p=make(words({0xc85f7c01,op,0xc8027c03,0xd4000001}));satchel_cpu_probe_set_x(p.get(),3,9);expect_stop(p.get(),SATCHEL_CPU_SVC);check(x(p.get(),2)==0,"element/narrow preserves exclusive reservation");
        auto fp=make(words({0x1e610802,op,0xd4000001}));std::array<uint64_t,2>a={UINT64_C(0x7fefffffffffffff),0},b={UINT64_C(0x4000000000000000),0};satchel_cpu_probe_set_q(fp.get(),0,reinterpret_cast<uint8_t*>(a.data()));satchel_cpu_probe_set_q(fp.get(),1,reinterpret_cast<uint8_t*>(b.data()));expect_stop(fp.get(),SATCHEL_CPU_SVC);uint32_t control=99,status=99;check(satchel_cpu_probe_get_fp_status(fp.get(),&control,&status)==SATCHEL_CPU_FP_OK&&status==20,"element/narrow preserves actual overflow/inexact flags");
        auto parent=make(words({op}));auto child=shared(parent.get());std::array<uint8_t,16>a1{},b1{},out{};a1.fill(0x55);b1.fill(0xaa);std::array<uint8_t,16> old31{};satchel_cpu_probe_set_q(parent.get(),31,old31.data());satchel_cpu_probe_set_q(child.get(),31,old31.data());satchel_cpu_probe_set_q(parent.get(),1,a1.data());satchel_cpu_probe_set_q(child.get(),1,b1.data());satchel_cpu_probe_run(parent.get(),1,nullptr,nullptr);satchel_cpu_probe_run(child.get(),1,nullptr,nullptr);check(satchel_cpu_probe_get_q(parent.get(),1,out.data())&&out==a1&&satchel_cpu_probe_get_q(child.get(),1,out.data())&&out==b1,"element/narrow shared-context source banks stay independent");check(satchel_cpu_probe_get_q(parent.get(),31,out.data())&&out==element_narrow_oracle(op,a1,old31)&&satchel_cpu_probe_get_q(child.get(),31,out.data())&&out==element_narrow_oracle(op,b1,old31),"element/narrow shared-context full destination results remain independent");
    }
}

static void simd_widen_reduce_tests(const char *fixtures) {
    std::array<uint8_t,1024> bytes{},actual{};
    for(unsigned seed=0;seed<264;seed++) {
        for(size_t i=0;i<bytes.size();i++)bytes[i]=uint8_t(seed+i*17);
        for(unsigned b=0;seed>=256&&b<16;b++) {
            if(seed==256)bytes[b]=0;
            if(seed==257)bytes[b]=255;
            if(seed==258)bytes[b]=b%2==1?0x7f:255;
            if(seed==259)bytes[b]=b%2==1?0x80:0;
            if(seed==260)bytes[b]=b%4==3?0x7f:255;
            if(seed==261)bytes[b]=b%4==3?0x80:0;
            if(seed==262)bytes[b]=(b/4)&1?255:0;
            if(seed==263)bytes[b]=b==0?1:0;
        }
        auto p=make(fixture(fixtures,"simdwidenreduce"),bytes.data(),bytes.size());
        fenv_t before{},after{};check(fegetenv(&before)==0,"widening reduction host FP environment before");
        const uint64_t native=probe_simdwidenreduce(bytes.data());expect_stop(p.get(),SATCHEL_CPU_SVC);
        check(native==1&&x(p.get(),0)==1,"native widening reductions preserve NZCV across30 arrangements and overlaps");
        check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==bytes,"native widening sums and complete upper zeroing match for all byte seeds");
        check(fegetenv(&after)==0&&before.__fpcr==after.__fpcr&&before.__fpsr==after.__fpsr,"widening reductions preserve host FP environment");
    }
    const uint32_t forms[]={0x0e303800,0x2e303800};
    const unsigned registers[][2]={{3,1},{1,1},{31,31},{0,31},{31,0}};
    for(unsigned kind=0;kind<2;kind++)for(unsigned q=0;q<2;q++)for(unsigned size=0;size<4;size++) {
        const bool valid=size<2||(size==2&&q);
        const unsigned width=1u<<size,total=q?16:8,destination_width=width*2;
        for(unsigned seed=0;seed<(valid?48u:1u);seed++)for(const auto &r:registers) {
            const unsigned rd=r[0],rn=r[1];auto p=make(words({forms[kind]|(q<<30)|(size<<22)|(rn<<5)|rd}));
            std::array<std::array<uint8_t,16>,32> bank{},expected{};
            for(unsigned reg=0;reg<32;reg++)for(unsigned b=0;b<16;b++)bank[reg][b]=uint8_t(reg*19+b*31+seed*47);
            for(unsigned b=0;b<16;b++) {
                if(seed==0)bank[rn][b]=0;
                if(seed==1)bank[rn][b]=255;
                if(seed==2)bank[rn][b]=b%width==width-1?0x7f:255;
                if(seed==3)bank[rn][b]=b%width==width-1?0x80:0;
                if(seed==4)bank[rn][b]=(b/width)&1?(b%width==width-1?0x80:0):(b%width==width-1?0x7f:255);
                if(seed==5)bank[rn][b]=b<width?255:0;
                if(seed==6)bank[rn][b]=b>=total-width&&b<total?255:0;
                if(seed>=7&&seed<39)bank[rn][b]=(seed-7)/8==b%width?uint8_t(1u<<((seed-7)%8)):0;
            }
            // High inactive lanes must never contribute to Q0 reductions.
            if(!q)for(unsigned b=8;b<16;b++)bank[rn][b]=uint8_t(0xf0+b);
            expected=bank;
            if(valid) {
                expected[rd].fill(0);
                // Independent little-endian schoolbook addition. Extend each
                // original byte string to the result width, then add with
                // byte carry; no typed signed accumulator or VIXL lane helper.
                for(unsigned lane=0;lane<total;lane+=width) {
                    unsigned carry=0;
                    const uint8_t extension=kind==0&&(bank[rn][lane+width-1]&128)?255:0;
                    for(unsigned b=0;b<destination_width;b++) {
                        const unsigned value=b<width?bank[rn][lane+b]:extension;
                        const unsigned sum=unsigned(expected[rd][b])+value+carry;
                        expected[rd][b]=uint8_t(sum);carry=sum>>8;
                    }
                }
            }
            for(unsigned reg=0;reg<32;reg++)check(satchel_cpu_probe_set_q(p.get(),reg,bank[reg].data()),"set widening reduction whole SIMD bank");
            for(unsigned reg=0;reg<31;reg++)check(satchel_cpu_probe_set_x(p.get(),reg,UINT64_C(0xa55a012345678900)+reg),"set widening reduction GPR sentinels");
            check(satchel_cpu_probe_test_set_known_fpcr(p.get(),0x02000000),"widening reduction DN fixture");
            const auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
            check(stop.reason==(valid?SATCHEL_CPU_BUDGET:SATCHEL_CPU_UNSUPPORTED)&&stop.steps==(valid?1u:0u),"exact widening allocation matrix rejects Q0S and every D form");
            for(unsigned reg=0;reg<32;reg++){std::array<uint8_t,16> out{};check(satchel_cpu_probe_get_q(p.get(),reg,out.data())&&out==expected[reg],"bytewise widening oracle, scalar width, alias and complete SIMD bank");}
            for(unsigned reg=0;reg<31;reg++)check(x(p.get(),reg)==UINT64_C(0xa55a012345678900)+reg,"widening reductions leave all GPRs unchanged");
            check(satchel_cpu_probe_test_stack_pointer(p.get())==stack_address+128,"widening reduction leaves SP unchanged");
            uint32_t control=99,status=88;const auto fp=satchel_cpu_probe_get_fp_status(p.get(),&control,&status);
            check(valid?(fp==SATCHEL_CPU_FP_OK&&control==0x02000000&&status==0):(fp==SATCHEL_CPU_FP_INVALID&&control==99&&status==88),"valid widening FP preserved; invalid terminal query preserves outputs");
            std::array<uint8_t,64> memory{};check(satchel_cpu_probe_read(p.get(),data_address,memory.data(),memory.size())&&std::all_of(memory.begin(),memory.end(),[](uint8_t b){return b==0;}),"widening reduction has no guest memory effect");
        }
    }
    for(uint32_t form:forms) {
        auto p=make(words({0xc85f7c01,form|(1u<<30)|(4u<<5)|5,0xc8027c03,0xd4000001}));satchel_cpu_probe_set_x(p.get(),3,9);expect_stop(p.get(),SATCHEL_CPU_SVC);check(x(p.get(),2)==0,"widening integer reduction preserves local reservation");
    }
    for(uint32_t form:forms) {
        auto p=make(words({0x1e610802,form|(1u<<30)|(4u<<5)|5,0xd4000001}));std::array<uint64_t,2>a={UINT64_C(0x7fefffffffffffff),0},b={UINT64_C(0x4000000000000000),0};satchel_cpu_probe_set_q(p.get(),0,reinterpret_cast<uint8_t*>(a.data()));satchel_cpu_probe_set_q(p.get(),1,reinterpret_cast<uint8_t*>(b.data()));expect_stop(p.get(),SATCHEL_CPU_SVC);uint32_t control=99,status=99;check(satchel_cpu_probe_get_fp_status(p.get(),&control,&status)==SATCHEL_CPU_FP_OK&&status==20,"widening integer reduction preserves actual overflow/inexact cumulative status");
    }
    // These bit-flipped original neighbors are valid CMGE/CMHS already in
    // the profile, not reserved widening forms. Retain exact positive checks.
    for(uint32_t op:{0x0e303c23u,0x2e303c23u}) {
        auto p=make(words({op}));std::array<uint8_t,16> a{},b{},out{};a.fill(255);b.fill(1);satchel_cpu_probe_set_q(p.get(),1,a.data());satchel_cpu_probe_set_q(p.get(),16,b.data());
        const auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(stop.reason==SATCHEL_CPU_BUDGET&&stop.steps==1,"bit-flipped widening neighbor is existing allocated CMGE/CMHS");
        check(satchel_cpu_probe_get_q(p.get(),3,out.data()),"read existing comparison neighbor");
        for(unsigned i=0;i<16;i++)check(out[i]==(op==0x2e303c23u&&i<8?255:0),"existing signed/unsigned comparison neighbor and upper-zero semantics");
    }
    for(uint32_t op:{0x5e303823u,0x7e303823u,0x8e303823u,0xae303823u,0x6e30f823u,0x6eb0f823u,0x5ef1b823u}) {
        auto p=make(words({op}));std::array<uint8_t,16> sentinel{},out{};sentinel.fill(0xa6);satchel_cpu_probe_set_q(p.get(),3,sentinel.data());const auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
        check(stop.reason==SATCHEL_CPU_UNSUPPORTED&&!stop.steps,"widening neighbors scalar reserved and FP remain excluded");check(satchel_cpu_probe_get_q(p.get(),3,out.data())&&out==sentinel,"excluded widening neighbor preserves destination");
    }
    auto parent=make(words({0x6e303820,0xd4000001}));auto child=shared(parent.get());std::array<uint8_t,16>a{},b{},out{};a.fill(255);b.fill(1);satchel_cpu_probe_set_q(parent.get(),1,a.data());satchel_cpu_probe_set_q(child.get(),1,b.data());expect_stop(parent.get(),SATCHEL_CPU_SVC);expect_stop(child.get(),SATCHEL_CPU_SVC);
    check(satchel_cpu_probe_get_q(parent.get(),0,out.data())&&out[0]==0xf0&&out[1]==15&&std::all_of(out.begin()+2,out.end(),[](uint8_t x){return x==0;}),"parent widened sum has private register state");
    check(satchel_cpu_probe_get_q(child.get(),0,out.data())&&out[0]==16&&std::all_of(out.begin()+1,out.end(),[](uint8_t x){return x==0;}),"sibling widened sum has private register state");
}

static void simd_count_tests(const char *fixtures) {
    std::array<uint8_t,1024> bytes{},actual{};
    for(unsigned seed=0;seed<256;seed++) {
        for(size_t i=0;i<bytes.size();i++)bytes[i]=uint8_t(seed+i*17);
        auto p=make(fixture(fixtures,"simdcount"),bytes.data(),bytes.size());
        fenv_t before{},after{};check(fegetenv(&before)==0,"SIMD count native host environment before");
        const uint64_t native=probe_simdcount(bytes.data());expect_stop(p.get(),SATCHEL_CPU_SVC);
        check(native==1&&x(p.get(),0)==1,"native CNT/CLS/CLZ preserve NZCV across42 arrangements and overlaps");
        check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==bytes,"native count results and complete upper lanes agree for every byte value");
        check(fegetenv(&after)==0&&before.__fpcr==after.__fpcr&&before.__fpsr==after.__fpsr,"integer vector counts preserve host floating environment");
    }
    const uint32_t forms[]={0x0e205800,0x0e204800,0x2e204800};
    const unsigned registers[][2]={{3,1},{1,1},{31,31},{0,31},{31,0}};
    for(unsigned kind=0;kind<3;kind++)for(unsigned q=0;q<2;q++)for(unsigned size=0;size<4;size++) {
        const bool valid=kind==0?size==0:size<3;
        const unsigned seeds=valid?(kind==0?256:70):1,width=1u<<size,bits=width*8,total=q?16:8;
        for(unsigned seed=0;seed<seeds;seed++)for(const auto &r:registers) {
            const unsigned rd=r[0],rn=r[1];auto p=make(words({forms[kind]|(q<<30)|(size<<22)|(rn<<5)|rd}));
            std::array<std::array<uint8_t,16>,32> bank{},expected{};
            for(unsigned reg=0;reg<32;reg++)for(unsigned b=0;b<16;b++)bank[reg][b]=uint8_t(reg*19+b*31+seed);
            if(kind==0)for(unsigned b=0;b<16;b++)bank[rn][b]=uint8_t(seed+b*17);
            else for(unsigned lane=0;lane<16;lane+=width)for(unsigned b=0;b<width;b++) {
                uint8_t value=seed==0?0:seed==1?255:seed==2?0x55:seed==3?0xaa:uint8_t(lane*17+b*37+seed);
                if(seed>=4&&seed<68){const unsigned selected=(seed-4)%32;value=uint8_t((selected/8==b)?1u<<(selected%8):0);if(seed>=36)value=uint8_t(~value);}
                bank[rn][lane+b]=value;
            }
            expected=bank;
            if(valid){expected[rd].fill(0);for(unsigned lane=0;lane<total;lane+=width){unsigned count=0;
                if(kind==0){for(unsigned bit=0;bit<8;bit++)count+=(bank[rn][lane]>>bit)&1;}
                else {const unsigned sign=(bank[rn][lane+width-1]>>7)&1;for(int bit=int(bits)-1-(kind==1);bit>=0;bit--){const unsigned value=(bank[rn][lane+unsigned(bit)/8]>>(unsigned(bit)%8))&1;if(value!=(kind==1?sign:0u))break;count++;}}
                expected[rd][lane]=uint8_t(count);
            }}
            for(unsigned reg=0;reg<32;reg++)check(satchel_cpu_probe_set_q(p.get(),reg,bank[reg].data()),"set full SIMD count bank");
            for(unsigned reg=0;reg<31;reg++)check(satchel_cpu_probe_set_x(p.get(),reg,UINT64_C(0xa55a012345678900)+reg),"set GPR count sentinels");
            check(satchel_cpu_probe_test_set_known_fpcr(p.get(),0x02000000),"count DN control fixture");
            const auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(stop.reason==(valid?SATCHEL_CPU_BUDGET:SATCHEL_CPU_UNSUPPORTED)&&stop.steps==(valid?1u:0u),"exact count allocation matrix handles valid B/H/S and rejects reserved sizes");
            for(unsigned reg=0;reg<32;reg++){std::array<uint8_t,16> out{};check(satchel_cpu_probe_get_q(p.get(),reg,out.data())&&out==expected[reg],"independent per-bit count oracle and whole-vector bank agree");}
            for(unsigned reg=0;reg<31;reg++)check(x(p.get(),reg)==UINT64_C(0xa55a012345678900)+reg,"count leaves every GPR unchanged");
            check(satchel_cpu_probe_test_stack_pointer(p.get())==stack_address+128,"count leaves SP unchanged");
            uint32_t control=99,status=88;const auto fp_result=satchel_cpu_probe_get_fp_status(p.get(),&control,&status);
            check(valid?(fp_result==SATCHEL_CPU_FP_OK&&control==0x02000000&&status==0):(fp_result==SATCHEL_CPU_FP_INVALID&&control==99&&status==88),"valid counts preserve FP state; terminal invalid forms reject FP query without changing outputs");
        }
    }
    for(uint32_t form:forms){auto p=make(words({0xc85f7c01,form|(1u<<30)|(4u<<5)|5,0xc8027c03,0xd4000001}));satchel_cpu_probe_set_x(p.get(),3,9);expect_stop(p.get(),SATCHEL_CPU_SVC);check(x(p.get(),2)==0,"vector bit counts preserve local reservation");}
    for(uint32_t form:forms){auto p=make(words({0x1e610802,form|(1u<<30)|(4u<<5)|5,0xd4000001}));std::array<uint64_t,2>a={UINT64_C(0x7fefffffffffffff),0},b={UINT64_C(0x4000000000000000),0};satchel_cpu_probe_set_q(p.get(),0,reinterpret_cast<uint8_t*>(a.data()));satchel_cpu_probe_set_q(p.get(),1,reinterpret_cast<uint8_t*>(b.data()));expect_stop(p.get(),SATCHEL_CPU_SVC);uint32_t control=99,status=99;check(satchel_cpu_probe_get_fp_status(p.get(),&control,&status)==SATCHEL_CPU_FP_OK&&status==20,"integer vector counts retain actual cumulative overflow/inexact status");}
    for(uint32_t op:{0x2e205820u,0x2e605820u,0x0e200820u,0x2e200820u,0x0e201820u,0x5e205820u,0x5e204820u,0x0e207820u,0x2e207820u,0x0e202820u,0x2e202820u}) {
        auto p=make(words({op}));std::array<uint8_t,16> sentinel{},out{};sentinel.fill(0x96);satchel_cpu_probe_set_q(p.get(),0,sentinel.data());
        const auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(stop.reason==SATCHEL_CPU_UNSUPPORTED&&stop.steps==0,"neighboring reverse/not/scalar/saturating/widening operations remain excluded");check(satchel_cpu_probe_get_q(p.get(),0,out.data())&&out==sentinel,"excluded miscellaneous form cannot change destination");
    }
    auto parent=make(words({0x4e205820,0xd4000001}));auto child=shared(parent.get());std::array<uint8_t,16>a{},b{},out{};a.fill(255);b.fill(1);satchel_cpu_probe_set_q(parent.get(),1,a.data());satchel_cpu_probe_set_q(child.get(),1,b.data());expect_stop(parent.get(),SATCHEL_CPU_SVC);expect_stop(child.get(),SATCHEL_CPU_SVC);check(satchel_cpu_probe_get_q(parent.get(),0,out.data())&&std::all_of(out.begin(),out.end(),[](uint8_t x){return x==8;}),"parent CNT has own source/destination bank");check(satchel_cpu_probe_get_q(child.get(),0,out.data())&&std::all_of(out.begin(),out.end(),[](uint8_t x){return x==1;}),"sibling CNT has independent register state");
}

static void fmov_gpr_tests(const char *fixtures) {
    const uint64_t patterns[]={0,UINT64_C(0x8000000000000000),UINT64_C(0x7ff0000000000000),UINT64_C(0xfff0000000000000),
        UINT64_C(0x7ff0000000000001),UINT64_C(0xfff0000000000123),UINT64_C(0x7ff8123456789abc),1,
        UINT64_C(0x000fffffffffffff),UINT64_C(0x0010000000000000),UINT64_MAX,UINT64_C(0x0123456789abcdef),
        UINT64_C(0x80000000),UINT64_C(0x7f800000),UINT64_C(0xff800000),UINT64_C(0x7f800001),UINT64_C(0x7fc12345),UINT64_C(0x007fffff)};
    for(uint64_t bits:patterns) {
        std::array<uint8_t,512> bytes{},actual{};
        for(size_t i=0;i<bytes.size();i++)bytes[i]=uint8_t(i*31+7);
        std::memcpy(bytes.data(),&bits,8);std::memcpy(bytes.data()+16,&bits,8);std::memcpy(bytes.data()+24,&bits,8);
        auto p=make(fixture(fixtures,"fmovgpr"),bytes.data(),bytes.size());
        fenv_t before{},after{};check(fegetenv(&before)==0,"FMOV native host environment before");
        const uint64_t native=probe_fmovgpr(bytes.data());expect_stop(p.get(),SATCHEL_CPU_SVC);
        check(native==1&&x(p.get(),0)==1,"native FMOV six forms and ZR preserve NZCV");
        check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==bytes,"native FMOV exact payloads including NaNs/subnormal/negative-zero agree");
        check(fegetenv(&after)==0&&before.__fpcr==after.__fpcr&&before.__fpsr==after.__fpsr,"FMOV native/interpreted bit copies preserve host floating environment");
    }
    const uint32_t forms[]={0x1e260000,0x1e270000,0x9e660000,0x9e670000,0x9eae0000,0x9eaf0000};
    const unsigned registers[][2]={{3,1},{1,1},{0,0},{31,31},{31,1},{1,31},{30,29},{8,15},{15,8}};
    for(unsigned kind=0;kind<6;kind++)for(uint64_t bits:patterns)for(const auto &r:registers) {
        const unsigned rd=r[0],rn=r[1];const bool to_vector=kind&1;
        auto p=make(words({forms[kind]|(rn<<5)|rd}));
        std::array<std::array<uint8_t,16>,32> bank{},expected{};std::array<uint64_t,31> general{},expected_g{};
        for(unsigned reg=0;reg<32;reg++){for(unsigned b=0;b<16;b++)bank[reg][b]=uint8_t(reg*19+b*31);}
        for(unsigned reg=0;reg<31;reg++)general[reg]=UINT64_C(0x456789abcdef0123)+reg;
        if(to_vector){if(rn<31)general[rn]=bits;}else{for(unsigned b=0;b<8;b++){bank[rn][b]=uint8_t(bits>>(b*8));bank[rn][b+8]=uint8_t(bits>>(b*8));}}
        expected=bank;expected_g=general;
        if(to_vector){const uint64_t source=rn==31?0:general[rn];const unsigned start=kind==5?8:0,width=kind==1?4:8;if(kind!=5)expected[rd].fill(0);for(unsigned b=0;b<width;b++)expected[rd][start+b]=uint8_t(source>>(8*b));}
        else if(rd<31){uint64_t value=0;const unsigned start=kind==4?8:0,width=kind==0?4:8;for(unsigned b=0;b<width;b++)value|=uint64_t(bank[rn][start+b])<<(8*b);expected_g[rd]=value;}
        for(unsigned reg=0;reg<32;reg++)check(satchel_cpu_probe_set_q(p.get(),reg,bank[reg].data()),"FMOV initialize independent SIMD bank");
        for(unsigned reg=0;reg<31;reg++)check(satchel_cpu_probe_set_x(p.get(),reg,general[reg]),"FMOV initialize independent GPR bank");
        check(satchel_cpu_probe_test_set_known_fpcr(p.get(),0x02000000),"FMOV owned DN control");
        fenv_t before{},after{};check(fegetenv(&before)==0,"FMOV scalar host environment before");
        const auto result=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(result.reason==SATCHEL_CPU_BUDGET&&result.steps==1,"FMOV exact form completes one instruction");
        for(unsigned reg=0;reg<32;reg++){std::array<uint8_t,16> out{};check(satchel_cpu_probe_get_q(p.get(),reg,out.data())&&out==expected[reg],"FMOV full SIMD bank and upper-lane oracle");}
        for(unsigned reg=0;reg<31;reg++)check(x(p.get(),reg)==expected_g[reg],"FMOV GPR bank including W upper clearing and discard destination");
        check(satchel_cpu_probe_test_stack_pointer(p.get())==stack_address+128,"FMOV register31 never means SP");
        uint32_t control=99,status=99;check(satchel_cpu_probe_get_fp_status(p.get(),&control,&status)==SATCHEL_CPU_FP_OK&&control==0x02000000&&status==0,"FMOV preserves known DN control and FPSR including signaling NaN transport");
        check(fegetenv(&after)==0&&before.__fpcr==after.__fpcr&&before.__fpsr==after.__fpsr,"FMOV leaves host FP environment untouched");
    }
    // Fixed arithmetic produces real preexisting overflow/inexact; transfers
    // preserve these cumulative flags, including when their payload is sNaN.
    for(uint32_t form:forms){auto p=make(words({0x1e610802,form|(4u<<5)|5,0xd4000001}));std::array<uint64_t,2> a={UINT64_C(0x7fefffffffffffff),0},b={UINT64_C(0x4000000000000000),0},snan={UINT64_C(0x7ff0000000000001),UINT64_C(0x7ff0000000000001)};
        satchel_cpu_probe_set_q(p.get(),0,reinterpret_cast<const uint8_t*>(a.data()));satchel_cpu_probe_set_q(p.get(),1,reinterpret_cast<const uint8_t*>(b.data()));satchel_cpu_probe_set_q(p.get(),4,reinterpret_cast<const uint8_t*>(snan.data()));satchel_cpu_probe_set_x(p.get(),4,snan[0]);expect_stop(p.get(),SATCHEL_CPU_SVC);uint32_t control=99,status=99;check(satchel_cpu_probe_get_fp_status(p.get(),&control,&status)==SATCHEL_CPU_FP_OK&&status==20,"FMOV preserves genuine preexisting FPSR overflow/inexact");}
    for(uint32_t form:forms){auto p=make(words({0xc85f7c01,form|(4u<<5)|5,0xc8027c03,0xd4000001}));satchel_cpu_probe_set_x(p.get(),3,9);expect_stop(p.get(),SATCHEL_CPU_SVC);check(x(p.get(),2)==0,"FMOV preserves local exclusive reservation");}
    // Nearby unallocated size combinations and other FP operations stay stopped.
    for(uint32_t op:{0x1ee60020u,0x1ee70020u,0x1e660020u,0x1e670020u,0x9e260020u,0x9e270020u,0x1e2e0020u,0x1e2f0020u,0x1e204020u,0x1e604020u,0x1e201000u,0x1e601000u,0x1e220020u,0x9e620020u}) {
        auto p=make(words({op}));std::array<uint8_t,16> sentinel{},out{};sentinel.fill(0xa5);satchel_cpu_probe_set_q(p.get(),0,sentinel.data());satchel_cpu_probe_set_x(p.get(),0,77);
        const auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(stop.reason==SATCHEL_CPU_UNSUPPORTED&&stop.steps==0,"FMOV excluded FP16/conversion/scalar/immediate form stops before mutation");check(x(p.get(),0)==77&&satchel_cpu_probe_get_q(p.get(),0,out.data())&&out==sentinel,"excluded FMOV leaves both destination banks unchanged");
    }
    auto parent=make(words({0x9e670020,0xd4000001}));auto child=shared(parent.get());satchel_cpu_probe_set_x(parent.get(),1,17);satchel_cpu_probe_set_x(child.get(),1,29);expect_stop(parent.get(),SATCHEL_CPU_SVC);expect_stop(child.get(),SATCHEL_CPU_SVC);std::array<uint64_t,2> q{};check(satchel_cpu_probe_get_q(parent.get(),0,reinterpret_cast<uint8_t*>(q.data()))&&q[0]==17&&q[1]==0,"parent FMOV owned vector value");check(satchel_cpu_probe_get_q(child.get(),0,reinterpret_cast<uint8_t*>(q.data()))&&q[0]==29&&q[1]==0,"sibling FMOV owns independent register banks");
}

static void simd_logical_tests(const char *fixtures) {
    std::array<uint8_t,4096> bytes{},actual{};
    for(unsigned seed=0;seed<20;seed++) {
        for(size_t i=0;i<bytes.size();i++)bytes[i]=uint8_t(i*37+seed*71);
        if(seed<8)for(unsigned i=0;i<16;i++){bytes[i]=(seed&1)?255:0;bytes[16+i]=(seed&2)?255:0;bytes[32+i]=(seed&4)?255:0;bytes[48+i]=uint8_t(i*17);}
        auto p=make(fixture(fixtures,"simdlogical"),bytes.data(),bytes.size());
        fenv_t before{},after{};check(fegetenv(&before)==0,"SIMD logical native host FP state before");
        const uint64_t native=probe_simdlogical(bytes.data());expect_stop(p.get(),SATCHEL_CPU_SVC);
        check(native==1&&x(p.get(),0)==native,"native SIMD logical operations preserve NZCV");
        check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==bytes,"native SIMD bitwise results and old-destination overlaps agree");
        check(satchel_cpu_probe_test_stack_pointer(p.get())==stack_address+128,"SIMD logical operations preserve SP");
        uint32_t control=99,status=99;check(satchel_cpu_probe_get_fp_status(p.get(),&control,&status)==SATCHEL_CPU_FP_OK&&control==0&&status==0,"SIMD logical operations preserve owned FP state");
        check(fegetenv(&after)==0&&before.__fpcr==after.__fpcr&&before.__fpsr==after.__fpsr,"native/interpreted SIMD logical operations preserve host floating environment");
    }
    const uint32_t forms[]={0x0e201c00,0x0e601c00,0x0ea01c00,0x0ee01c00,0x2e201c00,0x2e601c00,0x2ea01c00,0x2ee01c00};
    const unsigned regs[][3]={{3,1,2},{1,1,2},{2,1,2},{3,1,1},{31,31,31},{31,1,2},{3,31,2},{3,1,31},{0,0,0}};
    // A per-bit boolean truth table independently checks the XOR/mask helper
    // implementation. Inputs include all eight A/B/old-D bit combinations,
    // alternating masks and partial-byte patterns; aliases use final bank state.
    for(unsigned kind=0;kind<8;kind++)for(unsigned q=0;q<2;q++)for(unsigned seed=0;seed<16;seed++)for(const auto &r:regs) {
        const unsigned rd=r[0],rn=r[1],rm=r[2];const uint32_t op=forms[kind]|(q<<30)|(rm<<16)|(rn<<5)|rd;
        auto p=make(words({op}));std::array<std::array<uint8_t,16>,32> bank{};
        for(unsigned reg=0;reg<32;reg++)for(unsigned b=0;b<16;b++)bank[reg][b]=uint8_t(reg*19+b*37+seed*61);
        if(seed<8){bank[rd].fill((seed&4)?255:0);bank[rn].fill((seed&1)?255:0);bank[rm].fill((seed&2)?255:0);}
        for(unsigned reg=0;reg<32;reg++)check(satchel_cpu_probe_set_q(p.get(),reg,bank[reg].data()),"set independent SIMD logical register bank");
        check(satchel_cpu_probe_set_x(p.get(),1,UINT64_C(0xa55a123487654321)),"set logical GPR sentinel");
        check(satchel_cpu_probe_test_set_known_fpcr(p.get(),0x02000000),"SIMD logical DN control fixture");
        std::array<uint8_t,16> expected{},observed{};
        for(unsigned i=0;i<(q?16u:8u);i++)for(unsigned bit=0;bit<8;bit++) {
            const bool a=(bank[rn][i]>>bit)&1,b=(bank[rm][i]>>bit)&1,d=(bank[rd][i]>>bit)&1;
            const bool value=kind==0?(a&&b):kind==1?(a&&!b):kind==2?(a||b):kind==3?(a||!b):kind==4?(a!=b):kind==5?(d?a:b):kind==6?(b?a:d):(b?d:a);
            if(value)expected[i]|=uint8_t(1u<<bit);
        }
        const auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
        check(stop.reason==SATCHEL_CPU_BUDGET&&stop.steps==1,"exact vector logical operation executes once");
        for(unsigned reg=0;reg<32;reg++)check(satchel_cpu_probe_get_q(p.get(),reg,observed.data())&&observed==(reg==rd?expected:bank[reg]),"SIMD logical truth table, destructive overlaps, Q31 and upper clearing");
        check(x(p.get(),1)==UINT64_C(0xa55a123487654321)&&satchel_cpu_probe_test_stack_pointer(p.get())==stack_address+128,"vector logical operations leave distinct GPR/SP untouched");
        uint32_t control=99,status=99;check(satchel_cpu_probe_get_fp_status(p.get(),&control,&status)==SATCHEL_CPU_FP_OK&&control==0x02000000&&status==0,"logical operations preserve owned FPCR/FPSR");
        std::array<uint8_t,64> memory{};check(satchel_cpu_probe_read(p.get(),data_address,memory.data(),memory.size())&&std::all_of(memory.begin(),memory.end(),[](uint8_t b){return b==0;}),"vector logical operations have no guest memory effects");
    }
    std::array<uint8_t,16> sentinel{},observed{};sentinel.fill(0xa7);
    // This exact legacy unary exclusion is now intentionally admitted by the
    // count family. Preserve its instruction and test its full byte result.
    {
        auto counted=make(words({0x4e205823}));std::array<uint8_t,16> input{},output{};input.fill(0x81);
        check(satchel_cpu_probe_set_q(counted.get(),1,input.data())&&satchel_cpu_probe_set_q(counted.get(),3,sentinel.data()),"initialize formerly excluded CNT operands");
        const auto stop=satchel_cpu_probe_run(counted.get(),1,nullptr,nullptr);
        check(stop.reason==SATCHEL_CPU_BUDGET&&stop.steps==1,"formerly excluded exact CNT now executes one reviewed instruction");
        check(satchel_cpu_probe_get_q(counted.get(),3,output.data())&&std::all_of(output.begin(),output.end(),[](uint8_t b){return b==2;}),"formerly excluded CNT computes every byte population count");
        check(satchel_cpu_probe_get_q(counted.get(),1,output.data())&&output==input,"formerly excluded CNT preserves its source");
    }
    std::vector<uint32_t> excluded={0x0f001403,0x2f001403,0x4e221823,0x4e220c23,0x4e221423,0x4e22d423};
    for(uint32_t form:forms){excluded.push_back(form|0x50020023);excluded.push_back(form|0xc0020023);}
    for(uint32_t op:excluded) {
        auto p=make(words({op}));check(satchel_cpu_probe_set_q(p.get(),3,sentinel.data()),"set refused logical destination");
        const auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
        check(stop.reason==SATCHEL_CPU_UNSUPPORTED&&!stop.steps,"logical profile excludes unary/immediate/scalar/FP/reserved neighbors");
        check(satchel_cpu_probe_get_q(p.get(),3,observed.data())&&observed==sentinel,"refused logical operation leaves destination unchanged");
    }
    for(uint32_t form:forms)for(unsigned q:{0u,1u}) {
        const uint32_t op=form|(q<<30)|0x0002003f;
        auto p=make(words({0xc85f7c01,op,0xc8027c03,0xd4000001}));satchel_cpu_probe_set_x(p.get(),3,77);expect_stop(p.get(),SATCHEL_CPU_SVC);
        check(x(p.get(),2)==0,"vector logical operations preserve live exclusive reservation");
        std::array<uint64_t,2> a={UINT64_C(0x7fefffffffffffff),0},b={UINT64_C(0x4000000000000000),0};
        auto fp=make(words({0x1e610802,op,0xd4000001}));satchel_cpu_probe_set_q(fp.get(),0,reinterpret_cast<uint8_t*>(a.data()));satchel_cpu_probe_set_q(fp.get(),1,reinterpret_cast<uint8_t*>(b.data()));expect_stop(fp.get(),SATCHEL_CPU_SVC);
        uint32_t control=99,status=99;check(satchel_cpu_probe_get_fp_status(fp.get(),&control,&status)==SATCHEL_CPU_FP_OK&&status==20,"logical operations preserve actual cumulative FPSR20");
    }
    auto parent=make(words({0x6ee21c20,0x6ea21c20}));auto child=shared(parent.get());
    std::array<uint8_t,16> a{},b{},d{};a.fill(0xa5);b.fill(0x0f);d.fill(0x5a);
    for(SatchelCPUProbe *p:{parent.get(),child.get()}){satchel_cpu_probe_set_q(p,0,d.data());satchel_cpu_probe_set_q(p,1,a.data());satchel_cpu_probe_set_q(p,2,b.data());}
    check(satchel_cpu_probe_set_pc(child.get(),code_address+4),"SIMD logical sibling independent PC");
    check(satchel_cpu_probe_run(parent.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET&&satchel_cpu_probe_run(child.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET,"SIMD logical contexts execute separately");
    check(satchel_cpu_probe_get_q(parent.get(),0,observed.data())&&std::all_of(observed.begin(),observed.end(),[](uint8_t b){return b==0xaa;}),"parent BIF preserves its own selected destination bits");
    check(satchel_cpu_probe_get_q(child.get(),0,observed.data())&&std::all_of(observed.begin(),observed.end(),[](uint8_t b){return b==0x55;}),"child BIT has independent selected destination bits");
}

static void simd_reduce_tests(const char *fixtures) {
    std::array<uint8_t,2048> bytes{},actual{};
    for(unsigned seed=0;seed<16;seed++) {
        for(size_t i=0;i<bytes.size();i++)bytes[i]=uint8_t(i*37+seed*71);
        for(unsigned i=0;i<16;i++) {
            if(seed==0)bytes[i]=0;
            if(seed==1)bytes[i]=255;
            if(seed==2)bytes[i]=i%4==3?0x7f:255;
            if(seed==3)bytes[i]=i%4==3?0x80:0;
            if(seed==4)bytes[i]=i%2?0x80:0x7f;
            if(seed==5)bytes[i]=i==15?255:0;
        }
        auto p=make(fixture(fixtures,"simdreduce"),bytes.data(),bytes.size());
        fenv_t before{},after{};check(fegetenv(&before)==0,"reduction host FP state before");
        const uint64_t native=probe_simdreduce(bytes.data());expect_stop(p.get(),SATCHEL_CPU_SVC);
        check(native==1&&x(p.get(),0)==native,"native reductions all arrangements preserve NZCV");
        check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==bytes,"native reductions agree including overlap and zero upper lanes");
        check(satchel_cpu_probe_test_stack_pointer(p.get())==stack_address+128,"reductions preserve SP");
        uint32_t control=99,status=99;check(satchel_cpu_probe_get_fp_status(p.get(),&control,&status)==SATCHEL_CPU_FP_OK&&control==0&&status==0,"reductions leave known FP state unchanged");
        check(fegetenv(&after)==0&&before.__fpcr==after.__fpcr&&before.__fpsr==after.__fpsr,"native and simulated reductions preserve host FP state");
    }
    const uint32_t forms[]={0x0e31b800,0x0e30a800,0x0e31a800,0x2e30a800,0x2e31a800};
    const unsigned registers[][2]={{3,1},{1,1},{31,31},{0,31}};
    for(unsigned kind=0;kind<5;kind++)for(unsigned q=0;q<2;q++)for(unsigned size=0;size<4;size++)
    for(unsigned seed=0;seed<12;seed++)for(const auto &r:registers) {
        const unsigned rd=r[0],rn=r[1],width=1u<<size,total=q?16:8;
        const bool valid=size<2||(size==2&&q);
        auto p=make(words({forms[kind]|(q<<30)|(size<<22)|(rn<<5)|rd}));
        std::array<std::array<uint8_t,16>,32> bank{};
        for(unsigned reg=0;reg<32;reg++)for(unsigned b=0;b<16;b++)bank[reg][b]=uint8_t(reg*19+b*37+seed*61);
        for(unsigned b=0;b<16;b++) {
            if(seed==0)bank[rn][b]=0;
            if(seed==1)bank[rn][b]=255;
            if(seed==2)bank[rn][b]=b%width==width-1?0x7f:255;
            if(seed==3)bank[rn][b]=b%width==width-1?0x80:0;
            if(seed==4)bank[rn][b]=b==total-1?0x80:0;
            if(seed==5)bank[rn][b]=b<width?255:0;
            if(seed==6)bank[rn][b]=b>=total-width?255:0;
        }
        for(unsigned reg=0;reg<32;reg++)check(satchel_cpu_probe_set_q(p.get(),reg,bank[reg].data()),"set reduction register bank");
        check(satchel_cpu_probe_set_x(p.get(),1,UINT64_C(0xa55a123487654321)),"reduction GPR sentinel");
        check(satchel_cpu_probe_test_set_known_fpcr(p.get(),0x02000000),"reduction DN fixture");
        std::array<uint8_t,16> expected=bank[rd],observed{};
        if(valid) {
            expected.fill(0);
            if(kind==0) {
                // Bytewise addition with carry, modulo the destination lane.
                // Independent of VIXL's signed int64 accumulation and casts.
                for(unsigned lane=0;lane<total;lane+=width) {
                    unsigned carry=0;
                    for(unsigned b=0;b<width;b++) {
                        const unsigned sum=unsigned(expected[b])+bank[rn][lane+b]+carry;
                        expected[b]=uint8_t(sum);carry=sum>>8;
                    }
                }
            } else {
                // High-byte lexicographic ordering, with sign-bit bias only
                // for signed families. Retain the chosen original lane bits.
                unsigned selected=0;
                for(unsigned lane=width;lane<total;lane+=width) {
                    int order=0;
                    for(unsigned i=width;i>0;i--) {
                        unsigned a=bank[rn][lane+i-1],b=bank[rn][selected+i-1];
                        if((kind==1||kind==2)&&i==width){a^=128;b^=128;}
                        if(a!=b){order=a>b?1:-1;break;}
                    }
                    if((kind==1||kind==3)?order>0:order<0)selected=lane;
                }
                std::copy_n(bank[rn].begin()+selected,width,expected.begin());
            }
        }
        const auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
        check(stop.reason==(valid?SATCHEL_CPU_BUDGET:SATCHEL_CPU_UNSUPPORTED)&&stop.steps==(valid?1u:0u),"exact allocated reduction shape");
        for(unsigned reg=0;reg<32;reg++)check(satchel_cpu_probe_get_q(p.get(),reg,observed.data())&&observed==(reg==rd?expected:bank[reg]),"reduction scalar bits and full upper zero; other registers unchanged");
        check(x(p.get(),1)==UINT64_C(0xa55a123487654321)&&satchel_cpu_probe_test_stack_pointer(p.get())==stack_address+128,"reduction separate X/SP banks unchanged");
        uint32_t control=99,status=99;const auto fp=satchel_cpu_probe_get_fp_status(p.get(),&control,&status);
        check(valid?(fp==SATCHEL_CPU_FP_OK&&control==0x02000000&&status==0):(fp==SATCHEL_CPU_FP_INVALID&&control==99&&status==99),"reduction FP state preserved; terminal query atomic");
        std::array<uint8_t,64> memory{};check(satchel_cpu_probe_read(p.get(),data_address,memory.data(),memory.size())&&std::all_of(memory.begin(),memory.end(),[](uint8_t b){return b==0;}),"reduction has no guest memory effects");
    }
    for(uint32_t op:{0x0e303823u,0x2e303823u}) {
        auto p=make(words({op}));std::array<uint8_t,16> input{},output{};input.fill(255);satchel_cpu_probe_set_q(p.get(),1,input.data());const auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
        check(stop.reason==SATCHEL_CPU_BUDGET&&stop.steps==1,"formerly excluded widening forms now execute exactly once");
        check(satchel_cpu_probe_get_q(p.get(),3,output.data())&&output[0]==0xf8&&output[1]==(op==0x0e303823u?255:7)&&std::all_of(output.begin()+2,output.end(),[](uint8_t b){return b==0;}),"former widening exclusions now assert actual signed/unsigned widened results");
    }
    const uint32_t excluded[]={0x6e30f823,0x6eb0f823,0x6e30c823,0x6eb0c823,0x5ef1b823,0x0e31bc23,0x8e31b823};
    for(uint32_t op:excluded) {
        auto p=make(words({op}));std::array<uint8_t,16> before{},after{};before.fill(0xa7);check(satchel_cpu_probe_set_q(p.get(),3,before.data()),"set refused reduction sentinel");
        const auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
        check(stop.reason==SATCHEL_CPU_UNSUPPORTED&&!stop.steps,"FP scalar reserved reductions remain excluded");
        check(satchel_cpu_probe_get_q(p.get(),3,after.data())&&after==before,"refused reduction unchanged");
    }
    for(uint32_t form:forms)for(unsigned q:{0u,1u}) {
        const uint32_t op=form|(q<<30)|(q?0x00800000:0)|0x3f;
        auto p=make(words({0xc85f7c01,op,0xc8027c03,0xd4000001}));satchel_cpu_probe_set_x(p.get(),3,77);expect_stop(p.get(),SATCHEL_CPU_SVC);
        check(x(p.get(),2)==0,"register reduction preserves exclusive reservation");
        std::array<uint64_t,2> a={UINT64_C(0x7fefffffffffffff),0},b={UINT64_C(0x4000000000000000),0};
        auto fp=make(words({0x1e610802,op,0xd4000001}));satchel_cpu_probe_set_q(fp.get(),0,reinterpret_cast<uint8_t*>(a.data()));satchel_cpu_probe_set_q(fp.get(),1,reinterpret_cast<uint8_t*>(b.data()));expect_stop(fp.get(),SATCHEL_CPU_SVC);
        uint32_t control=99,status=99;check(satchel_cpu_probe_get_fp_status(fp.get(),&control,&status)==SATCHEL_CPU_FP_OK&&status==20,"reduction preserves actual FPSR20");
    }
    auto parent=make(words({0x6e30a820,0x4e30a820}));auto child=shared(parent.get());
    std::array<uint8_t,16> a{},out{};a.fill(0x7f);a[5]=0xff;
    for(SatchelCPUProbe *p:{parent.get(),child.get()})check(satchel_cpu_probe_set_q(p,1,a.data()),"reduction sibling source");
    check(satchel_cpu_probe_set_pc(child.get(),code_address+4),"reduction sibling PC");
    check(satchel_cpu_probe_run(parent.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET&&satchel_cpu_probe_run(child.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET,"reductions execute on separate contexts");
    check(satchel_cpu_probe_get_q(parent.get(),0,out.data())&&out[0]==255&&std::all_of(out.begin()+1,out.end(),[](uint8_t b){return b==0;}),"unsigned reduction stays in parent");
    check(satchel_cpu_probe_get_q(child.get(),0,out.data())&&out[0]==127&&std::all_of(out.begin()+1,out.end(),[](uint8_t b){return b==0;}),"signed reduction stays in child");
}

static void simd_permute_tests(const char *fixtures) {
    std::array<uint8_t,8192> bytes{},actual{};
    for(unsigned seed=0;seed<12;seed++) {
        for(size_t i=0;i<bytes.size();i++)bytes[i]=uint8_t(i*37+seed*53);
        auto p=make(fixture(fixtures,"simdpermute"),bytes.data(),bytes.size());
        fenv_t before{},after{};check(fegetenv(&before)==0,"permutation host FP before");
        const uint64_t native=probe_simdpermute(bytes.data());
        // This original exhaustive fixture contains more than1000 instructions.
        const auto stop=satchel_cpu_probe_run(p.get(),2000,nullptr,nullptr);
        check(stop.reason==SATCHEL_CPU_SVC,"full original permutation fixture completes");
        check(native==1&&x(p.get(),0)==1,"native/interpreted permutation NZCV unchanged");
        check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==bytes,"native permutation every arrangement/index and register overlap agrees");
        check(satchel_cpu_probe_test_stack_pointer(p.get())==stack_address+128,"permutation SP unchanged");
        uint32_t control=99,status=99;check(satchel_cpu_probe_get_fp_status(p.get(),&control,&status)==SATCHEL_CPU_FP_OK&&control==0&&status==0,"permutation FP state unchanged");
        check(fegetenv(&after)==0&&before.__fpcr==after.__fpcr&&before.__fpsr==after.__fpsr,"permutation host FP environment unchanged");
    }
    const uint32_t forms[]={0x0e001800,0x0e005800,0x0e002800,0x0e006800,0x0e003800,0x0e007800};
    const unsigned regs[][3]={{3,1,2},{1,1,2},{2,1,2},{31,31,31},{0,31,2},{31,1,31}};
    auto verify=[&](uint32_t op,unsigned kind,unsigned q,unsigned size,unsigned index,bool valid,unsigned seed,const unsigned *r) {
        const unsigned rd=r[0],rn=r[1],rm=r[2],width=1u<<size,total=q?16:8,lanes=total/width;
        auto p=make(words({op}));std::array<std::array<uint8_t,16>,32> bank{};
        for(unsigned reg=0;reg<32;reg++)for(unsigned b=0;b<16;b++)bank[reg][b]=uint8_t(reg*19+b*37+seed*61);
        if(seed==0)for(unsigned b=0;b<16;b++){bank[rn][b]=uint8_t(b);if(rm!=rn)bank[rm][b]=uint8_t(b+128);}
        for(unsigned reg=0;reg<32;reg++)check(satchel_cpu_probe_set_q(p.get(),reg,bank[reg].data()),"set permutation owned bank");
        check(satchel_cpu_probe_set_x(p.get(),1,UINT64_C(0xa55a123487654321)),"permutation separate GPR sentinel");
        check(satchel_cpu_probe_test_set_known_fpcr(p.get(),0x02000000),"permutation DN fixture");
        std::array<uint8_t,16> expected=bank[rd],observed{};
        if(valid) {
            expected.fill(0);
            // Position mapping over a logical concatenation; no typed SIMD
            // values or pinned temporary-buffer helper is used by this oracle.
            for(unsigned b=0;b<total;b++) {
                unsigned pos=0;
                if(kind==6)pos=b+index;
                else {
                    const unsigned lane=b/width,byte=b%width,second=kind&1;
                    if(kind<2)pos=(2*lane+second)*width+byte;
                    else if(kind<4)pos=((lane&1)?total:0)+((lane/2)*2+second)*width+byte;
                    else pos=((lane&1)?total:0)+(lane/2+second*(lanes/2))*width+byte;
                }
                expected[b]=pos<total?bank[rn][pos]:bank[rm][pos-total];
            }
        }
        const auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
        check(stop.reason==(valid?SATCHEL_CPU_BUDGET:SATCHEL_CPU_UNSUPPORTED)&&stop.steps==(valid?1u:0u),"permutation exact allocated shapes/index bounds");
        for(unsigned reg=0;reg<32;reg++)check(satchel_cpu_probe_get_q(p.get(),reg,observed.data())&&observed==(reg==rd?expected:bank[reg]),"permutation exact bytes, aliases, upper clearing and other registers");
        check(x(p.get(),1)==UINT64_C(0xa55a123487654321)&&satchel_cpu_probe_test_stack_pointer(p.get())==stack_address+128,"permutation separate X/SP unchanged");
        uint32_t control=99,status=99;const auto fp=satchel_cpu_probe_get_fp_status(p.get(),&control,&status);
        check(valid?(fp==SATCHEL_CPU_FP_OK&&control==0x02000000&&status==0):(fp==SATCHEL_CPU_FP_INVALID&&control==99&&status==99),"permutation FP preserved and terminal query atomic");
        std::array<uint8_t,64> memory{};check(satchel_cpu_probe_read(p.get(),data_address,memory.data(),memory.size())&&std::all_of(memory.begin(),memory.end(),[](uint8_t b){return b==0;}),"permutation no guest memory effects");
    };
    for(unsigned kind=0;kind<6;kind++)for(unsigned q=0;q<2;q++)for(unsigned size=0;size<4;size++)for(unsigned seed=0;seed<4;seed++)for(const auto &r:regs)
        verify(forms[kind]|(q<<30)|(size<<22)|(r[2]<<16)|(r[1]<<5)|r[0],kind,q,size,0,q||size!=3,seed,r);
    for(unsigned q=0;q<2;q++)for(unsigned index=0;index<16;index++)for(unsigned seed=0;seed<4;seed++)for(const auto &r:regs)
        verify(0x2e000000|(q<<30)|(r[2]<<16)|(index<<11)|(r[1]<<5)|r[0],6,q,0,index,q||index<8,seed,r);
    // The U=1 reinterpretation of this original candidate is allocated EXT8B,#7.
    // Keep its exact word as an explicit positive byte-effect check.
    verify(0x2e023823,6,0,0,7,true,0,regs[0]);
    const uint32_t excluded[]={0x0e020023,0x0e024023,0x8e023823,0x2e428023,0x2e228023,0x2e028023,0x2e020423};
    for(uint32_t op:excluded) {
        auto p=make(words({op}));std::array<uint8_t,16> before{},after{};before.fill(0xa7);check(satchel_cpu_probe_set_q(p.get(),3,before.data()),"refused permutation sentinel");
        const auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(stop.reason==SATCHEL_CPU_UNSUPPORTED&&!stop.steps,"table/reserved/non-permutation neighbors refused");
        check(satchel_cpu_probe_get_q(p.get(),3,after.data())&&after==before,"refused permutation leaves destination");
    }
    for(uint32_t form:{0x0e001800u,0x0e005800u,0x0e002800u,0x0e006800u,0x0e003800u,0x0e007800u,0x2e000000u})for(unsigned q:{0u,1u}) {
        const uint32_t op=form|(q<<30)|0x0002003f;
        auto p=make(words({0xc85f7c01,op,0xc8027c03,0xd4000001}));satchel_cpu_probe_set_x(p.get(),3,77);expect_stop(p.get(),SATCHEL_CPU_SVC);check(x(p.get(),2)==0,"permutation preserves exclusive reservation");
        std::array<uint64_t,2>a={UINT64_C(0x7fefffffffffffff),0},b={UINT64_C(0x4000000000000000),0};
        auto fp=make(words({0x1e610802,op,0xd4000001}));satchel_cpu_probe_set_q(fp.get(),0,reinterpret_cast<uint8_t*>(a.data()));satchel_cpu_probe_set_q(fp.get(),1,reinterpret_cast<uint8_t*>(b.data()));expect_stop(fp.get(),SATCHEL_CPU_SVC);
        uint32_t control=99,status=99;check(satchel_cpu_probe_get_fp_status(fp.get(),&control,&status)==SATCHEL_CPU_FP_OK&&status==20,"permutation preserves actual FPSR20");
    }
    auto parent=make(words({0x4e023820,0x4e027820}));auto child=shared(parent.get());
    std::array<uint8_t,16>a{},b{},out{};for(unsigned i=0;i<16;i++){a[i]=uint8_t(i);b[i]=uint8_t(i+128);}
    for(SatchelCPUProbe *p:{parent.get(),child.get()}){check(satchel_cpu_probe_set_q(p,1,a.data()),"permutation sibling A");check(satchel_cpu_probe_set_q(p,2,b.data()),"permutation sibling B");}
    check(satchel_cpu_probe_set_pc(child.get(),code_address+4),"permutation sibling independent PC");
    check(satchel_cpu_probe_run(parent.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET&&satchel_cpu_probe_run(child.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET,"permutation siblings execute");
    check(satchel_cpu_probe_get_q(parent.get(),0,out.data())&&out[0]==0&&out[1]==128&&out[14]==7&&out[15]==135,"ZIP1 parent survives sibling");
    check(satchel_cpu_probe_get_q(child.get(),0,out.data())&&out[0]==8&&out[1]==136&&out[14]==15&&out[15]==143,"ZIP2 child owns result");
}

static void simd_compare_tests(const char *fixtures) {
    std::array<uint8_t,4096> bytes{},actual{};
    for(unsigned seed=0;seed<16;seed++) {
        for(size_t i=0;i<bytes.size();i++)bytes[i]=uint8_t(i*37+seed*71);
        for(unsigned i=0;i<32;i++) {
            if(seed==0)bytes[i]=0;
            if(seed==1)bytes[i]=255;
            if(seed==2)bytes[i]=i<16?255:1;
            if(seed==3)bytes[i]=i<16?0:255;
            if(seed==4)bytes[i]=(i%8==7)?0x7f:255;
            if(seed==5)bytes[i]=(i%8==7)?0x80:0;
            if(seed==6)bytes[i]=i<16?0x55:0xaa;
        }
        auto p=make(fixture(fixtures,"simdcompare"),bytes.data(),bytes.size());
        fenv_t before{},after{};check(fegetenv(&before)==0,"SIMD comparison native host FP state before");
        const uint64_t native=probe_simdcompare(bytes.data());expect_stop(p.get(),SATCHEL_CPU_SVC);
        check(native==1&&x(p.get(),0)==native,"native SIMD comparison all arrangements preserve NZCV");
        check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==bytes,"native SIMD signed/unsigned comparison, bit-test and overlapping registers agree");
        check(satchel_cpu_probe_test_stack_pointer(p.get())==stack_address+128,"SIMD comparison preserves SP");
        uint32_t control=99,status=99;check(satchel_cpu_probe_get_fp_status(p.get(),&control,&status)==SATCHEL_CPU_FP_OK&&control==0&&status==0,"SIMD integer comparison leaves FP status known and unchanged");
        check(fegetenv(&after)==0&&before.__fpcr==after.__fpcr&&before.__fpsr==after.__fpsr,"native/interpreted SIMD comparisons preserve host floating environment");
    }
    const uint32_t forms[]={0x2e208c00,0x0e203c00,0x0e203400,0x2e203400,0x2e203c00,0x0e208c00};
    const unsigned regs[][3]={{3,1,2},{1,1,2},{2,1,2},{31,31,31},{0,31,2},{31,1,31}};
    // Independent byte-lexicographic ordering with sign-bit bias, avoiding the
    // helper's signed integer reads/comparisons. All-ones output fills every
    // byte in a true lane, never merely its low bit. CMTST checks every byte.
    for(unsigned kind=0;kind<6;kind++)for(unsigned q=0;q<2;q++)for(unsigned size=0;size<4;size++)
    for(unsigned seed=0;seed<12;seed++)for(const auto &r:regs) {
        const bool valid=q||size!=3;const unsigned rd=r[0],rn=r[1],rm=r[2],width=1u<<size;
        const uint32_t op=forms[kind]|(q<<30)|(size<<22)|(rm<<16)|(rn<<5)|rd;
        auto p=make(words({op}));std::array<std::array<uint8_t,16>,32> bank{};
        for(unsigned reg=0;reg<32;reg++)for(unsigned b=0;b<16;b++)bank[reg][b]=uint8_t(reg*19+b*37+seed*61);
        if(seed<8)for(unsigned b=0;b<16;b++) {
            bank[rn][b]=seed==0?0:seed==1?255:seed==2?(b%width==width-1?0x7f:255):seed==3?(b%width==width-1?0x80:0):seed==4?0x55:seed==5?0xaa:seed==6?(b%width==width-1?1:0):0;
            if(rm!=rn)bank[rm][b]=seed==0?0:seed==1?255:seed==2?1:seed==3?255:seed==4?0xaa:seed==5?0x55:seed==6?0:(b%width==width-1?1:0);
        }
        for(unsigned reg=0;reg<32;reg++)check(satchel_cpu_probe_set_q(p.get(),reg,bank[reg].data()),"set independent SIMD comparison bank");
        check(satchel_cpu_probe_set_x(p.get(),1,UINT64_C(0xa55a123487654321)),"set comparison GPR sentinel");
        check(satchel_cpu_probe_test_set_known_fpcr(p.get(),0x02000000),"SIMD comparison DN control fixture");
        std::array<uint8_t,16> expected=bank[rd],observed{};
        if(valid) {
            expected.fill(0);const unsigned total=q?16:8;
            for(unsigned lane=0;lane<total;lane+=width) {
                int order=0;bool any=false;
                for(unsigned b=0;b<width;b++)any|=(bank[rn][lane+b]&bank[rm][lane+b])!=0;
                for(unsigned i=width;i>0;i--) {
                    unsigned a=bank[rn][lane+i-1],b=bank[rm][lane+i-1];
                    if((kind==1||kind==2)&&i==width){a^=128;b^=128;}
                    if(a!=b){order=a>b?1:-1;break;}
                }
                const bool value=kind==0?order==0:(kind==1||kind==4)?order>=0:(kind==2||kind==3)?order>0:any;
                std::fill(expected.begin()+lane,expected.begin()+lane+width,value?255:0);
            }
        }
        const auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
        check(stop.reason==(valid?SATCHEL_CPU_BUDGET:SATCHEL_CPU_UNSUPPORTED)&&stop.steps==(valid?1u:0u),"SIMD comparisons exact allocated vector shapes");
        for(unsigned reg=0;reg<32;reg++)check(satchel_cpu_probe_get_q(p.get(),reg,observed.data())&&observed==(reg==rd?expected:bank[reg]),"SIMD comparison exact mask lanes, overlap and other registers");
        check(x(p.get(),1)==UINT64_C(0xa55a123487654321)&&satchel_cpu_probe_test_stack_pointer(p.get())==stack_address+128,"SIMD comparison leaves separate GPR and SP banks untouched");
        uint32_t control=99,status=99;const auto fp=satchel_cpu_probe_get_fp_status(p.get(),&control,&status);
        check(valid?(fp==SATCHEL_CPU_FP_OK&&control==0x02000000&&status==0):(fp==SATCHEL_CPU_FP_INVALID&&control==99&&status==99),"accepted comparisons preserve FP status; terminal status rejection is atomic");
        std::array<uint8_t,64> memory{};check(satchel_cpu_probe_read(p.get(),data_address,memory.data(),memory.size())&&std::all_of(memory.begin(),memory.end(),[](uint8_t b){return b==0;}),"SIMD comparison has no guest memory effects");
    }
    std::array<uint8_t,16> sentinel{},observed{};sentinel.fill(0xa7);
    std::vector<uint32_t> excluded={0x4e22e423,0x6e22e423,0x4ea09823,0x4ea08823,0x4e22bc23,0xce223423};
    for(uint32_t form:forms)excluded.push_back(form|0x50c20023); // scalar D, same register comparison
    for(uint32_t op:excluded) {
        auto p=make(words({op}));check(satchel_cpu_probe_set_q(p.get(),3,sentinel.data()),"set refused comparison destination");
        const auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
        check(stop.reason==SATCHEL_CPU_UNSUPPORTED&&!stop.steps,"SIMD comparison excludes scalar, FP, zero-immediate, pairwise and reserved forms");
        check(satchel_cpu_probe_get_q(p.get(),3,observed.data())&&observed==sentinel,"refused comparison leaves destination unchanged");
    }
    for(uint32_t form:forms)for(unsigned q:{0u,1u}) {
        const uint32_t op=form|(q<<30)|(q?0x00c00000:0)|0x0002003f;
        auto p=make(words({0xc85f7c01,op,0xc8027c03,0xd4000001}));satchel_cpu_probe_set_x(p.get(),3,77);expect_stop(p.get(),SATCHEL_CPU_SVC);
        check(x(p.get(),2)==0,"SIMD comparison preserves live exclusive reservation");
        std::array<uint64_t,2> a={UINT64_C(0x7fefffffffffffff),0},b={UINT64_C(0x4000000000000000),0};
        auto fp=make(words({0x1e610802,op,0xd4000001}));satchel_cpu_probe_set_q(fp.get(),0,reinterpret_cast<uint8_t*>(a.data()));satchel_cpu_probe_set_q(fp.get(),1,reinterpret_cast<uint8_t*>(b.data()));expect_stop(fp.get(),SATCHEL_CPU_SVC);
        uint32_t control=99,status=99;check(satchel_cpu_probe_get_fp_status(fp.get(),&control,&status)==SATCHEL_CPU_FP_OK&&status==20,"SIMD comparison preserves actual cumulative FPSR20");
    }
    auto parent=make(words({0x6ee23420,0x4ee23420}));auto child=shared(parent.get());
    std::array<uint64_t,2> a={UINT64_MAX,UINT64_C(0x7fffffffffffffff)},b={1,UINT64_C(0x8000000000000000)},out{};
    for(SatchelCPUProbe *p:{parent.get(),child.get()}){satchel_cpu_probe_set_q(p,1,reinterpret_cast<uint8_t*>(a.data()));satchel_cpu_probe_set_q(p,2,reinterpret_cast<uint8_t*>(b.data()));}
    check(satchel_cpu_probe_set_pc(child.get(),code_address+4),"SIMD comparison sibling independent PC");
    check(satchel_cpu_probe_run(parent.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET&&satchel_cpu_probe_run(child.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET,"SIMD comparison contexts execute separately");
    check(satchel_cpu_probe_get_q(parent.get(),0,reinterpret_cast<uint8_t*>(out.data()))&&out[0]==UINT64_MAX&&out[1]==0,"parent unsigned comparison survives sibling execution");
    check(satchel_cpu_probe_get_q(child.get(),0,reinterpret_cast<uint8_t*>(out.data()))&&out[0]==0&&out[1]==UINT64_MAX,"child signed comparison keeps distinct owned result");
}

static void simd_addsub_tests(const char *fixtures) {
    std::array<uint8_t,1024> bytes{},actual{};
    for(unsigned seed=0;seed<16;seed++) {
        for(size_t i=0;i<bytes.size();i++)bytes[i]=uint8_t(i*37+seed*71);
        for(unsigned i=0;i<32;i++) {
            if(seed==0)bytes[i]=0;
            if(seed==1)bytes[i]=255;
            if(seed==2)bytes[i]=i<16?255:1;
            if(seed==3)bytes[i]=i<16?0:255;
            if(seed==4)bytes[i]=(i%8==7)?0x7f:255;
            if(seed==5)bytes[i]=(i%8==7)?0x80:0;
        }
        auto p=make(fixture(fixtures,"simdaddsub"),bytes.data(),bytes.size());
        fenv_t before{},after{};check(fegetenv(&before)==0,"vector add/sub native host FP state before");
        const uint64_t native=probe_simdaddsub(bytes.data());expect_stop(p.get(),SATCHEL_CPU_SVC);
        check(native==1&&x(p.get(),0)==native,"native vector add/sub all arrangements preserve NZCV");
        check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==bytes,"native vector add/sub lane wrap, source overlap and complete memory canaries agree");
        check(satchel_cpu_probe_test_stack_pointer(p.get())==stack_address+128,"vector add/sub preserves SP");
        uint32_t control=99,status=99;check(satchel_cpu_probe_get_fp_status(p.get(),&control,&status)==SATCHEL_CPU_FP_OK&&control==0&&status==0,"vector integer overflow does not set FP flags or QC");
        check(fegetenv(&after)==0&&before.__fpcr==after.__fpcr&&before.__fpsr==after.__fpsr,"native and interpreted integer SIMD arithmetic preserve host FP state");
    }
    // Independent byte-at-a-time carry/borrow oracle, rather than the helper's
    // left-justified uint64 arithmetic. The same input patterns cover sign and
    // unsigned overflow, each lane boundary, equal operands and every Q width.
    const unsigned regs[][3]={{3,1,2},{1,1,2},{2,1,2},{31,31,31},{0,31,2},{31,1,31}};
    for(unsigned q=0;q<2;q++)for(unsigned size=0;size<4;size++)for(unsigned sub=0;sub<2;sub++)
    for(unsigned seed=0;seed<12;seed++)for(const auto &r:regs) {
        const bool valid=q||size!=3;const unsigned rd=r[0],rn=r[1],rm=r[2];
        const uint32_t op=0x0e208400|(q<<30)|(sub<<29)|(size<<22)|(rm<<16)|(rn<<5)|rd;
        auto p=make(words({op}));std::array<std::array<uint8_t,16>,32> bank{};
        for(unsigned reg=0;reg<32;reg++)for(unsigned b=0;b<16;b++)bank[reg][b]=uint8_t(reg*19+b*37+seed*61);
        if(seed<6) {
            for(unsigned b=0;b<16;b++) {
                bank[rn][b]=seed==0?0:seed==1?255:seed==2?(b%(1u<<size)==(1u<<size)-1?0x7f:255):seed==3?(b%(1u<<size)==(1u<<size)-1?0x80:0):seed==4?255:0;
                if(rm!=rn)bank[rm][b]=seed==0?0:seed==1?255:seed==2?1:seed==3?255:seed==4?1:255;
            }
        }
        for(unsigned reg=0;reg<32;reg++)check(satchel_cpu_probe_set_q(p.get(),reg,bank[reg].data()),"set independent SIMD arithmetic bank");
        check(satchel_cpu_probe_set_x(p.get(),1,UINT64_C(0xa55a123487654321)),"set unrelated integer sentinel");
        check(satchel_cpu_probe_test_set_known_fpcr(p.get(),0x02000000),"vector integer arithmetic DN control fixture");
        std::array<uint8_t,16> expected=bank[rd],observed{};
        if(valid) {
            expected.fill(0);const unsigned width=1u<<size,total=q?16:8;
            for(unsigned lane=0;lane<total;lane+=width) {
                int carry=0;
                for(unsigned b=0;b<width;b++) {
                    int value=sub?int(bank[rn][lane+b])-int(bank[rm][lane+b])-carry:int(bank[rn][lane+b])+int(bank[rm][lane+b])+carry;
                    expected[lane+b]=uint8_t(value);carry=sub?(value<0):(value>255);
                }
            }
        }
        const auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
        check(stop.reason==(valid?SATCHEL_CPU_BUDGET:SATCHEL_CPU_UNSUPPORTED)&&stop.steps==(valid?1u:0u),"integer vector ADD/SUB exact allocated arrangement guard");
        for(unsigned reg=0;reg<32;reg++)check(satchel_cpu_probe_get_q(p.get(),reg,observed.data())&&observed==(reg==rd?expected:bank[reg]),"vector add/sub exact destination and unchanged other registers");
        check(x(p.get(),1)==UINT64_C(0xa55a123487654321)&&satchel_cpu_probe_test_stack_pointer(p.get())==stack_address+128,"vector and GPR register-number overlap has no integer side effects");
        uint32_t control=99,status=99;const auto fp_result=satchel_cpu_probe_get_fp_status(p.get(),&control,&status);check(valid?(fp_result==SATCHEL_CPU_FP_OK&&control==0x02000000&&status==0):(fp_result==SATCHEL_CPU_FP_INVALID&&control==99&&status==99),"integer vector add/sub preserves FP state; terminal refusals reject status query atomically");
        std::array<uint8_t,64> memory{};check(satchel_cpu_probe_read(p.get(),data_address,memory.data(),memory.size())&&std::all_of(memory.begin(),memory.end(),[](uint8_t b){return b==0;}),"vector add/sub has no memory side effects");
    }
    const uint32_t excluded[]={0x0ee28423,0x2ee28423,0x5ee28423,0x7ee28423,0x4e22d423,0x4ea2d423,0x4e220c23,0x6e222c23,0x4e22bc23,0x4e224023,0x4e620423,0x4e228023,0xce228423};
    std::array<uint8_t,16> sentinel{},observed{};sentinel.fill(0xa7);
    for(uint32_t op:excluded) {
        auto p=make(words({op}));check(satchel_cpu_probe_set_q(p.get(),3,sentinel.data()),"set refused vector destination");
        const auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
        check(stop.reason==SATCHEL_CPU_UNSUPPORTED&&!stop.steps,"ADD/SUB profile excludes scalar, floating, saturating, pairwise, widening and reserved forms");
        check(satchel_cpu_probe_get_q(p.get(),3,observed.data())&&observed==sentinel,"refused vector arithmetic leaves destination unchanged");
    }
    for(uint32_t op:{0x0e22843fu,0x4ee2843fu,0x2e22843fu,0x6ee2843fu}) {
        auto p=make(words({0xc85f7c01,op,0xc8027c03,0xd4000001}));satchel_cpu_probe_set_x(p.get(),3,77);expect_stop(p.get(),SATCHEL_CPU_SVC);
        check(x(p.get(),2)==0,"integer SIMD add/sub preserves live exclusive reservation");
        std::array<uint64_t,2> a={UINT64_C(0x7fefffffffffffff),0},b={UINT64_C(0x4000000000000000),0};
        auto fp=make(words({0x1e610802,op,0xd4000001}));satchel_cpu_probe_set_q(fp.get(),0,reinterpret_cast<uint8_t*>(a.data()));satchel_cpu_probe_set_q(fp.get(),1,reinterpret_cast<uint8_t*>(b.data()));expect_stop(fp.get(),SATCHEL_CPU_SVC);
        uint32_t control=99,status=99;check(satchel_cpu_probe_get_fp_status(fp.get(),&control,&status)==SATCHEL_CPU_FP_OK&&status==20,"integer SIMD add/sub preserves actual overflow/inexact FPSR20");
    }
    auto parent=make(words({0x4ee28420,0x6ee28420}));auto child=shared(parent.get());
    std::array<uint64_t,2> a={UINT64_MAX,UINT64_C(0x7fffffffffffffff)},b={1,1},out{};
    for(SatchelCPUProbe *p:{parent.get(),child.get()}){satchel_cpu_probe_set_q(p,1,reinterpret_cast<uint8_t*>(a.data()));satchel_cpu_probe_set_q(p,2,reinterpret_cast<uint8_t*>(b.data()));}
    check(satchel_cpu_probe_set_pc(child.get(),code_address+4),"SIMD arithmetic sibling independent PC");
    check(satchel_cpu_probe_run(parent.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET&&satchel_cpu_probe_run(child.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET,"SIMD add/sub contexts execute separately");
    check(satchel_cpu_probe_get_q(parent.get(),0,reinterpret_cast<uint8_t*>(out.data()))&&out[0]==0&&out[1]==UINT64_C(0x8000000000000000),"parent vector addition survives sibling execution");
    check(satchel_cpu_probe_get_q(child.get(),0,reinterpret_cast<uint8_t*>(out.data()))&&out[0]==UINT64_MAX-1&&out[1]==UINT64_C(0x7ffffffffffffffe),"child vector subtraction remains context-owned");
}

static uint32_t d_single_op(uint32_t form,unsigned rt,unsigned rn,int offset) {
    return form | ((uint32_t(offset)&511)<<12) | (rn<<5) | rt;
}
static uint32_t d_unsigned_op(bool load,unsigned rt,unsigned rn,unsigned immediate) {
    return (load?0xfd400000u:0xfd000000u)|(immediate<<10)|(rn<<5)|rt;
}
static uint32_t simd_reg_op(uint32_t form,bool load,unsigned rt,unsigned rn,unsigned rm,unsigned extend,bool scaled) {
    return form|(load?0x00400000:0)|(rm<<16)|(extend<<13)|(unsigned(scaled)<<12)|(rn<<5)|rt;
}
static void simd_register_tests(const char *fixtures) {
    const uint32_t stores[]={0x3c200800,0x7c200800,0xbc200800,0xfc200800,0x3ca00800};
    const size_t widths[]={1,2,4,8,16};
    std::array<uint8_t,4096> bytes{},actual{};std::array<uint8_t,16> source{},q{};
    for(unsigned seed=0;seed<8;seed++) {
        for(size_t i=0;i<bytes.size();i++)bytes[i]=uint8_t(i*37+seed*23);
        auto p=make(fixture(fixtures,"simdregister"),bytes.data(),bytes.size());fenv_t before{},after{};check(fegetenv(&before)==0,"SIMD register native host FP snapshot");
        const uint64_t expected=probe_simdregister(bytes.data());expect_stop(p.get(),SATCHEL_CPU_SVC);
        check(expected==1&&x(p.get(),0)==expected,"native every SIMD width/register extension/scaling preserves flags");
        check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==bytes,"native SIMD index extension, exact lanes and high-bit clear agree");
        check(satchel_cpu_probe_test_stack_pointer(p.get())==stack_address+128,"native SIMD RmZR and Q31 with SP preserve distinct banks");
        uint32_t control=99,status=99;check(satchel_cpu_probe_get_fp_status(p.get(),&control,&status)==SATCHEL_CPU_FP_OK&&control==0&&status==0,"native SIMD register transfers preserve owned FP state");
        check(fegetenv(&after)==0&&before.__fpcr==after.__fpcr&&before.__fpsr==after.__fpsr,"native/interpreted SIMD register memory leaves host FP state unchanged");
    }
    for(unsigned i=0;i<16;i++)source[i]=uint8_t(i*13+71);
    for(unsigned index=0;index<5;index++) {
        const size_t width=widths[index];const uint32_t store=stores[index];
        for(bool load:{false,true})for(unsigned extend:{2u,3u,6u,7u})for(bool scaled:{false,true})for(unsigned rt:{1u,31u}) {
            for(size_t i=0;i<bytes.size();i++)bytes[i]=uint8_t(i*19+3);
            const uint64_t value=extend==2?UINT64_C(0xabcdef0000000001):extend==6?UINT64_C(0x12345678ffffffff):extend==7?UINT64_MAX:1;
            const uint64_t offset=(extend<6?uint64_t{1}:UINT64_MAX)*(scaled?width:1);
            const uint64_t base=data_address+768,address=base+offset;
            auto p=make(words({simd_reg_op(store,load,rt,1,2,extend,scaled),0xd4000001}),bytes.data(),bytes.size());satchel_cpu_probe_set_x(p.get(),1,base);satchel_cpu_probe_set_x(p.get(),2,value);satchel_cpu_probe_set_q(p.get(),rt,source.data());expect_stop(p.get(),SATCHEL_CPU_SVC);
            check(x(p.get(),1)==base&&x(p.get(),2)==value&&satchel_cpu_probe_get_q(p.get(),rt,q.data()),"SIMD offset extension never writes base or offset register");
            if(load)check(!memcmp(q.data(),bytes.data()+address-data_address,width)&&std::all_of(q.begin()+width,q.end(),[](uint8_t b){return b==0;}),"SIMD register load exact width and complete upper zero");else {memcpy(bytes.data()+address-data_address,source.data(),width);check(q==source,"SIMD register store preserves whole128-bit source");}
            check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==bytes,"SIMD scaled signed/unsigned register touches only exact effective span");
        }
        // Signed32 minimum, unsigned32 maximum and modulo64 wrap are valid
        // when the complete effective span lands in mapped memory.
        struct Edge {unsigned ext;uint64_t value,offset;};
        const Edge edges[]={{2,UINT64_MAX,UINT32_MAX},{6,UINT64_C(0x1234567880000000),UINT64_C(0xffffffff80000000)},{3,UINT64_MAX,UINT64_MAX},{7,UINT64_C(0x8000000000000000),UINT64_C(0x8000000000000000)}};
        for(const auto &edge:edges)for(bool scaled:{false,true})for(bool load:{false,true}) {
            bytes.fill(0xa7);const uint64_t offset=edge.offset*(scaled?width:1),base=data_address-offset;
            auto p=make(words({simd_reg_op(store,load,2,1,2,edge.ext,scaled),0xd4000001}),bytes.data(),bytes.size());satchel_cpu_probe_set_x(p.get(),1,base);satchel_cpu_probe_set_x(p.get(),2,edge.value);satchel_cpu_probe_set_q(p.get(),2,source.data());expect_stop(p.get(),SATCHEL_CPU_SVC);
            check(x(p.get(),1)==base&&x(p.get(),2)==edge.value&&satchel_cpu_probe_get_q(p.get(),2,q.data()),"SIMD extreme offset wraps by defined modulo arithmetic with distinct SIMD/XRm bank");
            if(load)check(!memcmp(q.data(),bytes.data(),width)&&std::all_of(q.begin()+width,q.end(),[](uint8_t b){return b==0;}),"SIMD extreme signed/unsigned load reaches intended mapped bytes");else memcpy(bytes.data(),source.data(),width);
            check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==bytes,"SIMD extreme register offsets preserve memory outside exact span");
        }
        for(bool load:{false,true})for(unsigned extend:{2u,3u,6u,7u})for(bool scaled:{false,true}) {
            auto p=make(words({simd_reg_op(store,load,31,31,31,extend,scaled),0xd4000001}));satchel_cpu_probe_set_sp(p.get(),data_address);satchel_cpu_probe_set_q(p.get(),31,source.data());expect_stop(p.get(),SATCHEL_CPU_SVC);
            check(satchel_cpu_probe_test_stack_pointer(p.get())==data_address,"SIMD Rm31 is ZR even when Rn31 is SP");
            auto bad=make(words({simd_reg_op(store,load,31,31,2,extend,false)}));satchel_cpu_probe_set_sp(bad.get(),data_address+1);satchel_cpu_probe_set_x(bad.get(),2,UINT64_MAX);satchel_cpu_probe_set_q(bad.get(),31,source.data());expect_stop(bad.get(),SATCHEL_CPU_MEMORY_FAULT);
            check(satchel_cpu_probe_test_stack_pointer(bad.get())==data_address+1&&x(bad.get(),2)==UINT64_MAX&&satchel_cpu_probe_get_q(bad.get(),31,q.data())&&q==source,"SIMD originalSP alignment checked before effective indexed address");
        }
        for(bool load:{false,true})for(size_t at:{size_t{0},bytes.size()-width}) {
            bytes.fill(0x5a);auto p=make(words({simd_reg_op(store,load,1,1,2,3,false),0xd4000001}),bytes.data(),bytes.size());satchel_cpu_probe_set_x(p.get(),1,data_address);satchel_cpu_probe_set_x(p.get(),2,at);satchel_cpu_probe_set_q(p.get(),1,source.data());expect_stop(p.get(),SATCHEL_CPU_SVC);
            check(x(p.get(),1)==data_address&&x(p.get(),2)==at&&satchel_cpu_probe_get_q(p.get(),1,q.data()),"SIMD indexed first/last full-width access never writes back");
            if(load)check(!memcmp(q.data(),bytes.data()+at,width)&&std::all_of(q.begin()+width,q.end(),[](uint8_t b){return b==0;}),"SIMD indexed endpoint load and upper zero");else memcpy(bytes.data()+at,source.data(),width);
            check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==bytes,"SIMD indexed endpoints preserve all surrounding bytes");
        }
        for(bool load:{false,true}) {
            bytes.fill(0x51);const uint64_t offset=bytes.size()-width+1;auto p=make(words({simd_reg_op(store,load,1,1,2,3,false)}),bytes.data(),bytes.size());satchel_cpu_probe_set_x(p.get(),1,data_address);satchel_cpu_probe_set_x(p.get(),2,offset);satchel_cpu_probe_set_q(p.get(),1,source.data());auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
            check(stop.reason==SATCHEL_CPU_MEMORY_FAULT&&!stop.steps&&x(p.get(),1)==data_address&&x(p.get(),2)==offset,"SIMD indexed final-byte failure preserves both GPRs");
            check(satchel_cpu_probe_get_q(p.get(),1,q.data())&&q==source&&satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==bytes,"SIMD indexed full-span failure is atomic for memory and Q");
        }
        std::array<uint8_t,8192> pages{},readback{};for(size_t i=0;i<pages.size();i++)pages[i]=uint8_t(i*7+13);
        for(unsigned permission:{0u,1u})for(bool load:{false,true}) {
            const uint64_t offset=4096-(width>1?width/2:0);auto p=make(words({simd_reg_op(store,load,1,1,2,3,false),0xd4000001}),pages.data(),pages.size());satchel_cpu_probe_set_x(p.get(),1,data_address);satchel_cpu_probe_set_x(p.get(),2,offset);satchel_cpu_probe_set_q(p.get(),1,source.data());
            check(satchel_cpu_probe_set_permissions(p.get(),data_address+4096,4096,3,permission)==SATCHEL_CPU_MAP_OK,"SIMD indexed fragmented permission setup");const bool allowed=load&&permission==1;expect_stop(p.get(),allowed?SATCHEL_CPU_SVC:SATCHEL_CPU_MEMORY_FAULT);
            check(x(p.get(),1)==data_address&&x(p.get(),2)==offset&&satchel_cpu_probe_get_q(p.get(),1,q.data()),"SIMD indexed fragmented access preserves address registers");
            if(allowed)check(!memcmp(q.data(),pages.data()+offset,width)&&std::all_of(q.begin()+width,q.end(),[](uint8_t b){return b==0;}),"SIMD indexed same-backing read crosses compatible permission views");else check(q==source,"SIMD indexed protection fault preserves complete Q");
            check(satchel_cpu_probe_set_permissions(p.get(),data_address+4096,4096,permission,3)==SATCHEL_CPU_MAP_OK&&satchel_cpu_probe_read(p.get(),data_address,readback.data(),readback.size())&&readback==pages,"SIMD indexed R/NONE tail never permits a partial prefix store");
        }
        const uint64_t target=0x800000;auto alias=make(words({simd_reg_op(store,false,31,1,2,3,true),0xd4000001}),pages.data(),pages.size());check(satchel_cpu_probe_map_alias(alias.get(),target,data_address,4096)==SATCHEL_CPU_MAP_OK,"SIMD indexed alias setup");satchel_cpu_probe_set_x(alias.get(),1,target);satchel_cpu_probe_set_x(alias.get(),2,1);satchel_cpu_probe_set_q(alias.get(),31,source.data());expect_stop(alias.get(),SATCHEL_CPU_SVC);
        check(satchel_cpu_probe_unmap_alias(alias.get(),target,data_address,4096)==SATCHEL_CPU_MAP_OK&&satchel_cpu_probe_read(alias.get(),data_address+width,q.data(),width)&&!memcmp(q.data(),source.data(),width),"SIMD indexed alias store reaches retained physical bytes");
        auto ro=make(words({simd_reg_op(store,false,31,1,2,3,true)}));uint64_t token;check(satchel_cpu_probe_shared_readonly_create(ro.get(),pages.data(),4096,&token)==SATCHEL_CPU_MAP_OK&&satchel_cpu_probe_shared_readonly_map(ro.get(),token,target,4096)==SATCHEL_CPU_MAP_OK,"SIMD indexed immutable shared setup");satchel_cpu_probe_set_x(ro.get(),1,target);satchel_cpu_probe_set_x(ro.get(),2,1);expect_stop(ro.get(),SATCHEL_CPU_MEMORY_FAULT);
        check(x(ro.get(),1)==target&&x(ro.get(),2)==1&&satchel_cpu_probe_read(ro.get(),target,actual.data(),4096)&&!memcmp(actual.data(),pages.data(),4096),"SIMD indexed write never changes readonly shared page");
        for(bool load:{false,true}) {auto p=make(words({0xc85f7c01,simd_reg_op(store,load,2,0,5,3,false),0xc8037c04,0xd4000001}));satchel_cpu_probe_set_x(p.get(),4,77);satchel_cpu_probe_set_x(p.get(),5,16);satchel_cpu_probe_set_q(p.get(),2,source.data());expect_stop(p.get(),SATCHEL_CPU_SVC);check(x(p.get(),3)==(load?0:1),"SIMD indexed load preserves reservation and committed store clears it");}
        auto fp=make(words({0x1e610802,simd_reg_op(store,true,3,0,5,3,false),simd_reg_op(store,false,3,0,6,3,false),0xd4000001}));satchel_cpu_probe_set_x(fp.get(),5,0);satchel_cpu_probe_set_x(fp.get(),6,16);
        const std::array<uint64_t,2>a={UINT64_C(0x7fefffffffffffff),0},b={UINT64_C(0x4000000000000000),0};satchel_cpu_probe_set_q(fp.get(),0,reinterpret_cast<const uint8_t*>(a.data()));satchel_cpu_probe_set_q(fp.get(),1,reinterpret_cast<const uint8_t*>(b.data()));expect_stop(fp.get(),SATCHEL_CPU_SVC);uint32_t control=99,status=99;check(satchel_cpu_probe_get_fp_status(fp.get(),&control,&status)==SATCHEL_CPU_FP_OK&&status==20,"SIMD indexed all widths preserve preexisting true FPSR flags");
    }
    for(unsigned size=0;size<4;size++)for(unsigned opc=0;opc<4;opc++)for(unsigned extend=0;extend<8;extend++)for(bool scaled:{false,true}) {
        const bool valid=(opc<2||size==0)&&(extend==2||extend==3||extend==6||extend==7);const uint32_t op=0x3c200800u|(size<<30)|(opc<<22)|(2u<<16)|(extend<<13)|(unsigned(scaled)<<12)|1;
        auto p=make(words({op}));satchel_cpu_probe_set_x(p.get(),2,0);satchel_cpu_probe_set_q(p.get(),1,source.data());auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
        check(stop.reason==(valid?SATCHEL_CPU_BUDGET:SATCHEL_CPU_UNSUPPORTED)&&stop.steps==(valid?1u:0u),"SIMD indexed every size/opc/extension/S classification");
        if(!valid)check(satchel_cpu_probe_get_q(p.get(),1,q.data())&&q==source,"SIMD unallocated register shape leaves Q unchanged");
    }
    for(uint32_t op:{0x3c206801u,0x3c606801u}) {auto p=make(words({op}));satchel_cpu_probe_set_x(p.get(),0,data_address/2);satchel_cpu_probe_set_q(p.get(),1,source.data());auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(stop.reason==SATCHEL_CPU_BUDGET&&stop.steps==1&&x(p.get(),0)==data_address/2,"formerly excluded allocated SIMD register transfers now execute exactly once");check(satchel_cpu_probe_get_q(p.get(),1,q.data()),"formerly excluded SIMD register destination observable");if(op&0x00400000)check(std::all_of(q.begin(),q.end(),[](uint8_t b){return b==0;}),"new positive byte indexed load zeros all upper bits");else {std::array<uint8_t,64> memory{};check(satchel_cpu_probe_read(p.get(),data_address,memory.data(),memory.size())&&memory[0]==source[0]&&std::all_of(memory.begin()+1,memory.end(),[](uint8_t b){return b==0;}),"new positive byte indexed store has exact single-byte effect");}}
    auto overlap=make(words({simd_reg_op(0x3ca00800,false,1,1,1,3,false),0xd4000001}));satchel_cpu_probe_set_x(overlap.get(),1,data_address/2);satchel_cpu_probe_set_q(overlap.get(),1,source.data());expect_stop(overlap.get(),SATCHEL_CPU_SVC);check(x(overlap.get(),1)==data_address/2&&satchel_cpu_probe_read(overlap.get(),data_address,q.data(),16)&&q==source,"SIMD same Rn/Rm/Rt number uses two reads of X and independent Q");
    auto parent=make(words({simd_reg_op(0x3ca00800,false,1,0,2,3,false),0xd4000001}));auto child=shared(parent.get());satchel_cpu_probe_set_pc(child.get(),code_address);satchel_cpu_probe_set_x(child.get(),0,data_address);satchel_cpu_probe_set_x(parent.get(),2,0);satchel_cpu_probe_set_x(child.get(),2,16);satchel_cpu_probe_set_q(parent.get(),1,source.data());std::array<uint8_t,16>other{};other.fill(0x82);satchel_cpu_probe_set_q(child.get(),1,other.data());expect_stop(parent.get(),SATCHEL_CPU_SVC);expect_stop(child.get(),SATCHEL_CPU_SVC);
    check(satchel_cpu_probe_read(parent.get(),data_address,q.data(),16)&&q==source&&satchel_cpu_probe_read(parent.get(),data_address+16,q.data(),16)&&q==other&&satchel_cpu_probe_get_q(parent.get(),1,q.data())&&q==source,"SIMD indexed sibling offsets and registers independent over shared memory");
}

static void simd_width_tests(const char *fixtures) {
    const uint32_t stores[]={0x3c000000,0x7c000000,0xbc000000,0x3c800000};
    const size_t widths[]={1,2,4,16};
    std::array<uint8_t,4096> bytes{},actual{};std::array<uint8_t,16> source{},q{};
    for(unsigned seed=0;seed<8;seed++) {
        for(size_t i=0;i<bytes.size();i++)bytes[i]=uint8_t(i*19+seed*37);
        auto p=make(fixture(fixtures,"simdsinglewidths"),bytes.data(),bytes.size());
        fenv_t host_before{},host_after{};check(fegetenv(&host_before)==0,"SIMD width native host FP snapshot");
        const uint64_t expected=probe_simdsinglewidths(bytes.data());auto stop=satchel_cpu_probe_run(p.get(),2000,nullptr,nullptr);
        check(stop.reason==SATCHEL_CPU_SVC&&expected==1&&x(p.get(),0)==expected,"native B/H/S/Q every signed mode/endpoints and flags");
        check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==bytes,"native SIMD exact-width bytes, upper-lane zero and canaries");
        check(satchel_cpu_probe_test_stack_pointer(p.get())==stack_address+128,"native B/H/S/Q SP and Q31 separate banks restore stack");
        uint32_t control=99,status=99;check(satchel_cpu_probe_get_fp_status(p.get(),&control,&status)==SATCHEL_CPU_FP_OK&&control==0&&status==0,"native SIMD bit copies preserve owned FP status");
        check(fegetenv(&host_after)==0&&host_after.__fpcr==host_before.__fpcr&&host_after.__fpsr==host_before.__fpsr,"native/interpreted SIMD width transfers leave host FP untouched");
    }
    for(unsigned i=0;i<16;i++)source[i]=uint8_t(i*23+13);
    for(unsigned index=0;index<4;index++) {
        const size_t width=widths[index];const uint32_t store=stores[index];
        for(bool load:{false,true})for(unsigned mode:{0u,1u,3u})for(unsigned rt:{1u,31u})for(int offset:{-256,-1,0,1,255}) {
            for(size_t i=0;i<bytes.size();i++)bytes[i]=uint8_t(i*17+9);
            uint32_t op=d_single_op(store|(load?0x00400000:0)|(mode<<10),rt,1,offset);
            auto p=make(words({op,0xd4000001}),bytes.data(),bytes.size());const uint64_t base=data_address+768,address=mode==1?base:base+offset;
            satchel_cpu_probe_set_x(p.get(),1,base);satchel_cpu_probe_set_q(p.get(),rt,source.data());expect_stop(p.get(),SATCHEL_CPU_SVC);
            check(x(p.get(),1)==(mode?base+offset:base)&&satchel_cpu_probe_get_q(p.get(),rt,q.data()),"SIMD all widths signed writeback including zero and same-number banks");
            if(load)check(!memcmp(q.data(),bytes.data()+address-data_address,width)&&std::all_of(q.begin()+width,q.end(),[](uint8_t b){return b==0;}),"SIMD narrow load zeroes every bit above width; Q keeps full128");
            else {memcpy(bytes.data()+address-data_address,source.data(),width);check(q==source,"SIMD width store preserves complete source register");}
            check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==bytes,"SIMD access touches exact width only");
        }
        for(bool load:{false,true})for(unsigned mode:{0u,1u,3u})for(size_t edge:{size_t{0},bytes.size()-width}) {
            bytes.fill(0xa7);const int offset=mode==3?-1:mode==1?1:0;const uint64_t address=data_address+edge,base=mode==3?address+1:address;
            auto p=make(words({d_single_op(store|(load?0x00400000:0)|(mode<<10),1,1,offset),0xd4000001}),bytes.data(),bytes.size());satchel_cpu_probe_set_x(p.get(),1,base);satchel_cpu_probe_set_q(p.get(),1,source.data());expect_stop(p.get(),SATCHEL_CPU_SVC);
            check(x(p.get(),1)==(mode?base+offset:base)&&satchel_cpu_probe_get_q(p.get(),1,q.data()),"SIMD exact first/last byte span and prescribed base");
            if(load)check(!memcmp(q.data(),bytes.data()+edge,width)&&std::all_of(q.begin()+width,q.end(),[](uint8_t b){return b==0;}),"SIMD endpoint full-width load and zero extension");else {memcpy(bytes.data()+edge,source.data(),width);check(q==source,"SIMD endpoint store retains whole source");}
            check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==bytes,"SIMD endpoint writes preserve surrounding bytes");
        }
        for(bool load:{false,true})for(unsigned mode:{0u,1u,3u}) {
            bytes.fill(0x65);const uint64_t end=data_address+bytes.size(),address=end-width+1,base=mode==3?end:address;const int offset=mode==3?1-int(width):mode==1?16:0;
            auto p=make(words({d_single_op(store|(load?0x00400000:0)|(mode<<10),1,1,offset)}),bytes.data(),bytes.size());satchel_cpu_probe_set_x(p.get(),1,base);satchel_cpu_probe_set_q(p.get(),1,source.data());auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
            check(stop.reason==SATCHEL_CPU_MEMORY_FAULT&&!stop.steps&&x(p.get(),1)==base,"SIMD tail-byte fault precedes any pre/post writeback");
            check(satchel_cpu_probe_get_q(p.get(),1,q.data())&&q==source&&satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==bytes,"SIMD tail fault leaves every register/backing byte unchanged");
            auto sp=make(words({d_single_op(store|(load?0x00400000:0)|(mode<<10),31,31,-1)}));satchel_cpu_probe_set_sp(sp.get(),data_address+1);satchel_cpu_probe_set_q(sp.get(),31,source.data());expect_stop(sp.get(),SATCHEL_CPU_MEMORY_FAULT);
            check(satchel_cpu_probe_test_stack_pointer(sp.get())==data_address+1&&satchel_cpu_probe_get_q(sp.get(),31,q.data())&&q==source,"SIMD original misaligned SP rejects even aligned effective address");
        }
        std::array<uint8_t,8192> pages{},readback{};for(size_t i=0;i<pages.size();i++)pages[i]=uint8_t(i*31+5);
        for(unsigned permission:{0u,1u})for(bool load:{false,true}) {
            const uint64_t address=data_address+4096-(width>1?width/2:0);
            auto p=make(words({d_single_op(store|(load?0x00400000:0)|0xc00,1,1,-1),0xd4000001}),pages.data(),pages.size());satchel_cpu_probe_set_x(p.get(),1,address+1);satchel_cpu_probe_set_q(p.get(),1,source.data());
            check(satchel_cpu_probe_set_permissions(p.get(),data_address+4096,4096,3,permission)==SATCHEL_CPU_MAP_OK,"SIMD mixed-permission retained backing setup");const bool allowed=load&&permission==1;expect_stop(p.get(),allowed?SATCHEL_CPU_SVC:SATCHEL_CPU_MEMORY_FAULT);
            check(x(p.get(),1)==address+(allowed?0:1)&&satchel_cpu_probe_get_q(p.get(),1,q.data()),"SIMD permission preflight protects writeback and whole register");
            if(allowed)check(!memcmp(q.data(),pages.data()+address-data_address,width)&&std::all_of(q.begin()+width,q.end(),[](uint8_t b){return b==0;}),"SIMD same-backing R/RW read crosses views with correct width");else check(q==source,"SIMD R/NONE fault preserves full128-bit register");
            check(satchel_cpu_probe_set_permissions(p.get(),data_address+4096,4096,permission,3)==SATCHEL_CPU_MAP_OK&&satchel_cpu_probe_read(p.get(),data_address,readback.data(),readback.size())&&readback==pages,"SIMD protected second fragment never permits partial store");
        }
        const uint64_t target=0x800000;auto alias=make(words({d_single_op(store|0xc00,31,1,16),0xd4000001}),pages.data(),pages.size());
        check(satchel_cpu_probe_map_alias(alias.get(),target,data_address,4096)==SATCHEL_CPU_MAP_OK,"SIMD retained alias setup");satchel_cpu_probe_set_x(alias.get(),1,target);satchel_cpu_probe_set_q(alias.get(),31,source.data());expect_stop(alias.get(),SATCHEL_CPU_SVC);
        check(x(alias.get(),1)==target+16&&satchel_cpu_probe_unmap_alias(alias.get(),target,data_address,4096)==SATCHEL_CPU_MAP_OK&&satchel_cpu_probe_read(alias.get(),data_address+16,q.data(),width)&&!memcmp(q.data(),source.data(),width),"SIMD alias write survives original backing restoration");
        auto ro=make(words({d_single_op(store|0xc00,31,1,16)}));uint64_t token;check(satchel_cpu_probe_shared_readonly_create(ro.get(),pages.data(),4096,&token)==SATCHEL_CPU_MAP_OK&&satchel_cpu_probe_shared_readonly_map(ro.get(),token,target,4096)==SATCHEL_CPU_MAP_OK,"SIMD immutable shared-page setup");satchel_cpu_probe_set_x(ro.get(),1,target);expect_stop(ro.get(),SATCHEL_CPU_MEMORY_FAULT);
        check(x(ro.get(),1)==target&&satchel_cpu_probe_read(ro.get(),target,actual.data(),4096)&&!memcmp(actual.data(),pages.data(),4096),"SIMD width store never modifies time shared page or base");
        for(bool load:{false,true}) {auto p=make(words({0xc85f7c01,d_single_op(store|(load?0x00400000:0),2,0,16),0xc8037c04,0xd4000001}));satchel_cpu_probe_set_x(p.get(),4,77);satchel_cpu_probe_set_q(p.get(),2,source.data());expect_stop(p.get(),SATCHEL_CPU_SVC);check(x(p.get(),3)==(load?0:1),"SIMD ordinary load preserves reservation; committed store clears it");}
        auto fp=make(words({0x1e610802,d_single_op(store|0x00400000,3,0,0),d_single_op(store,3,0,16),0xd4000001}));
        const std::array<uint64_t,2>a={UINT64_C(0x7fefffffffffffff),0},b={UINT64_C(0x4000000000000000),0};satchel_cpu_probe_set_q(fp.get(),0,reinterpret_cast<const uint8_t*>(a.data()));satchel_cpu_probe_set_q(fp.get(),1,reinterpret_cast<const uint8_t*>(b.data()));expect_stop(fp.get(),SATCHEL_CPU_SVC);uint32_t control=99,status=99;check(satchel_cpu_probe_get_fp_status(fp.get(),&control,&status)==SATCHEL_CPU_FP_OK&&status==20,"SIMD every width retains true preexisting cumulative FPSR");
    }
    // Previously negative pre-index widths are now explicit positive one-step checks.
    for(uint32_t op:{0x3c000c00u,0x7c000c00u,0xbc000c00u,0x3c800c00u,0x3c000001u,0x7c000001u}) {
        auto p=make(words({op}));satchel_cpu_probe_set_q(p.get(),op&31,source.data());auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
        check(stop.reason==SATCHEL_CPU_BUDGET&&stop.steps==1&&x(p.get(),0)==data_address,"formerly unsupported SIMD widths execute one zero-writeback instruction");
        const size_t width=(op&0x800000)?16:size_t{1}<<(op>>30);std::array<uint8_t,64> memory{};
        check(satchel_cpu_probe_read(p.get(),data_address,memory.data(),memory.size())&&!memcmp(memory.data(),source.data(),width)&&std::all_of(memory.begin()+width,memory.end(),[](uint8_t b){return b==0;}),"formerly excluded SIMD stores now have exact positive byte effects and intact canaries");
    }
    for(unsigned size=0;size<4;size++)for(unsigned opc=0;opc<4;opc++)for(unsigned mode=0;mode<4;mode++) {
        const bool valid=mode!=2&&(opc<2||size==0);auto p=make(words({0x3c000000u|(size<<30)|(opc<<22)|(mode<<10)}));satchel_cpu_probe_set_q(p.get(),0,source.data());auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
        check(stop.reason==(valid?SATCHEL_CPU_BUDGET:SATCHEL_CPU_UNSUPPORTED)&&stop.steps==(valid?1u:0u),"SIMD every size/opc/mode allocation shape is guarded");
        if(!valid)check(satchel_cpu_probe_get_q(p.get(),0,q.data())&&q==source,"reserved SIMD shape preserves destination");
    }
    for(uint32_t op:{0x3c200800u,0x3c600800u,0x3ca00800u,0x3ce00800u,0x7c200800u,0xbc600800u,0x3d000000u,0x7d400000u,0xbd400000u,0x9c000000u}){auto p=make(words({op}));satchel_cpu_probe_set_q(p.get(),0,source.data());auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(stop.reason==SATCHEL_CPU_UNSUPPORTED&&!stop.steps&&satchel_cpu_probe_get_q(p.get(),0,q.data())&&q==source,"SIMD unallocated extension/literal and new unsigned widths remain outside profile");}
    auto parent=make(words({d_single_op(0x3c800000,1,0,0),0xd4000001}));auto child=shared(parent.get());satchel_cpu_probe_set_pc(child.get(),code_address);check(satchel_cpu_probe_set_x(child.get(),0,data_address),"Q sibling explicit data argument");satchel_cpu_probe_set_q(parent.get(),1,source.data());std::array<uint8_t,16>other{};other.fill(0x71);satchel_cpu_probe_set_q(child.get(),1,other.data());expect_stop(parent.get(),SATCHEL_CPU_SVC);expect_stop(child.get(),SATCHEL_CPU_SVC);
    check(satchel_cpu_probe_read(parent.get(),data_address,q.data(),16)&&q==other&&satchel_cpu_probe_get_q(parent.get(),1,q.data())&&q==source,"Q shared backing remains distinct from per-context SIMD state");
}

static void simd_unsigned_tests(const char *fixtures) {
    std::array<uint8_t,32768> bytes{},actual{};
    for(uint64_t value:{uint64_t{0},UINT64_C(0x8000000000000000),uint64_t{1},UINT64_C(0x000fffffffffffff),UINT64_C(0x7ff0000000000000),UINT64_C(0x7ff8000000001234),UINT64_C(0x7ff0000000001234),UINT64_C(0xfff0000000005678),UINT64_MAX}) {
        for(size_t i=0;i<bytes.size();i++)bytes[i]=uint8_t(i*13+17);
        memcpy(bytes.data(),&value,8);memcpy(bytes.data()+16,&value,8);
        auto p=make(fixture(fixtures,"dunsigned"),bytes.data(),bytes.size());
        uint32_t control_before,status_before,control_after,status_after;
        check(satchel_cpu_probe_get_fp_status(p.get(),&control_before,&status_before)==SATCHEL_CPU_FP_OK,"D unsigned native fixture FP status before");
        fenv_t host_before{},host_after{};check(fegetenv(&host_before)==0,"D unsigned host FP snapshot");
        const uint64_t result=probe_dunsigned(bytes.data());expect_stop(p.get(),SATCHEL_CPU_SVC);
        check(result==1&&x(p.get(),0)==result,"native unsigned D preserves NZCV and separate D/X registers");
        check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==bytes,"native unsigned D zero/max/SP loads stores preserve NaN bits and clear upper64");
        check(satchel_cpu_probe_test_stack_pointer(p.get())==stack_address+128,"unsigned D native SP restored");
        check(satchel_cpu_probe_get_fp_status(p.get(),&control_after,&status_after)==SATCHEL_CPU_FP_OK&&control_after==control_before&&status_after==status_before,"unsigned D preserves owned FPCR/FPSR");
        check(fegetenv(&host_after)==0&&host_after.__fpcr==host_before.__fpcr&&host_after.__fpsr==host_before.__fpsr,"unsigned D including signaling NaN preserves host FP state");
    }
    std::array<uint8_t,16> source{},q{};for(unsigned i=0;i<16;i++)source[i]=uint8_t(i*31+11);
    for(bool load:{false,true})for(unsigned rt:{1u,31u})for(unsigned immediate:{0u,1u,2u,3u,255u,4094u,4095u}) {
        for(size_t i=0;i<bytes.size();i++)bytes[i]=uint8_t(i*7+21);
        auto p=make(words({d_unsigned_op(load,rt,1,immediate),0xd4000001}),bytes.data(),bytes.size());satchel_cpu_probe_set_x(p.get(),1,data_address);satchel_cpu_probe_set_q(p.get(),rt,source.data());expect_stop(p.get(),SATCHEL_CPU_SVC);
        check(x(p.get(),1)==data_address,"D unsigned immediate low bits0/1/2/3 never cause writeback");
        check(satchel_cpu_probe_get_q(p.get(),rt,q.data()),"D unsigned reads full SIMD state including D31");
        if(load)check(!memcmp(q.data(),bytes.data()+immediate*8,8)&&std::all_of(q.begin()+8,q.end(),[](uint8_t b){return b==0;}),"D unsigned scaled offset reads low64 and clears upper64");
        else {memcpy(bytes.data()+immediate*8,source.data(),8);check(q==source,"D unsigned store leaves full128 source intact");}
        check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==bytes,"D unsigned exact first/last8-byte and intermediate memory effects");
    }
    // These exact forms were previously rejected. Retain explicit positive
    // assertions now that they are reviewed, while keeping other widths excluded.
    for(uint32_t op:{0xfd000000u,0xfd400000u}){auto p=make(words({op}));const uint64_t base=x(p.get(),0);auto result=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(result.reason==SATCHEL_CPU_BUDGET&&result.steps==1&&x(p.get(),0)==base,"formerly unsupported D unsigned forms now execute exactly once without base mutation");}
    for(bool load:{false,true})for(unsigned rn:{1u,31u}) {
        bytes.fill(0xa9);const uint64_t base=data_address+(rn==31?16:4);
        auto p=make(words({d_unsigned_op(load,1,rn,4095)}),bytes.data(),bytes.size());if(rn==31)satchel_cpu_probe_set_sp(p.get(),base);else satchel_cpu_probe_set_x(p.get(),rn,base);satchel_cpu_probe_set_q(p.get(),1,source.data());
        auto result=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(result.reason==SATCHEL_CPU_MEMORY_FAULT&&!result.steps,"D unsigned tail/outside access faults before commit");
        check((rn==31?satchel_cpu_probe_test_stack_pointer(p.get()):x(p.get(),rn))==base&&satchel_cpu_probe_get_q(p.get(),1,q.data())&&q==source,"D unsigned fault preserves base/SP and complete SIMD state");
        check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==bytes,"D unsigned whole-span fault preserves backing canaries");
    }
    for(bool load:{false,true}){auto p=make(words({d_unsigned_op(load,31,31,1)}));satchel_cpu_probe_set_sp(p.get(),data_address+8);satchel_cpu_probe_set_q(p.get(),31,source.data());expect_stop(p.get(),SATCHEL_CPU_MEMORY_FAULT);check(satchel_cpu_probe_test_stack_pointer(p.get())==data_address+8&&satchel_cpu_probe_get_q(p.get(),31,q.data())&&q==source,"unsigned D checks original SP alignment even when effective address is aligned");}
    std::array<uint8_t,8192> pages{},readback{};for(size_t i=0;i<pages.size();i++)pages[i]=uint8_t(i*17+3);
    for(unsigned permission:{0u,1u})for(bool load:{false,true}) {
        auto p=make(words({d_unsigned_op(load,1,1,0),0xd4000001}),pages.data(),pages.size());satchel_cpu_probe_set_x(p.get(),1,data_address+4092);satchel_cpu_probe_set_q(p.get(),1,source.data());
        check(satchel_cpu_probe_set_permissions(p.get(),data_address+4096,4096,3,permission)==SATCHEL_CPU_MAP_OK,"D unsigned fragmented permission fixture");
        const bool allowed=permission==1&&load;expect_stop(p.get(),allowed?SATCHEL_CPU_SVC:SATCHEL_CPU_MEMORY_FAULT);
        check(x(p.get(),1)==data_address+4092&&satchel_cpu_probe_get_q(p.get(),1,q.data()),"D unsigned fragmented access never writes base");
        if(allowed)check(!memcmp(q.data(),pages.data()+4092,8)&&std::all_of(q.begin()+8,q.end(),[](uint8_t b){return b==0;}),"D unsigned same-backing RW/R load succeeds exactly");else check(q==source,"D unsigned R/NONE refusal preserves full SIMD register");
        check(satchel_cpu_probe_set_permissions(p.get(),data_address+4096,4096,permission,3)==SATCHEL_CPU_MAP_OK&&satchel_cpu_probe_read(p.get(),data_address,readback.data(),readback.size())&&readback==pages,"D unsigned mixed permission refuses partial stores to backing");
    }
    const uint64_t target=0x800000;
    auto alias=make(words({d_unsigned_op(false,31,1,3),0xd4000001}),pages.data(),pages.size());check(satchel_cpu_probe_map_alias(alias.get(),target,data_address,4096)==SATCHEL_CPU_MAP_OK,"D unsigned retained alias fixture");satchel_cpu_probe_set_x(alias.get(),1,target);satchel_cpu_probe_set_q(alias.get(),31,source.data());expect_stop(alias.get(),SATCHEL_CPU_SVC);
    check(x(alias.get(),1)==target&&satchel_cpu_probe_unmap_alias(alias.get(),target,data_address,4096)==SATCHEL_CPU_MAP_OK&&satchel_cpu_probe_read(alias.get(),data_address+24,q.data(),8)&&!memcmp(q.data(),source.data(),8),"D unsigned alias write survives source restoration without writeback");
    auto ro=make(words({d_unsigned_op(false,31,1,1)}));uint64_t token;check(satchel_cpu_probe_shared_readonly_create(ro.get(),pages.data(),4096,&token)==SATCHEL_CPU_MAP_OK&&satchel_cpu_probe_shared_readonly_map(ro.get(),token,target,4096)==SATCHEL_CPU_MAP_OK,"D unsigned immutable shared-page fixture");satchel_cpu_probe_set_x(ro.get(),1,target);expect_stop(ro.get(),SATCHEL_CPU_MEMORY_FAULT);check(x(ro.get(),1)==target&&satchel_cpu_probe_read(ro.get(),target,actual.data(),4096)&&!memcmp(actual.data(),pages.data(),4096),"D unsigned cannot write shared time page");
    for(uint32_t op:{0x3d000000u,0x3d400000u,0x7d000000u,0x7d400000u,0xbd000000u,0xbd400000u,0xfd800000u,0xfdc00000u,0xfc200800u,0xfc600800u,0x5c000000u}){auto p=make(words({op}));satchel_cpu_probe_set_q(p.get(),0,source.data());auto result=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(result.reason==SATCHEL_CPU_UNSUPPORTED&&!result.steps&&satchel_cpu_probe_get_q(p.get(),0,q.data())&&q==source,"D unsigned still rejects other scalar widths/reserved/register/literal forms");}
    for(bool load:{false,true}){auto p=make(words({0xc85f7c01,d_unsigned_op(load,2,0,2),0xc8037c04,0xd4000001}));satchel_cpu_probe_set_x(p.get(),4,77);satchel_cpu_probe_set_q(p.get(),2,source.data());expect_stop(p.get(),SATCHEL_CPU_SVC);check(x(p.get(),3)==(load?0:1),"D unsigned load preserves local exclusive reservation and store clears it");}
    auto fp=make(words({0x1e610802,d_unsigned_op(true,3,0,0),d_unsigned_op(false,3,0,2),0xd4000001}));
    const std::array<uint64_t,2> a={UINT64_C(0x7fefffffffffffff),0},b={UINT64_C(0x4000000000000000),0};satchel_cpu_probe_set_q(fp.get(),0,reinterpret_cast<const uint8_t*>(a.data()));satchel_cpu_probe_set_q(fp.get(),1,reinterpret_cast<const uint8_t*>(b.data()));expect_stop(fp.get(),SATCHEL_CPU_SVC);uint32_t control=99,status=99;check(satchel_cpu_probe_get_fp_status(fp.get(),&control,&status)==SATCHEL_CPU_FP_OK&&status==20,"D unsigned transfers retain actual preexisting cumulative FPSR flags");
}

static void simd_single_tests(const char *fixtures) {
    std::array<uint8_t,4096> bytes{},actual{};
    const uint64_t bits[]={0,UINT64_C(0x8000000000000000),1,UINT64_C(0xfffffffffffff),UINT64_C(0x7ff0000000000000),UINT64_C(0x7ff8000000001234),UINT64_C(0x7ff0000000001234),UINT64_C(0xfff0000000005678),UINT64_MAX};
    for(uint64_t value:bits) {
        for(size_t i=0;i<bytes.size();i++)bytes[i]=uint8_t(i*17+11);
        std::memcpy(bytes.data(),&value,8);std::memcpy(bytes.data()+16,&value,8);
        auto p=make(fixture(fixtures,"dsingle"),bytes.data(),bytes.size());
        uint32_t control_before,status_before,control_after,status_after;
        check(satchel_cpu_probe_get_fp_status(p.get(),&control_before,&status_before)==SATCHEL_CPU_FP_OK,"D single native fixture initial FP status");
        fenv_t host_before{},host_after{};check(fegetenv(&host_before)==0,"D single host FP snapshot");
        const uint64_t result=probe_dsingle(bytes.data());expect_stop(p.get(),SATCHEL_CPU_SVC);
        check(fegetenv(&host_after)==0 && host_before.__fpcr==host_after.__fpcr && host_before.__fpsr==host_after.__fpsr,"D single copies including signaling NaNs preserve host FP environment");
        check(result==1 && x(p.get(),0)==result,"native D unscaled/pre/post preserves NZCV and distinct D/X register banks");
        check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size()) && actual==bytes,"native D transfers preserve every bit including NaNs and clear load upper64 bits");
        check(satchel_cpu_probe_test_stack_pointer(p.get())==stack_address+128,"D stack store/load restores SP");
        check(satchel_cpu_probe_get_fp_status(p.get(),&control_after,&status_after)==SATCHEL_CPU_FP_OK && control_before==control_after && status_before==status_after,"D bit transfers preserve owned FPCR/FPSR");
    }
    std::array<uint8_t,16> source{},q{};for(unsigned i=0;i<16;i++)source[i]=uint8_t(i*23+7);
    const uint32_t forms[]={0xfc000000,0xfc400000,0xfc000400,0xfc400400,0xfc000c00,0xfc400c00};
    for(uint32_t form:forms)for(unsigned rt:{1u,31u})for(int offset:{-256,-8,0,8,255}) {
        for(size_t i=0;i<bytes.size();i++)bytes[i]=uint8_t(i*37+19);
        const bool load=form&0x00400000;const unsigned mode=(form>>10)&3;
        auto p=make(words({d_single_op(form,rt,1,offset),0xd4000001}),bytes.data(),bytes.size());
        const uint64_t base=data_address+768,address=mode==1?base:base+offset;
        satchel_cpu_probe_set_x(p.get(),1,base);satchel_cpu_probe_set_q(p.get(),rt,source.data());expect_stop(p.get(),SATCHEL_CPU_SVC);
        check(x(p.get(),1)==(mode?base+offset:base),"D single signed offset/writeback endpoints including zero");
        check(satchel_cpu_probe_get_q(p.get(),rt,q.data()),"D31 is actual vector state");
        if(load)check(!std::memcmp(q.data(),bytes.data()+address-data_address,8)&&std::all_of(q.begin()+8,q.end(),[](uint8_t b){return b==0;}),"D load copies64 bits and zeros entire upper64");
        else {std::memcpy(bytes.data()+address-data_address,source.data(),8);check(q==source,"D store preserves all128 source register bits");}
        check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==bytes,"D single touches only exact8 bytes");
    }
    for(uint32_t form:forms)for(size_t edge:{size_t{0},bytes.size()-8}) {
        for(size_t i=0;i<bytes.size();i++)bytes[i]=uint8_t(i*13+9);
        const unsigned mode=(form>>10)&3;const bool load=form&0x00400000;const int offset=mode==3?-8:mode==1?8:0;
        const uint64_t address=data_address+edge,base=mode==3?address+8:address;
        auto p=make(words({d_single_op(form,1,1,offset),0xd4000001}),bytes.data(),bytes.size());satchel_cpu_probe_set_x(p.get(),1,base);satchel_cpu_probe_set_q(p.get(),1,source.data());expect_stop(p.get(),SATCHEL_CPU_SVC);
        check(x(p.get(),1)==(mode?base+offset:base),"D exact first/last8-byte access preserves prescribed base update");
        check(satchel_cpu_probe_get_q(p.get(),1,q.data()),"D endpoint Q snapshot");
        if(load)check(!memcmp(q.data(),bytes.data()+edge,8)&&std::all_of(q.begin()+8,q.end(),[](uint8_t b){return b==0;}),"D exact endpoint load reads complete span only");
        else {memcpy(bytes.data()+edge,source.data(),8);check(q==source,"D endpoint store retains complete source");}
        check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==bytes,"D first/last access retains surrounding canaries");
    }
    for(uint32_t form:forms)for(unsigned rn:{1u,31u}) {
        bytes.fill(0xb7);const unsigned mode=(form>>10)&3;
        const uint64_t end=data_address+bytes.size();const int offset=mode==1?16:-4;
        const uint64_t base=(rn==31 || mode!=1)?end:end-4;
        auto p=make(words({d_single_op(form,1,rn,offset)}),bytes.data(),bytes.size());
        if(rn==31)satchel_cpu_probe_set_sp(p.get(),base);else satchel_cpu_probe_set_x(p.get(),rn,base);
        satchel_cpu_probe_set_q(p.get(),1,source.data());auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
        check(stop.reason==SATCHEL_CPU_MEMORY_FAULT&&!stop.steps,"D whole-span tail/unmapped access faults before instruction commit");
        check((rn==31?satchel_cpu_probe_test_stack_pointer(p.get()):x(p.get(),rn))==base,"D fault preserves pre/post base or SP");
        check(satchel_cpu_probe_get_q(p.get(),1,q.data())&&q==source&&satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==bytes,"D fault preserves complete destination and backing");
    }
    for(uint32_t form:forms){auto p=make(words({d_single_op(form,31,31,0)}));satchel_cpu_probe_set_sp(p.get(),data_address+1);satchel_cpu_probe_set_q(p.get(),31,source.data());expect_stop(p.get(),SATCHEL_CPU_MEMORY_FAULT);check(satchel_cpu_probe_test_stack_pointer(p.get())==data_address+1&&satchel_cpu_probe_get_q(p.get(),31,q.data())&&q==source,"D stack alignment faults before writeback or SIMD mutation");}
    std::array<uint8_t,8192> pages{},readback{};for(size_t i=0;i<pages.size();i++)pages[i]=uint8_t(i*11+3);
    for(bool load:{false,true}) {
        auto p=make(words({d_single_op(load?0xfc400c00:0xfc000c00,1,1,-4),0xd4000001}),pages.data(),pages.size());
        satchel_cpu_probe_set_x(p.get(),1,data_address+4096);satchel_cpu_probe_set_q(p.get(),1,source.data());
        check(satchel_cpu_probe_set_permissions(p.get(),data_address+4096,4096,3,1)==SATCHEL_CPU_MAP_OK,"D cross-view reprotection fixture");
        expect_stop(p.get(),load?SATCHEL_CPU_SVC:SATCHEL_CPU_MEMORY_FAULT);
        check(x(p.get(),1)==data_address+4096-(load?4:0),"D same-backing mixed permission span guards writeback");
        check(satchel_cpu_probe_read(p.get(),data_address,readback.data(),readback.size())&&readback==pages,"D cross-view read or refused store preserves every backing byte");
        check(satchel_cpu_probe_get_q(p.get(),1,q.data()),"D cross-view register read");
        if(load)check(!memcmp(q.data(),pages.data()+4092,8)&&std::all_of(q.begin()+8,q.end(),[](uint8_t b){return b==0;}),"D allowed cross-view read exact low bits and upper clear");
        else check(q==source,"D refused mixed-permission store keeps source intact");
    }
    for(bool load:{false,true}) {
        auto p=make(words({d_single_op(load?0xfc400c00:0xfc000c00,1,1,-4)}),pages.data(),pages.size());satchel_cpu_probe_set_x(p.get(),1,data_address+4096);satchel_cpu_probe_set_q(p.get(),1,source.data());
        check(satchel_cpu_probe_set_permissions(p.get(),data_address+4096,4096,3,0)==SATCHEL_CPU_MAP_OK,"D split inaccessible tail fixture");expect_stop(p.get(),SATCHEL_CPU_MEMORY_FAULT);
        check(x(p.get(),1)==data_address+4096&&satchel_cpu_probe_get_q(p.get(),1,q.data())&&q==source,"D RW/NONE fault preserves whole register and pre-index base");
        check(satchel_cpu_probe_set_permissions(p.get(),data_address+4096,4096,0,3)==SATCHEL_CPU_MAP_OK&&satchel_cpu_probe_read(p.get(),data_address,readback.data(),readback.size())&&readback==pages,"D inaccessible second half never mutates accessible prefix or retained hidden backing");
    }
    auto alias=make(words({d_single_op(0xfc000c00,31,1,8),0xd4000001}),pages.data(),pages.size());const uint64_t target=0x800000;
    check(satchel_cpu_probe_map_alias(alias.get(),target,data_address,4096)==SATCHEL_CPU_MAP_OK,"D alias fixture");satchel_cpu_probe_set_x(alias.get(),1,target);satchel_cpu_probe_set_q(alias.get(),31,source.data());expect_stop(alias.get(),SATCHEL_CPU_SVC);
    check(satchel_cpu_probe_unmap_alias(alias.get(),target,data_address,4096)==SATCHEL_CPU_MAP_OK&&satchel_cpu_probe_read(alias.get(),data_address+8,q.data(),8)&&!memcmp(q.data(),source.data(),8),"D store through retained alias survives source restoration");
    auto ro=make(words({d_single_op(0xfc000c00,31,1,8)}));uint64_t token;
    check(satchel_cpu_probe_shared_readonly_create(ro.get(),pages.data(),4096,&token)==SATCHEL_CPU_MAP_OK&&satchel_cpu_probe_shared_readonly_map(ro.get(),token,target,4096)==SATCHEL_CPU_MAP_OK,"D immutable time-page fixture");satchel_cpu_probe_set_x(ro.get(),1,target);satchel_cpu_probe_set_q(ro.get(),31,source.data());expect_stop(ro.get(),SATCHEL_CPU_MEMORY_FAULT);
    check(x(ro.get(),1)==target&&satchel_cpu_probe_read(ro.get(),target,actual.data(),4096)&&!memcmp(actual.data(),pages.data(),4096),"D shared readonly refusal preserves base and real backing");
    for(uint32_t op:{0xfc000800u,0xfc400800u,0xfc200800u,0xfc600800u,0xfc800000u,0xfcc00000u,0x5c000000u}) {
        auto p=make(words({op}));satchel_cpu_probe_set_q(p.get(),0,source.data());auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
        check(stop.reason==SATCHEL_CPU_UNSUPPORTED&&!stop.steps&&satchel_cpu_probe_get_q(p.get(),0,q.data())&&q==source,"D excludes register/literal/unprivileged/reserved forms");
    }
    for(bool load:{false,true}) {
        auto p=make(words({0xc85f7c01,d_single_op(load?0xfc400000:0xfc000000,2,0,16),0xc8037c04,0xd4000001}));satchel_cpu_probe_set_x(p.get(),4,77);satchel_cpu_probe_set_q(p.get(),2,source.data());expect_stop(p.get(),SATCHEL_CPU_SVC);
        check(x(p.get(),3)==(load?0:1),"D ordinary load preserves reservation; successful store invalidates it");
    }
    auto fp=make(words({0x1e610802,d_single_op(0xfc400000,3,0,0),d_single_op(0xfc000000,3,0,16),0xd4000001}));
    const std::array<uint64_t,2> a={UINT64_C(0x7fefffffffffffff),0},b={UINT64_C(0x4000000000000000),0};
    satchel_cpu_probe_set_q(fp.get(),0,reinterpret_cast<const uint8_t*>(a.data()));satchel_cpu_probe_set_q(fp.get(),1,reinterpret_cast<const uint8_t*>(b.data()));expect_stop(fp.get(),SATCHEL_CPU_SVC);
    uint32_t control=99,status=99;check(satchel_cpu_probe_get_fp_status(fp.get(),&control,&status)==SATCHEL_CPU_FP_OK&&status==20,"D loads/stores preserve preexisting true overflow/inexact FPSR");
    auto parent=make(words({d_single_op(0xfc000000,1,0,0),0xd4000001}));auto child=shared(parent.get());satchel_cpu_probe_set_pc(child.get(),code_address);check(satchel_cpu_probe_set_x(child.get(),0,data_address),"D sibling gets explicit guest data argument");satchel_cpu_probe_set_q(parent.get(),1,source.data());std::array<uint8_t,16> other{};other.fill(0x71);satchel_cpu_probe_set_q(child.get(),1,other.data());expect_stop(parent.get(),SATCHEL_CPU_SVC);expect_stop(child.get(),SATCHEL_CPU_SVC);
    check(satchel_cpu_probe_read(parent.get(),data_address,q.data(),8)&&!memcmp(q.data(),other.data(),8)&&satchel_cpu_probe_get_q(parent.get(),1,q.data())&&q==source,"D shared backing and context-local SIMD state remain distinct");
}

static void reprotect_tests() {
    constexpr uint64_t page=4096,target=0x7500000000,heap=0x7400000000;
    std::array<uint8_t,3*page> bytes{},actual{};for(size_t i=0;i<bytes.size();i++)bytes[i]=uint8_t(i*19+7);
    auto p=make(words({0xd503201f,0xd4000001}),bytes.data(),bytes.size());auto sibling=shared(p.get());
    const auto physical=satchel_cpu_probe_test_physical_bytes(p.get());
    check(satchel_cpu_probe_set_permissions(p.get(),data_address+page,page,3,1)==SATCHEL_CPU_MAP_OK,"permission split initial RW data to R");
    check(satchel_cpu_probe_check_range(sibling.get(),data_address,3*page,1) && !satchel_cpu_probe_check_range(sibling.get(),data_address,3*page,3),"permission split visible through sibling with whole backing readable");
    check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size()) && actual==bytes && satchel_cpu_probe_test_physical_bytes(p.get())==physical,"permission split preserves bytes and physical accounting");
    uint64_t value=91;SatchelCPUProbeWrite writes[]={{data_address,&value,8},{data_address+page,&value,8}};
    check(satchel_cpu_probe_writev(sibling.get(),writes,2)==SATCHEL_CPU_WRITE_MEMORY && satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size()) && actual==bytes,"readonly second span prevents every atomic write");
    check(satchel_cpu_probe_set_permissions(p.get(),data_address,3*page,3,0)==SATCHEL_CPU_MAP_INVALID && satchel_cpu_probe_check_range(p.get(),data_address,page,3),"mixed old permissions reject without changing prefix");
    check(satchel_cpu_probe_set_permissions(p.get(),data_address+page,page,1,0)==SATCHEL_CPU_MAP_OK,"read to inaccessible permission change");
    std::array<uint8_t,16> canary;canary.fill(0xcc);auto before=canary;
    check(!satchel_cpu_probe_read(sibling.get(),data_address+page,canary.data(),16) && canary==before && !satchel_cpu_probe_write(p.get(),data_address+page,&value,8),"inaccessible reads and writes preserve destination and backing");
    check(satchel_cpu_probe_set_permissions(sibling.get(),data_address+page,page,0,3)==SATCHEL_CPU_MAP_OK && satchel_cpu_probe_check_range(p.get(),data_address,3*page,3),"restored RW views coalesce across original full allocation");
    check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size()) && actual==bytes && satchel_cpu_probe_test_physical_bytes(p.get())==physical,"restored permissions preserve every byte and physical charge");
    for(unsigned old:{2u,4u,5u,7u,8u,~0u})for(unsigned permission:{0u,1u,3u})check(satchel_cpu_probe_set_permissions(p.get(),data_address,page,old,permission)==SATCHEL_CPU_MAP_INVALID,"invalid expected permission mask rejects");
    for(unsigned permission:{2u,4u,5u,7u,8u,~0u})check(satchel_cpu_probe_set_permissions(p.get(),data_address,page,3,permission)==SATCHEL_CPU_MAP_INVALID,"execute/write-only/unknown new permissions reject");
    for(auto range:std::array<std::array<uint64_t,2>,7>{{{{0,page}},{{data_address+1,page}},{{data_address,0}},{{data_address,page-1}},{{UINT64_MAX-page+1,page}},{{data_address,4*page}},{{target,page}}}})
        check(satchel_cpu_probe_set_permissions(p.get(),range[0],range[1],3,1)==SATCHEL_CPU_MAP_INVALID,"unaligned overflow hole or empty protection range rejects");
    for(unsigned stage:{1u,2u}){satchel_cpu_probe_test_mapping_allocation_failure(stage);check(satchel_cpu_probe_set_permissions(p.get(),data_address+page,page,3,1)==SATCHEL_CPU_MAP_ALLOCATION,"permission allocation failure before whole-view commit");check(satchel_cpu_probe_check_range(p.get(),data_address,3*page,3)&&satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size())&&actual==bytes&&satchel_cpu_probe_test_physical_bytes(p.get())==physical,"permission failure preserves every view byte and unique charge");}
    // Provenance is stronger than current permissions: initial R/RX, write-only,
    // immutable shared pages and alias views never become permission-mutable.
    std::array<uint8_t,page> code{};uint32_t svc=0xd4000001;std::memcpy(code.data(),&svc,4);char error[160];
    SatchelCPUProbeRegion regions[]={{code_address,code.data(),page,page,5},{data_address,bytes.data(),page,page,1},{target,bytes.data(),page,page,2}};
    Probe immutable(satchel_cpu_probe_create(regions,3,code_address,error,sizeof(error)),satchel_cpu_probe_destroy);check(bool(immutable),"immutable provenance fixture");
    check(satchel_cpu_probe_set_permissions(immutable.get(),code_address,page,5,3)==SATCHEL_CPU_MAP_INVALID && satchel_cpu_probe_set_permissions(immutable.get(),data_address,page,1,3)==SATCHEL_CPU_MAP_INVALID && satchel_cpu_probe_set_permissions(immutable.get(),data_address,page,1,1)==SATCHEL_CPU_MAP_INVALID && satchel_cpu_probe_set_permissions(immutable.get(),target,page,2,3)==SATCHEL_CPU_MAP_INVALID,"original R/RX/write-only provenance cannot be widened or bypassed as no-op");
    uint64_t token;check(satchel_cpu_probe_shared_readonly_create(p.get(),bytes.data(),page,&token)==SATCHEL_CPU_MAP_OK&&satchel_cpu_probe_shared_readonly_map(p.get(),token,target,page)==SATCHEL_CPU_MAP_OK,"protection immutable time-page fixture");
    check(satchel_cpu_probe_set_permissions(p.get(),target,page,1,0)==SATCHEL_CPU_MAP_INVALID&&satchel_cpu_probe_set_permissions(p.get(),target,page,1,3)==SATCHEL_CPU_MAP_INVALID,"immutable shared object rejects permission alterations");
    check(satchel_cpu_probe_shared_readonly_unmap(p.get(),token,target,page)==SATCHEL_CPU_MAP_OK&&satchel_cpu_probe_shared_readonly_release(p.get(),token)==SATCHEL_CPU_MAP_OK,"release shared fixture");
    check(satchel_cpu_probe_map_alias(p.get(),target,data_address,page)==SATCHEL_CPU_MAP_OK,"protection retained alias fixture");
    check(satchel_cpu_probe_set_permissions(p.get(),data_address,page,0,3)==SATCHEL_CPU_MAP_INVALID&&satchel_cpu_probe_set_permissions(p.get(),target,page,3,1)==SATCHEL_CPU_MAP_INVALID,"locked source and alias destination protection reject");
    check(satchel_cpu_probe_set_permissions(p.get(),data_address+page,page,3,1)==SATCHEL_CPU_MAP_OK&&satchel_cpu_probe_set_permissions(p.get(),data_address+page,page,1,3)==SATCHEL_CPU_MAP_OK,"unlocked disjoint bytes of aliased allocation retain independent permission eligibility");
    check(satchel_cpu_probe_unmap_alias(p.get(),target,data_address,page)==SATCHEL_CPU_MAP_OK,"restore alias after disjoint protection");
    // Dynamic allocation APIs cannot replace a permission fragment and charge
    // it as though it owned the whole retained allocation.
    check(satchel_cpu_probe_resize_rw(p.get(),heap,3*page)==SATCHEL_CPU_MAP_OK&&satchel_cpu_probe_write(p.get(),heap,bytes.data(),bytes.size()),"dynamic permission fixture");
    check(satchel_cpu_probe_set_permissions(p.get(),heap+page,page,3,1)==SATCHEL_CPU_MAP_OK,"dynamic partial protection");
    auto dynamic_physical=satchel_cpu_probe_test_physical_bytes(p.get());uint64_t changed=99;
    check(satchel_cpu_probe_resize_rw(p.get(),heap,4*page)==SATCHEL_CPU_MAP_INVALID&&satchel_cpu_probe_resize_rw(p.get(),heap,0)==SATCHEL_CPU_MAP_INVALID&&satchel_cpu_probe_map_missing_rw(p.get(),heap,page,&changed)==SATCHEL_CPU_MAP_INVALID&&changed==99&&satchel_cpu_probe_unmap_rw(p.get(),heap,page,&changed)==SATCHEL_CPU_MAP_INVALID&&changed==99,"fragmented allocation reshape rejects atomically");
    check(satchel_cpu_probe_test_physical_bytes(p.get())==dynamic_physical&&satchel_cpu_probe_read(p.get(),heap,actual.data(),actual.size())&&actual==bytes,"rejected fragment reshapes preserve complete backing");
    check(satchel_cpu_probe_set_permissions(p.get(),heap+page,page,1,3)==SATCHEL_CPU_MAP_OK&&satchel_cpu_probe_resize_rw(p.get(),heap,4*page)==SATCHEL_CPU_MAP_OK,"restoring/coalescing whole RW permits normal heap growth");
    check(satchel_cpu_probe_read(p.get(),heap,actual.data(),actual.size())&&actual==bytes,"growth after reprotection preserves prefix");
    std::array<uint8_t,page> zero{};actual.fill(9);check(satchel_cpu_probe_read(p.get(),heap+3*page,actual.data(),page)&&!std::memcmp(actual.data(),zero.data(),page),"new page zero after restored growth");
    // Initial adjacent independent allocations are allowed but never coalesced
    // physically; all-view preflight commits both permission spans atomically.
    SatchelCPUProbeRegion adjacent[]={{code_address,code.data(),page,page,5},{data_address,bytes.data(),page,page,3},{data_address+page,bytes.data()+page,page,page,3}};
    Probe separate(satchel_cpu_probe_create(adjacent,3,code_address,error,sizeof(error)),satchel_cpu_probe_destroy);check(bool(separate),"adjacent distinct backing permission fixture");
    check(satchel_cpu_probe_set_permissions(separate.get(),data_address,2*page,3,1)==SATCHEL_CPU_MAP_OK&&satchel_cpu_probe_check_range(separate.get(),data_address,page,1)&&satchel_cpu_probe_check_range(separate.get(),data_address+page,page,1)&&!satchel_cpu_probe_check_range(separate.get(),data_address,2*page,1),"permission change across backings preserves distinct physical identity and cross-backing guard");
    // Original shape matches the observed public request extent:76 of95 pages,
    // without storing any imported payload or runtime register contents.
    std::vector<uint8_t> large(95*page,0x7a),observed(large.size());auto prefix=make(words({svc}),large.data(),large.size());auto prefix_charge=satchel_cpu_probe_test_physical_bytes(prefix.get());
    check(satchel_cpu_probe_set_permissions(prefix.get(),data_address,76*page,3,1)==SATCHEL_CPU_MAP_OK&&satchel_cpu_probe_check_range(prefix.get(),data_address,76*page,1)&&!satchel_cpu_probe_check_range(prefix.get(),data_address,76*page,3)&&satchel_cpu_probe_check_range(prefix.get(),data_address+76*page,19*page,3),"original76-of95 page partial-prefix reprotection");
    check(satchel_cpu_probe_read(prefix.get(),data_address,observed.data(),observed.size())&&observed==large&&satchel_cpu_probe_test_physical_bytes(prefix.get())==prefix_charge,"large prefix permission split preserves exact physical backing and every byte");
    auto pair=make(words({pair_op(0xad000000,1,2,0,0)}),bytes.data(),bytes.size());satchel_cpu_probe_set_x(pair.get(),0,data_address+page-16);
    check(satchel_cpu_probe_set_permissions(pair.get(),data_address+page,page,3,1)==SATCHEL_CPU_MAP_OK,"SIMD pair crosses permission boundary fixture");expect_stop(pair.get(),SATCHEL_CPU_MEMORY_FAULT);
    check(satchel_cpu_probe_read(pair.get(),data_address,actual.data(),actual.size())&&actual==bytes,"Q pair full32-byte permission preflight prevents writable first-half partial store");
    // Actual interpreted reads may cross different permission views when all
    // views still grant READ and retain contiguous bytes of one allocation.
    for(bool vector_pair:{false,true}) {
        auto load=make(words({vector_pair?pair_op(0xad400000,1,2,0,0):0xf9400001u,svc}),bytes.data(),bytes.size());
        const size_t offset=page-(vector_pair?16:4);
        satchel_cpu_probe_set_x(load.get(),0,data_address+offset);
        check(satchel_cpu_probe_set_permissions(load.get(),data_address+page,page,3,1)==SATCHEL_CPU_MAP_OK,"guest load crosses RW/R view boundary fixture");
        expect_stop(load.get(),SATCHEL_CPU_SVC);
        if(vector_pair) {
            std::array<uint8_t,32> loaded{};
            check(satchel_cpu_probe_get_q(load.get(),1,loaded.data())&&satchel_cpu_probe_get_q(load.get(),2,loaded.data()+16)&&!std::memcmp(loaded.data(),bytes.data()+offset,loaded.size()),"interpreted LDP Q reads full32 bytes across contiguous RW/R views");
        } else {
            uint64_t expected;std::memcpy(&expected,bytes.data()+offset,sizeof(expected));
            check(x(load.get(),1)==expected,"interpreted LDR reads8 bytes across contiguous RW/R views");
        }
        check(satchel_cpu_probe_read(load.get(),data_address,actual.data(),actual.size())&&actual==bytes,"cross-permission guest reads preserve entire backing");
    }
    // Graphics guards compare retained physical offsets even when a complete
    // span crosses RW/R/NONE views and a disjoint page is alias-locked.
    std::array<uint8_t,4*page> physical_data{};auto overlap_probe=make(words({svc}),physical_data.data(),physical_data.size());
    check(satchel_cpu_probe_set_permissions(overlap_probe.get(),data_address+page,page,3,1)==SATCHEL_CPU_MAP_OK&&satchel_cpu_probe_set_permissions(overlap_probe.get(),data_address+2*page,page,3,0)==SATCHEL_CPU_MAP_OK&&satchel_cpu_probe_map_alias(overlap_probe.get(),target,data_address+3*page,page)==SATCHEL_CPU_MAP_OK,"physical guard mixed-permission and locked-alias fixture");
    for(unsigned restored=0;restored<2;restored++){
        int overlap=99;check(satchel_cpu_probe_physical_overlap(overlap_probe.get(),data_address,3*page,data_address+page-8,32,&overlap)&&overlap==1,"physical overlap crosses RW/R/NONE views independently of permissions");
        overlap=99;check(satchel_cpu_probe_physical_overlap(overlap_probe.get(),data_address,3*page,data_address+3*page,page,&overlap)&&overlap==0,"disjoint retained offsets remain disjoint through permission fragments");
        overlap=99;check(satchel_cpu_probe_physical_overlap(overlap_probe.get(),data_address,4*page,data_address+3*page,page,&overlap)&&overlap==1,"whole backing span includes locked or restored last page");
        overlap=99;check(!satchel_cpu_probe_physical_overlap(overlap_probe.get(),data_address,5*page,data_address,page,&overlap)&&overlap==99,"physical guard hole fails with unchanged output across permission states");
        if(!restored){overlap=99;check(satchel_cpu_probe_physical_overlap(overlap_probe.get(),data_address+3*page,page,target,page,&overlap)&&overlap==1,"locked source and readable alias retain exact physical correspondence");
            overlap=99;check(satchel_cpu_probe_physical_overlap(overlap_probe.get(),data_address,3*page,target,page,&overlap)&&overlap==0,"mixed permission span stays physically disjoint from other-page alias");
            check(satchel_cpu_probe_set_permissions(overlap_probe.get(),data_address+page,page,1,3)==SATCHEL_CPU_MAP_OK&&satchel_cpu_probe_set_permissions(overlap_probe.get(),data_address+2*page,page,0,3)==SATCHEL_CPU_MAP_OK&&satchel_cpu_probe_unmap_alias(overlap_probe.get(),target,data_address+3*page,page)==SATCHEL_CPU_MAP_OK&&satchel_cpu_probe_check_range(overlap_probe.get(),data_address,4*page,3),"physical guards preserve answers after restore and full coalesce");}
    }
    // Max final views, not transient staging descriptors, govern admission.
    auto full=make(words({svc}),bytes.data(),bytes.size());for(unsigned i=0;i<29;i++)check(satchel_cpu_probe_resize_rw(full.get(),heap+i*2*page,page)==SATCHEL_CPU_MAP_OK,"fill bounded map table");
    auto full_bytes=satchel_cpu_probe_test_physical_bytes(full.get());check(satchel_cpu_probe_set_permissions(full.get(),data_address+page,page,3,1)==SATCHEL_CPU_MAP_LIMIT&&satchel_cpu_probe_check_range(full.get(),data_address,3*page,3)&&satchel_cpu_probe_test_physical_bytes(full.get())==full_bytes,"view-limit split failure preserves map and physical charge");
    check(satchel_cpu_probe_set_permissions(full.get(),data_address,3*page,3,1)==SATCHEL_CPU_MAP_OK&&satchel_cpu_probe_set_permissions(full.get(),data_address,3*page,1,3)==SATCHEL_CPU_MAP_OK,"whole-view permission change fits full map table");
    // Monitor behavior is observable through real guest exclusive instructions.
    for(unsigned mode=0;mode<4;mode++){auto e=make(words({0xc85f7c01,0xc8027c03,svc}),bytes.data(),bytes.size());satchel_cpu_probe_set_x(e.get(),3,77);check(satchel_cpu_probe_run(e.get(),1,nullptr,nullptr).steps==1,"permission exclusive setup");
        if(mode==0)check(satchel_cpu_probe_set_permissions(e.get(),data_address,page,3,3)==SATCHEL_CPU_MAP_OK,"validated same-permission no-op");
        if(mode==1)check(satchel_cpu_probe_set_permissions(e.get(),data_address+page,page,3,1)==SATCHEL_CPU_MAP_OK,"actual disjoint permission commit clears reservations");
        if(mode==2)check(satchel_cpu_probe_set_permissions(e.get(),data_address,page,1,0)==SATCHEL_CPU_MAP_INVALID,"failed expected permission retains reservation");
        if(mode==3){satchel_cpu_probe_test_mapping_allocation_failure(2);check(satchel_cpu_probe_set_permissions(e.get(),data_address+page,page,3,1)==SATCHEL_CPU_MAP_ALLOCATION,"failed permission allocation retains reservation");}
        expect_stop(e.get(),SATCHEL_CPU_SVC);check(x(e.get(),2)==(mode==1?1u:0u),"only actual permission commit invalidates live guest reservation");}
    auto store=make(words({0xf9000001}),bytes.data(),bytes.size());satchel_cpu_probe_set_x(store.get(),1,88);check(satchel_cpu_probe_set_permissions(store.get(),data_address,page,3,1)==SATCHEL_CPU_MAP_OK,"readonly ordinary guest store fixture");expect_stop(store.get(),SATCHEL_CPU_MEMORY_FAULT);check(satchel_cpu_probe_read(store.get(),data_address,actual.data(),actual.size())&&actual==bytes,"guest STR respects reprotected read-only view");
    struct Busy{SatchelCPUProbe *a,*b;bool okay=false;} busy{p.get(),sibling.get()};auto stop=satchel_cpu_probe_run(p.get(),1,[](void *opaque){auto &b=*static_cast<Busy*>(opaque);b.okay=satchel_cpu_probe_set_permissions(b.a,data_address,page,3,1)==SATCHEL_CPU_MAP_BUSY&&satchel_cpu_probe_set_permissions(b.b,data_address,page,3,1)==SATCHEL_CPU_MAP_BUSY;std::thread worker([&b]{b.okay=b.okay&&satchel_cpu_probe_set_permissions(b.b,data_address,page,3,1)==SATCHEL_CPU_MAP_BUSY;});worker.join();return 1;},&busy);
    check(stop.reason==SATCHEL_CPU_CANCELLED&&busy.okay&&satchel_cpu_probe_check_range(p.get(),data_address,3*page,3),"active/sibling/cross-host permission mutation rejects without effects");
    p.reset();check(satchel_cpu_probe_set_permissions(sibling.get(),data_address,page,3,0)==SATCHEL_CPU_MAP_OK&&satchel_cpu_probe_set_permissions(sibling.get(),data_address,page,0,3)==SATCHEL_CPU_MAP_OK,"permission backing lifetime survives original context teardown");
}

static void mov_integer_tests(const char *fixtures) {
    // The fixed native program exhausts all256 immediates in each of20
    // arrangements, using actual hardware rather than the expansion oracle.
    std::vector<uint8_t> bytes(20*256*16+80+32,0xa7),actual(bytes.size());
    auto native=make(fixture(fixtures,"movinteger"),bytes.data(),bytes.size());
    satchel_cpu_probe_set_x(native.get(),1,0x123456789abcdef0);
    check(satchel_cpu_probe_test_set_known_fpcr(native.get(),0x02000000),"MOVI owned DN control fixture");
    fenv_t host_before{},host_after{};check(fegetenv(&host_before)==0,"MOVI host FP state before");
    const uint64_t result=probe_movinteger(bytes.data());
    auto run=satchel_cpu_probe_run(native.get(),20000,nullptr,nullptr);
    check(run.reason==SATCHEL_CPU_SVC && result==1 && x(native.get(),0)==result,"native all integer MOVI immediates and NZCV unchanged");
    check(satchel_cpu_probe_read(native.get(),data_address,actual.data(),actual.size())&&actual==bytes,"native MOVI every lane, upper64 clear, and trailing canaries");
    check(x(native.get(),1)==0x123456789abcdef0 && satchel_cpu_probe_test_stack_pointer(native.get())==stack_address+128,"MOVI does not modify unrelated GPR or SP");
    uint32_t control=99,status=99;check(satchel_cpu_probe_get_fp_status(native.get(),&control,&status)==SATCHEL_CPU_FP_OK && control==0x02000000 && status==0,"MOVI preserves owned FPCR/FPSR");
    check(fegetenv(&host_after)==0 && host_after.__fpcr==host_before.__fpcr && host_after.__fpsr==host_before.__fpsr,"native and interpreted integer MOVI preserve host FP state");
    std::array<uint8_t,16> before{},other{},observed{};before.fill(0xc3);other.fill(0x5a);
    // Classify every cmode/op/Q, including excluded ORR/BIC/MVNI/FMOV. Exact
    // byte expectations independently cover each lane and shifted-one region.
    for(unsigned q=0;q<2;q++)for(unsigned opbit=0;opbit<2;opbit++)for(unsigned cmode=0;cmode<16;cmode++)
    for(unsigned imm:{0u,1u,0x55u,0x80u,0xaau,0xffu})for(unsigned rd:{0u,31u}) {
        const uint32_t op=0x0f000400|(q<<30)|(opbit<<29)|(cmode<<12)|((imm&224)<<11)|((imm&31)<<5)|rd;
        const bool valid=opbit?cmode==14:(cmode>=12&&cmode<=14)||(cmode<12&&!(cmode&1));
        auto p=make(words({op}));satchel_cpu_probe_set_q(p.get(),rd,before.data());satchel_cpu_probe_set_q(p.get(),1,other.data());satchel_cpu_probe_set_x(p.get(),1,0xa55a);
        const auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
        check(stop.reason==(valid?SATCHEL_CPU_BUDGET:SATCHEL_CPU_UNSUPPORTED)&&stop.steps==(valid?1u:0u),"MOVI exact integer profile versus excluded modified immediates");
        std::array<uint8_t,16> expected=before;
        if(valid){expected.fill(0);unsigned lane_bytes=cmode<8||cmode==12||cmode==13?4:cmode<12?2:opbit?8:1;
            for(unsigned i=0;i<(q?16u:8u);i++){unsigned byte=i%lane_bytes;
                if(cmode==14)expected[i]=opbit?((imm&(1u<<byte))?255:0):imm;
                else if(cmode<12)expected[i]=byte==(cmode<8?cmode/2:(cmode-8)/2)?imm:0;
                else {unsigned shift=cmode==12?1:2;expected[i]=byte<shift?255:byte==shift?imm:0;}
            }
        }
        check(satchel_cpu_probe_get_q(p.get(),rd,observed.data())&&observed==expected,"MOVI exact bytes or fault-atomic destination");
        check(satchel_cpu_probe_get_q(p.get(),1,observed.data())&&observed==other&&x(p.get(),1)==0xa55a,"MOVI leaves unrelated SIMD and integer banks untouched");
        std::array<uint8_t,64> memory{};check(satchel_cpu_probe_read(p.get(),data_address,memory.data(),memory.size())&&std::all_of(memory.begin(),memory.end(),[](uint8_t b){return b==0;}),"MOVI has no guest data-memory effects");
    }
    for(uint32_t op:{0x6f00ec00u,0xef00e400u,0x6f08e400u,0x6f00e000u,0x6f00f400u,0x2f00f400u,0x4f00e400u|0x80000u}) {
        auto p=make(words({op}));satchel_cpu_probe_set_q(p.get(),0,before.data());auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
        check(stop.reason==SATCHEL_CPU_UNSUPPORTED && !stop.steps,"MOVI excludes FP16/FM0V and reserved fixed fields");
        check(satchel_cpu_probe_get_q(p.get(),0,observed.data())&&observed==before,"excluded MOVI shape leaves destination unchanged");
    }
    // Pure register immediate moves must not interfere with a live reservation.
    auto exclusive=make(words({0xc85f7c01,0x6f00e41f,0xc8027c03,0xd4000001}));satchel_cpu_probe_set_x(exclusive.get(),3,77);expect_stop(exclusive.get(),SATCHEL_CPU_SVC);
    check(x(exclusive.get(),2)==0,"MOVI preserves current exclusive reservation");
    std::array<uint64_t,2> a={UINT64_C(0x7fefffffffffffff),0},b={UINT64_C(0x4000000000000000),0};
    auto fp=make(words({0x1e610802,0x6f00e41f,0xd4000001}));satchel_cpu_probe_set_q(fp.get(),0,reinterpret_cast<uint8_t*>(a.data()));satchel_cpu_probe_set_q(fp.get(),1,reinterpret_cast<uint8_t*>(b.data()));expect_stop(fp.get(),SATCHEL_CPU_SVC);
    control=status=99;check(satchel_cpu_probe_get_fp_status(fp.get(),&control,&status)==SATCHEL_CPU_FP_OK && status==20,"MOVI preserves actual overflow/inexact cumulative status");
    auto parent=make(words({0x6f07e7e0,0x2f00e400}));auto child=shared(parent.get());
    check(satchel_cpu_probe_set_pc(child.get(),code_address+4),"MOVI sibling starts at distinct actual instruction");
    check(satchel_cpu_probe_run(parent.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET && satchel_cpu_probe_run(child.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET,"MOVI contexts execute independently");
    check(satchel_cpu_probe_get_q(parent.get(),0,observed.data())&&std::all_of(observed.begin(),observed.end(),[](uint8_t b){return b==255;}),"MOVI parent vector survives sibling run");
    check(satchel_cpu_probe_get_q(child.get(),0,observed.data())&&std::all_of(observed.begin(),observed.end(),[](uint8_t b){return b==0;}),"MOVI child scalar clears its own upper64 only");
}

static void dup_general_tests(const char *fixtures) {
    const uint64_t inputs[]={0,UINT64_MAX,UINT64_C(0x8000000000000000),UINT64_C(0x0123456789abcdef),
        UINT64_C(0xff007f0080000100),UINT64_C(0x7ff0000000000001),UINT64_C(0x0000000100000000),UINT64_C(0xa55aa55aa55aa55a)};
    for(uint64_t value:inputs){
        std::array<uint8_t,192> bytes{},actual{};bytes.fill(0xa7);
        auto p=make(fixture(fixtures,"dupgeneral"),bytes.data(),bytes.size());
        satchel_cpu_probe_set_x(p.get(),1,value);
        uint32_t before_control,before_status,after_control,after_status;
        check(satchel_cpu_probe_get_fp_status(p.get(),&before_control,&before_status)==SATCHEL_CPU_FP_OK,"DUP native fixture owned FP status before");
        const uint64_t result=probe_dupgeneral(bytes.data(),value);expect_stop(p.get(),SATCHEL_CPU_SVC);
        check(result==1 && x(p.get(),0)==result,"native general DUP arrangements, ignored bits, ZR and NZCV");
        check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size()) && actual==bytes,"native DUP all128 destination bits including 64-bit high zero and canaries");
        check(x(p.get(),1)==value && satchel_cpu_probe_test_stack_pointer(p.get())==stack_address+128,"DUP GPR source and SP unchanged");
        check(satchel_cpu_probe_get_fp_status(p.get(),&after_control,&after_status)==SATCHEL_CPU_FP_OK && before_control==after_control && before_status==after_status,"DUP does not interpret FP payloads or change cumulative status");
    }
    std::array<uint8_t,16> old{},other{},actual{};old.fill(0xc3);other.fill(0x5a);
    // Exhaust every imm5, Q and the real Q31 versus general ZR distinction.
    for(unsigned q=0;q<2;q++)for(unsigned imm=0;imm<32;imm++)for(unsigned rn:{1u,31u})for(unsigned rd:{1u,31u}){
        const uint32_t op=0x0e000c00|(q<<30)|(imm<<16)|(rn<<5)|rd;
        auto p=make(words({op}));satchel_cpu_probe_set_x(p.get(),1,inputs[3]);satchel_cpu_probe_set_q(p.get(),rd,old.data());satchel_cpu_probe_set_q(p.get(),2,other.data());
        const bool valid=(imm&15) && ((imm&15)!=8 || q);
        const auto result=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
        check(result.reason==(valid?SATCHEL_CPU_BUDGET:SATCHEL_CPU_UNSUPPORTED) && result.steps==(valid?1u:0u),"DUP every imm5/Q allocated versus unallocated classification");
        std::array<uint8_t,16> expected=valid?std::array<uint8_t,16>{}:old;
        if(valid){unsigned width=1;while(!(imm&width))width<<=1;const uint64_t value=rn==31?0:inputs[3];
            for(unsigned lane=0;lane<(q?16u:8u);lane++)expected[lane]=uint8_t(value>>(8*(lane%width)));}
        check(satchel_cpu_probe_get_q(p.get(),rd,actual.data()) && actual==expected,"DUP checked byte replication or unchanged fault destination");
        check(satchel_cpu_probe_get_q(p.get(),2,actual.data()) && actual==other,"DUP leaves unrelated Q register untouched");
        check(x(p.get(),1)==inputs[3] && satchel_cpu_probe_test_stack_pointer(p.get())==stack_address+128,"DUP bank alias and ZR never modify GPR/SP");
        uint32_t control=99,status=99;const auto fp_result=satchel_cpu_probe_get_fp_status(p.get(),&control,&status);
        check(valid?(fp_result==SATCHEL_CPU_FP_OK && control==0 && status==0):(fp_result==SATCHEL_CPU_FP_INVALID && control==99 && status==99),"DUP preserves known FP status; terminal faults reject the status query atomically");
    }
    element_narrow_positive(0x4e010420); // Exact old element-DUP exclusion now checks complete real result.
    // Remaining scalar/insert/move copy families remain excluded.
    for(uint32_t op:{0x5e010420u,0x4e011c20u,0x6e010420u,0x0e013c20u,0x0e012c20u,0xce010c20u,0x6e010c20u}){
        auto p=make(words({op}));satchel_cpu_probe_set_q(p.get(),0,old.data());auto result=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
        check(result.reason==SATCHEL_CPU_UNSUPPORTED && !result.steps,"DUP excludes element/scalar/insert/move/reserved forms");
        check(satchel_cpu_probe_get_q(p.get(),0,actual.data()) && actual==old,"excluded copy preserves Q destination");
    }
    for(unsigned bit=10;bit<32;bit++)if(bit<16 || bit>20){
        auto p=make(words({0x4e010c20u^(1u<<bit)}));const auto result=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);
        check(result.steps<=1 && result.reason!=SATCHEL_CPU_INTERNAL_FAULT,"DUP fixed-field mutations never reach unsupported upstream helper");
    }
    auto exclusive=make(words({0xc85f7c01,0x4e010c20,0xc8027c03,0xd4000001}));satchel_cpu_probe_set_x(exclusive.get(),3,77);expect_stop(exclusive.get(),SATCHEL_CPU_SVC);
    check(x(exclusive.get(),2)==0,"register-only DUP preserves current exclusive reservation");
    std::array<uint64_t,2> a={UINT64_C(0x7fefffffffffffff),0},b={UINT64_C(0x4000000000000000),0};
    auto fp=make(words({0x1e610802,0x4e010c20,0xd4000001}));satchel_cpu_probe_set_q(fp.get(),0,reinterpret_cast<uint8_t*>(a.data()));satchel_cpu_probe_set_q(fp.get(),1,reinterpret_cast<uint8_t*>(b.data()));expect_stop(fp.get(),SATCHEL_CPU_SVC);
    uint32_t control=99,status=99;check(satchel_cpu_probe_get_fp_status(fp.get(),&control,&status)==SATCHEL_CPU_FP_OK && status==20,"DUP preserves actual preexisting overflow/inexact cumulative status");
    auto parent=make(words({0x4e010c20}));auto child=shared(parent.get());satchel_cpu_probe_set_x(parent.get(),1,0x11);satchel_cpu_probe_set_x(child.get(),1,0x22);
    check(satchel_cpu_probe_set_pc(child.get(),code_address),"DUP sibling starts at actual code entry");
    check(satchel_cpu_probe_run(parent.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET && satchel_cpu_probe_run(child.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET,"DUP independent shared-memory contexts execute");
    check(satchel_cpu_probe_get_q(parent.get(),0,actual.data()) && std::all_of(actual.begin(),actual.end(),[](uint8_t v){return v==0x11;}),"DUP parent Q remains local after sibling run");
    check(satchel_cpu_probe_get_q(child.get(),0,actual.data()) && std::all_of(actual.begin(),actual.end(),[](uint8_t v){return v==0x22;}),"DUP child Q and GPR state is independent");
}
static void simd_pair_tests(const char *fixtures) {
    std::array<uint8_t,4096> bytes{}, actual{};
    for(unsigned seed=0;seed<8;seed++) {
        for(size_t i=0;i<bytes.size();i++)bytes[i]=uint8_t(i*37+seed*91);
        auto p=make(fixture(fixtures,"qpairs"),bytes.data(),bytes.size());
        uint32_t before_control,before_status,after_control,after_status;
        check(satchel_cpu_probe_get_fp_status(p.get(),&before_control,&before_status)==SATCHEL_CPU_FP_OK,"Q pair FP status before native fixture");
        const uint64_t expected=probe_qpairs(bytes.data());expect_stop(p.get(),SATCHEL_CPU_SVC);
        check(expected==96 && x(p.get(),0)==expected,"native Q pair offset/pre/post/zero-writeback and separate register-file result");
        check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size()) && actual==bytes,"native Q pair all128-bit lane bytes and signed offset endpoints");
        check(satchel_cpu_probe_test_stack_pointer(p.get())==stack_address+128,"native stack Q pair restores SP");
        check(satchel_cpu_probe_get_fp_status(p.get(),&after_control,&after_status)==SATCHEL_CPU_FP_OK && before_control==after_control && before_status==after_status,"Q bit transfers preserve FPCR/FPSR");
    }
    std::array<uint8_t,16> first{},second{},q1{},q2{};for(unsigned i=0;i<16;i++){first[i]=uint8_t(11*i+7);second[i]=uint8_t(255-13*i);}
    // Every addressing form and signed immediate endpoint uses32-byte preflight.
    for(uint32_t form:{0xad000000u,0xad400000u,0xad800000u,0xadc00000u,0xac800000u,0xacc00000u}) {
        const unsigned mode=(form>>23)&3;const bool load=form&0x00400000;
        for(int offset:{-1024,-16,0,16,1008}) {
            for(size_t i=0;i<bytes.size();i++)bytes[i]=uint8_t(i*7+19);
            auto p=make(words({pair_op(form,1,31,1,offset),0xd4000001}),bytes.data(),bytes.size());
            const uint64_t base=data_address+1536,address=mode==1?base:base+offset;
            check(satchel_cpu_probe_set_x(p.get(),1,base) && satchel_cpu_probe_set_q(p.get(),1,first.data()) && satchel_cpu_probe_set_q(p.get(),31,second.data()),"Q register numbers may match writeback X register");
            expect_stop(p.get(),SATCHEL_CPU_SVC);
            check(x(p.get(),1)==(mode==2?base:base+offset),"Q pair exact base writeback");
            check(satchel_cpu_probe_get_q(p.get(),1,q1.data()) && satchel_cpu_probe_get_q(p.get(),31,q2.data()),"Q31 is real vector register");
            if(load)check(!memcmp(q1.data(),bytes.data()+address-data_address,16) && !memcmp(q2.data(),bytes.data()+address-data_address+16,16),"pair loads all16 bytes per register");
            else {memcpy(bytes.data()+address-data_address,first.data(),16);memcpy(bytes.data()+address-data_address+16,second.data(),16);check(q1==first && q2==second,"pair stores preserve SIMD registers");}
            check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size()) && actual==bytes,"Q pair exact whole-memory effects");
        }
        // A second-half fault cannot update first memory/register or the base.
        for(unsigned rn:{0u,31u}) {
            bytes.fill(0xa5);const int offset=mode==3?-32:mode==1?32:0;
            const uint64_t target=data_address+4096-16,base=mode==3?target+32:target;
            auto p=make(words({pair_op(form,1,31,rn,offset)}),bytes.data(),bytes.size());
            if(rn==31)satchel_cpu_probe_set_sp(p.get(),base);else satchel_cpu_probe_set_x(p.get(),rn,base);
            satchel_cpu_probe_set_q(p.get(),1,first.data());satchel_cpu_probe_set_q(p.get(),31,second.data());
            auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(stop.reason==SATCHEL_CPU_MEMORY_FAULT && stop.steps==0,"Q pair second-half bounds fault before effects");
            check((rn==31?satchel_cpu_probe_test_stack_pointer(p.get()):x(p.get(),rn))==base,"Q pair fault preserves GPR/SP writeback");
            check(satchel_cpu_probe_get_q(p.get(),1,q1.data()) && satchel_cpu_probe_get_q(p.get(),31,q2.data()) && q1==first && q2==second,"Q pair fault preserves both destinations");
            check(satchel_cpu_probe_read(p.get(),data_address,actual.data(),actual.size()) && actual==bytes,"Q pair fault preserves entire backing");
        }
        for(uint64_t base:{uint64_t{0},UINT64_MAX-15}) {
            auto p=make(words({pair_op(form,1,2,0,0)}));satchel_cpu_probe_set_x(p.get(),0,base);auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(stop.reason==SATCHEL_CPU_MEMORY_FAULT && !stop.steps && x(p.get(),0)==base,"Q pair address overflow/unmapped fault");
        }
        auto sp=make(words({pair_op(form,1,2,31,0)}));satchel_cpu_probe_set_sp(sp.get(),stack_address+8);expect_stop(sp.get(),SATCHEL_CPU_MEMORY_FAULT);check(satchel_cpu_probe_test_stack_pointer(sp.get())==stack_address+8,"Q pair stack alignment checked before writeback");
        // Permissions are checked for the WHOLE pair, across every access.
        auto code=words({pair_op(form,1,2,0,0)});std::array<uint8_t,32> memory{};memory.fill(0x3c);char error[160];
        SatchelCPUProbeRegion parts[]={{code_address,code.data(),4,4,5},{data_address,memory.data(),16,16,3},{data_address+16,memory.data()+16,16,16,load?2u:1u}};
        Probe p(satchel_cpu_probe_create(parts,3,code_address,error,sizeof(error)),satchel_cpu_probe_destroy);check(bool(p),"Q pair mixed permission mappings");satchel_cpu_probe_set_x(p.get(),0,data_address);satchel_cpu_probe_set_q(p.get(),1,first.data());satchel_cpu_probe_set_q(p.get(),2,second.data());expect_stop(p.get(),SATCHEL_CPU_MEMORY_FAULT);
        std::array<uint8_t,16> initial{};initial.fill(0x3c);check(satchel_cpu_probe_read(p.get(),data_address,q1.data(),16)&&q1==initial,"protected second half leaves first store unchanged");check(satchel_cpu_probe_get_q(p.get(),1,q1.data())&&q1==first,"protected second half leaves first load unchanged");
    }
    for(uint32_t form:{0xad400000u,0xadc00000u,0xacc00000u}) {
        auto p=make(words({pair_op(form,31,31,0,16)}));expect_stop(p.get(),SATCHEL_CPU_UNSUPPORTED);check(x(p.get(),0)==data_address,"duplicate Q load rejected before base mutation");
    }
    for(uint32_t form:{0xad000000u,0xad800000u,0xac800000u}) {
        auto p=make(words({pair_op(form,31,31,0,0),0xd4000001}));satchel_cpu_probe_set_q(p.get(),31,first.data());expect_stop(p.get(),SATCHEL_CPU_SVC);std::array<uint8_t,32> observed{};check(satchel_cpu_probe_read(p.get(),data_address,observed.data(),32)&&!memcmp(observed.data(),first.data(),16)&&!memcmp(observed.data()+16,first.data(),16),"duplicate Q store sources are valid");
    }
    for(uint32_t other:{0x2d000000u,0x6d000000u,0xed000000u,0xac000000u,0xac400000u,0x2d800000u,0x6cc00000u}) {
        auto p=make(words({other|1u|(2u<<10)}));expect_stop(p.get(),SATCHEL_CPU_UNSUPPORTED);
    }
    // Crossing a virtual page within one genuine allocation remains valid.
    std::array<uint8_t,8192> pages{};auto cross=make(words({pair_op(0xad000000,1,2,0,0),pair_op(0xad400000,3,4,0,0),0xd4000001}),pages.data(),pages.size());satchel_cpu_probe_set_x(cross.get(),0,data_address+4096-16);satchel_cpu_probe_set_q(cross.get(),1,first.data());satchel_cpu_probe_set_q(cross.get(),2,second.data());expect_stop(cross.get(),SATCHEL_CPU_SVC);check(satchel_cpu_probe_get_q(cross.get(),3,q1.data())&&satchel_cpu_probe_get_q(cross.get(),4,q2.data())&&q1==first&&q2==second,"Q pair crosses real mapped page boundary with all bits");
    // Retained aliases use the same bytes, while locked sources remain denied.
    auto alias=make(words({pair_op(0xad000000,1,2,0,0),0xd4000001}),pages.data(),pages.size());const uint64_t target=0x800000;check(satchel_cpu_probe_map_alias(alias.get(),target,data_address,4096)==SATCHEL_CPU_MAP_OK,"Q pair alias fixture");satchel_cpu_probe_set_x(alias.get(),0,target+32);satchel_cpu_probe_set_q(alias.get(),1,first.data());satchel_cpu_probe_set_q(alias.get(),2,second.data());expect_stop(alias.get(),SATCHEL_CPU_SVC);check(satchel_cpu_probe_unmap_alias(alias.get(),target,data_address,4096)==SATCHEL_CPU_MAP_OK && satchel_cpu_probe_read(alias.get(),data_address+32,q1.data(),16)&&q1==first,"Q pair alias stores survive source restoration");
    auto ro=make(words({pair_op(0xad000000,1,2,0,0)}));uint64_t token;check(satchel_cpu_probe_shared_readonly_create(ro.get(),bytes.data(),4096,&token)==SATCHEL_CPU_MAP_OK && satchel_cpu_probe_shared_readonly_map(ro.get(),token,target,4096)==SATCHEL_CPU_MAP_OK,"Q pair readonly shared fixture");satchel_cpu_probe_set_x(ro.get(),0,target);expect_stop(ro.get(),SATCHEL_CPU_MEMORY_FAULT);check(satchel_cpu_probe_read(ro.get(),target,actual.data(),actual.size())&&actual==bytes,"Q pair cannot write shared time-page backing");
    auto exclusive=make(words({0xc85f7c01,pair_op(0xad000000,2,3,0,16),0xc8047c05,0xd4000001}));satchel_cpu_probe_set_x(exclusive.get(),5,77);satchel_cpu_probe_set_q(exclusive.get(),2,first.data());satchel_cpu_probe_set_q(exclusive.get(),3,second.data());expect_stop(exclusive.get(),SATCHEL_CPU_SVC);check(x(exclusive.get(),4)==1,"successful ordinary Q pair store invalidates exclusive reservation");
    // Fixed-form corruption remains bounded, including adjacent unallocated
    // size/opcode families. Only original synthetic words enter this test.
    for(unsigned bit=22;bit<32;bit++){auto p=make(words({pair_op(0xad000000,1,2,0,0)^(1u<<bit)}));auto stop=satchel_cpu_probe_run(p.get(),1,nullptr,nullptr);check(stop.steps<=1 && stop.reason!=SATCHEL_CPU_INTERNAL_FAULT,"Q pair fixed-bit mutations stay bounded");}
}

static void readonly_shared_tests() {
    constexpr uint64_t page=4096, first=0x7400000000, second=first+page, third=second+page;
    std::array<uint8_t,page> bytes{};for(size_t i=0;i<bytes.size();i++)bytes[i]=uint8_t(i*17+3);
    const auto original=bytes;auto p=make(words({0xd503201f,0xd4000001}));auto sibling=shared(p.get());
    const uint64_t baseline=satchel_cpu_probe_test_physical_bytes(p.get());uint64_t token=99;
    for(size_t size:{size_t(0),size_t(page-1),size_t(page+1),size_t(2*page)})
        check(satchel_cpu_probe_shared_readonly_create(p.get(),bytes.data(),size,&token)==SATCHEL_CPU_MAP_INVALID && token==99,"shared page exact immutable extent, output unchanged");
    check(satchel_cpu_probe_shared_readonly_create(nullptr,bytes.data(),page,&token)==SATCHEL_CPU_MAP_INVALID &&
          satchel_cpu_probe_shared_readonly_create(p.get(),nullptr,page,&token)==SATCHEL_CPU_MAP_INVALID &&
          satchel_cpu_probe_shared_readonly_create(p.get(),bytes.data(),page,nullptr)==SATCHEL_CPU_MAP_INVALID,"shared page null input guards");
    for(unsigned stage:{1u,2u}){satchel_cpu_probe_test_mapping_allocation_failure(stage);
        check(satchel_cpu_probe_shared_readonly_create(p.get(),bytes.data(),page,&token)==SATCHEL_CPU_MAP_ALLOCATION && token==99 &&
              satchel_cpu_probe_test_physical_bytes(p.get())==baseline,"shared snapshot/control allocation rollback");}
    check(satchel_cpu_probe_shared_readonly_create(p.get(),bytes.data(),page,&token)==SATCHEL_CPU_MAP_OK && token && token!=99,"create owned shared snapshot");
    bytes.fill(0);std::array<uint8_t,page> observed{};
    check(satchel_cpu_probe_test_physical_bytes(p.get())==baseline+page && !satchel_cpu_probe_read(p.get(),first,observed.data(),1),"owned unbound shared page charged but not mapped");
    for(uint64_t address:{uint64_t(0),first+1,UINT64_MAX-page+1,code_address,data_address,stack_address})
        check(satchel_cpu_probe_shared_readonly_map(p.get(),token,address,page)==SATCHEL_CPU_MAP_INVALID,"shared mapping alignment/overlap/overflow rejected");
    check(satchel_cpu_probe_shared_readonly_map(p.get(),token,first,page-1)==SATCHEL_CPU_MAP_INVALID &&
          satchel_cpu_probe_shared_readonly_map(p.get(),0,first,page)==SATCHEL_CPU_MAP_INVALID,"shared mapping exact object/size required");
    for(unsigned stage:{1u,2u}){satchel_cpu_probe_test_mapping_allocation_failure(stage);
        check(satchel_cpu_probe_shared_readonly_map(p.get(),token,first,page)==SATCHEL_CPU_MAP_ALLOCATION &&
              !satchel_cpu_probe_check_range(p.get(),first,page,SATCHEL_CPU_READ) &&
              satchel_cpu_probe_test_physical_bytes(p.get())==baseline+page,"shared mapping allocation rollback retains only original object");}
    check(satchel_cpu_probe_shared_readonly_map(p.get(),token,first,page)==SATCHEL_CPU_MAP_OK &&
          satchel_cpu_probe_shared_readonly_map(sibling.get(),token,second,page)==SATCHEL_CPU_MAP_OK,"two shared-context mappings of one immutable allocation");
    check(satchel_cpu_probe_read(sibling.get(),first,observed.data(),page) && observed==original &&
          satchel_cpu_probe_read(p.get(),second,observed.data(),page) && observed==original,"snapshot copied before source lifetime ended");
    check(satchel_cpu_probe_test_same_backing(p.get(),first,second) && satchel_cpu_probe_test_physical_bytes(p.get())==baseline+page,"readonly aliases share actual backing, charged once");
    int overlap=0;check(satchel_cpu_probe_physical_overlap(p.get(),first,page,second,page,&overlap) && overlap,"shared aliases participate in physical overlap guard");
    check(satchel_cpu_probe_check_range(p.get(),first,page,SATCHEL_CPU_READ) && !satchel_cpu_probe_check_range(p.get(),first,page,SATCHEL_CPU_WRITE) &&
          !satchel_cpu_probe_check_range(p.get(),first,page,SATCHEL_CPU_EXECUTE) && !satchel_cpu_probe_set_pc(p.get(),first),"immutable page never writable/executable");
    uint64_t value=123,canary=0;check(!satchel_cpu_probe_write(p.get(),first,&value,8),"checked single write denies immutable page");
    SatchelCPUProbeWrite writes[]={{data_address,&value,8},{first,&value,8}};
    check(satchel_cpu_probe_writev(p.get(),writes,2)==SATCHEL_CPU_WRITE_MEMORY && satchel_cpu_probe_read(p.get(),data_address,&canary,8) && !canary,"readonly second span preserves whole writev transaction");
    uint64_t changed=99;
    check(satchel_cpu_probe_resize_rw(p.get(),first,page)==SATCHEL_CPU_MAP_INVALID && satchel_cpu_probe_resize_rw(p.get(),first,0)==SATCHEL_CPU_MAP_INVALID &&
          satchel_cpu_probe_map_missing_rw(p.get(),first,page,&changed)==SATCHEL_CPU_MAP_INVALID && changed==99 &&
          satchel_cpu_probe_unmap_rw(p.get(),first,page,&changed)==SATCHEL_CPU_MAP_INVALID && changed==99 &&
          satchel_cpu_probe_map_alias(p.get(),third,first,page)==SATCHEL_CPU_MAP_INVALID &&
          satchel_cpu_probe_unmap_alias(p.get(),second,first,page)==SATCHEL_CPU_MAP_INVALID,"no RW replacement/alias escape from shared immutable backing");
    check(satchel_cpu_probe_shared_readonly_unmap(p.get(),token,first,page/2)==SATCHEL_CPU_MAP_INVALID &&
          satchel_cpu_probe_shared_readonly_unmap(p.get(),token+1,first,page)==SATCHEL_CPU_MAP_INVALID &&
          satchel_cpu_probe_shared_readonly_unmap(p.get(),token,data_address,page)==SATCHEL_CPU_MAP_INVALID,"unmap requires exact object and full mapped extent");
    auto stranger=make(words({0xd503201f}));check(satchel_cpu_probe_shared_readonly_map(stranger.get(),token,first,page)==SATCHEL_CPU_MAP_INVALID &&
          satchel_cpu_probe_shared_readonly_release(stranger.get(),token)==SATCHEL_CPU_MAP_INVALID,"shared object identity cannot cross process contexts");
    struct Busy{SatchelCPUProbe *a,*b;const void *bytes;uint64_t token;bool okay=false;} busy{p.get(),sibling.get(),original.data(),token};
    auto result=satchel_cpu_probe_run(p.get(),1,[](void *opaque)->int{auto &b=*static_cast<Busy*>(opaque);uint64_t out=99;
        b.okay=satchel_cpu_probe_shared_readonly_create(b.a,b.bytes,4096,&out)==SATCHEL_CPU_MAP_BUSY && out==99 &&
            satchel_cpu_probe_shared_readonly_map(b.b,b.token,0x7500000000,4096)==SATCHEL_CPU_MAP_BUSY &&
            satchel_cpu_probe_shared_readonly_unmap(b.a,b.token,0x7400000000,4096)==SATCHEL_CPU_MAP_BUSY &&
            satchel_cpu_probe_shared_readonly_release(b.b,b.token)==SATCHEL_CPU_MAP_BUSY;
        std::thread thread([&b]{b.okay=b.okay && satchel_cpu_probe_shared_readonly_release(b.b,b.token)==SATCHEL_CPU_MAP_BUSY;});thread.join();return 1;},&busy);
    check(result.reason==SATCHEL_CPU_CANCELLED && busy.okay,"shared page APIs reject running/sibling/cross-host reentry without mutations");
    p.reset();check(satchel_cpu_probe_read(sibling.get(),first,observed.data(),page) && observed==original,"parent destruction preserves retained shared owner/context");
    check(satchel_cpu_probe_shared_readonly_release(sibling.get(),token)==SATCHEL_CPU_MAP_OK &&
          satchel_cpu_probe_test_physical_bytes(sibling.get())==baseline+page,"owner release retains mapped shared allocation");
    check(satchel_cpu_probe_shared_readonly_release(sibling.get(),token)==SATCHEL_CPU_MAP_INVALID &&
          satchel_cpu_probe_shared_readonly_map(sibling.get(),token,third,page)==SATCHEL_CPU_MAP_INVALID,"released owner cannot be reused to map or release twice");
    check(satchel_cpu_probe_shared_readonly_unmap(sibling.get(),token,first,page)==SATCHEL_CPU_MAP_OK &&
          satchel_cpu_probe_test_physical_bytes(sibling.get())==baseline+page && satchel_cpu_probe_read(sibling.get(),second,observed.data(),page) && observed==original,"first unmap preserves last physical reference");
    check(satchel_cpu_probe_shared_readonly_unmap(sibling.get(),token,second,page)==SATCHEL_CPU_MAP_OK &&
          satchel_cpu_probe_test_physical_bytes(sibling.get())==baseline,"last map releases unique backing charge");
    uint64_t replacement=0;check(satchel_cpu_probe_shared_readonly_create(sibling.get(),original.data(),page,&replacement)==SATCHEL_CPU_MAP_OK && replacement!=token &&
          satchel_cpu_probe_shared_readonly_map(sibling.get(),token,first,page)==SATCHEL_CPU_MAP_INVALID,"registry reuse never revives stale object token");
    // Exact physical budget includes an owned-but-unmapped page and all dynamic
    // allocators, while aliases independently consume the virtual view budget.
    {
        constexpr uint64_t cap=256*1024*1024;uint32_t nop=0xd503201f;
        SatchelCPUProbeRegion regions[]={{code_address,&nop,4,4,5},{data_address,nullptr,0,size_t(cap-page-4),3}};char error[160];
        Probe full(satchel_cpu_probe_create(regions,2,code_address,error,sizeof(error)),satchel_cpu_probe_destroy);check(bool(full),"shared physical budget fixture");uint64_t id=0,sentinel=99;
        check(satchel_cpu_probe_shared_readonly_create(full.get(),original.data(),page,&id)==SATCHEL_CPU_MAP_OK && satchel_cpu_probe_test_physical_bytes(full.get())==cap,"one shared page exactly fills physical capacity");
        check(satchel_cpu_probe_shared_readonly_create(full.get(),original.data(),page,&sentinel)==SATCHEL_CPU_MAP_LIMIT && sentinel==99 &&
              satchel_cpu_probe_resize_rw(full.get(),first,page)==SATCHEL_CPU_MAP_LIMIT &&
              satchel_cpu_probe_map_missing_rw(full.get(),first,page,&sentinel)==SATCHEL_CPU_MAP_LIMIT && sentinel==99,"owned unmapped page cannot be bypassed by dynamic allocators");
        for(unsigned i=0;i<30;i++)check(satchel_cpu_probe_shared_readonly_map(full.get(),id,first+i*page,page)==SATCHEL_CPU_MAP_OK,"shared alias consumes one bounded virtual view");
        check(satchel_cpu_probe_shared_readonly_map(full.get(),id,first+30*page,page)==SATCHEL_CPU_MAP_LIMIT && satchel_cpu_probe_test_physical_bytes(full.get())==cap,"alias view cap independent of unique backing budget");
        check(satchel_cpu_probe_shared_readonly_release(full.get(),id)==SATCHEL_CPU_MAP_OK,"release fully mapped shared budget object");
        for(unsigned i=0;i<30;i++)check(satchel_cpu_probe_shared_readonly_unmap(full.get(),id,first+i*page,page)==SATCHEL_CPU_MAP_OK,"exact alias unmap after owner release");
        check(satchel_cpu_probe_test_physical_bytes(full.get())==cap-page && satchel_cpu_probe_resize_rw(full.get(),first,page)==SATCHEL_CPU_MAP_OK,"last shared reference returns physical budget to ordinary allocator");
    }
    {
        auto many=make(words({0xd503201f}));const uint64_t initial=satchel_cpu_probe_test_physical_bytes(many.get());std::array<uint64_t,32> ids{};
        for(auto &id:ids)check(satchel_cpu_probe_shared_readonly_create(many.get(),original.data(),page,&id)==SATCHEL_CPU_MAP_OK,"bounded owned shared registry slot");
        uint64_t out=99;check(satchel_cpu_probe_shared_readonly_create(many.get(),original.data(),page,&out)==SATCHEL_CPU_MAP_LIMIT && out==99 &&
            satchel_cpu_probe_test_physical_bytes(many.get())==initial+32*page,"owned registry cap not confused with mapped views");
        check(satchel_cpu_probe_shared_readonly_map(many.get(),ids[0],first,page)==SATCHEL_CPU_MAP_OK,"last CPU teardown owns mapped and unmapped shared pages");
        // Destructor is exercised with all server ownership still live.
    }
    for(uint32_t store:{0xf9000001u,0xb9000001u,0x79000001u,0x39000001u,0x78000001u,0xa9000801u,0x3d800001u,0xc89ffc01u}){
        auto denied=make(words({store}));uint64_t id=0;check(satchel_cpu_probe_shared_readonly_create(denied.get(),original.data(),page,&id)==SATCHEL_CPU_MAP_OK &&
            satchel_cpu_probe_shared_readonly_map(denied.get(),id,first,page)==SATCHEL_CPU_MAP_OK,"readonly guest store fixture");satchel_cpu_probe_set_x(denied.get(),0,first);
        auto fault=satchel_cpu_probe_run(denied.get(),1,nullptr,nullptr);check(fault.reason==SATCHEL_CPU_MEMORY_FAULT && !fault.steps &&
            satchel_cpu_probe_read(denied.get(),first,observed.data(),page) && observed==original,"admitted scalar/pair/SIMD/release stores cannot mutate immutable page");
    }
    for(bool wide:{false,true}){auto denied=make(words({wide?0xc85ffc01u:0x885ffc01u,wide?0xc802fc01u:0x8802fc01u}));uint64_t id=0;
        check(satchel_cpu_probe_shared_readonly_create(denied.get(),original.data(),page,&id)==SATCHEL_CPU_MAP_OK && satchel_cpu_probe_shared_readonly_map(denied.get(),id,first,page)==SATCHEL_CPU_MAP_OK,"readonly exclusive fixture");
        satchel_cpu_probe_set_x(denied.get(),0,first);satchel_cpu_probe_set_x(denied.get(),2,99);auto fault=satchel_cpu_probe_run(denied.get(),2,nullptr,nullptr);
        check(fault.reason==SATCHEL_CPU_MEMORY_FAULT && fault.steps==1 && x(denied.get(),2)==99 && satchel_cpu_probe_read(denied.get(),first,observed.data(),page) && observed==original,"exclusive read allowed but store preflight denies without status/byte mutation");}
    // Failed mapping and owner-only lifecycle preserve an unrelated exclusive
    // reservation; actual map/unmap commits conservatively clear all contexts.
    for(unsigned mode=0;mode<4;mode++){auto atomic=make(words({0xc85f7c01,0xc8027c03,0xd4000001}));uint64_t id=0;satchel_cpu_probe_set_x(atomic.get(),3,9);
        check(satchel_cpu_probe_shared_readonly_create(atomic.get(),original.data(),page,&id)==SATCHEL_CPU_MAP_OK,"reservation shared-object fixture");
        if(mode==2)check(satchel_cpu_probe_shared_readonly_map(atomic.get(),id,first,page)==SATCHEL_CPU_MAP_OK,"reservation unmap fixture");
        check(satchel_cpu_probe_run(atomic.get(),1,nullptr,nullptr).reason==SATCHEL_CPU_BUDGET,"establish exclusive reservation");
        if(mode==0)check(satchel_cpu_probe_shared_readonly_release(atomic.get(),id)==SATCHEL_CPU_MAP_OK,"owner-only release preserves guest memory");
        if(mode==1){satchel_cpu_probe_test_mapping_allocation_failure(2);check(satchel_cpu_probe_shared_readonly_map(atomic.get(),id,first,page)==SATCHEL_CPU_MAP_ALLOCATION,"failed shared map reservation transaction");}
        if(mode==2)check(satchel_cpu_probe_shared_readonly_unmap(atomic.get(),id,first,page)==SATCHEL_CPU_MAP_OK,"shared unmap clears reservation");
        if(mode==3)check(satchel_cpu_probe_shared_readonly_map(atomic.get(),id,first,page)==SATCHEL_CPU_MAP_OK,"shared map clears reservation");
        expect_stop(atomic.get(),SATCHEL_CPU_SVC);check((x(atomic.get(),2)==0)==(mode<2),"only successful map mutations invalidate reservation");
    }
}

int main(int argc, char **argv) {
    if(argc==3 && std::strcmp(argv[2],"--fdiv-only")==0){fdiv_tests(argv[1]);std::cout<<checks<<" FDIV checks passed (focused original fixtures only).\n";return 0;}
    if(argc==3 && std::strcmp(argv[2],"--isb-only")==0){isb_tests(argv[1]);barrier_tests(argv[1]);dsb_tests(argv[1]);clrex_tests(argv[1]);std::cout<<checks<<" ISB/legacy system checks passed (focused original fixtures only).\n";return 0;}
    if(argc==3 && std::strcmp(argv[2],"--fcvtzu-only")==0){fcvtzu2d_tests(argv[1]);std::cout<<checks<<" FCVTZU2D checks passed (focused original fixtures only).\n";return 0;}
    if(argc==3 && std::strcmp(argv[2],"--fcvtl-only")==0){fcvtl_tests(argv[1]);std::cout<<checks<<" FCVTL checks passed (focused original fixtures only).\n";return 0;}
    if(argc==3 && std::strcmp(argv[2],"--fmul-indexed-only")==0){fmulindexed_tests(argv[1]);std::cout<<checks<<" indexed FMUL checks passed (focused original fixtures only).\n";return 0;}
    if(argc==3 && std::strcmp(argv[2],"--ucvtf64-only")==0){ucvtf64_tests(argv[1]);ucvtf_tests(argv[1]);std::cout<<checks<<" UCVTF W/X S/D checks passed (focused original fixtures only).\n";return 0;}
    if(argc==3 && std::strcmp(argv[2],"--ucvtf-only")==0){ucvtf_tests(argv[1]);std::cout<<checks<<" UCVTF checks passed (focused original fixtures only).\n";return 0;}
    if(argc==3 && std::strcmp(argv[2],"--element-narrow-only")==0){element_narrow_tests(argv[1]);std::cout<<checks<<" element/narrow checks passed (focused original fixtures only).\n";return 0;}
    if(argc==3 && std::strcmp(argv[2],"--prefetch-only")==0) {
        prefetch_tests(argv[1]);
        std::cout<<checks<<" prefetch checks passed (focused original fixtures only).\n";
        return 0;
    }
    if(argc==3 && std::strcmp(argv[2],"--reprotect-only")==0) {
        reprotect_tests();
        std::cout<<checks<<" reprotect checks passed (original fixtures; no game compatibility claim).\n";
        return 0;
    }
    check(argc==2,"fixture directory argument");
    reprotect_tests();
    prefetch_tests(argv[1]);
    element_narrow_tests(argv[1]);
    simd_count_tests(argv[1]);
    simd_widen_reduce_tests(argv[1]);
    fmov_gpr_tests(argv[1]);
    simd_logical_tests(argv[1]);
    simd_reduce_tests(argv[1]);
    simd_permute_tests(argv[1]);
    simd_compare_tests(argv[1]);
    simd_addsub_tests(argv[1]);
    simd_register_tests(argv[1]);
    simd_width_tests(argv[1]);
    mov_integer_tests(argv[1]);
    simd_unsigned_tests(argv[1]);
    simd_single_tests(argv[1]);
    dup_general_tests(argv[1]);
    simd_pair_tests(argv[1]);
    readonly_shared_tests();
    binary64_tests();
    fcvtzu2d_tests(argv[1]);
    fcvtl_tests(argv[1]);
    fmulindexed_tests(argv[1]);
    fdiv_tests(argv[1]);
    ucvtf64_tests(argv[1]);
    ucvtf_tests(argv[1]);
    physical_overlap_tests();
    alias_tests();
    fp_status_tests();
    clock_tests(argv[1]);
    mapping_tests();
    range_tests();
    range_mapping_tests();
    check(argc == 2, "fixture directory");
    shared_context_tests(argv[1]);
    multiwrite_tests();
    barrier_tests(argv[1]);
    dsb_tests(argv[1]);
    isb_tests(argv[1]);
    clrex_tests(argv[1]);
    breakpoint_tests(argv[1]);
    acquire_tests(argv[1]);
    ordered_tests(argv[1]);
    for (uint64_t input : {uint64_t{0}, uint64_t{20}, UINT64_MAX - 6}) {
        uint64_t native = 0, simulated = 0;
        auto p = make(fixture(argv[1], "arithmetic"));
        satchel_cpu_probe_set_x(p.get(), 1, input);
        auto expected = probe_arithmetic(&native, input);
        expect_stop(p.get(), SATCHEL_CPU_SVC);
        check(x(p.get(), 0) == expected, "native integer/flags/branch equivalence");
        check(satchel_cpu_probe_read(p.get(), data_address, &simulated, sizeof(simulated)), "read data");
        check(simulated == native, "native integer memory equivalence");
    }
    std::array<uint8_t, 64> bytes{};
    for (size_t i = 0; i < 32; ++i) bytes[i] = uint8_t(i * 11);
    auto vector = make(fixture(argv[1], "vector"), bytes.data());
    probe_vector(bytes.data()); expect_stop(vector.get(), SATCHEL_CPU_SVC);
    std::array<uint8_t, 64> actual{};
    check(satchel_cpu_probe_read(vector.get(), data_address, actual.data(), actual.size()), "read vector data");
    check(actual == bytes, "native SIMD memory equivalence");
    std::array<uint8_t, 16> q{};
    check(satchel_cpu_probe_get_q(vector.get(), 2, q.data()), "read vector register");
    check(std::memcmp(q.data(), bytes.data() + 32, 16) == 0, "SIMD register equivalence");
    double a = 1.5, b = 2.25;
    bytes.fill(0); std::memcpy(bytes.data(), &a, 8); std::memcpy(bytes.data() + 16, &b, 8);
    auto fp = make(fixture(argv[1], "floating"), bytes.data());
    probe_floating(bytes.data()); expect_stop(fp.get(), SATCHEL_CPU_SVC);
    check(satchel_cpu_probe_read(fp.get(), data_address, actual.data(), actual.size()), "read FP data");
    check(actual == bytes, "native FP memory equivalence");
    bytes.fill(0);
    auto pair = make(fixture(argv[1], "pairs"), bytes.data());
    auto pair_expected = probe_pairs(bytes.data()); expect_stop(pair.get(), SATCHEL_CPU_SVC);
    check(x(pair.get(), 0) == pair_expected && pair_expected == 78, "native GPR pair/writeback equivalence");
    check(satchel_cpu_probe_read(pair.get(), data_address, actual.data(), actual.size()), "read pair data");
    check(actual == bytes, "native pair offset/pre/post memory equivalence");
    check(x(pair.get(), 29) == 0 && x(pair.get(), 30) == 0, "stack frame registers restored");
    bytes.fill(0xa5);
    auto wpair = make(fixture(argv[1], "wpairs"), bytes.data());
    auto wpair_expected = probe_wpairs(bytes.data()); expect_stop(wpair.get(), SATCHEL_CPU_SVC);
    check(x(wpair.get(), 0) == wpair_expected && wpair_expected == UINT64_C(0x89abcdef) + UINT64_C(0xfedc3210) + 40,
          "native W pair zero-extension/writeback equivalence");
    check(satchel_cpu_probe_read(wpair.get(), data_address, actual.data(), actual.size()) && actual == bytes,
          "native W pair truncation/offset/pre/post/stack memory equivalence");
    bytes.fill(0xa5);
    auto byte_probe = make(fixture(argv[1], "bytes"), bytes.data());
    auto byte_expected = probe_bytes(bytes.data()); expect_stop(byte_probe.get(), SATCHEL_CPU_SVC);
    check(x(byte_probe.get(), 0) == byte_expected && byte_expected == 460, "native byte store/load/zero-extension equivalence");
    check(satchel_cpu_probe_read(byte_probe.get(), data_address, actual.data(), actual.size()), "read byte data");
    check(actual == bytes, "byte stores preserve every neighboring byte");
    bytes.fill(0xa5);
    auto unscaled = make(fixture(argv[1], "unscaled"), bytes.data());
    auto unscaled_expected = probe_unscaled(bytes.data()); expect_stop(unscaled.get(), SATCHEL_CPU_SVC);
    check(x(unscaled.get(), 0) == unscaled_expected, "native unscaled/pre/post byte/W/X equivalence");
    check(satchel_cpu_probe_read(unscaled.get(), data_address, actual.data(), actual.size()) && actual == bytes,
          "native negative/unaligned/zero-writeback scalar memory equivalence");
    const auto halfword_unscaled_code = fixture(argv[1], "halfunscaled");
    for (uint64_t value : {uint64_t{0},uint64_t{1},uint64_t{0xffff},uint64_t{0x10000},
                          uint64_t{0x8000},uint64_t{0x123456789abcdef0},UINT64_MAX}) {
        std::array<uint8_t,576> native,got; native.fill(0xa5);
        auto p=make(halfword_unscaled_code,native.data(),native.size());satchel_cpu_probe_set_x(p.get(),1,value);
        probe_halfunscaled(native.data(),value);expect_stop(p.get(),SATCHEL_CPU_SVC);
        check(satchel_cpu_probe_read(p.get(),data_address,got.data(),got.size()) && got==native,
              "native halfword offset/pre/post truncation, zero extension, unaligned memory and signed limits match complete guarded buffer");
        uint64_t flags=0;std::memcpy(&flags,native.data()+24,8);
        check(flags==1,"native halfword transfers preserve NZCV");
        uint32_t fpcr=1,fpsr=1;
        check(satchel_cpu_probe_get_fp_status(p.get(),&fpcr,&fpsr)==SATCHEL_CPU_FP_OK && fpcr==0 && fpsr==0,
              "integer halfword transfers preserve known owned FP state");
    }
    for(uint32_t mode : {0u,0x400u,0xc00u}) {
        const uint16_t value=0xfedc;
        auto p=make(words({scalar_op(0x78400000|mode,1,0,0),0xd4000001}),&value,2);
        satchel_cpu_probe_set_x(p.get(),1,UINT64_MAX);expect_stop(p.get(),SATCHEL_CPU_SVC);
        check(x(p.get(),1)==value && x(p.get(),0)==data_address,
              "halfword zero-offset modes zero extend W result and preserve base");
        auto badsp=make(words({scalar_op(0x78000000|mode,1,31,0)}));
        satchel_cpu_probe_set_sp(badsp.get(),stack_address+1);expect_stop(badsp.get(),SATCHEL_CPU_MEMORY_FAULT);
        for(uint64_t address : {uint64_t{0},UINT64_MAX,code_address}){
            auto fault=make(words({scalar_op(0x78000000|mode,1,0,0)}));
            satchel_cpu_probe_set_x(fault.get(),0,address);satchel_cpu_probe_set_x(fault.get(),1,0xffff);
            expect_stop(fault.get(),SATCHEL_CPU_MEMORY_FAULT);
            check(x(fault.get(),0)==address && x(fault.get(),1)==0xffff,
                  "halfword invalid/protected access preserves source and writeback base");
        }
    }
    const uint16_t alias_half=0xfedc;
    auto half_alias=make(words({scalar_op(0x78400000,0,0,0),0xd4000001}),&alias_half,2);
    expect_stop(half_alias.get(),SATCHEL_CPU_SVC);check(x(half_alias.get(),0)==alias_half,
          "halfword unscaled non-writeback destination may alias base");
    auto half_reservation=make(words({0xc85f7c01,scalar_op(0x78000000,2,0,0),0xc8037c01,0xd4000001}));
    satchel_cpu_probe_set_x(half_reservation.get(),2,0xaabb);expect_stop(half_reservation.get(),SATCHEL_CPU_SVC);
    uint64_t half_after=0;
    check(x(half_reservation.get(),3)==1 && satchel_cpu_probe_read(half_reservation.get(),data_address,&half_after,8) && half_after==0xaabb,
          "halfword ordinary store invalidates reservation and failed STXR cannot undo it");
    for(uint32_t invalid : {0x78200001u,0x78400801u,0x78000801u,0x78800001u,0x78c00001u}){
        auto p=make(words({invalid}));expect_stop(p.get(),SATCHEL_CPU_UNSUPPORTED);
    }
    const uint64_t multiply_cases[][3] = {
        {0, 0, 0}, {0, UINT64_MAX, UINT64_MAX}, {1, 1, 0},
        {UINT64_MAX, UINT64_MAX, UINT64_MAX}, {UINT64_MAX, 2, 0},
        {UINT64_C(0x8000000000000000), 2, 1},
        {UINT64_C(0xffffffff), UINT64_C(0xffffffff), UINT64_C(0xffffffff)},
        {UINT64_C(0xdeadbeefffffffff), UINT64_C(0x1234567800000002), UINT64_C(0xcafebabe00000003)},
        {UINT64_C(0x123456789abcdef0), UINT64_C(0xfedcba9876543210), UINT64_C(0x8000000080000000)}};
    const auto multiply_code = fixture(argv[1], "multiply");
    for (const auto &inputs : multiply_cases) {
        bytes.fill(0xa5);
        auto p = make(multiply_code, bytes.data());
        for (unsigned i = 0; i < 3; ++i) satchel_cpu_probe_set_x(p.get(), i + 1, inputs[i]);
        probe_multiply(bytes.data(), inputs[0], inputs[1], inputs[2]);
        expect_stop(p.get(), SATCHEL_CPU_SVC);
        check(satchel_cpu_probe_read(p.get(), data_address, actual.data(), actual.size()) && actual == bytes,
              "native multiply overflow/zero-extension/NZCV equivalence");
        const uint64_t product = inputs[0] * inputs[1];
        const uint64_t expected[] = {product + inputs[2], inputs[2] - product,
            uint32_t(product + inputs[2]), uint32_t(inputs[2] - product), product, uint32_t(product), 1};
        check(std::memcmp(actual.data(), expected, sizeof(expected)) == 0,
              "multiply results match unsigned modular arithmetic and preserve NZCV");
    }
    const auto highmultiply_code = fixture(argv[1], "highmultiply");
    uint64_t product_random = UINT64_C(0xc37e59c294073219);
    for (unsigned c = 0; c < 141; ++c) {
        uint64_t inputs[3];
        for (unsigned i = 0; i < 3; ++i) {
            product_random ^= product_random << 13; product_random ^= product_random >> 7; product_random ^= product_random << 17;
            inputs[i] = c < 9 ? multiply_cases[c][i] : product_random;
        }
        if (c >= 9 && c < 13) {
            const uint64_t edge[] = {UINT64_C(0x8000000000000000), UINT64_C(0x7fffffffffffffff),
                                     UINT64_C(0x80000000), UINT64_C(0x7fffffff)};
            inputs[0] = inputs[1] = edge[c - 9]; inputs[2] = UINT64_MAX;
        }
        std::array<uint8_t, 128> native{}, interpreted{};
        native.fill(0xa5);
        auto p = make(highmultiply_code, native.data(), native.size());
        for (unsigned i = 0; i < 3; ++i) satchel_cpu_probe_set_x(p.get(), i + 1, inputs[i]);
        probe_highmultiply(native.data(), inputs[0], inputs[1], inputs[2]);
        expect_stop(p.get(), SATCHEL_CPU_SVC);
        check(satchel_cpu_probe_read(p.get(), data_address, interpreted.data(), interpreted.size()) && interpreted == native,
              "native high/widening multiply signed extremes, accumulator wrap, aliases and NZCV");
        uint64_t flags; std::memcpy(&flags, interpreted.data() + 104, sizeof(flags));
        check(flags == 1, "high/widening multiply preserves NZCV across all forms");
    }
    // High multiply has no W form or accumulator, and bit15 must remain zero.
    for (uint32_t base : {0x9b407c00u, 0x9bc07c00u}) {
        for (unsigned ra = 0; ra < 31; ++ra) {
            auto p = make(words({(base & ~0x7c00u) | (ra << 10)}));
            expect_stop(p.get(), SATCHEL_CPU_UNSUPPORTED);
        }
        for (uint32_t change : {0x80000000u, 0x8000u, 0x20000000u}) {
            auto p = make(words({base ^ change})); expect_stop(p.get(), SATCHEL_CPU_UNSUPPORTED);
        }
    }
    for (uint32_t base : {0x9b200000u, 0x9ba00000u}) {
        auto p = make(words({base ^ 0x80000000u})); expect_stop(p.get(), SATCHEL_CPU_UNSUPPORTED);
    }
    const auto tls_code = fixture(argv[1], "tls");
    for (const auto &inputs : multiply_cases) {
        bytes.fill(0xa5);
        auto p = make(tls_code, bytes.data());
        uint64_t ro = UINT64_MAX, rw = UINT64_MAX;
        check(satchel_cpu_probe_get_tls(p.get(), &ro, &rw) && ro == 0 && rw == 0,
              "new context thread registers are zero, never inherited from host");
        check(satchel_cpu_probe_set_tls(p.get(), inputs[0], inputs[1]), "set guest thread register values");
        satchel_cpu_probe_set_x(p.get(), 3, inputs[2]);
        probe_tls(bytes.data(), inputs[0], inputs[1], inputs[2]);
        expect_stop(p.get(), SATCHEL_CPU_SVC);
        check(satchel_cpu_probe_read(p.get(), data_address, actual.data(), actual.size()) && actual == bytes,
              "TLS guest assembly matches independent native value model and preserves flags");
        const uint64_t expected[] = {inputs[0], inputs[1], inputs[2], inputs[0], 0, 1};
        check(std::memcmp(actual.data(), expected, sizeof(expected)) == 0, "TLS width/read-only/ZR semantics");
        check(satchel_cpu_probe_get_tls(p.get(), &ro, &rw) && ro == inputs[0] && rw == 0,
              "MSR updates guest writable value only");
    }
    auto tls_a = make(words({0xd53bd040, 0xd4000001, 0xd53bd061, 0xd4000001}));
    auto tls_b = make(words({0xd53bd040, 0xd4000001}));
    check(satchel_cpu_probe_set_tls(tls_a.get(), 0x1111222233334444, 0xaaaabbbbccccdddd), "initialize TLS context A");
    check(satchel_cpu_probe_set_tls(tls_b.get(), 0x55, 0x66), "initialize TLS context B");
    expect_stop(tls_a.get(), SATCHEL_CPU_SVC); expect_stop(tls_b.get(), SATCHEL_CPU_SVC);
    check(x(tls_a.get(), 0) == UINT64_C(0xaaaabbbbccccdddd) && x(tls_b.get(), 0) == 0x66,
          "interleaved contexts keep independent guest TLS registers");
    check(satchel_cpu_probe_set_tls(tls_a.get(), 0x777788889999aaaa, 0xbb), "scheduler changes saved TLS context");
    expect_stop(tls_a.get(), SATCHEL_CPU_SVC);
    check(x(tls_a.get(), 1) == UINT64_C(0x777788889999aaaa), "TLS setter is visible after SVC resume");
    uint64_t tls_ro = 0x11, tls_rw = 0x22;
    check(!satchel_cpu_probe_set_tls(nullptr, 0, 0), "null TLS setter rejected");
    check(!satchel_cpu_probe_get_tls(nullptr, &tls_ro, &tls_rw) && tls_ro == 0x11 && tls_rw == 0x22,
          "null TLS getter preserves both outputs");
    check(!satchel_cpu_probe_get_tls(tls_a.get(), &tls_ro, nullptr) && tls_ro == 0x11,
          "TLS getter preflights both outputs");
    check(!satchel_cpu_probe_get_tls(tls_a.get(), nullptr, &tls_rw) && tls_rw == 0x22,
          "TLS getter rejects missing readonly output");
    for (unsigned rt = 0; rt < 32; ++rt) {
        auto p = make(words({0xd51bd040u | rt, 0xd53bd042, 0x910003e3, 0xd4000001}));
        if (rt < 31) satchel_cpu_probe_set_x(p.get(), rt, UINT64_MAX - rt);
        expect_stop(p.get(), SATCHEL_CPU_SVC);
        check(x(p.get(), 2) == (rt == 31 ? 0 : UINT64_MAX - rt), "MSR accepts all GPRs including ZR");
        check(x(p.get(), 3) == stack_address + 128, "TLS ZR accesses never change SP");
    }
    // CNTVCT is covered positively by clock_tests; the adjacent reserved timer
    // register remains a negative system-register admission check.
    for (uint32_t instruction : {0xd51bd060u, 0xd53bd080u, 0xd53be060u, 0xd538d040u, 0xd53b4200u,
                                 0xd53b4400u, 0xd51b4200u, 0xd51b4400u, 0xd53bd000u}) {
        auto p = make(words({instruction}));
        satchel_cpu_probe_set_tls(p.get(), 0x1234, 0x5678);
        expect_stop(p.get(), SATCHEL_CPU_UNSUPPORTED);
        check(satchel_cpu_probe_get_tls(p.get(), &tls_ro, &tls_rw) && tls_ro == 0x1234 && tls_rw == 0x5678,
              "unsupported system registers cannot mutate guest TLS");
    }
    for (uint64_t address : {UINT64_MAX, uint64_t{0}, code_address, data_address + 60}) {
        auto p = make(words({0xd53bd040, 0xf9000001, 0xd4000001})); // MRS then STR X1,[X0].
        satchel_cpu_probe_set_tls(p.get(), 0, address);
        expect_stop(p.get(), SATCHEL_CPU_MEMORY_FAULT);
        check(satchel_cpu_probe_get_tls(p.get(), &tls_ro, &tls_rw) && tls_rw == address,
              "TLS pointer use remains bounded and permission checked");
    }
    const auto register_code = fixture(argv[1], "registerloads");
    for (uint32_t first : {0u, 0x80000000u, 0x7fffffffu, 0xffffffffu}) {
        std::array<uint8_t, 128> input{}, interpreted{};
        input.fill(0xa5);
        const uint32_t input_words[] = {first, 0xffffffff, 0x7fffffff, 0x12345678};
        const uint64_t input_wide = UINT64_C(0xfedcba9876543210);
        std::memcpy(input.data(), input_words, sizeof(input_words));
        std::memcpy(input.data() + 16, &input_wide, 8);
        auto p = make(register_code, input.data(), input.size());
        probe_registerloads(input.data()); expect_stop(p.get(), SATCHEL_CPU_SVC);
        check(satchel_cpu_probe_read(p.get(), data_address, interpreted.data(), interpreted.size()) && interpreted == input,
              "native register-offset load extension/scaling/SP semantics");
        const uint64_t expected[] = {uint64_t(int64_t(int32_t(first))), UINT64_MAX,
            0x7fffffff, 0x12345678, first, 0x78, input_wide, input_wide};
        check(std::memcmp(interpreted.data() + 64, expected, sizeof(expected)) == 0,
              "LDRSW sign extension differs from W/B load zero extension");
    }
    const auto testbranch_code = fixture(argv[1], "testbranch");
    for (uint64_t bits : {uint64_t{0}, UINT64_MAX, uint64_t{1}, UINT64_C(0x8000000000000000),
                         UINT64_C(0x100000000), UINT64_C(0xaaaaaaaaaaaaaaaa), UINT64_C(0x5555555555555555)}) {
        bytes.fill(0xa5);
        auto p = make(testbranch_code, bytes.data()); satchel_cpu_probe_set_x(p.get(), 1, bits);
        probe_testbranch(bytes.data(), bits); expect_stop(p.get(), SATCHEL_CPU_SVC);
        check(satchel_cpu_probe_read(p.get(), data_address, actual.data(), actual.size()) && actual == bytes,
              "native TBZ/TBNZ all bit positions/branches/NZCV equivalence");
        uint64_t ones = 0;
        for (unsigned bit = 0; bit < 64; ++bit) ones += (bits >> bit) & 1;
        const uint64_t expected[] = {ones, 64 - ones, 2, 1};
        check(std::memcmp(actual.data(), expected, sizeof(expected)) == 0,
              "test branches preserve all flags and execute backward/zero-register semantics");
    }
    const auto bitcount_code = fixture(argv[1], "bitcount");
    for (uint64_t bits : {uint64_t{0}, UINT64_MAX, uint64_t{1}, UINT64_C(0x8000000000000000),
                         UINT64_C(0x100000000), UINT64_C(0x80000000), UINT64_C(0x0123456789abcdef),
                         UINT64_C(0xaaaaaaaaaaaaaaaa), UINT64_C(0x5555555555555555)}) {
        bytes.fill(0xa5);
        auto p = make(bitcount_code, bytes.data()); satchel_cpu_probe_set_x(p.get(), 1, bits);
        probe_bitcount(bytes.data(), bits); expect_stop(p.get(), SATCHEL_CPU_SVC);
        check(satchel_cpu_probe_read(p.get(), data_address, actual.data(), actual.size()) && actual == bytes,
              "native RBIT/CLZ W/X flags/zero/overflow-boundary equivalence");
        const uint64_t expected[] = {reference_reverse(bits, 64), reference_reverse(bits, 32),
            reference_leading(bits, 64), reference_leading(bits, 32), bits, uint32_t(bits), 96, 1};
        check(std::memcmp(actual.data(), expected, sizeof(expected)) == 0,
              "RBIT inverse and CLZ zero/width/zero-extension semantics");
    }
    const auto variable_code = fixture(argv[1], "variableshift");
    for (uint64_t value : {uint64_t{0}, uint64_t{1}, UINT64_MAX, UINT64_C(0x8000000000000000),
                          UINT64_C(0x80000000), UINT64_C(0x0123456789abcdef)}) {
        for (uint64_t count : {uint64_t{0}, uint64_t{1}, uint64_t{31}, uint64_t{32}, uint64_t{33},
                               uint64_t{63}, uint64_t{64}, uint64_t{65}, UINT64_MAX, UINT64_C(0x100000020)}) {
            std::array<uint8_t, 128> native{}, interpreted{}; native.fill(0xa5);
            auto p = make(variable_code, native.data(), native.size());
            satchel_cpu_probe_set_x(p.get(), 1, value); satchel_cpu_probe_set_x(p.get(), 2, count);
            probe_variableshift(native.data(), value, count); expect_stop(p.get(), SATCHEL_CPU_SVC);
            check(satchel_cpu_probe_read(p.get(), data_address, interpreted.data(), interpreted.size()) && interpreted == native,
                  "native variable-shift width/count/flags equivalence");
            uint64_t expected[9] = {};
            for (unsigned operation = 0; operation < 4; ++operation) {
                expected[operation] = reference_variable_shift(value, count, 64, operation);
                expected[operation + 4] = reference_variable_shift(value, count, 32, operation);
            }
            expected[8] = 1;
            check(std::memcmp(interpreted.data(), expected, sizeof(expected)) == 0,
                  "variable shifts use masked counts, sign fill, rotation and W zero-extension");
        }
    }
    const auto register_memory_code = fixture(argv[1], "registermemory");
    for (uint64_t value : {uint64_t{0}, UINT64_MAX, UINT64_C(0x7f7f), UINT64_C(0x8080),
                          UINT64_C(0x8000), UINT64_C(0x0123456789abcdef)}) {
        std::array<uint8_t, 160> native{}, interpreted{}; native.fill(0xa5);
        auto p = make(register_memory_code, native.data(), native.size()); satchel_cpu_probe_set_x(p.get(), 1, value);
        probe_registermemory(native.data(), value); expect_stop(p.get(), SATCHEL_CPU_SVC);
        check(satchel_cpu_probe_read(p.get(), data_address, interpreted.data(), interpreted.size()) && interpreted == native,
              "native register stores and signed-byte/halfword loads preserve width/address/flags");
        const uint64_t signed_byte = uint64_t(int64_t(int8_t(value)));
        const uint64_t signed_half = uint64_t(int64_t(int16_t(value)));
        const uint64_t expected[] = {signed_byte, uint32_t(signed_byte), signed_half, uint32_t(signed_half),
            uint16_t(value), uint8_t(value), uint32_t(value), value, 1, uint16_t(value)};
        check(std::memcmp(interpreted.data() + 64, expected, sizeof(expected)) == 0,
              "signed B/H loads extend to selected destination width");
    }
    const auto conditional_code = fixture(argv[1], "conditional");
    const uint64_t conditional_cases[][4] = {
        {0, UINT64_MAX, 0, 0}, {UINT64_MAX, UINT64_MAX, 0, 1},
        {UINT64_C(0x8000000000000000), UINT64_C(0x8000000000000000), UINT64_C(0x8000000000000000), 1},
        {UINT64_C(0x123456789abcdef0), 0, UINT64_C(0x7fffffffffffffff), UINT64_MAX},
        {UINT64_C(0xdeadbeefffffffff), UINT64_C(0xcafebabe80000000), 2, 1},
        {1, 2, UINT64_MAX, 0}, {UINT64_MAX, UINT64_C(0x123456789abcdef0), 0, UINT64_MAX}};
    for (const auto &args : conditional_cases) {
        std::array<uint64_t, 133> native{}, interpreted{}, expected{};
        auto p = make(conditional_code, native.data(), sizeof(native));
        for (unsigned i = 0; i < 4; ++i) satchel_cpu_probe_set_x(p.get(), i + 1, args[i]);
        probe_conditional(native.data(), args[0], args[1], args[2], args[3]); expect_stop(p.get(), SATCHEL_CPU_SVC);
        check(satchel_cpu_probe_read(p.get(), data_address, interpreted.data(), sizeof(interpreted)) && interpreted == native,
              "native conditional select all conditions/operations/widths equivalence");
        const uint64_t difference = args[2] - args[3];
        const bool n = difference >> 63, z = difference == 0, c = args[2] >= args[3];
        const bool v = ((args[2] ^ args[3]) & (args[2] ^ difference)) >> 63;
        for (unsigned condition = 0; condition < 16; ++condition) {
            for (unsigned operation = 0; operation < 4; ++operation) {
                uint64_t value = args[0];
                if (!reference_condition(condition, n, z, c, v)) {
                    value = args[1];
                    if (operation == 1) ++value;
                    if (operation == 2) value = ~value;
                    if (operation == 3) value = uint64_t{0} - value;
                }
                expected[condition * 8 + operation] = value;
                expected[condition * 8 + operation + 4] = uint32_t(value);
            }
        }
        expected[128] = z; expected[129] = n; expected[130] = n ? UINT64_MAX : 0;
        expected[131] = n != v ? uint64_t{0} - args[0] : args[0];
        expected[132] = uint64_t(n) | (uint64_t(z) << 1) | (uint64_t(c) << 2) | (uint64_t(v) << 3);
        check(interpreted == expected, "conditional selection preserves NZCV and modular/W/alias semantics");
    }
    const auto division_code = fixture(argv[1], "division");
    const uint64_t division_cases[][2] = {
        {0, 0}, {UINT64_MAX, 0}, {0, UINT64_MAX}, {123, 3}, {UINT64_MAX, UINT64_MAX},
        {UINT64_C(0x8000000000000000), UINT64_MAX}, {UINT64_C(0x8000000000000000), 2},
        {UINT64_C(0x80000000), UINT64_C(0xffffffff)}, {UINT64_C(0x80000000), UINT64_C(0x100000000)},
        {uint64_t(-17), 5}, {17, uint64_t(-5)}, {UINT64_MAX, 2},
        {UINT64_C(0x7fffffff), UINT64_C(0x80000000)}, {UINT64_C(0x123456789abcdef0), UINT64_C(0xfedcba98)}};
    for (const auto &args : division_cases) {
        bytes.fill(0xa5);
        auto p = make(division_code, bytes.data());
        satchel_cpu_probe_set_x(p.get(), 1, args[0]); satchel_cpu_probe_set_x(p.get(), 2, args[1]);
        probe_division(bytes.data(), args[0], args[1]); expect_stop(p.get(), SATCHEL_CPU_SVC);
        check(satchel_cpu_probe_read(p.get(), data_address, actual.data(), actual.size()) && actual == bytes,
              "native UDIV/SDIV width/zero/overflow/NZCV equivalence");
        const uint64_t expected[] = {reference_division(args[0], args[1], true, false),
            reference_division(args[0], args[1], true, true), reference_division(args[0], args[1], false, false),
            reference_division(args[0], args[1], false, true), 0, 0, 0, 1};
        check(std::memcmp(actual.data(), expected, sizeof(expected)) == 0,
              "division truncates toward zero, handles MIN/-1 and zero, and zero-extends W");
    }
    const auto immediates_code = fixture(argv[1], "immediates");
    for (uint64_t value : {uint64_t{0}, UINT64_MAX, UINT64_C(0x80), UINT64_C(0x8000), UINT64_C(0x80000000),
                          UINT64_C(0x7f7f), UINT64_C(0x7fff), UINT64_C(0x0123456789abcdef)}) {
        std::array<uint8_t, 8192> native{}, interpreted{}; native.fill(0xa5);
        auto p = make(immediates_code, native.data(), native.size()); satchel_cpu_probe_set_x(p.get(), 1, value);
        probe_immediates(native.data(), value); expect_stop(p.get(), SATCHEL_CPU_SVC);
        check(satchel_cpu_probe_read(p.get(), data_address, interpreted.data(), interpreted.size()) && interpreted == native,
              "native immediate integer transfer widths/scaled-limit/SP/ZR/flags equivalence");
        const uint64_t signed_byte = uint64_t(int64_t(int8_t(value)));
        const uint64_t signed_half = uint64_t(int64_t(int16_t(value)));
        const uint64_t signed_word = uint64_t(int64_t(int32_t(value)));
        const uint64_t expected[] = {uint16_t(value), signed_byte, uint32_t(signed_byte), signed_half,
            uint32_t(signed_half), signed_word, uint8_t(value), uint32_t(value), value, 0, 1};
        check(std::memcmp(interpreted.data() + 64, expected, sizeof(expected)) == 0,
              "unsigned immediate signed-load extension and halfword truncation/zero-extension");
    }
    const uint64_t compare_cases[][4] = {
        {0, 0, 0, 0}, {UINT64_MAX, 1, 0, 1},
        {UINT64_C(0x8000000000000000), 1, UINT64_C(0x8000000000000000), 1},
        {UINT64_C(0x7fffffffffffffff), 1, UINT64_C(0x7fffffffffffffff), UINT64_MAX},
        {UINT64_C(0x80000000), UINT64_MAX, 2, 1}, {UINT64_C(0x7fffffff), 1, UINT64_MAX, 0},
        {17, UINT64_C(0xabcdef1280018080), 0, UINT64_MAX}};
    const auto compare_code = fixture(argv[1], "conditionalcompare");
    const auto extended_code = fixture(argv[1], "extended");
    for (const auto &args : compare_cases) {
        const uint64_t flags = reference_arithmetic_flags(args[2], args[3], 64, true);
        const bool n = flags & 8, z = flags & 4, c = flags & 2, v = flags & 1;
        std::array<uint64_t, 2048> native_compare{}, interpreted_compare{}, expected_compare{};
        auto p = make(compare_code, native_compare.data(), sizeof(native_compare));
        for (unsigned i = 0; i < 4; ++i) satchel_cpu_probe_set_x(p.get(), i + 1, args[i]);
        probe_conditionalcompare(native_compare.data(), args[0], args[1], args[2], args[3]);
        auto report = satchel_cpu_probe_run(p.get(), 30000, nullptr, nullptr);
        check(report.reason == SATCHEL_CPU_SVC && report.svc == 0x42, "conditional-compare matrix completes");
        check(satchel_cpu_probe_read(p.get(), data_address, interpreted_compare.data(), sizeof(interpreted_compare)) &&
              interpreted_compare == native_compare, "native CCMP/CCMN all conditions/NZCV literals/forms/widths");
        size_t index = 0;
        for (unsigned condition = 0; condition < 16; ++condition) {
            for (unsigned literal = 0; literal < 16; ++literal) {
                for (unsigned operation = 0; operation < 8; ++operation) {
                    const unsigned width = (operation & 2) ? 32 : 64;
                    const uint64_t rhs = operation < 4 ? args[1] : (operation == 4 || operation == 7) ? 0 : 31;
                    expected_compare[index++] = reference_condition(condition, n, z, c, v)
                        ? reference_arithmetic_flags(args[0], rhs, width, !(operation & 1)) : literal;
                }
            }
        }
        check(interpreted_compare == expected_compare, "conditional compare selects arithmetic or exact fallback NZCV");
        std::array<uint64_t, 260> native_extended{}, interpreted_extended{}, expected_extended{};
        auto ext = make(extended_code, native_extended.data(), sizeof(native_extended));
        for (unsigned i = 0; i < 4; ++i) satchel_cpu_probe_set_x(ext.get(), i + 1, args[i]);
        probe_extended(native_extended.data(), args[0], args[1], args[2], args[3]);
        report = satchel_cpu_probe_run(ext.get(), 3000, nullptr, nullptr);
        check(report.reason == SATCHEL_CPU_SVC && report.svc == 0x42, "extended-register matrix completes");
        check(satchel_cpu_probe_read(ext.get(), data_address, interpreted_extended.data(), sizeof(interpreted_extended)) &&
              interpreted_extended == native_extended, "native ADD/SUB all extensions/widths/flags/SP/shift limits");
        index = 0;
        for (unsigned extension = 0; extension < 8; ++extension) {
            for (unsigned amount : {0u, 4u}) {
                for (unsigned width : {32u, 64u}) {
                    for (unsigned operation = 0; operation < 4; ++operation) {
                        const uint64_t a = width == 64 ? args[0] : uint32_t(args[0]);
                        const uint64_t b = reference_extend(args[1], extension, width, amount);
                        uint64_t result = (operation & 1) ? a - b : a + b;
                        if (width == 32) result = uint32_t(result);
                        expected_extended[index++] = result;
                        expected_extended[index++] = operation >= 2
                            ? reference_arithmetic_flags(a, b, width, operation & 1) : flags;
                    }
                }
            }
        }
        expected_extended[256] = 0; expected_extended[257] = 6;
        expected_extended[258] = 0; expected_extended[259] = 0;
        check(interpreted_extended == expected_extended, "extended arithmetic matches modular extension and exact NZCV");
    }
    uint64_t atomic_value = 41;
    auto atomic = make(fixture(argv[1], "exclusive"), &atomic_value, sizeof(atomic_value));
    auto atomic_expected = probe_exclusive(&atomic_value); expect_stop(atomic.get(), SATCHEL_CPU_SVC);
    check(x(atomic.get(), 0) == atomic_expected, "single-thread exclusive native equivalence");
    uint64_t atomic_actual = 0; satchel_cpu_probe_read(atomic.get(), data_address, &atomic_actual, 8);
    check(atomic_actual == atomic_value, "exclusive memory equivalence");
    auto adr = make(fixture(argv[1], "address")); expect_stop(adr.get(), SATCHEL_CPU_SVC);
    check(x(adr.get(), 0) == code_address + 8, "ADR produces guest address");
    check(x(adr.get(), 1) == code_address, "ADRP produces guest page");
    auto svc = make(words({0xd4000aa1, 0x91000400, 0xd4000ac1}));
    auto s = satchel_cpu_probe_run(svc.get(), 10, nullptr, nullptr);
    check(s.reason == SATCHEL_CPU_SVC && s.svc == 0x55 && s.steps == 1 && s.pc == code_address, "SVC captures boundary");
    satchel_cpu_probe_set_x(svc.get(), 0, 8); s = satchel_cpu_probe_run(svc.get(), 10, nullptr, nullptr);
    check(s.reason == SATCHEL_CPU_SVC && s.svc == 0x56 && x(svc.get(), 0) == 9, "SVC resumes with HLE register result");
    auto loop = make(words({0x14000000}));
    s = satchel_cpu_probe_run(loop.get(), 37, nullptr, nullptr);
    check(s.reason == SATCHEL_CPU_BUDGET && s.steps == 37, "instruction budget");
    int polls = 0;
    s = satchel_cpu_probe_run(loop.get(), 100, [](void *p) -> int { return ++*static_cast<int *>(p) == 4; }, &polls);
    check(s.reason == SATCHEL_CPU_CANCELLED && s.steps == 3, "cancellation at instruction boundary");
    auto testbranch_loop = make(words({0x3600001f}));
    s = satchel_cpu_probe_run(testbranch_loop.get(), 17, nullptr, nullptr);
    check(s.reason == SATCHEL_CPU_BUDGET && s.steps == 17 && s.pc == code_address, "TBZ zero-offset respects instruction budget");
    for (uint32_t offset : {0x1fffu, 0x2000u}) {
        auto p = make(words({0x3600001fu | (offset << 5)}));
        expect_stop(p.get(), SATCHEL_CPU_MEMORY_FAULT);
    }
    for (uint32_t op : {0xd4400000u, 0xd4200000u, 0xd538d080u, 0xffffffffu, 0x52c00000u,
                       0x0ac0f800u, 0x91800000u}) {
        auto p = make(words({op})); expect_stop(p.get(), SATCHEL_CPU_UNSUPPORTED);
        s = satchel_cpu_probe_run(p.get(), 1, nullptr, nullptr);
        check(s.reason == SATCHEL_CPU_UNSUPPORTED && s.steps == 0, "unsupported fault stays terminal");
    }
    for (uint64_t address : {uint64_t{0}, UINT64_MAX, data_address + 60}) {
        auto p = make(words({0xf9400001})); satchel_cpu_probe_set_x(p.get(), 0, address);
        expect_stop(p.get(), SATCHEL_CPU_MEMORY_FAULT);
    }
    auto write_code = make(words({0xf9000001})); satchel_cpu_probe_set_x(write_code.get(), 0, code_address);
    expect_stop(write_code.get(), SATCHEL_CPU_MEMORY_FAULT);
    auto indirect = make(words({0xd61f0000}));
    satchel_cpu_probe_set_x(indirect.get(), 0, reinterpret_cast<uint64_t>(&probe_arithmetic));
    expect_stop(indirect.get(), SATCHEL_CPU_MEMORY_FAULT);
    auto unaligned_pc = make(words({0xd61f0000})); satchel_cpu_probe_set_x(unaligned_pc.get(), 0, code_address + 1);
    expect_stop(unaligned_pc.get(), SATCHEL_CPU_MEMORY_FAULT);
    auto fallthrough = make(words({0xd503201f})); expect_stop(fallthrough.get(), SATCHEL_CPU_MEMORY_FAULT);
    check(!satchel_cpu_probe_set_x(svc.get(), 31, 0), "invalid register rejected");
    check(!satchel_cpu_probe_read(svc.get(), UINT64_MAX, actual.data(), 16), "host read API bounds");
    bytes.fill(0x3a);
    auto writable = make(words({0xd4000001}), bytes.data());
    std::array<uint8_t, 64> replacement{}; replacement.fill(0xb7);
    check(satchel_cpu_probe_write(writable.get(), data_address, replacement.data(), replacement.size()), "checked full-span write API");
    check(satchel_cpu_probe_read(writable.get(), data_address, actual.data(), actual.size()) && actual == replacement,
          "write API copies exactly the requested bytes");
    for (uint64_t address : {data_address + 60, UINT64_MAX - 3, uint64_t{0}, code_address}) {
        check(!satchel_cpu_probe_write(writable.get(), address, bytes.data(), 8), "write API rejects bounds/execute mapping");
        check(satchel_cpu_probe_read(writable.get(), data_address, actual.data(), actual.size()) && actual == replacement,
              "failed write API preserves every writable byte");
    }
    check(!satchel_cpu_probe_write(nullptr, data_address, bytes.data(), bytes.size()), "write API rejects null context");
    check(!satchel_cpu_probe_write(writable.get(), data_address, nullptr, 8), "write API rejects null source");
    check(!satchel_cpu_probe_write(writable.get(), data_address, bytes.data(), SIZE_MAX), "write API rejects overflowing span");
    check(!satchel_cpu_probe_write(writable.get(), data_address, bytes.data(), 0), "write API rejects empty span");
    check(satchel_cpu_probe_read(writable.get(), data_address, actual.data(), actual.size()) && actual == replacement,
          "invalid write API arguments preserve mapped data");
    const auto code = words({0xd4000001});
    SatchelCPUProbeRegion bad[] = {{code_address, code.data(), 4, 4, 7}};
    char error[160];
    check(!satchel_cpu_probe_create(bad, 1, code_address, error, sizeof(error)), "W+X mapping rejected");
    bad[0].permissions = 5; bad[0].bytes_size = 5;
    check(!satchel_cpu_probe_create(bad, 1, code_address, error, sizeof(error)), "oversized segment input rejected");
    bad[0].bytes_size = 4; bad[0].address = UINT64_MAX - 1;
    check(!satchel_cpu_probe_create(bad, 1, code_address, error, sizeof(error)), "mapping overflow rejected");
    bad[0] = {code_address, code.data(), 4, 257 * 1024 * 1024, 5};
    check(!satchel_cpu_probe_create(bad, 1, code_address, error, sizeof(error)), "mapping allocation cap");
    SatchelCPUProbeRegion overlap[] = {{code_address, code.data(), 4, 4, 5},
                                 {code_address + 2, nullptr, 0, 16, 3}};
    check(!satchel_cpu_probe_create(overlap, 2, code_address, error, sizeof(error)), "overlapping mappings rejected");
    bad[0] = {code_address, code.data(), 4, 4, 3};
    check(!satchel_cpu_probe_create(bad, 1, code_address, error, sizeof(error)), "non-executable entry rejected");
    const uint64_t original_words[] = {UINT64_C(0x123456789abcdef0), UINT64_C(0xfedcba9876543210)};
    SatchelCPUProbeRegion write_boundaries[] = {{code_address, code.data(), 4, 4, 5},
        {data_address, &original_words[0], 8, 8, SATCHEL_CPU_READ | SATCHEL_CPU_WRITE},
        {data_address + 8, &original_words[1], 8, 8, SATCHEL_CPU_READ}};
    Probe write_boundary(satchel_cpu_probe_create(write_boundaries, 3, code_address, error, sizeof(error)), satchel_cpu_probe_destroy);
    check(bool(write_boundary), "write permission-boundary context");
    check(!satchel_cpu_probe_write(write_boundary.get(), data_address + 4, bytes.data(), 8), "write rejects RW-to-R boundary before first byte");
    check(!satchel_cpu_probe_write(write_boundary.get(), data_address + 8, bytes.data(), 8), "write rejects read-only mapping");
    check(!satchel_cpu_probe_write(write_boundary.get(), code_address, bytes.data(), 4), "write rejects readable executable mapping");
    uint64_t observed_words[2] = {};
    check(satchel_cpu_probe_read(write_boundary.get(), data_address, &observed_words[0], 8) &&
          satchel_cpu_probe_read(write_boundary.get(), data_address + 8, &observed_words[1], 8) &&
          std::memcmp(observed_words, original_words, sizeof(original_words)) == 0,
          "permission/boundary write failures preserve both mappings");
    uint32_t observed_code = 0;
    check(satchel_cpu_probe_read(write_boundary.get(), code_address, &observed_code, 4) && observed_code == 0xd4000001,
          "write API cannot modify executable code");
    // Forward/backward cross-segment branches preserve guest addresses and LR.
    const auto caller = words({0xd63f0000, 0xd4000001});
    const auto callee = words({0x91000421, 0xd65f03c0});
    SatchelCPUProbeRegion calls[] = {{code_address, caller.data(), 8, 8, 5},
                               {data_address, callee.data(), 8, 8, 5}};
    Probe call(satchel_cpu_probe_create(calls, 2, code_address, error, sizeof(error)), satchel_cpu_probe_destroy);
    check(bool(call), "cross-segment context");
    satchel_cpu_probe_set_x(call.get(), 0, data_address); satchel_cpu_probe_set_x(call.get(), 1, 41);
    expect_stop(call.get(), SATCHEL_CPU_SVC);
    check(x(call.get(), 1) == 42 && x(call.get(), 30) == code_address + 4, "BLR/RET virtual address semantics");
    for (uint32_t form : {0xa9000000u, 0xa9400000u, 0xa9800000u, 0xa9c00000u, 0xa8800000u, 0xa8c00000u,
                          0x29000000u, 0x29400000u, 0x29800000u, 0x29c00000u, 0x28800000u, 0x28c00000u}) {
        const unsigned mode = (form >> 23) & 3;
        const int offset = mode == 3 ? -16 : mode == 1 ? 16 : 0;
        const size_t width = (form & 0x80000000) ? 8 : 4;
        const uint64_t target = data_address + 64 - width;
        const uint64_t base = mode == 3 ? target + 16 : target;
        bytes.fill(0xa5);
        auto p = make(words({pair_op(form, 1, 2, 0, offset)}), bytes.data());
        satchel_cpu_probe_set_x(p.get(), 0, base); satchel_cpu_probe_set_x(p.get(), 1, 17); satchel_cpu_probe_set_x(p.get(), 2, 29);
        expect_stop(p.get(), SATCHEL_CPU_MEMORY_FAULT);
        check(x(p.get(), 0) == base, "pair fault preserves writeback base");
        check(x(p.get(), 1) == 17 && x(p.get(), 2) == 29, "pair fault preserves destination registers");
        check(satchel_cpu_probe_read(p.get(), data_address, actual.data(), actual.size()) && actual == bytes,
              "pair crossing end cannot partially write first word");
    }
    for (uint32_t form : {0xa9800000u, 0xa9c00000u, 0xa8800000u, 0xa8c00000u,
                          0x29800000u, 0x29c00000u, 0x28800000u, 0x28c00000u}) {
        for (bool second : {false, true}) {
            auto p = make(words({pair_op(form, second ? 1 : 0, second ? 0 : 2, 0, 16)}));
            expect_stop(p.get(), SATCHEL_CPU_UNSUPPORTED);
            check(x(p.get(), 0) == data_address, "writeback overlap rejected before effects");
        }
    }
    for (uint32_t form : {0xa9400000u, 0xa9c00000u, 0xa8c00000u, 0x29400000u, 0x29c00000u, 0x28c00000u}) {
        auto p = make(words({pair_op(form, 1, 1, 0, 16)})); expect_stop(p.get(), SATCHEL_CPU_UNSUPPORTED);
    }
    for (uint32_t other : {0x69000000u, 0xac000000u, 0xa8000000u, 0x28000000u, 0x69400000u}) {
        auto p = make(words({pair_op(other, 1, 2, 0, 0)})); expect_stop(p.get(), SATCHEL_CPU_UNSUPPORTED);
    }
    for (uint32_t form : {0xa9000000u, 0x29000000u}) {
        auto pair_store_code = make(words({pair_op(form, 1, 2, 0, 0), 0, 0, 0}));
        satchel_cpu_probe_set_x(pair_store_code.get(), 0, code_address);
        expect_stop(pair_store_code.get(), SATCHEL_CPU_MEMORY_FAULT);
    }
    auto pair_sp = make(words({pair_op(0xa9000000, 1, 2, 31, 0)}));
    satchel_cpu_probe_set_sp(pair_sp.get(), stack_address + 8); expect_stop(pair_sp.get(), SATCHEL_CPU_MEMORY_FAULT);
    auto repeated_store = make(words({pair_op(0xa9000000, 1, 1, 0, 0), 0xd4000001}));
    satchel_cpu_probe_set_x(repeated_store.get(), 1, 44); expect_stop(repeated_store.get(), SATCHEL_CPU_SVC);
    uint64_t stored_pair[2] = {};
    check(satchel_cpu_probe_read(repeated_store.get(), data_address, stored_pair, sizeof(stored_pair)) &&
          stored_pair[0] == 44 && stored_pair[1] == 44, "valid duplicate store source allowed");
    for (uint32_t form : {0x39000000u, 0x39400000u}) {
        for (uint64_t address : {uint64_t{0}, UINT64_MAX, data_address + 64}) {
            bytes.fill(0xa5);
            auto p = make(words({form | 1}), bytes.data()); satchel_cpu_probe_set_x(p.get(), 0, address);
            satchel_cpu_probe_set_x(p.get(), 1, 0xff);
            expect_stop(p.get(), SATCHEL_CPU_MEMORY_FAULT);
            check(satchel_cpu_probe_read(p.get(), data_address, actual.data(), actual.size()) && actual == bytes,
                  "byte bounds fault preserves guest memory");
            check(x(p.get(), 1) == 0xff && x(p.get(), 0) == address, "byte fault preserves registers");
        }
    }
    auto byte_write_code = make(words({0x39000001})); satchel_cpu_probe_set_x(byte_write_code.get(), 0, code_address);
    expect_stop(byte_write_code.get(), SATCHEL_CPU_MEMORY_FAULT);
    // Previously unsupported byte post-index forms now have explicit semantics.
    for (uint32_t byte_post : {0x38001401u, 0x38401401u}) {
        bytes.fill(0xa5);
        auto p = make(words({byte_post, 0xd4000001}), bytes.data()); satchel_cpu_probe_set_x(p.get(), 1, 0x12ff);
        expect_stop(p.get(), SATCHEL_CPU_SVC);
        check(x(p.get(), 0) == data_address + 1, "byte post-index updates base exactly once");
        if (byte_post & 0x00400000) check(x(p.get(), 1) == 0xa5, "byte post-index load zero extends");
        else {
            uint8_t first = 0;
            check(satchel_cpu_probe_read(p.get(), data_address, &first, 1) && first == 0xff, "byte post-index store truncates");
        }
    }
    prefetch_unchanged({0xf9800001},UINT64_MAX,1); // Former prefetch exclusion now asserts exact unchanged-state behavior.
    for (uint32_t other : {0x38000801u, 0x38400801u, 0xb9c00001u, 0xf9c00001u}) {
        auto p = make(words({other})); expect_stop(p.get(), SATCHEL_CPU_UNSUPPORTED);
    }
    // The last encodable unsigned byte offset (4095) must also stay bounded.
    std::array<uint8_t, 4096> page{}; page[4095] = 0xf1;
    auto byte_limit = make(words({0x39400001u | (4095u << 10), 0x39000001u | (4094u << 10), 0xd4000001}), page.data(), page.size());
    expect_stop(byte_limit.get(), SATCHEL_CPU_SVC);
    uint8_t last_bytes[2] = {};
    check(satchel_cpu_probe_read(byte_limit.get(), data_address + 4094, last_bytes, 2) &&
          last_bytes[0] == 0xf1 && last_bytes[1] == 0xf1 && x(byte_limit.get(), 1) == 0xf1,
          "maximum unsigned byte offset remains within mapping");
    for (uint32_t base_form : {0x38000000u, 0x38400000u, 0x78000000u, 0x78400000u, 0xb8000000u, 0xb8400000u, 0xf8000000u, 0xf8400000u}) {
        const size_t width = size_t{1} << (base_form >> 30);
        for (uint32_t mode : {0u, 0x400u, 0xc00u}) {
            const uint32_t form = base_form | mode;
            const uint64_t target = data_address + 65 - width;
            const bool post = mode == 0x400;
            const uint64_t base = post ? target : target + 16;
            bytes.fill(0xa5);
            auto p = make(words({scalar_op(form, 1, 0, post ? 16 : -16)}), bytes.data());
            satchel_cpu_probe_set_x(p.get(), 0, base); satchel_cpu_probe_set_x(p.get(), 1, 0xff);
            expect_stop(p.get(), SATCHEL_CPU_MEMORY_FAULT);
            check(x(p.get(), 0) == base && x(p.get(), 1) == 0xff,
                  "unscaled boundary fault preserves base and destination");
            check(satchel_cpu_probe_read(p.get(), data_address, actual.data(), actual.size()) && actual == bytes,
                  "unscaled boundary fault preserves entire memory");
            if (mode) {
                auto overlap_p = make(words({scalar_op(form, 0, 0, 0)}));
                expect_stop(overlap_p.get(), SATCHEL_CPU_UNSUPPORTED);
                check(x(overlap_p.get(), 0) == data_address, "scalar overlapping writeback rejected before effects");
            }
            if (!(base_form & 0x00400000)) {
                auto read_only = make(words({scalar_op(form, 1, 0, post ? 16 : -16), 0, 0, 0}));
                satchel_cpu_probe_set_x(read_only.get(), 0, post ? code_address : code_address + 16);
                expect_stop(read_only.get(), SATCHEL_CPU_MEMORY_FAULT);
            }
        }
    }
    // Privileged/unprivileged, signed and SIMD register-offset paths
    // must remain outside the expansion, even when adjacent encodings are valid.
    for (uint32_t other : {0xb8000801u, 0xf8400801u, 0x38800001u, 0x38c00001u,
                           0x78000801u, 0x78400801u, 0x78800001u, 0x78c00001u,
                           0x3c200801u}) {
        auto p = make(words({other})); expect_stop(p.get(), SATCHEL_CPU_UNSUPPORTED);
    }
    auto bad_scalar_sp = make(words({scalar_op(0xf8000000, 1, 31, 0)}));
    satchel_cpu_probe_set_sp(bad_scalar_sp.get(), stack_address + 1);
    expect_stop(bad_scalar_sp.get(), SATCHEL_CPU_MEMORY_FAULT);
    auto stack_zero = make(words({scalar_op(0xf8000c00, 31, 31, -16), scalar_op(0xf8400400, 1, 31, 16), 0xd4000001}));
    expect_stop(stack_zero.get(), SATCHEL_CPU_SVC);
    check(x(stack_zero.get(), 1) == 0, "SP base does not alias XZR writeback source");
    uint64_t alias_value = 55;
    auto offset_alias = make(words({scalar_op(0xf8400000, 0, 0, 0), 0xd4000001}), &alias_value, 8);
    expect_stop(offset_alias.get(), SATCHEL_CPU_SVC);
    check(x(offset_alias.get(), 0) == 55, "unscaled offset allows base/destination alias without writeback");
    page.fill(0); page[511] = 0xa5;
    auto signed_limits = make(words({scalar_op(0xf8000000, 1, 0, -256), scalar_op(0xf8400000, 2, 0, 255), 0xd4000001}), page.data(), page.size());
    satchel_cpu_probe_set_x(signed_limits.get(), 0, data_address + 256);
    satchel_cpu_probe_set_x(signed_limits.get(), 1, UINT64_C(0x123456789abcdef0));
    expect_stop(signed_limits.get(), SATCHEL_CPU_SVC);
    uint64_t first_word = 0;
    check(satchel_cpu_probe_read(signed_limits.get(), data_address, &first_word, 8) &&
          first_word == UINT64_C(0x123456789abcdef0) && x(signed_limits.get(), 2) == 0xa5,
          "signed unscaled limits and unaligned X load semantics");
    for (uint32_t other : {0x1b200000u, 0x9b600000u, 0x9be00000u, 0x9b400000u,
                           0x9bc00000u, 0x3b000000u, 0x5b000000u, 0x1b800000u}) {
        auto p = make(words({other})); expect_stop(p.get(), SATCHEL_CPU_UNSUPPORTED);
    }
    auto multiply_alias = make(words({0x9b010000, 0x9b01fc02, 0x9b01001f, 0x910003e4, 0xd4000001}));
    satchel_cpu_probe_set_x(multiply_alias.get(), 0, UINT64_MAX);
    satchel_cpu_probe_set_x(multiply_alias.get(), 1, 2);
    expect_stop(multiply_alias.get(), SATCHEL_CPU_SVC);
    check(x(multiply_alias.get(), 0) == UINT64_MAX - 2, "MADD destination/accumulator/source alias reads old values");
    check(x(multiply_alias.get(), 2) == 6, "MNEG uses zero accumulator and modular subtraction");
    check(x(multiply_alias.get(), 4) == stack_address + 128, "MADD XZR destination does not change SP");
    for (uint32_t form : {0x38600800u, 0x78600800u, 0xb8600800u, 0xf8600800u, 0xb8a00800u,
                          0x38a00800u, 0x38e00800u, 0x78a00800u, 0x78e00800u}) {
        const size_t width = size_t{1} << (form >> 30);
        for (unsigned extend : {2u, 3u, 6u, 7u}) {
            for (bool scaled : {false, true}) {
                bytes.fill(0xfe);
                auto p = make(words({register_load(form, 1, 0, 2, extend, scaled), 0xd4000001}), bytes.data());
                const uint64_t index = extend == 2 ? UINT64_C(0xabcdef0000000002) :
                    extend == 6 ? UINT64_C(0x12345678fffffffe) : extend == 7 ? UINT64_MAX - 1 : 2;
                satchel_cpu_probe_set_x(p.get(), 0, data_address + 32); satchel_cpu_probe_set_x(p.get(), 2, index);
                satchel_cpu_probe_set_x(p.get(), 1, UINT64_MAX);
                expect_stop(p.get(), SATCHEL_CPU_SVC);
                uint64_t expected = 0; std::memcpy(&expected, bytes.data(), width);
                if (form & 0x00800000) {
                    expected |= ~((uint64_t{1} << (width * 8)) - 1); // 0xfe.. has a set sign bit.
                    if (form & 0x00400000) expected = uint32_t(expected);
                }
                check(x(p.get(), 1) == expected && x(p.get(), 0) == data_address + 32 && x(p.get(), 2) == index,
                      "register extension/scaling produces correct read without writeback");
                check(satchel_cpu_probe_read(p.get(), data_address, actual.data(), actual.size()) && actual == bytes,
                      "register-offset loads leave memory unchanged");
            }
        }
        for (unsigned extend : {0u, 1u, 4u, 5u}) {
            auto p = make(words({register_load(form, 1, 0, 2, extend, true)}));
            expect_stop(p.get(), SATCHEL_CPU_UNSUPPORTED);
        }
        for (uint64_t base : {uint64_t{0}, UINT64_MAX, data_address + 65 - width}) {
            auto p = make(words({register_load(form, 1, 0, 2, 3, false)}));
            satchel_cpu_probe_set_x(p.get(), 0, base); satchel_cpu_probe_set_x(p.get(), 2, 0);
            satchel_cpu_probe_set_x(p.get(), 1, 0x777);
            expect_stop(p.get(), SATCHEL_CPU_MEMORY_FAULT);
            check(x(p.get(), 1) == 0x777 && x(p.get(), 0) == base, "register load fault preserves base/destination");
        }
    }
    prefetch_unchanged({0xf8a06801},UINT64_MAX,1); // Existing register-hint instruction retained as a positive.
    for (uint32_t other : {0xb8e06801u, 0xf8e06801u, 0x3c200801u, 0x3c600801u,
                           0x38606401u, 0x38606c01u}) {
        auto p = make(words({other})); expect_stop(p.get(), SATCHEL_CPU_UNSUPPORTED);
    }
    for (uint32_t form : {0x38200800u, 0x78200800u, 0xb8200800u, 0xf8200800u}) {
        const size_t width = size_t{1} << (form >> 30);
        for (unsigned extend : {2u, 3u, 6u, 7u}) {
            for (bool scaled : {false, true}) {
                bytes.fill(0xa5);
                auto p = make(words({register_load(form, 1, 0, 2, extend, scaled), 0xd4000001}), bytes.data());
                const uint64_t index = extend == 2 ? UINT64_C(0xabcdef0000000002) :
                    extend == 6 ? UINT64_C(0x12345678fffffffe) : extend == 7 ? UINT64_MAX - 1 : 2;
                const uint64_t value = UINT64_C(0x0123456789abcdef);
                satchel_cpu_probe_set_x(p.get(), 0, data_address + 32); satchel_cpu_probe_set_x(p.get(), 2, index);
                satchel_cpu_probe_set_x(p.get(), 1, value); expect_stop(p.get(), SATCHEL_CPU_SVC);
                const int position = 32 + (extend >= 6 ? -2 : 2) * int(scaled ? width : 1);
                std::memcpy(bytes.data() + position, &value, width);
                check(satchel_cpu_probe_read(p.get(), data_address, actual.data(), actual.size()) && actual == bytes,
                      "register store writes only its checked width with correct extension/scale");
                check(x(p.get(), 0) == data_address + 32 && x(p.get(), 1) == value && x(p.get(), 2) == index,
                      "register stores do not write back or change source/index");
            }
        }
        for (unsigned extend : {0u, 1u, 4u, 5u}) {
            auto p = make(words({register_load(form, 1, 0, 2, extend, true)}));
            expect_stop(p.get(), SATCHEL_CPU_UNSUPPORTED);
        }
        for (uint64_t base : {uint64_t{0}, UINT64_MAX, data_address + 65 - width}) {
            bytes.fill(0xa5);
            auto p = make(words({register_load(form, 1, 0, 31, 3, false)}), bytes.data());
            satchel_cpu_probe_set_x(p.get(), 0, base); satchel_cpu_probe_set_x(p.get(), 1, UINT64_MAX);
            expect_stop(p.get(), SATCHEL_CPU_MEMORY_FAULT);
            check(satchel_cpu_probe_read(p.get(), data_address, actual.data(), actual.size()) && actual == bytes,
                  "register store bounds failure preserves whole destination");
        }
        auto p = make(words({register_load(form, 1, 0, 31, 3, false), 0, 0, 0}));
        satchel_cpu_probe_set_x(p.get(), 0, code_address); expect_stop(p.get(), SATCHEL_CPU_MEMORY_FAULT);
    }
    auto register_sp = make(words({register_load(0xb8a00800, 1, 31, 31, 3, false)}));
    satchel_cpu_probe_set_sp(register_sp.get(), stack_address + 1);
    expect_stop(register_sp.get(), SATCHEL_CPU_MEMORY_FAULT);
    const auto protected_code = words({register_load(0xb8a00800, 1, 0, 31, 3, false)});
    const uint32_t protected_word = 0x80000000;
    SatchelCPUProbeRegion no_read_regions[] = {{code_address, protected_code.data(), 4, 4, 5},
        {data_address, &protected_word, 4, 4, SATCHEL_CPU_WRITE}};
    Probe no_read(satchel_cpu_probe_create(no_read_regions, 2, code_address, error, sizeof(error)), satchel_cpu_probe_destroy);
    check(bool(no_read), "write-only diagnostic mapping");
    satchel_cpu_probe_set_x(no_read.get(), 0, data_address);
    expect_stop(no_read.get(), SATCHEL_CPU_MEMORY_FAULT);
    for (uint32_t other : {0x5ac00400u, 0x5ac00800u, 0x5ac01400u, 0xdac10000u, 0x7ac00000u, 0x5ac02000u}) {
        auto p = make(words({other})); expect_stop(p.get(), SATCHEL_CPU_UNSUPPORTED);
    }
    auto bit_alias = make(words({0xdac00000, 0xdac01000, 0xdac0001f, 0x910003e4, 0xd4000001}));
    satchel_cpu_probe_set_x(bit_alias.get(), 0, 1); expect_stop(bit_alias.get(), SATCHEL_CPU_SVC);
    check(x(bit_alias.get(), 0) == 0 && x(bit_alias.get(), 4) == stack_address + 128,
          "RBIT/CLZ source-destination alias and XZR do not change SP");
    for (uint32_t other : {0x1ac01000u, 0x1ac01400u, 0x9ac03000u, 0x1ac04000u, 0x3ac02000u, 0x1ac06000u}) {
        auto p = make(words({other})); expect_stop(p.get(), SATCHEL_CPU_UNSUPPORTED);
    }
    auto shift_alias = make(words({0x9ac22022, 0x9ac22442, 0xd4000001}));
    satchel_cpu_probe_set_x(shift_alias.get(), 1, 1); satchel_cpu_probe_set_x(shift_alias.get(), 2, 63);
    expect_stop(shift_alias.get(), SATCHEL_CPU_SVC);
    check(x(shift_alias.get(), 2) == UINT64_C(0x8000000000000000), "variable shifts read aliased count/source before writing");
    for (uint32_t other : {0x1a800800u, 0x3a800000u, 0x1a400000u, 0x1e200c00u}) {
        auto p = make(words({other})); expect_stop(p.get(), SATCHEL_CPU_UNSUPPORTED);
    }
    auto division_alias = make(words({0x9ac20822, 0x9ac20c42, 0x9ac2083f, 0x910003e4, 0xd4000001}));
    satchel_cpu_probe_set_x(division_alias.get(), 1, 100); satchel_cpu_probe_set_x(division_alias.get(), 2, 4);
    expect_stop(division_alias.get(), SATCHEL_CPU_SVC);
    check(x(division_alias.get(), 2) == 1 && x(division_alias.get(), 4) == stack_address + 128,
          "division destination/divisor/source aliases and XZR preserve SP");
    for (uint32_t other : {0x3ac00800u, 0x5ac00800u, 0x1ac01800u, 0x1ae00800u}) {
        auto p = make(words({other})); expect_stop(p.get(), SATCHEL_CPU_UNSUPPORTED);
    }
    for (uint32_t form : {0x79000000u, 0x79400000u, 0x39800000u, 0x39c00000u,
                          0x79800000u, 0x79c00000u, 0xb9800000u}) {
        const size_t width = size_t{1} << (form >> 30);
        const bool load = form & 0x00c00000;
        std::vector<uint8_t> maximum(width * 4096, 0xfe);
        auto p = make(words({form | (4095u << 10) | 1u, 0xd4000001}), maximum.data(), maximum.size());
        const uint64_t source = UINT64_C(0x0123456789abcdef);
        satchel_cpu_probe_set_x(p.get(), 1, source); expect_stop(p.get(), SATCHEL_CPU_SVC);
        if (load) {
            uint64_t expected = 0; std::memcpy(&expected, maximum.data(), width);
            if (form & 0x00800000) {
                expected |= ~((uint64_t{1} << (width * 8)) - 1);
                if (form & 0x00400000) expected = uint32_t(expected);
            }
            check(x(p.get(), 1) == expected, "maximum scaled immediate signed/unsigned load value");
        } else {
            std::vector<uint8_t> changed(maximum.size());
            std::memcpy(maximum.data() + width * 4095, &source, width);
            check(satchel_cpu_probe_read(p.get(), data_address, changed.data(), changed.size()) && changed == maximum,
                  "maximum scaled halfword store preserves all other bytes");
        }
        bytes.fill(0xa5);
        auto crossing = make(words({form | 1}), bytes.data());
        const uint64_t crossing_address = data_address + 65 - width;
        satchel_cpu_probe_set_x(crossing.get(), 0, crossing_address); satchel_cpu_probe_set_x(crossing.get(), 1, 0x777);
        expect_stop(crossing.get(), SATCHEL_CPU_MEMORY_FAULT);
        check(satchel_cpu_probe_read(crossing.get(), data_address, actual.data(), actual.size()) && actual == bytes &&
              x(crossing.get(), 0) == crossing_address && x(crossing.get(), 1) == 0x777,
              "immediate signed/halfword boundary faults preserve memory/registers");
        auto misaligned_sp = make(words({form | (31u << 5) | 1u}));
        satchel_cpu_probe_set_sp(misaligned_sp.get(), stack_address + 1);
        expect_stop(misaligned_sp.get(), SATCHEL_CPU_MEMORY_FAULT);
    }
    auto halfword_code = make(words({0x79000001})); satchel_cpu_probe_set_x(halfword_code.get(), 0, code_address);
    expect_stop(halfword_code.get(), SATCHEL_CPU_MEMORY_FAULT);
    prefetch_unchanged({0xf9800001},UINT64_MAX,1); // Former prefetch exclusion now asserts exact unchanged-state behavior.
    for (uint32_t other : {0xb9c00001u, 0xf9c00001u, 0x7d400001u, 0xbd400001u}) {
        auto p = make(words({other})); expect_stop(p.get(), SATCHEL_CPU_UNSUPPORTED);
    }
    for (uint32_t other : {0x3a400400u, 0x3a400010u, 0x3a600000u, 0x1a400800u, 0x3a400c00u,
                           0x0b600000u, 0x0ba00000u, 0x0be00000u}) {
        auto p = make(words({other})); expect_stop(p.get(), SATCHEL_CPU_UNSUPPORTED);
    }
    for (uint32_t sf : {0u, 0x80000000u}) {
        for (uint32_t operation : {0u, 0x40000000u, 0x20000000u, 0x60000000u}) {
            for (unsigned amount : {5u, 6u, 7u}) {
                auto p = make(words({0x0b220025 | sf | operation | (amount << 10)}));
                expect_stop(p.get(), SATCHEL_CPU_UNSUPPORTED);
            }
        }
    }
    // WSP writes are tested only inside the bounded interpreter; truncating
    // the actual Mac stack pointer in the native comparison would be invalid.
    auto wsp = make(words({0x0b2243ff, 0x910003e4, 0xd4000001}));
    satchel_cpu_probe_set_sp(wsp.get(), UINT64_C(0x1234567800010000)); satchel_cpu_probe_set_x(wsp.get(), 2, 16);
    expect_stop(wsp.get(), SATCHEL_CPU_SVC);
    check(x(wsp.get(), 4) == 0x10010, "WSP destination zero-extends without changing host SP");
    // Deterministic malformed-encoding/address smoke fuzz under ASan + UBSan.
    // This is a regression test, not an exhaustive proof of decoder safety.
    uint32_t random = 0x963a5c71;
    const uint32_t families[] = {0x11000000, 0x0b000000, 0x0a000000, 0x12800000,
        0x13000000, 0x12000000, 0xf9400000, 0xf9000000, 0x3dc00000, 0x3d800000,
        0xc85f7c00, 0xc8007c00, 0x4e208400, 0x1e602800, 0x1e600800, 0x91800000};
    for (unsigned i = 0; i < 1024; ++i) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        uint32_t op = (i & 1) ? random : families[(i / 2) % 16] | (random & 0x001f03ff);
        auto p = make(words({op}));
        for (unsigned reg = 0; reg < 31; ++reg)
            satchel_cpu_probe_set_x(p.get(), reg, (uint64_t(random) << 32) | (random ^ reg));
        satchel_cpu_probe_set_sp(p.get(), UINT64_MAX - 15);
        auto r = satchel_cpu_probe_run(p.get(), 1, nullptr, nullptr);
        check(r.steps <= 1 && r.reason != SATCHEL_CPU_INTERNAL_FAULT, "malformed instruction remains bounded");
    }
    for (unsigned i = 0; i < 512; ++i) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        const uint32_t op = ((i < 256) ? 0xa8000000 : 0x28000000) | (random & 0x01ffffff);
        auto p = make(words({op}));
        for (unsigned reg = 0; reg < 31; ++reg)
            satchel_cpu_probe_set_x(p.get(), reg, (uint64_t(random) << 32) | (random ^ reg));
        auto r = satchel_cpu_probe_run(p.get(), 1, nullptr, nullptr);
        check(r.steps <= 1 && r.reason != SATCHEL_CPU_INTERNAL_FAULT, "hostile pair encoding remains bounded");
    }
    for (unsigned i = 0; i < 128; ++i) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        const uint32_t op = 0x39000000 | (random & 0x007fffff);
        auto p = make(words({op}));
        for (unsigned reg = 0; reg < 31; ++reg)
            satchel_cpu_probe_set_x(p.get(), reg, (uint64_t(random) << 32) | (random ^ reg));
        auto r = satchel_cpu_probe_run(p.get(), 1, nullptr, nullptr);
        check(r.steps <= 1 && r.reason != SATCHEL_CPU_INTERNAL_FAULT, "hostile byte transfer stays bounded");
    }
    for (unsigned i = 0; i < 256; ++i) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        const uint32_t forms[] = {0x38000000, 0xb8000000, 0xf8000000};
        const uint32_t op = forms[i % 3] | (random & 0x007fffff);
        auto p = make(words({op}));
        for (unsigned reg = 0; reg < 31; ++reg)
            satchel_cpu_probe_set_x(p.get(), reg, (uint64_t(random) << 32) | (random ^ reg));
        auto r = satchel_cpu_probe_run(p.get(), 1, nullptr, nullptr);
        check(r.steps <= 1 && r.reason != SATCHEL_CPU_INTERNAL_FAULT, "hostile unscaled family stays bounded");
    }
    for (unsigned i = 0; i < 128; ++i) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        const uint32_t op = 0x1b000000 | (random & 0xe0ffffff);
        auto p = make(words({op}));
        for (unsigned reg = 0; reg < 31; ++reg)
            satchel_cpu_probe_set_x(p.get(), reg, (uint64_t(random) << 32) | (random ^ reg));
        auto r = satchel_cpu_probe_run(p.get(), 1, nullptr, nullptr);
        check(r.steps <= 1 && r.reason != SATCHEL_CPU_INTERNAL_FAULT, "multiply reserved encodings stay bounded");
    }
    for (unsigned i = 0; i < 256; ++i) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        const uint32_t forms[] = {0x38600800, 0xb8600800, 0xf8600800, 0xb8a00800};
        const uint32_t op = forms[i % 4] | (random & 0x001ff3ff);
        auto p = make(words({op}));
        for (unsigned reg = 0; reg < 31; ++reg)
            satchel_cpu_probe_set_x(p.get(), reg, (uint64_t(random) << 32) | (random ^ reg));
        auto r = satchel_cpu_probe_run(p.get(), 1, nullptr, nullptr);
        check(r.steps <= 1 && r.reason != SATCHEL_CPU_INTERNAL_FAULT, "register-offset malformed extension stays bounded");
    }
    for (unsigned i = 0; i < 256; ++i) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        const uint32_t forms[] = {0x38200800, 0x78200800, 0xb8200800, 0xf8200800,
            0x78600800, 0x38a00800, 0x38e00800, 0x78a00800, 0x78e00800};
        const uint32_t op = forms[i % 9] | (random & 0x001ff3ff);
        auto p = make(words({op}));
        for (unsigned reg = 0; reg < 31; ++reg)
            satchel_cpu_probe_set_x(p.get(), reg, (uint64_t(random) << 32) | (random ^ reg));
        auto r = satchel_cpu_probe_run(p.get(), 1, nullptr, nullptr);
        check(r.steps <= 1 && r.reason != SATCHEL_CPU_INTERNAL_FAULT, "register store/signed-load malformed extension stays bounded");
    }
    for (unsigned i = 0; i < 128; ++i) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        const bool leading = i & 1, wide = i & 2;
        const uint32_t op = 0x5ac00020 | (leading ? 0x1000 : 0) | (wide ? 0x80000000 : 0);
        const uint64_t value = (uint64_t(random) << 32) | (random ^ i);
        auto p = make(words({op})); satchel_cpu_probe_set_x(p.get(), 1, value);
        auto r = satchel_cpu_probe_run(p.get(), 1, nullptr, nullptr);
        const unsigned width = wide ? 64 : 32;
        check(r.reason == SATCHEL_CPU_BUDGET && r.steps == 1 &&
              x(p.get(), 0) == (leading ? reference_leading(value, width) : reference_reverse(value, width)),
              "unsigned bit operations remain defined for hostile bit patterns");
    }
    for (unsigned i = 0; i < 128; ++i) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        const bool wide = i & 1, is_signed = i & 2;
        const uint32_t op = 0x1ac20820 | (wide ? 0x80000000 : 0) | (is_signed ? 0x400 : 0);
        const uint64_t numerator = (uint64_t(random) << 32) | (random ^ i);
        const uint64_t denominator = i % 7 == 0 ? 0 : (uint64_t(random ^ 0xfedcba98) << 32) | (random ^ (i * 3));
        auto p = make(words({op}));
        satchel_cpu_probe_set_x(p.get(), 1, numerator); satchel_cpu_probe_set_x(p.get(), 2, denominator);
        auto r = satchel_cpu_probe_run(p.get(), 1, nullptr, nullptr);
        check(r.reason == SATCHEL_CPU_BUDGET && r.steps == 1 &&
              x(p.get(), 0) == reference_division(numerator, denominator, wide, is_signed),
              "division remains defined across hostile signed and zero operands");
    }
    for (unsigned i = 0; i < 128; ++i) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        const uint32_t forms[] = {0x79000000, 0x79400000, 0x39800000, 0x39c00000, 0x79800000, 0x79c00000, 0xb9800000};
        const uint32_t op = forms[i % 7] | (random & 0x003fffff);
        auto p = make(words({op}));
        for (unsigned reg = 0; reg < 31; ++reg)
            satchel_cpu_probe_set_x(p.get(), reg, (uint64_t(random) << 32) | (random ^ reg));
        auto r = satchel_cpu_probe_run(p.get(), 1, nullptr, nullptr);
        check(r.steps <= 1 && r.reason != SATCHEL_CPU_INTERNAL_FAULT, "signed/halfword immediate transfer remains bounded");
    }
    for (unsigned i = 0; i < 256; ++i) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        const uint32_t op = (i & 1) ? 0x3a400000 | (random & 0xc01fffff)
                                    : 0x0b200000 | (random & 0xe01fffff);
        auto p = make(words({op}));
        for (unsigned reg = 0; reg < 31; ++reg)
            satchel_cpu_probe_set_x(p.get(), reg, (uint64_t(random) << 32) | (random ^ reg));
        auto r = satchel_cpu_probe_run(p.get(), 1, nullptr, nullptr);
        check(r.steps <= 1 && r.reason != SATCHEL_CPU_INTERNAL_FAULT, "conditional/extended reserved encodings remain bounded");
    }
    std::cout << checks << " CPU probe checks passed (original fixtures; no game compatibility claim).\n";
}
