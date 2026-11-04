#include "NSOLoader.h"
#include <CommonCrypto/CommonDigest.h>
#include <compression.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void u32(uint8_t *p, uint32_t n) { memcpy(p, &n, 4); }
static unsigned checks;
static void expect(bool value) { assert(value); checks++; }

int main(void) {
    uint8_t file[32768] = {0}, expected[16384] = {0}, image[16384], saved[32768];
    memcpy(file, "NSO0", 4); u32(file + 12, 0x3f); u32(file + 60, 1024);
    for (unsigned i = 0; i < 3072; i++) expected[i] = (uint8_t)(i % 31);
    for (unsigned i = 0; i < 2000; i++) expected[4096 + i] = (uint8_t)((i * 113) % 251);
    memset(expected + 8192, 0x67, 1024);
    const uint32_t offsets[] = {0, 4096, 8192}, lengths[] = {3072, 2000, 1024};
    size_t size = 256;
    for (unsigned i = 0; i < 3; i++) {
        size_t stored = compression_encode_buffer(file + size, sizeof(file) - size,
                                                  expected + offsets[i], lengths[i], NULL, COMPRESSION_LZ4_RAW);
        assert(stored && stored <= lengths[i]);
        u32(file + 16 + i * 16, (uint32_t)size); u32(file + 20 + i * 16, offsets[i]);
        u32(file + 24 + i * 16, lengths[i]); u32(file + 96 + i * 4, (uint32_t)stored);
        CC_SHA256(expected + offsets[i], lengths[i], file + 160 + i * 32);
        size += stored;
    }
    SatchelNSOInfo info; char error[256];
    expect(satchel_nso_inspect(file, size, &info, error, sizeof(error)));
    expect(info.image_size == 10240 && info.bss_size == 1024);
    expect(satchel_nso_decode(file, size, image, info.image_size, error, sizeof(error)));
    expect(!memcmp(image, expected, info.image_size));
    expect(satchel_nso_segment(&info, 1).memory_offset == 4096);
    expect(!satchel_nso_decode(file, size, image, info.image_size - 1, error, sizeof(error)));
    memcpy(saved, file, sizeof(file));

    file[160] ^= 1;
    expect(!satchel_nso_decode(file, size, image, info.image_size, error, sizeof(error)));
    expect(strstr(error, "SHA-256 mismatch") != NULL);
    for (unsigned i = 0; i < info.image_size; i++) assert(image[i] == 0);
    checks++;
    memcpy(file, saved, sizeof(file));
    u32(file + 36, 0); expect(!satchel_nso_inspect(file, size, &info, error, sizeof(error)));
    memcpy(file, saved, sizeof(file));
    u32(file + 32, 256); expect(!satchel_nso_inspect(file, size, &info, error, sizeof(error)));
    memcpy(file, saved, sizeof(file));
    u32(file + 60, UINT32_MAX); expect(!satchel_nso_inspect(file, size, &info, error, sizeof(error)));
    memcpy(file, saved, sizeof(file));
    u32(file + 12, 0xbf); expect(!satchel_nso_inspect(file, size, &info, error, sizeof(error)));
    expect(strstr(error, "ZBIC") != NULL);
    memcpy(file, saved, sizeof(file));
    u32(file + 0x94, UINT32_MAX); expect(!satchel_nso_inspect(file, size, &info, error, sizeof(error)));
    memcpy(file, saved, sizeof(file));
    expect(!satchel_nso_inspect(file, size - 1, &info, error, sizeof(error)));

    // Malformed raw block: offset zero and a truncated literal-length extension.
    memcpy(file, saved, sizeof(file));
    u32(file + 96, 3); file[256] = 0; file[257] = 0; file[258] = 0;
    expect(satchel_nso_inspect(file, size, &info, error, sizeof(error)));
    expect(!satchel_nso_decode(file, size, image, info.image_size, error, sizeof(error)));
    memcpy(file, saved, sizeof(file));
    u32(file + 96, 1); file[256] = 0xf0;
    expect(!satchel_nso_decode(file, size, image, info.image_size, error, sizeof(error)));

    // Independently laid-out uncompressed segments, including execute-only text.
    memset(file, 0, sizeof(file)); memcpy(file, "NSO0", 4); u32(file + 12, 0x78); u32(file + 60, 1024);
    size = 256;
    for (unsigned i = 0; i < 3; i++) {
        u32(file + 16 + i * 16, (uint32_t)size); u32(file + 20 + i * 16, offsets[i]);
        u32(file + 24 + i * 16, lengths[i]);
        memcpy(file + size, expected + offsets[i], lengths[i]);
        CC_SHA256(expected + offsets[i], lengths[i], file + 160 + i * 32); size += lengths[i];
    }
    expect(satchel_nso_inspect(file, size, &info, error, sizeof(error)));
    expect(satchel_nso_decode(file, size, image, info.image_size, error, sizeof(error)));
    expect(!memcmp(image, expected, info.image_size));

    // Deterministic malformed-header/stream mutation under ASan/UBSan. Any
    // accepted image remains bounded and is never executed by this test.
    uint32_t random = 0x12345678;
    memcpy(saved, file, sizeof(file));
    for (unsigned n = 0; n < 1000; n++) {
        memcpy(file, saved, sizeof(file));
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        file[random % size] ^= (uint8_t)(1 + random % 255);
        if (satchel_nso_inspect(file, size, &info, error, sizeof(error)) && info.image_size <= sizeof(image))
            (void)satchel_nso_decode(file, size, image, info.image_size, error, sizeof(error));
    }
    printf("NSO loader: %u assertions and 1000 sanitized mutations passed; Apple LZ4 encoder reference matched.\n", checks);
    return 0;
}
