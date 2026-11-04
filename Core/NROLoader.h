#ifndef SATCHEL_NRO_LOADER_H
#define SATCHEL_NRO_LOADER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { SATCHEL_NRO_FILE_LIMIT = 64 * 1024 * 1024, SATCHEL_NRO_MEMORY_SIZE = 64 * 1024 * 1024,
       SATCHEL_NRO_BASE = 0x10000, SATCHEL_NRO_HEAP = 0x2000000, SATCHEL_NRO_HEAP_SIZE = 0x1000000,
       SATCHEL_NRO_CONFIG = 0x3E00000, SATCHEL_NRO_TLS = 0x3E01000,
       SATCHEL_NRO_RETURN = 0x3E02000, SATCHEL_NRO_STACK = 0x3F00000 };
typedef struct {
    uint32_t image_size, text_offset, text_size, ro_offset, ro_size, data_offset, data_size, bss_size;
    uint8_t build_id[32];
    bool has_assets;
} SatchelNROInfo;

bool satchel_nro_inspect(const uint8_t *bytes, size_t size, SatchelNROInfo *info, char *error, size_t capacity);
#endif
