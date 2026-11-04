// Synthetic inputs exercise the actual engine SetShaders and key side table.
#include <CoreFoundation/CoreFoundation.h>
#include <CommonCrypto/CommonDigest.h>
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { uint64_t data; const void *control; } NVNshaderData;
typedef struct { uint32_t magic, nstage; uint64_t serial; NVNshaderData sd[6]; } prog_t;
typedef prog_t NVNprogram;
typedef int NVNdevice;
typedef int NVNboolean;
#define NVN(name, result, ...) result name(__VA_ARGS__)
#define MAGIC(a,b,c,d) 1
#define LOG(...) ((void)0)
#define OLOG(...) ((void)0)
struct dm_profile { unsigned shader_skip_off, shader_skip_len; };
static const struct dm_profile *dm_prof(void) { static struct dm_profile profile; return &profile; }
static int mapped;
static int mtl_addr(uint64_t address, size_t size, void **buffer, uint64_t *offset) { return mapped; }
static uint64_t nvn_new_serial(void) { static uint64_t serial; return ++serial; }
static void mtl_prog_release(prog_t *program) {}
static void dm_backtrace(uint64_t frame) { abort(); }
static unsigned prefetch_count;
static uint8_t copied_control[0x880], copied_code[64];
void satchel_phone_shader_prefetch(const char *key, const void *control, size_t controls, const void *code, size_t count) {
    assert(mapped && strlen(key) == 16 && controls <= sizeof copied_control && count == sizeof copied_code);
    memcpy(copied_control, control, controls); memcpy(copied_code, code, count); prefetch_count++;
}
#include "phone-programs.inc"

int main(int argc, char **argv) {
    assert(argc == 2);
    int snapshot = strcmp(argv[1], "offline") != 0;
    unsetenv("DM_NVN_SHDUMP"); unsetenv("DM_PHONE_SHADERS"); unsetenv("DM_NVN_PROGTRACE");
    if (!strcmp(argv[1], "phone")) setenv("DM_PHONE_SHADERS", "1", 1);
    if (!strcmp(argv[1], "capture")) setenv("DM_NVN_SHDUMP", "unused-fixture", 1);
    if (!snapshot) setenv("DM_PHONE_SHADERS", "0", 1);
    for (int delayed = 0; delayed < 2; delayed++) {
        for (uint32_t size = 0x760; size <= 0x880; size += 0x120) {
            uint8_t *control = calloc(1, size), code[64] = {0};
            uint32_t codeSize = sizeof code;
            memcpy(control + 0x20, &size, 4); memcpy(control + 0x704, &codeSize, 4);
            control[0x710] = 73; code[0] = 19;
            prog_t program;
            assert(nvnProgramInitialize(&program, NULL));
            NVNshaderData input = {(uintptr_t)code, control};
            uint8_t expected[8], actual[8]; mapped = 1;
            assert(keys_hash(&input, 1, expected)); mapped = !delayed;
            unsigned before = prefetch_count;
            assert(nvnProgramSetShaders(&program, 1, &input));
            assert(prefetch_count - before == (!strcmp(argv[1], "phone") && !delayed));
            assert((program.sd[0].control != control) == snapshot);
            if (snapshot) { memset(control, 0, size); free(control); }
            if (prefetch_count != before) { assert(copied_control[0x710] == 73 && copied_code[0] == 19); }
            mapped = 1;
            assert(prog_key(&program, 0, actual) && !memcmp(expected, actual, 8));
            // Replacement can refer to the previous owned snapshot.
            NVNshaderData replacement = program.sd[0];
            assert(nvnProgramSetShaders(&program, 1, &replacement));
            assert(prog_key(&program, 0, actual) && !memcmp(expected, actual, 8));
            assert(nvnProgramSetShaders(&program, 0, &replacement));
            prog_keys_t *keys = pk_get(&program, 0);
            for (int i = 0; i < 6; i++) assert(keys->capture[i] == NULL);
            nvnProgramFinalize(&program); assert(pk_get(&program, 0) == NULL);
            if (!snapshot) free(control);
        }
    }
    assert(prefetch_count == (!strcmp(argv[1], "phone") ? 6 : 0));
    puts("PASS: control lifetime, mapped prefetch, deferred key, replacement and finalize");
}
