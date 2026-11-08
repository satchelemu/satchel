"""3D view resample (DM_PRESENT_UPSCALE view), applied by overlay.py after present_filter.py.

Some games render the 3D scene into a reduced target and upscale it into the full-size target with
AMD's FSR 1 EASU before they draw the HUD over that target (README.md, "3D view resample", names
the measured game and sizes). With `view`, such a draw is found from the NVN state alone and
replaced by ViewUpscale.metal, a Catmull-Rom resample of the same source into the same target;
the HUD draws that follow are unchanged and stay at full resolution. A draw qualifies only when all of these hold:

- three vertices, one instance, not indirect, no geometry, tessellation, transform feedback,
  rasterizer discard or merged-pass machinery, and it is the first draw of a new pass;
- one colour target (level 0, layer 0, no format view), no depth or stencil, no pending clear,
  blending off, all channels written, logic operation copy, a viewport and scissor covering it;
- the fragment shader binds exactly one texture (a single-sample 2D colour texture smaller than
  the target in both directions) plus samplers and uniform buffers, and no images or storage;
- one of its uniform buffers starts with FSR 1's first EASU constant (AMD's FsrEasuCon con0) for
  that whole source drawn to that whole target: in / out and 0.5 in / out - 0.5 in both directions.
  The game's later constants are not read (a game may pack other values after con0).

Anything else keeps the game's draw. No title is named. Unset (and every other value) leaves
every draw as it was: the only change on those paths is one cached flag check per draw.
"""
from pathlib import Path


def c_string(text):
    return '\n'.join('"' + line.replace('\\', '\\\\').replace('"', '\\"') + '\\n"' for line in text.splitlines())


def apply(root, path):
    text = path.read_text()
    shader = Path(__file__).with_name('ViewUpscale.metal').read_text()
    code = r'''// ---------- 3D view resample (DM_PRESENT_UPSCALE view; Satchel view_upscale.py) ----------
static const char g_view_msl[] =
''' + c_string(shader) + r''';
static int fmt_int_or_ds(MTLPixelFormat f);
static int g_view_mode = -1;
// Diagnostic Mac builds only (quality-diagnostics.py): 1 or 0 overrides the setting for a burst's frames, -1 follows it.
static int satchel_view_diag = -1;
static uint64_t g_view_n, g_view_kept;   // draws replaced; FSR 1 draws left to the game because a check refused them
static int view_mode(void) {
    if (satchel_view_diag >= 0) return satchel_view_diag;
    if (g_view_mode >= 0) return g_view_mode;
    const char *e = getenv("DM_PRESENT_UPSCALE"); int v = e && !strcmp(e, "view");
    if (v) MLOG("3D view resample (DM_PRESENT_UPSCALE view): on, a game's FSR 1 upscale of its 3D view is drawn as a Catmull-Rom resample; the HUD drawn after it is unchanged");
    return g_view_mode = v;
}
static id<MTLLibrary> g_view_lib; static _Atomic int g_view_ready;   // 0 not started, 1 compiling, 2 ready, 3 failed (the game's draw stays)
static void view_prepare(void) {
    int expected = 0; if (!atomic_compare_exchange_strong(&g_view_ready, &expected, 1)) return;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        uint64_t t0 = now_ns(); NSError *e = nil; id<MTLLibrary> lib = [g_dev newLibraryWithSource:@(g_view_msl) options:nil error:&e];
        if (!lib) { MLOG("[RED] 3D view resample: the shader did not compile (%s); the game's upscale stays", e.localizedDescription.UTF8String); atomic_store(&g_view_ready, 3); return; }
        g_view_lib = lib; atomic_store_explicit(&g_view_ready, 2, memory_order_release);
        MLOG("3D view resample: shader ready in %.1f ms", (now_ns() - t0) / 1e6);
    });
}
// FSR 1's first EASU constant (AMD's FsrEasuCon con0) for the whole w x h source drawn to the whole W x H target: in / out and
// 0.5 in / out - 0.5, the output pixel centre to input texel mapping. What follows con0 is not read: a game may pack other values there.
static int view_fsr_con(const float *c, double w, double h, double W, double H) {
    const double e[4] = {w / W, h / H, 0.5 * w / W - 0.5, 0.5 * h / H - 0.5};
    for (int i = 0; i < 4; i++) if (!isfinite(c[i]) || fabs(c[i] - e[i]) > 1e-4 * fmax(fabs(e[i]), 1e-3)) return 0;
    return 1;
}
static int view_binding_ok(int kind) {
    return kind == NVN_RB_CBUF || kind == NVN_RB_SAMP || kind == NVN_RB_SAMPT || kind == NVN_RB_SAMPI || kind == NVN_RB_TEX || kind == NVN_RB_C1 ||
           kind == NVN_RB_DRV0 || kind == NVN_RB_DRV2 || kind == NVN_RB_SUPPORT || kind == NVN_RB_BORDER;
}
// The source and target of an FSR 1 EASU draw from a reduced view into the full-size target (see view_upscale.py), or 0
#define V_NO(n) do { *why = (n); return 0; } while (0)
static int view_find(NvProg *pr, const p_draw *dw, const p_draw_idx *di, __strong id<MTLTexture> *src, __strong id<MTLTexture> *dst, int *why) {
    if ((dw ? dw->count : di->count) != 3 || (dw ? dw->instances : di->instances) != 1 || (dw ? dw->ind : di->ind) || (dw ? dw->prim : di->prim) != 4) V_NO(1);
    if (g_enc || g_geo_program || g_defer_replaying || g_defer.active || g_fz.active || g_mtk.on || g_geo_mesh.on || S.rast_discard || S.tf_on) V_NO(2);
    if (dw ? dw->mdi_cnt : di->mdi_cnt) V_NO(3);
    NvShader *fs = pr ? pr->sh[1] : nil;
    if (!fs || !pr->sh[0] || pr->sh[2] || pr->sh[3] || pr->sh[4] || (fs->has_alpha && atest_on()) || (fs->has_logic && lop_on())) V_NO(4);
    if (!R.has_rt || R.rt.n < 1 || !R.rt.color[0] || R.rt.depth || R.clear_d || R.clear_s) V_NO(5);
    for (int i = 0; i < 8; i++) if (R.clear_c[i] || (i > 0 && i < R.rt.n && R.rt.color[i])) V_NO(6);
    if (R.rt.has_cview[0] && ((R.rt.cview[0].set_levels && R.rt.cview[0].base_level) || (R.rt.cview[0].set_layers && R.rt.cview[0].min_layer) || R.rt.cview[0].set_format)) V_NO(7);
    if ((S.color.blend_en & 1) || (S.chmask.mask & 0xf) != 0xf || S.color.logic_op != 3) V_NO(8);
    id<MTLTexture> d = tex_mtl(R.rt.color[0]);
    if (!d || d.textureType != MTLTextureType2D || d.sampleCount != 1 || !(d.usage & MTLTextureUsageRenderTarget) || fmt_int_or_ds(d.pixelFormat) || mtl_fmt_block(d.pixelFormat)) V_NO(9);
    double W = d.width, H = d.height;
    if (S.has_vp && !(S.vp[0][0] == 0 && S.vp[0][1] == 0 && S.vp[0][2] == (float)W && S.vp[0][3] == (float)H)) V_NO(10);
    if (!scissor_full(R.rt.color[0], &R.rt.cview[0], R.rt.has_cview[0])) V_NO(11);
    int ntex = 0, treg = -1, ok = 1;
    for (int i = 0; i < fs->rf.nb; i++) { int k = fs->rf.b[i].kind; if (!view_binding_ok(k)) ok = 0; if (k == NVN_RB_TEX) { ntex++; treg = fs->rf.b[i].reg; } }
    if (ntex != 1) V_NO(12);
    int tid = (int)(tex_slot_h(S.tex[1], S.img[1], (unsigned)treg) & 0xfffff);
    os_unfair_lock_lock(&g_rmu);
    id<MTLTexture> s = tid > 0 && tid < g_ntreg && g_treg[tid].ok ? tex_by_id(tid) : nil;
    os_unfair_lock_unlock(&g_rmu);
    if (!s || s == d || s.textureType != MTLTextureType2D || s.sampleCount != 1 || fmt_int_or_ds(s.pixelFormat) || mtl_fmt_block(s.pixelFormat)) V_NO(13);
    double w = s.width, h = s.height;
    if (w >= W || h >= H) V_NO(14);
    int fsr = 0;
    for (int i = 0; i < fs->rf.nb && !fsr; i++) {
        if (fs->rf.b[i].kind != NVN_RB_CBUF || fs->rf.b[i].reg >= 16) continue;
        const p_bind_buf *u = &S.ubo[1][fs->rf.b[i].reg];
        if (!u->addr || u->size < 16 || pend_hits(u->addr, 16) || (g_nubd && ubd_hits(u->addr, u->addr + 16))) continue;
        float c[4]; memcpy(c, (const void *)(uintptr_t)u->addr, sizeof c);
        fsr = view_fsr_con(c, w, h, W, H);
    }
    if (!fsr) { static int n; if (n++ < 8) for (int i = 0; i < fs->rf.nb; i++) if (fs->rf.b[i].kind == NVN_RB_CBUF && fs->rf.b[i].reg < 16 && S.ubo[1][fs->rf.b[i].reg].addr) {
        const float *c = (const float *)(uintptr_t)S.ubo[1][fs->rf.b[i].reg].addr;
        MLOG("3D view resample: fragment %s uniform slot %d begins %g %g %g %g (source %gx%g, target %gx%g): not FSR 1's con0", fs->key, fs->rf.b[i].reg, c[0], c[1], c[2], c[3], w, h, W, H); }
        V_NO(15); }
    if (!ok) { g_view_kept++; static int once; if (!once++) { char k[160]; size_t o = 0; for (int i = 0; i < fs->rf.nb && o < sizeof k - 8; i++) o += snprintf(k + o, sizeof k - o, " %d", fs->rf.b[i].kind);
        MLOG("3D view resample: an FSR 1 draw (fragment %s) binds kinds%s, some of which the resample does not take; the game's draw stays", fs->key, k); } V_NO(16); }
    *src = s; *dst = d; return 1;
}
#undef V_NO
// Draws the resample of src into the whole of dst in a pass of its own, in place of the game's draw: 1 when drawn
static int view_encode(id<MTLTexture> s, id<MTLTexture> d) {
    static NSMutableDictionary<NSNumber *, id<MTLRenderPipelineState>> *pso; static id<MTLSamplerState> samp; static id<MTL4ArgumentTable> at; static int failed;
    if (failed) return 0;
    int ready = atomic_load_explicit(&g_view_ready, memory_order_acquire);
    if (ready != 2) { if (!ready) view_prepare(); return 0; }
    if (!pso) {
        NSError *e = nil; MTL4ArgumentTableDescriptor *ad = [MTL4ArgumentTableDescriptor new]; ad.maxTextureBindCount = 1; ad.maxSamplerStateBindCount = 1; ad.maxBufferBindCount = 1;
        at = rec_table([g_dev newArgumentTableWithDescriptor:ad error:&e]);
        MTLSamplerDescriptor *sd = [MTLSamplerDescriptor new]; sd.minFilter = sd.magFilter = MTLSamplerMinMagFilterLinear;
        sd.sAddressMode = sd.tAddressMode = MTLSamplerAddressModeClampToEdge; sd.supportArgumentBuffers = YES; samp = [g_dev newSamplerStateWithDescriptor:sd];
        if (!at || !samp) { failed = 1; MLOG("[RED] 3D view resample: cannot create its argument table or sampler (%s); the game's upscale stays", e.localizedDescription.UTF8String); return 0; }
        pso = [NSMutableDictionary dictionary];
    }
    NSNumber *pk = @(d.pixelFormat); id<MTLRenderPipelineState> ps = pso[pk];
    if (!ps) {
        MTLRenderPipelineDescriptor *pd = [MTLRenderPipelineDescriptor new];
        pd.vertexFunction = [g_view_lib newFunctionWithName:@"satchel_view_vs"]; pd.fragmentFunction = [g_view_lib newFunctionWithName:@"satchel_view_fs"];
        pd.colorAttachments[0].pixelFormat = d.pixelFormat; NSError *e = nil; ps = [g_dev newRenderPipelineStateWithDescriptor:pd error:&e];
        if (!ps) { { static int once; if (!once++) MLOG("[RED] 3D view resample: no pipeline for target format %lu (%s); the game's upscale stays", (unsigned long)d.pixelFormat, e.localizedDescription.UTF8String); } return 0; }
        pso[pk] = ps;
    }
    id<MTLTexture> sv = [s newTextureViewWithPixelFormat:s.pixelFormat textureType:MTLTextureType2D levels:NSMakeRange(0, 1) slices:NSMakeRange(0, 1)];
    id<MTLBuffer> pb; size_t po = arena_alloc(16, &pb);
    if (!sv || !pb) { static int once; if (!once++) MLOG("[RED] 3D view resample: cannot create the source view or its constants; the game's upscale stays"); return 0; }
    float *c = (float *)((uint8_t *)pb.contents + po); c[0] = (float)s.width; c[1] = (float)s.height; c[2] = 1.0f / c[0]; c[3] = 1.0f / c[1];
    rp_end(); if (g_blit) { enc_b_end(g_blit); g_blit = nil; }
    MTL4RenderPassDescriptor *rp = [MTL4RenderPassDescriptor new];
    rp.colorAttachments[0].texture = d; rp.colorAttachments[0].loadAction = MTLLoadActionDontCare; rp.colorAttachments[0].storeAction = MTLStoreActionStore;   // every pixel is drawn
    ser_gate(); id<MTL4RenderCommandEncoder> en = enc_r_begin(rp);
    [en setArgumentTable:at atStages:MTLRenderStageFragment];
    [at setTexture:sv.gpuResourceID atIndex:0]; [at setSamplerState:samp.gpuResourceID atIndex:0]; [at setAddress:pb.gpuAddress + po atIndex:0];
    [en setViewport:(MTLViewport){0, 0, d.width, d.height, 0, 1}]; [en setScissorRect:(MTLScissorRect){0, 0, d.width, d.height}];
    [en setRenderPipelineState:ps]; [en drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3]; enc_r_end(en);
    defer_obj_unref(sv);
    args_dirty_all(); g_pso_dirty = g_dss_dirty = 1; g_last_end_relaxed = 0;   // the next pass (the HUD) is ordered after this one in full
    if (!g_view_n++) MLOG("3D view resample: first FSR 1 draw replaced, %lux%lu format %lu into %lux%lu format %lu", (unsigned long)s.width, (unsigned long)s.height, (unsigned long)s.pixelFormat,
                          (unsigned long)d.width, (unsigned long)d.height, (unsigned long)d.pixelFormat);
    return 1;
}
'''
    anchor = 'static void draw_(const p_draw *dw, const p_draw_idx *di) {\n'
    if text.count(anchor) != 1:
        raise RuntimeError('View resample draw anchor is not unique')
    text = text.replace(anchor, code + anchor, 1)
    hook = '    if (!g_enc && tbdr_on() && !g_geo_program) hoist_try();'
    if text.count(hook) != 1:
        raise RuntimeError('View resample hook anchor is not unique')
    text = text.replace(hook, r'''    if (!defer_q_draw && view_mode()) {   // DM_PRESENT_UPSCALE view (view_upscale.py): a game's FSR 1 upscale of its 3D view, drawn as the resample instead
        id<MTLTexture> vs_ = nil, vd_ = nil;
        int why = 0; static int nwhy;
        if (!view_find(pr, dw, di, &vs_, &vd_, &why)) { if (why > 1 && nwhy < 24 && R.has_rt && R.rt.color[0] && R.rt.color[0]->d.width >= 1280) { nwhy++;
            MLOG("3D view resample: a three-vertex draw into %ux%u stays the game's (check %d, fragment %s)", R.rt.color[0]->d.width, R.rt.color[0]->d.height, why, pr->sh[1] ? pr->sh[1]->key : "-"); } }
        else { if (view_encode(vs_, vd_)) { PD("N %llu 3D view resample drew this FSR 1 upscale (%lux%lu into %lux%lu)", g_stat_pass + 1, (unsigned long)vs_.width, (unsigned long)vs_.height, (unsigned long)vd_.width, (unsigned long)vd_.height); return; } }
    }
''' + hook, 1)
    # The present treats view as off: neither the spatial scaler nor the edge smoothing runs.
    anchor = '!strcmp(e, "off") || !strcmp(e, "edge") || !strcmp(e, "0"))) v = 0;'
    if text.count(anchor) != 1:
        raise RuntimeError('View resample present-mode anchor changed')
    text = text.replace(anchor, '!strcmp(e, "off") || !strcmp(e, "edge") || !strcmp(e, "view") || !strcmp(e, "0"))) v = 0;', 1)
    path.write_text(text)
