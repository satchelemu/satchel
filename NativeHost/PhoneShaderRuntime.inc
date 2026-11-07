// Called only on the NVN submit thread. Keys come from owned control snapshots.
static NSMutableArray<NvShader *> *satchel_phone_retired;
static int satchel_phone_draw_prepare;   // set while draw_ prepares its own bound stages
static void satchel_phone_prepare_program(const prog_t *program, uint32_t wanted) {
    if (!satchel_phone_enabled() || !program || !program->serial || !program->nstage) return;
    static NSMutableDictionary<NSNumber *, NSNumber *> *attempts, *epochs;
    if (!attempts) { attempts = [NSMutableDictionary dictionary]; epochs = [NSMutableDictionary dictionary]; }
    NSNumber *identity = @(program->serial), *previous = attempts[identity];
    uint32_t mask = previous.unsignedIntValue | wanted;
    if (!previous || (wanted & ~previous.unsignedIntValue)) {
        shmap_load();
        int complete = 1;
        if (attempts.count >= 65536) { [attempts removeAllObjects]; [epochs removeAllObjects]; }
        // Fragment inputs are real pair observations. They determine vertex padding.
        for (int pass = 0; pass < 2; pass++) for (unsigned i = 0; i < program->nstage; i++) {
            int stage = satchel_capture_stage(&program->sd[i]);
            if (stage < 0) { complete = 0; continue; }
            if ((pass == 0) != (stage == 1)) continue;
            uint8_t digest[8]; if (!prog_key(program, i, digest)) { complete = 0; continue; }
            char key[17]; for (int j = 0; j < 8; j++) snprintf(key + 2*j, 3, "%02x", digest[j]);
            if (builtin_get(key)) continue;
            const NVNshaderData *sd = &program->sd[i]; uint32_t own, count;
            memcpy(&own, (const uint8_t *)sd->control + 0x20, 4);
            memcpy(&count, (const uint8_t *)sd->control + 0x704, 4);
            size_t controls = own >= 0x760 && own < 0x880 ? own : 0x880;
#ifdef SATCHEL_SHADER_TIME
            // Diagnostic split: the draw's own stages, or a look-ahead walk resolving a later program.
            int result = SATCHEL_SHADER_TIME(satchel_phone_draw_prepare ? "prepare-draw" : "prepare-lookahead",
                satchel_phone_shader_prepare(key, sd->control, controls, (const void *)(uintptr_t)sd->data, count, stage == 0 ? mask : 0));
#else
            int result = satchel_phone_shader_prepare(key, sd->control, controls, (const void *)(uintptr_t)sd->data, count, stage == 0 ? mask : 0);
#endif
            if (result > 0) {
                static NSString *const names[] = {@"vertex", @"pixel", @"geometry", @"hull", @"domain", @"compute"};
                g_shmap[@(key)] = names[stage];
                if (stage == 1 && satchel_phone_shader_inputs) mask |= satchel_phone_shader_inputs(key);
                if (result == 1) {
                    if (!satchel_phone_retired) satchel_phone_retired = [NSMutableArray array];
                    for (NSString *name in g_shcache.allKeys) if ([name hasPrefix:@(key)]) {
                        // Pipeline keys retain shader addresses. Keep revisions alive
                        // so allocator address reuse cannot select an older pipeline.
                        if (satchel_phone_retired.count >= 4096) MDIE("phone shader revision budget exceeded");
                        [satchel_phone_retired addObject:g_shcache[name]]; [g_shcache removeObjectForKey:name];
                    }
                    satchel_phone_epoch++;
                    MLOG("PhoneShader key=%s stage=%d translated", key, stage);
                }
            } else {
                complete = 0;
                MLOG("[RED] PhoneShader key=%s stage=%d prepare failed status=%d", key, stage, result);
            }
        }
        if (complete) attempts[identity] = @(mask);
    }
    if (!epochs[identity] || epochs[identity].unsignedLongLongValue != satchel_phone_epoch) {
        serial_del(program->serial); epochs[identity] = @(satchel_phone_epoch);
    }
}
// Launch prewarm of recorded pipelines (DM_SATCHEL_PREWARM=1, set by -SatchelPipelinePrewarm YES).
#define SATCHEL_PHONE_PREWARM 1   // graphics/prewarm.patch uses the hooks below only when this file is part of the build
static int satchel_phone_prewarm_on(void) {
    const char *v = getenv("DM_SATCHEL_PREWARM");   // set by the launch policy before a session starts
    return v && !strcmp(v, "1") && satchel_phone_enabled() && satchel_phone_shader_prepare_known;
}
// Called only by the blocking prewarm on the submit thread, before the first submission encodes
// anything (mtl_prepare): a key of the recorded lists that the shader map does not hold yet is
// prepared from this device's capture of it, through the same preparation a draw makes, so the
// prewarm builds the libraries and pipelines that draw would build. Keys without a capture (compute
// programs, other stages) stay unknown and are built at first use, as before.
static int satchel_phone_prewarm_known(NSString *key) {
    if (!satchel_phone_prewarm_on() || key.length != 16) return 0;
    int32_t stage = -1;
    int result = satchel_phone_shader_prepare_known(key.UTF8String, &stage);
    if (result <= 0 || (stage != 0 && stage != 1 && stage != 5)) return 0;
    static NSString *const names[] = {@"vertex", @"pixel", @"geometry", @"hull", @"domain", @"compute"};
    g_shmap[key] = names[stage];
    if (result == 1) {
        if (!satchel_phone_retired) satchel_phone_retired = [NSMutableArray array];
        for (NSString *name in g_shcache.allKeys) if ([name hasPrefix:key]) {
            if (satchel_phone_retired.count >= 4096) MDIE("phone shader revision budget exceeded");
            [satchel_phone_retired addObject:g_shcache[name]]; [g_shcache removeObjectForKey:name];
        }
        satchel_phone_epoch++;
        MLOG("PhoneShader key=%s stage=%d translated (prewarm)", key.UTF8String, stage);
    }
    return 1;
}
static uint32_t satchel_phone_fragment_inputs(const prog_t *program) {
    if (!program || !satchel_phone_shader_inputs) return 0;
    NSString *key = satchel_capture_key(program, 1, NULL);
    return key ? satchel_phone_shader_inputs(key.UTF8String) : 0;
}
// Bounded first use. Called on the game's recording threads (BindProgram, hle_nvn.c), never on the
// draw path: the program's vertex and pixel (or compute) stages, as the draw would prepare them, go to
// the ahead lanes. Lossy dedup by program serial; the draw stays the authority and joins or repeats
// any missing work exactly. (Hints at ProgramSetShaders were tried: Zelda registers far more programs
// than it draws soon, and that work delayed the libraries its draws needed.)
void satchel_phone_ahead(const prog_t *program) {
    if (!satchel_phone_ahead_on() || !program || !program->serial || !program->nstage || program->nstage > 6) return;
    static _Atomic uint64_t seen[4096];
    uint64_t serial = program->serial; size_t slot = (size_t)((serial * 0x9E3779B97F4A7C15ull) >> 52);
    if (atomic_load_explicit(&seen[slot], memory_order_relaxed) == serial) return;
    atomic_store_explicit(&seen[slot], serial, memory_order_relaxed);
    int index[2] = {-1, -1};
    for (unsigned i = 0; i < program->nstage; i++) {
        int stage = satchel_capture_stage(&program->sd[i]);
        if ((stage == 0 || stage == 1) && index[stage] < 0) index[stage] = (int)i;
        // A compute program goes in the pixel slot (no vertex padding); embedded kernels never translate.
        if (stage == 5 && program->nstage == 1) index[1] = (int)i;
    }
    char keys[2][17]; const void *controls[2] = {0}, *codes[2] = {0}; size_t control_sizes[2] = {0}, code_sizes[2] = {0};
    int any = 0;
    for (int s = 0; s < 2; s++) {
        if (index[s] < 0) continue;
        uint8_t digest[8]; if (!prog_key(program, (unsigned)index[s], digest)) continue;
        for (int j = 0; j < 8; j++) snprintf(keys[s] + 2*j, 3, "%02x", digest[j]);
        int embedded = 0;
        for (unsigned b = 0; b < sizeof k_nvn_builtin_map / sizeof *k_nvn_builtin_map; b++) embedded |= !strcmp(keys[s], k_nvn_builtin_map[b].ctl);
        if (embedded) continue;
        const NVNshaderData *sd = &program->sd[index[s]]; uint32_t own, count;
        memcpy(&own, (const uint8_t *)sd->control + 0x20, 4);
        memcpy(&count, (const uint8_t *)sd->control + 0x704, 4);
        controls[s] = sd->control; control_sizes[s] = own >= 0x760 && own < 0x880 ? own : 0x880;
        codes[s] = (const void *)(uintptr_t)sd->data; code_sizes[s] = count; any = 1;
    }
    if (any) satchel_phone_shader_ahead(controls[0] ? keys[0] : NULL, controls[0], control_sizes[0], codes[0], code_sizes[0],
                                        controls[1] ? keys[1] : NULL, controls[1], control_sizes[1], codes[1], code_sizes[1]);
}
// Submit thread, prediction walks only: in bounded mode a program whose ahead work has not finished is
// left out of the walk instead of being translated and loaded there, one by one, on the frame path.
// Its own draw still prepares it exactly. Exact and async modes walk every program, as before.
enum { SATCHEL_AHEAD_WALKS = 32 };   // the speculative limit (16 in this mode) is in graphics/async_pipelines.py
static int satchel_phone_predict_skipped;   // the last walk left out a program still being prepared
static int satchel_phone_predict_ready(const prog_t *program) {
    if (!satchel_phone_ahead_on() || !program) return 1;
    if (program->serial && serial_get(program->serial)) return 1;
    for (unsigned i = 0; g_shcache && i < program->nstage; i++) {
        int stage = satchel_capture_stage(&program->sd[i]);
        if (stage < 0) break;
        uint8_t digest[8]; if (!prog_key(program, i, digest)) break;
        char key[17]; for (int j = 0; j < 8; j++) snprintf(key + 2*j, 3, "%02x", digest[j]);
        if (g_shcache[[@(key) stringByAppendingString:@"_b"]]) continue;
        if (!satchel_phone_shader_ahead_ready(key)) break;
        if (i + 1 == program->nstage) return 1;
    }
    satchel_phone_predict_skipped = 1;
    return 0;
}
// A later pipeline miss in the same submission walks again (bounded mode only) when the previous walk
// left programs out and more ahead jobs have finished since: at most SATCHEL_AHEAD_WALKS times, 10 ms apart.
static int satchel_phone_predict_again(uint64_t submission) {
    static uint64_t current = UINT64_MAX, last, progress; static int walks;
    if (!satchel_phone_ahead_on()) return 0;
    if (submission != current) { current = submission; walks = 0; }
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    uint64_t now = (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec, done = satchel_phone_shader_ahead_ready(NULL);
    if (!satchel_phone_predict_skipped || walks >= SATCHEL_AHEAD_WALKS || now - last < 10000000 || done == progress) return 0;
    walks++; last = now; progress = done; satchel_phone_predict_skipped = 0;
    return 1;
}
