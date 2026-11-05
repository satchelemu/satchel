#undef main
int main(void) { @autoreleasepool {
    setenv("DM_PSO_ARCHIVE", "off", 1);
    NSString *dir = [@(getenv("TMPDIR")) stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
    EXPECT("fixture directory", 1, [NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil]);
    mtl_init();
    if (!fixtures(dir)) return 1;
    NvShader *vs = shader_load(@"warm-vs", 0, 0), *fs = shader_load(@"warm-fs", 1, 0);
    EXPECT("fixture shaders loaded", 1, vs && fs);
    if (!vs || !fs) return 1;
    // This synthetic vertex shader has no attributes. Its ready pulling entry
    // is identical; the full attribute-conversion fixtures remain in t_pull.
    vs->has_pull = 1; vs->pfn_state = 1; vs->pfn = vs->fn;
    for (int coverage = 0; coverage < 2; coverage++) {
        state_reset();
        pso_key key = {0}; key.vs = (__bridge void *)vs; key.fs = (__bridge void *)fs;
        key.cfmt[0] = MTLPixelFormatRGBA8Unorm; key.samples = 1; key.chmask = 15; key.a2c = coverage;
        MTLRenderPipelineDescriptor *exact = pso_desc(&key, vs, fs, nil, 0);
        NSError *error = nil; id<MTLRenderPipelineState> reference = pso_build(exact, &error);
        EXPECT("reference pipeline built", 1, reference != nil);
        satchel_fallback_start(&key, vs, fs, exact);
        EXPECT("one pending fallback", 1, satchel_fallbacks.count);
        satchel_fallback_start(&key, vs, fs, exact);
        EXPECT("duplicate fallback shares owner", 1, satchel_fallbacks.count);
        // Mutation after enqueue must not change the worker's descriptor.
        exact.fragmentFunction = nil; exact.alphaToCoverageEnabled = !coverage;
        uint64_t deadline = satchel_shader_now() + 10000000000;
        while (satchel_fallbacks.count && satchel_shader_now() < deadline) {
            satchel_fallback_collect(); usleep(1000);
        }
        EXPECT("fallback completion consumed", 0, satchel_fallbacks.count);
        id<MTLRenderPipelineState> fallback = hm_get_id(&g_pso_hy, &key);
        EXPECT("fallback published under exact state", 1, fallback != nil);
        if (fallback && reference) {
            uint8_t a[4], b[4]; pixels(reference, a); pixels(fallback, b);
            EXPECT("fallback preserves shader and coverage pixels", 0, memcmp(a, b, sizeof a));
        }
    }
    pso_key unsupported = {0}; unsupported.samples = 1;
    MTLRenderPipelineDescriptor *descriptor = [MTLRenderPipelineDescriptor new];
    vs->layer_out = 1; satchel_fallback_start(&unsupported, vs, fs, descriptor);
    EXPECT("layer-writing shader never gets pulling fallback", 0, satchel_fallbacks.count);
    vs->layer_out = 0; vs->pfn_state = 0;
    satchel_fallback_start(&unsupported, vs, fs, descriptor);
    EXPECT("missing pulling library never loads on submit", 0, satchel_fallbacks.count);
    EXPECT("missing pulling function remains unrequested", 0, vs->pfn_state);
    [NSFileManager.defaultManager removeItemAtPath:dir error:nil];
    printf("t_satchel_async_pixels: %d checks, %d failed\n", checks, failed);
    return failed != 0;
} }
