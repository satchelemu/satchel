// Translation ahead (developer trial: DM_SHADER_AHEAD=<lanes 1..8>, set by -SatchelShaderAhead; any
// pipeline mode). Included before the engine's prediction walk (translate_ahead.py). Submit thread only.
//
// On a game's first launch the draws of a new scene, and the prediction walk that looks ahead for their
// pipelines, translate each new program and compile its libraries one at a time on the submit thread.
// Here, at a submission's first draw with an untranslated stage and at its first pipeline miss (before
// the walk), the programs the rest of the submission's draws bind are sent to the ahead lanes in draw
// order (PhoneShaderRuntime.inc, ShaderWarmupResources.swift). Each job makes the request that draw
// makes (its pixel stage, then its vertex stage padded with that pixel stage's inputs and the key's
// persisted mask). These are hints only: the draws and the walk still prepare every program themselves,
// and the source store and compiler join identical in-flight work, so each finds a finished or running
// job instead of starting its own. No draw changes: same requests, same translations, same pipelines.
static uint64_t satchel_translate_submission = UINT64_MAX;
// The launch policy sets DM_SHADER_AHEAD before the first session; read once, so an unset trial costs
// the draw path one load and compare.
static int satchel_translate_lanes = -1;
static int satchel_translate_on(void) {
    if (satchel_translate_lanes < 0) satchel_translate_lanes = satchel_shader_ahead_lanes();
    return satchel_translate_lanes > 0;
}
static void satchel_translate_rest(void) {
    if (!satchel_translate_on() || satchel_translate_submission == g_sub_serial || !g_exec_next || !satchel_phone_ahead_jobs_on()) return;
    satchel_translate_submission = g_sub_serial;
    uint64_t start = satchel_shader_now();
    // The replay's own stage bindings and return stack, so the walk sees the rest of every caller too.
    const prog_t *bst[5]; memcpy(bst, S.bst, sizeof bst);
    fz_cursor cur = {.p = g_exec_next};
    if (g_ret_n <= 8) for (int k = 0; k < g_ret_n; k++) cur.ret[cur.ret_n++] = g_ret[k];
    enum { SLOTS = 1024, JOBS = 256 };
    static const prog_t *seen[SLOTS][2]; memset(seen, 0, sizeof seen);
    int draws = 0, sent = 0, pairs = 0;
    for (int guard = 0; guard < 200000 && sent < JOBS && pairs < SLOTS / 2; guard++) {
        const uint8_t *q; op_hdr hq; const void *aq;
        if (!fz_next(&cur, &q, &hq, &aq)) break;
        const prog_t *vertex, *pixel;
        if (hq.op == OP_BIND_PROGRAM) {
            const p_bind_program *bp_ = aq; int bits = gfx_stage_bits(bp_->stages);
            for (int st = 0; st < 5; st++) if (bits >> st & 1) bst[st] = bp_->prog;
            if (!(bp_->stages & 0x20) || !bp_->prog) continue;
            vertex = NULL; pixel = bp_->prog;   // a compute program: its dispatch prepares it the same way
        } else if (hq.op == OP_DRAW || hq.op == OP_DRAW_IDX) {
            draws++;
            vertex = bst[0]; pixel = bst[1];
            if (!vertex && !pixel) continue;
        } else continue;
        // Each pair once per walk (open addressing on the two program pointers).
        size_t slot = (size_t)((((uintptr_t)vertex * 0x9E3779B97F4A7C15ull) ^ ((uintptr_t)pixel * 0xC2B2AE3D27D4EB4Full)) >> 54);
        while ((seen[slot][0] || seen[slot][1]) && (seen[slot][0] != vertex || seen[slot][1] != pixel)) slot = (slot + 1) % SLOTS;
        if (seen[slot][0] || seen[slot][1]) continue;   // this pair was sent already
        seen[slot][0] = vertex; seen[slot][1] = pixel; pairs++;
        if (vertex && pixel && vertex != pixel) {
            // Stages bound from two programs: the walk prepares each program on its own (bind_stages) and
            // the draw pads the vertex stage with the other program's pixel inputs. Send all three.
            sent += satchel_phone_ahead_send(vertex, vertex, 1, 1);
            sent += satchel_phone_ahead_send(pixel, pixel, 1, 1);
        }
        sent += satchel_phone_ahead_send(vertex, pixel, 1, 1);
    }
    satchel_shader_event("translate-rest", start);
    static int logged;
    if (getenv("DM_SHADER_EVENTS") && logged++ < 64)
        MLOG("translation ahead walk: %d draws, %d program pairs, %d jobs sent, depth %d, stopped %d", draws, pairs, sent, g_ret_n, cur.bad);
}
// The draw's trigger (draw_, before it prepares its own stages): only when one of its stages is new.
static void satchel_translate_rest_draw(void) {
    if (!satchel_translate_on() || satchel_translate_submission == g_sub_serial || !g_exec_next) return;
    for (int st = 0; st < 2; st++) {
        char key[17]; const void *control, *code; size_t control_size, code_size;
        if (satchel_phone_ahead_stage(S.bst[st], st, key, &control, &control_size, &code, &code_size) && !g_shmap[@(key)]) {
            satchel_translate_rest(); return;
        }
    }
}
