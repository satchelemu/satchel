"""Translation ahead (developer trial, DM_SHADER_AHEAD): send a submission's remaining programs to the
ahead lanes at its first new shader and at its first pipeline miss. Builds with phone shaders only."""
import shutil


def apply(root, source):
    text = source.read_text()
    if '#include "PhoneShaderRuntime.inc"' not in text:
        return
    needle = 'static void pso_predict_rest_(void);\nstatic void pso_predict_rest(void) {'
    assert text.count(needle) == 1
    text = text.replace(needle, '#include "TranslateAhead.inc"\n' + needle)
    # Before the walk and its library prefetch: the walk then joins jobs already under way on the lanes.
    needle = ' shader_prefetch_rest(); pso_predict_rest_();'
    assert text.count(needle) == 1
    text = text.replace(needle, ' satchel_translate_rest();' + needle)
    # Before the draw prepares its own stages.
    needle = '        satchel_phone_prepare_program(S.bst[1], 0);\n'
    assert text.count(needle) == 1
    text = text.replace(needle, '        satchel_translate_rest_draw();\n' + needle)
    source.write_text(text)
    shutil.copyfile(root / 'NativeHost/graphics/TranslateAhead.inc', source.parent / 'TranslateAhead.inc')
