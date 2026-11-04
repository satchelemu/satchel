#include "HomebrewFormat.h"
#include "NROLoader.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void check_truncation(size_t offset, const char *magic, SatchelHomebrewFormat expected) {
    size_t length = offset + 4;
    uint8_t *bytes = calloc(length, 1); assert(bytes);
    memcpy(bytes + offset, magic, 4);
    /* Test the actual allocation boundary, not just a small reported length. */
    for (size_t size = 0; size < length; size++) {
        uint8_t *short_bytes = malloc(size ? size : 1); assert(short_bytes);
        memcpy(short_bytes, bytes, size);
        assert(satchel_homebrew_format(short_bytes, size) == SATCHEL_FORMAT_UNKNOWN);
        free(short_bytes);
    }
    assert(satchel_homebrew_format(bytes, length) == expected);
    free(bytes);
}

static void check_nro_file(const char *path) {
    FILE *f = fopen(path, "rb"); assert(f);
    assert(fseek(f, 0, SEEK_END) == 0); long count = ftell(f); assert(count > 0);
    rewind(f);
    uint8_t *bytes = malloc((size_t)count); assert(bytes);
    assert(fread(bytes, 1, (size_t)count, f) == (size_t)count); fclose(f);
    assert(satchel_homebrew_format(bytes, (size_t)count) == SATCHEL_FORMAT_NRO);
    SatchelNROInfo info; char error[256];
    assert(satchel_nro_inspect(bytes, (size_t)count, &info, error, sizeof error));
    free(bytes);
}

int main(int argc, char **argv) {
    assert(satchel_homebrew_format(NULL, 0) == SATCHEL_FORMAT_UNKNOWN);
    assert(satchel_homebrew_format(NULL, SATCHEL_HOMEBREW_PROBE_SIZE) == SATCHEL_FORMAT_UNKNOWN);
    check_truncation(0x10, "NRO0", SATCHEL_FORMAT_NRO);
    check_truncation(0, "PFS0", SATCHEL_FORMAT_PFS0);
    check_truncation(0x100, "HEAD", SATCHEL_FORMAT_XCI);
    check_truncation(0x1100, "HEAD", SATCHEL_FORMAT_XCI);
    check_truncation(0, "NSO0", SATCHEL_FORMAT_NSO);
    check_truncation(0, "PK\003\004", SATCHEL_FORMAT_ZIP);
    check_truncation(0, "PK\005\006", SATCHEL_FORMAT_ZIP);
    check_truncation(0, "PK\007\010", SATCHEL_FORMAT_ZIP);

    uint8_t bytes[SATCHEL_HOMEBREW_PROBE_SIZE] = {0};
    memcpy(bytes + 0x11, "NRO0", 4);
    assert(satchel_homebrew_format(bytes, sizeof bytes) == SATCHEL_FORMAT_UNKNOWN);
    memset(bytes, 0, sizeof bytes); memcpy(bytes + 0x10, "NRO0", 4);
    SatchelNROInfo info; char error[256];
    assert(satchel_homebrew_format(bytes, sizeof bytes) == SATCHEL_FORMAT_NRO);
    assert(!satchel_nro_inspect(bytes, sizeof bytes, &info, error, sizeof error));
    /* An NRO's code/data may legitimately spell HEAD at an XCI magic offset. */
    memcpy(bytes + 0x100, "HEAD", 4);
    assert(satchel_homebrew_format(bytes, sizeof bytes) == SATCHEL_FORMAT_NRO);
    memcpy(bytes, "PFS0", 4);
    assert(satchel_homebrew_format(bytes, sizeof bytes) == SATCHEL_FORMAT_PFS0);
    memset(bytes, 0xFF, sizeof bytes); memcpy(bytes, "PFS0", 4);
    assert(satchel_homebrew_format(bytes, sizeof bytes) == SATCHEL_FORMAT_PFS0);
    assert(strstr(satchel_homebrew_format_error(SATCHEL_FORMAT_PFS0), "not implemented"));
    assert(strstr(satchel_homebrew_format_error(SATCHEL_FORMAT_XCI), "not implemented"));
    for (int kind = SATCHEL_FORMAT_UNKNOWN; kind <= SATCHEL_FORMAT_ZIP; kind++) {
        assert((satchel_homebrew_format_error((SatchelHomebrewFormat)kind)[0] == 0) == (kind == SATCHEL_FORMAT_NRO));
    }
    assert(satchel_homebrew_format_error((SatchelHomebrewFormat)999)[0]);
    for (int i = 1; i < argc; i++) check_nro_file(argv[i]);
    puts("PASS: content-based NRO/PFS0/XCI/NSO/ZIP routing, all signature truncations, malformed NRO validation, container precedence, renamed NRO files");
    return 0;
}
