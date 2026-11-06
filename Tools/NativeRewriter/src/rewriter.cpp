// Native translation of NX2Apple dm/tools/patch_nso.py. See licenses/NX2Apple-MIT.txt.
#include "NativeRewriter.h"
#include <CommonCrypto/CommonDigest.h>
#include <algorithm>
#include <array>
#include <capstone/capstone.h>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
using Bytes = std::vector<uint8_t>;
using Addr = int64_t;
using AS = std::set<Addr>;
using RS = std::set<std::string>;
struct Failure {
    nr_status status;
    Addr at;
    const char *message;
};
static void require(bool ok, const char *s, Addr at = 0, nr_status code = NR_ASSERTION) {
    if (!ok)
        throw Failure{code, at, s};
}
static std::string hx(Addr n) {
    std::ostringstream s;
    if (n < 0) {
        s << "-";
        n = -n;
    }
    s << "0x" << std::hex << n;
    return s.str();
}
static std::string hexbytes(const uint8_t *p, size_t n) {
    static const char h[] = "0123456789abcdef";
    std::string s;
    s.reserve(2 * n);
    for (size_t i = 0; i < n; i++) {
        s += h[p[i] >> 4];
        s += h[p[i] & 15];
    }
    return s;
}
static std::string wordhex(uint32_t w) {
    std::ostringstream s;
    s << std::hex;
    s.width(8);
    s.fill('0');
    s << w;
    return s.str();
}
static bool begins(const std::string &s, const std::string &p) {
    return s.compare(0, p.size(), p) == 0;
}
static bool one(const std::string &s, std::initializer_list<const char *> v) {
    for (auto x : v)
        if (s == x)
            return true;
    return false;
}
template <class T> static std::set<T> uni(std::set<T> a, const std::set<T> &b) {
    a.insert(b.begin(), b.end());
    return a;
}
template <class T> static std::set<T> sub(std::set<T> a, const std::set<T> &b) {
    for (auto &x : b)
        a.erase(x);
    return a;
}
template <class T> static bool intersects(const std::set<T> &a, const std::set<T> &b) {
    for (auto &x : a)
        if (b.count(x))
            return true;
    return false;
}
static Addr align(Addr n, Addr a) {
    return (n + a - 1) & -a;
}
static uint32_t B(Addr a, Addr t) {
    Addr d = t - a;
    require(d >= -(1 << 27) && d < (1 << 27) && d % 4 == 0, "branch out of range", a, NR_BRANCH_RANGE);
    return 0x14000000 | ((d >> 2) & 0x3ffffff);
}
static uint32_t ADRP(int r, Addr a, Addr t) {
    Addr d = ((t & ~4095LL) - (a & ~4095LL)) >> 12;
    require(d >= -(1 << 20) && d < (1 << 20), "ADRP out of range", a, NR_BRANCH_RANGE);
    return 0x90000000 | ((d & 3) << 29) | (((d >> 2) & 0x7ffff) << 5) | r;
}
static uint32_t pair(int t, int u, int n, int off, uint32_t op) {
    return op | ((((off >= 0 ? off / 8 : -((-off + 7) / 8))) & 127) << 15) | (u << 10) | (n << 5) | t;
}
static uint32_t STP(int t, int u, int n, int off) {
    return pair(t, u, n, off, 0xa9000000);
}
static uint32_t LDP(int t, int u, int n, int off) {
    return pair(t, u, n, off, 0xa9400000);
}
static uint32_t STP_PRE(int t, int u, int n, int off) {
    return pair(t, u, n, off, 0xa9800000);
}
static uint32_t LDP_POST(int t, int u, int n, int off) {
    return pair(t, u, n, off, 0xa8c00000);
}
static uint32_t STUR(int t, int n, int off) {
    return 0xf8000000 | ((off & 511) << 12) | (n << 5) | t;
}
static uint32_t LDUR(int t, int n, int off) {
    return 0xf8400000 | ((off & 511) << 12) | (n << 5) | t;
}
static uint32_t LDRX(int t, int n, int off = 0) {
    return 0xf9400000 | (off << 10) | (n << 5) | t;
}
static uint32_t STRX(int t, int n, int off = 0) {
    return 0xf9000000 | (off << 10) | (n << 5) | t;
}
static uint32_t ADDI(int t, int n, int off, int sh = 0) {
    return 0x91000000 | (sh << 22) | (off << 10) | (n << 5) | t;
}
static uint32_t SUBI(int t, int n, int off, int sh = 0) {
    return 0xd1000000 | (sh << 22) | (off << 10) | (n << 5) | t;
}
static uint32_t ADDR(int t, int n, int m) {
    return 0x8b000000 | (m << 16) | (n << 5) | t;
}
static uint32_t MRS_TPIDRRO(int t) {
    return 0xd53bd060 | t;
}
static uint32_t BR(int n) {
    return 0xd61f0000 | (n << 5);
}
static uint32_t BLR(int n) {
    return 0xd63f0000 | (n << 5);
}
static constexpr uint32_t NOP = 0xd503201f;
struct Ins {
    Addr a = 0;
    uint32_t w = 0;
    std::string m, op;
    cs_arm64 d{};
    RS reads, writes;
    std::array<std::string, 8> names;
    bool valid = false;
    Addr target() const {
        return d.operands[d.op_count - 1].imm;
    }
};
using IL = std::vector<Ins>;
static bool has18(const Ins &i) {
    static const std::regex re("\\b[xw]18\\b");
    return std::regex_search(i.op, re);
}
static bool cond(const Ins &i) {
    return one(i.m, {"cbz", "cbnz", "tbz", "tbnz"}) || begins(i.m, "b.");
}
static bool direct(const Ins &i) {
    return cond(i) || one(i.m, {"b", "bl"});
}
static bool ldex(const Ins &i) {
    return one(i.m, {"ldxr", "ldaxr", "ldxp", "ldaxp", "ldxrb", "ldaxrb", "ldxrh", "ldaxrh"});
}
static bool stex(const Ins &i) {
    return one(i.m, {"stxr", "stlxr", "stxp", "stlxp", "stxrb", "stlxrb", "stxrh", "stlxrh"});
}
static int num(const std::string &s) {
    if (s.size() > 1 && (s[0] == 'x' || s[0] == 'w') && isdigit(s[1]))
        return std::stoi(s.substr(1));
    return -1;
}
static std::string canon(std::string s) {
    if (s == "fp")
        return "x29";
    if (s == "lr")
        return "x30";
    if (s == "wsp")
        return "sp";
    return s;
}
static std::string norm(std::string s) {
    s = canon(s);
    if (s == "wzr" || s == "xzr")
        return "";
    if (num(s) >= 0)
        return "x" + s.substr(1);
    if (s.size() > 1 && std::string("bhsdqv").find(s[0]) != std::string::npos && isdigit(s[1]))
        return "v" + s.substr(1);
    return s;
}
// patch_nso.py written_w, writes_reg and writes_reg_any (nxnative f8a5618d): the general-purpose
// registers an instruction writes, as wN names, since a write of wN or xN changes the same register.
// Capstone names x30 lr and x29 fp, and lists a compare's first operand as written.
static RS writtenW(const Ins &p) {
    RS out;
    for (auto &n : p.writes) {
        int k = num(n == "lr" ? "x30" : n == "fp" ? "x29" : n);
        if (k >= 0)
            out.insert("w" + std::to_string(k));
    }
    return out;
}
static bool compares(const Ins &p) {
    return one(p.m, {"cmp", "cmn", "tst"});
}
static bool writesReg(const Ins &p, const std::string &reg) {
    return !compares(p) && writtenW(p).count("w" + reg.substr(1));
}
static bool writesRegAny(const Ins &p, const RS &regs) {
    return !compares(p) && intersects(writtenW(p), regs);
}
struct Cut {
    Addr a;
    Bytes old, now;
    std::string why;
};
struct Obs {
    Addr a;
    uint32_t id, w;
    std::string why;
};
struct Gate {
    Addr ret;
    std::string kind;
    uint32_t arg;
    Addr orig;
};
struct Node {
    std::string kind, base;
    int64_t k = 0;
    int pre = 0;
    IL ins;
    RS uses, defs;
    std::vector<Addr> succ;
};
struct Region {
    Addr head = 0;
    AS R, valid, vary;
    std::map<Addr, Node> nodes;
};
struct nr_result {
    std::array<Bytes, 7> artifacts;
};
class Engine {
    const nr_request &r;
    nr_progress progress = nullptr;
    void report(unsigned phase, uint64_t done, uint64_t total) {
        if (progress) progress(r.context, phase, done, total);
    }
    csh md = 0;
    cs_insn *scratch = nullptr;
    size_t ticks = 0;
    Bytes img, original, patch, back;
    Bytes *area = &patch;
    Addr base = 0, text = 0, ro = 0, data = 0, key = 0, po = 0, fs = 0, imageSize = 0, fileLen = 0,
         backOff = 0, front = 0, split = 0;
    bool far = false, two = false, big = false;
    std::string bid;
    std::map<Addr, Addr> functions;
    bool conflictingFunctions = false;
    std::vector<Addr> starts;
    std::vector<std::pair<Addr, Addr>> ranges, blocks;
    std::map<Addr, std::pair<std::string, int>> kinds;
    AS done;
    std::vector<IL> windows;
    using Parts = std::vector<std::pair<IL, Addr>>;
    std::vector<Parts> retries;
    std::map<Addr, std::pair<Addr, int>> jumps;
    std::map<Addr, std::vector<Addr>> tables;
    AS tailcalls;
    std::vector<Cut> cuts;
    std::vector<Obs> obs;
    std::vector<Gate> gates;
    std::map<Addr, std::vector<Addr>> spinCand;
    AS touched;
    std::map<Addr, Ins> spinCache;
    std::vector<Region> regions;
    void poll() {
        if ((++ticks & 1023) == 0 && r.cancelled && r.cancelled(r.context))
            throw Failure{NR_CANCELLED, 0, "cancelled"};
    }
    void records(size_t n) {
        require(n <= r.max_records, "record limit", 0, NR_LIMIT);
        poll();
    }
    void bounds(Addr a, size_t n) const {
        require(a >= 0 && (uint64_t)a <= img.size() && n <= img.size() - (size_t)a,
                "image access out of bounds", a, NR_INVALID_INPUT);
    }
    uint64_t rd(Addr a, int n = 4) const {
        bounds(a, n);
        uint64_t v = 0;
        for (int i = 0; i < n; i++)
            v |= uint64_t(img[a + i]) << (8 * i);
        return v;
    }
    void put(Addr a, uint32_t w) {
        bounds(a, 4);
        for (int i = 0; i < 4; i++)
            img[a + i] = w >> (8 * i);
    }
    static void putbuf(Bytes &b, size_t a, uint32_t w) {
        for (int i = 0; i < 4; i++)
            b[a + i] = w >> (8 * i);
    }
    Ins decode(uint32_t w, Addr a, bool strict = true) {
        poll();
        uint8_t b[4];
        for (int i = 0; i < 4; i++)
            b[i] = w >> (i * 8);
        const uint8_t *p = b;
        size_t n = 4;
        uint64_t pc = a;
        Ins out;
        out.a = a;
        out.w = w;
        if (!cs_disasm_iter(md, &p, &n, &pc, scratch)) {
            require(!strict, "undecodable instruction", a);
            return out;
        }
        out.valid = true;
        out.m = scratch->mnemonic;
        out.op = scratch->op_str;
        out.d = scratch->detail->arm64;
        cs_regs rr, ww;
        uint8_t nr = 0, nw = 0;
        require(cs_regs_access(md, scratch, rr, &nr, ww, &nw) == CS_ERR_OK, "register access decode", a);
        for (int i = 0; i < nr; i++)
            out.reads.insert(cs_reg_name(md, rr[i]));
        for (int i = 0; i < nw; i++)
            out.writes.insert(cs_reg_name(md, ww[i]));
        for (int i = 0; i < out.d.op_count; i++)
            if (out.d.operands[i].type == ARM64_OP_REG)
                out.names[i] = cs_reg_name(md, out.d.operands[i].reg);
        return out;
    }
    IL disfn(Addr a, Addr e) {
        require(e >= a && size_t(e - a) <= r.max_function_bytes, "function analysis limit", a, NR_LIMIT);
        bounds(a, e - a);
        IL l;
        for (; a + 4 <= e; a += 4) {
            auto i = decode(rd(a), a, false);
            if (i.valid)
                l.push_back(std::move(i));
        }
        return l;
    }
    std::set<int> regs(const Ins &i) {
        std::set<int> s;
        static const std::regex re("\\b[xw](\\d+)\\b");
        for (auto it = std::sregex_iterator(i.op.begin(), i.op.end(), re); it != std::sregex_iterator(); ++it)
            s.insert(std::stoi((*it)[1]));
        for (auto &n : uni(i.reads, i.writes))
            if (num(n) >= 0)
                s.insert(num(n));
        return s;
    }
    std::pair<int, int> pick(const std::set<int> &s) {
        std::vector<int> v;
        for (int n = 0; n < 16 && v.size() < 2; n++)
            if (!s.count(n))
                v.push_back(n);
        require(v.size() == 2, "no scratch registers");
        return {v[0], v[1]};
    }
    uint32_t subst(const Ins &i, int s) {
        static const std::regex re("\\b([xw])18\\b");
        std::string want;
        size_t last = 0;
        for (auto it = std::sregex_iterator(i.op.begin(), i.op.end(), re); it != std::sregex_iterator();
             ++it) {
            want += i.op.substr(last, it->position() - last);
            want += (*it)[1].str() + std::to_string(s);
            last = it->position() + it->length();
        }
        want += i.op.substr(last);
        std::vector<int> pos;
        for (int p : {0, 5, 10, 16})
            if (((i.w >> p) & 31) == 18)
                pos.push_back(p);
        for (int count = pos.size(); count > 0; count--)
            for (unsigned rank = (1u << pos.size()) - 1; rank > 0; rank--) {
                unsigned mask = 0;
                for (size_t bit = 0; bit < pos.size(); bit++)
                    if (rank & (1u << bit))
                        mask |= 1u << (pos.size() - 1 - bit);
                if (__builtin_popcount(mask) != count)
                    continue;
                uint32_t w = i.w;
                for (size_t j = 0; j < pos.size(); j++)
                    if (mask & (1u << j))
                        w = (w & ~(31u << pos[j])) | (s << pos[j]);
                auto d = decode(w, i.a, false);
                if (d.valid && d.m == i.m && d.op == want)
                    return w;
            }
        throw Failure{NR_ASSERTION, i.a, "register substitution failed"};
    }
    Addr here() const {
        return base + area->size();
    }
    void use(Addr a) {
        area = two && a >= split ? &back : &patch;
        base = area == &back ? backOff : po;
    }
    void emit(uint32_t w) {
        require(patch.size() + back.size() + 4 <= r.max_patch_bytes, "patch size limit", here(), NR_LIMIT);
        size_t n = area->size();
        area->resize(n + 4);
        putbuf(*area, n, w);
        poll();
    }
    void many(std::initializer_list<uint32_t> v) {
        for (auto w : v)
            emit(w);
    }
    void slot(int n, int tmp, int k) {
        emit(ADRP(tmp, here(), key));
        emit(LDRX(tmp, tmp, k));
        emit(ADDR(n, n, tmp));
    }
    void gatecall() {
        emit(MRS_TPIDRRO(30));
        slot(30, 29, 3);
        many({LDRX(30, 30), LDRX(29, 31)});
    }
    void branch(Addr a, Addr to) {
        put(a, B(a, to));
    }
    void gate(Addr off, std::string kind, uint32_t arg, std::optional<uint32_t> word = {}) {
        Addr st = here();
        emit(STP_PRE(29, 30, 31, -16));
        gatecall();
        emit(BLR(30));
        Addr ret = here();
        emit(LDP_POST(29, 30, 31, 16));
        if (word)
            emit(*word);
        emit(B(here(), off + 4));
        gates.push_back({ret, kind, arg, off});
        records(gates.size());
        branch(off, st);
    }
    uint32_t rebranch(const Ins &i, Addr pc, Addr to) {
        if (i.m == "b")
            return B(pc, to);
        Addr d = to - pc;
        int bits = one(i.m, {"tbz", "tbnz"}) ? 14 : 19;
        require(d >= -(1LL << (bits + 1)) && d < (1LL << (bits + 1)) && d % 4 == 0,
                "conditional branch out of range", pc, NR_BRANCH_RANGE);
        return (i.w & ~(((1u << bits) - 1) << 5)) | (((d >> 2) & ((1u << bits) - 1)) << 5);
    }
    bool conflict(Addr a) {
        if (kinds.count(a))
            return true;
        for (auto &w : windows)
            if (w.front().a <= a && a < w.back().a + 4)
                return true;
        return false;
    }
    std::pair<Addr, int> pointer(Addr a, int enc, Addr ehs) {
        int fmt = enc & 15, n;
        int64_t v;
        if (fmt == 3 || fmt == 11) {
            n = 4;
            v = fmt == 11 ? (int32_t)rd(a) : int64_t(rd(a));
        } else if (fmt == 0 || fmt == 4 || fmt == 12) {
            n = 8;
            v = rd(a, 8);
        } else
            throw Failure{NR_UNSUPPORTED, a, "unwind pointer encoding"};
        require(v >= -(1LL << 33) && v <= (1LL << 33), "unwind value out of range", a, NR_INVALID_INPUT);
        if ((enc & 0x70) == 0x10)
            v += a;
        else if ((enc & 0x70) == 0x30)
            v += ehs;
        return {v, n};
    }
    uint64_t uleb(Addr &p) {
        uint64_t v = 0;
        for (int sh = 0; sh < 64; sh += 7) {
            auto b = rd(p++, 1);
            v |= (b & 127) << sh;
            if (!(b & 128))
                return v;
        }
        throw Failure{NR_INVALID_INPUT, p, "invalid LEB128"};
    }
    void matchcuts();
    void boundaries();
    void scan();
    void analyze();
    void singles();
    void observe();
    void frame();
    void spins();
    void output(nr_result &);
    Addr window(const Parts &parts, bool retry);
    std::optional<Addr> tablebase(const IL &, int, const std::string &);
    std::vector<Addr> tablecands(const IL &, const std::string &);
    int halfcount(const IL &, int, Addr);
    std::vector<Addr> offsets(const IL &, int, bool);
    bool tailEnd(const IL &, int);
    bool tailPadded(const IL &, int);
    bool tailcall(const IL &, int);
    std::optional<Addr> fpsPtr;
    uint64_t pools[3] = {2048, 512, 1280};   // snowdrop_pools.DEFAULT
    Addr fpsField = 0;
    std::map<Addr, __int128> gotValues();
    void fpsTarget();
    void snowdrop();
    std::map<std::string, std::pair<Addr, Addr>> symbols();
    Ins spinins(Addr a) {
        if (a < 0 || a + 4 > text)
            return {};
        auto it = spinCache.find(a);
        if (it != spinCache.end())
            return it->second;
        auto i = decode(rd(a), a, false);
        spinCache[a] = i;
        records(spinCache.size());
        return i;
    }
    std::string regname(int n) {
        auto p = cs_reg_name(md, n);
        return p ? p : "";
    }
    std::pair<RS, RS> spinrw(const Ins &);
    std::optional<Node> rmw(Addr);
    std::optional<Node> lock(Addr);
    std::optional<Node> store(const Ins &);
    bool quick(Addr, Addr);
    Region region1(Addr, const std::vector<Addr> &, const AS &, bool);
    Region region(Addr, const std::vector<Addr> &);
    void spinemit(const Region &);

  public:
    Engine(const nr_request &q, nr_progress callback = nullptr) : r(q), progress(callback) {
        int maj, min;
        cs_version(&maj, &min);
        require(maj == 5, "Capstone major must be 5", 0, NR_UNSUPPORTED);
        require(cs_open(CS_ARCH_ARM64, CS_MODE_ARM, &md) == CS_ERR_OK, "Capstone initialization");
        cs_option(md, CS_OPT_DETAIL, CS_OPT_ON);
        scratch = cs_malloc(md);
        require(scratch, "Capstone allocation", 0, NR_ALLOCATION);
    }
    ~Engine() {
        if (scratch)
            cs_free(scratch, 1);
        if (md)
            cs_close(&md);
    }
    void run(nr_result &out) {
        require(r.abi_version == 1 || r.abi_version == NR_ABI_VERSION, "ABI version", 0, NR_INVALID_INPUT);
        // An ABI 1 request ends at context: its pool sizes are never read.
        if (r.abi_version >= 2 && (r.snowdrop_pools_mb[0] || r.snowdrop_pools_mb[1] || r.snowdrop_pools_mb[2])) {
            uint64_t sum = 0;
            for (auto mb : r.snowdrop_pools_mb) {
                require(mb > 0, "Snowdrop pool sizes", 0, NR_INVALID_INPUT);
                sum += mb;
            }
            require(sum < 4096, "Snowdrop pool sizes", 0, NR_INVALID_INPUT);
            for (int j = 0; j < 3; j++)
                pools[j] = r.snowdrop_pools_mb[j];
        }
        require(r.max_image_bytes && r.max_patch_bytes && r.max_records && r.max_function_bytes,
                "explicit limits required", 0, NR_INVALID_INPUT);
        require(r.max_image_bytes <= 0x40000000 && r.max_patch_bytes <= 0x10000000 &&
                    r.max_function_bytes <= 0x1000000 && r.max_records <= 8000000,
                "limits exceed implementation ceilings", 0, NR_LIMIT);
        require(r.module_name && strnlen(r.module_name, 64) < 64, "module name", 0, NR_INVALID_INPUT);
        require(!(r.profile.match_count && r.profile.cut_count),
                "known cuts and fallback matches are mutually exclusive", 0, NR_INVALID_INPUT);
        require(!r.profile.match_count || r.profile.match_rules, "match rules pointer", 0, NR_INVALID_INPUT);
        records(r.profile.match_count);
        require(!r.profile.cut_count || r.profile.cuts, "cuts pointer", 0, NR_INVALID_INPUT);
        require(!r.profile.observation_count || r.profile.observations, "observations pointer", 0,
                NR_INVALID_INPUT);
        require(!r.profile.capability_count || r.profile.capabilities, "capabilities pointer", 0,
                NR_INVALID_INPUT);
        records(r.profile.cut_count);
        records(r.profile.observation_count);
        records(r.profile.capability_count);
        // Bound aggregate profile data before any payload is copied or analyzed.
        size_t profileBytes = 0, profileRecords = 0;
        auto charge = [&](size_t n) {
            require(n <= r.max_patch_bytes - profileBytes, "profile data limit", 0, NR_LIMIT);
            profileBytes += n;
        };
        auto count = [&](size_t n) {
            require(n <= r.max_records - profileRecords, "profile record limit", 0, NR_LIMIT);
            profileRecords += n;
            poll();
        };
        auto string = [&](const char *p, size_t limit) {
            require(p && strnlen(p, limit) < limit, "invalid profile string", 0, NR_INVALID_INPUT);
            charge(strnlen(p, limit) + 1);
        };
        auto cutBudget = [&](const nr_cut *c, size_t n) {
            require(!n || c, "cut pointer", 0, NR_INVALID_INPUT);
            count(n);
            for (size_t j = 0; j < n; j++) {
                charge(c[j].size);
                charge(c[j].size);
                string(c[j].reason, 4096);
            }
        };
        cutBudget(r.profile.cuts, r.profile.cut_count);
        count(r.profile.observation_count);
        for (size_t j = 0; j < r.profile.observation_count; j++)
            string(r.profile.observations[j].reason, 4096);
        count(r.profile.capability_count);
        for (size_t j = 0; j < r.profile.capability_count; j++)
            string(r.profile.capabilities[j], 128);
        count(r.profile.match_count);
        for (size_t j = 0; j < r.profile.match_count; j++) {
            auto &rule = r.profile.match_rules[j];
            string(rule.name, 4096);
            count(rule.function_count);
            require(!rule.function_count || rule.functions, "match functions pointer", 0, NR_INVALID_INPUT);
            for (size_t k = 0; k < rule.function_count; k++) {
                auto &fn = rule.functions[k];
                count(fn.fingerprint_count);
                require(!fn.fingerprint_count || fn.fingerprints, "fingerprints pointer", 0,
                        NR_INVALID_INPUT);
                for (size_t z = 0; z < fn.fingerprint_count; z++)
                    string(fn.fingerprints[z], 65);
                cutBudget(fn.cuts, fn.cut_count);
            }
        }
        uint64_t end = 0;
        for (int j = 0; j < 3; j++) {
            auto &s = r.segments[j];
            require(s.offset % 4096 == 0 && s.offset >= end && s.size <= 0x7fffffff && s.offset <= 0x7fffffff,
                    "segment layout", s.offset, NR_INVALID_INPUT);
            require(!s.size || s.bytes, "segment pointer", s.offset, NR_INVALID_INPUT);
            require(s.offset + s.size <= r.max_image_bytes, "image size limit", s.offset, NR_LIMIT);
            end = s.offset + s.size;
        }
        require(r.segments[0].offset == 0 && r.segments[0].size % 4 == 0, "text layout", 0, NR_INVALID_INPUT);
        require(r.bss_size <= 0xffffffffu, "BSS size", 0, NR_INVALID_INPUT);
        img.resize(end);
        for (auto &s : r.segments)
            if (s.size)
                memcpy(img.data() + s.offset, s.bytes, s.size);
        text = r.segments[0].size;
        ro = r.segments[1].offset;
        data = r.segments[2].offset;
        original.assign(img.begin(), img.begin() + text);
        bid = hexbytes(r.build_id, 16);
        Addr near = align(align(end + r.bss_size, 0x4000) + 0x4000, 0x10000);
        far = near > 0x7000000;
        front = 0x2000000;
        big = far && text + front > 0x7ff0000;
        two = big && text > 0x7ff0000;
        require(!big, "large-text front retry and two-area layout not yet ported", text, NR_UNSUPPORTED);
        key = align(end + r.bss_size, 0x4000);
        po = far ? -front : align(end + r.bss_size + 0x4000, 0x10000);
        fileLen = align(end, 0x4000);
        imageSize = key + 0x4000;
        if (!far) {
            require(end + r.bss_size + 0x4000 <= r.max_image_bytes, "image with BSS limit", 0, NR_LIMIT);
            img.resize(end + r.bss_size + 0x4000);
        }
        if (r.profile.match_count) {
            boundaries();
            matchcuts();
            blocks.clear();
            ranges.clear();
            starts.clear();
            functions.clear();
        }
        for (size_t n = 0; n < r.profile.cut_count; n++) {
            auto &c = r.profile.cuts[n];
            require(c.size && c.original && c.replacement && c.reason && strnlen(c.reason, 4096) < 4096,
                    "cut data", c.offset, NR_INVALID_INPUT);
            bounds(c.offset, c.size);
            require(memcmp(img.data() + c.offset, c.original, c.size) == 0, "cut original bytes mismatch",
                    c.offset);
            cuts.push_back({(Addr)c.offset, Bytes(c.original, c.original + c.size),
                            Bytes(c.replacement, c.replacement + c.size), c.reason});
            memcpy(img.data() + c.offset, c.replacement, c.size);
        }
        // Until the Unreal analyzers are ported, their discovery markers fail closed.
        auto marker = [&](const char *name, int width, bool terminated = true) {
            Bytes pattern;
            for (const char *p = name; *p; p++) {
                pattern.push_back(*p);
                for (int j = 1; j < width; j++)
                    pattern.push_back(0);
            }
            if (terminated)
                for (int j = 0; j < width; j++)
                    pattern.push_back(0);
            return std::search(img.begin() + ro, img.begin() + data, pattern.begin(), pattern.end()) !=
                   img.begin() + data;
        };
        require(!marker("Binned3", 2), "Unreal allocator analysis not yet ported", ro, NR_UNSUPPORTED);
        if (std::string(r.module_name) == "main") {
            require(!marker("SwitchVirtualAddressSpaceSize", 1, false),
                    "Unreal region analysis not yet ported", ro, NR_UNSUPPORTED);
            for (auto name : {"t.MaxFPS", "rhi.SyncInterval", "nvn.SyncInterval", "r.Streaming.PoolSize",
                              "Project.FrameRateLimit"})
                require(!marker(name, 2) && !marker(name, 4),
                        "Unreal console variable analysis not yet ported", ro, NR_UNSUPPORTED);
        }
        if (std::string(r.module_name) == "main") {
            fpsTarget();
            snowdrop();
        }
        report(0, 0, uint64_t(text));
        boundaries();
        report(0, uint64_t(text), uint64_t(text));
        scan();
        analyze();
        singles(); report(3, kinds.size() * 4, kinds.size() * 4);
        report(4, 0, uint64_t(text)); observe(); report(4, uint64_t(text), uint64_t(text));
        report(5, 0, uint64_t(text)); frame(); report(5, uint64_t(text), uint64_t(text));
        spins();
        report(7, 0, uint64_t(img.size())); output(out); report(7, uint64_t(out.artifacts[NR_IMAGE].size()), uint64_t(out.artifacts[NR_IMAGE].size()));
    }
};
void Engine::boundaries() {
    Addr mod = rd(4);
    require(rd(mod) == 0x30444f4d, "no MOD0", mod);
    Addr ehs = mod + (int32_t)rd(mod + 16), ehe = mod + (int32_t)rd(mod + 20);
    bounds(ehs, 4);
    int penc = rd(ehs + 1, 1), cenc = rd(ehs + 2, 1), tenc = rd(ehs + 3, 1);
    for (int enc : {penc, cenc, tenc})
        require((enc & 15) == 3 || (enc & 15) == 11 || (enc & 15) == 4, "unsupported unwind header encoding",
                ehs, NR_UNSUPPORTED);
    Addr p = ehs + 4;
    auto q = pointer(p, penc, ehs);
    p += q.second;
    q = pointer(p, cenc, ehs);
    p += q.second;
    Addr nf = q.first;
    require(nf >= 0, "negative FDE count", p, NR_INVALID_INPUT);
    records(nf);
    std::vector<std::pair<Addr, Addr>> fdes;
    AS ss;
    for (Addr n = 0; n < nf; n++) {
        auto a = pointer(p, tenc, ehs);
        p += a.second;
        auto f = pointer(p, tenc, ehs);
        p += f.second;
        if (a.first < text)
            ss.insert(a.first);
        fdes.push_back({a.first, f.first});
    }
    starts.assign(ss.begin(), ss.end());
    std::map<Addr, int> encs;
    std::vector<std::pair<Addr, Addr>> fr;
    if (ehe - ehs >= 12 && rd(ehs, 1) == 1 && tenc != 0xff && tenc != 0)
        for (auto &f : fdes) {
            Addr cp = f.second + 4, cie = cp - rd(cp);
            int enc = 0;
            if (encs.count(cie))
                enc = encs[cie];
            else {
                Addr o = cie + 8;
                int ver = rd(o++, 1);
                std::string aug;
                while (rd(o, 1)) {
                    require(aug.size() < 64, "CIE augmentation limit", o, NR_LIMIT);
                    aug += char(rd(o++, 1));
                }
                o++;
                uleb(o);
                uleb(o);
                if (ver == 1)
                    o++;
                else
                    uleb(o);
                if (begins(aug, "z")) {
                    uleb(o);
                    for (size_t i = 1; i < aug.size(); i++) {
                        if (aug[i] == 'R')
                            enc = rd(o++, 1);
                        else if (aug[i] == 'P') {
                            int pe = rd(o++, 1);
                            o += pointer(o, pe, ehs).second;
                        } else if (aug[i] == 'L')
                            o++;
                    }
                }
                encs[cie] = enc;
            }
            auto a = pointer(f.second + 8, enc, ehs);
            auto len = pointer(f.second + 8 + a.second, enc & 15, ehs);
            fr.push_back({a.first, a.first + len.first});
        }
    std::sort(fr.begin(), fr.end());
    for (auto [a, e] : fr)
        if (a >= 0 && a < e && e <= text && a % 4 == 0 && e % 4 == 0) {
            if (functions.count(a) && functions[a] != e)
                conflictingFunctions = true;
            functions[a] = e;
        }
    bool nro = img.size() >= 0x14 && rd(0x10) == 0x304f524e;
    Addr last = nro ? 0x80 : 0;
    std::vector<std::pair<Addr, Addr>> gaps;
    for (auto [s, e] : fr) {
        if (s >= text)
            break;
        if (s > last)
            gaps.push_back({last, s});
        last = std::max(last, e);
    }
    if (last < text)
        gaps.push_back({last, text});
    if (nro)
        blocks.push_back({8, 0x80});
    for (auto [s, e] : gaps) {
        s = align(s, 4);
        e &= ~3LL;
        if (e - s < 0x1000)
            continue;
        int bad = 0;
        for (Addr a = s; a < e; a += 4)
            if (!decode(rd(a), a, false).valid)
                bad++;
        if (bad * 4 > (e - s) / 4)
            blocks.push_back({s, e});
    }
    last = 0;
    for (auto [s, e] : blocks) {
        ranges.push_back({last, s});
        last = e;
    }
    ranges.push_back({last, text});
}
void Engine::scan() {
    uint64_t complete = 0, total = 0;
    for (auto [s, e] : ranges) total += e - s;
    report(1, 0, total);
    for (auto [s, e] : ranges) {
        for (Addr a = s; a + 16 <= e; a += 4) {
            uint32_t w = rd(a);
            if ((w & 0xbffffc00) != 0x885f7c00)
                continue;
            uint32_t c = rd(a + 4), st = rd(a + 8), c2 = rd(a + 12);
            int rt = w & 31, rn = (w >> 5) & 31, rs = (st >> 16) & 31, rk = st & 31;
            auto cb = [](uint32_t x, int r, int back) {
                return (x & 0x7f00001f) == (0x35000000u | r) &&
                       ((x >> 5) & 0x7ffff) == unsigned(0x80000 - back);
            };
            bool fenced = a + 20 <= e && (rd(a + 16) == 0xd5033bbf || rd(a + 16) == 0xd50339bf);
            if (cb(c, rt, 1) && cb(c2, rs, 3) && (st & 0xbfe0fc00) == 0x88007c00 &&
                int((st >> 5) & 31) == rn && rk != 31 && rs != rn && rs != rk && !fenced)
                put(a, w | 0x8000);
        }
        for (Addr a = s; a < e; a += 4) {
            if (((a - s) & 0xffff) == 0) report(1, complete + a - s, total);
            uint32_t w = rd(a), t = w >> 24;
            Addr d = 4;
            if (t == 0x16 || t == 0x17)
                d = (Addr(w & 0x3ffffff) - (1 << 26)) * 4;
            else if ((t & 0x7e) == 0x36)
                d = w & 0x40000 ? (Addr((w >> 5) & 0x3fff) - (1 << 14)) * 4 : 4;
            else if (t == 0x54)
                d = (w & 0x800000) && !(w & 0x10) ? (Addr((w >> 5) & 0x7ffff) - (1 << 19)) * 4 : 4;
            else if ((t & 0x7e) == 0x34)
                d = w & 0x800000 ? (Addr((w >> 5) & 0x7ffff) - (1 << 19)) * 4 : 4;
            if (d >= -512 && d <= 0)
                spinCand[a + d].push_back(a);
            std::pair<std::string, int> k;
            if ((w & 0xffe0001f) == 0xd4000001)
                k = {"svc", int((w >> 5) & 65535)};
            else if ((w & 0xffffffe0) == 0xd53bd040)
                k = {"tpidr_r", 0};
            else if ((w & 0xffffffe0) == 0xd51bd040)
                k = {"tpidr_w", 0};
            else if ((w & 0xffffffe0) == 0xd53bd060)
                k = {"tpidrro_r", 0};
            else if ((w & 0xffffffe0) == 0xd53be020)
                k = {"cntpct", int(w & 31)};
            else if ((w & 0xffffffe0) == 0xd53be040)
                k = {"cntvct", int(w & 31)};
            else if ((w & 0xffffffe0) == 0xd53b0020)
                k = {"ctr", int(w & 31)};
            else if ((w & 0xffe0ffff) == 0xcb2063ff && ((w >> 16) & 31) != 18)
                k = {"sp_align", int((w >> 16) & 31)};
            else {
                auto i = decode(w, a, false);
                if (i.valid && has18(i))
                    k = {"x18", 0};
            }
            if (!k.first.empty())
                kinds[a] = k;
            records(kinds.size() + spinCand.size());
        }
        complete += e - s; report(1, complete, total);
    }
}
std::optional<Addr> Engine::tablebase(const IL &l, int k, const std::string &rb) {
    std::optional<Addr> lo;
    for (int j = k - 1; j >= std::max(0, k - 256); j--) {
        auto &p = l[j];
        if (!writesReg(p, rb))
            continue;
        std::smatch m;
        if (!lo && p.m == "add" &&
            std::regex_match(p.op, m, std::regex(rb + ", " + rb + ", #(0x[0-9a-f]+|[0-9]+)"))) {
            lo = std::stoll(m[1], nullptr, 0);
            continue;
        }
        if (lo && p.m == "adrp" && begins(p.op, rb + ","))
            return p.d.operands[1].imm + *lo;
        return {};
    }
    return {};
}
std::vector<Addr> Engine::tablecands(const IL &l, const std::string &rb) {
    AS out;
    for (size_t k = 0; k < l.size(); k++) {
        auto &p = l[k];
        if (p.m != "adrp" || !begins(p.op, rb + ","))
            continue;
        for (size_t j = k + 1; j < std::min(k + 9, l.size()); j++)
            if (writesReg(l[j], rb)) {
                std::smatch m;
                if (l[j].m == "add" &&
                    std::regex_match(l[j].op, m, std::regex(rb + ", " + rb + ", #(0x[0-9a-f]+|[0-9]+)")))
                    out.insert(p.d.operands[1].imm + std::stoll(m[1], nullptr, 0));
                break;
            }
    }
    return {out.begin(), out.end()};
}
int Engine::halfcount(const IL &l, int k, Addr base) {
    std::smatch m;
    if (l[k].m != "ldrh" ||
        !std::regex_match(l[k].op, m, std::regex("w[0-9]+, \\[(x[0-9]+), [wx]([0-9]+), (?:lsl|uxtw) #1\\]")))
        return 65536;
    std::string rb = m[1];
    RS idx = {"w" + m[2].str()};
    int kc = -1;
    Addr n = 0;
    for (int j = k - 1; j >= std::max(0, k - 32); j--) {
        auto &p = l[j];
        if (p.m == "mov" && std::regex_match(p.op, m, std::regex("(w[0-9]+), (w[0-9]+)")) &&
            idx.count(m[1])) {
            // A copy replaces the index: a check of its old value bounds nothing.
            idx.erase(m[1].str());
            idx.insert(m[2]);
            continue;
        }
        if (p.m == "cmp" && std::regex_match(p.op, m, std::regex("(w[0-9]+), #(0x[0-9a-f]+|[0-9]+)")) &&
            idx.count(m[1])) {
            kc = j;
            n = std::stoll(m[2], nullptr, 0) + 1;
            break;
        }
        if (intersects(writtenW(p), idx))
            return 65536;
    }
    if (kc < 0 || !one(l[kc + 1].m, {"b.hi", "b.hs", "b.cs"}))
        return 65536;
    auto fit = [&](Addr tb) -> int {
        if (tb < 0 || n <= 0 || uint64_t(tb + 2 * n) > img.size())
            return 0;
        int mx = 0;
        for (int j = 0; j < n; j++) {
            int v = rd(tb + 2 * j, 2);
            Addr t = base + 4 * v;
            if (t < l.front().a || t >= l.back().a + 4)
                return 0;
            mx = std::max(mx, v);
        }
        return mx + 1;
    };
    auto tb = tablebase(l, k, rb);
    if (tb)
        return fit(*tb) ? fit(*tb) : 65536;
    int count = 0, result = 0;
    for (auto b : tablecands(l, rb)) {
        int v = fit(b);
        if (v) {
            count++;
            result = v;
        }
    }
    return count == 1 ? result : 65536;
}
std::vector<Addr> Engine::offsets(const IL &l, int j, bool adr) {
    int lo = std::max(0, j - 16), ka = -1;
    std::smatch m;
    for (int q = j - 1; q >= lo; q--)
        if (l[q].m == "add" && std::regex_match(l[q].op, m, std::regex("x18, (x[0-9]+), (x[0-9]+)"))) {
            ka = q;
            break;
        }
    if (ka < 0)
        return {};
    std::string aa = m[1], bb = m[2];
    for (int attempt = 0; attempt < (adr ? 2 : 1); attempt++) {
        std::string ent = attempt ? bb : aa, rb = attempt ? aa : bb;
        int kl = -1, wa = -1;
        std::string tbreg, ix;
        for (int q = (adr ? ka : j) - 1; q >= lo; q--)
            if (l[q].m == "ldrsw" &&
                std::regex_match(l[q].op, m,
                                 std::regex(ent + ", \\[(" + (adr ? std::string("x[0-9]+") : rb) +
                                            "), x([0-9]+), lsl #2\\]"))) {
                kl = q;
                tbreg = m[1];
                ix = m[2];
                break;
            }
        if (kl < 0)
            continue;
        if (adr) {
            for (int q = ka - 1; q >= lo; q--)
                if (writesReg(l[q], rb)) {
                    wa = q;
                    break;
                }
            if (wa < 0 || l[wa].m != "adr")
                continue;
        }
        RS idx = {"w" + ix};
        int kc = -1;
        Addr n = 0;
        // jump_offsets (nxnative f8a5618d): an index changed between its check and the load
        // bounds nothing, and a masked copy may be checked instead of the index itself.
        bool changed = false;
        std::optional<std::tuple<std::string, long long, int>> masked;
        auto wname = [](const std::string &x) { return "w" + x.substr(1); };
        static const std::regex copyRe("(w[0-9]+), (w[0-9]+)"),
            andRe("([wx][0-9]+), ([wx][0-9]+), #(0x[0-9a-f]+|[0-9]+)"),
            cmpW("(w[0-9]+), #(0x[0-9a-f]+|[0-9]+)"), cmpWX("([wx][0-9]+), #(0x[0-9a-f]+|[0-9]+)");
        for (int q = kl - 1; q >= lo; q--) {
            auto &p = l[q];
            if (p.m == "mov" && std::regex_match(p.op, m, copyRe) && idx.count(m[1])) {
                idx.erase(m[1].str());
                idx.insert(m[2]);
                continue;
            }
            if (adr) {
                if (p.m == "cmp" && std::regex_match(p.op, m, cmpWX) && idx.count(wname(m[1]))) {
                    kc = q;
                    n = std::stoll(m[2], nullptr, 0) + 1;
                    break;
                }
                if (intersects(writtenW(p), idx))
                    return {};
                continue;
            }
            if (p.m == "and" && std::regex_match(p.op, m, andRe) && idx.count(wname(m[1])) && !masked) {
                masked = std::make_tuple(wname(m[2]), std::stoll(m[3], nullptr, 0), q);
                continue;
            }
            if (!(p.m == "cmp" && std::regex_match(p.op, m, cmpW))) {
                // Capstone lists only x30 as a call's write, so a call counts for the caller-saved registers.
                bool call = one(p.m, {"bl", "blr"}) &&
                            std::any_of(idx.begin(), idx.end(), [](const std::string &x) { return num(x) <= 18; });
                if (writesRegAny(p, idx) || call)
                    changed = true;
                continue;
            }
            if (idx.count(wname(m[1]))) {
                kc = q;
                n = std::stoll(m[2], nullptr, 0) + 1;
                break;
            }
            if (masked) {
                // The checked register must be the same source and the same mask, with the source
                // unchanged up to the index's own and.
                auto &[source, mask, at] = *masked;
                std::string checked = wname(m[1]);
                for (int z = q - 1; z >= lo; z--) {
                    if (!writtenW(l[z]).count(checked))
                        continue;
                    std::smatch mm;
                    if (l[z].m == "and" && std::regex_match(l[z].op, mm, andRe) && wname(mm[2]) == source &&
                        std::stoll(mm[3], nullptr, 0) == mask) {
                        bool clean = true;
                        for (int y = z + 1; y < at; y++)
                            clean &= !writtenW(l[y]).count(source);
                        if (clean) {
                            kc = q;
                            n = std::stoll(m[2], nullptr, 0) + 1;
                        }
                    }
                    break;
                }
                if (kc >= 0)
                    break;
            }
        }
        if (kc >= 0 && changed)
            return {};
        if (kc < 0)
            return {};
        records(n);
        auto targets = [&](Addr tb) {
            std::vector<Addr> v;
            if (tb < 0 || uint64_t(tb + 4 * n) > img.size())
                return v;
            for (Addr z = 0; z < n; z++)
                v.push_back((adr ? l[wa].d.operands[1].imm : tb) + (int32_t)rd(tb + 4 * z));
            return v;
        };
        auto tb = tablebase(l, kl, tbreg);
        if (tb)
            return targets(*tb);
        if (adr) {
            std::vector<std::vector<Addr>> good;
            for (auto b : tablecands(l, tbreg)) {
                auto v = targets(b);
                if (!v.empty() && std::all_of(v.begin(), v.end(), [&](Addr t) {
                        return t >= l.front().a && t < l.back().a + 4 && t % 4 == 0;
                    }))
                    good.push_back(v);
            }
            if (good.size() == 1)
                return good[0];
        }
        return {};
    }
    return {};
}
// Pinned nxnative tail_call_at_end (MIT, seedds): a terminal indirect tail call takes its pointer
// from the last x18 load. Unlike upstream, an intervening w18 write is rejected too (fails closed).
bool Engine::tailEnd(const IL &l, int j) {
    static const std::regex load("x18, \\[(x[0-9]+|sp)(, #(0x[0-9a-f]+|[0-9]+))?\\]");
    for (int q = j - 1; q >= 0; q--) {
        if (!l[q].writes.count("x18") && !l[q].writes.count("w18"))
            continue;
        return l[q].m == "ldr" && std::regex_match(l[q].op, load);
    }
    return false;
}
static const std::regex &pointerLoad() {
    static const std::regex re("x18, (x[0-9]+, )?\\[(x[0-9]+|sp)(, #(0x[0-9a-f]+|[0-9]+))?\\]");
    return re;
}
// tail_call_padded (nxnative f8a5618d): an unframed, straight-line pointer thunk ending in br x18
// and zero trap padding.
bool Engine::tailPadded(const IL &l, int j) {
    if (j + 1 >= int(l.size()))
        return false;
    for (size_t k = j + 1; k < l.size(); k++)
        if (l[k].m != "udf" || l[k].op != "#0")
            return false;
    for (size_t k = 1; k < l.size(); k++)
        if (l[k].a != l[k - 1].a + 4)
            return false;
    static const RS frame = {"sp", "wsp", "fp", "lr", "x29", "w29", "x30", "w30"};
    for (int k = 0; k < j; k++)
        if (!one(l[k].m, {"mov", "ldr", "ldrh", "ldp", "add", "str", "stp"}) || intersects(l[k].writes, frame))
            return false;
    for (int k = j - 1; k >= 0; k--) {
        if (!l[k].writes.count("x18") && !l[k].writes.count("w18"))
            continue;
        return one(l[k].m, {"ldr", "ldp"}) && std::regex_match(l[k].op, pointerLoad());
    }
    return false;
}
// tail_call (nxnative f8a5618d): after terminal or padded unframed tails, a br x18 with other code
// after it is a tail call only when the frame is provably gone. Two forms: a 16-byte frame thunk
// with only guards and argument moves and its error blocks after the br, or an ordinary function
// with sub sp first and add sp right before the br, its pair saved to and restored from one slot.
// Nothing writes sp, x29 or x30 on the way, no stack store touches the pair, and from the restore
// to the br every path runs straight. Anything not proven is rejected.
bool Engine::tailcall(const IL &l, int j) {
    int count = l.size();
    if (j + 1 == count)
        return tailEnd(l, j);
    if (tailPadded(l, j))
        return true;
    if (j < 4)
        return false;
    for (int k = 1; k < count; k++)
        if (l[k].a != l[k - 1].a + 4)
            return false;
    int load = -1;
    for (int k = j - 1; k >= 0 && load < 0; k--)
        if (l[k].writes.count("x18") || l[k].writes.count("w18"))
            load = k;
    if (load < 0 || !one(l[load].m, {"ldr", "ldp"}) || !std::regex_match(l[load].op, pointerLoad()))
        return false;
    bool thunk = l[0].m == "stp" && l[0].op == "x29, x30, [sp, #-0x10]!";
    int restore = -1, start;
    int64_t disp;
    bool saved, fpSet;
    if (thunk) {
        if (l[1].m != "mov" || l[1].op != "x29, sp")
            return false;
        if (l[j - 1].m != "ldp" || l[j - 1].op != "x29, x30, [sp], #0x10")
            return false;
        restore = j - 1, start = 2, disp = 0, saved = true, fpSet = true;
    } else {
        static const std::regex spImm("sp, sp, #(0x[0-9a-f]+|[0-9]+)"),
            pairRe("x29, x30, \\[sp(, #(0x[0-9a-f]+|[0-9]+))?\\]");
        std::smatch sub, add;
        if (!(l[0].m == "sub" && std::regex_match(l[0].op, sub, spImm)) ||
            !(l[j - 1].m == "add" && std::regex_match(l[j - 1].op, add, spImm)) ||
            std::stoll(sub[1], nullptr, 0) != std::stoll(add[1], nullptr, 0))
            return false;
        int64_t size = std::stoll(sub[1], nullptr, 0);
        for (int k = j - 2; k > 0 && restore < 0; k--)
            if (l[k].m == "ldp" && std::regex_match(l[k].op, pairRe))
                restore = k;
        if (restore < 0)
            return false;
        disp = l[restore].d.operands[l[restore].d.op_count - 1].mem.disp;
        if (disp < 0 || disp + 16 > size)
            return false;
        start = 1, saved = false, fpSet = false;
    }
    int tail = std::min(load, restore);
    Addr afterBr = l[j].a, setupEnd = l[start].a;
    std::vector<Addr> targets;
    bool guarded = false;
    for (int k = start; k < j - 1; k++) {
        auto &p = l[k];
        if (k == restore)
            continue;
        if (cond(p) || p.m == "b") {
            Addr t = p.target();
            if (!saved || k >= restore || (p.m == "b" && k >= tail))
                return false;
            if (t > afterBr)
                guarded = true;
            else if (thunk)
                return false;
            targets.push_back(t);
            continue;
        }
        if (one(p.m, {"bl", "blr"})) {
            // A saved x30, and x18 not live across the call.
            if (thunk || !saved || k >= tail)
                return false;
            continue;
        }
        if (one(p.m, {"br", "ret", "udf", "brk", "hlt", "svc", "hvc", "smc", "msr", "sys", "dc", "ic"}))
            return false;
        if (intersects(p.writes, RS{"sp", "wsp", "lr", "x30", "w30"}))
            return false;
        if (intersects(p.writes, RS{"fp", "x29", "w29"})) {
            bool setsFp = p.m == "mov" ? p.op == "x29, sp" && !disp
                                       : p.m == "add" && begins(p.op, "x29, sp, #") && p.d.operands[2].imm == disp;
            if (k > restore || fpSet || !saved || !setsFp)
                return false;
            fpSet = true;
            setupEnd = l[k + 1].a;
            continue;
        }
        if (!saved && p.m == "stp" && p.op == l[restore].op) {
            saved = true;
            setupEnd = l[k + 1].a;
            continue;
        }
        if (p.op.find('[') == std::string::npos || begins(p.m, "ldr") || begins(p.m, "ldur") ||
            begins(p.m, "ldp") || begins(p.m, "ldar") || begins(p.m, "prfm"))
            continue;
        // A stack store's bytes are checked against the saved pair; a register offset is unproven.
        if (k > restore || !one(p.m, {"str", "strb", "strh", "stur", "sturb", "sturh", "stp"}) || p.d.writeback)
            return false;
        auto &mem = p.d.operands[p.d.op_count - 1].mem;
        std::string base = regname(mem.base);
        if (!one(base, {"sp", "fp", "x29"})) {
            if (thunk)
                return false;
            continue;
        }
        if (mem.index || (base != "sp" && !fpSet))
            return false;
        int64_t at = mem.disp + (base != "sp" ? disp : 0);
        int64_t width;
        char last = p.m.back();
        if (last == 'b' || last == 'h')
            width = last == 'b' ? 1 : 2;
        else {
            std::string reg = regname(p.d.operands[0].reg);
            char c = reg.empty() ? 0 : reg[0];
            width = c == 'x' ? 8 : c == 'w' ? 4 : c == 'q' ? 16 : c == 'd' ? 8 : c == 's' ? 4 : c == 'h' ? 2 : c == 'b' ? 1 : 8;
        }
        if (p.m == "stp")
            width *= 2;
        if (at < 0 || (at < disp + 16 && at + width > disp))
            return false;
    }
    if (!saved)
        return false;
    // What follows the br never comes back to a place whose stack it cannot vouch for.
    bool leaves = false, returns = false;
    for (int k = j + 1; k < count; k++) {
        auto &p = l[k];
        if (p.m == "bl") {
            if (l.front().a <= p.target() && p.target() <= l.back().a)
                return false;
            leaves = true;
        } else if ((cond(p) || p.m == "b") && p.target() <= afterBr)
            return false;
        returns |= one(p.m, {"ret", "br", "blr"});
    }
    if (thunk && (!guarded || !leaves || !one(l.back().m, {"bl", "udf", "b"}) || returns))
        return false;
    for (auto t : targets)
        if (t < setupEnd || (l[restore].a < t && t <= afterBr))
            return false;
    return true;
}
// loader_census.py got_values (nxnative f8a5618d): {address: the address it holds once relocated}
// for the image's relative relocations (RELA R_AARCH64_RELATIVE, ABS64 and GLOB_DAT of a symbol the
// module defines, and RELR, whose addend is in place). Values keep Python's unbounded sums.
std::map<Addr, __int128> Engine::gotValues() {
    std::map<Addr, __int128> out;
    Addr mod0 = rd(4);
    if (mod0 + 4 > Addr(img.size()) || rd(mod0) != 0x30444f4d)
        return out;
    Addr dyn = mod0 + (int32_t)rd(mod0 + 4);
    std::map<int64_t, uint64_t> tags;
    for (Addr k = 0; k < 4096; k++) {
        int64_t t = rd(dyn + k * 16, 8);
        if (t == 0)
            break;
        tags.emplace(t, rd(dyn + k * 16 + 8, 8));
    }
    auto tag = [&](int64_t t) -> uint64_t {
        auto it = tags.find(t);
        return it == tags.end() ? 0 : it->second;
    };
    auto at = [&](int64_t t) {
        require(tags.count(t), "relocation table without its address", dyn, NR_INVALID_INPUT);
        return Addr(tags[t]);
    };
    for (uint64_t k = 0; k < tag(8) / 24; k++) {
        Addr e = at(7) + Addr(k * 24);
        uint64_t off = rd(e, 8), info = rd(e + 8, 8);
        int64_t add = rd(e + 16, 8);
        uint32_t type = info & 0xffffffff;
        if (type == 1027)
            out[Addr(off)] = add;
        else if ((type == 257 || type == 1025) && tags.count(6)) {
            Addr sym = Addr(tags[6]) + Addr((info >> 32) * 24) + 6;
            if (rd(sym, 2))
                out[Addr(off)] = __int128(rd(sym + 2, 8)) + add;
        }
        poll();
    }
    Addr where = 0;
    for (uint64_t k = 0; k < tag(35) / 8; k++) {
        uint64_t e = rd(at(36) + Addr(k * 8), 8);
        if (!(e & 1)) {
            where = e;
            out[where] = rd(where, 8);
            where += 8;
        } else {
            for (int b = 0; b < 63; b++)
                if ((e >> (b + 1)) & 1)
                    out[where + b * 8] = rd(where + b * 8, 8);
            where += 63 * 8;
        }
        poll();
    }
    return out;
}
// fps_target.py (nxnative f8a5618d): a target frame rate the game steps by, found in main by the
// code that reads and sets it. The field is an int32 in an object a global points to; its readers
// divide 60.0 by it (ldr sN, [xB, #F]; scvtf; fdiv by a 60.0 made by movz and fmov), and the game
// stores an immediate 30 or 60 there. At least eight readers and a store of 30 make the target;
// exactly one such field must exist. The host writes the profile's target there (hle_fpslock.c).
void Engine::fpsTarget() {
    auto word = [&](Addr a) { return uint32_t(rd(a)); };
    auto movzW = [](uint32_t w) -> std::optional<std::pair<int, uint32_t>> {
        if ((w & 0xFF800000) != 0x52800000)
            return {};
        return std::make_pair(int(w & 31), ((w >> 5) & 0xFFFF) << (16 * ((w >> 21) & 1)));
    };
    auto writes = [](uint32_t u) -> int {   // the general register an instruction writes, roughly; -1 for none
        if ((u & 0x0A000000) == 0x08000000)
            return !((u >> 22) & 1) || ((u >> 26) & 1) ? -1 : int(u & 31);
        if ((u & 0x1C000000) == 0x14000000)
            return -1;
        if ((u & 0x1F000000) == 0x1E000000 && ((u >> 29) & 1) == 0 && (u & 0xFFFFFC00) != 0x1E260000 &&
            (u & 0xFFFFFC00) != 0x9E660000)
            return -1;
        return u & 31;
    };
    auto isAdrp = [](uint32_t w) { return (w & 0x9F000000) == 0x90000000; };
    auto adrpPage = [](uint32_t w, Addr pc) {
        int64_t imm = int64_t((((w >> 5) & 0x7FFFF) << 2) | ((w >> 29) & 3)) << 12;
        if (imm & (1LL << 32))
            imm -= 1LL << 33;
        return (pc & ~Addr(0xFFF)) + imm;
    };
    std::optional<std::map<Addr, __int128>> rel;
    auto pageOf = [&](Addr i, int a) -> std::optional<Addr> {
        for (Addr k = i - 4; k > std::max<Addr>(0, i - 24); k -= 4) {
            uint32_t u = word(k);
            if (isAdrp(u) && int(u & 31) == a)
                return adrpPage(u, k);
            if (writes(u) == a)
                return {};
        }
        return {};
    };
    auto globalOf = [&](Addr i, int b) -> std::optional<__int128> {
        for (Addr k = i - 4; k > std::max<Addr>(0, i - 40); k -= 4) {
            uint32_t u = word(k);
            if (int(u & 31) == b && (u & 0xFFC00000) == 0xF9400000) {   // ldr xB, [xA, #imm]
                if ((u >> 10) & 0xFFF)
                    return {};
                int a = (u >> 5) & 31;
                for (Addr k2 = k - 4; k2 > std::max<Addr>(0, k - 32); k2 -= 4) {
                    uint32_t v = word(k2);
                    if (int(v & 31) != a)
                        continue;
                    if ((v & 0xFFC00000) == 0x91000000 && int((v >> 5) & 31) == a) {   // add xA, xA, #lo
                        auto page = pageOf(k2, a);
                        if (!page)
                            return {};
                        return __int128(*page + ((v >> 10) & 0xFFF));
                    }
                    if ((v & 0xFFC00000) == 0xF9400000 && int((v >> 5) & 31) == a) {   // ldr xA, [xA, #got]
                        auto page = pageOf(k2, a);
                        if (!page)
                            return {};
                        if (!rel)
                            rel = gotValues();
                        auto it = rel->find(*page + ((v >> 10) & 0xFFF) * 8);
                        if (it == rel->end())
                            return {};
                        return it->second;
                    }
                    return {};
                }
                return {};
            }
            if (writes(u) == b)
                return {};
        }
        return {};
    };
    std::map<std::pair<__int128, Addr>, size_t> readers;
    std::set<std::pair<__int128, Addr>> thirty;
    for (Addr i = 0; i < text; i += 4) {
        uint32_t u = word(i);
        if ((u & 0xFFC00000) == 0xBD400000) {   // ldr sT, [xB, #F]
            uint32_t t = u & 31;
            int b = (u >> 5) & 31;
            Addr f = ((u >> 10) & 0xFFF) * 4, conv = -1, div = -1, fm = -1;
            for (Addr k = i + 4; k < std::min(text, i + 24) && conv < 0; k += 4)
                if ((word(k) & 0xFFFFFFE0) == (0x5E21D800 | t << 5))   // scvtf sC, sT
                    conv = k;
            if (conv < 0)
                continue;
            uint32_t c = word(conv) & 31;
            for (Addr k = conv + 4; k < std::min(text, conv + 20) && div < 0; k += 4)
                if ((word(k) & 0xFFE0FC00) == 0x1E201800 && ((word(k) >> 16) & 31) == c)   // fdiv sD, sM, sC
                    div = k;
            if (div < 0)
                continue;
            uint32_t m = (word(div) >> 5) & 31;
            for (Addr k = div - 4; k > std::max<Addr>(0, div - 48) && fm < 0; k -= 4)
                if ((word(k) & 0xFFFFFC1F) == (0x1E270000 | m))   // fmov sM, wK
                    fm = k;
            if (fm < 0)
                continue;
            bool sixty = false;
            for (Addr k = fm - 4; k > std::max<Addr>(0, fm - 32) && !sixty; k -= 4) {
                auto z = movzW(word(k));
                sixty = z && z->first == int((word(fm) >> 5) & 31) && z->second == 0x42700000;
            }
            if (!sixty)
                continue;
            auto g = globalOf(i, b);
            if (g && *g >= data)
                readers[{*g, f}]++;
        } else if ((u & 0xFFC00000) == 0xB9000000) {   // str wK, [xB, #F]
            int reg = u & 31, b = (u >> 5) & 31;
            Addr f = ((u >> 10) & 0xFFF) * 4;
            std::optional<uint32_t> val;
            for (Addr k = i - 4; k > std::max<Addr>(0, i - 32) && !val; k -= 4) {
                auto z = movzW(word(k));
                if (z && z->first == reg)
                    val = z->second;
            }
            if (!val || (*val != 30 && *val != 60))
                continue;
            auto g = globalOf(i, b);
            if (g && *g >= data && *val == 30)
                thirty.insert({*g, f});
        }
        if ((i & 0xffff) == 0)
            poll();
    }
    std::vector<std::pair<__int128, Addr>> found;
    for (auto &[key, n] : readers)
        if (n >= 8 && thirty.count(key))
            found.push_back(key);
    if (found.size() != 1)
        return;
    require(found[0].first <= std::numeric_limits<int64_t>::max(), "frame rate target pointer out of range", 0,
            NR_UNSUPPORTED);
    fpsPtr = Addr(found[0].first);
    fpsField = found[0].second;
}
// snowdrop_pools.py (nxnative f8a5618d): Snowdrop's three-pool page allocator in main, found by its
// constructor's structure. Its 12 GB reservation becomes the pool sizes in MB (large, 512 KB pages,
// 64 KB pages) the title's profile decides on, passed in the request, or upstream's default.
void Engine::snowdrop() {
    static constexpr uint64_t G4 = 0x100000000ULL, G12 = 0x300000000ULL;
    static constexpr uint32_t W1_ZERO = 0x2A1F03E1, X2_ZERO = 0xAA1F03E2;
    static constexpr int WINDOW = 48;
    const uint64_t *SIZES = pools;
    const Addr n = text / 4;
    auto W = [&](Addr j) { return uint32_t(rd(j * 4)); };
    auto loaded = [](uint32_t w, uint64_t value) {
        if ((w & 0xFFE00000) == 0xD2C00000)   // movz x, #imm, lsl #32
            return (uint64_t((w >> 5) & 0xFFFF) << 32) == value;
        if ((w & 0xFFC003E0) == 0xB24003E0) {   // orr x, xzr, #bitmask with a 64-bit element
            unsigned immr = (w >> 16) & 63, imms = (w >> 10) & 63;
            uint64_t v = imms < 63 ? (1ULL << (imms + 1)) - 1 : ~0ULL;
            uint64_t r = immr ? (v >> immr) | (v << (64 - immr)) : v;
            return r == value;
        }
        return false;
    };
    auto isBl = [](uint32_t w) { return (w & 0xFC000000) == 0x94000000; };
    auto blTarget = [](uint32_t w, Addr pc) {
        int64_t imm = w & 0x3FFFFFF;
        return pc + (imm & (1 << 25) ? imm - (1 << 26) : imm) * 4;
    };
    auto pairOffset = [](uint32_t w) {
        int64_t imm = (w >> 15) & 0x7F;
        return (imm & 0x40 ? imm - 0x80 : imm) * 8;
    };
    auto storesPairAt = [&](uint32_t w, int rn, int64_t off) {
        return (w & 0xFFC00000) == 0xA9000000 && int((w >> 5) & 31) == rn && pairOffset(w) == off;
    };
    struct Site {
        Addr sizes, end, total, pages, w1Meta, x2Meta, w1, x2;
        int64_t field;
        int xs;
    };
    auto check = [&](Addr a, Addr k) -> std::optional<Site> {
        std::vector<Addr> calls;
        for (Addr j = a; j < std::min(k + 8, n); j++)
            if (isBl(W(j)))
                calls.push_back(j);
        if (calls.size() < 4 || !(calls[2] < k && k < calls[3]))
            return {};
        Addr b0 = calls[0], b1 = calls[1], b2 = calls[2], b3 = calls[3];
        int xa = W(a) & 31;
        std::vector<Addr> g12;
        for (Addr j = b1; j < b2; j++)
            if (loaded(W(j), G12))
                g12.push_back(j);
        if (g12.size() != 2 || (W(g12[1]) & 31) != 0)
            return {};
        int xb = W(g12[0]) & 31;
        if (xb < 19 || xb > 28)
            return {};
        std::vector<int> movs;
        for (Addr j = std::max<Addr>(0, a - 8); j < b0; j++)
            if ((W(j) & 0xFFFFFFE0) == 0xAA0003E0 && (W(j) & 31) >= 19 && (W(j) & 31) <= 28)
                movs.push_back(W(j) & 31);
        if (movs.size() != 1)
            return {};
        int xs = movs[0];
        std::set<int64_t> fields;
        for (Addr j = a + 1; j < b0; j++) {
            uint32_t w = W(j);
            int base = (w >> 5) & 31;
            if (base != 0 && base != xs)
                continue;
            if ((w & 0xFFC00000) == 0xF9000000 && int(w & 31) == xa)
                fields.insert(((w >> 10) & 0xFFF) * 8);
            else if ((w & 0xFFC00000) == 0xA9000000 && int(w & 31) == xa && int((w >> 10) & 31) == xa) {
                fields.insert(pairOffset(w));
                fields.insert(pairOffset(w) + 8);
            }
        }
        int64_t f = fields.empty() ? 0 : *fields.begin();
        if (fields != std::set<int64_t>{f, f + 8, f + 0x10})
            return {};
        Addr t1 = blTarget(W(b1), b1 * 4), t2 = blTarget(W(b2), b2 * 4);
        if (t1 != t2)
            return {};
        std::vector<Addr> args;
        for (auto [lo, hi] : {std::make_pair(b0, b1), std::make_pair(b1, b2)}) {
            std::vector<Addr> w1, x2;
            for (Addr j = lo; j < hi; j++) {
                if (W(j) == W1_ZERO)
                    w1.push_back(j);
                if (W(j) == X2_ZERO)
                    x2.push_back(j);
            }
            if (w1.size() != 1 || x2.size() != 1 || w1[0] > x2[0])
                return {};
            args.push_back(w1[0]);
            args.push_back(x2[0]);
        }
        bool meta = false, region = false, end = false;
        for (Addr j = b1; j < b2; j++)
            meta |= storesPairAt(W(j), xs, f + 0x28);
        for (Addr j = b2; j < b3; j++) {
            region |= storesPairAt(W(j), xs, f + 0x18);
            end |= (W(j) & 0xFFE0FC00) == 0x8B000000 && int((W(j) >> 16) & 31) == xb;
        }
        if (!meta || !region || !end)
            return {};
        Addr r = t1 / 4;   // t1 is a multiple of 4
        if (r < 0 || r >= n - 12)
            return {};
        Addr call = -1;
        for (Addr j = r; j < r + 12 && call < 0; j++)
            if (isBl(W(j)))
                call = j;
        if (call < 0)
            return {};
        bool overwrites = false;
        for (Addr j = r; j < call; j++)
            overwrites |= (W(j) & 0xFFE0FFFF) == 0xAA0003E1;
        if (!overwrites)
            return {};
        return Site{a, g12[0], g12[1], k, args[0], args[1], args[2], args[3], f, xs};
    };
    std::vector<std::pair<Addr, Addr>> cands;
    for (uint32_t pattern : {0x52A00025u, 0x321003E5u})   // mov w5, #0x10000: movz, or orr w5, wzr
        for (Addr k = 0; k < n; k++) {
            if (W(k) != pattern)
                continue;
            for (Addr a = k - 1; a > std::max<Addr>(-1, k - WINDOW); a--)
                if (loaded(W(a), G4)) {
                    int twelve = 0;
                    for (Addr j = a; j < k; j++)
                        twelve += loaded(W(j), G12);
                    if (twelve >= 2)
                        cands.push_back({a, k});
                    break;
                }
            if ((k & 0x3fff) == 0)
                poll();
        }
    std::vector<Site> good;
    for (auto [a, k] : cands)
        if (auto site = check(a, k))
            good.push_back(*site);
    if (good.size() != 1)
        return;
    auto &s = good[0];
    uint64_t large = SIZES[0] << 20, p512 = SIZES[1] << 20, p64 = SIZES[2] << 20, total = large + p512 + p64;
    auto movzX = [](int rd, uint64_t value) {
        require((value & 0xFFFF) == 0 && value > 0 && value < (1ULL << 32), "Snowdrop pool size encoding");
        return uint32_t(0xD2A00000 | (value >> 16) << 5 | rd);
    };
    auto strX = [](int rt, int rn, int64_t off) {
        require(off % 8 == 0 && off >= 0 && off < 0x8000, "Snowdrop pool field encoding");
        return uint32_t(0xF9000000 | (off / 8) << 10 | rn << 5 | rt);
    };
    auto mb = [](uint64_t x) { return std::to_string(x); };
    int xa = W(s.sizes) & 31, xb = W(s.end) & 31;
    std::vector<std::tuple<Addr, uint32_t, std::string>> next = {
        {s.sizes * 4, movzX(xa, p512), "each range " + mb(SIZES[1]) + " MB; the two that differ are set below"},
        {s.end * 4, movzX(xb, total), "combined range end " + mb(total >> 20) + " MB"},
        {s.w1 * 4, movzX(1, large), "the large range; the reservation discards incoming x1"},
        {s.total * 4, movzX(0, total), "combined reservation " + mb(total >> 20) + " MB"},
        {s.x2 * 4, strX(1, s.xs, s.field + 0x10), "the large range field " + mb(SIZES[0]) + " MB; the reservation never reads x2"},
        {s.pages * 4, uint32_t(0x52800005 | (large >> 16) << 5),
         "the large allocator's maximum 64 KB page count, matching its " + mb(SIZES[0]) + " MB"}};
    if (p64 != p512) {
        next.push_back({s.w1Meta * 4, movzX(1, p64), "the 64 KB range; the reservation discards incoming x1"});
        next.push_back({s.x2Meta * 4, strX(1, s.xs, s.field),
                        "the 64 KB range field " + mb(SIZES[2]) + " MB; the reservation never reads x2"});
    }
    std::sort(next.begin(), next.end());
    std::vector<Cut> made;
    for (auto &[at, w, why] : next) {
        Bytes now(4);
        putbuf(now, 0, w);
        made.push_back({at, Bytes(img.begin() + at, img.begin() + at + 4), now, "Snowdrop allocator: " + why});
    }
    for (auto &c : made) {
        memcpy(img.data() + c.a, c.now.data(), 4);
        cuts.push_back(c);
    }
    records(cuts.size());
}
void Engine::analyze() {
    std::set<std::pair<Addr, Addr>> funcs;
    for (auto &[a, k] : kinds)
        if (k.first == "x18") {
            auto it = std::upper_bound(starts.begin(), starts.end(), a);
            Addr lo = a, hi = a + 4;
            if (it != starts.begin()) {
                lo = *(it - 1);
                hi = it == starts.end() ? text : *it;
            } else if (!starts.empty()) {
                lo = 0;
                for (auto [s, e] : blocks) {
                    (void)s;
                    if (e <= a)
                        lo = std::max(lo, e);
                }
                hi = starts.front();
            }
            funcs.insert({lo, hi});
        }
    uint64_t analyzed = 0, analyzeTotal = 0;
    for (auto [fa, fb] : funcs) analyzeTotal += fb - fa;
    report(2, 0, analyzeTotal);
    for (auto [fa, fb] : funcs) {
        report(2, analyzed, analyzeTotal); analyzed += fb - fa;
        require(fb - fa <= 0x40000, "large function splitting not yet ported", fa, NR_UNSUPPORTED);
        auto l = disfn(fa, fb);
        std::vector<IL> ws;
        for (size_t i = 0; i < l.size(); i++) {
            if (!ldex(l[i]))
                continue;
            size_t j = i;
            while (j < l.size() && !stex(l[j]))
                j++;
            if (j >= l.size() || j - i >= 16)
                continue;
            bool other = false;
            for (size_t q = i + 1; q < j; q++)
                if (ldex(l[q]))
                    other = true;
            if (other)
                continue;
            if (j + 1 < l.size() && one(l[j + 1].m, {"cbz", "cbnz"}))
                j++;
            bool h = false;
            for (size_t q = i; q <= j; q++)
                h |= has18(l[q]);
            if (h) {
                IL w(l.begin() + i, l.begin() + j + 1);
                ws.push_back(w);
                windows.push_back(w);
                for (auto &x : w)
                    done.insert(x.a);
            }
        }
        for (int j = 0; j < int(l.size()); j++)
            if (l[j].m == "br" && l[j].op == "x18") {
                std::optional<Addr> b;
                int width = 0;
                for (int q = j - 1; q >= std::max(0, j - 16); q--)
                    if (l[q].m == "adr" && begins(l[q].op, "x18,")) {
                        b = l[q].d.operands[1].imm;
                        break;
                    }
                for (int q = j - 1; q >= std::max(0, j - 16); q--)
                    if (l[q].m == "add" && begins(l[q].op, "x18, x18,") &&
                        l[q].op.find("lsl #2") != std::string::npos) {
                        int idx = num(canon(l[q].names[2]));
                        for (int k = j - 1; k >= std::max(0, j - 16); k--) {
                            if (num(l[k].names[0]) != idx)
                                continue;
                            if (one(l[k].m, {"ldrb", "ldrsb"}))
                                width = 256;
                            if (one(l[k].m, {"ldrh", "ldrsh"}))
                                width = b && *b ? halfcount(l, k, *b) : 65536;
                            if (width)
                                break;
                        }
                        break;
                    }
                if (b && *b && width)
                    jumps[l[j].a] = {*b, width};
                else {
                    auto ts = offsets(l, j, false);
                    if (ts.empty())
                        ts = offsets(l, j, true);
                    if (ts.empty() && tailcall(l, j)) {
                        tailcalls.insert(l[j].a);
                        continue;
                    }
                    require(!ts.empty(), "unrecognized x18 jump table (extended recognizers not yet ported)",
                            l[j].a, NR_UNSUPPORTED);
                    tables[l[j].a] = std::move(ts);
                }
            }
        AS inwin, heads, rmid;
        for (auto &w : ws) {
            heads.insert(w.front().a);
            for (auto &x : w)
                inwin.insert(x.a);
        }
        for (int k = 0; k < int(l.size()); k++) {
            auto &x = l[k];
            if (!direct(x))
                continue;
            Addr t = x.target();
            if (!inwin.count(t) || heads.count(t))
                continue;
            IL tail, rb;
            Addr ft = 0;
            for (auto &w : ws)
                if (std::any_of(w.begin(), w.end(), [&](const Ins &i) { return i.a == t; })) {
                    ft = w.back().a + 4;
                    for (auto &i : w)
                        if (i.a >= t)
                            tail.push_back(i);
                    break;
                }
            if (!tail.empty() && stex(tail[0]) && !one(x.m, {"b", "bl"}))
                for (int p = k - 1; p > std::max(-1, k - 8); p--) {
                    auto &q = l[p];
                    if (ldex(q)) {
                        rb = IL(l.begin() + p, l.begin() + k + 1);
                        break;
                    }
                    if (direct(q) || one(q.m, {"br", "blr", "ret"}) || stex(q))
                        break;
                }
            require(!rb.empty(), "branch into window", x.a);
            for (auto &y : rb)
                require(!done.count(y.a), "retry block overlaps window", rb[0].a);
            retries.push_back({{rb, x.a + 4}, {tail, ft}});
            for (auto &y : rb)
                done.insert(y.a);
            for (size_t p = 1; p < rb.size(); p++)
                rmid.insert(rb[p].a);
        }
        for (auto &x : l)
            if (direct(x))
                require(!rmid.count(x.target()), "branch into retry block", x.a);
        for (auto &[a, ts] : tables)
            if (a >= fa && a < fb)
                for (auto t : ts)
                    require(!inwin.count(t) || heads.count(t), "jump table into window", a);
        records(done.size() + windows.size() + retries.size() + tables.size() + jumps.size());
    }
    report(2, analyzed, analyzeTotal);
    for (auto &w : windows) {
        use(w.front().a);
        auto st = window({{w, w.back().a + 4}}, false);
        branch(w.front().a, st);
    }
    for (auto &p : retries) {
        use(p[0].first[0].a);
        auto st = window(p, true);
        branch(p[0].first[0].a, st);
    }
}
Addr Engine::window(const Parts &parts, bool retry) {
    std::set<int> used;
    for (auto &p : parts)
        for (auto &i : p.first)
            used = uni(used, regs(i));
    auto [s, t] = pick(used);
    Addr st = here();
    many({STP(s, t, 31, -16), MRS_TPIDRRO(t)});
    slot(t, s, 0);
    emit(LDRX(s, t));
    std::map<Addr, Addr> pos;
    Addr at = here();
    for (auto &p : parts) {
        for (auto &i : p.first) {
            pos[i.a] = at;
            at += 4;
        }
        at += 16;
    }
    Addr exits = at;
    std::vector<Addr> ext;
    for (auto &p : parts) {
        for (auto &i : p.first) {
            uint32_t w = has18(i) ? subst(i, s) : i.w;
            if (cond(i)) {
                Addr to = i.target();
                if (pos.count(to))
                    to = pos[to];
                else {
                    auto it = std::find(ext.begin(), ext.end(), to);
                    if (!retry || it == ext.end()) {
                        ext.push_back(to);
                        to = exits + 16 * (ext.size() - 1);
                    } else
                        to = exits + 16 * std::distance(ext.begin(), it);
                }
                Ins copy = i;
                copy.w = w;
                w = rebranch(copy, here(), to);
                auto d = decode(w, here());
                require(d.m == i.m && d.target() == to, "window branch relocation", i.a);
            } else if (i.m == "adrp") {
                int n = num(i.names[0]);
                require(n >= 0, "ADRP register", i.a);
                w = ADRP(n == 18 ? s : n, here(), i.d.operands[1].imm);
            } else
                require(!one(i.m, {"adr", "b", "bl", "br", "blr", "ret"}) &&
                            !(i.m == "ldr" && i.op.find('[') == std::string::npos),
                        "unsupported instruction in exclusive window", i.a);
            emit(w);
        }
        many({STRX(s, t), LDP(s, t, 31, -16)});
        emit(B(here(), p.second));
        emit(NOP);
    }
    for (auto a : ext) {
        many({STRX(s, t), LDP(s, t, 31, -16)});
        emit(B(here(), a));
        emit(NOP);
    }
    return st;
}
void Engine::singles() {
    uint64_t processed = 0;
    for (auto &[a, k] : kinds) {
        report(3, processed, kinds.size() * 4); processed += 4;
        if (done.count(a))
            continue;
        use(a);
        auto i = decode(rd(a), a);
        Addr st = here();
        int n = i.w & 31;
        auto [s, t] = pick(regs(i));
        if (k.first == "svc" || k.first == "cntpct" || k.first == "cntvct") {
            gate(a, k.first, k.second);
            continue;
        }
        if (k.first == "ctr") {
            require(k.second != 18, "ctr read into x18", a);
            put(a, 0xd2a00080 | k.second);
            continue;
        }
        if (k.first == "tpidr_r" || k.first == "tpidrro_r") {
            int tmp = n == 16 ? 17 : 16;
            many({STUR(tmp, 31, -16), MRS_TPIDRRO(n)});
            slot(n, tmp, k.first == "tpidr_r" ? 1 : 2);
            many({LDRX(n, n), LDUR(tmp, 31, -16)});
            emit(B(here(), a + 4));
        } else if (k.first == "tpidr_w") {
            t = pick({n}).first;
            int tmp = 16 != n && 16 != t ? 16 : 17;
            many({STUR(t, 31, -16), STUR(tmp, 31, -24), MRS_TPIDRRO(t)});
            slot(t, tmp, 1);
            many({STRX(n, t), LDUR(tmp, 31, -24), LDUR(t, 31, -16)});
            emit(B(here(), a + 4));
        } else if (k.first == "sp_align") {
            n = k.second;
            many({ADDI(n, n, 15), uint32_t(0x927cec00 | (n << 5) | n), i.w});
            emit(B(here(), a + 4));
        } else if (i.m == "br") {
            if (tailcalls.count(a)) {
                // site_br_call preserves the caller's LR. x16/x17 are ABI call temporaries.
                emit(MRS_TPIDRRO(16));
                slot(16, 17, 0);
                many({LDRX(16, 16), BR(16)});
            } else if (tables.count(a)) {
                many({STUR(16, 31, -16), STUR(17, 31, -24), MRS_TPIDRRO(16)});
                slot(16, 17, 0);
                emit(LDRX(16, 16));
                AS uniq(tables[a].begin(), tables[a].end());
                Addr pads = here() + 4 * (4 * uniq.size() + 1);
                size_t j = 0;
                for (auto to : uniq) {
                    emit(ADRP(17, here(), to));
                    emit(ADDI(17, 17, to & 4095));
                    emit(0xeb11021f);
                    Ins eq = decode(0x54000000, here());
                    emit(rebranch(eq, here(), pads + 12 * j++));
                }
                emit(0xd4200300);
                for (auto to : uniq) {
                    many({LDUR(16, 31, -16), LDUR(17, 31, -24)});
                    emit(B(here(), to));
                }
            } else {
                require(jumps.count(a), "missing jump recognition", a);
                auto [b, c] = jumps[a];
                many({STUR(16, 31, -16), STUR(17, 31, -24), MRS_TPIDRRO(16)});
                slot(16, 17, 0);
                many({LDRX(16, 16), LDUR(17, 31, -24)});
                Addr pad = st + 96, delta = pad - b;
                auto op = delta >= 0 ? ADDI : SUBI;
                Addr hi = std::abs(delta) >> 12, lo = std::abs(delta) & 4095;
                int count = 0;
                while (hi) {
                    int h = std::min<Addr>(hi, 4095);
                    emit(op(16, 16, h, 1));
                    hi -= h;
                    count++;
                }
                if (lo) {
                    emit(op(16, 16, lo, 0));
                    count++;
                }
                require(count <= 12, "jump delta exceeds pad reservation", a);
                emit(BR(16));
                while (here() < pad)
                    emit(NOP);
                Addr tails = pad + 4 * c;
                for (int j = 0; j < c; j++)
                    emit(B(here(), tails + 8 * j));
                for (int j = 0; j < c; j++) {
                    emit(LDUR(16, 31, -16));
                    emit(B(here(), b + 4 * j));
                }
            }
        } else if (one(i.m, {"cbz", "cbnz", "tbz", "tbnz"})) {
            many({STP(s, t, 31, -16), MRS_TPIDRRO(t)});
            slot(t, s, 0);
            emit(LDRX(s, t));
            Addr taken = here() + 12;
            uint32_t w = one(i.m, {"cbz", "cbnz"}) ? ((i.w & 0xff000000) | (3 << 5) | s)
                                                   : ((i.w & 0xfff80000) | (3 << 5) | s);
            auto dd = decode(w, here());
            require(dd.m == i.m && dd.target() == taken, "conditional relocation", a);
            emit(w);
            emit(LDP(s, t, 31, -16));
            emit(B(here(), a + 4));
            require(here() == taken, "conditional landing pad", a);
            emit(LDP(s, t, 31, -16));
            emit(B(here(), i.target()));
        } else if (one(i.m, {"adr", "adrp"})) {
            s = 0;
            t = 1;
            many({STP(s, t, 31, -16), MRS_TPIDRRO(t)});
            slot(t, s, 0);
            Addr target = i.d.operands[1].imm;
            emit(ADRP(s, here(), target));
            if (i.m == "adr")
                emit(ADDI(s, s, target & 4095));
            many({STRX(s, t), LDP(s, t, 31, -16)});
            emit(B(here(), a + 4));
        } else if (i.m == "blr") {
            emit(MRS_TPIDRRO(16));
            slot(16, 17, 0);
            emit(LDRX(16, 16));
            emit(ADRP(30, here(), a + 4));
            many({ADDI(30, 30, (a + 4) & 4095), BR(16)});
        } else if (begins(i.op, "sp,") || begins(i.op, "wsp,")) {
            many({STP(s, t, 31, -16), MRS_TPIDRRO(t)});
            slot(t, s, 0);
            many({LDRX(s, t), ADDI(t, 31, 0), subst(i, s), LDP(s, t, t, -16)});
            emit(B(here(), a + 4));
        } else {
            require(!one(i.m, {"ret", "b", "bl"}) && !(i.m == "ldr" && i.op.find('[') == std::string::npos),
                    "unsupported x18 site", a, NR_UNSUPPORTED);
            std::smatch m;
            int delta = 0;
            if (std::regex_search(i.op, m, std::regex("\\[sp, #(-?(?:0x)?[0-9a-f]+)\\]!")) ||
                std::regex_search(i.op, m, std::regex("\\[sp\\], #(-?(?:0x)?[0-9a-f]+)$")))
                delta = std::stoi(m[1], nullptr, 0);
            require(delta == 0 || delta >= -96, "SP writeback below scratch", a);
            int save = delta ? -112 : -16;
            many({STP(s, t, 31, save), MRS_TPIDRRO(t)});
            slot(t, s, 0);
            many({LDRX(s, t), subst(i, s), STRX(s, t), LDP(s, t, 31, save - delta)});
            emit(B(here(), a + 4));
        }
        branch(a, st);
    }
}
std::map<std::string, std::pair<Addr, Addr>> Engine::symbols() {
    std::map<std::string, std::pair<Addr, Addr>> out;
    try {
        Addr mod = rd(4), dyn = mod + (int32_t)rd(mod + 4);
        std::map<Addr, Addr> tags;
        for (int i = 0; i < 4096; i++) {
            Addr t = rd(dyn + 16 * i, 8), v = rd(dyn + 16 * i + 8, 8);
            if (!t)
                break;
            tags.emplace(t, v);
        }
        if (!tags.count(6) || !tags.count(5))
            return {};
        Addr sy = tags[6], str = tags[5], n = tags.count(4) ? rd(tags[4] + 4) : (str - sy) / 24;
        require(n >= 0, "symbol count", sy);
        records(n);
        for (Addr i = 1; i < n; i++) {
            Addr at = sy + 24 * i;
            if (!rd(at + 6, 2))
                continue;
            Addr name = str + rd(at), value = rd(at + 8, 8), size = rd(at + 16, 8);
            std::string s;
            while (rd(name, 1)) {
                require(s.size() < 4096, "symbol name limit", name, NR_LIMIT);
                s += char(rd(name++, 1));
            }
            out[s] = {value, size};
        }
    } catch (Failure &f) {
        if (f.status == NR_LIMIT || f.status == NR_CANCELLED)
            throw;
        return {};
    }
    return out;
}
void Engine::observe() {
    for (size_t j = 0; j < r.profile.observation_count; j++) {
        auto &o = r.profile.observations[j];
        require(o.reason && strnlen(o.reason, 4096) < 4096, "observation reason", o.offset, NR_INVALID_INPUT);
        require(rd(o.offset) == o.original_word, "observation original instruction mismatch", o.offset);
        require(o.offset < uint64_t(text) && !conflict(o.offset), "observation conflicts with rewrite",
                o.offset);
        obs.push_back({(Addr)o.offset, o.number, o.original_word, o.reason});
    }
    auto add = [&](Addr a, uint32_t id, std::string why) { obs.push_back({a, id, uint32_t(rd(a)), why}); };
    auto sym = symbols();
    std::string ask = "_ZN2nn2os6detail11VammManager21AllocateAddressRegionEPmm",
                free = "_ZN2nn2os6detail11VammManager17FreeAddressRegionEm";
    if (sym.count(ask) && sym.count(free)) {
        auto [a, n] = sym[ask];
        auto l = disfn(a, a + n);
        std::vector<std::string> regs;
        std::smatch m;
        for (size_t j = 0; j < std::min<size_t>(16, l.size()); j++)
            if (l[j].m == "mov" && std::regex_match(l[j].op, m, std::regex("(x[0-9]+), x1")))
                regs.push_back(m[1]);
        std::vector<std::pair<Addr, int>> st;
        if (regs.size() == 1)
            for (auto &i : l)
                if (i.m == "str" && std::regex_match(i.op, m, std::regex("x([0-9]+), \\[" + regs[0] + "\\]")))
                    st.push_back({i.a, std::stoi(m[1])});
        Addr f = sym[free].first;
        if (st.size() == 1 && a < text && f < text && st[0].first < text && !conflict(a) && !conflict(f) &&
            !conflict(st[0].first)) {
            add(a, 22, "VammManager::AllocateAddressRegion entry: the size in x2");
            add(st[0].first, 23 | (st[0].second << 8),
                "VammManager::AllocateAddressRegion: the new region's address in x" +
                    std::to_string(st[0].second) + ", stored through the out pointer");
            add(f, 24, "VammManager::FreeAddressRegion entry: the address in x1");
        }
    }
    for (auto name :
         {"_ZN2nn3mem17StandardAllocator10InitializeEPvm", "_ZN2nn3mem17StandardAllocator10InitializeEPvmb"})
        if (sym.count(name)) {
            Addr a = sym[name].first;
            if (a < text && !conflict(a))
                add(a, 25,
                    std::string("StandardAllocator::Initialize(void *, size_t") +
                        (name[strlen(name) - 1] == 'b' ? ", bool" : "") +
                        ") entry: the size in x2, capped when the address in x1 is null");
        }
    for (auto &o : obs) {
        auto i = decode(o.w, o.a);
        require(!one(i.m, {"adr", "adrp", "b", "bl", "br", "blr", "ret", "cbz", "cbnz", "tbz", "tbnz"}) &&
                    !begins(i.m, "b.") && !(begins(i.m, "ldr") && i.op.find('[') == std::string::npos) &&
                    !has18(i),
                "observation cannot relocate instruction", o.a);
        use(o.a);
        gate(o.a, "obs", o.id, o.w);
    }
    records(obs.size());
}

void Engine::frame() {
    if (!r.profile.recipe)
        return;
    require(r.profile.recipe == 1 && bid == "277178b7dba1b6d4949a84778d4abc58" &&
                std::string(r.module_name) == "main",
            "unknown frame recipe/build", 0, NR_UNSUPPORTED);
    fs = key + 0x4000;
    if (far)
        imageSize = fs + 0x4000;
    require(fs % 4096 == 0 && (far || fs + 0x40 <= po), "frame state page layout");
    for (size_t p = std::min<size_t>(fs, img.size()); p < img.size(); p++)
        require(img[p] == 0, "frame state page is not zero");
    auto site = [&](Addr a, std::initializer_list<uint32_t> words) {
        for (auto w : words) {
            require(rd(a) == w, "frame step original bytes mismatch", a);
            require(!conflict(a), "frame step conflicts with rewrite", a);
            a += 4;
        }
    };
    auto save = [&](Addr a, std::initializer_list<uint32_t> words, const char *why) {
        Bytes old(img.begin() + a, img.begin() + a + words.size() * 4);
        Addr p = a;
        for (auto w : words) {
            put(p, w);
            p += 4;
        }
        cuts.push_back({a, old, Bytes(img.begin() + a, img.begin() + p), why});
    };
    use(0);
    const Addr step = 0x2db18c, epilogue = 0x2db284, fade = 0x1fbc80, swapchain = 0xbe14a8, view = 0xd5cc0,
               dynamic = 0xd5d80, FS_STATE_OFF = fs;
    const int base = 24, dst = 10;
    const uint32_t fmadd = 0x1f080400 | dst;
    site(step, {0x39416009});
    Addr start = here();
    emit(ADRP(16, here(), FS_STATE_OFF));
    many({uint32_t(0xbd400200), uint32_t(0x1e202008), uint32_t(0x54000640), uint32_t(0xb9402202),
          uint32_t(0x340001a2), uint32_t(0x1e230041), uint32_t(0x1e27d003), uint32_t(0x1e211872),
          uint32_t(0x1e322a52), uint32_t(0xbd002412), uint32_t(0x52800785), uint32_t(0x1ac208a5),
          uint32_t(0xb9005005), uint32_t(0xf9401a04), uint32_t(0x8b224084), uint32_t(0xf9001a04)});
    emit(B(here(), epilogue));
    many({uint32_t(0xf9406c22), uint32_t(0x9e630041), uint32_t(0x529f0003), uint32_t(0x72a02483),
          uint32_t(0x1e630062), uint32_t(0x1e621821), uint32_t(0xb9400604), uint32_t(0x1e27d003),
          uint32_t(0x7100009f), uint32_t(0x1e201c60), uint32_t(0x1e22c000), uint32_t(0x1e6e1003),
          uint32_t(0x1e601864), uint32_t(0x1e65d005), uint32_t(0x1e651865), uint32_t(0xfd400606),
          uint32_t(0x1e663827), uint32_t(0x1e6448e7), uint32_t(0x1e6558e7), uint32_t(0x1e6728c6),
          uint32_t(0x1e6138c6), uint32_t(0x2f00e410), uint32_t(0x1e7048c6), uint32_t(0x1e6a1011),
          uint32_t(0x1e7158c6), uint32_t(0xfd000606), uint32_t(0x1e671872), uint32_t(0x1e624252),
          uint32_t(0xbd002412), uint32_t(0x1e380245), uint32_t(0xb9005005), uint32_t(0xfd400e13),
          uint32_t(0x1e672a73), uint32_t(0xfd000e13)});
    emit(B(here(), epilogue));
    emit(0x39416009);
    emit(B(here(), step + 4));
    save(step, {B(step, start)},
         "frame step from the host in whole vsyncs, by elapsed time until it has decided one (state page "
         "target fps; 0 = original interval logic)");
    const int readers[2][3] = {{0x2daa54, 0x2daa6c, 19}, {0x3e562c, 0x3e5638, 22}};
    for (auto &rr : readers) {
        int at = rr[0], cvt = rr[1], r = rr[2];
        site(at, {uint32_t(0xbd425000 | (r << 5))});
        site(cvt, {0x5e21d800});
        start = here();
        emit(STP_PRE(16, 17, 31, -16));
        emit(ADRP(16, here(), FS_STATE_OFF));
        many({uint32_t(0xb9400211), uint32_t(0x34000071), uint32_t(3175228416 | r << 5), uint32_t(0x14000003),
              uint32_t(3175239680 | r << 5), uint32_t(0x5e21d800), uint32_t(0xa8c147f0)});
        emit(B(here(), at + 4));
        save(at, {B(at, start)}, "game step reads the float fps (original integer fps and scvtf when off)");
        save(cvt, {NOP}, "scvtf moved into the stub at the load");
    }
    site(fade, {0x1e202008, fmadd});
    start = here();
    emit(ADRP(8, here(), FS_STATE_OFF));
    many({uint32_t(0xb9400108), uint32_t(0x340001a8), uint32_t(0x1e202028), uint32_t(0x54000101),
          uint32_t(2019532808 | base << 5), uint32_t(0x7100151f), uint32_t(0x540000a1),
          uint32_t(2019524616 | base << 5), uint32_t(0x7100151f), uint32_t(0x54000041), uint32_t(0x1e2e1008),
          uint32_t(fmadd), uint32_t(0x1e202008), uint32_t(0x14000003), uint32_t(0x1e202008),
          uint32_t(fmadd)});
    emit(B(here(), fade + 8));
    save(fade, {B(fade, start), NOP},
         "D-pad menu fade starts at full rate under fractional steps (FPSLocker)");
    site(swapchain, {0x3940a261});
    start = here();
    emit(ADRP(16, here(), FS_STATE_OFF));
    many({uint32_t(0xb9400210), uint32_t(0x34000070), uint32_t(0x52800021), uint32_t(0x14000002),
          uint32_t(0x3940a261)});
    emit(B(here(), swapchain + 4));
    save(swapchain, {B(swapchain, start)}, "swapchain argument 1 when on (FPSLocker)");
    site(view, {0x52a8d008, 0x1e250840, 0x1e240861, 0x1e270104});
    site(dynamic, {0xbd402906});
    start = here();
    emit(ADRP(8, here(), FS_STATE_OFF));
    many({uint32_t(0xbd401106), uint32_t(0x1e250840), uint32_t(0x1e240861), uint32_t(0x1e2020c8),
          uint32_t(0x5400006d), uint32_t(0x1e260800), uint32_t(0x1e260821), uint32_t(0x52a8d008),
          uint32_t(0x1e270104)});
    emit(B(here(), view + 0x10));
    save(view, {B(view, start)}, "view render size x the host render scale (1.0 = original)");
    start = here();
    emit(0xbd402906);
    emit(ADRP(8, here(), FS_STATE_OFF));
    many({uint32_t(0xbd401107), uint32_t(0x1e2e1010), uint32_t(0x1e3020e0), uint32_t(0x54000080),
          uint32_t(0x1e2020e8), uint32_t(0x5400004d), uint32_t(0x1e204206)});
    emit(B(here(), dynamic + 4));
    save(dynamic, {B(dynamic, start)}, "dynamic resolution only at render scale 1.0");
}
std::pair<RS, RS> Engine::spinrw(const Ins &i) {
    RS ops, rr, ww;
    std::vector<std::string> regs;
    for (int n = 0; n < i.d.op_count; n++)
        if (i.d.operands[n].type == ARM64_OP_REG) {
            auto r = norm(i.names[n]);
            regs.push_back(r);
            if (!r.empty())
                ops.insert(r);
        }
    if (one(i.m, {"cbz", "cbnz", "tbz", "tbnz"}))
        return {ops, {}};
    if (begins(i.m, "b."))
        return {{"nzcv"}, {}};
    if (i.m == "b")
        return {};
    if (one(i.m, {"cmp", "cmn", "tst", "fcmp", "fcmpe"}))
        return {ops, {"nzcv"}};
    if (one(i.m, {"ccmp", "ccmn", "fccmp", "fccmpe"}))
        return {uni(ops, RS{"nzcv"}), {"nzcv"}};
    for (auto &r : i.reads)
        if (!norm(r).empty())
            rr.insert(norm(r));
    for (auto &w : i.writes)
        if (!norm(w).empty())
            ww.insert(norm(w));
    if (begins(i.m, "ld") || begins(i.m, "prfm") || begins(i.m, "prfum"))
        return {rr, uni(ww, ops)};
    if (!regs.empty() && !regs[0].empty())
        ww.insert(regs[0]);
    for (size_t n = 1; n < regs.size(); n++)
        if (!regs[n].empty())
            rr.insert(regs[n]);
    return {rr, ww};
}
std::optional<Node> Engine::rmw(Addr a) {
    IL l;
    for (int j = 0; j < 4; j++) {
        auto i = spinins(a + 4 * j);
        if (!i.valid)
            return {};
        l.push_back(i);
    }
    if (!one(l[0].m, {"ldxr", "ldaxr"}) || !one(l[1].m, {"add", "sub"}) || !one(l[2].m, {"stxr", "stlxr"}) ||
        l[3].m != "cbnz")
        return {};
    if (l[1].d.op_count != 3 || l[1].d.operands[2].type != ARM64_OP_IMM ||
        l[1].op.find("lsl") != std::string::npos)
        return {};
    auto b = l[0].d.operands[1].mem;
    auto st = l[2].d.operands[2].mem;
    if (b.disp || b.index || st.base != b.base || st.disp)
        return {};
    if (l[1].names[1] != l[0].names[0] || l[2].names[1] != l[1].names[0] || l[3].names[0] != l[2].names[0] ||
        l[3].target() != a)
        return {};
    Node n;
    n.kind = "rmw";
    n.base = norm(regname(b.base));
    n.k = l[1].d.operands[2].imm * (l[1].m == "add" ? 1 : -1);
    n.uses = {n.base};
    n.defs = {norm(l[0].names[0]), norm(l[1].names[0]), norm(l[2].names[0])};
    n.defs.erase("");
    n.ins = l;
    n.succ = {a + 16};
    return n;
}
std::optional<Node> Engine::lock(Addr a) {
    IL l;
    for (int j = 0; j < 6; j++)
        l.push_back(spinins(a + 4 * j));
    if (!l[0].valid)
        return {};
    auto plain = [](const Ins &i, int n) {
        auto &o = i.d.operands[n];
        return o.type == ARM64_OP_MEM && !o.mem.index && !o.mem.disp;
    };
    auto make = [&](std::string kind, std::string base, IL ins, int pre, int count, RS uses, RS defs) {
        Node n;
        n.kind = kind;
        n.base = base;
        n.ins = ins;
        n.pre = pre;
        n.k = kind == "acq" ? 1 : kind == "rel" ? -1 : 0;
        n.uses = uses;
        n.defs = defs;
        n.succ = {a + 4 * count};
        return n;
    };
    bool all = std::all_of(l.begin(), l.end(), [](const Ins &i) { return i.valid; });
    if (one(l[0].m, {"ldxr", "ldaxr"}) && all && begins(l[0].names[0], "w") && plain(l[0], 1)) {
        int base = l[0].d.operands[1].mem.base;
        auto bn = norm(regname(base));
        if (l[1].m == "cbz" && l[1].names[0] == l[0].names[0] && l[1].target() == a + 16 &&
            l[2].m == "clrex" && l[3].m == "b" && l[3].target() == a && one(l[4].m, {"stxr", "stlxr"}) &&
            plain(l[4], 2) && int(l[4].d.operands[2].mem.base) == base &&
            !one(l[4].names[1], {"wzr", "xzr"}) && l[5].m == "cbnz" && l[5].names[0] == l[4].names[0] &&
            l[5].target() == a)
            return make("acq", bn, l, 0, 6, {bn, norm(l[4].names[1])},
                        {norm(l[0].names[0]), norm(l[4].names[0])});
        auto p = spinins(a - 4);
        if (p.valid && p.m == "clrex" && l[1].m == "cmp" && l[1].names[0] == l[0].names[0] &&
            l[1].d.operands[1].type == ARM64_OP_IMM && l[2].m == "b.ne" && l[2].target() == a - 4 &&
            one(l[3].m, {"stxr", "stlxr"}) && l[3].names[1] == "wzr" && plain(l[3], 2) &&
            int(l[3].d.operands[2].mem.base) == base && l[4].m == "cbnz" && l[4].names[0] == l[3].names[0] &&
            l[4].target() == a) {
            IL ins = {p};
            ins.insert(ins.end(), l.begin(), l.begin() + 5);
            return make("rel", bn, ins, 1, 5, {bn}, {norm(l[0].names[0]), norm(l[3].names[0]), "nzcv"});
        }
    }
    if (l[0].m == "stlr" && l[0].d.op_count == 2 && one(l[0].names[0], {"wzr", "xzr"}) && plain(l[0], 1)) {
        auto bn = norm(regname(l[0].d.operands[1].mem.base));
        return make("rel", bn, {l[0]}, 0, 1, {bn}, {});
    }
    if (one(l[0].m, {"ldr", "ldrb", "ldrh"}) && l[1].valid && l[2].valid && l[0].d.op_count == 2 &&
        begins(l[0].names[0], "w") && l[0].d.operands[1].type == ARM64_OP_MEM &&
        !l[0].d.operands[1].mem.index && !l[0].d.writeback && l[1].m == "cmp" &&
        l[1].names[0] == l[0].names[0] && l[1].d.operands[1].type == ARM64_OP_IMM &&
        one(l[2].m, {"b.eq", "b.ne"}) && l[2].target() == a) {
        auto bn = norm(regname(l[0].d.operands[1].mem.base));
        return make("tst", bn, IL(l.begin(), l.begin() + 3), 0, 3, {bn}, {norm(l[0].names[0]), "nzcv"});
    }
    return {};
}
std::optional<Node> Engine::store(const Ins &i) {
    if (!i.valid || !one(i.m, {"str", "strb", "strh", "stur", "sturb", "sturh"}) || i.d.writeback ||
        i.d.op_count != 2 || i.d.operands[0].type != ARM64_OP_REG || i.d.operands[1].type != ARM64_OP_MEM ||
        i.d.operands[1].mem.index)
        return {};
    if (!begins(i.names[0], "w") && i.names[0] != "xzr")
        return {};
    Node n;
    n.kind = "stc";
    n.ins = {i};
    n.uses = {norm(i.names[0]), norm(regname(i.d.operands[1].mem.base))};
    n.uses.erase("");
    n.succ = {i.a + 4};
    return n;
}
bool Engine::quick(Addr T, Addr hi) {
    static const std::pair<uint32_t, uint32_t> stop[] = {
        {0xfc000000, 0x94000000}, {0xfe000000, 0xd6000000}, {0xff000000, 0xd4000000},
        {0x3fc00000, 0x39000000}, {0x3fc00000, 0x3d000000}, {0x3fc00000, 0x3d800000},
        {0x3fe00000, 0x38000000}, {0x3fe00c00, 0x38200800}, {0x3fe00000, 0x3c000000},
        {0x3fe00000, 0x3c800000}, {0x3a400000, 0x28000000}, {0x3f200c00, 0x38200000}};
    AS seen;
    std::vector<Addr> todo = {T};
    while (!todo.empty()) {
        Addr a = todo.back();
        todo.pop_back();
        if (seen.count(a) || a < T || a > hi)
            continue;
        seen.insert(a);
        uint32_t w = rd(a), t = w >> 24;
        std::vector<Addr> succ;
        if (t >= 0x14 && t <= 0x17)
            succ = {a + (Addr((w & 0x3ffffff) ^ 0x2000000) - 0x2000000) * 4};
        else if (t == 0x54 || (t & 0x7e) == 0x34)
            succ = {a + (Addr(((w >> 5) & 0x7ffff) ^ 0x40000) - 0x40000) * 4, a + 4};
        else if ((t & 0x7e) == 0x36)
            succ = {a + (Addr(((w >> 5) & 0x3fff) ^ 0x2000) - 0x2000) * 4, a + 4};
        else {
            bool bad = false;
            for (auto [m, v] : stop)
                if ((w & m) == v)
                    bad = true;
            uint32_t a1 = w & 0xbfc00000, a2 = w & 0xbfe00c00;
            if (bad && a1 != 0xb9000000 && a1 != 0x39000000 && a1 != 0x79000000 && a2 != 0xb8000000 &&
                a2 != 0x38000000 && a2 != 0x78000000)
                continue;
            succ = {a + 4};
        }
        for (auto s : succ) {
            if (s == T)
                return true;
            todo.push_back(s);
        }
    }
    return false;
}
Region Engine::region1(Addr T, const std::vector<Addr> &bes, const AS &ban, bool stores) {
    Region g;
    g.head = T;
    Addr hi = *std::max_element(bes.begin(), bes.end());
    for (auto [s, e] : blocks)
        if (s < hi + 4 && T < e)
            return {};
    if (!quick(T, hi))
        return {};
    AS back, bad, inner;
    std::vector<Addr> todo = {T};
    while (!todo.empty()) {
        Addr a = todo.back();
        todo.pop_back();
        if (g.nodes.count(a) || bad.count(a))
            continue;
        if (ban.count(a)) {
            bad.insert(a);
            continue;
        }
        std::optional<Node> n;
        if (a + 12 <= hi && (rd(a) & 0x3fe00000) == 0x08400000)
            n = rmw(a);
        if (!n) {
            auto lk = lock(a);
            if (lk && !(lk->kind == "tst" && a == T))
                n = lk;
        }
        if (!n && stores)
            n = store(spinins(a));
        if (!n) {
            auto x = spinins(a);
            if (!x.valid) {
                bad.insert(a);
                continue;
            }
            std::string k;
            if (direct(x) && x.m != "bl")
                k = "br";
            else if (one(x.m, {"adr", "adrp"}))
                k = "pc";
            else {
                bool no = one(x.m, {"bl",   "blr",   "br",    "ret",   "eret",  "svc",   "hvc",    "smc",
                                    "brk",  "hlt",   "udf",   "mrs",   "msr",   "sys",   "sysl",   "wfe",
                                    "wfi",  "sev",   "sevl",  "clrex", "dc",    "ic",    "at",     "tlbi",
                                    "drps", "dcps1", "dcps2", "dcps3", "blraa", "blrab", "blraaz", "blrabz",
                                    "braa", "brab",  "braaz", "brabz", "retaa", "retab", "eretaa", "eretab"});
                for (auto p : {"st", "cas", "swp", "ldadd", "ldclr", "ldeor", "ldset", "ldsmax", "ldsmin",
                               "ldumax", "ldumin"})
                    if (begins(x.m, p))
                        no = true;
                bool mem = false;
                for (int j = 0; j < x.d.op_count; j++)
                    mem |= x.d.operands[j].type == ARM64_OP_MEM;
                if (!no) {
                    bool load = begins(x.m, "ld") || begins(x.m, "prfm") || begins(x.m, "prfum");
                    if (load ? mem : !mem)
                        k = "ok";
                }
            }
            if (k.empty()) {
                bad.insert(a);
                continue;
            }
            Node v;
            v.kind = k;
            v.ins = {x};
            v.succ = k == "br"
                         ? (x.m == "b" ? std::vector<Addr>{x.target()} : std::vector<Addr>{x.target(), a + 4})
                         : std::vector<Addr>{a + 4};
            n = v;
        }
        bool touch = false;
        for (size_t j = 0; j < n->ins.size(); j++)
            if (touched.count(a + 4 * (int(j) - n->pre)))
                touch = true;
        if (touch) {
            bad.insert(a);
            continue;
        }
        g.nodes[a] = *n;
        if (n->kind == "rmw")
            for (int j = 1; j < 4; j++)
                inner.insert(a + 4 * j);
        if (one(n->kind, {"acq", "rel", "tst"}))
            for (size_t j = 0; j < n->ins.size(); j++)
                if (int(j) != n->pre)
                    inner.insert(a + 4 * (int(j) - n->pre));
        for (auto s : n->succ) {
            if (s == T)
                back.insert(a);
            else if (T < s && s <= hi)
                todo.push_back(s);
        }
    }
    if (back.empty())
        return {};
    for (auto &[a, n] : g.nodes) {
        if (inner.count(a))
            return {};
        for (auto s : n.succ)
            if (inner.count(s))
                return {};
    }
    auto prune = [&](AS keep) {
        bool more = true;
        while (more) {
            more = false;
            for (auto &[a, n] : g.nodes)
                if (!keep.count(a))
                    for (auto s : n.succ)
                        if (s != T && keep.count(s)) {
                            keep.insert(a);
                            more = true;
                            break;
                        }
        }
        return keep;
    };
    g.R = prune(back);
    for (auto a : g.R) {
        auto &n = g.nodes[a];
        if (!one(n.kind, {"rmw", "acq", "rel", "tst", "stc"})) {
            auto rw = spinrw(n.ins[0]);
            n.uses = rw.first;
            n.defs = rw.second;
        }
    }
    int paths = 0, steps = 0;
    AS badBack;
    using Vers = std::map<std::string, int>;
    using Deltas = std::map<std::tuple<bool, std::string, int>, int64_t>;
    struct SpinNo {};
    std::function<void(Addr, AS, Vers, Deltas)> walk = [&](Addr a, AS on, Vers ver, Deltas delta) {
        if (++steps > 4096)
            throw SpinNo{};
        poll();
        auto &n = g.nodes[a];
        if (one(n.kind, {"rmw", "acq", "rel"})) {
            auto key = std::make_tuple(n.kind != "rmw", n.base, ver[n.base]);
            delta[key] += n.k;
            if (n.kind != "rmw" && (delta[key] < 0 || delta[key] > 1))
                throw SpinNo{};
        }
        for (auto &r : n.defs)
            ver[r]++;
        for (auto s : n.succ) {
            if (s == T) {
                if (++paths > 256)
                    throw SpinNo{};
                for (auto &[k, v] : delta) {
                    (void)k;
                    if (v)
                        badBack.insert(a);
                }
            } else if (g.R.count(s)) {
                if (on.count(s))
                    throw SpinNo{};
                AS next = on;
                next.insert(s);
                walk(s, next, ver, delta);
            }
        }
    };
    try {
        walk(T, {T}, {}, {});
    } catch (SpinNo &) {
        return {};
    }
    g.valid = sub(back, badBack);
    if (g.valid.empty())
        return {};
    g.R = prune(g.valid);
    std::map<Addr, RS> live;
    bool more = true;
    while (more) {
        more = false;
        for (auto it = g.R.rbegin(); it != g.R.rend(); ++it) {
            Addr a = *it;
            auto &n = g.nodes[a];
            RS out;
            for (auto s : n.succ) {
                if (s == T) {
                    if (g.valid.count(a))
                        out = uni(out, live[T]);
                } else if (g.R.count(s))
                    out = uni(out, live[s]);
            }
            auto v = uni(n.uses, sub(out, n.defs));
            if (v != live[a]) {
                live[a] = v;
                more = true;
            }
        }
    }
    RS defs;
    int st = 0;
    bool polled = false;
    for (auto a : g.R) {
        auto &n = g.nodes[a];
        defs = uni(defs, n.defs);
        st += n.kind == "stc";
        if (one(n.kind, {"rmw", "acq", "rel", "tst"}) || begins(n.ins[0].m, "ld"))
            polled = true;
    }
    if (st && !polled)
        return {};
    for (auto a : g.R)
        if (g.nodes[a].kind == "stc" && intersects(g.nodes[a].uses, defs))
            g.vary.insert(a);
    if (!g.vary.empty()) {
        g.R.clear();
        return g;
    }
    if (st > 4 || intersects(live[T], defs))
        return {};
    return g;
}
Region Engine::region(Addr T, const std::vector<Addr> &bes) {
    AS ban;
    Region g;
    for (int j = 0; j < 8; j++) {
        g = region1(T, bes, ban, true);
        if (g.vary.empty())
            break;
        ban = uni(ban, g.vary);
    }
    auto old = region1(T, bes, {}, false);
    if (!g.R.empty() &&
        (old.R.empty() || std::includes(g.valid.begin(), g.valid.end(), old.valid.begin(), old.valid.end())))
        return g;
    return old;
}
void Engine::spinemit(const Region &g) {
    use(g.head);
    std::map<Addr, Addr> cpy, tramp;
    struct Fix {
        size_t idx;
        std::optional<Ins> x;
        Addr info;
        int mode;
    };
    std::vector<Fix> fix;
    std::vector<Addr> R(g.R.begin(), g.R.end());
    for (size_t j = 0; j < R.size(); j++) {
        Addr a = R[j];
        auto &n = g.nodes.at(a);
        cpy[a] = here();
        auto &x = n.ins[0];
        if (n.kind == "rmw") {
            for (int q = 0; q < 3; q++)
                emit(n.ins[q].w);
            fix.push_back({area->size(), n.ins[3], a, 1});
            emit(0);
        } else if (one(n.kind, {"acq", "rel"})) {
            cpy[a] = here() + 4 * n.pre;
            for (auto &y : n.ins)
                emit(y.w);
        } else if (n.kind == "tst") {
            emit(n.ins[0].w);
            emit(n.ins[1].w);
            fix.push_back({area->size(), n.ins[2], a, 2});
            emit(0);
        } else if (n.kind == "pc") {
            int r = num(norm(x.names[0]));
            require(r >= 0, "spin PC destination", a);
            Addr to = x.d.operands[1].imm;
            emit(ADRP(r, here(), to));
            if (x.m == "adr")
                emit(ADDI(r, r, to & 4095));
        } else if (n.kind == "br") {
            fix.push_back({area->size(), x, 0, 0});
            emit(0);
        } else
            emit(x.w);
        if (!(n.kind == "br" && x.m == "b")) {
            Addr ft = a + 4 * (n.ins.size() - n.pre);
            if (!(j + 1 < R.size() && R[j + 1] == ft)) {
                fix.push_back({area->size(), {}, ft, 0});
                emit(0);
            }
        }
    }
    Addr pause = here();
    emit(STP_PRE(29, 30, 31, -16));
    gatecall();
    emit(BLR(30));
    Addr ret = here();
    emit(LDP_POST(29, 30, 31, 16));
    gates.push_back({ret, "spin", 0, g.head});
    emit(B(here(), cpy.at(g.head)));
    auto out = [&](Addr t) {
        if (!tramp.count(t)) {
            tramp[t] = here();
            emit(B(here(), t));
        }
        return tramp[t];
    };
    for (auto &f : fix) {
        Addr pc = base + f.idx;
        uint32_t w;
        if (!f.x)
            w = B(pc, cpy.count(f.info) ? cpy.at(f.info) : f.info);
        else if (f.mode == 2)
            w = rebranch(*f.x, pc, out(f.info));
        else if (f.mode == 1)
            w = rebranch(*f.x, pc, cpy.at(f.info));
        else {
            Addr t = f.x->target();
            Addr d = t == g.head && g.valid.count(f.x->a) ? pause
                     : cpy.count(t) && t != g.head        ? cpy.at(t)
                     : f.x->m != "b"                      ? out(t)
                                                          : t;
            w = rebranch(*f.x, pc, d);
        }
        putbuf(*area, f.idx, w);
    }
    for (auto a : g.valid) {
        auto &x = g.nodes.at(a).ins[0];
        if (x.m == "b")
            branch(a, cpy.at(g.head));
        else {
            Addr st = here();
            emit(rebranch(x, here(), cpy.at(g.head)));
            emit(B(here(), a + 4));
            branch(a, st);
        }
    }
    records(gates.size());
}
void Engine::spins() {
    for (auto &[a, k] : kinds) {
        (void)k;
        touched.insert(a);
    }
    touched = uni(touched, done);
    for (auto &o : obs)
        touched.insert(o.a);
    for (auto &c : cuts)
        for (size_t j = 0; j < c.old.size(); j += 4)
            touched.insert(c.a + j);
    uint64_t spinDone = 0, spinTotal = 0;
    for (auto &[T, bes] : spinCand) spinTotal += bes.size() * 4;
    report(6, 0, spinTotal);
    for (auto &[T, bes] : spinCand) {
        report(6, spinDone, spinTotal); spinDone += bes.size() * 4;
        auto g = region(T, bes);
        if (!g.R.empty())
            regions.push_back(std::move(g));
        records(regions.size());
        spinCache.clear();
    }
    for (auto &g : regions)
        spinemit(g);
    report(6, spinTotal, spinTotal);
}
void Engine::output(nr_result &out) {
    size_t head = far ? fileLen : po;
    require(!far || patch.size() <= size_t(front - 0x4000), "front patch does not fit");
    require(head <= r.max_image_bytes && patch.size() <= r.max_patch_bytes, "output limit", 0, NR_LIMIT);
    auto &image = out.artifacts[NR_IMAGE];
    image = std::move(img);
    image.resize(head);
    image.insert(image.end(), patch.begin(), patch.end());
    std::ostringstream fix, gt, si, cu, ob, ch;
    fix << "# PATCH_OFF\t" << hx(po) << "\n# KEY_PAGE_OFF\t" << hx(key)
        << "\n# KEY_PAGE_SIZE\t0x4000\n# TEXT_END\t" << hx(text) << "\n# RO_OFF\t" << hx(ro)
        << "\n# DATA_OFF\t" << hx(data) << "\n";
    if (far)
        fix << "# IMAGE_SIZE\t" << hx(imageSize) << "\n# FILE_IMAGE_LEN\t" << hx(fileLen) << "\n";
    fix << "# KEY_K18_IDX\t0\n# KEY_KTLS_IDX\t1\n# KEY_KTLR_IDX\t2\n# KEY_KGATE_IDX\t3\n";
    if (fs)
        fix << "# FPS_STATE_OFF\t" << hx(fs) << "\n";
    if (fpsPtr)
        fix << "# FPS_TARGET_PTR\t" << hx(*fpsPtr) << "\n# FPS_TARGET_FIELD\t" << hx(fpsField) << "\n";
    if (fs)
        fix << "# RENDER_SCALE\t1\n";
    if (std::string(r.module_name) == "main")
        for (size_t j = 0; j < r.profile.capability_count; j++) {
            auto p = r.profile.capabilities[j];
            require(p && strnlen(p, 128) < 128 && std::regex_match(p, std::regex("[A-Z][A-Z0-9_]*")),
                    "invalid capability");
            fix << "# " << p << "\t1\n";
        }
    fix << "# BUILD_ID\t" << bid << "\n";
    for (auto &g : gates)
        gt << hx(g.ret) << '\t' << g.kind << '\t' << g.arg << '\t' << hx(g.orig) << '\n';
    for (auto &[a, k] : kinds)
        if (!done.count(a))
            si << hx(a) << '\t' << k.first << '\n';
    for (auto &w : windows)
        si << hx(w.front().a) << "\tW\t" << hx(w.back().a + 4) << '\n';
    for (auto &[a, j] : jumps)
        si << hx(a) << "\tJ\t" << hx(j.first) << '\t' << j.second << '\n';
    for (auto &[a, ts] : tables) {
        si << hx(a) << "\tT\t";
        for (size_t j = 0; j < ts.size(); j++)
            si << (j ? " " : "") << hx(ts[j]);
        si << '\n';
    }
    for (auto &g : regions) {
        si << hx(g.head) << "\tS\t";
        bool first = true;
        for (auto a : g.valid) {
            si << (first ? "" : " ") << hx(a);
            first = false;
        }
        si << '\n';
    }
    cu << "# BUILD_ID\t" << bid << '\n';
    for (auto &c : cuts)
        cu << hx(c.a) << '\t' << hexbytes(c.old.data(), c.old.size()) << '\t'
           << hexbytes(c.now.data(), c.now.size()) << '\t' << c.why << '\n';
    ob << "# BUILD_ID\t" << bid << '\n';
    for (auto &o : obs)
        ob << hx(o.a) << '\t' << o.id << '\t' << wordhex(o.w) << '\t' << o.why << '\n';
    AS changed;
    for (Addr a = 0; a < text; a += 4)
        if (memcmp(image.data() + a, original.data() + a, 4))
            changed.insert(a);
    for (auto &w : windows)
        for (Addr a = w.front().a; a < w.back().a + 4; a += 4)
            changed.insert(a);
    for (auto &g : regions)
        for (auto a : g.R) {
            auto &n = g.nodes.at(a);
            for (size_t j = 0; j < n.ins.size(); j++)
                changed.insert(a + 4 * (int(j) - n.pre));
        }
    ch << "# BUILD_ID\t" << bid << '\n';
    Addr start = -1, len = 0;
    for (auto a : changed) {
        if (start >= 0 && start + len == a)
            len += 4;
        else {
            if (start >= 0)
                ch << hx(start) << '\t' << hx(len) << '\n';
            start = a;
            len = 4;
        }
    }
    if (start >= 0)
        ch << hx(start) << '\t' << hx(len) << '\n';
    std::ostringstream *streams[] = {&fix, &gt, &si, &cu, &ob, &ch};
    for (int j = 0; j < 6; j++) {
        auto s = streams[j]->str();
        require(s.size() <= r.max_patch_bytes, "table output limit", 0, NR_LIMIT);
        out.artifacts[j + 1] = Bytes(s.begin(), s.end());
    }
}
extern "C" NR_API nr_status nr_rewrite(const nr_request *r, nr_result **out, nr_error *err) {
    return nr_rewrite_progress(r, out, err, nullptr);
}
extern "C" NR_API nr_status nr_rewrite_progress(const nr_request *r, nr_result **out, nr_error *err, nr_progress progress) {
    if (out)
        *out = nullptr;
    if (err)
        std::memset(err, 0, sizeof(*err));
    nr_status code = NR_OK;
    Addr at = 0;
    const char *msg = "";
    try {
        require(r && out, "null request/result", 0, NR_INVALID_INPUT);
        if (r->cancelled && r->cancelled(r->context))
            throw Failure{NR_CANCELLED, 0, "cancelled"};
        auto result = std::make_unique<nr_result>();
        Engine e(*r, progress);
        e.run(*result);
        *out = result.release();
        return NR_OK;
    } catch (const Failure &f) {
        code = f.status;
        at = f.at;
        msg = f.message;
    } catch (const std::bad_alloc &) {
        code = NR_ALLOCATION;
        msg = "allocation failed";
    } catch (...) {
        code = NR_INTERNAL;
        msg = "internal error";
    }
    if (err) {
        err->code = code;
        err->offset = at;
        std::strncpy(err->message, msg, sizeof(err->message) - 1);
    }
    return code;
}
extern "C" NR_API nr_bytes nr_result_get(const nr_result *r, nr_artifact a) {
    if (!r || a < NR_IMAGE || a > NR_CHANGED)
        return {nullptr, 0};
    auto &v = r->artifacts[a];
    return {v.data(), v.size()};
}
extern "C" NR_API void nr_result_free(nr_result *r) {
    delete r;
}
static int64_t signedbits(uint32_t w, int n) {
    return (int64_t(w) ^ (1LL << (n - 1))) - (1LL << (n - 1));
}
static std::optional<std::pair<uint32_t, Addr>> relative(uint32_t w) {
    if ((w & 0x7c000000) == 0x14000000)
        return {{0x03ffffff, signedbits(w & 0x3ffffff, 26) * 4}};
    if ((w & 0xff000010) == 0x54000000 || (w & 0x7e000000) == 0x34000000)
        return {{0x00ffffe0, signedbits((w >> 5) & 0x7ffff, 19) * 4}};
    if ((w & 0x7e000000) == 0x36000000)
        return {{0x0007ffe0, signedbits((w >> 5) & 0x3fff, 14) * 4}};
    return {};
}
static bool pcrel(uint32_t w) {
    return bool(relative(w)) || (w & 0x1f000000) == 0x10000000 || (w & 0x3b000000) == 0x18000000;
}
static std::string sha(const void *p, size_t n) {
    require(n <= UINT32_MAX, "digest size limit", 0, NR_LIMIT);
    uint8_t h[32];
    CC_SHA256(p, (CC_LONG)n, h);
    return hexbytes(h, 32);
}
static std::string quotejson(const std::string &s) {
    std::string out = "\"";
    static const char hex[] = "0123456789abcdef";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if (c < 32) {
            out += "\\u00";
            out += hex[c >> 4];
            out += hex[c & 15];
        } else {
            require(c < 128, "non-ASCII import fingerprint not yet ported", 0, NR_UNSUPPORTED);
            out += c;
        }
    }
    return out + '"';
}
void Engine::matchcuts() {
    if (conflictingFunctions)
        return;
    struct Miss {};
    using Body = std::pair<std::string, std::vector<std::pair<Addr, Addr>>>;
    std::map<Addr, Body> bodies;
    std::map<Addr, std::string> fps;
    std::map<Addr, std::string> imports;
    try {
        Addr mod = rd(4), dyn = mod + (int32_t)rd(mod + 4);
        std::map<Addr, Addr> tags;
        for (Addr p = dyn; p < std::min<Addr>(img.size() - 15, dyn + 4096 * 16); p += 16) {
            Addr t = rd(p, 8);
            if (!t)
                break;
            tags[t] = rd(p + 8, 8);
        }
        if (tags.count(20) && tags[20] == 7 && tags.count(23) && tags.count(2) && tags.count(6) &&
            tags.count(5)) {
            records(tags[2] / 24);
            for (Addr p = tags[23]; p < tags[23] + tags[2]; p += 24) {
                Addr slot = rd(p, 8);
                uint64_t info = rd(p + 8, 8);
                if (uint32_t(info) != 1026)
                    continue;
                Addr name = tags[5] + rd(tags[6] + (info >> 32) * 24);
                std::string s;
                while (rd(name, 1)) {
                    require(s.size() < 4096, "import name limit", name, NR_LIMIT);
                    s += char(rd(name++, 1));
                }
                imports[slot] = s;
            }
        }
    } catch (Failure &f) {
        if (f.status == NR_LIMIT || f.status == NR_CANCELLED)
            throw;
        imports.clear();
    }
    auto body = [&](Addr start) -> Body {
        if (bodies.count(start))
            return bodies[start];
        if (!functions.count(start))
            throw Miss{};
        Addr end = functions.at(start);
        auto ins = disfn(start, end);
        if (ins.size() * 4 != size_t(end - start))
            throw Miss{};
        Bytes normalized(img.begin() + start, img.begin() + end);
        std::vector<std::pair<Addr, Addr>> branches;
        for (size_t i = 0; i < ins.size(); i++) {
            uint32_t w = ins[i].w;
            auto rel = relative(w);
            uint32_t norm = uint32_t(normalized[4 * i]) | (uint32_t(normalized[4 * i + 1]) << 8) |
                            (uint32_t(normalized[4 * i + 2]) << 16) | (uint32_t(normalized[4 * i + 3]) << 24);
            if (rel) {
                Addr t = ins[i].a + rel->second;
                if (t < start || t >= end) {
                    norm &= ~rel->first;
                    branches.push_back({4 * i, t});
                }
            } else if ((w & 0x1f000000) == 0x10000000) {
                Addr d = signedbits(((w >> 29) & 3) | (((w >> 5) & 0x7ffff) << 2), 21);
                if ((w & 0x80000000) || ins[i].a + d < start || ins[i].a + d >= end)
                    norm &= 0x9f00001f;
            } else if ((w & 0x3b000000) == 0x18000000) {
                Addr t = ins[i].a + signedbits((w >> 5) & 0x7ffff, 19) * 4;
                if (t < start || t >= end)
                    norm &= 0xff00001f;
            }
            putbuf(normalized, 4 * i, norm);
            if ((w & 0x9f000000) != 0x90000000)
                continue;
            int rd = w & 31;
            for (size_t j = i + 1; j < std::min(i + 9, ins.size()); j++) {
                auto u = ins[j].w;
                if (((u & 0xffc00000) == 0x91000000 || (u & 0x3b000000) == 0x39000000) &&
                    int((u >> 5) & 31) == rd) {
                    uint32_t n = uint32_t(normalized[4 * j]) | (uint32_t(normalized[4 * j + 1]) << 8) |
                                 (uint32_t(normalized[4 * j + 2]) << 16) |
                                 (uint32_t(normalized[4 * j + 3]) << 24);
                    putbuf(normalized, 4 * j, n & ~0x3ffc00);
                    break;
                }
                if (relative(u) || one(ins[j].m, {"br", "blr", "ret"}))
                    break;
                if (ins[j].writes.count("x" + std::to_string(rd)) ||
                    ins[j].writes.count("w" + std::to_string(rd)) ||
                    (rd == 29 && ins[j].writes.count("fp")) || (rd == 30 && ins[j].writes.count("lr")))
                    break;
            }
        }
        Body b = {sha(normalized.data(), normalized.size()), branches};
        bodies[start] = b;
        records(bodies.size());
        return b;
    };
    auto callee = [&](Addr target) -> std::string {
        if (target >= 0 && target <= std::min<Addr>(text, img.size()) - 16) {
            uint32_t a = rd(target), b = rd(target + 4), c = rd(target + 8), d = rd(target + 12);
            if ((a & 0x9f00001f) == 0x90000010 && (b & 0xffc003ff) == 0xf9400211 &&
                (c & 0xffc003ff) == 0x91000210 && d == 0xd61f0220) {
                Addr page = (target & ~4095LL) +
                            (signedbits(((a >> 29) & 3) | (((a >> 5) & 0x7ffff) << 2), 21) * 4096),
                     lo = ((b >> 10) & 4095) * 8;
                if (((c >> 10) & 4095) == lo && imports.count(page + lo))
                    return "[\"import\"," + quotejson(imports[page + lo]) + "]";
            }
            if ((a & 0xffe0001f) == 0xd4000001 && b == 0xd65f03c0 && !functions.count(target))
                return "[\"svc\"," + std::to_string((a >> 5) & 65535) + "]";
        }
        if (!functions.count(target))
            throw Miss{};
        return "[\"function\"," + std::to_string(functions[target] - target) + "," +
               quotejson(body(target).first) + "]";
    };
    auto fingerprint = [&](Addr start) {
        if (fps.count(start))
            return fps[start];
        auto b = body(start);
        std::string s = "[" + quotejson(b.first) + ",[";
        bool first = true;
        for (auto [off, target] : b.second) {
            if (!first)
                s += ",";
            first = false;
            s += "[" + std::to_string(off) + "," + callee(target) + "]";
        }
        s += "]]";
        auto fp = sha(s.data(), s.size());
        fps[start] = fp;
        return fp;
    };
    auto matchfn = [&](const nr_match_function &f) -> std::pair<Addr, std::vector<Cut>> {
        require(f.size && f.size % 4 == 0 && f.fingerprint_count && f.fingerprints && f.cut_count && f.cuts,
                "invalid function rule", 0, NR_INVALID_INPUT);
        require(f.size <= r.max_function_bytes, "match function limit", 0, NR_LIMIT);
        records(f.cut_count);
        records(f.fingerprint_count);
        RS wanted;
        for (size_t j = 0; j < f.fingerprint_count; j++) {
            auto p = f.fingerprints[j];
            require(p && strnlen(p, 65) == 64 && std::regex_match(p, std::regex("[0-9a-f]{64}")),
                    "invalid function fingerprint", 0, NR_INVALID_INPUT);
            wanted.insert(p);
        }
        AS occupied;
        for (size_t j = 0; j < f.cut_count; j++) {
            auto &c = f.cuts[j];
            require(c.size && c.size % 4 == 0 && c.offset % 4 == 0 && c.offset <= f.size &&
                        c.size <= f.size - c.offset && c.original && c.replacement && c.reason &&
                        strnlen(c.reason, 4096) > 0 && strnlen(c.reason, 4096) < 4096,
                    "invalid function-relative cut", c.offset, NR_INVALID_INPUT);
            for (size_t k = 0; k < c.size; k += 4) {
                require(!occupied.count(c.offset + k), "overlapping function cuts", c.offset,
                        NR_INVALID_INPUT);
                occupied.insert(c.offset + k);
                uint32_t w = 0;
                for (int z = 0; z < 4; z++)
                    w |= uint32_t(c.replacement[k + z]) << (8 * z);
                require(!pcrel(w), "replacement contains PC-relative instruction", c.offset,
                        NR_INVALID_INPUT);
            }
        }
        std::vector<Addr> matches;
        for (auto [a, e] : functions)
            if (e - a == Addr(f.size)) {
                try {
                    if (wanted.count(fingerprint(a)))
                        matches.push_back(a);
                } catch (Miss &) {
                }
            }
        if (matches.size() != 1)
            throw Miss{};
        Addr a = matches[0];
        std::vector<Cut> out;
        for (size_t j = 0; j < f.cut_count; j++) {
            auto &c = f.cuts[j];
            bounds(a + c.offset, c.size);
            if (memcmp(img.data() + a + c.offset, c.original, c.size))
                throw Miss{};
            out.push_back({a + Addr(c.offset), Bytes(c.original, c.original + c.size),
                           Bytes(c.replacement, c.replacement + c.size),
                           std::string(c.reason) + " [function match]"});
        }
        return {a, out};
    };
    std::vector<std::pair<std::string, std::vector<const nr_match_rule *>>> groups;
    for (size_t j = 0; j < r.profile.match_count; j++) {
        auto &rule = r.profile.match_rules[j];
        require(rule.name && strnlen(rule.name, 4096) > 0 && strnlen(rule.name, 4096) < 4096 &&
                    rule.function_count && rule.functions,
                "invalid match rule", 0, NR_INVALID_INPUT);
        records(rule.function_count);
        auto it = std::find_if(groups.begin(), groups.end(), [&](auto &g) { return g.first == rule.name; });
        if (it == groups.end())
            groups.push_back({rule.name, {&rule}});
        else
            it->second.push_back(&rule);
    }
    std::vector<Cut> resolved;
    for (auto &group : groups) {
        std::vector<std::vector<Cut>> found;
        for (auto rule : group.second) {
            AS used;
            std::vector<Cut> got;
            try {
                for (size_t j = 0; j < rule->function_count; j++) {
                    auto [a, v] = matchfn(rule->functions[j]);
                    if (!used.insert(a).second)
                        throw Miss{};
                    got.insert(got.end(), v.begin(), v.end());
                }
                found.push_back(got);
            } catch (Miss &) {
            }
        }
        if (found.size() == 1)
            resolved.insert(resolved.end(), found[0].begin(), found[0].end());
    }
    AS occupied;
    for (auto &c : resolved)
        for (size_t j = 0; j < c.old.size(); j++) {
            require(occupied.insert(c.a + j).second, "function patches overlap", c.a);
            records(occupied.size());
        }
    for (auto &c : resolved) {
        memcpy(img.data() + c.a, c.now.data(), c.now.size());
        cuts.push_back(c);
    }
}
