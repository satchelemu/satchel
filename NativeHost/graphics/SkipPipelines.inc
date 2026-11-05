// A deliberately narrow terminal-pass proof. No speculative resource ownership
// or replay: only the unfinished draw is omitted, and the next draw retries.
// Future operations are followed across calls, returns and every remaining handle.
// Any unsupported operation, alias or incomplete scan requires the exact pipeline.
static int satchel_skip_can_draw(void) {
    if (!R.has_rt || (!R.rt.n && !R.rt.depth) || !g_exec_next || g_ret_n > 8 ||
        S.tf_on || S.rast_discard || S.ds.st || g_vis_counting || g_geo_program ||
        g_geo_mesh.on || g_fz.active || g_fz_checking || g_defer_replaying ||
        defer_has_active() || g_sm_pass || g_rt_merged || g_rt_range_active || g_hoist_n ||
        g_nrtm || g_nbcrt || g_lp.count || g_ms_na || getenv("DM_NVN_TEXSNAP")) return 0;
    // Pass-end mirrors and compressed/depth view shadows copy attachment
    // results into other storage even without an explicit copy command.
    NvProg *pr = S.prog ? prog_get(S.prog) : nil;
    if (!pr || !pr->sh[0] || !pr->sh[1] || pr->sh[2] || pr->sh[3] || pr->sh[4]) return 0;
    for (int st = 0; st < 2; st++) {
        NvShader *sh = pr->sh[st];
        if (!sh->fn || sh->builtin || sh->rf.bindless || sh->rf.dyn) return 0;
        for (int i = 0; i < sh->rf.nb; i++) {
            const nvn_rbind *b = &sh->rf.b[i];
            // Reject storage even when reflection calls it read-only. Images,
            // indirect bindings and unknown reflection kinds cannot prove purity.
            if (b->rw) return 0;
            switch (b->kind) {
                case NVN_RB_CBUF: case NVN_RB_UBOP: case NVN_RB_TEX:
                case NVN_RB_SAMP: case NVN_RB_SAMPT: case NVN_RB_SAMPI:
                case NVN_RB_SUPPORT: case NVN_RB_C1: case NVN_RB_DRV0:
                case NVN_RB_DRV2: case NVN_RB_BORDER: break;
                default: return 0;
            }
        }
    }
    // No later draw is admitted by this first policy. It avoids propagating an
    // omitted depth/color result through feedback sampling or shader side effects.
    // State-only suffixes are harmless; even a copy to an unrelated texture waits.
    fz_cursor cursor = {.p = g_exec_next, .ret_n = g_ret_n};
    memcpy(cursor.ret, g_ret, sizeof(*g_ret) * (size_t)g_ret_n);
    op_hdr h; const void *a;
    for (int count = 0; count < 4096; count++) {
        if (!fz_next(&cursor, NULL, &h, &a)) return !cursor.bad && !cursor.p;
        switch (h.op) {
            case OP_VIEWPORTS: case OP_SCISSOR: case OP_DEPTH_RANGE:
            case OP_BLEND_COLOR: case OP_POLY_OFFSET: case OP_DEPTH_CLAMP:
            case OP_VP_SWIZZLE: case OP_PRIM_RESTART: case OP_ALPHA_REF: break;
            default: return 0;
        }
    }
    return 0;
}
