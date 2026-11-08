#!/usr/bin/env python3
"""Check profile persistence and the real Metal presentation path with synthetic pixels."""
from pathlib import Path
import os, shlex, shutil, subprocess, sys
ROOT = Path(__file__).resolve().parents[2]
OUT = Path(os.environ['SATCHEL_OUT']) / 'gpu-mac'
ENGINE = OUT / 'engine'
checks = OUT / 'checks'
checks.mkdir(exist_ok=True)
sys.path.insert(0, str(ROOT / 'NativeApp'))
from build_support import wait
wait()
shutil.copyfile(ROOT / 'NativeHost/PlayerProfiles.plist', checks / 'PlayerProfiles.plist')
for name, source in [('upscale-settings', 'graphics/upscale-settings-check.m'), ('display-settings', 'display/check.m')]:
    exe = checks / name
    subprocess.run(['nice', '-n', '10', 'xcrun', 'clang', '-fobjc-arc', '-fmodules', '-framework', 'Foundation',
                    '-framework', 'CoreGraphics', '-I', ROOT / 'NativeHost', ROOT / 'NativeHost/PlayerSettings.m',
                    # PlayerSettings.m tells the controller profiles the title (as controllers/check.py links it).
                    ROOT / 'App/ControllerProfiles.m', '-framework', 'GameController',
                    ROOT / 'NativeHost' / source, '-o', exe], check=True)
    subprocess.run([exe, 'setup'], check=True)
    if name == 'upscale-settings':
        subprocess.run([exe, 'override-save', '-SatchelUpscaler', 'spatial'], check=True)
        for arguments, expected in [([], 'off'), (['-SatchelUpscaler', 'spatial'], 'spatial'),
                                    (['-SatchelUpscaler', 'off'], 'off'), (['-SatchelUpscaler', 'temporal'], 'off'),
                                    (['-SatchelUpscaler', 'edge'], 'edge'), (['-SatchelUpscaler', 'edge-spatial'], 'edge-spatial'),
                                    (['-SatchelUpscaler', 'view'], 'view'),
                                    (['-SatchelUpscaler'], 'off'), ([], 'off')]:
            subprocess.run([exe, 'read', *arguments], env=dict(os.environ, EXPECT_UPSCALE=expected), check=True)
# Include all upstream crop, gamma, orientation, resize and readback assertions unchanged.
tests = ENGINE / 'dm/gfx/tests'
source = '#define main upstream_main\n' + (tests / 't_present_crop.m').read_text() + '\n#undef main\n'
source += r'''
int main(void) { @autoreleasepool {
    int result = upstream_main();
    if (result) return result;
    for (const char **p = (const char *[]){"1", "1.0", "nan", "0.4", "0.65junk", "", NULL}; *p; p++) {
        setenv("DM_RENDER_SCALE", *p, 1); setenv("DM_PRESENT_UPSCALE", "spatial", 1); g_fx_mode = -1;
        EXPECT("full scale or malformed scale keeps current filter", 0, fx_mode());
    }
    setenv("DM_RENDER_SCALE", "0.65", 1); setenv("DM_PRESENT_UPSCALE", "off", 1); g_fx_mode = -1;
    EXPECT("off at reduced scale keeps current filter", 0, fx_mode());
    setenv("DM_PRESENT_UPSCALE", "spatial", 1); g_fx_mode = -1;
    EXPECT("spatial uses device capability", [MTLFXSpatialScalerDescriptor supportsMetal4FX:g_dev], fx_mode());
    // Reject unsupported perceptual formats before a scaler factory or command encoding.
    MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float width:32 height:32 mipmapped:NO];
    id<MTLTexture> hdr = [g_dev newTextureWithDescriptor:td];
    fx_state fx = {0}; NSUInteger rect[4] = {0, 0, 32, 32};
    EXPECT("HDR stays with current filter", 1, fx_encode(&fx, nil, hdr, rect, (MTLViewport){0,0,64,64,0,1}, MTLPixelFormatBGRA8Unorm, 0) == nil);
    EXPECT("unsupported format allocates no scaler", 0, fx.made);
    // Edge smoothing (present_filter.py): real Metal presents of synthetic pixels through the offscreen present.
    setenv("DM_PRESENT_UPSCALE", "edge", 1); setenv("DM_RENDER_SCALE", "1.0", 1); g_fx_mode = -1; g_edge_mode = -1;
    EXPECT("edge setting turns smoothing on", 1, edge_mode());
    EXPECT("edge setting keeps the spatial scaler off", 0, fx_mode());
    atomic_store(&g_edge_ready, 1); edge_build();
    for (int i = 0; i < 1000 && atomic_load(&g_edge_ready) == 1; i++) usleep(10000);
    EXPECT("edge filter compiles", 2, atomic_load(&g_edge_ready));
    setenv("DM_PRESENT_UPSCALE", "edge-spatial", 1); setenv("DM_RENDER_SCALE", "0.65", 1); g_fx_mode = -1; g_edge_mode = -1;
    EXPECT("edge-spatial turns smoothing on", 1, edge_mode());
    EXPECT("edge-spatial keeps the spatial scaler's rules", [MTLFXSpatialScalerDescriptor supportsMetal4FX:g_dev], fx_mode());
    EXPECT("the smoothing step stays one texel at reduced scale (hundredths)", 100, (int)lroundf(g_edge_step * 100));
    g_fx_mode = 0;
    for (int pattern = 0; pattern < 2; pattern++) {
        // 0: one flat colour; 1: a dark field with a bright shallow staircase (one texel up every four).
        enum { W = 64, H = 64, OW = 96, OH = 96 };
        MTLTextureDescriptor *sd = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:W height:H mipmapped:NO];
        sd.storageMode = MTLStorageModeShared; sd.usage = MTLTextureUsageShaderRead;
        id<MTLTexture> img = [g_dev newTextureWithDescriptor:sd]; static uint8_t px[W * H * 4];
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
            uint8_t v = pattern ? (y >= 20 + x / 4 ? 230 : 20) : 120;
            uint8_t *q = px + 4 * (y * W + x); q[0] = v; q[1] = (uint8_t)(v / 2); q[2] = v; q[3] = 255;
        }
        [img replaceRegion:MTLRegionMake2D(0, 0, W, H) mipmapLevel:0 withBytes:px bytesPerRow:W * 4]; res_add(img); res_commit();
        static uint8_t got[2][OW * OH * 4];
        for (int mode = 0; mode < 2; mode++) {
            g_edge_mode = mode;
            MTLTextureDescriptor *od = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:OW height:OH mipmapped:NO];
            od.storageMode = MTLStorageModePrivate; od.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
            id<MTLTexture> out = [g_dev newTextureWithDescriptor:od];
            EXPECT(mode ? "smoothed present completes" : "plain present completes", PRESENT_STANDARD, present_offscreen_wait(img, NULL, out));
            id<MTLBuffer> buf = [g_dev newBufferWithLength:OW * OH * 4 options:MTLResourceStorageModeShared];
            id<MTLCommandBuffer> cb = [g_q commandBuffer]; id<MTLBlitCommandEncoder> b = [cb blitCommandEncoder];
            [b copyFromTexture:out sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(OW, OH, 1) toBuffer:buf destinationOffset:0 destinationBytesPerRow:OW * 4 destinationBytesPerImage:OW * OH * 4];
            [b endEncoding]; [cb commit]; [cb waitUntilCompleted]; memcpy(got[mode], buf.contents, sizeof got[mode]);
        }
        int changed = 0, far = 0, between[2] = {0, 0};
        for (int y = 0; y < OH; y++) for (int x = 0; x < OW; x++) {
            const uint8_t *a = got[0] + 4 * (y * OW + x), *e = got[1] + 4 * (y * OW + x);
            if (memcmp(a, e, 4)) { changed++; double sy = (y + 0.5) * H / OH, sx = (x + 0.5) * W / OW; if (fabs(sy - (20 + sx / 4)) > 4) far++; }
            if (pattern) for (int m = 0; m < 2; m++) { uint8_t v = got[m][4 * (y * OW + x)]; between[m] += v > 40 && v < 210; }
        }
        if (!pattern) EXPECT("flat colour is unchanged by smoothing (differing pixels)", 0, changed);
        else {
            EXPECT("the staircase is smoothed (some pixels change)", 1, changed > 0);
            EXPECT("pixels away from the edge are unchanged", 0, far);
            EXPECT("smoothing adds intermediate values along the stair", 1, between[1] > between[0]);
        }
        res_remove(img);
    }
    g_edge_mode = -1; unsetenv("DM_PRESENT_UPSCALE");
    // 3D view resample (view_upscale.py): the setting, the FSR 1 constant check and the resample shader with real Metal.
    setenv("DM_PRESENT_UPSCALE", "view", 1); g_fx_mode = -1; g_edge_mode = -1; g_view_mode = -1;
    EXPECT("view turns the 3D view resample on", 1, view_mode());
    EXPECT("view keeps the spatial scaler off", 0, fx_mode());
    EXPECT("view keeps the edge smoothing off", 0, edge_mode());
    setenv("DM_PRESENT_UPSCALE", "edge", 1); g_view_mode = -1; EXPECT("edge leaves the view resample off", 0, view_mode());
    setenv("DM_PRESENT_UPSCALE", "spatial", 1); g_view_mode = -1; EXPECT("spatial leaves the view resample off", 0, view_mode());
    unsetenv("DM_PRESENT_UPSCALE"); g_view_mode = -1; g_fx_mode = -1; g_edge_mode = -1; EXPECT("unset leaves the view resample off", 0, view_mode());
    {   const float c[4] = {1040.f / 1920, 584.f / 1080, 0.5f * 1040 / 1920 - 0.5f, 0.5f * 584 / 1080 - 0.5f};
        EXPECT("FSR 1 con0 for the whole source and target matches", 1, view_fsr_con(c, 1040, 584, 1920, 1080));
        EXPECT("another source size does not match", 0, view_fsr_con(c, 1600, 896, 1920, 1080));
        EXPECT("another target size does not match", 0, view_fsr_con(c, 1040, 584, 1280, 720));
        float v[4]; memcpy(v, c, sizeof v); v[0] *= 0.9f; v[2] = 0.5f * v[0] - 0.5f;   // a viewport narrower than the source texture
        EXPECT("a source viewport smaller than its texture does not match", 0, view_fsr_con(v, 1040, 584, 1920, 1080));
        memcpy(v, c, sizeof v); v[3] += 0.01f; EXPECT("an offset centre mapping does not match", 0, view_fsr_con(v, 1040, 584, 1920, 1080));
        memset(v, 0, sizeof v); EXPECT("zero constants do not match", 0, view_fsr_con(v, 1040, 584, 1920, 1080));
        v[0] = NAN; EXPECT("non-finite constants do not match", 0, view_fsr_con(v, 1040, 584, 1920, 1080)); }
    view_prepare();
    for (int i = 0; i < 1000 && atomic_load(&g_view_ready) == 1; i++) usleep(10000);
    EXPECT("the view resample shader compiles", 2, atomic_load(&g_view_ready));
    if (atomic_load(&g_view_ready) == 2) {
        enum { SW = 48, SH = 27, DW = 100, DH = 56 };
        MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
        pd.vertexFunction = [g_view_lib newFunctionWithName:@"satchel_view_vs"]; pd.fragmentFunction = [g_view_lib newFunctionWithName:@"satchel_view_fs"];
        pd.colorAttachments[0].pixelFormat = MTLPixelFormatRGBA32Float; NSError *e = nil;
        id<MTLRenderPipelineState> ps = [g_dev newRenderPipelineStateWithDescriptor:pd error:&e];
        MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new]; sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterLinear; sd.sAddressMode = sd.tAddressMode = MTLSamplerAddressModeClampToEdge;
        id<MTLSamplerState> ss = [g_dev newSamplerStateWithDescriptor:sd];
        EXPECT("the view resample pipeline is made", 1, ps != nil && ss != nil);
        for (int pattern = 0; ps && pattern < 2; pattern++) {
            static float src[SH][SW][4], dst[DH][DW][4];
            for (int y = 0; y < SH; y++) for (int x = 0; x < SW; x++) for (int k = 0; k < 4; k++)
                src[y][x][k] = pattern ? (float)(0.5 + 0.4 * sin(0.37 * x + 0.21 * y + k)) : 0.375f;
            MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float width:SW height:SH mipmapped:NO];
            td.usage = MTLTextureUsageShaderRead; td.storageMode = MTLStorageModeShared;
            id<MTLTexture> st = [g_dev newTextureWithDescriptor:td]; [st replaceRegion:MTLRegionMake2D(0, 0, SW, SH) mipmapLevel:0 withBytes:src bytesPerRow:SW * 16];
            td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float width:DW height:DH mipmapped:NO];
            td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead; td.storageMode = MTLStorageModeShared;
            id<MTLTexture> dt = [g_dev newTextureWithDescriptor:td];
            MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor new]; rp.colorAttachments[0].texture = dt; rp.colorAttachments[0].loadAction = MTLLoadActionDontCare; rp.colorAttachments[0].storeAction = MTLStoreActionStore;
            id<MTLCommandBuffer> cb = [g_q commandBuffer]; id<MTLRenderCommandEncoder> en = [cb renderCommandEncoderWithDescriptor:rp];
            float cst[4] = {SW, SH, 1.0f / SW, 1.0f / SH};
            [en setRenderPipelineState:ps]; [en setFragmentTexture:st atIndex:0]; [en setFragmentSamplerState:ss atIndex:0]; [en setFragmentBytes:cst length:sizeof cst atIndex:0];
            [en drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3]; [en endEncoding]; [cb commit]; [cb waitUntilCompleted];
            [dt getBytes:dst bytesPerRow:DW * 16 fromRegion:MTLRegionMake2D(0, 0, DW, DH) mipmapLevel:0];
            double worst = 0; int flat_diff = 0;
            for (int y = 0; y < DH; y++) for (int x = 0; x < DW; x++) {
                double qx = (x + 0.5) * SW / DW - 0.5, qy = (y + 0.5) * SH / DH - 0.5;
                if (pattern && (qx < 2 || qy < 2 || qx > SW - 3 || qy > SH - 3)) continue;   // the interior: the edge clamp of the bilinear taps is not a texel clamp
                for (int k = 0; k < 4; k++) {
                    double ref = 0, fx = qx - floor(qx), fy = qy - floor(qy);
                    for (int j = -1; j <= 2; j++) for (int i = -1; i <= 2; i++) {
                        double tx = fabs(i - fx), ty = fabs(j - fy);
                        #define CR(t) ((t) < 1 ? 1.5 * (t) * (t) * (t) - 2.5 * (t) * (t) + 1 : (t) < 2 ? -0.5 * (t) * (t) * (t) + 2.5 * (t) * (t) - 4 * (t) + 2 : 0)
                        int sx = (int)floor(qx) + i, sy = (int)floor(qy) + j; sx = sx < 0 ? 0 : sx >= SW ? SW - 1 : sx; sy = sy < 0 ? 0 : sy >= SH ? SH - 1 : sy;
                        ref += CR(tx) * CR(ty) * src[sy][sx][k];
                        #undef CR
                    }
                    if (k < 3 && ref < 0) ref = 0; if (k == 3) ref = ref < 0 ? 0 : ref > 1 ? 1 : ref;
                    double d = fabs(dst[y][x][k] - ref); if (d > worst) worst = d;
                    if (!pattern && fabs(dst[y][x][k] - src[0][0][k]) > 1e-6) flat_diff++;
                }
            }
            if (!pattern) EXPECT("a flat source stays flat within 1e-6 (differing values)", 0, flat_diff);
            else EXPECT("the resample matches Catmull-Rom within 1/256 in the interior", 1, worst < 1.0 / 256);
            if (pattern) printf("view resample: largest difference from Catmull-Rom %.6f\n", worst);
        }
    }
    printf("upscale presentation: %d checks, %d failed\n", cases, failed);
    return failed != 0;
} }
'''
p = tests / 'satchel_upscale_check.m'
p.write_text(source)
dry = subprocess.check_output(['make', '-n', '-B', 'GAME=0100F2C0115B6000', 't_present_crop'], cwd=tests, text=True)
args = next(shlex.split(line) for line in dry.splitlines() if ' -o t_present_crop ' in line)
args[args.index('t_present_crop.m')] = str(p)
args[args.index('-o') + 1] = str(checks / 'upscale-present')
subprocess.run(['nice', '-n', '10', *args], cwd=tests, check=True)
subprocess.run([checks / 'upscale-present'], env=dict(os.environ, MTL_DEBUG_LAYER='1'), check=True)
