"""Move actual Metal pipeline calls to two workers, keeping cache waits outside."""
from pathlib import Path
import re
import shutil


def apply(root, source):
    text = source.read_text()
    # Archive lookups are kept in place. Duplicate base requests join the owner's
    # result before reaching these helpers, so they cannot occupy compiler lanes.
    patterns = [
        (r'\[g_dev newComputePipelineStateWithFunction:([\w>\-]+) error:(&\w+)\]',
         r'satchel_compute_build(g_dev, \1, \2)', 7),
        (r'\[g_dev newRenderPipelineStateWithDescriptor:(\w+) error:(&?\w+)\]',
         r'satchel_render_device(g_dev, \1, \2)', 7),
        (r'\[(g_comp4|ep->comp) newRenderPipelineStateWithDescriptor:(\w+) compilerTaskOptions:(\w+) error:(\w+)\]',
         r'satchel_render_compile(\1, \2, \3, \4)', 3),   # the third: a pipeline built whole (a pixel shader that reads the primitive ID)
        (r'\[g_comp4 newRenderPipelineStateBySpecializationWithDescriptor:(\w+) pipeline:(\w+) error:(\w+)\]',
         r'satchel_render_specialize(g_comp4, \1, \2, \3)', 1),
    ]
    for pattern, replacement, expected in patterns:
        text, count = re.subn(pattern, replacement, text)
        assert count == expected, (pattern, count)
    # The submitting thread of the current submission (bounded mode orders the lanes by it).
    needle = 'void mtl_sub_begin(void) {'
    assert text.count(needle) == 1
    text = text.replace(needle, needle + ' satchel_submitter = pthread_self();')
    needle = '#include <objc/runtime.h>'
    assert text.count(needle) == 1
    text = text.replace(needle, needle + '\n#include "ShaderBuildQueue.inc"')
    source.write_text(text)
    shutil.copyfile(root / 'NativeHost/graphics/ShaderBuildQueue.inc', source.parent / 'ShaderBuildQueue.inc')
