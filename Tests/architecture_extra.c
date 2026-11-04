#include "Interpreter.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void native_extra_probe(uint64_t *values);
extern void native_integer_probe(uint64_t *values);
extern void native_simd_probe(uint64_t *values);
extern void native_simdlogic_probe(uint64_t *values);
extern void native_vectoradd_probe(uint64_t *values);
static void check_fault(const uint32_t *code, size_t count, const char *reason) {
    SatchelVM *vm = satchel_create((const uint8_t *)code, count * 4); assert(vm);
    assert(satchel_frame(vm, 0, 0, 100) == SATCHEL_FAULT);
    assert(strstr(satchel_error(vm), reason)); satchel_destroy(vm);
}
static void compare(const char *path, void (*probe)(uint64_t *)) {
    uint64_t native[16] = {0}; probe(native);
    FILE *f = fopen(path, "rb"); assert(f);
    assert(fseek(f, 0, SEEK_END) == 0); long size = ftell(f); rewind(f); assert(size > 0);
    uint8_t *bytes = malloc((size_t)size); assert(bytes);
    assert(fread(bytes, 1, (size_t)size, f) == (size_t)size); fclose(f);
    SatchelVM *vm = satchel_create(bytes, (size_t)size); assert(vm); free(bytes);
    SatchelResult result = satchel_frame(vm, 0, 0, 10000);
    if (result != SATCHEL_YIELD) fprintf(stderr, "%s\n", satchel_error(vm));
    assert(result == SATCHEL_YIELD);
    for (unsigned i = 0; i < 16; i++) {
        uint64_t actual = satchel_register(vm, i + 1);
        if (actual != native[i]) fprintf(stderr, "x%u native=%llx interpreted=%llx\n", i + 1, (unsigned long long)native[i], (unsigned long long)actual);
        assert(actual == native[i]);
    }
    satchel_destroy(vm);
}
int main(int argc, char **argv) {
    assert(argc == 6);
    compare(argv[1], native_extra_probe);
    compare(argv[2], native_integer_probe);
    compare(argv[3], native_simd_probe);
    compare(argv[4], native_simdlogic_probe);
    compare(argv[5], native_vectoradd_probe);
    /* misaligned LDXR and protected-code STLR must fault rather than touch host memory */
    const uint32_t misaligned[] = {0xD2A00402, 0x91000442, 0xC85F7C41};
    const uint32_t protected[] = {0xD2820002, 0xC89FFC41};
    check_fault(misaligned, 3, "Unaligned exclusive access");
    check_fault(protected, 2, "read-only code");
    const uint32_t cache_bad[] = {0x92800000, 0xD50B7E20};
    const uint32_t zero_prohibited[] = {0xD2A00400, 0xD50B7420};
    check_fault(cache_bad, 2, "outside sandbox");
    check_fault(zero_prohibited, 2, "DC ZVA prohibited");
    const uint32_t cache_info[] = {0xD53B0020, 0xD41E0021};
    SatchelVM *cache = satchel_create((const uint8_t *)cache_info, sizeof(cache_info)); assert(cache);
    assert(satchel_frame(cache, 0, 0, 100) == SATCHEL_YIELD);
    uint64_t ctr = satchel_register(cache, 0);
    assert((4u << (ctr & 15)) == 64 && (4u << ((ctr >> 16) & 15)) == 64);
    assert((ctr & 0xB0000000) == 0xB0000000); satchel_destroy(cache);
    const uint32_t prefetch[] = {0x92800000, 0xF9800000, 0xD41E0021};
    SatchelVM *hint = satchel_create((const uint8_t *)prefetch, sizeof(prefetch)); assert(hint);
    assert(satchel_frame(hint, 0, 0, 100) == SATCHEL_YIELD); satchel_destroy(hint);
    puts("PASS: 80 registers match native ARM64 exclusives, barriers, TLS, integer operations and SIMD string/memory operations; alignment/write protection enforced");
}
