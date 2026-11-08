// Round 12 synthetic contracts (audrec:u, grc:c, hidbus, lp2p:app, mnpp:app, notif:a, npns:u, ntc) in both transports.
static uint32_t r12_call(SystemObject o, unsigned cmd, const void *args, size_t n) { return system_call(o, cmd, 0, args, n); }
static uint8_t *r12_out(int domain) { return message + (domain ? 48 : 32); }
// A request with one type-A send buffer of `size` bytes (the layout of a type-B descriptor).
static uint32_t r12_send_call(SystemObject o, unsigned cmd, uint32_t size, const void *args, size_t n) {
    int slot = satchel_session_slot(o.h); assert(slot >= 0);
    const char *kind = satchel_domains[satchel_sessions[slot].tree].objects[o.id - 1].kind;
    for (unsigned i = 0; i < sizeof round12_contracts / sizeof *round12_contracts; i++)
        if (round12_contracts[i].command == cmd && !strcmp(round12_contracts[i].kind, kind)) round12_contracts[i].hits[o.domain]++;
    make(cmd, o.domain, 2, args, n);
    satchel_put32(message, 4 | (1u << 20)); satchel_put32(message + 8, size);
    if (o.domain) satchel_put32(message + 36, o.id);
    assert(!satchel_session_send(o.h, message, sizeof message, NULL));
    unsigned cm = 16 + (o.domain ? 16 : 0);
    assert(satchel_u32(message + cm) == 0x4f434653);
    return satchel_u32(message + cm + 8);
}
static int r12_zero(const uint8_t *p, size_t n) { for (size_t i = 0; i < n; i++) if (p[i]) return 0; return 1; }
static void test_round12(void) {
    for (int domain = 0; domain < 2; domain++) {
        satchel_sessions_reset();
        unsigned before = test_event_count();
        uint8_t *out = r12_out(domain);

        // audrec:u: the recorder opens with two channels at the requested rate and records nothing.
        SystemObject audrec = system_root("audrec:u", domain);
        uint32_t open[4] = {48000, 0, 0x51, 0};
        assert(!r12_call(audrec, 0, open, 16));
        assert(satchel_u32(out) == 48000 && satchel_u32(out + 4) == 2 && !satchel_u32(out + 8) && !satchel_u32(out + 12));
        uint32_t child = satchel_u32(message + (domain ? 64 : 12)); assert(child);   // the object follows the 16 data bytes
        SystemObject recorder = {domain ? audrec.h : child, domain ? child : 1, domain};
        if (domain) assert(r12_call(audrec, 0, open, 8) == SATCHEL_IPC_INVALID);
        assert(r12_call(audrec, 1, NULL, 0) == SATCHEL_IPC_UNKNOWN);
        assert(!r12_call(recorder, 0, NULL, 0) && !satchel_u32(out));
        assert(!r12_call(recorder, 1, NULL, 0) && !r12_call(recorder, 2, NULL, 0));
        uint64_t pointer = 0x12340000; uint32_t work[4] = {48000, 2, 0, 0};
        assert(!r12_call(recorder, 3, &pointer, 8) && !r12_call(recorder, 8, &pointer, 8) && !r12_call(recorder, 11, work, 16));
        uint32_t first = system_event(recorder, 4, 0), again = system_event(recorder, 4, 0);
        assert(first != again && test_event_same(first, again));
        assert(!r12_call(recorder, 5, NULL, 0) && r12_zero(out, 16));
        assert(!r12_call(recorder, 9, NULL, 0) && r12_zero(out, 16));
        assert(!r12_call(recorder, 6, &pointer, 8) && !out[0]);
        assert(!r12_call(recorder, 7, &pointer, 8) && !satchel_u64(out));
        assert(!r12_call(recorder, 10, NULL, 0) && !out[0]);
        if (domain) {
            static const unsigned sized[] = {3, 6, 7, 8, 11};
            for (unsigned i = 0; i < 5; i++) assert(r12_call(recorder, sized[i], &pointer, 4) == SATCHEL_IPC_INVALID);
        }
        assert(r12_call(recorder, 12, NULL, 0) == SATCHEL_IPC_UNKNOWN);
        satchel_event_close(first); satchel_event_close(again);
        system_close(recorder); system_close(audrec);
        assert(test_event_count() == before);

        // Registered without commands: the session opens, every command stays unknown.
        const char *const bare[] = {"grc:c", "notif:a", "npns:u"};
        for (unsigned i = 0; i < 3; i++) {
            SystemObject o = system_root(bare[i], domain);
            assert(r12_call(o, 0, NULL, 0) == SATCHEL_IPC_UNKNOWN && r12_call(o, 1, NULL, 0) == SATCHEL_IPC_UNKNOWN);
            system_close(o);
        }

        // hidbus: no rail device, so no bus handle.
        SystemObject hidbus = system_root("hidbus", domain);
        uint32_t bus[6] = {0x20, 0, 1, 0, 0x51, 0};
        assert(!r12_call(hidbus, 1, bus, 24) && r12_zero(out, 16));
        if (domain) assert(r12_call(hidbus, 1, bus, 16) == SATCHEL_IPC_INVALID);
        assert(r12_call(hidbus, 0, NULL, 0) == SATCHEL_IPC_UNKNOWN);
        system_close(hidbus);

        // lp2p:app: both objects open; Initialize answers 0; the monitor's two events are its own and never signal.
        SystemObject lp2p = system_root("lp2p:app", domain);
        SystemObject sf = system_child(lp2p, 0, NULL, 0), monitor = system_child(lp2p, 8, NULL, 0);
        assert(r12_call(lp2p, 1, NULL, 0) == SATCHEL_IPC_UNKNOWN);
        assert(!r12_call(sf, 0, NULL, 0) && !satchel_u32(out));
        static const unsigned groups[] = {768, 776, 1536, 1544};
        for (unsigned i = 0; i < 4; i++) assert(!r12_call(sf, groups[i], NULL, 0));
        assert(r12_call(sf, 256, NULL, 0) == SATCHEL_IPC_UNKNOWN);
        assert(!r12_call(monitor, 0, NULL, 0) && !satchel_u32(out));
        uint32_t state = system_event(monitor, 256, 0), state2 = system_event(monitor, 256, 0), join = system_event(monitor, 328, 0);
        assert(test_event_same(state, state2) && !test_event_same(state, join));
        static const unsigned infos[] = {288, 296, 312};
        for (unsigned i = 0; i < 3; i++) assert(!r12_call(monitor, infos[i], NULL, 0));
        assert(r12_call(monitor, 768, NULL, 0) == SATCHEL_IPC_UNKNOWN);
        satchel_event_close(state); satchel_event_close(state2); satchel_event_close(join);
        system_close(sf); system_close(monitor); system_close(lp2p);
        assert(test_event_count() == before);

        // mnpp:app: Initialize succeeds; telemetry checks the user and the title buffer and stores nothing.
        SystemObject mnpp = system_root("mnpp:app", domain);
        uint64_t pid = 0; assert(!r12_call(mnpp, 0, &pid, 8));
        if (domain) assert(r12_call(mnpp, 0, NULL, 0) == SATCHEL_IPC_INVALID);
        uint64_t user[2] = {0, 0};
        assert(r12_call(mnpp, 1, user, 16) == SATCHEL_IPC_INVALID);   // no title buffer
        assert(r12_send_call(mnpp, 1, 16, user, 16) == SATCHEL_MNPP_ARGUMENT);
        user[0] = 1;
        assert(!r12_send_call(mnpp, 1, 16, user, 16) && !r12_send_call(mnpp, 1, 64, user, 16));
        assert(r12_send_call(mnpp, 1, 65, user, 16) == SATCHEL_MNPP_BUFFER_SIZE);
        if (domain) assert(r12_send_call(mnpp, 1, 16, user, 8) == SATCHEL_IPC_INVALID);
        assert(r12_call(mnpp, 2, NULL, 0) == SATCHEL_IPC_UNKNOWN);
        system_close(mnpp);

        // ntc: offline, StartTask is NetworkTimeNotAvailable; Cancel signals the finish event and sets the result.
        SystemObject ntc = system_root("ntc", domain);
        uint64_t unknown = 0;
        SystemObject task = system_child(ntc, 0, &unknown, 8);
        if (domain) assert(r12_call(ntc, 0, NULL, 0) == SATCHEL_IPC_INVALID);
        assert(r12_call(task, 0, NULL, 0) == SATCHEL_NTC_UNAVAILABLE);
        assert(!r12_call(task, 2, NULL, 0));
        uint32_t finish = system_event(task, 1, 0);
        assert(!r12_call(task, 3, NULL, 0) && test_event_signaled(finish));
        assert(r12_call(task, 2, NULL, 0) == SATCHEL_NTC_CANCELED);
        uint32_t finish2 = system_event(task, 1, 1); assert(test_event_same(finish, finish2));
        assert(r12_call(task, 0, NULL, 0) == SATCHEL_NTC_UNAVAILABLE && r12_call(task, 2, NULL, 0) == SATCHEL_NTC_CANCELED);
        assert(r12_call(task, 4, NULL, 0) == SATCHEL_IPC_UNKNOWN);
        SystemObject early = system_child(ntc, 0, &unknown, 8);   // canceled before anyone asked for the event
        assert(!r12_call(early, 3, NULL, 0));
        uint32_t done = system_event(early, 1, 1);
        satchel_event_close(finish); satchel_event_close(finish2); satchel_event_close(done);
        system_close(early); system_close(task); system_close(ntc);
        assert(test_event_count() == before);
        satchel_sessions_reset(); assert(!test_event_count());
    }
    for (unsigned i = 0; i < sizeof round12_contracts / sizeof *round12_contracts; i++) {
        if (!round12_contracts[i].hits[0] || !round12_contracts[i].hits[1]) printf("missing round 12 contract %s %u\n", round12_contracts[i].kind, round12_contracts[i].command);
        assert(round12_contracts[i].hits[0] && round12_contracts[i].hits[1]);
    }
    puts("PASS: every round 12 contract (audrec:u, hidbus, lp2p:app, mnpp:app, ntc) in both transports; grc:c, notif:a and npns:u open with no commands; argument, buffer and user checks, event lifetime, cancel and reset");
}
