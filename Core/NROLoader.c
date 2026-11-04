#include "NROLoader.h"
#include <stdio.h>
#include <string.h>

static uint32_t u32(const uint8_t *p) { uint32_t x; memcpy(&x, p, 4); return x; }
static uint64_t u64(const uint8_t *p) { uint64_t x; memcpy(&x, p, 8); return x; }
static bool reject(char *error, size_t capacity, const char *message) {
    if (error && capacity) snprintf(error, capacity, "%s", message);
    return false;
}

/* NRO0 layout from switchbrew/libnx nx/include/switch/nro.h. Offsets are both
 * file offsets and offsets from the load base. No native host pointers are used.
 */
bool satchel_nro_inspect(const uint8_t *bytes, size_t size, SatchelNROInfo *info, char *error, size_t capacity) {
    if (error && capacity) error[0] = 0;
    if (!bytes || !info || size < 0x80) return reject(error, capacity, "File is too short for an NRO header.");
    memset(info, 0, sizeof(*info));
    if (size > SATCHEL_NRO_FILE_LIMIT) return reject(error, capacity, "This prototype accepts NRO files up to 64 MiB.");
    if (memcmp(bytes + 0x10, "NRO0", 4)) return reject(error, capacity, "Not a Switch NRO0 executable. Choose a .nro homebrew file.");
    if (u32(bytes + 0x14) != 0 || u32(bytes + 0x1C) != 0) return reject(error, capacity, "Unsupported NRO version or flags.");
    uint32_t image = u32(bytes + 0x18);
    if (image < 0x80 || image > size || (image & 0xFFF)) return reject(error, capacity, "NRO image length is truncated or not page aligned.");
    uint32_t previous = 0;
    for (unsigned i = 0; i < 3; i++) {
        uint32_t offset = u32(bytes + 0x20 + i * 8), length = u32(bytes + 0x24 + i * 8);
        if ((offset & 0xFFF) || (length & 0xFFF) || offset != previous || offset > image || length > image - offset)
            return reject(error, capacity, "NRO segments must be page aligned, contiguous, and within the image.");
        if (i == 0 && length < 0x80) return reject(error, capacity, "NRO text segment is missing its header.");
        previous = offset + length;
    }
    if (previous != image) return reject(error, capacity, "NRO segments do not cover the declared image.");
    uint32_t bss = u32(bytes + 0x38);
    if ((uint64_t)SATCHEL_NRO_BASE + image + bss > SATCHEL_NRO_HEAP)
        return reject(error, capacity, "NRO image and BSS exceed this prototype's 32 MiB module region.");
    if ((u32(bytes) & 0xFC000000) != 0x14000000)
        return reject(error, capacity, "NRO entrypoint does not begin with the required branch.");
    uint32_t mod = u32(bytes + 4);
    if (mod && (mod > image - 28 || (mod & 3) || memcmp(bytes + mod, "MOD0", 4)))
        return reject(error, capacity, "Invalid MOD0 offset or signature.");
    if (size > image) {
        if (size - image < 56 || memcmp(bytes + image, "ASET", 4) || u32(bytes + image + 4) != 0)
            return reject(error, capacity, "Unsupported or truncated NRO asset header.");
        for (unsigned i = 0; i < 3; i++) {
            uint64_t offset = u64(bytes + image + 8 + i * 16), length = u64(bytes + image + 16 + i * 16);
            if (length && (offset < 56 || offset > size - image || length > size - image - offset))
                return reject(error, capacity, "NRO asset extends outside the file.");
        }
        info->has_assets = true;
    }
    info->image_size = image; info->bss_size = bss;
    info->text_offset = u32(bytes + 0x20); info->text_size = u32(bytes + 0x24);
    info->ro_offset = u32(bytes + 0x28); info->ro_size = u32(bytes + 0x2C);
    info->data_offset = u32(bytes + 0x30); info->data_size = u32(bytes + 0x34);
    memcpy(info->build_id, bytes + 0x40, 32);
    return true;
}
