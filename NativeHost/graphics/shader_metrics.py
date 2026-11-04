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
    text = '#include "ShaderMetrics.inc"\n' + text
    source.write_text(text)
    shutil.copyfile(root / 'NativeHost/graphics/ShaderMetrics.inc', source.parent / 'ShaderMetrics.inc')
