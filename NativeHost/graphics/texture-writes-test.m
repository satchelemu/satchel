// The texture-write watch (texture-writes.patch in hle_nvn.c, texture-writes-metal.patch in nvn_mtl.m) on real Metal textures.
// check.py places this after including the overlay's nvn_mtl.m and a patched copy of hle_nvn.c, as t_cpu_tex does.
uint64_t g_t0_ns;
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t *g_mod; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int checks, failed;
#define CHECK(c, label) do { checks++; if (!(c)) { failed++; fprintf(stderr, "FAIL: %s\n", label); } } while (0)

static pool_t g_pool; static uint8_t *g_mem; static const size_t g_len = 8u << 20;
void crash_install(void);
static void texture(NVNtexture *tex, int format, uint64_t offset, int side) {
    NVNtextureBuilder tb; h_nvnTextureBuilderSetDefaults(&tb);
    h_nvnTextureBuilderSetWidth(&tb, side); h_nvnTextureBuilderSetHeight(&tb, side); h_nvnTextureBuilderSetFormat(&tb, format);
    h_nvnTextureBuilderSetStorage(&tb, (NVNmemoryPool *)&g_pool, offset);
    if (!h_nvnTextureInitialize(tex, &tb)) { fprintf(stderr, "FAIL: texture init\n"); exit(1); }
}
// First use: the replay resolves the texture on the submit thread and makes its host texture.
static void use(const NVNtexture *tex) {
    mtl_reg_tex(1, (const tex_t *)tex, NULL);
    mtl_sub_begin(); (void)tex_by_id(1); mtl_wait(mtl_sub_commit(), 5000000000ull);
    mtl_reg_tex(1, NULL, NULL);
}
static uint8_t *storage(const NVNtexture *tex) { return ((const tex_t *)tex)->mem; }
static void poll_all(void) { for (int i = 0; i < 4; i++) tw_poll(); }

int main(void) { @autoreleasepool {
    setenv("DM_TEX_WRITE_WATCH", "1", 1);
    crash_install();   // the page-fault hooks of dirty-page tracking
    mtl_init();
    g_mem = mmap(NULL, g_len, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
    if (g_mem == MAP_FAILED) return 1;
    pool_builder_t pb = {.mem = g_mem, .size = g_len, .flags = NVN_MEMORY_POOL_FLAGS_CPU_CACHED | NVN_MEMORY_POOL_FLAGS_GPU_CACHED};
    CHECK(h_nvnMemoryPoolInitialize((NVNmemoryPool *)&g_pool, (const NVNmemoryPoolBuilder *)&pb), "CPU-cached pool");
    CHECK(!dm_prof()->tex_flush_upload && !dm_prof()->tex_dirty_upload, "fixture profile has the upload switches off");
    // The GPU also writes this pool, so the host takes an uncompressed texture's first content only from flushed ranges.
    mtl_pool_gpu_written(&g_pool);
    // a: block compressed, empty at first use (a GPU copy), then written without a flush: counted after first use.
    // b: the same, then flushed: counted. c: block compressed, written before its first use: the host takes it from its storage, never counted.
    // d: used and never written. e: uncompressed 2D (render-target use): its later write is not watched, as nvn_ft_poll skips it.
    // f: used, then finalized before the write: gone from the list. j: uncompressed, written before its first use without a flush:
    // its GPU copy misses that content, logged but not counted. i: the same with a flush: the one-time fill takes it, not counted.
    NVNtexture a, b, c, d, e, f, i, j;
    // 1024 x 1024 DXT1 is 512 KiB, 256 x 256 RGBA8 256 KiB: whole pages each, at least 512 KiB apart.
    texture(&a, NVN_FORMAT_RGBA_DXT1, 0, 1024); texture(&b, NVN_FORMAT_RGBA_DXT1, 1u << 20, 1024); texture(&c, NVN_FORMAT_RGBA_DXT1, 2u << 20, 1024);
    texture(&d, NVN_FORMAT_RGBA_DXT1, 3u << 20, 1024); texture(&e, NVN_FORMAT_RGBA8, 4u << 20, 256); texture(&f, NVN_FORMAT_RGBA_DXT1, 6u << 20, 1024);
    texture(&j, NVN_FORMAT_RGBA8, 5u << 20, 256); texture(&i, NVN_FORMAT_RGBA8, (5u << 20) + (512u << 10), 256);
    CHECK(atomic_load(&g_stw_mode) == 1 && g_stw_n == 8 && g_stw_watched == 8, "eight sampled textures watched");
    memset(storage(&c), 0x5a, 4096);
    memset(storage(&j), 0x5a, 4096);
    memset(storage(&i), 0x5a, 4096); h_nvnMemoryPoolFlushMappedRange((const NVNmemoryPool *)&g_pool, (5u << 20) + (512u << 10), 4096);
    use(&a); use(&b); use(&c); use(&d); use(&e); use(&f); use(&j); use(&i);
    poll_all();
    CHECK(mtl_tex_gpu_copy(((tex_t *)&a)->serial) == 1 && mtl_tex_gpu_copy(((tex_t *)&c)->serial) == 4 && mtl_tex_gpu_copy(((tex_t *)&e)->serial) == 2 &&
          mtl_tex_gpu_copy(((tex_t *)&j)->serial) == 2, "replay holdings read back");
    CHECK(g_stw_used == 7 && g_stw_n == 7, "used textures leave state 0; the counted one leaves the list, the one taken from storage stays");
    CHECK(g_stw_before == 1 && g_stw_reused + g_stw_flushed + g_stw_unflushed == 0, "only the unflushed write before first use is noted, outside the count");
    h_nvnTextureFinalize(&f);
    memset(storage(&a) + 16384, 0x33, 4096);
    memset(storage(&c) + 16384, 0x33, 4096);
    memset(storage(&e) + 16384, 0x33, 4096);
    memset(storage(&f) + 16384, 0x33, 4096);
    h_nvnMemoryPoolFlushMappedRange((const NVNmemoryPool *)&g_pool, (1u << 20) + 64, 256);
    CHECK(g_stw_flushed == 1, "a flush over a used texture counts it");
    // The summary line, as the app reads it (NativeBootFeedback.LogSignals)
    char path[1024]; snprintf(path, sizeof path, "%s/tw-log-%d.txt", getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp", getpid());
    fflush(stderr); int saved = dup(2); FILE *log = freopen(path, "w", stderr);
    g_stw_log_ns = 0;   // the line is rate limited to one every two seconds; the first poll above logged just now
    poll_all();
    fflush(stderr); dup2(saved, 2); close(saved); (void)log;
    CHECK(g_stw_unflushed == 1 && g_stw_flushed == 1 && g_stw_before == 1, "the unflushed write after first use counts once, nothing else");
    FILE *in = fopen(path, "r"); char text[4096] = {0}; if (in) { fread(text, 1, sizeof text - 1, in); fclose(in); } unlink(path);
    CHECK(strstr(text, "[nvn] texture writes the GPU copy misses: 2 textures (flushed over shared storage 0, flushed after 1, written after 1; written before first use 1, not counted); 7 of 8 watched textures used, 0 taken from storage then GPU copies") != NULL, "summary line");
    memset(storage(&a) + 32768, 0x44, 4096); poll_all();
    CHECK(g_stw_unflushed == 1, "a counted texture is not counted again");
    CHECK(!strstr(text, "0x") && !strstr(text, "ser="), "no address or serial in the summary");
    // A managed session's reset, then a profile with the upload switches on: no watch.
    satchel_tw_reset();
    CHECK(atomic_load(&g_stw_mode) == -1 && !g_stw_n && !g_stw_before && !g_stw_flushed && !g_stw_unflushed && !g_stw_watched, "session reset clears the watch");
    ((struct dm_profile *)dm_prof())->tex_flush_upload = 1;
    NVNtexture g; texture(&g, NVN_FORMAT_RGBA_DXT1, 7u << 20, 256);
    CHECK(atomic_load(&g_stw_mode) == 0 && !g_stw_n, "the watch is off with the switches on");
    ((struct dm_profile *)dm_prof())->tex_flush_upload = 0;
    satchel_tw_reset(); unsetenv("DM_TEX_WRITE_WATCH");
    NVNtexture h; texture(&h, NVN_FORMAT_RGBA_DXT1, 7u << 20, 256);
    CHECK(atomic_load(&g_stw_mode) == 0 && !g_stw_n, "the watch is off without DM_TEX_WRITE_WATCH");
    printf("t_satchel_texture_writes: %d checks, %d failed\n", checks, failed);
    return failed != 0;
} }
