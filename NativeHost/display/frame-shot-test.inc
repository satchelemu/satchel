#undef main
static void check_shot(id<MTLTexture> src, const int crop[4], int screen,
                       const char *name, int width, int height, int bars) {
    char path[1024]; snprintf(path, sizeof path, "%s/%s.png", getenv("SHOT_TEST_OUT"), name);
    shot(src, path, screen, crop);
    NSURL *url = [NSURL fileURLWithPath:@(path)];
    CGImageSourceRef source = CGImageSourceCreateWithURL((__bridge CFURLRef)url, NULL);
    CGImageRef image = source ? CGImageSourceCreateImageAtIndex(source, 0, NULL) : NULL;
    EXPECT("shot creates PNG", 1, image != NULL);
    if (image) {
        EXPECT("shot canvas width", width, (int)CGImageGetWidth(image));
        EXPECT("shot canvas height", height, (int)CGImageGetHeight(image));
        uint8_t *pixels = calloc(width * height, 4);
        CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB();
        CGContextRef ctx = CGBitmapContextCreate(pixels, width, height, 8, width * 4, cs, kCGImageAlphaNoneSkipLast);
        CGContextDrawImage(ctx, CGRectMake(0, 0, width, height), image);
        int bad = 0;
        for (int y = 0; y < height; y++) for (int x = 0; x < width; x++) {
            int black = bars == 1 && (y < height / 4 || y >= height * 3 / 4);
            if (bars == 2) black = x < 2 || x >= 10 || y < 1 || y >= 5;
            const uint8_t *p = pixels + (y * width + x) * 4;
            bad += p[0] != (black ? 0 : 255) || p[1] || p[2];
        }
        EXPECT("shot uses crop, scale and aspect fit", 0, bad);
        free(pixels); CGContextRelease(ctx); CGColorSpaceRelease(cs); CGImageRelease(image);
    }
    if (source) CFRelease(source);
}
int main(void) { @autoreleasepool {
    if (crop_main()) return 1;
    g_fx_mode = 0;
    MTLTextureDescriptor *d = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm width:12 height:6 mipmapped:NO];
    d.storageMode = MTLStorageModeShared; d.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> src = [g_dev newTextureWithDescriptor:d];
    uint8_t pixels[12 * 6 * 4] = {0};
    for (int y = 1; y < 5; y++) for (int x = 2; x < 10; x++) memcpy(pixels + (y * 12 + x) * 4, colors[0], 4);
    [src replaceRegion:MTLRegionMake2D(0, 0, 12, 6) mipmapLevel:0 withBytes:pixels bytesPerRow:12 * 4];
    res_add(src); res_commit();
    int crop[4] = {2, 1, 8, 4};
    shot_test_layer = [CAMetalLayer layer]; shot_test_layer.drawableSize = CGSizeMake(16, 8);
    const char *mode = getenv("DM_NVN_SHOT_OUT");
    if (mode && *mode) {
        int raw = !strcmp(mode, "raw");
        check_shot(src, crop, 1, raw ? "raw" : "explicit-size", raw ? 12 : 20, raw ? 6 : 10, raw ? 2 : 0);
        printf("frame screenshot override suite: %d checks, %d failed\n", cases, failed);
        return failed != 0;
    }
    check_shot(src, crop, 1, "cropped", 16, 8, 0);
    shot_test_layer.drawableSize = CGSizeMake(16, 16);
    check_shot(src, crop, 1, "aspect-fit", 16, 16, 1);
    atomic_store(&g_origin_ll, 1);
    check_shot(src, crop, 1, "lower-left", 16, 16, 1);
    atomic_store(&g_origin_ll, 0);
    shot_test_layer = nil;
    check_shot(src, crop, 1, "headless", 12, 6, 0);
    // Whole-frame games retain all of their content through the same pass.
    for (int i = 0; i < 12 * 6; i++) memcpy(pixels + i * 4, colors[0], 4);
    [src replaceRegion:MTLRegionMake2D(0, 0, 12, 6) mipmapLevel:0 withBytes:pixels bytesPerRow:12 * 4];
    check_shot(src, NULL, 1, "whole-frame", 12, 6, 0);
    check_shot(src, crop, 0, "texture-dump", 12, 6, 0);
    // Full-frame surfaces used by the other reviewed games, with no unused border.
    const int sizes[2][2] = {{1920, 1080}, {1600, 900}};
    const char *names[2] = {"totk-full-frame", "odyssey-full-frame"};
    shot_test_layer = [CAMetalLayer layer]; shot_test_layer.drawableSize = CGSizeMake(2560, 1440);
    for (int k = 0; k < 2; k++) {
        int w = sizes[k][0], h = sizes[k][1];
        d.width = w; d.height = h;
        id<MTLTexture> full = [g_dev newTextureWithDescriptor:d];
        uint8_t *rgba = malloc(w * h * 4);
        for (int i = 0; i < w * h; i++) memcpy(rgba + i * 4, colors[0], 4);
        [full replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0 withBytes:rgba bytesPerRow:w * 4];
        free(rgba); res_add(full); res_commit();
        int whole[4] = {0, 0, w, h};
        check_shot(full, whole, 1, names[k], 2560, 1440, 0);
        res_remove(full);
    }
    printf("frame screenshot suite: %d checks, %d failed\n", cases, failed);
    return failed != 0;
} }
