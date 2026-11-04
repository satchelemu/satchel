// Called only on the NVN submit thread. Keys come from owned control snapshots.
static NSMutableArray<NvShader *> *satchel_phone_retired;
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
            int result = satchel_phone_shader_prepare(key, sd->control, controls, (const void *)(uintptr_t)sd->data, count, stage == 0 ? mask : 0);
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
static uint32_t satchel_phone_fragment_inputs(const prog_t *program) {
    if (!program || !satchel_phone_shader_inputs) return 0;
    NSString *key = satchel_capture_key(program, 1, NULL);
    return key ? satchel_phone_shader_inputs(key.UTF8String) : 0;
}
