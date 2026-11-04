// Synthetic reference contracts for both transport ownership modes.
static uint32_t fs_path_call(SystemObject object, unsigned command, const char *path, int pointer) {
    size_t size = strlen(path) + 1; assert(size <= sizeof buffer); memcpy(buffer, path, size);
    uint8_t args[16] = {0};
    make(command, object.domain, pointer ? 0 : 2, command == 8 ? args : NULL, command == 8 ? 16 : 0);
    if (pointer) {
        satchel_put32(message, 4 | (1u << 16));
        satchel_put32(message + 8, (uint32_t)size << 16); satchel_put32(message + 12, 0x12340000);
    } else {
        satchel_put32(message, 4 | (1u << 20)); satchel_put32(message + 8, (uint32_t)size);
    }
    if (object.domain) satchel_put32(message + (pointer ? 20 : 36), object.id);
    for (unsigned i = 0; i < sizeof round8_contracts / sizeof *round8_contracts; i++)
        if (!strcmp(round8_contracts[i].kind, command == 8 ? "fsp-srv" : "IFileSystem") && round8_contracts[i].command == command) round8_contracts[i].hits[object.domain]++;
    assert(!satchel_session_send(object.h, message, sizeof message, NULL));
    return satchel_u32(message + (object.domain ? 40 : 24));
}
static void test_round8(void) {
    for (int domain = 0; domain < 2; domain++) {
        satchel_sessions_reset();
        uint8_t args[40] = {0};
        uint8_t *out = message + (domain ? 48 : 32);
        SystemObject fs = system_root("fsp-srv", domain);
        assert(fs_path_call(fs, 8, "content:/absent.nca", 0) == 0x202);
        assert(fs_path_call(fs, 8, "content:/absent.nsp", 1) == 0x202);
        assert(system_call(fs, 8, 0, NULL, 0) == SATCHEL_IPC_INVALID);
        uint8_t attribute[72] = {0}; attribute[0] = 1;
        uint64_t program = satchel_program_id(); memcpy(attribute + 8, &program, 8);
        memcpy(attribute + 16, satchel_uid, 16); attribute[40] = 1;
        for (unsigned factory = 51; factory <= 53; factory += 2) {
            SystemObject volume = system_child(fs, factory, attribute, sizeof attribute);
            system_close(fs); // A child retains the mount after its parent closes.
            for (int pointer = 0; pointer < 2; pointer++) {
                assert(!fs_path_call(volume, 11, "/", pointer) && satchel_u64(out) > (35ull << 30));
                assert(!fs_path_call(volume, 12, "/", pointer) && satchel_u64(out) > (35ull << 30));
                assert(!fs_path_call(volume, 14, "/file", pointer));
                assert(satchel_u64(out + 8) == 1700000000 && satchel_u64(out + 16) == 1600000000 && satchel_u64(out + 24) == 1);
                assert(fs_path_call(volume, 14, "/absent", pointer) == 0x202);
                assert(fs_path_call(volume, 14, "/../outside", pointer) == 0x202);
                assert(fs_path_call(volume, 14, "/link/etc", pointer) == 0x202);
                assert(fs_path_call(volume, 14, "relative", pointer) == ((6001u << 9) | 2u));
            }
            assert(system_call(volume, 14, 0, NULL, 0) == SATCHEL_IPC_INVALID);
            system_close(volume); fs = system_root("fsp-srv", domain);
        }
        uint8_t device[72] = {0}; device[0] = 1; device[40] = 3;
        SystemObject volume = system_child(fs, 51, device, sizeof device);
        assert(!fs_path_call(volume, 12, "/", 1) && satchel_u64(out) > (35ull << 30));
        system_close(volume);
        device[40] = 4; device[0] = 3;
        assert(system_call(fs, 51, 0, device, sizeof device) == 0x7d402);
        device[40] = 5; device[0] = 1;
        assert(system_call(fs, 51, 0, device, sizeof device) == 0x7d402);
        device[40] = 3; device[32] = 1;
        assert(system_call(fs, 51, 0, device, sizeof device) == 0x7d402);
        attribute[16] ^= 1; assert(system_call(fs, 51, 0, attribute, sizeof attribute) == 0x7d402);
        attribute[16] ^= 1; attribute[8] ^= 1; assert(system_call(fs, 53, 0, attribute, sizeof attribute) == 0x7d402);
        attribute[8] ^= 1; attribute[40] = 0; assert(system_call(fs, 51, 0, attribute, sizeof attribute) == 0x7d402);
        assert(system_call(fs, 51, 0, NULL, 0) == SATCHEL_IPC_INVALID);
        system_close(fs);
        SystemObject hid = system_root("hid", domain);
        const unsigned stubs[] = {60,61,63,64,92,134,207,209,210,300,301,302,303,1002,1004};
        for (unsigned i = 0; i < sizeof stubs / sizeof *stubs; i++)
            assert(!system_call(hid, stubs[i], 0, args, sizeof args));
        assert(!system_call(hid, 208, 0, args, sizeof args) && satchel_u64(out) == 0);
        satchel_put32(args + 8, 0x3f800000); satchel_put32(args + 12, 0x40000000);
        assert(!system_call(hid, 73, 0, args, 24));
        assert(!system_call(hid, 74, 0, args, 16));
        assert(satchel_u32(out) == 0x3f800000 && satchel_u32(out + 4) == 0x40000000);
        assert(!system_call(hid, 75, 0, args, 16));
        assert(!system_call(hid, 74, 0, args, 16) && !satchel_u64(out));
        satchel_put32(args + 8, 3);
        assert(!system_call(hid, 76, 0, args, 20));
        assert(!system_call(hid, 77, 0, args, 16) && satchel_u32(out) == 3);
        assert(!system_call(hid, 78, 0, args, 16));
        assert(!system_call(hid, 77, 0, args, 16) && !satchel_u32(out));
        const unsigned setters[] = {84,132,401}, getters[] = {85,131,400};
        for (unsigned i = 0; i < 3; i++) {
            satchel_put32(args, 1); assert(!system_call(hid, setters[i], 0, args, sizeof args));
            assert(!system_call(hid, getters[i], 0, args, sizeof args) && out[0] == 1);
            satchel_put32(args, 0); assert(!system_call(hid, setters[i], 0, args, sizeof args));
            assert(!system_call(hid, getters[i], 0, args, sizeof args) && out[0] == 0);
        }
        satchel_put32(args, 0x3f000000);
        assert(!system_call(hid, 308, 0, args, 12));
        assert(!system_call(hid, 309, 0, args, 8) && satchel_u32(out) == 0x3f000000);
        assert(system_call(hid, 73, 0, NULL, 0) == SATCHEL_IPC_INVALID);
        assert(system_call(hid, 1004, 0, NULL, 0) == SATCHEL_IPC_INVALID);
        uint32_t duplicate; assert(!satchel_session_control(hid.h, 2, &duplicate));
        SystemObject alias = {duplicate, hid.id, domain};
        assert(!system_call(alias, 309, 0, args, 8) && satchel_u32(out) == 0x3f000000);
        satchel_session_close(duplicate);
        SystemObject independent = system_root("hid", domain);
        assert(!system_call(independent, 309, 0, args, 8) && !satchel_u32(out));
        system_close(independent); system_close(hid);
        SystemObject roots[4]; const char *ports[] = {"time:a", "time:r", "time:s", "time:u"};
        for (unsigned i = 0; i < 4; i++) roots[i] = system_root(ports[i], domain);
        for (unsigned i = 0; i < 4; i++) {
            uint32_t result = system_call(roots[i], 51, 0, NULL, 0);
            assert(result == (i == 2 ? SATCHEL_TIME_NOT_IMPLEMENTED : 0));
            if (i != 2) assert(satchel_u64(out) == 1700000001);
            assert(!system_call(roots[i], 201, 0, NULL, 0));
            if (i == 3) continue;
            result = system_call(roots[i], 50, 0, args, 8);
            assert(result == (i == 2 ? SATCHEL_TIME_NOT_IMPLEMENTED : i == 0 ? SATCHEL_TIME_PERMISSION : 0));
            result = system_call(roots[i], 102, 0, NULL, 0);
            assert(result == (i == 2 ? SATCHEL_TIME_NOT_IMPLEMENTED : 0));
            if (i != 2) assert(satchel_u32(out) == 2019);
            assert(!system_call(roots[i], 100, 0, NULL, 0) && !out[0]);
            if (i) assert(system_call(roots[i], 101, 0, args, 1) == SATCHEL_TIME_PERMISSION);
            SystemObject zone = system_child(roots[i], 3, NULL, 0);
            assert(!system_call(zone, 2, 0, NULL, 0) && !satchel_u32(out));
            memset(args, 0, sizeof args); memcpy(args, "UTC", 3);
            assert(system_call(zone, 1, 0, args, 36) == (i == 0 ? ((989u << 9) | 116u) : SATCHEL_TIME_PERMISSION));
            uint32_t zero = 0;
            assert(system_call(zone, 3, 2, &zero, 4) == (i == 2 ? SATCHEL_TIME_NOT_IMPLEMENTED : 0));
            if (i != 2) assert(satchel_u32(out) == 1 && !strcmp((char *)buffer, "UTC"));
            system_close(zone);
        }
        // Privileged factories retain their exact clock permissions. No factory
        // marks the uninitialized network clock usable.
        for (unsigned i = 0; i < 3; i++) {
            for (unsigned command = 0; command < 6; command++) {
                if (command == 3) continue;
                SystemObject clock = system_child(roots[i], command, NULL, 0);
                int network = command == 1 || command == 5, steady = command == 2;
                assert(system_call(clock, 0, 0, NULL, 0) == (network ? SATCHEL_TIME_UNINITIALIZED : 0));
                if (steady) {
                    int64_t offset = 3000000000ll;
                    assert(system_call(clock, 3, 0, &offset, 8) == (i == 1 ? 0 : SATCHEL_TIME_PERMISSION));
                    assert(!system_call(clock, 2, 0, NULL, 0));
                    if (i == 1) assert(satchel_u64(out) == (uint64_t)offset);
                    offset = 0; assert(system_call(clock, 3, 0, &offset, 8) == (i == 1 ? 0 : SATCHEL_TIME_PERMISSION));
                    offset = -1000000000ll;
                    assert(system_call(clock, 201, 0, &offset, 8) == (i == 1 ? 0 : SATCHEL_TIME_PERMISSION));
                    assert(!system_call(clock, 200, 0, NULL, 0));
                    if (i == 1) assert(satchel_u64(out) == (uint64_t)offset);
                    offset = 0; assert(system_call(clock, 201, 0, &offset, 8) == (i == 1 ? 0 : SATCHEL_TIME_PERMISSION));
                    assert(!system_call(clock, 101, 0, NULL, 0) && !out[0]);
                    assert(!system_call(clock, 102, 0, NULL, 0) && !satchel_u32(out));
                } else {
                    uint8_t context[32]; satchel_clock_context(context);
                    int64_t desired = 1800000000;
                    uint32_t expected = network ? (i == 2 ? SATCHEL_TIME_UNINITIALIZED : SATCHEL_TIME_PERMISSION) : i == 0 ? (command == 0 ? SATCHEL_TIME_NOT_IMPLEMENTED : 0) : SATCHEL_TIME_PERMISSION;
                    assert(system_call(clock, 1, 0, &desired, 8) == expected);
                    if (!network && i == 0 && command == 4) {
                        int64_t sdk; assert(!satchel_clock_now(&sdk) && sdk == desired);
                    }
                    assert(system_call(clock, 2, 0, NULL, 0) == (network ? SATCHEL_TIME_UNINITIALIZED : 0));
                    assert(system_call(clock, 3, 0, context, 32) == expected);
                }
                system_close(clock);
            }
        }
        args[0] = 1; assert(!system_call(roots[0], 101, 0, args, 1));
        for (unsigned i = 0; i < 4; i++) {
            assert(!system_call(roots[i], 100, 0, NULL, 0) && out[0] == 1);
            assert(!system_call(roots[i], 201, 0, NULL, 0) && satchel_u64(out) == 123);
            assert(!memcmp(out + 8, "test-steady-0001", 16));
        }
        args[0] = 0; assert(!system_call(roots[0], 101, 0, args, 1));
        for (unsigned i = 0; i < 4; i++) system_close(roots[i]);
        satchel_sessions_reset();
        SystemObject reset = system_root("time:a", domain);
        assert(!system_call(reset, 100, 0, NULL, 0) && out[0] == 0);
        satchel_sessions_reset(); assert(!test_event_count());
    }
    for (unsigned i = 0; i < sizeof round8_contracts / sizeof *round8_contracts; i++)
        assert(round8_contracts[i].hits[0] && round8_contracts[i].hits[1]);
    puts("PASS: every round 8 contract in both transports, state, permissions, truncation, duplicate ownership and reset");
}
