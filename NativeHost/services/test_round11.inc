// Round 11 synthetic contracts (apm, psm, csrng, mm:u, fatal:u) in both transports.
static uint32_t r11_call(SystemObject o, unsigned cmd, const void *args, size_t n) { return system_call(o, cmd, 0, args, n); }
static uint32_t r11_u32(int domain) { return satchel_u32(message + (domain ? 48 : 32)); }
static void test_round11(void) {
    for (int domain = 0; domain < 2; domain++) {
        satchel_sessions_reset();
        uint8_t *out = message + (domain ? 48 : 32);
        const char *const managers[] = {"apm", "apm:am"};
        for (unsigned m = 0; m < 2; m++) {
            SystemObject apm = system_root(managers[m], domain);
            test_perf_mode = 1; assert(!r11_call(apm, 1, NULL, 0) && r11_u32(domain) == 1);
            test_perf_mode = 0; assert(!r11_call(apm, 1, NULL, 0) && r11_u32(domain) == 0);
            assert(!r11_call(apm, 6, NULL, 0) && out[0] == (m ? 1 : 0)); // State is shared by the port pair.
            SystemObject session = system_child(apm, 0, NULL, 0);
            uint32_t mode = 0;
            if (!m) {
                assert(!r11_call(session, 1, &mode, 4) && r11_u32(domain) == 0x00020003);
                mode = 1; assert(!r11_call(session, 1, &mode, 4) && r11_u32(domain) == 0x00020004);
            }
            mode = 2; assert(r11_call(session, 1, &mode, 4) == SATCHEL_APM_INVALID);
            if (domain) assert(r11_call(session, 1, NULL, 0) == SATCHEL_IPC_INVALID); // Session requests carry CMIF padding.
            uint32_t set[2] = {0, 0x00020005};
            assert(!r11_call(session, 0, set, 8));
            mode = 0; assert(!r11_call(session, 1, &mode, 4) && r11_u32(domain) == 0x00020005);
            set[0] = 2; assert(r11_call(session, 0, set, 8) == SATCHEL_APM_INVALID);
            if (domain) assert(r11_call(session, 0, set, 4) == SATCHEL_IPC_INVALID);
            uint8_t on = 1; assert(!r11_call(session, 2, &on, 1));
            if (domain) assert(r11_call(session, 2, NULL, 0) == SATCHEL_IPC_INVALID);
            assert(!r11_call(apm, 6, NULL, 0) && out[0] == 1);
            assert(r11_call(session, 9, NULL, 0) == SATCHEL_IPC_UNKNOWN);
            system_close(session); system_close(apm);
        }
        satchel_sessions_reset(); // The session reset restores the reference defaults.
        SystemObject apm = system_root("apm", domain), session = system_child(apm, 0, NULL, 0);
        uint32_t mode = 0; assert(!r11_call(session, 1, &mode, 4) && r11_u32(domain) == 0x00020003);
        assert(!r11_call(apm, 6, NULL, 0) && out[0] == 0);
        system_close(session); system_close(apm);

        SystemObject psm = system_root("psm", domain);
        assert(!r11_call(psm, 0, NULL, 0) && r11_u32(domain) == 100);
        assert(!r11_call(psm, 1, NULL, 0) && r11_u32(domain) == 1);
        assert(r11_call(psm, 2, NULL, 0) == SATCHEL_IPC_UNKNOWN);
        SystemObject power = system_child(psm, 7, NULL, 0);
        unsigned before = test_event_count();
        uint32_t first = system_event(power, 0, 0), again = system_event(power, 0, 0);
        assert(first != again && test_event_same(first, again));
        assert(!r11_call(power, 1, NULL, 0)); // Unbind releases the session's own handle only.
        assert(test_event_count() == before + 2);
        uint32_t fresh = system_event(power, 0, 0); assert(!test_event_same(fresh, first));
        assert(!r11_call(power, 1, NULL, 0)); assert(!r11_call(power, 1, NULL, 0));
        for (unsigned c = 2; c <= 4; c++) {
            uint8_t enabled = 1; assert(!r11_call(power, c, &enabled, 1));
            if (domain) assert(r11_call(power, c, NULL, 0) == SATCHEL_IPC_INVALID);
        }
        satchel_event_close(first); satchel_event_close(again); satchel_event_close(fresh);
        satchel_event_close(system_event(power, 0, 0)); // Bound again: closing the object releases it.
        system_close(power); system_close(psm);
        assert(test_event_count() == before);

        SystemObject rng = system_root("csrng", domain);
        memset(buffer, 0, sizeof buffer);
        assert(!system_call(rng, 0, 2, NULL, 0));
        for (unsigned i = 0; i < sizeof buffer; i++) assert(buffer[i]);
        assert(system_call(rng, 0, 0, NULL, 0) == SATCHEL_IPC_INVALID);
        system_close(rng);

        SystemObject mm = system_root("mm:u", domain);
        uint32_t a[3] = {2, 0, 1};
        assert(!r11_call(mm, 4, a, 12) && r11_u32(domain) == 1);
        assert(!r11_call(mm, 4, a, 12) && r11_u32(domain) == 2);
        uint32_t s[3] = {1, 100, UINT32_MAX}; assert(!r11_call(mm, 6, s, 12));
        uint32_t id = 1; assert(!r11_call(mm, 7, &id, 4) && r11_u32(domain) == 100);
        id = 2; assert(!r11_call(mm, 7, &id, 4) && r11_u32(domain) == 0);
        id = 99; assert(!r11_call(mm, 7, &id, 4) && r11_u32(domain) == 0);
        assert(!r11_call(mm, 6, (uint32_t[3]){99, 1, 1}, 12)); // An unknown request is ignored.
        id = 1; assert(!r11_call(mm, 5, &id, 4)); assert(!r11_call(mm, 7, &id, 4) && r11_u32(domain) == 0);
        uint32_t old[3] = {3, 0, 0}; assert(!r11_call(mm, 0, old, 12) && !r11_u32(domain));
        assert(!r11_call(mm, 0, old, 12)); // A second request for the same module: the first one answers.
        uint32_t olds[3] = {3, 200, 300}; assert(!r11_call(mm, 2, olds, 12));
        uint32_t module = 3; assert(!r11_call(mm, 3, &module, 4) && r11_u32(domain) == 200);
        assert(!r11_call(mm, 1, &module, 4)); assert(!r11_call(mm, 3, &module, 4) && r11_u32(domain) == 0);
        assert(!r11_call(mm, 1, &module, 4)); assert(!r11_call(mm, 1, &module, 4));
        if (domain) for (unsigned c = 0; c < 8; c++) assert(r11_call(mm, c, NULL, 0) == SATCHEL_IPC_INVALID);
        assert(r11_call(mm, 8, NULL, 0) == SATCHEL_IPC_UNKNOWN);
        unsigned made = 0; uint32_t result;
        while (!(result = r11_call(mm, 4, a, 12))) made++;
        assert(result == SATCHEL_ROUND11_FULL && made == SATCHEL_MM_MAX - 1);
        satchel_sessions_reset(); mm = system_root("mm:u", domain);
        assert(!r11_call(mm, 4, a, 12) && r11_u32(domain) == 1);
        system_close(mm);

        SystemObject fatal = system_root("fatal:u", domain);
        uint64_t code = 0x2a2; uint8_t f[16] = {0}; memcpy(f, &code, 8);
        assert(!r11_call(fatal, 0, f, 8) && !strcmp(last_emit, "fatal-2162-0001-policy0"));
        satchel_put32(f + 8, 2); assert(!r11_call(fatal, 1, f, 12) && !strcmp(last_emit, "fatal-2162-0001-policy2"));
        assert(!r11_call(fatal, 2, f, 12) && !strcmp(last_emit, "fatal-2162-0001-policy2"));
        if (domain) assert(r11_call(fatal, 1, f, 8) == SATCHEL_IPC_INVALID && r11_call(fatal, 0, f, 4) == SATCHEL_IPC_INVALID);
        assert(r11_call(fatal, 3, f, 12) == SATCHEL_IPC_UNKNOWN);
        unsigned seen = logs; for (unsigned i = 0; i < 20; i++) (void)r11_call(fatal, 0, f, 8);
        assert(logs - seen <= 8); // Bounded diagnostics.
        system_close(fatal);
        satchel_sessions_reset(); assert(!test_event_count());
    }
    for (unsigned i = 0; i < sizeof round11_contracts / sizeof *round11_contracts; i++) {
        if (!round11_contracts[i].hits[0] || !round11_contracts[i].hits[1]) printf("missing round 11 contract %s %u\n", round11_contracts[i].kind, round11_contracts[i].command);
        assert(round11_contracts[i].hits[0] && round11_contracts[i].hits[1]);
    }
    puts("PASS: every round 11 contract (apm, psm, csrng, mm:u, fatal:u) in both transports, state, bounds, event lifetime and reset");
}
