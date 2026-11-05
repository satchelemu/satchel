"""Add timed demand selection without changing pinned code or cache eviction."""
import shutil


def apply(root, source):
    text = source.read_text()
    needle = 'void mtl_set_frame(uint64_t f) { atomic_store(&g_cur_frame, f);'
    assert text.count(needle) == 1
    text = text.replace(needle, 'static void satchel_pipeline_end_frame(void);\nvoid mtl_set_frame(uint64_t f) { satchel_pipeline_end_frame(); atomic_store(&g_cur_frame, f);')
    needle = '// The pipeline time line of the timing summary (hle_nvn.c)'
    assert text.count(needle) == 1
    text = text.replace(needle, '#include "AsyncPipelines.inc"\n' + needle)
    needle = '    if (!took && !g_pso_lmap && !ms_fn && hybrid_on()) {'
    assert text.count(needle) == 1
    text = text.replace(needle, '''    int asynchronous = satchel_async_on();
    if (asynchronous && !g_pso_lmap && !ms_fn) {
        ps = satchel_async_take(&k, vs, fs, &took, &g_pso_pull);
        if (satchel_skipped) return nil;
        if (g_pso_pull) { g_hy_ns += bp_now() - bt; return ps; }
    }
    if (!asynchronous && !took && !g_pso_lmap && !ms_fn && hybrid_on()) {''')
    if 'int satchel_mtl_session_retire(void)' in text:
        needle = 'satchel_mtl_release_strong();'
        assert text.count(needle) == 1
        text = text.replace(needle, needle + ' satchel_async_reset();')
    needle = 'static void draw_(const p_draw *dw, const p_draw_idx *di);'
    assert text.count(needle) == 1
    text = text.replace(needle, '#include "SkipPipelines.inc"\n' + needle)
    needle = 'g_pso_amp = g_sm_pass; cur_ps = pso_get(pr, nil); g_pso_amp = 0;'
    assert text.count(needle) == 1
    text = text.replace(needle, 'g_pso_amp = g_sm_pass; satchel_skipped = 0; satchel_skip_context = 1; cur_ps = pso_get(pr, nil); satchel_skip_context = 0; g_pso_amp = 0;')
    source.write_text(text)
    shutil.copyfile(root / 'NativeHost/graphics/SkipPipelines.inc', source.parent / 'SkipPipelines.inc')
    shutil.copyfile(root / 'NativeHost/graphics/AsyncPipelines.inc', source.parent / 'AsyncPipelines.inc')
