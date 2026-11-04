#include "NSOLoader.h"
#include <CommonCrypto/CommonDigest.h>
#include <stdio.h>
#include <string.h>

static uint32_t u32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
static bool reject(char *error, size_t capacity, const char *message) {
    if (error && capacity) snprintf(error, capacity, "%s", message);
    return false;
}

/* NSO0 layout: https://switchbrew.org/wiki/NSO0 (revision 14440).
 * Independent bounded decoder. LZ4 block grammar follows the public format:
 * https://github.com/lz4/lz4/blob/dev/doc/lz4_Block_format.md
 */
bool satchel_nso_inspect(const uint8_t *bytes, size_t size, SatchelNSOInfo *info, char *error, size_t capacity) {
    if (error && capacity) error[0] = 0;
    if (!bytes || !info || size < 256 || size > SATCHEL_NSO_FILE_LIMIT)
        return reject(error, capacity, "NSO file length is outside the bounded loader limits.");
    memset(info, 0, sizeof(*info));
    if (memcmp(bytes, "NSO0", 4) || u32(bytes + 4) || u32(bytes + 8))
        return reject(error, capacity, "Unsupported NSO signature or version.");
    uint32_t flags = u32(bytes + 12);
    if (flags & ~UINT32_C(0x7f))
        return reject(error, capacity, "Unsupported NSO flags or ZBIC compression; this loader supports raw and LZ4 segments.");
    uint64_t previous = 0;
    for (unsigned i = 0; i < 3; i++) {
        const uint8_t *s = bytes + 16 + i * 16;
        SatchelNSOSegment segment = {u32(s), (flags & (1u << i)) ? u32(bytes + 96 + i * 4) : u32(s + 8), u32(s + 4), u32(s + 8)};
        if ((segment.memory_offset & 4095) || segment.memory_offset < previous ||
            segment.memory_offset > SATCHEL_NSO_IMAGE_LIMIT || segment.size > SATCHEL_NSO_IMAGE_LIMIT - segment.memory_offset)
            return reject(error, capacity, "NSO memory segments overlap, are unaligned, or exceed the image limit.");
        if (i == 0 && (segment.memory_offset != 0 || segment.size < 4 || (segment.size & 3)))
            return reject(error, capacity, "NSO text must start at offset zero and contain aligned instructions.");
        if (segment.size && (!segment.stored_size || segment.file_offset < 256 ||
            segment.file_offset > size || segment.stored_size > size - segment.file_offset))
            return reject(error, capacity, "NSO segment bytes are missing or truncated.");
        if (!segment.size && segment.stored_size)
            return reject(error, capacity, "Empty NSO segment has unexpected stored bytes.");
        for (unsigned j = 0; j < i; j++) {
            SatchelNSOSegment other = info->segments[j];
            if (segment.stored_size && other.stored_size &&
                (uint64_t)segment.file_offset < (uint64_t)other.file_offset + other.stored_size &&
                (uint64_t)other.file_offset < (uint64_t)segment.file_offset + segment.stored_size)
                return reject(error, capacity, "NSO file segments overlap.");
        }
        previous = (uint64_t)segment.memory_offset + segment.size;
        info->segments[i] = segment;
    }
    uint32_t bss = u32(bytes + 60);
    if (bss > SATCHEL_NSO_IMAGE_LIMIT - previous)
        return reject(error, capacity, "NSO BSS exceeds the bounded image limit.");
    // Optional module name and rodata-relative embedded/dynamic tables.
    uint32_t name_offset = u32(bytes + 28), name_size = u32(bytes + 44);
    if (name_size && (name_offset < 256 || name_offset > size || name_size > size - name_offset))
        return reject(error, capacity, "NSO module name extends outside the file.");
    for (unsigned at = 0x88; at <= 0x98; at += 8) {
        uint32_t offset = u32(bytes + at), length = u32(bytes + at + 4);
        if (length && (offset > info->segments[1].size || length > info->segments[1].size - offset))
            return reject(error, capacity, "NSO dynamic table extends outside rodata.");
    }
    info->flags = flags; info->bss_size = bss; info->image_size = (uint32_t)(previous + bss);
    memcpy(info->build_id, bytes + 64, 32);
    return true;
}

static bool extend_length(const uint8_t *source, size_t size, size_t *position, size_t limit, size_t *length) {
    unsigned extra;
    do {
        if (*position >= size) return false;
        extra = source[(*position)++];
        if (*length > limit || extra > limit - *length) return false;
        *length += extra;
    } while (extra == 255);
    return true;
}

bool satchel_lz4_decode(const uint8_t *source, size_t size, uint8_t *output, size_t capacity) {
    size_t in = 0, out = 0;
    while (in < size) {
        unsigned token = source[in++];
        size_t literals = token >> 4;
        if (literals == 15 && !extend_length(source, size, &in, capacity, &literals)) return false;
        if (literals > size - in || literals > capacity - out) return false;
        memcpy(output + out, source + in, literals); in += literals; out += literals;
        if (in == size) return out == capacity;
        if (size - in < 2) return false;
        size_t offset = (size_t)source[in] | (size_t)source[in + 1] << 8; in += 2;
        if (!offset || offset > out) return false;
        size_t match = token & 15;
        if (match == 15 && !extend_length(source, size, &in, capacity, &match)) return false;
        if (match > capacity - out || capacity - out - match < 4) return false;
        match += 4;
        // Byte-wise expansion intentionally supports overlapping LZ4 matches.
        for (size_t i = 0; i < match; i++) { output[out] = output[out - offset]; out++; }
    }
    return false; // A block must end with a literal sequence, not a match.
}

bool satchel_nso_decode(const uint8_t *bytes, size_t size, uint8_t *image, size_t image_size, char *error, size_t capacity) {
    SatchelNSOInfo info;
    if (!satchel_nso_inspect(bytes, size, &info, error, capacity)) return false;
    if (!image || image_size != info.image_size)
        return reject(error, capacity, "NSO destination must match its validated image size.");
    memset(image, 0, image_size);
    for (unsigned i = 0; i < 3; i++) {
        SatchelNSOSegment segment = info.segments[i];
        uint8_t *destination = image + segment.memory_offset;
        if (segment.size) {
            if (info.flags & (1u << i)) {
                if (!satchel_lz4_decode(bytes + segment.file_offset, segment.stored_size, destination, segment.size)) {
                    memset(image, 0, image_size);
                    return reject(error, capacity, "NSO LZ4 segment is malformed or has an unexpected decoded size.");
                }
            } else { memcpy(destination, bytes + segment.file_offset, segment.size); }
        }
        if (info.flags & (8u << i)) {
            uint8_t hash[CC_SHA256_DIGEST_LENGTH];
            CC_SHA256(destination, segment.size, hash);
            if (memcmp(hash, bytes + 160 + i * 32, 32)) {
                memset(image, 0, image_size);
                return reject(error, capacity, "NSO decoded segment SHA-256 mismatch.");
            }
        }
    }
    return true;
}

SatchelNSOSegment satchel_nso_segment(const SatchelNSOInfo *info, unsigned index) {
    return info && index < 3 ? info->segments[index] : (SatchelNSOSegment){0};
}
