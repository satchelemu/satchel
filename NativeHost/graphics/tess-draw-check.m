// Synthetic NVN patch draws through the host (tessellation_draw.py) on the Mac GPU, against a CPU reference draw.
// tess-draw-check.py builds this with the overlaid nvn_mtl.m and hands it a directory of translated public-synthetic shaders
// (vertex, hull and domain, NativeShaders/public_corpus.py's generators) and cases.json. Each case is one command stream:
// render targets, raster state, vertex streams, SetPatchSize (OP_BIND_STATE kind 6), the program and a patch draw, replayed
// by mtl_exec as a game's submission is. Checks:
//   pixels: four float targets against a CPU reference draw of the same patches (coverage with back faces culled, the control
//     point value the indices name, the per-patch value, the levels and the patch number), and a plain triangle draw of the
//     patch corners through the same stream (the reference's conventions, and an unchanged non-patch draw);
//   tessellation: the host's tessellator output per patch against the specification's vertex count, the triangulation of a
//     disk (2V - B - 2 triangles), the domain's area, the winding, and the outer edge positions (exact k / n for equal
//     spacing; for fractional spacing n + 1 mirrored points with n - 2 full segments of 1 / f).
// Only counts and failures are printed.
#include "../nvn/nvn_mtl.m"
void *front_metal_layer(void) { return NULL; }
module_t *module_of(const void *p) { (void)p; return NULL; }
module_t *g_mod; int g_nmod;
void svc_thread_dump(void) {}
uint64_t now_ns(void) { return bp_now(); }
static int checks, failed;
static char failure[512];
#define EXPECT(cond, ...) do { checks++; if (!(cond)) { if (!failed++) snprintf(failure, sizeof failure, __VA_ARGS__); if (failed <= 12) { fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } } while (0)
enum { W = 96, H = 96 };
static id<MTLLibrary> library(NSString *source) {
    NSError *e = nil; MTLCompileOptions *o = [MTLCompileOptions new]; o.languageVersion = MTLLanguageVersion3_2; o.preserveInvariance = YES;
    id<MTLLibrary> l = [g_dev newLibraryWithSource:source options:o error:&e];
    if (!l) { fprintf(stderr, "%s\n", e.localizedDescription.UTF8String); abort(); } return l;
}
static NvShader *hand(id<MTLLibrary> l, NSString *name, int stage) {
    NvShader *s = [NvShader new]; s->stage = stage; s->fn = [l newFunctionWithName:name]; s->gs_loc = s->lay_loc = -1;
    static uint64_t identity = 0x7100000000000000ull; snprintf(s->key, sizeof s->key, "%016llx_b", (unsigned long long)++identity);
    return s;
}
static double edge_param(int domain, int e, const float *p) {   // parameter along outer edge e from the corner its numbering starts at
    if (domain == 1) return e == 0 ? p[2] : e == 1 ? p[0] : p[1];
    return e == 0 ? 1 - p[1] : e == 1 ? p[0] : e == 2 ? p[1] : 1 - p[0];
}
static int on_edge(int domain, int e, const float *p) {
    if (domain == 1) return p[e == 0 ? 0 : e == 1 ? 1 : 2] == 0;
    return e == 0 ? p[0] == 0 : e == 1 ? p[1] == 0 : e == 2 ? p[0] == 1 : p[1] == 1;
}
typedef struct { int n; double f; } level;
static level level_of(double x, int spacing) {   // the specification's rounding (OpenGL 4.6, 11.2.2)
    if (isnan(x)) x = 0;
    level l;
    if (spacing == 1) { l.f = fmin(fmax(x, 2), 64); l.n = (int)ceil(l.f / 2) * 2; }
    else if (spacing == 2) { l.f = fmin(fmax(x, 1), 63); l.n = (int)ceil((l.f - 1) / 2) * 2 + 1; }
    else { l.n = (int)ceil(fmin(fmax(x, 1), 64)); l.f = l.n; }
    if (l.f > l.n) l.f = l.n;
    return l;
}
static int cases_run, pixels_checked, patches_checked, triangles_checked, edges_checked, near_edges;
static void run_case(NSDictionary *c, NvShader *vs, NvShader *fs, NvShader *ctl_fs, int number) {
    NSString *name = c[@"name"];
    const char *label = name.UTF8String;
    int domain = [c[@"domainCode"] intValue], spacing = [c[@"spacingCode"] intValue], cw = [c[@"cw"] boolValue], n = [c[@"patchSize"] intValue];
    int patches = [c[@"patches"] intValue], indexed = [c[@"indexed"] boolValue], instances = [c[@"instances"] intValue], pixels = [c[@"pixels"] boolValue];
    int base_vertex = [c[@"baseVertex"] intValue], first_vertex = [c[@"firstVertex"] intValue], unset = [c[@"noPatchSize"] boolValue];
    double outer[4], inner[2]; for (int i = 0; i < 4; i++) outer[i] = [c[@"outer"][i] doubleValue]; for (int i = 0; i < 2; i++) inner[i] = [c[@"inner"][i] doubleValue];
    NvShader *hs = shader_get([c[@"hull"] stringByAppendingString:@"_b"], 3), *ds = shader_get([c[@"domain"] stringByAppendingString:@"_b"], 4);
    EXPECT(hs && ds && hs->tess_role == 1 && ds->tess_role == 2, "%s: hull and domain load with their tessellation facts", label);
    if (!hs || !ds) return;
    EXPECT(ds->tess_domain == domain && ds->tess_spacing == spacing && ds->tess_cw == cw && hs->tess_out == (uint32_t)n, "%s: reflected mode", label);
    NvProg *pr = [NvProg new]; pr->sh[0] = vs; pr->sh[1] = fs; pr->sh[3] = hs; pr->sh[4] = ds;
    NvProg *plain = [NvProg new]; plain->sh[0] = vs; plain->sh[1] = ctl_fs;
    static NSMutableArray *keep; if (!keep) keep = [NSMutableArray array]; [keep addObject:pr]; [keep addObject:plain];
    static prog_t cp[64], cq[64]; cp[number].serial = 9100 + number; cq[number].serial = 9300 + number; serial_put(cp[number].serial, pr); serial_put(cq[number].serial, plain);
    // Targets: four RGBA32F for the patch draw, one for the plain draw of the patch corners
    tex_t t[5]; memset(t, 0, sizeof t);
    for (int i = 0; i < 5; i++) { t[i].magic = MAGIC('T','E','X',0); t[i].serial = 9500 + number * 8 + i; t[i].off = (int64_t)(i + 1) << 20; t[i].d.width = W; t[i].d.height = H;
        t[i].d.depth = t[i].d.samples = t[i].d.levels = 1; t[i].d.target = NVN_TEXTURE_TARGET_2D; t[i].d.format = NVN_FORMAT_RGBA32F; }
    // Guest memory: commands, then positions, values and indices
    size_t size = 1 << 20; uint8_t *mem; if (posix_memalign((void **)&mem, getpagesize(), size)) abort(); memset(mem, 0, size); mtl_pool_add(mem, size);
    float *pos = (float *)(mem + (512 << 10)), *val = (float *)(mem + (640 << 10)); uint32_t *ix = (uint32_t *)(mem + (768 << 10)), *cix = (uint32_t *)(mem + (896 << 10));
    NSArray *corners = c[@"positions"];   // per patch, its control points in clip space
    int nv = patches * n, total = first_vertex + nv + (indexed ? base_vertex + 8 : 0) + 16;
    for (int v = 0; v < total; v++) { val[4 * v] = v + 0.5f; val[4 * v + 1] = 2.0f * v; val[4 * v + 2] = 3; val[4 * v + 3] = 4; pos[4 * v + 3] = 1; }
    // Indexed: control point k of patch p is vertex 7 + 2 (p n + (n - 1 - k)) + base vertex (descending and spread); plain: first_vertex + p n + k
    for (int p = 0; p < patches; p++) for (int k = 0; k < n; k++) {
        int vtx = indexed ? 7 + 2 * (p * n + (n - 1 - k)) + base_vertex : first_vertex + p * n + k;
        if (indexed) ix[p * n + k] = (uint32_t)(vtx - base_vertex);   // control point k is corner k, at a vertex number that falls as k rises
        for (int a = 0; a < 2; a++) pos[4 * vtx + a] = [corners[p][k][a] floatValue];
        pos[4 * vtx + 2] = 0; pos[4 * vtx + 3] = 1;
    }
    // Control-point vertex numbers in patch order
    int vnum[64][32];
    for (int p = 0; p < patches; p++) for (int k = 0; k < n; k++) vnum[p][k] = indexed ? (int)ix[p * n + k] + base_vertex : first_vertex + p * n + k;
    // The plain draw: triangles of the patch corners in the patch's own vertex order (quads as two), through 32-bit indices
    int ncix = 0;
    for (int p = 0; p < patches; p++) {
        if (n == 3) { cix[ncix++] = vnum[p][0]; cix[ncix++] = vnum[p][1]; cix[ncix++] = vnum[p][2]; }
        else { int q[6] = {0, 1, 2, 0, 2, 3}; for (int k = 0; k < 6; k++) cix[ncix++] = vnum[p][q[k]]; }
    }
    if (cw) for (int i = 0; i < ncix; i += 3) { uint32_t s = cix[i + 1]; cix[i + 1] = cix[i + 2]; cix[i + 2] = s; }   // clockwise emission: the corner order reversed
    // Recorded stream
    uint8_t *commands = mem; size_t used = 0;
    #define EMIT(OP, X) do { size_t z = (8 + sizeof(X) + 7) & ~7ull; op_hdr h = {OP, (uint32_t)z}; memcpy(commands + used, &h, 8); memcpy(commands + used + 8, &(X), sizeof(X)); used += z; } while (0)
    p_set_rt rt = {.n = 4}; for (int i = 0; i < 4; i++) rt.color[i] = &t[i]; EMIT(OP_SET_RT, rt);
    p_viewports vp = {.n = 1}; vp.v[0][2] = W; vp.v[0][3] = H; vp.v[0][5] = 1; EMIT(OP_VIEWPORTS, vp); p_scissor sc = {0, 0, W, H}; EMIT(OP_SCISSOR, sc);
    for (int i = 0; i < 4; i++) { p_clear_color cl = {.index = i, .mask = 15, .rgba = {0, 0, 0, 0}}; EMIT(OP_CLEAR_COLOR, cl); }
    p_bind_state bs = {.kind = 4}; poly_st poly = {.cull = NVN_FACE_BACK, .front = NVN_FRONT_FACE_CCW, .mode = NVN_POLYGON_MODE_FILL}; memcpy(bs.bytes, &poly, sizeof poly); EMIT(OP_BIND_STATE, bs);
    p_bind_program bp = {&cp[number], 0x1f}; EMIT(OP_BIND_PROGRAM, bp);
    p_bind_vattr va = {.n = 2}; va_st a0 = {.fmt = NVN_FORMAT_RGBA32F, .offset = 0, .stream = 0}, a1 = {.fmt = NVN_FORMAT_RGBA32F, .offset = 0, .stream = 1};
    memcpy(&va.st[0], &a0, 4); memcpy(&va.st[1], &a1, 4); EMIT(OP_BIND_VATTR, va);
    p_bind_vstream vst = {.n = 2}; vst.st[0].stride = 16; vst.st[1].stride = 16; vst.st[1].divisor = instances > 1 ? 1 : 0; EMIT(OP_BIND_VSTREAM, vst);
    p_bind_vbuf vb = {.n = 2}; vb.r[0] = (NVNbufferRange){(uint64_t)pos, 128 << 10}; vb.r[1] = (NVNbufferRange){(uint64_t)val, 128 << 10}; EMIT(OP_BIND_VBUF, vb);
    p_bind_state ps = {.kind = 6}; int32_t patch_size = n; memcpy(ps.bytes, &patch_size, 4); if (!unset) EMIT(OP_BIND_STATE, ps);
    if (indexed) { p_draw_idx di = {.prim = NVN_DRAW_PRIMITIVE_PATCHES, .itype = 2, .count = nv, .base_vertex = base_vertex, .instances = instances, .ibuf = (uint64_t)ix}; EMIT(OP_DRAW_IDX, di); }
    else { p_draw d = {.prim = NVN_DRAW_PRIMITIVE_PATCHES, .first = first_vertex, .count = nv, .instances = instances}; EMIT(OP_DRAW, d); }
    // The plain draw into the fifth target
    p_set_rt rt2 = {.n = 1}; rt2.color[0] = &t[4]; EMIT(OP_SET_RT, rt2);
    p_clear_color cl2 = {.index = 0, .mask = 15, .rgba = {0, 0, 0, 0}}; EMIT(OP_CLEAR_COLOR, cl2);
    p_bind_program bq = {&cq[number], 0x1f}; EMIT(OP_BIND_PROGRAM, bq);
    p_draw_idx dc = {.prim = 4, .itype = 2, .count = ncix, .instances = 1, .ibuf = (uint64_t)cix}; EMIT(OP_DRAW_IDX, dc);
    op_hdr end = {OP_END, 8}; memcpy(commands + used, &end, 8);
    #undef EMIT
    state_reset(); memset(&R, 0, sizeof R);
    uint64_t draws0 = g_tess_draws, refused0 = g_tess_refused;
    mtl_sub_begin(); uint64_t h = (uint64_t)commands; mtl_sub_index(&h, 1); mtl_exec(commands);
    if (unset) EXPECT(g_tess_draws == draws0 && g_tess_refused == refused0 + 1, "%s: a patch draw without a patch size is refused", label);
    else EXPECT(g_tess_draws == draws0 + 1 && g_tess_refused == refused0, "%s: the patch draw went through the route", label);
    // The tessellator's outputs of each instance, readable after completion
    typedef struct { const uint32_t *ranges, *indices, *totals; const float *points; } tess_out;
    tess_out out[8] = {0};
    for (int i = 0; i < instances && i < 8; i++) {
        uint64_t want[4] = {g_tess_job[i].ranges, g_tess_job[i].indices, g_tess_job[i].totals, g_tess_job[i].points};
        const void *got[4] = {0};
        for (int k = 0; k < 4; k++) for (NvArena *a in g_sub_arenas) if (want[k] >= a->gpu && want[k] + 4 <= a->gpu + a->buf.length) got[k] = (const uint8_t *)a->buf.contents + (want[k] - a->gpu);
        out[i] = (tess_out){got[0], got[1], got[2], got[3]};
    }
    id<MTLBuffer> rb = [g_dev newBufferWithLength:5 * W * H * 16 options:MTLResourceStorageModeShared]; res_add(rb);
    for (int i = 0; i < 5; i++)
        [blit() copyFromTexture:tex_mtl(&t[i]) sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0) sourceSize:MTLSizeMake(W, H, 1) toBuffer:rb destinationOffset:(NSUInteger)i * W * H * 16 destinationBytesPerRow:W * 16 destinationBytesPerImage:W * H * 16];
    EXPECT(mtl_wait(mtl_sub_commit(), 5000000000ull) == 1, "%s: submission completed", label);
    EXPECT(mtl_last_commit_error() == 0, "%s: GPU error", label);
    const float *img = rb.contents;
    // Tessellation of every patch of every instance
    for (int i = 0; i < instances && !unset; i++) {
        tess_out o = out[i];
        EXPECT(o.ranges && o.indices && o.totals && o.points, "%s: tessellator buffers found", label); if (!o.ranges || !o.totals) continue;
        EXPECT(o.totals[9] == 0, "%s: no patch dropped", label);
        uint32_t sv = 0, si = 0;
        for (int p = 0; p < patches; p++) {
            uint32_t fv = o.ranges[4 * p], nvx = o.ranges[4 * p + 1], fi = o.ranges[4 * p + 2], ni = o.ranges[4 * p + 3]; sv += nvx; si += ni;
            const float *pt = o.points + 4 * fv;
            // The specification's vertex set size and boundary
            int expected = 0, boundary = 0;
            if (domain == 0) {
                int lines = level_of(outer[0], 0).n; level d = level_of(outer[1], spacing);
                EXPECT(nvx == (uint32_t)(lines * (d.n + 1)) && ni == (uint32_t)(2 * lines * d.n), "%s: isoline counts", label);
                patches_checked++; continue;
            }
            level e[4], in0 = level_of(inner[0], spacing), in1 = level_of(inner[1], spacing);
            int ne = domain == 1 ? 3 : 4, ones = 1;
            for (int k = 0; k < ne; k++) { e[k] = level_of(outer[k], spacing); ones &= e[k].n == 1; }
            if (domain == 1) {
                if (ones && in0.n == 1) { expected = 3; boundary = 3; }
                else { if (in0.n == 1) in0.n = spacing == 2 ? 3 : 2; expected = boundary = e[0].n + e[1].n + e[2].n;
                    for (int r = 1; r <= in0.n / 2; r++) { int m = in0.n - 2 * r; expected += m ? 3 * m : 1; } }
            } else {
                if (ones && in0.n == 1 && in1.n == 1) { expected = 4; boundary = 4; }
                else { if (in0.n == 1) in0.n = spacing == 2 ? 3 : 2; if (in1.n == 1) in1.n = spacing == 2 ? 3 : 2;
                    expected = boundary = e[0].n + e[1].n + e[2].n + e[3].n;
                    for (int r = 1; r <= MIN(in0.n, in1.n) / 2; r++) { int m0 = in0.n - 2 * r, m1 = in1.n - 2 * r; expected += !m0 && !m1 ? 1 : !m1 ? m0 + 1 : !m0 ? m1 + 1 : 2 * (m0 + m1); } }
            }
            EXPECT(nvx == (uint32_t)expected, "%s: patch %d has %u domain points, the specification's set has %d", label, p, nvx, expected);
            EXPECT(ni == (uint32_t)(3 * (2 * expected - boundary - 2)), "%s: patch %d has %u indices, a triangulated disk of %d points (%d on the boundary) has %d", label, p, ni, expected, boundary, 3 * (2 * expected - boundary - 2));
            for (uint32_t k = 0; k < nvx; k++) {
                const float *q = pt + 4 * k; uint32_t id; memcpy(&id, q + 3, 4);
                EXPECT(id == (uint32_t)p && q[0] >= 0 && q[0] <= 1 && q[1] >= 0 && q[1] <= 1 && q[2] >= 0 && q[2] <= 1, "%s: point outside its patch's domain", label);
                if (domain == 1) EXPECT(fabs(q[0] + q[1] + q[2] - 1) < 2e-6, "%s: barycentric sum", label);
            }
            double area = 0;
            for (uint32_t k = 0; k + 2 < ni; k += 3) {
                const uint32_t *x = o.indices + fi + k; int ok = x[0] >= fv && x[0] < fv + nvx && x[1] >= fv && x[1] < fv + nvx && x[2] >= fv && x[2] < fv + nvx;
                EXPECT(ok, "%s: index outside its patch", label); if (!ok) break;
                const float *a = o.points + 4 * x[0], *b = o.points + 4 * x[1], *d = o.points + 4 * x[2];
                double s = ((double)b[0] - a[0]) * ((double)d[1] - a[1]) - ((double)d[0] - a[0]) * ((double)b[1] - a[1]); s = cw ? -s / 2 : s / 2;
                EXPECT(s >= -1e-9, "%s: a triangle against the winding", label); area += s; triangles_checked++;
            }
            EXPECT(fabs(area - (domain == 1 ? 0.5 : 1.0)) < 1e-5, "%s: patch %d covers %g of the domain", label, p, area);
            // Outer edges
            int single = domain == 1 ? ones && level_of(inner[0], spacing).n == 1 : ones && level_of(inner[0], spacing).n == 1 && level_of(inner[1], spacing).n == 1;
            for (int k = 0; k < ne; k++) {
                double ts[80]; int m = 0;
                for (uint32_t q = 0; q < nvx && m < 80; q++) if (on_edge(domain, k, pt + 4 * q)) ts[m++] = edge_param(domain, k, pt + 4 * q);
                for (int x = 1; x < m; x++) for (int y = x; y > 0 && ts[y - 1] > ts[y]; y--) { double s = ts[y]; ts[y] = ts[y - 1]; ts[y - 1] = s; }
                int en = single ? 1 : e[k].n;
                EXPECT(m == en + 1, "%s: outer edge %d has %d points, expected %d", label, k, m, en + 1);
                if (m != en + 1) continue;
                if (single || e[k].f >= e[k].n) for (int q = 0; q <= en; q++) EXPECT(fabs(ts[q] - (double)q / en) <= 0.5 / 65536 + 1e-7, "%s: outer edge %d point %d at %.7f, not %d/%d", label, k, q, ts[q], q, en);
                else {
                    int full = 0; for (int q = 0; q < en; q++) full += fabs((ts[q + 1] - ts[q]) - 1 / e[k].f) <= 2.0 / 65536;
                    EXPECT(full >= en - 2, "%s: outer edge %d has %d segments of 1/f, expected %d", label, k, full, en - 2);
                    for (int q = 0; q <= en; q++) EXPECT(ts[q] + ts[en - q] == 1, "%s: outer edge %d not mirrored", label, k);
                }
                edges_checked++;
            }
            patches_checked++;
        }
        EXPECT(o.totals[5] == sv && o.totals[0] == si, "%s: totals cover the patches", label);
    }
    // Pixels against the CPU reference draw
    if (pixels) {
        const float *ctl = img + 4 * W * H * 4;
        int covered = 0, near_edge = 0;
        for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) {
            double px = (x + 0.5) / W * 2 - 1, py = 1 - (y + 0.5) / H * 2;   // Metal's window: the top row is clip y = +1
            int hit = -1; double margin = 1e9;
            for (int p = 0; p < patches; p++) {
                double P[4][2]; for (int k = 0; k < n; k++) for (int a = 0; a < 2; a++) P[k][a] = [corners[p][k][a] doubleValue];
                // Domain map: triangles u P0 + v P1 + w P2, quads P0 + u (P1 - P0) + v (P3 - P0); solve for (u, v)
                double o0[2], ax[2], bx[2];
                if (n == 3) { o0[0] = P[2][0]; o0[1] = P[2][1]; for (int a = 0; a < 2; a++) { ax[a] = P[0][a] - P[2][a]; bx[a] = P[1][a] - P[2][a]; } }
                else { o0[0] = P[0][0]; o0[1] = P[0][1]; for (int a = 0; a < 2; a++) { ax[a] = P[1][a] - P[0][a]; bx[a] = P[3][a] - P[0][a]; } }
                double det = ax[0] * bx[1] - ax[1] * bx[0], u = ((px - o0[0]) * bx[1] - (py - o0[1]) * bx[0]) / det, v = (ax[0] * (py - o0[1]) - ax[1] * (px - o0[0])) / det;
                double la = hypot(ax[0], ax[1]), lb = hypot(bx[0], bx[1]), sx = W / 2.0;   // distances to the edges, in pixels
                double dists[4] = {u * fabs(det) / lb * sx, v * fabs(det) / la * sx, n == 3 ? (1 - u - v) * fabs(det) / hypot(ax[0] - bx[0], ax[1] - bx[1]) * sx : (1 - u) * fabs(det) / lb * sx, (1 - v) * fabs(det) / la * sx};
                double dmin = 1e9; for (int k = 0; k < (n == 3 ? 3 : 4); k++) dmin = fmin(dmin, dists[k]);
                if (fabs(dmin) < margin) margin = fabs(dmin);
                int front = (det > 0) == !cw;   // the tessellator's winding through the domain map; NVN front face counter-clockwise, back faces culled
                if (dmin > 0 && front) hit = p;
            }
            if (margin < 0.01) { near_edge++; continue; }
            const float *c0 = img + 4 * (y * W + x), *c1 = c0 + 4 * W * H, *c2 = c1 + 4 * W * H, *c3 = c2 + 4 * W * H, *cc = ctl + 4 * (y * W + x);
            EXPECT((c1[3] == 1) == (hit >= 0 && !unset), "%s: pixel %d,%d coverage %d, the reference draw %d", label, x, y, c1[3] == 1, hit >= 0 && !unset);
            EXPECT((cc[3] == 1) == (hit >= 0), "%s: pixel %d,%d plain corner draw coverage %d, the reference draw %d", label, x, y, cc[3] == 1, hit >= 0);
            pixels_checked++;
            if (hit < 0 || c1[3] != 1) continue;
            covered++;
            int v1 = instances > 1 ? instances - 1 : vnum[hit][1];   // per instance values: the last instance's, drawn last
            EXPECT(c0[0] == v1 + 0.5f && c0[1] == 2.0f * v1 && c0[2] == 3 && c0[3] == 4, "%s: pixel %d,%d control point value %g %g, expected vertex %d", label, x, y, c0[0], c0[1], v1);
            EXPECT(c1[0] == 0.25f && c1[1] == 0.5f && c1[2] == 0.75f, "%s: per-patch value", label);
            EXPECT(c2[0] == (float)outer[0] && c2[1] == (float)outer[1] && c2[2] == (float)outer[2] && c2[3] == (domain == 1 ? 0 : (float)outer[3]), "%s: outer levels %g %g %g %g", label, c2[0], c2[1], c2[2], c2[3]);
            uint32_t id; memcpy(&id, c3 + 2, 4);
            EXPECT(c3[0] == (float)inner[0] && c3[1] == (domain == 1 ? 0 : (float)inner[1]) && id == (uint32_t)hit, "%s: inner levels %g %g, patch %u of %d", label, c3[0], c3[1], id, hit);
        }
        near_edges += near_edge;
        EXPECT(near_edge * 50 < W * H, "%s: %d pixel centres within 0.01 px of a patch edge", label, near_edge);
        if ([c[@"expectEmpty"] boolValue] || unset) EXPECT(covered == 0, "%s: a back-facing patch drew %d pixels", label, covered);
        else EXPECT(covered > 300, "%s: only %d pixels drawn", label, covered);
    }
    res_remove(rb);
    for (int i = 0; i < 5; i++) mtl_tex_release(&t[i]);
    mtl_pool_del(mem); mtl_drain(); free(mem);
    cases_run++;
}
int main(int argc, char **argv) { @autoreleasepool {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc < 2) { fprintf(stderr, "usage: tess-draw-check <fixtures>\n"); return 2; }
    mtl_init();
    g_msl_dir = @(argv[1]); g_shcache = [NSMutableDictionary dictionary];
    NSDictionary *spec = [NSJSONSerialization JSONObjectWithData:[NSData dataWithContentsOfFile:[g_msl_dir stringByAppendingPathComponent:@"cases.json"]] options:0 error:nil];
    NvShader *vs = shader_get([spec[@"vertex"] stringByAppendingString:@"_b"], 0);
    EXPECT(vs && vs->geo_fn, "translated vertex shader with its capture entry");
    if (!vs) { printf("{\"passed\":false,\"failure\":\"vertex shader\"}\n"); return 1; }
    id<MTLLibrary> l = library(@"#include <metal_stdlib>\nusing namespace metal;\n"
        "struct dm_in { float4 l0 [[user(locn0), flat]]; float4 l1 [[user(locn1), flat]]; float4 l2 [[user(locn2), flat]]; float4 l3 [[user(locn3), flat]]; };\n"
        "struct dm_out { float4 c0 [[color(0)]]; float4 c1 [[color(1)]]; float4 c2 [[color(2)]]; float4 c3 [[color(3)]]; };\n"
        "fragment dm_out tess_fs(dm_in in [[stage_in]]) { dm_out o; o.c0 = in.l0; o.c1 = in.l1; o.c2 = in.l2; o.c3 = in.l3; return o; }\n"
        "fragment float4 plain_fs() { return float4(1.0); }\n");
    NvShader *fs = hand(l, @"tess_fs", 1), *ctl = hand(l, @"plain_fs", 1);
    int number = 0;
    for (NSDictionary *c in spec[@"cases"]) run_case(c, vs, fs, ctl, number++);
    printf("{\"passed\":%s,\"cases\":%d,\"checks\":%d,\"failed\":%d,\"pixels\":%d,\"patches\":%d,\"triangles\":%d,\"edges\":%d,\"nearEdgeSkipped\":%d,\"tessDraws\":%llu,\"refused\":%llu%s%s%s}\n",
           failed ? "false" : "true", cases_run, checks, failed, pixels_checked, patches_checked, triangles_checked, edges_checked, near_edges,
           (unsigned long long)g_tess_draws, (unsigned long long)g_tess_refused, failed ? ",\"failure\":\"" : "", failed ? failure : "", failed ? "\"" : "");
    return failed != 0;
}}
