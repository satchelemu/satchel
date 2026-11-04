// Reuse the pinned MIT fixture's real Metal upload and literal-pixel checks.
#define main pinned_image_alias_main
#include "t_image_alias.m"
#undef main

int main(void) { @autoreleasepool {
    setenv("DM_PSO_PREBUILD", "0", 1);
    setenv("DM_PSO_PREPARE", "0", 1);
    setenv("DM_NVN_ENC_THREAD", "0", 1);
    mtl_init();
    void *storage = NULL; posix_memalign(&storage, 16384, 16 << 20); memset(storage, 0, 16 << 20);
    pool_t pool = {.magic=MAGIC('P','O','O','L'), .mem=storage, .size=16 << 20,
        .flags=NVN_MEMORY_POOL_FLAGS_CPU_NO_ACCESS|NVN_MEMORY_POOL_FLAGS_GPU_CACHED|NVN_MEMORY_POOL_FLAGS_COMPRESSIBLE};
    __weak id<MTLTexture> retired;
    @autoreleasepool {
        mtl_sub_begin();
        tex_t t = image(&pool, 700001, 64, 64);
        mtl_tex_init(&t); mtl_tex_release(&t);
        id<MTLTexture> texture = tex_mtl(&t); retired = texture;
        CHECK(g_ghost.n == 1, "finalized descriptor can still be replayed on a live pool");
        pixels(texture, 0x32547698);
        CHECK(tex_get(t.serial) == texture, "live pool preserves the ghost and its rendered pixels");
        pool_t other_pool = pool;
        tex_t other = image(&other_pool, 700002, 64, 64);
        mtl_tex_init(&other); mtl_tex_release(&other);
        id<MTLTexture> other_texture = tex_mtl(&other);
        mtl_alias_drop_pool(&pool);
        CHECK(tex_get(t.serial) == nil, "pool finalization drops its finalized texture ghosts");
        CHECK(tex_get(other.serial) == other_texture, "another live pool retains its finalized texture");
        texture = nil;
        CHECK(retired != nil, "retirement keeps the texture alive until the submission completes");
        pixels(other_texture, 0x19283746);
        mtl_alias_drop_pool(&other_pool);
        mtl_sub_commit(); mtl_drain();
    }
    @autoreleasepool { defer_drain(); }
    CHECK(retired == nil, "retired pool texture is released after GPU completion");
    mtl_sub_begin();
    for (int i = 0; i < 24; i++) {
        id<MTLBuffer> buffer; size_t offset = arena_alloc(4u << 20, &buffer);
        memset((uint8_t *)buffer.contents + offset, i + 1, 4u << 20);
    }
    mtl_sub_commit(); mtl_drain();
    CHECK(g_arenas.count >= 24, "real scratch burst allocates more than the idle budget");
    mtl_sub_begin();
    CHECK(g_arenas.count >= 24, "recently used completed scratch is retained");
    mtl_sub_commit(); mtl_drain();
    for (int i = 0; i < ARENA_IDLE + 2; i++) { mtl_sub_begin(); mtl_sub_commit(); mtl_drain(); }
    size_t retained = 0; for (NvArena *arena in g_arenas) retained += arena->len;
    CHECK(retained <= (64u << 20), "idle small scratch buffers respect the combined 64 MiB budget");
    mtl_sub_begin();
    id<MTLBuffer> buffer; size_t offset = arena_alloc(4u << 20, &buffer);
    memset((uint8_t *)buffer.contents + offset, 0x73, 4u << 20);
    id<MTLBuffer> readback = [g_dev newBufferWithLength:4u << 20 options:MTLResourceStorageModeShared];
    res_add(readback);
    enc_b_copy_buffer(blit(), buffer, offset, readback, 0, 4u << 20);
    mtl_sub_commit(); mtl_drain();
    int valid = 1; for (size_t i = 0; i < readback.length; i++) if (((uint8_t *)readback.contents)[i] != 0x73) { valid = 0; break; }
    CHECK(valid && !mtl_last_commit_error(), "reused scratch survives an actual GPU copy after trimming");
    res_remove(readback); free(storage);
    printf("scene memory: %d checks, %d failed, idle scratch %zu MiB\n", checks, failed, retained >> 20);
    return failed != 0;
} }
