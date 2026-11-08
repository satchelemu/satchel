// ---------- Tessellation: NVN patch draws through the compute route (Satchel tessellation_draw.py) ----------
// Metal 4 has no tessellator. A patch draw (NVN_DRAW_PRIMITIVE_PATCHES, 14) with hull (sh[3]) and domain (sh[4]) shaders runs as
// NativeShaders/RuntimeIntegration.md's tessellation handoff describes:
//   1. the vertex shader's capture entry (main0_gs, the geometry route's capture, vertex buffer 13) writes each vertex the draw names, keeping the words
//      the hull reads (its reflection's capture_words);
//   2. the hull kernel runs one threadgroup per patch of N = SetPatchSize control points and writes the route's canonical patch (528 bytes) and control
//      point (576 bytes) records;
//   3. the tessellator kernels (NativeShaders/Tessellator.swift's source, g_tess_msl) turn each patch's levels into domain points and an index list, with
//      the domain shader's reflected domain, spacing, winding and point mode;
//   4. the domain shader, a plain vertex function, is drawn in the program's place through draw(): the pixel stage, render pass, raster state and pipeline
//      key are the draw's own, as a geometry draw's raster replay is (g_geo_program). The domain's resources are NVN stage 4's.
// Each instance is a draw of its own (patch numbers restart per instance, as gl_PrimitiveID does). Unsupported shapes (no hull or no domain, a geometry
// stage after tessellation, transform feedback, indirect-count multi-draws, primitive restart inside the indices, an unset patch size) are refused with a
// red line, never drawn partly. Patches past the tessellator's capacity are dropped whole and counted (g_tess_stat, reported in the log).
static struct { int on, point_mode; uint64_t points, patches, cps, count, indices, index_bytes, totals; } g_tess_raster;
static id<MTLComputePipelineState> g_tess_k[4];   // count, scan, emit, note
static id<MTLBuffer> g_tess_stat; static uint32_t g_tess_stat_logged;
static uint64_t g_tess_draws, g_tess_patches, g_tess_refused;
static struct { uint64_t records, cps, ranges, points, indices, totals, count; } g_tess_job[256];   // the last patch draw's buffers per instance (submit thread only; the Mac check reads them)
// Capacity a draw (instance): 8 MiB of points and 8 MiB of indices at most, and no more than its patches can emit at level 64
// (triangles 3,169 points and 18,432 indices, quads 4,225 and 24,576, isolines 4,160 and 8,192; points only in point mode)
enum { TESS_MAX_VERTICES = 1 << 19, TESS_MAX_INDICES = 2 << 20, TESS_MAX_PATCHES = 1 << 20, TESS_MAX_INSTANCES = 256 };
static const uint32_t g_tess_worst[3][2] = {{4160, 8192}, {3169, 18432}, {4225, 24576}};   // isolines, triangles, quads
#define TESS_NO(...) do { g_tess_refused++; static int once_; if (!once_++) MLOG("[RED] tessellation draw skipped: " __VA_ARGS__); return; } while (0)
// The recorder's encoder stand-in (nvn_rec.h DmRecEnc) has no threadgroup memory call: the encode thread makes it on the encoder it holds, in order
@interface DmRecEnc (SatchelTessellation) - (void)setThreadgroupMemoryLength:(NSUInteger)length atIndex:(NSUInteger)index; @end
@implementation DmRecEnc (SatchelTessellation)
- (void)setThreadgroupMemoryLength:(NSUInteger)length atIndex:(NSUInteger)index {
    id block = [^{ [(id<MTL4ComputeCommandEncoder>)g_rx_enc setThreadgroupMemoryLength:length atIndex:index]; } copy];
    rec_t *r = rec_put(RO_Q_BLOCK, 1); r->a[0] = rec_obj(block);
}
@end
// Built for the session's device and kept beside its residency set: a later native session makes both again (session/SessionGraphicsState.inc
// resets g_dev and g_res), so the pipelines and the counter follow them instead of outliving them
static id g_tess_dev, g_tess_res;   // strong, so a new session's objects cannot take a retired one's address
static int tess_kernels(void) {
    static int failed;
    if (g_tess_k[3] && g_tess_dev == g_dev && g_tess_res == g_res) return 1;
    if (failed && g_tess_dev == g_dev) return 0;
    failed = 0; g_tess_dev = g_dev; g_tess_res = g_res; g_tess_stat_logged = 0;
    NSError *e = nil; id<MTLLibrary> lib = [g_dev newLibraryWithSource:@(g_tess_msl) options:nil error:&e];
    const char *names[4] = {"dm_tess_count", "dm_tess_scan", "dm_tess_emit", "dm_tess_note"};
    for (int i = 0; i < 4 && lib; i++) if (!(g_tess_k[i] = [g_dev newComputePipelineStateWithFunction:[lib newFunctionWithName:@(names[i])] error:&e])) break;
    if (!g_tess_k[3]) { failed = 1; for (int i = 0; i < 4; i++) g_tess_k[i] = nil; MLOG("[RED] tessellator kernels: %s", e.localizedDescription.UTF8String); return 0; }
    g_tess_stat = [g_dev newBufferWithLength:16 options:MTLResourceStorageModeShared]; memset(g_tess_stat.contents, 0, 16); res_add(g_tess_stat);
    return 1;
}
static void tess_draw(NvProg *pr, const p_draw *dw0, const p_draw_idx *di0) {
    NvShader *vs = pr ? pr->sh[0] : nil, *hs = pr ? pr->sh[3] : nil, *ds = pr ? pr->sh[4] : nil;
    if (!hs || !ds || hs->tess_role != 1 || ds->tess_role != 2 || hs->mesh_kind || ds->mesh_kind)
        TESS_NO("a patch draw needs translated hull and domain shaders (vertex %s hull %s domain %s)", vs ? vs->key : "-", hs ? hs->key : "-", ds ? ds->key : "-");
    if (!vs || !vs->geo_fn) TESS_NO("vertex shader %s has no capture entry", vs ? vs->key : "-");
    if (pr->sh[2]) TESS_NO("a geometry stage after tessellation is not drawn (geometry %s)", pr->sh[2]->key);
    if (S.tf_on) TESS_NO("transform feedback of tessellated output is not drawn");
    if ((dw0 && dw0->mdi_cnt) || (di0 && di0->mdi_cnt)) TESS_NO("indirect-count multi-draws of patches are not drawn");
    if (di0 && (di0->itype < 0 || di0->itype > 2)) TESS_NO("index type %d", di0->itype);
    int n = S.tess_patch;
    if (n < 1 || n > 32) TESS_NO("patch size %d (SetPatchSize unset or out of range)", n);
    uint32_t outv = hs->tess_out, words = hs->tess_words;
    if (outv < 1 || outv > 32 || !words || !hs->tess_slot) TESS_NO("hull %s has no output vertex count or capture words", hs->key);
    if (!tess_kernels()) return;
    image_prepare(pr, 1u << 0 | 1u << 3 | 1u << 4);
    if (defer_has_active()) defer_fallback();
    lc_land(); rp_flush(); rp_end(); ubd_land();
    p_draw dw = dw0 ? *dw0 : (p_draw){0}; p_draw_idx di = di0 ? *di0 : (p_draw_idx){0};
    int drained = 0;
    if (geo_conversion_hazard(vs) && !geo_sync_cpu(&drained)) TESS_NO("a converted vertex stream is still being written");
    uint64_t ind = dw0 ? dw.ind : di.ind;
    if (ind) {   // DrawArraysIndirect / DrawElementsIndirect: the arguments as the GPU leaves them, read after a wait when this submission may write them
        uint32_t a[5] = {0}; size_t bytes = dw0 ? 16 : 20;
        if (geo_cpu_hazard(ind, bytes) && !geo_sync_cpu(&drained)) TESS_NO("indirect arguments are still being written");
        if (!nvn_host_readable(ind, bytes)) TESS_NO("indirect arguments are not readable");
        memcpy(a, (const void *)(uintptr_t)ind, bytes);
        if (a[0] > INT32_MAX || a[1] > INT32_MAX) TESS_NO("indirect counts exceed the host range");
        if (dw0) { dw.count = (int32_t)a[0]; dw.instances = (int32_t)a[1]; dw.first = (int32_t)a[2]; dw.base_instance = (int32_t)a[3]; dw.ind = 0; }
        else { di.count = (int32_t)a[0]; di.instances = (int32_t)a[1]; di.ibuf += (uint64_t)a[2] << di.itype; di.base_vertex = (int32_t)a[3]; di.base_instance = (int32_t)a[4]; di.ind = 0; }
    }
    int count = dw0 ? dw.count : di.count, instances = dw0 ? dw.instances : di.instances;
    uint32_t first_instance = (uint32_t)(dw0 ? dw.base_instance : di.base_instance);
    if (count <= 0 || instances < 0) return;
    if (!instances) instances = 1;
    uint64_t patches = (uint64_t)count / (uint64_t)n;   // an incomplete last patch is not drawn
    if (!patches) return;
    if (patches > TESS_MAX_PATCHES || instances > TESS_MAX_INSTANCES) TESS_NO("%llu patches x %d instances exceed the route's bound", (unsigned long long)patches, instances);
    // Vertices the capture writes: a range for a plain draw, the sorted unique indices for an indexed one; each control point's capture record
    uint32_t lo = 0, hi = 0; size_t unique = 0; uint32_t *order = NULL;
    int isz = di0 ? 1 << di.itype : 0; uint64_t used = patches * (uint64_t)n;
    if (di0) {
        if (geo_index_wait(di.ibuf, used * isz) && !geo_sync_cpu(&drained)) TESS_NO("the index buffer is still being written");
        if (!nvn_host_readable(di.ibuf, used * isz)) TESS_NO("the index buffer is not readable");
        order = malloc(used * 4 * 2); if (!order) TESS_NO("index allocation failed");
        uint32_t restart = S.restart.index & (isz == 1 ? UINT8_MAX : isz == 2 ? UINT16_MAX : UINT32_MAX); lo = UINT32_MAX;
        for (uint64_t i = 0; i < used; i++) {
            uint32_t v = 0; memcpy(&v, (const uint8_t *)(uintptr_t)di.ibuf + i * isz, isz);
            if (S.restart.on && v == restart) { free(order); TESS_NO("primitive restart inside a patch list"); }
            order[i] = v; lo = MIN(lo, v); hi = MAX(hi, v);
        }
        if ((int64_t)lo + di.base_vertex < 0 || (int64_t)hi + di.base_vertex > UINT32_MAX) { free(order); TESS_NO("vertex numbers outside 32 bits"); }
    } else { lo = 0; hi = (uint32_t)(used - 1); if ((uint64_t)(uint32_t)dw.first + used > (uint64_t)UINT32_MAX + 1) TESS_NO("vertex numbers outside 32 bits"); }
    uint64_t span = (uint64_t)hi - lo + 1;
    int64_t first = di0 ? (int64_t)lo + di.base_vertex : dw.first;
    uint64_t cap_bytes = span * instances * words * 4;
    const uint32_t *worst = g_tess_worst[ds->tess_domain < 3 ? ds->tess_domain : 2];
    uint64_t maxv = MIN(patches * worst[0], (uint64_t)TESS_MAX_VERTICES), maxi = ds->tess_points ? 1 : MIN(patches * worst[1], (uint64_t)TESS_MAX_INDICES);
    uint64_t per = patches * (528 + 576ull * outv + 16) + maxv * 16 + maxi * 4 + 64 + 32 + 16 + 16 + 64 * 5;   // records, ranges, points, indices, totals, params, counts
    uint64_t total = cap_bytes + sizeof(nvn_geo_capture) + used * 4 + span * 4 + 16 * instances + per * instances + 256 * 16;
    if (cap_bytes > (256ull << 20) || total > (512ull << 20)) { free(order); TESS_NO("%llu MiB of scratch, above the route's bound", (unsigned long long)(total >> 20)); }
    if (!hs->tess_pso) { NSError *e = nil; hs->tess_pso = [g_dev newComputePipelineStateWithFunction:hs->fn error:&e];
        if (!hs->tess_pso) { free(order); TESS_NO("hull %s pipeline: %s", hs->key, e.localizedDescription.UTF8String); } }
    uint32_t threads = MAX((uint32_t)n, outv);
    if (hs->tess_pso.maxTotalThreadsPerThreadgroup < threads) { free(order); TESS_NO("hull %s cannot run %u threads", hs->key, threads); }
    id<MTLRenderPipelineState> capture = geo_capture_pso(vs); if (!capture) { free(order); TESS_NO("no capture pipeline for vertex %s", vs->key); }
    id<MTLBuffer> scratch; size_t off = arena_alloc((size_t)total, &scratch);
    uint8_t *cpu = (uint8_t *)scratch.contents + off; uint64_t gpu = scratch.gpuAddress + off; size_t at = 0;
    #define TESS_ALLOC(N, G, P) do { at = (at + 255) & ~(size_t)255; (G) = gpu + at; (P) = cpu + at; at += (size_t)(N); } while (0)
    uint64_t g_cap, g_info, g_map, g_ids; void *p_cap, *p_info, *p_map, *p_ids;
    TESS_ALLOC(MAX(cap_bytes, 16ull), g_cap, p_cap); TESS_ALLOC(sizeof(nvn_geo_capture), g_info, p_info);
    TESS_ALLOC(MAX(used * 4, 16ull), g_map, p_map); TESS_ALLOC(MAX(span * 4, 16ull), g_ids, p_ids);
    (void)p_cap;
    nvn_geo_capture ci = {g_cap, 0, {(uint32_t)first, (uint32_t)span, first_instance, (uint32_t)instances}, words, {0}};
    for (int k = 0; k < GEO_CAPTURE_WORDS; k++) ci.slot[k] = hs->tess_slot[k];
    memcpy(p_info, &ci, sizeof ci);
    size_t nids = 0;
    if (di0) {   // capture record of each control point: its index from the lowest; the capture draws each vertex once
        for (uint64_t i = 0; i < used; i++) ((uint32_t *)p_map)[i] = order[i] - lo;
        nids = geo_capture_unique(order, used, 0, 0, lo, span, (uint32_t *)p_ids, span);
        free(order); order = NULL;
        if (nids == SIZE_MAX || !nids) TESS_NO("capture indices exceed their range");
    }
    // 1. Capture, all instances at once: instance i's records start at i * span
    p_draw range = {.first = (int32_t)first, .count = (int32_t)span, .instances = instances, .base_instance = (int32_t)first_instance};
    vbuf_prepare(&vs->rf, &range, NULL);
    if (g_blit) { enc_b_end(g_blit); g_blit = nil; }
    g_draw_binst = first_instance;
    MTL4RenderPassDescriptor *rd = [MTL4RenderPassDescriptor new]; rd.renderTargetWidth = rd.renderTargetHeight = rd.defaultRasterSampleCount = 1;
    id<MTL4RenderCommandEncoder> re = enc_r_begin(rd);
    enc_r_pso(re, capture); bind_vbufs(re);
    vconv_bind(re, vs, 1, &range, NULL);
    args_dirty_all(); encode_args(re, vs, 0, 1); enc_r_vbuf(re, 13, g_info);
    if (di0) enc_r_draw_indexed(re, MTLPrimitiveTypePoint, nids, MTLIndexTypeUInt32, g_ids, span * 4, instances, di.base_vertex, first_instance);
    else enc_r_draw_instanced(re, MTLPrimitiveTypePoint, (NSUInteger)first, span, instances, first_instance);
    enc_r_end(re);
    // 2. and 3. Per instance: hull, then the tessellator over its patches
    typeof(g_tess_job[0]) *job = g_tess_job;
    NvShader *dsh = ds; uint32_t mode[4] = {dsh->tess_domain, dsh->tess_spacing, dsh->tess_cw, dsh->tess_points};
    id<MTL4ComputeCommandEncoder> ce = enc_c_begin(NULL);
    for (int i = 0; i < instances; i++) {
        void *p; uint64_t g_ti, g_par, g_tp;
        TESS_ALLOC(patches * 528, job[i].records, p); TESS_ALLOC(patches * 576 * outv, job[i].cps, p);
        TESS_ALLOC(patches * 16, job[i].ranges, p); TESS_ALLOC(maxv * 16, job[i].points, p); TESS_ALLOC(maxi * 4, job[i].indices, p);
        TESS_ALLOC(64, job[i].totals, p); memset(p, 0, 64);
        TESS_ALLOC(16, job[i].count, p); memcpy(p, &outv, 4);
        TESS_ALLOC(16, g_ti, p); { struct { uint64_t index; uint32_t first, pad; } ti = {di0 ? g_map : 0, (uint32_t)(i * span), 0}; memcpy(p, &ti, 16); }
        TESS_ALLOC(16, g_par, p); { uint32_t par[2] = {(uint32_t)n, (uint32_t)patches}; memcpy(p, par, 8); }
        TESS_ALLOC(32, g_tp, p); { uint32_t tp[8] = {mode[0], mode[1], mode[2], mode[3], (uint32_t)patches, (uint32_t)maxv, (uint32_t)maxi, 0}; memcpy(p, tp, 32); }
        if (at > total) MDIE("tessellation scratch size accounting");
        if (i) [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
        g_draw_binst = first_instance + (uint32_t)i;
        args_dirty_all(); encode_args(ce, hs, 3, 0); enc_c_pso(ce, hs->tess_pso);
        enc_c_args(ce, g_info, 13); enc_c_args(ce, g_ti, 14); enc_c_args(ce, job[i].records, 26); enc_c_args(ce, job[i].cps, 28); enc_c_args(ce, g_par, 29);
        if (hs->tess_in_bytes) [(id<MTL4ComputeCommandEncoder>)ce setThreadgroupMemoryLength:((size_t)hs->tess_in_bytes * n + 15) & ~(size_t)15 atIndex:0];
        enc_c_dispatch(ce, MTLSizeMake(patches, 1, 1), MTLSizeMake(threads, 1, 1));
        [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
        enc_c_args(ce, job[i].records, 0); enc_c_args(ce, g_tp, 1); enc_c_args(ce, job[i].ranges, 2); enc_c_args(ce, job[i].totals, 3);
        enc_c_args(ce, job[i].points, 4); enc_c_args(ce, job[i].indices, 5); enc_c_args(ce, g_tess_stat.gpuAddress, 6);
        enc_c_pso(ce, g_tess_k[0]); enc_c_dispatch(ce, MTLSizeMake((patches + 63) / 64, 1, 1), MTLSizeMake(64, 1, 1));
        [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
        enc_c_pso(ce, g_tess_k[1]); enc_c_dispatch(ce, MTLSizeMake(1, 1, 1), MTLSizeMake(MIN(1024u, (uint32_t)g_tess_k[1].maxTotalThreadsPerThreadgroup), 1, 1));
        [ce barrierAfterEncoderStages:MTLStageDispatch beforeEncoderStages:MTLStageDispatch visibilityOptions:MTL4VisibilityOptionDevice];
        enc_c_pso(ce, g_tess_k[2]); enc_c_dispatch(ce, MTLSizeMake((patches + 63) / 64, 1, 1), MTLSizeMake(64, 1, 1));
        enc_c_pso(ce, g_tess_k[3]); enc_c_dispatch(ce, MTLSizeMake(1, 1, 1), MTLSizeMake(1, 1, 1));
    }
    #undef TESS_ALLOC
    enc_c_end(ce);
    swrite_note(pr); g_wr_epoch++;
    args_dirty_all(); g_enc_pso = nil; g_enc_dss = nil; g_ed.ok = 0;
    // 4. The domain shader in the vertex stage's place, with the program's pixel stage, once per instance
    NvProg *raster = pr->tess_raster;
    if (!raster || raster->sh[0] != ds || raster->sh[1] != pr->sh[1]) { raster = [NvProg new]; raster->sh[0] = ds; raster->sh[1] = pr->sh[1]; pr->tess_raster = raster; }
    for (int i = 0; i < instances; i++) {
        g_tess_raster = (typeof(g_tess_raster)){1, dsh->tess_points, job[i].points, job[i].records, job[i].cps, job[i].count, job[i].indices, maxi * 4, job[i].totals};
        g_geo_program = raster; args_dirty_all();
        p_draw synthetic = {.prim = dsh->tess_points ? 0 : dsh->tess_domain == 0 ? 1 : 4, .count = 3, .instances = 1, .base_instance = (int32_t)(first_instance + (uint32_t)i)};
        draw(&synthetic, NULL);
        g_geo_program = nil; g_tess_raster.on = 0; args_dirty_all();
    }
    g_tess_draws++; g_tess_patches += patches * instances;
    uint32_t dropped = ((const uint32_t *)g_tess_stat.contents)[0];   // written by earlier submissions' dm_tess_note; a diagnostic, read without waiting
    if (dropped > g_tess_stat_logged) { g_tess_stat_logged = dropped; MLOG("[RED] tessellation: %u patches dropped so far at the tessellator's capacity (%u points, %u indices a draw)", dropped, (unsigned)TESS_MAX_VERTICES, (unsigned)TESS_MAX_INDICES); }
    if (g_tess_draws == 1) MLOG("tessellation: first patch draw, vertex %s hull %s domain %s fragment %s, %llu patches of %d control points, %d instances, domain %u spacing %u %s%s",
                                vs->key, hs->key, ds->key, pr->sh[1] ? pr->sh[1]->key : "-", (unsigned long long)patches, n, instances, mode[0], mode[1], mode[2] ? "cw" : "ccw", mode[3] ? " points" : "");
}
#undef TESS_NO
// The domain draw's encode (draw_, while g_tess_raster.on): its tessellator outputs at the domain function's buffers 16 to 19, through the totals' indirect arguments
static void tess_raster_encode(id<MTLRenderCommandEncoder> en_, MTLPrimitiveType mp) {
    id<MTL4RenderCommandEncoder> en = (id<MTL4RenderCommandEncoder>)en_;
    enc_r_vbuf(en, 16, g_tess_raster.points); enc_r_vbuf(en, 17, g_tess_raster.patches); enc_r_vbuf(en, 18, g_tess_raster.cps); enc_r_vbuf(en, 19, g_tess_raster.count);
    if (g_tess_raster.point_mode) enc_r_draw_indirect(en, MTLPrimitiveTypePoint, g_tess_raster.totals + 20);
    else enc_r_draw_indexed_indirect(en, mp, MTLIndexTypeUInt32, g_tess_raster.indices, (NSUInteger)g_tess_raster.index_bytes, g_tess_raster.totals);
}
