#undef main
int main(void) { @autoreleasepool {
    setenv("DM_PSO_MTL4", "0", 1); setenv("DM_PSO_HYBRID", "0", 1);
    setenv("DM_NVN_PREDICT", "0", 1);
    test_vs = [NvShader new]; test_fs = [NvShader new];
    unsetenv("DM_PSO_BUDGET_MS");
    EXPECT_EQ("default budget is four milliseconds", 4000000, satchel_pipeline_budget_ns());
    for (const char **p = (const char *[]){"nan", "inf", "-1", "17", "4garbage", NULL}; *p; p++) {
        setenv("DM_PSO_BUDGET_MS", *p, 1);
        EXPECT_EQ("invalid budget uses default", 4000000, satchel_pipeline_budget_ns());
    }
    setenv("DM_PSO_BUDGET_MS", "0.5", 1);
    EXPECT_EQ("fractional budget", 500000, satchel_pipeline_budget_ns());
    setenv("DM_PSO_BUDGET_MS", "4", 1);
    GateDevice *device = [[GateDevice alloc] initBlockingThrough:1 nilCall:0];
    g_dev = (id)device;
    pso_key key = make_key(1);
    EXPECT_EQ("queued exact build", 1, pso_pend_demand(&key, test_vs, test_fs));
    EXPECT_TRUE("compiler held", [device waitForEntered:1 timeoutMs:5000]);
    atomic_store(&g_cur_frame, 1);
    int took = 0, pulling = 0;
    uint64_t start = satchel_shader_now();
    id result = satchel_async_take(&key, test_vs, test_fs, &took, &pulling);
    uint64_t elapsed = satchel_shader_now() - start;
    EXPECT_TRUE("deadline stops optional wait while compiler remains held", result == nil && elapsed < 50000000);
    EXPECT_EQ("deadline does not consume unfinished exact result", 0, took);
    EXPECT_EQ("deadline never invents fallback", 0, pulling);
    EXPECT_EQ("frame budget exhausted", 0, satchel_async_remaining);
    EXPECT_TRUE("timeout preserves pending job", queue_index(&key) >= 0);
    EXPECT_EQ("timeout does not cache failure", 0, hm_get_id(&g_pso_fail, &key) != nil);
    uint64_t kicked = g_pp_kicked;
    result = satchel_async_take(&key, test_vs, test_fs, &took, &pulling);
    EXPECT_EQ("repeat does not queue duplicate", kicked, g_pp_kicked);
    EXPECT_EQ("same frame has no fresh wait budget", 0, satchel_async_remaining);
    // A live unfinished compiler job may omit only a proven terminal draw.
    NvProg *program = [NvProg new]; program->sh[0] = test_vs; program->sh[1] = test_fs;
    test_vs->fn = (id)[NSObject new]; test_fs->fn = (id)[NSObject new];
    prog_t control = {.magic = MAGIC('P', 'R', 'O', 'G'), .nstage = 2, .serial = 0x7f020001};
    serial_put(control.serial, program); S.prog = &control; R.has_rt = 1; R.rt.n = 1;
    uint8_t suffix[256] = {0}; size_t used = 0;
    append_op(suffix, &used, OP_END, NULL, 0); g_exec_next = suffix;
    EXPECT_TRUE("known terminal draw is eligible", satchel_skip_can_draw());
    satchel_skip_context = 1; setenv("DM_PSO_SKIP", "1", 1);
    result = satchel_async_take(&key, test_vs, test_fs, &took, &pulling);
    EXPECT_TRUE("eligible timeout omits once", result == nil && satchel_skipped);
    EXPECT_EQ("one draw counted", 1, atomic_load(&satchel_skips_current));
    EXPECT_TRUE("skipped request remains pending", queue_index(&key) >= 0);
    S.tf_on = 1;
    result = satchel_async_take(&key, test_vs, test_fs, &took, &pulling);
    EXPECT_EQ("transform feedback cannot skip", 0, satchel_skipped); S.tf_on = 0;
    g_vis_counting = 1; EXPECT_EQ("active query cannot skip", 0, satchel_skip_can_draw()); g_vis_counting = 0;
    g_nrtm = 1; EXPECT_EQ("implicit storage readback cannot skip", 0, satchel_skip_can_draw()); g_nrtm = 0;
    g_nbcrt = 1; EXPECT_EQ("shadow writeback cannot skip", 0, satchel_skip_can_draw()); g_nbcrt = 0;
    g_ms_na = 1; EXPECT_EQ("diagnostic readback cannot skip", 0, satchel_skip_can_draw()); g_ms_na = 0;
    setenv("DM_NVN_TEXSNAP", "1", 1); EXPECT_EQ("attachment snapshot cannot skip", 0, satchel_skip_can_draw()); unsetenv("DM_NVN_TEXSNAP");
    S.ds.st = 1; EXPECT_EQ("stencil cannot skip", 0, satchel_skip_can_draw()); S.ds.st = 0;
    test_vs->rf.bindless = 1; EXPECT_EQ("unknown bindings cannot skip", 0, satchel_skip_can_draw()); test_vs->rf.bindless = 0;
    test_fs->rf.nb = 1;
    test_fs->rf.b[0].kind = NVN_RB_SBUFP;
    EXPECT_EQ("storage cannot skip", 0, satchel_skip_can_draw());
    int readonly[] = {NVN_RB_CBUF, NVN_RB_UBOP, NVN_RB_TEX, NVN_RB_SAMP, NVN_RB_SUPPORT,
        NVN_RB_C1, NVN_RB_DRV0, NVN_RB_DRV2, NVN_RB_SAMPT, NVN_RB_SAMPI, NVN_RB_BORDER};
    for (unsigned i = 0; i < sizeof readonly / sizeof *readonly; i++) {
        test_fs->rf.b[0].kind = readonly[i];
        EXPECT_TRUE("known read-only binding admitted", satchel_skip_can_draw());
        test_fs->rf.b[0].rw = 1;
        EXPECT_EQ("writable reflection always waits", 0, satchel_skip_can_draw());
        test_fs->rf.b[0].rw = 0;
    }
    test_fs->rf.b[0].kind = NVN_RB_IMG; EXPECT_EQ("image cannot skip", 0, satchel_skip_can_draw()); test_fs->rf.nb = 0;
    int forbidden[] = {OP_DRAW, OP_DRAW_IDX, OP_DRAW_TEX, OP_SET_RT, OP_COPY_T2T, OP_COPY_T2B,
        OP_COPY_B2T, OP_COPY_B2B, OP_COPY_STRIDE, OP_REPORT_COUNTER, OP_RESET_COUNTER,
        OP_TF_BIND, OP_TF_STATE, OP_DISPATCH, OP_SIGNAL_EVENT, OP_FENCE_SYNC, OP_UPDATE_UB,
        OP_BIND_PROGRAM, OP_BIND_IMG, OP_BIND_SSBO, OP_RENDER_COND, OP_COUNT_};
    uint8_t payload[128] = {0};
    for (unsigned i = 0; i < sizeof forbidden / sizeof *forbidden; i++) {
        used = 0; append_op(suffix, &used, forbidden[i], payload, sizeof payload); append_op(suffix, &used, OP_END, NULL, 0);
        EXPECT_EQ("unsafe suffix requires waiting", 0, satchel_skip_can_draw());
    }
    uint8_t terminal[16] = {0}; used = 0; append_op(terminal, &used, OP_END, NULL, 0);
    g_exec_next = terminal;
    uint64_t rest[] = {(uint64_t)suffix}; g_rest = rest; g_nrest = 1;
    EXPECT_EQ("remaining submission handle inspected", 0, satchel_skip_can_draw()); g_nrest = 0;
    g_ret[0] = suffix; g_ret_n = 1;
    EXPECT_EQ("caller suffix inspected", 0, satchel_skip_can_draw()); g_ret_n = 0;
    used = 0; uint64_t called = (uint64_t)suffix;
    uint8_t caller[64] = {0}; append_op(caller, &used, OP_CALL, &called, sizeof called); append_op(caller, &used, OP_END, NULL, 0);
    g_exec_next = caller; EXPECT_EQ("called suffix inspected", 0, satchel_skip_can_draw());
    called = (uint64_t)terminal; memcpy(caller + 8, &called, sizeof called);
    EXPECT_TRUE("complete harmless call accepted", satchel_skip_can_draw());
    satchel_pipeline_end_frame(); EXPECT_EQ("completed frame exposes skipped count", 1, satchel_pipeline_skipped_draws());
    satchel_pipeline_end_frame(); EXPECT_EQ("next frame resets skipped count", 0, satchel_pipeline_skipped_draws());
    satchel_skip_context = 0; unsetenv("DM_PSO_SKIP"); g_exec_next = NULL; S.prog = NULL;
    TestPipelineState *fallback = [TestPipelineState new];
    pso_key wrong = key; wrong.a2c = 1;
    hm_put_id(&g_pso_hy, &wrong, fallback);
    result = satchel_async_take(&key, test_vs, test_fs, &took, &pulling);
    EXPECT_TRUE("different coverage fallback rejected", result == nil && pulling == 0);
    hm_put_id(&g_pso_hy, &key, fallback);
    result = satchel_async_take(&key, test_vs, test_fs, &took, &pulling);
    EXPECT_TRUE("exact-key ready fallback survives held compiler", result == fallback && pulling == 1 && took == 0);
    EXPECT_TRUE("fallback retains pending exact work", queue_index(&key) >= 0);
    [device releaseAll]; EXPECT_TRUE("exact compile completes", pending_done(&key, 5000));
    pulling = took = 0;
    result = satchel_async_take(&key, test_vs, test_fs, &took, &pulling);
    EXPECT_TRUE("next request takes exact result", result == [device resultForCall:1] && took == 1 && pulling == 0);
    EXPECT_EQ("exact compiler never ran on caller", 0, [device syncCalls]);
    EXPECT_EQ("all requests shared one compile", 1, [device entered]);
    EXPECT_EQ("exact job released after consumption", -1, queue_index(&key));
    satchel_async_reset();
    EXPECT_EQ("session resets frame budget identity", UINT64_MAX, satchel_async_frame);
    EXPECT_EQ("session releases pending fallback owners", 0, satchel_fallbacks != nil);
    printf("== %d async pipeline checks, %d failed ==\n", g_checks, g_failed);
    return g_failed != 0;
} }
