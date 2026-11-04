#include "Interpreter.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

volatile uint32_t reference_pixels[160 * 240];
volatile int reference_state[7];
extern void reference_main(void);
static SatchelVM *vm;
static jmp_buf done;
static int frame, previous_touch = 80, previous_reset;
static uint64_t checkpoint;

void reference_yield(int *touch, int *reset) {
    SatchelResult result = satchel_frame(vm, previous_touch, previous_reset, 1000000);
    if (result != SATCHEL_YIELD) {
        fprintf(stderr, "Frame %d result %d: %s\n", frame, result, satchel_error(vm)); exit(1);
    }
    frame++;
    for (unsigned i = 0; i < 7; i++) {
        if (satchel_state(vm, i) != reference_state[i]) {
            fprintf(stderr, "Frame %d state %u: interpreted %d != native %d\n", frame, i, satchel_state(vm, i), reference_state[i]); exit(1);
        }
    }
    if (memcmp(satchel_pixels(vm), (const void *)reference_pixels, sizeof(reference_pixels))) {
        fprintf(stderr, "Frame %d framebuffer differs from native reference\n", frame); exit(1);
    }
    if (frame == 600) checkpoint = satchel_checksum(vm);
    if (frame == 1800) longjmp(done, 1);
    /* First 600 frames match the device validation. Then test missed balls,
       losing lives, edge clamps, and reset through the actual guest loop. */
    *touch = frame < 600 ? reference_state[3] : frame < 1200 ? -40 : 220;
    *reset = frame == 1300;
    previous_touch = *touch; previous_reset = *reset;
}

static void boundaries(void) {
    const uint32_t unknown[] = {0x00000000};
    SatchelVM *test = satchel_create((const uint8_t *)unknown, sizeof unknown);
    assert(test && satchel_frame(test, 0, 0, 1) == SATCHEL_FAULT);
    assert(strstr(satchel_error(test), "Unsupported ARM64")); satchel_destroy(test);
    const uint32_t loop[] = {0x14000000};
    test = satchel_create((const uint8_t *)loop, sizeof loop);
    assert(satchel_frame(test, 0, 0, 100) == SATCHEL_BUDGET);
    assert(satchel_instructions(test) == 100); satchel_destroy(test);
    /* mov x0, #-1; ldr x1, [x0] */
    const uint32_t outside[] = {0x92800000, 0xF9400001};
    test = satchel_create((const uint8_t *)outside, sizeof outside);
    assert(satchel_frame(test, 0, 0, 10) == SATCHEL_FAULT);
    assert(strstr(satchel_error(test), "outside sandbox")); satchel_destroy(test);
    /* mov x0, #0x1000; str xzr, [x0] */
    const uint32_t code_write[] = {0xD2820000, 0xF900001F};
    test = satchel_create((const uint8_t *)code_write, sizeof code_write);
    assert(satchel_frame(test, 0, 0, 10) == SATCHEL_FAULT);
    assert(strstr(satchel_error(test), "read-only code")); satchel_destroy(test);
    const uint32_t service[] = {0xD4000021};
    test = satchel_create((const uint8_t *)service, sizeof service);
    assert(satchel_frame(test, 0, 0, 10) == SATCHEL_FAULT);
    assert(strstr(satchel_error(test), "Unsupported service")); satchel_destroy(test);
    assert(satchel_create((const uint8_t *)loop, 3) == NULL);
    puts("PASS: invalid opcode, instruction budget, memory bounds, code protection, unknown service, malformed image");
}

int main(int argc, char **argv) {
    if (argc != 3) return 2;
    FILE *f = fopen(argv[1], "rb"); assert(f);
    assert(fseek(f, 0, SEEK_END) == 0); long size = ftell(f); rewind(f);
    assert(size > 0); uint8_t *code = malloc((size_t)size); assert(code);
    assert(fread(code, 1, (size_t)size, f) == (size_t)size); fclose(f);
    vm = satchel_create(code, (size_t)size); free(code); assert(vm);
    clock_t start = clock();
    if (!setjmp(done)) reference_main();
    double seconds = (double)(clock() - start) / CLOCKS_PER_SEC;
    printf("PASS: %d frames, every pixel and all 7 state fields match independent native C execution\n", frame);
    printf("Executed %llu guest instructions in %.3f CPU seconds (includes native reference and comparisons)\n", (unsigned long long)satchel_instructions(vm), seconds);
    FILE *expected = fopen(argv[2], "w"); assert(expected);
    fprintf(expected, "{\"frames\":600,\"framebufferFNV1a\":\"%016llx\",\"workload\":\"Original ARM64 brick-breaker; private host ABI\"}\n", (unsigned long long)checkpoint);
    fclose(expected);
    printf("Device validation expected checksum: %016llx\n", (unsigned long long)checkpoint);
    satchel_destroy(vm); boundaries();
    return 0;
}
