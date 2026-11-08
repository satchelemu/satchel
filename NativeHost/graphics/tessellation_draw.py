"""NVN patch draws through the tessellation compute route, applied by overlay.py after view_upscale.py.

The host recorded nothing for nvnCommandBufferSetPatchSize (an empty implementation) and refused every
draw of primitive 14 ("primitive type 14 not implemented"). This overlay:

- records SetPatchSize and Set{Inner,Outer}TessellationLevels as OP_BIND_STATE kinds 6, 7 and 8 in
  hle_nvn.c (16 to 24 bytes a record, as the state binds already are); replay keeps them in the draw
  state S, which every lookahead walk saves and restores with the rest of it;
- loads the tessellation facts of hull (stage 3) and domain (stage 4) reflection in shader_load, and
  the domain's main0_z under the same depth convention a vertex shader takes;
- sends a patch draw to tess_draw (TessellationDraw.inc): vertex capture, hull kernel, compute
  tessellator (NativeShaders/Tessellator.swift's source, embedded here as g_tess_msl), then the domain
  shader drawn through draw() in the vertex stage's place with the program's pixel stage.

Every other draw takes the same path as before: the changes on it are a primitive check before the
geometry route, the stage number of the vertex stage's arguments (0 unless a domain draw is being
encoded) and a flag check before the encode. Levels set by SetInner/OuterTessellationLevels apply only
to a program without a hull shader, which this route refuses; they are recorded for that case.
"""
from pathlib import Path
import shutil


def c_string(text):
    return '\n'.join('"' + line.replace('\\', '\\\\').replace('"', '\\"') + '\\n"' for line in text.splitlines())


def tessellator_source(root):
    """The compute tessellator's Metal source, from the Swift file that owns it, plus the host's note kernel."""
    text = (root / 'NativeShaders/Tessellator.swift').read_text()
    start, end = 'public static let source = #"""\n', '\n"""#'
    if text.count(start) != 1:
        raise RuntimeError('Tessellator source anchor changed')
    body = text[text.index(start) + len(start):]
    body = body[:body.index(end)]
    # Counts patches dropped at capacity into the host's diagnostic counter (TessellationDraw.inc g_tess_stat).
    note = ('\nkernel void dm_tess_note(device dm_tess_totals& totals [[buffer(3)]], device atomic_uint* stat [[buffer(6)]]) {\n'
            '    atomic_fetch_add_explicit(stat, atomic_load_explicit(&totals.dropped, memory_order_relaxed), memory_order_relaxed);\n}\n')
    return body + note


def replace_once(text, anchor, replacement, what):
    if text.count(anchor) != 1:
        raise RuntimeError('Tessellation overlay anchor is not unique: ' + what)
    return text.replace(anchor, replacement, 1)


def apply(root, path):
    text = path.read_text()
    # Draw state: patch size and the levels a program without a hull would use (OP_BIND_STATE kinds 6, 7, 8).
    text = replace_once(text, '    int tex_base, samp_base;   // host-table bases',
        '    int32_t tess_patch; float tess_outer[4], tess_inner[2];   // SetPatchSize (0: never set) and Set{Outer,Inner}TessellationLevels (tessellation_draw.py)\n'
        '    int tex_base, samp_base;   // host-table bases', 'draw state')
    text = replace_once(text, '                case 5: dst = &S.ds; n = sizeof S.ds; break;\n',
        '                case 5: dst = &S.ds; n = sizeof S.ds; break;\n'
        '                case 6: dst = &S.tess_patch; n = sizeof S.tess_patch; break;\n'
        '                case 7: dst = S.tess_outer; n = sizeof S.tess_outer; break;\n'
        '                case 8: dst = S.tess_inner; n = sizeof S.tess_inner; break;\n', 'state kinds')
    text = replace_once(text, 'if (b->kind == 5) g_dss_dirty = 1; else g_pso_dirty = 1; }',
        'if (b->kind == 5) g_dss_dirty = 1; else if (b->kind < 6) g_pso_dirty = 1; }', 'state dirty bits')
    # Reflection facts on the shader objects.
    text = replace_once(text, '    uint8_t mesh_kind, mesh_notask; uint16_t mesh_threads, mesh_payload;',
        '    uint8_t tess_role, tess_domain, tess_spacing, tess_cw, tess_points; uint32_t tess_out, tess_in_bytes, tess_words; int16_t *tess_slot; id<MTLComputePipelineState> tess_pso;   // hull (role 1) and domain (2) reflection (tessellation_draw.py)\n'
        '    uint8_t mesh_kind, mesh_notask; uint16_t mesh_threads, mesh_payload;', 'shader fields')
    text = replace_once(text, '- (void)dealloc { free(rf.c1); free(geo_slot); }', '- (void)dealloc { free(rf.c1); free(geo_slot); free(tess_slot); }', 'shader dealloc')
    text = replace_once(text, 'NvProg *geo_raster, *geo_lean_raster;', 'NvProg *geo_raster, *geo_lean_raster, *tess_raster;', 'program fields')
    text = replace_once(text, '    NSDictionary *mst = [j[@"mesh_stage"] isKindOfClass:[NSDictionary class]] ? j[@"mesh_stage"] : nil;\n', r'''    NSDictionary *tess = (stage == 3 || stage == 4) && [j[@"tessellation"] isKindOfClass:[NSDictionary class]] ? j[@"tessellation"] : nil;
    if (tess && [tess[@"control_point_bytes"] intValue] == 576 && [tess[@"patch_bytes"] intValue] == 528) {   // the route's records (NativeShaders/Tessellation.swift)
        if (stage == 3 && [tess[@"role"] isEqual:@"control"] && [tess[@"capture_words"] isKindOfClass:[NSArray class]] && [tess[@"capture_words"] count] <= GEO_CAPTURE_WORDS) {
            NSArray *w = tess[@"capture_words"]; o->tess_slot = malloc(GEO_CAPTURE_WORDS * sizeof *o->tess_slot);
            for (int k = 0; k < GEO_CAPTURE_WORDS; k++) o->tess_slot[k] = -1;
            for (NSUInteger i = 0; i < w.count; i++) { int k = [w[i] intValue]; if (k >= 0 && k < GEO_CAPTURE_WORDS) o->tess_slot[k] = (int16_t)i; }
            o->tess_words = (uint32_t)w.count; o->tess_out = [tess[@"output_vertices"] unsignedIntValue]; o->tess_in_bytes = [tess[@"input_bytes_per_vertex"] unsignedIntValue]; o->tess_role = 1;
        } else if (stage == 4 && [tess[@"role"] isEqual:@"evaluation"]) {
            NSDictionary *domains = @{@"isolines": @0, @"triangles": @1, @"quads": @2}, *spacings = @{@"equal": @0, @"fractional_even": @1, @"fractional_odd": @2};
            NSNumber *dm = domains[tess[@"domain"]], *sp = spacings[tess[@"spacing"]];
            if (dm && sp) { o->tess_domain = dm.unsignedCharValue; o->tess_spacing = sp.unsignedCharValue; o->tess_cw = [tess[@"winding"] isEqual:@"cw"]; o->tess_points = [tess[@"point_mode"] boolValue]; o->tess_role = 2; }
            if (atomic_load(&g_depth_zero) && j[@"entry_z"]) entry = j[@"entry_z"];   // the depth convention, as a vertex shader takes it
        }
        if (!o->tess_role) MLOG("[RED] shader %s: tessellation reflection not understood", pc.UTF8String);
    }
    NSDictionary *mst = [j[@"mesh_stage"] isKindOfClass:[NSDictionary class]] ? j[@"mesh_stage"] : nil;
''', 'shader load')
    # The route, before draw_.
    shutil.copyfile(Path(__file__).with_name('TessellationDraw.inc'), path.parent / 'TessellationDraw.inc')
    code = '// ---------- Compute tessellator source (NativeShaders/Tessellator.swift; tessellation_draw.py) ----------\nstatic const char g_tess_msl[] =\n' + c_string(tessellator_source(root)) + ';\n#include "TessellationDraw.inc"\n'
    text = replace_once(text, 'static void draw_(const p_draw *dw, const p_draw_idx *di) {\n', code + 'static void draw_(const p_draw *dw, const p_draw_idx *di) {\n', 'draw definition')
    text = replace_once(text, '    if (!g_geo_program && S.prog) {\n        NvProg *geometry_program = prog_get(S.prog);\n',
        '    if (!g_geo_program && S.prog && (dw ? dw->prim : di->prim) == NVN_DRAW_PRIMITIVE_PATCHES) { tess_draw(prog_get(S.prog), dw, di); return; }   // tessellation_draw.py\n'
        '    if (!g_geo_program && S.prog) {\n        NvProg *geometry_program = prog_get(S.prog);\n', 'patch draw hook')
    text = replace_once(text, '    if (pr->sh[0]) encode_args(en, pr->sh[0], 0, 1);\n',
        '    if (pr->sh[0]) encode_args(en, pr->sh[0], g_tess_raster.on ? 4 : 0, 1);   // a domain shader in the vertex stage keeps NVN stage 4\'s resources\n', 'vertex stage arguments')
    text = replace_once(text, '    if (g_geo_program) {\n        if (g_geo_count) enc_r_draw_indexed(',
        '    if (g_tess_raster.on) { tess_raster_encode(en, mp); return; }\n    if (g_geo_program) {\n        if (g_geo_count) enc_r_draw_indexed(', 'domain encode')
    path.write_text(text)


def host(path):
    """hle_nvn.c: record the patch size and default levels instead of dropping them."""
    text = path.read_text()
    text = replace_once(text, '    "nvnCommandBufferSetPatchSize",\n', '', 'empty implementation list')
    anchor = next(line for line in text.splitlines(keepends=True) if line.startswith('static void bind_state(NVNcommandBuffer *c, int kind, const void *st, size_t n) {'))
    text = replace_once(text, anchor, anchor +
        '// Tessellation state as OP_BIND_STATE kinds 6, 7 and 8 (Satchel tessellation_draw.py; nvn_mtl.m keeps them in S)\n'
        'NVN(nvnCommandBufferSetPatchSize, void, NVNcommandBuffer *c, int n) { int32_t v = n; bind_state(c, 6, &v, sizeof v);\n'
        '    static _Atomic int once; if (!atomic_exchange(&once, 1)) LOG("[nvn] SetPatchSize %d: the game records tessellation state; patch draws take the tessellation route", n); }\n'
        'NVN(nvnCommandBufferSetOuterTessellationLevels, void, NVNcommandBuffer *c, const float *l) { bind_state(c, 7, l, 4 * sizeof *l); }\n'
        'NVN(nvnCommandBufferSetInnerTessellationLevels, void, NVNcommandBuffer *c, const float *l) { bind_state(c, 8, l, 2 * sizeof *l); }\n',
        'state recording')
    path.write_text(text)
