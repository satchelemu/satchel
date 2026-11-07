"""Instrument actual Metal calls, with thread IDs to separate submit and workers."""
from pathlib import Path
import re
import shutil


def apply(root, source):
    text = source.read_text()
    direct = 'static id<MTLRenderPipelineState> pso_build(MTLRenderPipelineDescriptor *d, NSError **e) { return pso_build_ex(d, e, 1); }'
    text = text.replace(direct, direct.replace('return pso_build_ex(d, e, 1);', 'return SATCHEL_SHADER_TIME("pipeline-use", pso_build_ex(d, e, 1));'))
    # Find the balanced Objective C message containing each creation selector.
    # The engine's calls are synchronous; no callback completion is timed here.
    spans = []
    for match in re.finditer(r'\bnew(?:Render|Compute)PipelineState\w*:', text):
        start = text.rfind('[', 0, match.start())
        depth = 0
        for end in range(start, len(text)):
            if text[end] == '[': depth += 1
            elif text[end] == ']':
                depth -= 1
                if depth == 0: break
        else: raise RuntimeError('Unclosed Metal creation message')
        receiver = text[start + 1:match.start()].strip()
        kind = 'pipeline-archive' if receiver == 'a' or receiver == 'g_arch_bundle' else 'pipeline-create'
        spans.append((start, end + 1, kind))
    if len(spans) != 5: raise RuntimeError('Pinned Metal archive lookup sites changed')
    for start, end, kind in reversed(spans):
        text = text[:start] + 'SATCHEL_SHADER_TIME("' + kind + '", ' + text[start:end] + ')' + text[end:]
    needle = 'void mtl_sub_begin(void) {'
    assert text.count(needle) == 1
    text = text.replace(needle, needle + '\n    satchel_shader_event("submit-thread", satchel_shader_now());')
    # Pending pipeline waits can dominate even when all actual builds use workers.
    needle = 'pso_pend_take(&k)'
    if needle in text:
        start = text.index(needle)
        end = text.index(';', start)
        text = text[:start] + 'SATCHEL_SHADER_TIME("pipeline-wait", ' + text[start:end] + ')' + text[end:]
    # Frame boundaries on the event clock, for per-frame stall attribution (bench_shaders.py).
    needle = 'void mtl_set_frame(uint64_t f) {'
    assert text.count(needle) == 1
    text = text.replace(needle, needle + ' satchel_shader_event("frame", satchel_shader_now());')
    # The draw's own stage preparation, apart from look-ahead walks that resolve later programs.
    needle = '        satchel_phone_prepare_program(S.bst[1], 0);\n'
    if needle in text:
        assert text.count(needle) == 1
        end = '        for (int stage = 2; stage < 5; stage++) satchel_phone_prepare_program(S.bst[stage], 0);\n'
        assert text.count(end) == 1
        text = text.replace(needle, '        satchel_phone_draw_prepare = 1;\n' + needle)
        text = text.replace(end, end + '        satchel_phone_draw_prepare = 0;\n')
    # Shader setup (library, functions, reflection) and pipeline descriptors on the frame path.
    needle = 'uint64_t bt = bp_now(); o = shader_load(pc, stage, 0);'
    assert text.count(needle) == 1
    text = text.replace(needle, 'uint64_t bt = bp_now(); o = SATCHEL_SHADER_TIME("shader-load", shader_load(pc, stage, 0));')
    needle = 'static MTLRenderPipelineDescriptor *pso_desc(const pso_key *k, NvShader *vs, NvShader *fs, id<MTLFunction> fsfn, int merged) {'
    assert text.count(needle) == 1
    text = text.replace(needle, needle.replace('pso_desc(', 'pso_desc_(')[:-2] + ';\n' +
        needle + ' return SATCHEL_SHADER_TIME("pipeline-descriptor", pso_desc_(k, vs, fs, fsfn, merged)); }\n' +
        needle.replace('pso_desc(', 'pso_desc_('))
    needle = 'static void pso_predict_rest(void) { uint64_t t0 = bp_now();'
    assert text.count(needle) == 1
    text = text.replace(needle, needle + ' uint64_t satchel_walk = satchel_shader_now();')
    needle = 'g_pt_pred_n++; g_pt_pred_ns += bp_now() - t0; }'
    assert text.count(needle) == 1
    text = text.replace(needle, 'g_pt_pred_n++; g_pt_pred_ns += bp_now() - t0; satchel_shader_event("prediction", satchel_walk); }')
    # Which pipelines a draw built itself: merged-pass or specialized-fragment keys, which the
    # prediction walk cannot compute, apart from ordinary keys it did not admit or predict.
    needle = '        MTLRenderPipelineDescriptor *d = pso_desc(&k, vs, fs, ms_fn ? ms_fn : g_pso_fsfn, g_pso_lmap != NULL);\n        ps = pso_build(d, &e);\n'
    assert text.count(needle) == 1
    text = text.replace(needle, needle.replace('ps = pso_build(d, &e);', 'ps = SATCHEL_SHADER_TIME(g_pso_lmap || ms_fn ? "pipeline-own-merged" : "pipeline-own", pso_build(d, &e));'))
    needle = '    if (ps) g_pp_taken++;\n'
    assert text.count(needle) == 1
    text = text.replace(needle, '    if (ps) { g_pp_taken++; satchel_shader_event("pipeline-taken", satchel_shader_now()); }\n')
    # Diagnostic (DM_SHADER_EVENTS only): each pipeline a draw builds itself is compared with the keys the
    # last prediction walk computed, by the first field group that differs (payload-free event kinds).
    needle = 'static void pso_predict_rest(void);\n'
    assert text.count(needle) == 1
    text = text.replace(needle, needle + r'''// Shader names are copied while the walk's shader objects are alive; only pointers are compared later.
static pso_key satchel_walk_keys[320]; static char satchel_walk_names[320][2][24]; static int satchel_walk_n;
static void satchel_walk_note(const pso_key *k) {
    if (satchel_walk_n >= 320) return;
    satchel_walk_keys[satchel_walk_n] = *k;
    strlcpy(satchel_walk_names[satchel_walk_n][0], k->vs ? ((__bridge NvShader *)k->vs)->key : "", 24);
    strlcpy(satchel_walk_names[satchel_walk_n][1], k->fs ? ((__bridge NvShader *)k->fs)->key : "", 24);
    satchel_walk_n++;
}
static void satchel_own_classify(const pso_key *k) {
    if (!getenv("DM_SHADER_EVENTS")) return;
    int best = 0; const char *kind = "own-unseen";
    const char *vname = k->vs ? ((__bridge NvShader *)k->vs)->key : "", *fname = k->fs ? ((__bridge NvShader *)k->fs)->key : "";
    for (int i = 0; i < satchel_walk_n; i++) {
        const pso_key *q = &satchel_walk_keys[i]; int score; const char *why;
        if (!memcmp(q, k, sizeof *k)) { kind = "own-predicted"; break; }
        if (q->fs != k->fs) {
            if (best < 1 && q->vs == k->vs) { best = 1; kind = !strcmp(satchel_walk_names[i][1], fname) ? "own-fs-revision-differs" : "own-fs-differs"; }
            continue;
        }
        if (q->vs != k->vs) { score = 2; why = strcmp(satchel_walk_names[i][0], vname) ? "own-vs-differs" : "own-vs-revision-differs"; }
        else if (memcmp(q->cfmt, k->cfmt, sizeof k->cfmt) || q->samples != k->samples) { score = 3; why = "own-color-format-differs"; }
        else if (q->dfmt != k->dfmt || q->sfmt != k->sfmt) { score = 4; why = "own-depth-format-differs"; }
        else if (memcmp(q->attr, k->attr, sizeof k->attr) || memcmp(q->stream, k->stream, sizeof k->stream) || memcmp(q->attr_hi, k->attr_hi, sizeof k->attr_hi)) { score = 5; why = "own-vertex-layout-differs"; }
        else if (q->blend_en != k->blend_en || q->chmask != k->chmask || q->a2c != k->a2c || memcmp(q->blend, k->blend, sizeof k->blend)) { score = 6; why = "own-blend-differs"; }
        else { score = 7; why = "own-other-differs"; }
        if (score > best) { best = score; kind = why; }
    }
    if (!strcmp(kind, "own-unseen")) for (int i = 0; i < satchel_walk_n; i++)
        if (!strcmp(satchel_walk_names[i][0], vname) && !strcmp(satchel_walk_names[i][1], fname)) {
            // The walk saw this vertex and pixel pair, through other shader objects (reloaded or translated again).
            pso_key q = satchel_walk_keys[i]; q.vs = k->vs; q.fs = k->fs;
            kind = !memcmp(&q, k, sizeof q) ? "own-objects-differ" : "own-objects-and-state-differ"; break;
        }
    satchel_shader_event(kind, satchel_shader_now());
}
''')
    needle = '        ps = SATCHEL_SHADER_TIME(g_pso_lmap || ms_fn ? "pipeline-own-merged" : "pipeline-own", pso_build(d, &e));'
    assert text.count(needle) == 1
    text = text.replace(needle, '        if (!g_pso_lmap && !ms_fn) satchel_own_classify(&k);\n' + needle)
    needle = '    (void)df; (void)sf;\n'
    assert text.count(needle) == 1
    text = text.replace(needle, needle + '    satchel_walk_n = 0; for (int i = 0; i < na; i++) satchel_walk_note(&qa[i].k); for (int i = 0; i < nb; i++) satchel_walk_note(&qb[i].k);\n')
    text = '#include "ShaderMetrics.inc"\n' + text
    source.write_text(text)
    shutil.copyfile(root / 'NativeHost/graphics/ShaderMetrics.inc', source.parent / 'ShaderMetrics.inc')
