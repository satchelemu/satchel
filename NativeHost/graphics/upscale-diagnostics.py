"""Opt-in private Mac presentation A/B and same-source screenshot instrumentation."""
def apply(text):
    changes = {
        '    g_stat_draw++;\n': r'''    g_stat_draw++;
    // Private stage identifiers only. Shader payloads never enter the log.
    if (getenv("DM_UPSCALE_AUDIT") && g_gp_state == 1 && g_gp_cur >= 0 && pr) {
        static FILE *audit; if (!audit) audit = fopen(getenv("DM_UPSCALE_AUDIT"), "w");
        if (audit) {
            fprintf(audit, "%llu\t%llu\t%s\t%s\n", (unsigned long long)atomic_load(&g_cur_frame),
                    (unsigned long long)g_stat_pass, pr->sh[0] ? pr->sh[0]->key : "-", pr->sh[1] ? pr->sh[1]->key : "-");
            fflush(audit);
        }
    }
''',
        '    int kind = present_encode_m4(cb, src, dr.texture, crop, g_present_uv[alloc_i], &g_fx_present);': '''    // Diagnostic Mac build only. Each 120-present phase uses the same source path.
    uint64_t fx_sample = g_present_n, fx_phase = fx_sample / 120;
    const char *trial = getenv("DM_UPSCALE_TRIAL");
    if (trial) g_fx_mode = !strcmp(trial, "ab") ? (int)(fx_phase & 1) : 0;
    int kind = present_encode_m4(cb, src, dr.texture, crop, g_present_uv[alloc_i], &g_fx_present);''',
        '        present_gpu_note(f.GPUEndTime - f.GPUStartTime, fx_kind);': '''        if (trial && fx_sample % 30 == 0)
            MLOG("[upscale-time] present %llu phase %llu spatial %d gpu_ms %.6f error %d", (unsigned long long)fx_sample,
                 (unsigned long long)fx_phase, fx_kind, (f.GPUEndTime - f.GPUStartTime) * 1000, f.error != nil);
        present_gpu_note(f.GPUEndTime - f.GPUStartTime, fx_kind);''',
        'static double g_link_it_tin;': '''// Same source texture, crop, color space, origin and output canvas for both images.
static void satchel_compare_shot(id<MTLTexture> src, const char *path, const int crop[4]) {
    if (!getenv("DM_UPSCALE_COMPARE")) { shot(src, path, 1, crop); return; }
    int saved = g_fx_mode;
    for (int mode = 0; mode < 2; mode++) {
        g_fx_mode = mode; char name[1200];
        snprintf(name, sizeof name, "%s.%s.png", path, mode ? "spatial" : "off");
        shot(src, name, 1, crop);
    }
    g_fx_mode = saved;
}
static double g_link_it_tin;''',
        'shot(src, path, 1, crop); }': 'satchel_compare_shot(src, path, crop); }',
        'shot(src, g_shotq[i].path, 1, crop);': 'satchel_compare_shot(src, g_shotq[i].path, crop);',
    }
    for anchor, replacement in changes.items():
        if text.count(anchor) != 1:
            raise RuntimeError('Presentation diagnostic anchor is not unique')
        text = text.replace(anchor, replacement, 1)
    return text
