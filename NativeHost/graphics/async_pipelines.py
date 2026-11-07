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
    # Bounded mode: a prediction walk leaves out programs whose ahead work is unfinished
    # (PhoneShaderRuntime.inc). Other modes and builds without phone shaders walk all of them.
    if '#include "PhoneShaderRuntime.inc"' in text:
        needle = '            __unsafe_unretained NvProg *pr = S.prog ? prog_get(S.prog) : nil; if (!pr || !pr->sh[0]) continue;'
        assert text.count(needle) == 1
        text = text.replace(needle, needle.replace('S.prog ? prog_get(S.prog) : nil', 'S.prog && satchel_phone_predict_ready(S.prog) ? prog_get(S.prog) : nil'))
        # Bounded mode admits more of the walk's pipelines to the existing lanes (16, or DM_PSO_SPECULATIVE
        # in any mode), walks again later in the submission while ahead work completes, and never compiles
        # a pulling library there.
        needle = '    if (spec && active >= PSO_PEND_ACTIVE) return -1;'
        assert text.count(needle) == 1
        text = text.replace(needle, needle.replace('PSO_PEND_ACTIVE', '(satchel_speculative_setting() ? satchel_speculative_setting() : satchel_phone_ahead_on() ? 16 : PSO_PEND_ACTIVE)'))
        needle = '    for (int i = 0; i < na + nb && admitted < PSO_PREDICT_BUDGET; i++) {'
        assert text.count(needle) == 1
        text = text.replace(needle, needle.replace('PSO_PREDICT_BUDGET', '(satchel_speculative_setting() ? satchel_speculative_setting() : satchel_phone_ahead_on() ? 16 : PSO_PREDICT_BUDGET)'))
        needle = '        if (hy && !hm_get_id(&g_pso, qk) && hybrid_get(qk, qv, qf) != nil) continue;'
        assert text.count(needle) == 1
        text = text.replace(needle, needle.replace('if (hy && ', 'if (hy && !satchel_phone_ahead_on() && '))
        needle = '    pso_pend_start(slot, gen, pso_desc(k, vs, fs, nil, 0), dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0));'
        assert text.count(needle) == 1
        text = text.replace(needle, needle.replace('dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0)', 'satchel_speculative_queue()'))
        needle = '    memcpy(&S, &Ssave, sizeof S); R.sc = sc0; R.has_sc = has_sc0; args_dirty_all();'
        assert text.count(needle) == 1
        text = text.replace(needle, '    { static int said; if (getenv("DM_SHADER_EVENTS") && said++ < 64) MLOG("parallel precompile walk: %d draws, %d likely and %d alternative keys, %d admitted", draws, na, nb, admitted); }\n' + needle)
        needle = 'static void pso_predict_rest_(void) {\n    if (!g_exec_next) return;'
        assert text.count(needle) == 1
        text = text.replace(needle, needle + ' satchel_phone_predict_skipped = 0;')
        needle = '    if (predict && g_pp_submit_mark != g_sub_serial) {'
        assert text.count(needle) == 1
        text = text.replace(needle, '    if (predict && (g_pp_submit_mark != g_sub_serial || satchel_phone_predict_again(g_sub_serial))) {')
    needle = 'static void draw_(const p_draw *dw, const p_draw_idx *di);'
    assert text.count(needle) == 1
    text = text.replace(needle, '#include "SkipPipelines.inc"\n' + needle)
    needle = 'g_pso_amp = g_sm_pass; cur_ps = pso_get(pr, nil); g_pso_amp = 0;'
    assert text.count(needle) == 1
    text = text.replace(needle, 'g_pso_amp = g_sm_pass; satchel_skipped = 0; satchel_skip_context = 1; cur_ps = pso_get(pr, nil); satchel_skip_context = 0; g_pso_amp = 0;')
    source.write_text(text)
    shutil.copyfile(root / 'NativeHost/graphics/SkipPipelines.inc', source.parent / 'SkipPipelines.inc')
    shutil.copyfile(root / 'NativeHost/graphics/AsyncPipelines.inc', source.parent / 'AsyncPipelines.inc')
