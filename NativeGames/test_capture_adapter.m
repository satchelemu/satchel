// Synthetic capture test. No game bytes or translated shader code.
#import <Foundation/Foundation.h>
#include <CommonCrypto/CommonDigest.h>
#include <assert.h>
#include "nvn_obj.h"
static int mtl_addr(uint64_t address, uint64_t length, void **buffer, uint64_t *offset) {
    if (!address || length != 0x80) return 0;
    *buffer = (void *)(uintptr_t)address; *offset = 0; return 1;
}
int prog_key(const prog_t *p, unsigned stage, uint8_t key[8]) {
    const NVNshaderData *sd = &p->sd[stage];
    uint32_t size; memcpy(&size, (const uint8_t *)sd->control + 0x704, 4);
    CC_SHA256_CTX ctx; uint8_t digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Init(&ctx); CC_SHA256_Update(&ctx, sd->control, 0x880);
    CC_SHA256_Update(&ctx, (void *)(uintptr_t)sd->data, size); CC_SHA256_Final(digest, &ctx);
    memcpy(key, digest, 8); return 1;
}
#define MLOG(...) do { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } while (0)
#include "RuntimeCapture.inc"
int main(int argc, char **argv) {
    assert(argc == 2); setenv("DM_NVN_SHDUMP", argv[1], 1);
    @autoreleasepool {
        uint8_t controls[8][0x880] = {0}, codes[8][0x80] = {0}; prog_t programs[8] = {0};
        unsigned stages[] = {1, 5, 4, 2, 3, 0, 1, 5};
        for (unsigned i = 0; i < 8; i++) {
            uint32_t *ctl = (void *)controls[i], *code = (void *)codes[i];
            ctl[0] = 0x98761234; ctl[0x20/4] = 0x880; ctl[0x704/4] = 0x80; ctl[0x100/4] = i;
            code[0] = i == 5 ? 0x12345679 : 0x12345678; code[0x30/4] = stages[i] << 10;
            if (i == 5) { ctl[0x748/4] = 8; ctl[0x74c/4] = 4; ctl[0x750/4] = 1; }
            programs[i].serial = i + 1; programs[i].nstage = 1;
            programs[i].sd[0] = (NVNshaderData){.control=controls[i], .data=(uintptr_t)codes[i]};
            satchel_capture_program(&programs[i]); satchel_capture_program(&programs[i]);
        }
        const prog_t *bound[5] = {&programs[0], &programs[1]};
        satchel_capture_draw(bound); satchel_capture_draw(bound);
        bound[0] = &programs[6]; bound[1] = &programs[7]; satchel_capture_draw(bound);
        bound[1] = NULL; satchel_capture_draw(bound);
        prog_t doubleVertex = {.serial=9, .nstage=2, .sd={programs[0].sd[0],programs[6].sd[0]}};
        satchel_capture_program(&doubleVertex); bound[0] = &doubleVertex; bound[1] = &programs[1]; satchel_capture_draw(bound);
        NSString *directory = @(argv[1]); NSError *error;
        NSArray *files = [[NSFileManager defaultManager] contentsOfDirectoryAtPath:directory error:&error];
        assert(files && files.count == 18);
        NSString *pairs = [NSString stringWithContentsOfFile:[directory stringByAppendingPathComponent:@"draw-pairs.tsv"] encoding:NSUTF8StringEncoding error:&error];
        assert([[pairs componentsSeparatedByString:@"\n"] count] == 4);
        printf("PASS adapter: 8 unique shaders, 3 actual pairs, duplicate binds and null fragment handled\n");
    }
}
