#include "Interpreter.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

extern void native_probe(uint64_t *values);
int main(int argc, char **argv) {
    assert(argc == 2);
    uint64_t native[16] = {0}; native_probe(native);
    FILE *f = fopen(argv[1], "rb"); assert(f);
    assert(fseek(f, 0, SEEK_END) == 0); long size = ftell(f); rewind(f); assert(size > 0);
    uint8_t *bytes = malloc((size_t)size); assert(bytes);
    assert(fread(bytes, 1, (size_t)size, f) == (size_t)size); fclose(f);
    SatchelVM *vm = satchel_create(bytes, (size_t)size); assert(vm); free(bytes);
    SatchelResult result = satchel_frame(vm, 0, 0, 1000);
    if (result != SATCHEL_YIELD) fprintf(stderr, "%s\n", satchel_error(vm));
    assert(result == SATCHEL_YIELD);
    for (unsigned i = 0; i < 16; i++) {
        uint64_t actual = satchel_register(vm, i + 1);
        if (actual != native[i]) fprintf(stderr, "x%u native=%llx interpreted=%llx\n", i + 1, (unsigned long long)native[i], (unsigned long long)actual);
        assert(actual == native[i]);
    }
    satchel_destroy(vm);
    puts("PASS: 16 registers match physical ARM64 CPU execution (logical immediates, high multiply, bit operations, PC-relative addressing, signed loads, SIMD DUP/transfers)");
}
