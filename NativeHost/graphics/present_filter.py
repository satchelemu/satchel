"""Edge smoothing in the present (DM_PRESENT_UPSCALE edge and edge-spatial), applied last by overlay.py.

The filter is PresentFilter.metal, embedded as source and compiled once on a background queue
when the setting is on; presents use the existing bilinear pass until it is ready. It smooths
the valid crop at the scan-out's own texel centres into a crop-sized texture that the existing
present then shows, bilinearly or, with edge-spatial, through the MetalFX spatial scaler under
that scaler's existing rules. Off (the default) and spatial leave every present unchanged.
"""
from pathlib import Path


def c_string(text):
    lines = []
    for line in text.splitlines():
        lines.append('"' + line.replace('\\', '\\\\').replace('"', '\\"') + '\\n"')
    return '\n'.join(lines)


def apply(root, path):
    text = path.read_text()
    shader = (Path(__file__).with_name('PresentFilter.metal')).read_text()
    changes = {
        # The smoothing pass's twelve constants follow the present's own eight floats.
        '        g_present_uv[i] = [g_dev newBufferWithLength:sizeof(float) * 8 options:MTLResourceStorageModeShared];':
        '        g_present_uv[i] = [g_dev newBufferWithLength:sizeof(float) * 20 options:MTLResourceStorageModeShared];',
        '    id<MTLBuffer> uvb = [g_dev newBufferWithLength:sizeof(float) * 8 options:MTLResourceStorageModeShared];':
        '    id<MTLBuffer> uvb = [g_dev newBufferWithLength:sizeof(float) * 20 options:MTLResourceStorageModeShared];',
        'typedef struct { id<MTL4FXSpatialScaler> sc; id<MTLTexture> in, out; uint64_t made_at; int made; NSUInteger refused[6]; } fx_state;':
        'typedef struct { id<MTL4FXSpatialScaler> sc; id<MTLTexture> in, out, edge; uint64_t made_at; int made; NSUInteger refused[6]; } fx_state;   // edge: the smoothed crop (present_filter.py)',
        '    if (e && (!strcmp(e, "spatial") || !strcmp(e, "metalfx") || !strcmp(e, "1"))) v = 1;\n    else if (e && (!strcmp(e, "off") || !strcmp(e, "0"))) v = 0;':
        '    if (e && (!strcmp(e, "spatial") || !strcmp(e, "edge-spatial") || !strcmp(e, "metalfx") || !strcmp(e, "1"))) v = 1;\n    else if (e && (!strcmp(e, "off") || !strcmp(e, "edge") || !strcmp(e, "0"))) v = 0;',
        '    id<MTLTexture> tex = present_raw_src(src), up = fx ? fx_encode(fx, cb, tex, r, vp, dst.pixelFormat, fx == &g_fx_present) : nil;':
        '''    id<MTLTexture> tex = present_raw_src(src), sm = edge_encode(fx, cb, tex, r, uvb, fx == &g_fx_present);
    if (sm) { tex = sm; r[0] = r[1] = 0; }   // the smoothed crop, at the origin of its own texture
    id<MTLTexture> up = fx ? fx_encode(fx, cb, tex, r, vp, dst.pixelFormat, fx == &g_fx_present) : nil;''',
        '// The present pass: src\'s valid rectangle drawn into dst': r'''// ---------- Edge smoothing in the present (DM_PRESENT_UPSCALE edge, edge-spatial; Satchel present_filter.py) ----------
static const char g_edge_msl[] =
''' + c_string(shader) + r''';
static int g_edge_mode = -1;
// Neighbour step of the smoothing in scan-out texels. Longer steps (PresentFilter.metal) smooth a reduced 3D view's
// stairs more but blur the full-resolution HUD drawn over it (NativeHost/graphics/README.md), so the present keeps 1.
static float g_edge_step = 1;
static int edge_mode(void) {
    if (g_edge_mode >= 0) return g_edge_mode;
    const char *e = getenv("DM_PRESENT_UPSCALE");
    int v = e && (!strcmp(e, "edge") || !strcmp(e, "edge-spatial"));
    if (v && !g_q4) { MLOG("present edge smoothing: needs the Metal 4 present; off"); v = 0; }
    MLOG("present edge smoothing (DM_PRESENT_UPSCALE): %s", v ? "on, the scan-out is smoothed along its edges before it is shown" : "off");
    return g_edge_mode = v;
}
static id<MTLRenderPipelineState> g_edge_pso[2];   // RGBA8Unorm, BGRA8Unorm targets; written once before g_edge_ready becomes 2
static _Atomic int g_edge_ready;   // 0 not started, 1 compiling, 2 ready, 3 failed (bilinear stays)
static void edge_build(void) {
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        uint64_t t0 = now_ns(); NSError *e = nil; id<MTLDevice> dev = g_dev;
        id<MTLLibrary> lib = [dev newLibraryWithSource:@(g_edge_msl) options:nil error:&e];
        id<MTLRenderPipelineState> p[2] = {nil, nil};
        for (int i = 0; lib && i < 2; i++) {
            MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
            pd.vertexFunction = [lib newFunctionWithName:@"satchel_filter_vs"]; pd.fragmentFunction = [lib newFunctionWithName:@"satchel_edge_fs"];
            pd.colorAttachments[0].pixelFormat = i ? MTLPixelFormatBGRA8Unorm : MTLPixelFormatRGBA8Unorm;
            p[i] = [dev newRenderPipelineStateWithDescriptor:pd error:&e];
        }
        if (!p[0] || !p[1]) { MLOG("[RED] present edge smoothing: the filter did not compile (%s); the present stays bilinear", e.localizedDescription.UTF8String); atomic_store(&g_edge_ready, 3); return; }
        g_edge_pso[0] = p[0]; g_edge_pso[1] = p[1];
        atomic_store_explicit(&g_edge_ready, 2, memory_order_release);
        MLOG("present edge smoothing: filter ready in %.1f ms", (now_ns() - t0) / 1e6);
    });
}
// Smooths the crop r of raw into fx->edge (crop-sized, at its origin); nil keeps the plain present. The pass's queue barrier orders it after the frame's
// work and after the previous present's read of fx->edge; the present pass's own barrier orders it before that pass. remake_wait: as fx_encode.
static id<MTLTexture> edge_encode(fx_state *fx, id<MTL4CommandBuffer> cb, id<MTLTexture> raw, const NSUInteger r[4], id<MTLBuffer> uvb, int remake_wait) {
    if (!fx || !edge_mode()) return nil;
    int ready = atomic_load_explicit(&g_edge_ready, memory_order_acquire);
    if (!ready) { int expected = 0; if (atomic_compare_exchange_strong(&g_edge_ready, &expected, 1)) edge_build(); return nil; }
    if (ready != 2) return nil;
    MTLPixelFormat pf = raw.pixelFormat; NSUInteger cw = r[2], ch = r[3];
    if ((pf != MTLPixelFormatRGBA8Unorm && pf != MTLPixelFormatBGRA8Unorm) || raw.textureType != MTLTextureType2D || raw.sampleCount != 1 || !cw || !ch) return nil;
    if (!fx->edge || fx->edge.width != cw || fx->edge.height != ch || fx->edge.pixelFormat != pf) {
        if (remake_wait && fx->edge) { pthread_mutex_lock(&g_pq_mu); while (g_pq_inflight) pthread_cond_wait(&g_pq_cv, &g_pq_mu); pthread_mutex_unlock(&g_pq_mu); }
        res_remove(fx->edge); fx->edge = nil;
        MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pf width:cw height:ch mipmapped:NO];
        td.storageMode = MTLStorageModePrivate; td.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        id<MTLTexture> t = [g_dev newTextureWithDescriptor:td];
        if (!t) { MLOG("[RED] present edge smoothing: cannot create its %lux%lu texture", (unsigned long)cw, (unsigned long)ch); return nil; }
        t.label = @"present edge smoothing"; res_add(t); res_commit(); fx->edge = t;
        MLOG("present edge smoothing: %lux%lu format %lu", (unsigned long)cw, (unsigned long)ch, (unsigned long)pf);
    }
    float *c = (float *)uvb.contents + 8; double w = raw.width, h = raw.height;
    c[0] = r[0] / w; c[1] = r[1] / h; c[2] = cw / w; c[3] = ch / h;
    c[4] = (r[0] + 0.5) / w; c[5] = (r[1] + 0.5) / h; c[6] = (r[0] + cw - 0.5) / w; c[7] = (r[1] + ch - 0.5) / h;
    c[8] = g_edge_step; c[9] = c[10] = c[11] = 0;
    MTL4RenderPassDescriptor *rp = [MTL4RenderPassDescriptor new];
    rp.colorAttachments[0].texture = fx->edge; rp.colorAttachments[0].loadAction = MTLLoadActionDontCare; rp.colorAttachments[0].storeAction = MTLStoreActionStore;
    id<MTL4RenderCommandEncoder> en = [cb renderCommandEncoderWithDescriptor:rp];
    if (!en) { static int once; if (!once++) MLOG("[RED] present edge smoothing: no render encoder; the present stays bilinear"); return nil; }
    [en barrierAfterQueueStages:MTLStageAll beforeStages:MTLStageAll visibilityOptions:resource_vis()];
    [en setArgumentTable:g_present_args atStages:MTLRenderStageFragment];
    [g_present_args setTexture:raw.gpuResourceID atIndex:0];
    [g_present_args setSamplerState:g_blit_samp.gpuResourceID atIndex:0];
    [g_present_args setAddress:uvb.gpuAddress + 8 * sizeof(float) atIndex:0];
    [en setViewport:(MTLViewport){0, 0, cw, ch, 0, 1}]; [en setRenderPipelineState:g_edge_pso[pf == MTLPixelFormatBGRA8Unorm]];
    [en drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
    m4_end(en);
    return fx->edge;
}
// The present pass: src's valid rectangle drawn into dst''',
    }
    for anchor, replacement in changes.items():
        if text.count(anchor) != 1:
            raise RuntimeError('Present filter anchor is not unique: ' + anchor[:60])
        text = text.replace(anchor, replacement, 1)
    # Offscreen presents own their smoothing texture as they own the scaler's.
    anchor = 'res_remove(fx.in); res_remove(fx.out);'
    if text.count(anchor) != 2:
        raise RuntimeError('Present filter offscreen anchor changed')
    text = text.replace(anchor, 'res_remove(fx.in); res_remove(fx.out); res_remove(fx.edge);')
    path.write_text(text)
