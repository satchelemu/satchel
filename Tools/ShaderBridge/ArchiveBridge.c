#include "ArchiveBridge.h"
#include <zstd.h>

void *satchel_zstd_create(const void *dictionary, size_t size, int window_log_limit) {
    if (size > 16 * 1024 * 1024 || (!dictionary && size) || window_log_limit < 10 || window_log_limit > 27) return NULL;
    ZSTD_DCtx *context = ZSTD_createDCtx();
    if (!context) return NULL;
    if (ZSTD_isError(ZSTD_DCtx_setParameter(context, ZSTD_d_windowLogMax, window_log_limit)) ||
        ZSTD_isError(ZSTD_DCtx_loadDictionary(context, dictionary, size))) {
        ZSTD_freeDCtx(context);
        return NULL;
    }
    return context;
}
void satchel_zstd_free(void *context) { ZSTD_freeDCtx(context); }
uint32_t satchel_zstd_dictionary_id(const void *header, size_t size) { return ZSTD_getDictID_fromFrame(header, size); }
int satchel_zstd_step(void *context, const void *input, size_t *input_size, void *output, size_t *output_size) {
    if (!context || !input_size || !output_size || (!input && *input_size) || (!output && *output_size)) return -1;
    ZSTD_inBuffer in = {input, *input_size, 0};
    ZSTD_outBuffer out = {output, *output_size, 0};
    size_t result = ZSTD_decompressStream(context, &out, &in);
    *input_size = in.pos; *output_size = out.pos;
    return ZSTD_isError(result) ? -2 : result == 0 ? 0 : 1;
}
