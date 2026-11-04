// Round 4 commands through owned handles and domain object IDs, using synthetic clocks.
static uint32_t port_request(uint32_t handle, int domain, uint32_t object, uint32_t cmd,
                             const void *args, size_t n) {
    make(cmd, domain, 0, args, n);
    if (domain) satchel_put32(message + 20, object);
    assert(!satchel_session_send(handle, message, sizeof message, NULL));
    return satchel_u32(message + (domain ? 40 : 24));
}
static uint32_t port_child(uint32_t handle, int domain, uint32_t command) {
    uint64_t zero = 0;
    assert(!port_request(handle, domain, 1, command, &zero, 8));
    if (domain) {
        assert(satchel_u32(message + 16) == 1 && !(satchel_u32(message + 4) >> 31));
        return satchel_u32(message + 48);
    }
    assert(satchel_u32(message + 4) >> 31 && satchel_u32(message + 8) == 32);
    return satchel_u32(message + 12);
}
static void *session_race(void *unused) {
    (void)unused;
    for (unsigned i = 0; i < 500; i++) {
        uint32_t h, d, object;
        assert(!satchel_session_connect(&h, "time:u", NULL));
        assert(!satchel_session_control(h, 1, &object) && object == 1);
        assert(!satchel_session_control(h, 2, &d));
        satchel_session_close(h); satchel_session_close(d);
    }
    return NULL;
}
static void test_ports(void) {
    satchel_sessions_reset();
    for (int domain = 0; domain < 2; domain++) {
        uint32_t root, object;
        assert(!satchel_session_connect(&root, "time:u", NULL));
        if (domain) assert(!satchel_session_control(root, 1, &object));
        uint8_t *out = message + (domain ? 48 : 32);
        const unsigned roots[] = {0, 1, 2, 4, 5};
        for (unsigned i = 0; i < 5; i++) {
            uint32_t child = port_child(root, domain, roots[i]);
            uint32_t h = domain ? root : child, id = domain ? child : 1;
            int network = roots[i] == 1 || roots[i] == 5;
            if (roots[i] == 2) {
                assert(!port_request(h, domain, id, 0, NULL, 0));
                assert(satchel_u64(out) == 123 && !memcmp(out + 8, "test-steady-0001", 16));
                unsigned zeros[] = {2, 101, 102, 200};
                for (unsigned k = 0; k < 4; k++) {
                    assert(!port_request(h, domain, id, zeros[k], NULL, 0));
                    assert(!satchel_u64(out));
                }
                unsigned denied[] = {3, 201};
                for (unsigned k = 0; k < 2; k++) assert(port_request(h, domain, id, denied[k], NULL, 0) == SATCHEL_TIME_PERMISSION);
            } else {
                assert(port_request(h, domain, id, 0, NULL, 0) == (network ? SATCHEL_TIME_UNINITIALIZED : 0));
                if (!network) assert(satchel_u64(out) == 1700000000);
                assert(port_request(h, domain, id, 2, NULL, 0) == (network ? SATCHEL_TIME_UNINITIALIZED : 0));
                if (!network) {
                    assert(satchel_u64(out) + satchel_u64(out + 8) == 1700000000);
                    assert(!memcmp(out + 16, "test-steady-0001", 16));
                }
                assert(port_request(h, domain, id, 1, NULL, 0) == SATCHEL_TIME_PERMISSION);
                assert(port_request(h, domain, id, 3, NULL, 0) == SATCHEL_TIME_PERMISSION);
            }
            assert(port_request(h, domain, id, 987654, NULL, 0) == SATCHEL_IPC_UNKNOWN);
            if (!domain) satchel_session_close(child);
            else {
                make(0, 1, 0, NULL, 0); message[16] = 2; satchel_put32(message + 20, child);
                assert(!satchel_session_send(root, message, sizeof message, NULL));
                make(0, 1, 0, NULL, 0); satchel_put32(message + 20, child);
                assert(satchel_session_send(root, message, sizeof message, NULL) == SATCHEL_IPC_INVALID);
            }
        }
        assert(!port_request(root, domain, 1, 100, NULL, 0) && !out[0]);
        assert(!port_request(root, domain, 1, 200, NULL, 0) && !out[0]);
        assert(!port_request(root, domain, 1, 102, NULL, 0) && satchel_u32(out) == 2019);
        assert(port_request(root, domain, 1, 50, NULL, 0) == SATCHEL_TIME_PERMISSION);
        assert(port_request(root, domain, 1, 101, NULL, 0) == SATCHEL_TIME_PERMISSION);
        satchel_session_close(root);
        assert(!satchel_session_connect(&root, "nifm:u", NULL));
        if (domain) assert(!satchel_session_control(root, 1, &object));
        for (unsigned command = 4; command <= 5; command++) {
            uint32_t child = port_child(root, domain, command);
            uint32_t h = domain ? root : child, id = domain ? child : 1;
            const unsigned offline[] = {5, 12, 15, 18};
            for (unsigned i = 0; i < 4; i++) {
                memset(buffer, 0xcc, sizeof buffer);
                assert(port_request(h, domain, id, offline[i], NULL, 0) == SATCHEL_NO_INTERNET);
                for (unsigned b = 0; b < sizeof buffer; b++) assert(buffer[b] == 0xcc);
                assert(!satchel_u64(out)); // Error replies expose no invented state or IP address.
            }
            if (!domain) satchel_session_close(child);
        }
        // A domain declares the exact command argument length, excluding HIPC padding.
        if (domain) assert(port_request(root, 1, 1, 5, NULL, 0) == SATCHEL_IPC_INVALID);
        satchel_session_close(root);
    }
    // Duplicated domains share objects and keep them alive after the original handle closes.
    uint32_t root, duplicate, object;
    assert(!satchel_session_connect(&root, "time:u", NULL));
    assert(!satchel_session_control(root, 2, &duplicate));
    assert(!satchel_session_control(root, 1, &object));
    uint32_t child = port_child(root, 1, 0);
    satchel_session_close(root);
    assert(!port_request(duplicate, 1, child, 0, NULL, 0));
    assert(satchel_u64(message + 48) == 1700000000);
    // Close only the root domain object. Existing children remain usable.
    make(0, 1, 0, NULL, 0); message[16] = 2;
    assert(!satchel_session_send(duplicate, message, sizeof message, NULL));
    assert(!port_request(duplicate, 1, child, 2, NULL, 0));
    satchel_session_close(duplicate);
    for (unsigned i = 0; i < SATCHEL_SF_MAX; i++) assert(!satchel_domains[i].refs);
    // Capacity is an error, never a host abort or successful reply without an object.
    assert(!satchel_session_connect(&root, "time:u", NULL));
    assert(!satchel_session_control(root, 1, &object));
    for (unsigned i = 1; i < SATCHEL_DOMAIN_MAX; i++) assert(port_child(root, 1, 0) == i + 1);
    assert(port_request(root, 1, 1, 0, NULL, 0) == SATCHEL_SF_FULL);
    satchel_session_close(root);
    uint32_t handles[SATCHEL_SF_MAX];
    for (unsigned i = 0; i < SATCHEL_SF_MAX; i++) assert(!satchel_session_connect(&handles[i], "time:u", NULL));
    assert(satchel_session_connect(&root, "set", NULL) == SATCHEL_SF_FULL);
    assert(port_request(handles[0], 0, 1, 0, NULL, 0) == SATCHEL_SF_FULL);
    for (unsigned i = 0; i < SATCHEL_SF_MAX; i++) satchel_session_close(handles[i]);
    pthread_t threads[4];
    for (unsigned i = 0; i < 4; i++) assert(!pthread_create(&threads[i], NULL, session_race, NULL));
    for (unsigned i = 0; i < 4; i++) pthread_join(threads[i], NULL);
    for (unsigned i = 0; i < SATCHEL_SF_MAX; i++) assert(!satchel_domains[i].refs);
    assert(!satchel_session_connect(&root, "time:u", NULL)); satchel_sessions_reset();
    assert(satchel_session_send(root, message, sizeof message, NULL) == SATCHEL_SF_BAD_HANDLE);
    puts("PASS: 27 new command contracts, moved handles, domain children, shared duplicates, close, capacity, reset, concurrent ownership");
}

// Round 5: the profile returned from acc:u0 uses the host's existing local user.
static void test_profiles(void) {
    satchel_sessions_reset();
    for (int domain = 0; domain < 2; domain++) {
        uint32_t root, object;
        assert(!satchel_session_connect(&root, "acc:u0", NULL));
        if (domain) assert(!satchel_session_control(root, 1, &object));
        uint64_t bad[2] = {0};
        assert(port_request(root, domain, 1, 5, bad, sizeof bad) == SATCHEL_ACC_NULL);
        bad[0] = 42;
        assert(port_request(root, domain, 1, 5, bad, sizeof bad) == SATCHEL_ACC_NOT_FOUND);
        if (domain) assert(port_request(root, domain, 1, 5, NULL, 0) == SATCHEL_IPC_INVALID);
        assert(!port_request(root, domain, 1, 5, satchel_uid, sizeof satchel_uid));
        uint32_t child = satchel_u32(message + (domain ? 48 : 12));
        uint32_t handle = domain ? root : child, id = domain ? child : 1;
        uint8_t *out = message + (domain ? 48 : 32);
        unsetenv("DM_NICK");
        assert(!port_request(handle, domain, id, 1, NULL, 0));
        assert(!memcmp(out, satchel_uid, 16) && !satchel_u64(out + 16));
        assert(!memcmp(out + 24, "Player", 6));
        for (unsigned i = 30; i < 56; i++) assert(!out[i]);
        setenv("DM_NICK", "Synthetic player", 1);
        assert(!port_request(handle, domain, id, 1, NULL, 0));
        assert(!memcmp(out + 24, "Synthetic player", 16));
        unsetenv("DM_NICK");
        make(1, domain, 0, NULL, 0);
        if (domain) satchel_put32(message + 20, id);
        assert(satchel_session_send(handle, message, 64, NULL) == SATCHEL_IPC_INVALID);
        memset(buffer, 0xcc, sizeof buffer);
        make(0, domain, 1, NULL, 0);
        if (domain) satchel_put32(message + 20, id);
        assert(!satchel_session_send(handle, message, sizeof message, NULL));
        assert(!satchel_u32(message + (domain ? 40 : 24)));
        assert(!memcmp(out, satchel_uid, 16) && !memcmp(out + 24, "Player", 6));
        for (unsigned i = 0; i < 128; i++) assert(buffer[i] == (i == 4 || i == 8 ? 1 : 0));
        assert(buffer[128] == 0xcc);
        make(0, domain, 1, NULL, 0);
        if (domain) satchel_put32(message + 20, id);
        size_t end = 8 + (satchel_u32(message + 4) & 0x3ff) * 4;
        uint64_t short_buffer = 0x12340000ull | (127ull << 48); memcpy(message + end, &short_buffer, 8);
        memset(buffer, 0xcc, sizeof buffer);
        assert(!satchel_session_send(handle, message, sizeof message, NULL));
        assert(satchel_u32(message + (domain ? 40 : 24)) == SATCHEL_ACC_BUFFER);
        for (unsigned i = 0; i < sizeof buffer; i++) assert(buffer[i] == 0xcc);
        assert(port_request(handle, domain, id, 0, NULL, 0) == SATCHEL_ACC_BUFFER);
        assert(port_request(handle, domain, id, 11, NULL, 0) == SATCHEL_IPC_UNKNOWN);
        uint32_t duplicate;
        assert(!satchel_session_control(handle, 2, &duplicate));
        satchel_session_close(handle);
        assert(!port_request(duplicate, domain, id, 1, NULL, 0));
        satchel_session_close(duplicate);
        if (!domain) satchel_session_close(root);
    }
    satchel_sessions_reset();
    puts("PASS: 3 profile contracts in session and domain mode, UID errors, 56-byte replies, nickname, bounded UserData, no image fabrication, duplicate lifetime");
}
