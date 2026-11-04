#undef main
int main(void) { @autoreleasepool {
    NSString *dir = [@(getenv("TMPDIR")) stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
    EXPECT("fixture directory", 1, [NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil]);
    setenv("DM_PSO_ARCHIVE", [dir stringByAppendingPathComponent:@"archive"].fileSystemRepresentation, 1);
    setenv("DM_PSO_MTL4", "1", 1);
    mtl_init();
    if (!fixtures(dir)) return 1;
    pso4_init();
    NvShader *vs = shader_load(@"warm-vs", 0, 0), *fs = shader_load(@"warm-fs", 1, 0);
    EXPECT("vertex loaded", 1, vs != nil); EXPECT("fragment loaded", 1, fs != nil);
    if (!vs || !fs) return 1;
    MTLRenderPipelineDescriptor *d = [MTLRenderPipelineDescriptor new];
    d.vertexFunction = vs->fn; d.fragmentFunction = fs->fn;
    d.vertexDescriptor = [MTLVertexDescriptor vertexDescriptor];
    d.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA8Unorm;
    NSError *error = nil;
    // The prebuild captures directly, without populating the pointer-keyed live base cache.
    id base = arch_capture(pso_desc4(d, 1), &error, 0);
    EXPECT("prebuild captured a base", 1, base != nil);
    EXPECT("unpublished base cannot serve a draw", 1, pso_build_ex(d, &error, 0) == nil);
    arch_flush_wait();
    uint64_t built = g_pb_built;
    id<MTLRenderPipelineState> ps = pso_build_ex(d, &error, 0);
    EXPECT("live archive supplies prebuilt base without restart", 1, ps != nil);
    EXPECT("draw compiled no base", built, g_pb_built);
    EXPECT("draw used published archive", 1, g_pb_archived);
    if (ps) {
        uint8_t result[4], expected[4] = {128, 64, 255, 128};
        pixels(ps, result);
        EXPECT("published pipeline draws the expected pixels", 1, memcmp(result, expected, 4) == 0);
    }
    for (NSString *stage in @[@"vs", @"fs"]) for (NSString *ext in @[@"json", @"metallib"]) {
        NSString *from = [dir stringByAppendingPathComponent:[NSString stringWithFormat:@"warm-%@.%@", stage, ext]];
        NSString *to = [dir stringByAppendingPathComponent:[NSString stringWithFormat:@"warm-%@_b.%@", stage, ext]];
        EXPECT("prebuild fixture copied", 1, [NSFileManager.defaultManager copyItemAtPath:from toPath:to error:nil]);
    }
    unsetenv("DM_PSO_PREBUILD_A2C");
    NSString *oldStamp = pre_stamp(@"synthetic pair", 0);
    setenv("DM_PSO_PREBUILD_A2C", "1", 1);
    EXPECT("coverage policy invalidates old preparation stamp", 0, [oldStamp isEqual:pre_stamp(@"synthetic pair", 0)]);
    pre_pair(@"warm-vs", @"warm-fs", 0);
    arch_flush_wait();
    d.alphaToCoverageEnabled = YES;
    built = g_pb_built;
    ps = pso_build_ex(d, &error, 0);
    EXPECT("enabled coverage base ready before its first draw", 1, ps != nil);
    EXPECT("enabled coverage draw compiled no base", built, g_pb_built);
    id<MTLRenderPipelineState> reference = pso_build_dev(d, &error);
    EXPECT("coverage reference pipeline built", 1, reference != nil);
    if (ps && reference) {
        uint8_t actual[4], expected[4]; pixels(ps, actual); pixels(reference, expected);
        EXPECT("coverage pixels match the exact synchronous pipeline", 1, memcmp(actual, expected, 4) == 0);
    }
    [NSFileManager.defaultManager removeItemAtPath:dir error:nil];
    printf("t_satchel_archive_live: %d checks, %d failed\n", checks, failed);
    return failed != 0;
} }
