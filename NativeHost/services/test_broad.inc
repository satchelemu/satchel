// Round 6 synthetic contracts, both moved-session and domain transports.
static uint32_t broad_call(uint32_t h, int domain, uint32_t id, uint32_t cmd,
                           unsigned descriptor, const void *args, size_t n) {
    make(cmd, domain, descriptor, args, n);
    if (domain) satchel_put32(message + (descriptor == 2 ? 36 : 20), id);
    assert(!satchel_session_send(h, message, sizeof message, NULL));
    unsigned raw = (satchel_u32(message + 4) >> 31) ? 12 + ((satchel_u32(message + 8) >> 1) & 15) * 4 + ((satchel_u32(message + 8) >> 5) & 15) * 4 : 8;
    unsigned cm = ((raw + 15) & ~15u) + (domain ? 16 : 0);
    assert(satchel_u32(message + cm) == 0x4f434653);
    return satchel_u32(message + cm + 8);
}
static void broad_events(uint32_t h, int domain, uint32_t id, uint32_t cmd, unsigned count, int signaled) {
    assert(!broad_call(h, domain, id, cmd, 0, NULL, 0));
    assert(satchel_u32(message + 4) >> 31 && satchel_u32(message + 8) == count * 2);
    uint32_t handles[2];
    for (unsigned i = 0; i < count; i++) {
        handles[i] = satchel_u32(message + 12 + i * 4);
        assert(test_event_signaled(handles[i]) == signaled);
    }
    assert(!broad_call(h, domain, id, cmd, 0, NULL, 0));
    for (unsigned i = 0; i < count; i++) {
        uint32_t again = satchel_u32(message + 12 + i * 4);
        assert(again != handles[i]);
        assert(test_event_same(again, handles[i]));
        satchel_event_close(again); satchel_event_close(handles[i]);
    }
}
static void test_broad(void) {
    for (int domain = 0; domain < 2; domain++) {
        satchel_sessions_reset();
        uint32_t root, object;
        uint8_t args[48] = {0};
        uint8_t *out = message + (domain ? 48 : 32);
        assert(!satchel_session_connect(&root, "aoc:u", NULL));
        if (domain) assert(!satchel_session_control(root, 1, &object));
        uint64_t title = satchel_program_id(); memcpy(args, &title, 8);
        for (unsigned explicit_id = 0; explicit_id < 2; explicit_id++) {
            unsigned count_cmd = explicit_id ? 0 : 2, list_cmd = explicit_id ? 1 : 3;
            unsigned off = explicit_id ? 8 : 0;
            if (explicit_id) memcpy(args, &title, 8);
            assert(!broad_call(root, domain, 1, count_cmd, 0, args, off));
            assert(satchel_u32(out) == 3);
            satchel_put32(args + off, 1); satchel_put32(args + off + 4, 2);
            memset(buffer, 0xcc, sizeof buffer);
            assert(!broad_call(root, domain, 1, list_cmd, 2, args, off + 8));
            assert(satchel_u32(out) == 2 && satchel_u32(buffer) == 7 && satchel_u32(buffer + 4) == 2000 && buffer[8] == 0xcc);
            satchel_put32(args + off, 0); satchel_put32(args + off + 4, 0);
            assert(!broad_call(root, domain, 1, list_cmd, 2, args, off + 8) && !satchel_u32(out));
            satchel_put32(args + off + 4, 2);
            satchel_put32(args + off, UINT32_MAX);
            assert(!broad_call(root, domain, 1, list_cmd, 2, args, off + 8) && !satchel_u32(out));
            satchel_put32(args + off + 4, 65);
            assert(broad_call(root, domain, 1, list_cmd, 2, args, off + 8) == SATCHEL_AOC_BUFFER);
            assert(broad_call(root, domain, 1, list_cmd, 0, args, off + 8) == SATCHEL_IPC_INVALID);
            assert(!broad_call(root, domain, 1, explicit_id ? 4 : 5, 0, args, off));
            assert(satchel_u64(out) == title + 0x1000);
            satchel_put32(args + off, 7);
            assert(!broad_call(root, domain, 1, explicit_id ? 6 : 7, 0, args, off + 4));
        }
        uint64_t foreign = title + 0x2000;
        assert(!broad_call(root, domain, 1, 0, 0, &foreign, 8) && !satchel_u32(out));
        assert(!broad_call(root, domain, 1, 9, 0, NULL, 0) && satchel_u64(out) == ((1000ull << 32) | 2164));
        assert(!broad_call(root, domain, 1, 50, 0, NULL, 0));
        broad_events(root, domain, 1, 8, 1, 0); broad_events(root, domain, 1, 10, 1, 0);
        // A failed two-handle reply must release its client copies.
        satchel_session_close(root);
        assert(test_event_count() == 0);
        assert(!satchel_session_connect(&root, "nifm:u", NULL));
        if (domain) assert(!satchel_session_control(root, 1, &object));
        uint32_t general = port_child(root, domain, 4);
        uint32_t gh = domain ? root : general, gid = domain ? general : 1;
        uint32_t version = 2;
        assert(!broad_call(gh, domain, gid, 4, 0, &version, 4));
        uint32_t request = satchel_u32(message + (domain ? 48 : 12));
        uint32_t h = domain ? root : request, id = domain ? request : 1;
        assert(!broad_call(h, domain, id, 0, 0, NULL, 0) && satchel_u32(out) == 1);
        assert(broad_call(h, domain, id, 1, 0, NULL, 0) == SATCHEL_NO_INTERNET);
        broad_events(h, domain, id, 2, 2, 0);
        assert(!broad_call(h, domain, id, 3, 0, NULL, 0));
        assert(!broad_call(h, domain, id, 4, 0, NULL, 0));
        assert(!broad_call(h, domain, id, 11, 0, &version, 1));
        assert(broad_call(h, domain, id, 21, 0, &version, 4) == ((180u << 9) | 110u));
        if (domain) {
            unsigned before = test_event_count();
            make(2, 1, 0, NULL, 0); satchel_put32(message + 20, id);
            assert(satchel_session_send(h, message, 64, NULL) == SATCHEL_IPC_INVALID);
            assert(test_event_count() == before);
        }
        if (!domain) { satchel_session_close(request); satchel_session_close(general); }
        satchel_session_close(root); assert(test_event_count() == 0);
        assert(!satchel_session_connect(&root, "friend:u", NULL));
        if (domain) assert(!satchel_session_control(root, 1, &object));
        uint32_t friend = port_child(root, domain, 0);
        h = domain ? root : friend; id = domain ? friend : 1;
        broad_events(h, domain, id, 0, 1, 1);
        assert(!broad_call(h, domain, id, 1, 0, NULL, 0));
        memset(args, 0, sizeof args); memcpy(args + 8, satchel_uid, 16);
        const unsigned lists[] = {10100, 10101, 10400};
        for (unsigned i = 0; i < 3; i++) {
            unsigned desc = lists[i] == 10101 ? 2 : 1;
            memset(buffer, 0xcc, sizeof buffer);
            assert(!broad_call(h, domain, id, lists[i], desc, args, sizeof args) && !satchel_u32(out));
            for (unsigned j = 0; j < sizeof buffer; j++) assert(buffer[j] == 0xcc);
            memset(args + 8, 0, 16);
            uint32_t result = broad_call(h, domain, id, lists[i], desc, args, sizeof args);
            assert(result == (i == 2 ? 0 : SATCHEL_FRIEND_ARGUMENT));
            memcpy(args + 8, satchel_uid, 16);
        }
        const unsigned available[] = {10120, 10121, 10420, 10421};
        for (unsigned i = 0; i < 4; i++) {
            assert(!broad_call(h, domain, id, available[i], 0, satchel_uid, 16));
            if (!(i & 1)) assert(out[0] == 1);
        }
        memset(args, 0, 16);
        assert(broad_call(root, domain, 1, 1, 0, args, 16) == SATCHEL_FRIEND_ARGUMENT);
        assert(!broad_call(root, domain, 1, 1, 0, satchel_uid, 16));
        uint32_t notify = satchel_u32(message + (domain ? 48 : 12));
        h = domain ? root : notify; id = domain ? notify : 1;
        broad_events(h, domain, id, 0, 1, 0);
        assert(!broad_call(h, domain, id, 1, 0, NULL, 0));
        assert(broad_call(h, domain, id, 2, 0, NULL, 0) == SATCHEL_FRIEND_EMPTY);
        // Client copies survive the service object, and duplicates retain owner state.
        assert(!broad_call(h, domain, id, 0, 0, NULL, 0));
        uint32_t ev = satchel_u32(message + 12), dup;
        assert(!satchel_session_control(h, 2, &dup)); satchel_session_close(h);
        assert(broad_call(dup, domain, id, 2, 0, NULL, 0) == SATCHEL_FRIEND_EMPTY);
        satchel_session_close(dup);
        assert(test_event_signaled(ev) == 0); satchel_event_close(ev);
        if (!domain) { satchel_session_close(friend); satchel_session_close(root); }
        assert(test_event_count() == 0);
        assert(!satchel_session_connect(&root, "prepo:u", NULL));
        if (domain) assert(!satchel_session_control(root, 1, &object));
        for (unsigned cmd = 10100; cmd <= 10105; cmd++) assert(!broad_call(root, domain, 1, cmd, 0, satchel_uid, 16));
        assert(!broad_call(root, domain, 1, 10300, 0, NULL, 0) && !satchel_u32(out));
        assert(!broad_call(root, domain, 1, 40100, 0, NULL, 0) && out[0] == 1);
        assert(!broad_call(root, domain, 1, 10200, 0, NULL, 0));
        assert(!broad_call(root, domain, 1, 10300, 0, NULL, 0) && satchel_u32(out) == 1);
        uint8_t disabled = 0;
        assert(!broad_call(root, domain, 1, 40101, 0, &disabled, 1));
        assert(!broad_call(root, domain, 1, 10300, 0, NULL, 0) && !satchel_u32(out));
        assert(!broad_call(root, domain, 1, 40100, 0, NULL, 0) && !out[0]);
        assert(!broad_call(root, domain, 1, 10400, 0, NULL, 0)); uint64_t sid = satchel_u64(out); assert(sid);
        assert(!satchel_session_control(root, 2, &dup)); satchel_session_close(root);
        assert(!broad_call(dup, domain, 1, 10400, 0, NULL, 0) && satchel_u64(out) == sid);
        satchel_session_close(dup);
        assert(!satchel_session_connect(&root, "prepo:u", NULL));
        assert(!broad_call(root, 0, 1, 10300, 0, NULL, 0) && !satchel_u32(message + 32));
        satchel_sessions_reset(); assert(test_event_count() == 0);
    }
    // Copy allocation failure is a CMIF error and releases any earlier copies.
    satchel_sessions_reset();
    uint32_t root, object;
    assert(!satchel_session_connect(&root, "nifm:u", NULL));
    uint32_t general = port_child(root, 0, 4);
    uint32_t version = 1;
    assert(!broad_call(general, 0, 1, 4, 0, &version, 4));
    uint32_t request = satchel_u32(message + 12);
    broad_events(request, 0, 1, 2, 2, 0); // Two retained owners.
    uint32_t held[128]; unsigned count = 0;
    while ((held[count] = satchel_event_create(0))) count++;
    satchel_event_close(held[--count]); // One free slot, reply needs two copies.
    unsigned before = test_event_count();
    assert(broad_call(request, 0, 1, 2, 0, NULL, 0) == SATCHEL_SF_FULL);
    assert(!(satchel_u32(message + 4) >> 31) && test_event_count() == before);
    for (unsigned i = 0; i < count; i++) satchel_event_close(held[i]);
    assert(!satchel_session_control(request, 1, &object));
    make(0, 1, 0, NULL, 0); message[16] = 2;
    assert(!satchel_session_send(request, message, sizeof message, NULL));
    assert(!test_event_count());
    satchel_sessions_reset();
    puts("PASS: 45 round 6 command contracts in both transports, DLC bounds, real-copy wire headers, offline results, notification queues, per-object prepo state");
}
