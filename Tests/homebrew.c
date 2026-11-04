#include "Interpreter.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *f = fopen(path, "rb"); assert(f);
    assert(fseek(f, 0, SEEK_END) == 0); long length = ftell(f); assert(length > 0);
    *size = (size_t)length; rewind(f);
    uint8_t *bytes = malloc(*size); assert(bytes);
    assert(fread(bytes, 1, *size, f) == *size); fclose(f); return bytes;
}
static void word(uint8_t *p, unsigned offset, uint32_t value) { memcpy(p + offset, &value, 4); }
static void invalid(uint8_t *bytes, size_t size) {
    SatchelNROInfo info; char error[256];
    assert(!satchel_nro_inspect(bytes, size, &info, error, sizeof error)); assert(error[0]);
    assert(!satchel_create_nro(bytes, size, error, sizeof error));
}
static void guest_fault(const uint8_t *original, size_t size, const uint32_t *instructions, size_t count, const char *message) {
    uint8_t *bytes = malloc(size); assert(bytes); memcpy(bytes, original, size);
    memcpy(bytes + 0x80, instructions, count * 4);
    char error[256]; SatchelVM *vm = satchel_create_nro(bytes, size, error, sizeof error); assert(vm);
    assert(satchel_frame(vm, 0, 0, 100) == SATCHEL_FAULT);
    assert(strstr(satchel_error(vm), message));
    satchel_destroy(vm); free(bytes);
}
int main(int argc, char **argv) {
    assert(argc >= 2);
    size_t size; uint8_t *bytes = read_file(argv[1], &size);
    char error[256]; SatchelNROInfo info;
    assert(satchel_nro_inspect(bytes, size, &info, error, sizeof error));
    assert(info.text_size == 4096 && info.ro_size == 4096 && info.data_size == 4096 && info.bss_size == 4096);
    SatchelVM *vm = satchel_create_nro(bytes, size, error, sizeof error); assert(vm);
    assert(satchel_register(vm, 0) == SATCHEL_NRO_CONFIG && satchel_register(vm, 1) == UINT64_MAX);
    SatchelResult initial = satchel_frame(vm, 0, 0, 10000);
    if (initial != SATCHEL_EXITED) fprintf(stderr, "NRO run result %d: %s\n", initial, satchel_error(vm));
    assert(initial == SATCHEL_EXITED);
    assert(satchel_exit_code(vm) == 0);
    assert(strstr(satchel_output(vm), "Hello from Switch homebrew!"));
    assert(!strstr(satchel_output(vm), "FAIL"));
    printf("PASS: NRO mapped, real loader ABI, BSS, stack, signed loads, ADR, logical immediates, TLS, SVC output/ticks, clean return (%llu instructions)\n", (unsigned long long)satchel_instructions(vm));
    satchel_destroy(vm);

    uint8_t *bad = malloc(size + 56); assert(bad);
    invalid(bytes, 0); invalid(bytes, 127); invalid(bytes, size - 1);
    const unsigned fields[] = {0x10, 0x14, 0x18, 0x1C, 0x20, 0x24, 0x28, 0x2C, 0x30, 0x34, 0x38, 4};
    for (unsigned i = 0; i < sizeof(fields) / sizeof(fields[0]); i++) {
        memcpy(bad, bytes, size); word(bad, fields[i], UINT32_MAX); invalid(bad, size);
    }
    memcpy(bad, bytes, size); memset(bad + size, 0, 56); memcpy(bad + size, "ASET", 4);
    word(bad, (unsigned)size + 8, UINT32_MAX); word(bad, (unsigned)size + 16, 64); invalid(bad, size + 56);
    const uint32_t write_text[] = {0xD2A00029, 0xF900013F}; /* x9=0x10000; str xzr,[x9] */
    const uint32_t write_ro[] = {0xD2820009, 0xF2A00029, 0xF900013F}; /* x9=0x11000 */
    const uint32_t unmapped[] = {0xD2800009, 0xF9400120};
    const uint32_t debug_bad[] = {0x92800000, 0xD2800081, 0xD40004E1};
    const uint32_t service[] = {0xD4000101}; /* svc CreateThread: still unsupported */
    const uint32_t private_abi[] = {0xD41E0021};
    guest_fault(bytes, size, write_text, 2, "protected");
    guest_fault(bytes, size, write_ro, 3, "protected");
    guest_fault(bytes, size, unmapped, 2, "unmapped");
    guest_fault(bytes, size, debug_bad, 3, "outside sandbox");
    guest_fault(bytes, size, service, 1, "Horizon SVC not implemented: 0x8");
    guest_fault(bytes, size, private_abi, 1, "Horizon SVC not implemented: 0xf001");
    puts("PASS: malformed/truncated/overflowing headers, asset bounds, memory permissions, invalid syscall pointers, unsupported SVCs, private ABI separation");
    free(bad); free(bytes);

    if (argc == 3) {
        bytes = read_file(argv[2], &size);
        assert(satchel_nro_inspect(bytes, size, &info, error, sizeof error));
        vm = satchel_create_nro(bytes, size, error, sizeof error); assert(vm);
        SatchelResult result = satchel_frame(vm, 0, 0, 1000000);
        printf("Third-party NRO: %zu bytes, %llu instructions, result=%d, %s\n", size, (unsigned long long)satchel_instructions(vm), result, satchel_error(vm));
        assert(result == SATCHEL_FAULT || result == SATCHEL_BUDGET || result == SATCHEL_EXITED);
        satchel_destroy(vm); free(bytes);
    }
    return 0;
}
