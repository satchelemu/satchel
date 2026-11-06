// Compile the implementation into this test only, to exercise internal branch boundaries.
#include "../src/rewriter.cpp"
#include <iostream>
int main() {
    unsigned count = 0;
    auto check = [&](bool good) {
        if (!good)
            throw std::runtime_error("encoder check failed");
        ++count;
    };
    check(B(0, 0) == 0x14000000);
    check(B(0, -(1LL << 27)) == 0x16000000);
    check(B(0, (1LL << 27) - 4) == 0x15ffffff);
    for (Addr to : {-(1LL << 27) - 4, (1LL << 27), 1LL, 2LL}) {
        bool rejected = false;
        try {
            (void)B(0, to);
        } catch (const Failure &f) {
            rejected = f.status == NR_BRANCH_RANGE;
        }
        check(rejected);
    }
    check(ADRP(0, 0, -(1LL << 32)) == 0x90800000);
    check(ADRP(0, 0, (1LL << 32) - 4096) == 0xf07fffe0);
    check(ADRP(17, 4095, 4096) == 0xb0000011);
    for (Addr to : {-(1LL << 32) - 4096, (1LL << 32)}) {
        bool rejected = false;
        try {
            (void)ADRP(0, 0, to);
        } catch (const Failure &f) {
            rejected = f.status == NR_BRANCH_RANGE;
        }
        check(rejected);
    }
    std::cout << count << " encoder boundary checks passed\n";
    Bytes image(0x3000);
    auto word = [&](size_t at, uint32_t w) {
        for (int j = 0; j < 4; j++)
            image.at(at + j) = w >> (8 * j);
    };
    word(0, 0x14000040);
    word(4, 0x1000);
    word(0x1000, 0x30444f4d);
    word(0x1004, 0x100);
    word(0x1010, 0x40);
    word(0x1014, 0x54);
    word(0x1040, 0x3b031b01);
    word(0x1044, 0x4c);
    word(0x1048, 1);
    word(0x104c, uint32_t(0x100 - 0x1040));
    word(0x1050, 0x60);
    word(0x1080, 12);
    word(0x1088, 0x00527a01);
    word(0x108c, 0x011e7801);
    image[0x1090] = 0x1b;
    word(0x10a0, 16);
    word(0x10a4, 0x24);
    word(0x10a8, uint32_t(0x100 - 0x10a8));
    word(0x10ac, 0x100);
    word(0x100, 0xd4000021);
    word(0x104, 0xd65f03c0);
    nr_request q{};
    q.abi_version = NR_ABI_VERSION;
    for (int j = 0; j < 3; j++)
        q.segments[j] = {image.data() + 0x1000 * j, 0x1000, uint64_t(0x1000 * j)};
    q.bss_size = 0x100;
    q.module_name = "fixture";
    q.max_image_bytes = 1 << 20;
    q.max_patch_bytes = 1 << 20;
    q.max_function_bytes = 1 << 18;
    q.max_records = 4096;
    auto invoke = [&](nr_status expected) {
        nr_result *out = nullptr;
        nr_error error{};
        auto before = image;
        auto status = nr_rewrite(&q, &out, &error);
        if (status != expected) {
            std::cerr << "unexpected status " << status << ": " << error.message << " at " << hx(error.offset)
                      << "\n";
            throw std::runtime_error("C API status");
        }
        check(image == before);
        check(expected == NR_OK ? out != nullptr : out == nullptr);
        if (out) {
            check(nr_result_get(out, NR_IMAGE).size > 0x10000);
            nr_result_free(out);
        }
    };
    unsigned before = count;
    invoke(NR_OK);
    q.cancelled = [](void *) { return 1; };
    invoke(NR_CANCELLED);
    q.cancelled = nullptr;
    size_t cap = q.max_patch_bytes;
    q.max_patch_bytes = 4;
    invoke(NR_LIMIT);
    q.max_patch_bytes = cap;
    q.abi_version = 99;
    invoke(NR_INVALID_INPUT);
    // ABI 1 ends at context: its pool sizes are never read, even when the bytes after it are not zero.
    q.snowdrop_pools_mb[0] = 9999;
    q.abi_version = 1;
    invoke(NR_OK);
    q.abi_version = NR_ABI_VERSION;
    invoke(NR_INVALID_INPUT);   // pools of 4,096 MB or more in all
    q.snowdrop_pools_mb[0] = 2048;
    invoke(NR_INVALID_INPUT);   // a zero pool beside nonzero ones
    q.snowdrop_pools_mb[1] = 512;
    q.snowdrop_pools_mb[2] = 512;
    invoke(NR_OK);
    q.snowdrop_pools_mb[0] = q.snowdrop_pools_mb[1] = q.snowdrop_pools_mb[2] = 0;
    uint8_t old[4] = {}, replacement[4] = {};
    nr_cut cut{0x100, old, replacement, 4, "synthetic"};
    q.profile.cuts = &cut;
    q.profile.cut_count = 1;
    invoke(NR_ASSERTION);
    q.profile.cuts = nullptr;
    q.profile.cut_count = 0;
    q.profile.recipe = 99;
    invoke(NR_UNSUPPORTED);
    q.profile.recipe = 0;
    // Metadata mutation cases are synthetic and do not contain game instructions.
    for (size_t field : {size_t(4), size_t(0x1010), size_t(0x1014), size_t(0x1044), size_t(0x1048),
                         size_t(0x104c), size_t(0x1050), size_t(0x10a4), size_t(0x10a8), size_t(0x10ac)}) {
        Bytes original = image;
        for (uint32_t value : {0u, 1u, 0x7fffffffu, 0x80000000u, 0xfffffffcu, 0xffffffffu}) {
            word(field, value);
            nr_result *out = nullptr;
            nr_error error{};
            auto status = nr_rewrite(&q, &out, &error);
            check(status == NR_OK ? out != nullptr : out == nullptr);
            nr_result_free(out);
            image = original;
        }
    }
    std::cout << count - before << " native API and malformed-metadata checks passed\n";
}
