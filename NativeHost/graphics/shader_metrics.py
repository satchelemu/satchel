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
    text = '#include "ShaderMetrics.inc"\n' + text
    source.write_text(text)
    shutil.copyfile(root / 'NativeHost/graphics/ShaderMetrics.inc', source.parent / 'ShaderMetrics.inc')
