#ifndef SATCHEL_NSO_LOADER_H
#define SATCHEL_NSO_LOADER_H
#include "SwitchCompression.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { SATCHEL_NSO_FILE_LIMIT = 128 * 1024 * 1024, SATCHEL_NSO_IMAGE_LIMIT = 512 * 1024 * 1024 };
typedef struct {
    uint32_t file_offset, stored_size, memory_offset, size;
} SatchelNSOSegment;
typedef struct {
    uint32_t flags, image_size, bss_size;
    SatchelNSOSegment segments[3];
    uint8_t build_id[32];
} SatchelNSOInfo;

/* Bounded raw LZ4 block decoder shared with NCA compressed storage. */
bool satchel_lz4_decode(const uint8_t *source, size_t size, uint8_t *output, size_t capacity);

bool satchel_nso_inspect(const uint8_t *bytes, size_t size, SatchelNSOInfo *info, char *error, size_t capacity);
/* Produces data only. Does not allocate executable host memory or relocate/link. */
bool satchel_nso_decode(const uint8_t *bytes, size_t size, uint8_t *image, size_t image_size,
                   char *error, size_t capacity);
SatchelNSOSegment satchel_nso_segment(const SatchelNSOInfo *info, unsigned index);
#endif
