// General NVN capture adapter for the pinned NX2Apple replay host.
// All calls run on the submit thread. Captured bytes stay in DM_NVN_SHDUMP.
static const char *satchel_capture_dir(void) {
    static const char *dir; static int checked;
    if (!checked) {
        dir = getenv("DM_NVN_SHDUMP"); checked = 1;
        if (dir && *dir) {
            // An empty table is evidence that this capture hook ran, including
            // compute-only workloads. Existing cumulative pairs are preserved.
            FILE *file = fopen([[@(dir) stringByAppendingPathComponent:@"draw-pairs.tsv"] fileSystemRepresentation], "a");
            if (file) fclose(file);
            else { MLOG("[RED] runtime shader capture pair table cannot be initialized"); dir = NULL; }
        }
    }
    return dir && *dir ? dir : NULL;
}
static int satchel_capture_stage(const NVNshaderData *sd) {
    if (!sd || !sd->control || !sd->data) return -1;
    uint32_t size, magic; void *buffer; uint64_t offset;
    memcpy(&size, (const uint8_t *)sd->control + 0x704, 4);
    if (size < 4 || size >= (1u << 24) || !mtl_addr(sd->data, size, &buffer, &offset)) return -1;
    const uint8_t *code = (const void *)(uintptr_t)sd->data;
    memcpy(&magic, code, 4);
    if (magic == 0x12345679) return 5;
    if (magic == 0x12345678) {
        if (size < 0x34) return -1;
        uint32_t word; memcpy(&word, code + 0x30, 4);
        int stage = (word >> 10) & 15;
        if (stage == 0 && (word & 31) == 1) return 0;
        static const int stages[] = {-1, 0, 3, 4, 2, 1};
        return stage < 6 ? stages[stage] : -1;
    }
    uint32_t local[3]; memcpy(local, (const uint8_t *)sd->control + 0x748, sizeof local);
    return local[0] && local[1] && local[2] ? 5 : -1;
}
static NSString *satchel_capture_key(const prog_t *program, int stage, unsigned *index) {
    if (!program) return nil;
    for (unsigned end = program->nstage; end; end--) {
        unsigned i = end - 1;
        if (satchel_capture_stage(&program->sd[i]) != stage) continue;
        uint8_t key[8]; if (!prog_key(program, i, key)) return nil;
        char name[17]; for (int j = 0; j < 8; j++) snprintf(name + j * 2, 3, "%02x", key[j]);
        if (index) *index = i;
        return @(name);
    }
    return nil;
}
static void satchel_capture_program(const prog_t *program) {
    const char *dir = satchel_capture_dir(); if (!dir || !program) return;
    static NSMutableSet<NSString *> *written;
    if (!written) written = [NSMutableSet set];
    for (unsigned i = 0; i < program->nstage; i++) {
        int stage = satchel_capture_stage(&program->sd[i]); uint8_t digest[8];
        if (stage < 0 || !prog_key(program, i, digest)) {
            MLOG("[RED] runtime shader capture cannot identify program %llu index %u", (unsigned long long)program->serial, i); continue;
        }
        NSString *key = [NSString stringWithFormat:@"%02x%02x%02x%02x%02x%02x%02x%02x", digest[0], digest[1], digest[2], digest[3], digest[4], digest[5], digest[6], digest[7]];
        if ([written containsObject:key]) continue;
        const NVNshaderData *sd = &program->sd[i]; uint32_t own, count;
        memcpy(&own, (const uint8_t *)sd->control + 0x20, 4);
        memcpy(&count, (const uint8_t *)sd->control + 0x704, 4);
        uint32_t ctl = own >= 0x760 && own < 0x880 ? own : 0x880;
        NSString *base = [@(dir) stringByAppendingPathComponent:key]; NSError *error = nil;
        NSData *code = [NSData dataWithBytes:(const void *)(uintptr_t)sd->data length:count];
        NSData *control = [NSData dataWithBytes:sd->control length:ctl];
        if (![code writeToFile:[base stringByAppendingString:@".code"] options:NSDataWritingAtomic error:&error] ||
            ![control writeToFile:[base stringByAppendingString:@".ctl"] options:NSDataWritingAtomic error:&error]) {
            MLOG("[RED] runtime shader capture write failed: %s", error.localizedDescription.UTF8String); continue;
        }
        // Metadata follows the complete files. It contains identity, never shader bytes.
        FILE *file = fopen([[@(dir) stringByAppendingPathComponent:@"bound-programs.tsv"] fileSystemRepresentation], "a");
        if (!file) { MLOG("[RED] runtime shader capture inventory cannot be written"); continue; }
        fprintf(file, "%llu\t%d\t%s\n", (unsigned long long)program->serial, stage, key.UTF8String); fclose(file);
        [written addObject:key];
    }
}
static void satchel_capture_draw(const prog_t *const *bound) {
    const char *dir = satchel_capture_dir(); if (!dir) return;
    NSString *vertex = satchel_capture_key(bound[0], 0, NULL), *pixel = satchel_capture_key(bound[1], 1, NULL);
    if (!vertex || !pixel) return;
    NSString *pair = [NSString stringWithFormat:@"%@\t%@", vertex, pixel];
    static NSMutableSet<NSString *> *written;
    if (!written) written = [NSMutableSet set];
    if ([written containsObject:pair]) return;
    satchel_capture_program(bound[0]); satchel_capture_program(bound[1]);
    FILE *file = fopen([[@(dir) stringByAppendingPathComponent:@"draw-pairs.tsv"] fileSystemRepresentation], "a");
    if (!file) { MLOG("[RED] runtime shader capture pair table cannot be written"); return; }
    fprintf(file, "%s\n", pair.UTF8String); fclose(file); [written addObject:pair];
}
