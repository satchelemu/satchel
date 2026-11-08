// Round 7 reference contracts. Synthetic state and storage only.
#include "round7-contracts.inc"
#include "round8-contracts.inc"
#include "round11-contracts.inc"
#include "round12-contracts.inc"
typedef struct { uint32_t h, id; int domain; } SystemObject;
static uint32_t system_call(SystemObject o, unsigned cmd, unsigned descriptor, const void *args, size_t n) {
    int slot = satchel_session_slot(o.h); assert(slot >= 0);
    const char *kind = satchel_domains[satchel_sessions[slot].tree].objects[o.id - 1].kind;
    for (unsigned i = 0; i < sizeof system_contracts / sizeof *system_contracts; i++)
        if (system_contracts[i].command == cmd && !strcmp(system_contracts[i].kind, kind)) system_contracts[i].hits[o.domain]++;
    for (unsigned i = 0; i < sizeof round8_contracts / sizeof *round8_contracts; i++)
        if (round8_contracts[i].command == cmd && !strcmp(round8_contracts[i].kind, kind)) round8_contracts[i].hits[o.domain]++;
    for (unsigned i = 0; i < sizeof round11_contracts / sizeof *round11_contracts; i++)
        if (round11_contracts[i].command == cmd && !strcmp(round11_contracts[i].kind, kind)) round11_contracts[i].hits[o.domain]++;
    for (unsigned i = 0; i < sizeof round12_contracts / sizeof *round12_contracts; i++)
        if (round12_contracts[i].command == cmd && !strcmp(round12_contracts[i].kind, kind)) round12_contracts[i].hits[o.domain]++;
    return broad_call(o.h, o.domain, o.id, cmd, descriptor, args, n);
}
static SystemObject system_root(const char *name, int domain) {
    SystemObject o = {.id = 1, .domain = domain};
    assert(!satchel_session_connect(&o.h, name, NULL));
    if (domain) assert(!satchel_session_control(o.h, 1, &o.id));
    return o;
}
static SystemObject system_child(SystemObject parent, unsigned cmd, const void *args, size_t n) {
    assert(!system_call(parent, cmd, 0, args, n));
    uint32_t child = satchel_u32(message + (parent.domain ? 48 : 12)); assert(child);
    return (SystemObject){parent.domain ? parent.h : child, parent.domain ? child : 1, parent.domain};
}
static void system_close(SystemObject o) {
    if (!o.domain) { satchel_session_close(o.h); return; }
    make(0, 1, 0, NULL, 0); message[16] = 2; satchel_put32(message + 20, o.id);
    assert(!satchel_session_send(o.h, message, sizeof message, NULL));
}
static uint32_t system_event(SystemObject o, unsigned cmd, int signaled) {
    assert(!system_call(o, cmd, 0, NULL, 0));
    assert(satchel_u32(message + 8) == 2);
    uint32_t event = satchel_u32(message + 12); assert(test_event_signaled(event) == signaled); return event;
}
static uint32_t system_push(SystemObject applet, unsigned cmd, SystemObject storage) {
    for (unsigned i = 0; i < sizeof system_contracts / sizeof *system_contracts; i++)
        if (system_contracts[i].command == cmd && !strcmp(system_contracts[i].kind, "ILibraryAppletAccessor")) system_contracts[i].hits[applet.domain]++;
    if (applet.domain) {
        size_t end = make(cmd, 1, 0, NULL, 0);
        message[17] = 1; satchel_put32(message + 20, applet.id);
        satchel_put32(message + 48, storage.id); // Domain object IDs follow the CMIF data.
        (void)end;
    } else {
        make(cmd, 0, 0, NULL, 0);
        // One moved object handle ends at offset 16, so the CMIF offset is unchanged.
        satchel_put32(message + 4, (satchel_u32(message + 4) & 0x3ff) - 2 + (1u << 31));
        satchel_put32(message + 8, 1u << 5); satchel_put32(message + 12, storage.h);
    }
    assert(!satchel_session_send(applet.h, message, sizeof message, NULL));
    return satchel_u32(message + (applet.domain ? 40 : 24));
}
static void system_write(SystemObject accessor, const uint8_t *bytes, size_t count) {
    for (unsigned i = 0; i < sizeof system_contracts / sizeof *system_contracts; i++)
        if (system_contracts[i].command == 10 && !strcmp(system_contracts[i].kind, "IStorageAccessor")) system_contracts[i].hits[accessor.domain]++;
    assert(count <= sizeof buffer); memcpy(buffer, bytes, count);
    uint64_t offset = 0; make(10, accessor.domain, 2, &offset, 8);
    // Turn the output map-alias descriptor into an input descriptor.
    satchel_put32(message, 4 | (1u << 20)); satchel_put32(message + 8, (uint32_t)count);
    if (accessor.domain) satchel_put32(message + 36, accessor.id);
    assert(!satchel_session_send(accessor.h, message, sizeof message, NULL));
    assert(!satchel_u32(message + (accessor.domain ? 40 : 24)));
}
static void test_system(void) {
    for (int domain = 0; domain < 2; domain++) {
        satchel_sessions_reset();
        uint64_t zero = 0, one = 1; uint32_t value = 7;
        uint8_t *out = message + (domain ? 48 : 32);
        SystemObject root = system_root("appletOE", domain);
        SystemObject proxy = system_child(root, 0, &zero, 8);
        SystemObject functions = system_child(proxy, 20, NULL, 0);
        SystemObject self = system_child(proxy, 1, NULL, 0);
        SystemObject common = system_child(proxy, 0, NULL, 0);
        SystemObject creator = system_child(proxy, 11, NULL, 0);
        assert(!system_call(functions, 22, 0, &value, 4));
        assert(!system_call(functions, 40, 0, NULL, 0) && out[0] == 1);
        const unsigned flags[] = {60,90};
        for (unsigned i = 0; i < 2; i++) assert(!system_call(functions, flags[i], 0, &one, 1));
        assert(!system_call(functions, 65, 0, NULL, 0) && !out[0]);
        assert(!system_call(functions, 121, 0, NULL, 0));
        assert(system_call(functions, 141, 0, NULL, 0) == SATCHEL_AM_UNAVAILABLE);
        const unsigned events[] = {130,140,150,160,210}; uint32_t event_handles[5];
        for (unsigned i = 0; i < 5; i++) event_handles[i] = system_event(functions, events[i], 0);
        for (unsigned i = 1; i < 5; i++) assert(!test_event_same(event_handles[0], event_handles[i]));
        for (unsigned i = 0; i < 5; i++) satchel_event_close(event_handles[i]);
        assert(system_call(functions, 1, 0, &one, 4) == SATCHEL_AM_UNAVAILABLE);
        value = 99; assert(system_call(functions, 1, 0, &value, 4) == SATCHEL_AM_OBJECT);
        value = 2; SystemObject storage = system_child(functions, 1, &value, 4);
        SystemObject accessor = system_child(storage, 0, NULL, 0);
        system_close(storage); // Accessors retain storage after its parent closes.
        assert(!system_call(accessor, 0, 0, NULL, 0) && satchel_u64(out) == 0x88);
        memset(buffer, 0xcc, sizeof buffer);
        assert(!system_call(accessor, 11, 2, &zero, 8));
        assert(satchel_u32(buffer) == 0xc79497ca && satchel_u32(buffer + 4) == 1 && !memcmp(buffer + 8, satchel_uid, 16));
        for (unsigned i = 24; i < 0x88; i++) assert(!buffer[i]);
        assert(buffer[0x88] == 0xcc);
        uint64_t offset = 0x89; assert(system_call(accessor, 11, 2, &offset, 8) == SATCHEL_AM_BOUNDS);
        offset = 0x88; assert(!system_call(accessor, 11, 2, &offset, 8));
        system_close(accessor);
        assert(!system_call(self, 1, 0, NULL, 0)); assert(!system_call(self, 2, 0, NULL, 0));
        assert(system_call(self, 4, 0, NULL, 0) == ((512u << 9) | 128u));
        assert(!system_call(self, 3, 0, NULL, 0)); assert(!system_call(self, 3, 0, NULL, 0));
        assert(!system_call(self, 4, 0, NULL, 0)); assert(!system_call(self, 4, 0, NULL, 0));
        assert(system_call(self, 4, 0, NULL, 0) == ((512u << 9) | 128u));
        const unsigned settings[] = {14,50};
        for (unsigned i = 0; i < 2; i++) assert(!system_call(self, settings[i], 0, &one, 4));
        assert(!system_call(self, 65, 0, NULL, 0));
        assert(!system_call(self, 90, 0, NULL, 0) && !satchel_u64(out));
        uint32_t launchable = system_event(self, 9, 1), suspended = system_event(self, 91, 1);
        assert(!test_event_same(launchable, suspended));
        system_close(self); assert(test_event_signaled(launchable) == 1);
        satchel_event_close(launchable); satchel_event_close(suspended);
        assert(!system_call(common, 8, 0, NULL, 0) && !out[0]);
        assert(!system_call(common, 10, 0, NULL, 0)); satchel_event_close(system_event(common, 13, 0));
        assert(!system_call(common, 50, 0, NULL, 0) && !out[0]);
        assert(!system_call(common, 51, 0, &one, 1)); assert(!system_call(common, 50, 0, NULL, 0) && out[0]);
        assert(!system_call(common, 52, 0, &one, 1));
        assert(!system_call(common, 54, 0, NULL, 0)); assert(!system_call(common, 50, 0, NULL, 0) && !out[0]);
        assert(!system_call(common, 53, 0, NULL, 0)); assert(!system_call(common, 50, 0, NULL, 0) && out[0]);
        assert(!system_call(common, 62, 0, NULL, 0) && !satchel_u32(out));
        assert(!system_call(common, 300, 0, NULL, 0) && !out[0]);
        assert(!system_call(common, 900, 0, NULL, 0));
        uint64_t size = 64;
        assert(system_call(creator, 10, 0, &zero, 8) == SATCHEL_AM_OBJECT);
        uint64_t too_large = 65537; assert(system_call(creator, 10, 0, &too_large, 8) == SATCHEL_AM_OBJECT);
        for (unsigned applet_id = 0xe; applet_id <= 0x11; applet_id += 3) {
            uint32_t args[4] = {applet_id,0,0,0};
            SystemObject applet = system_child(creator, applet_id == 0xe ? 0 : 3, args, applet_id == 0xe ? 8 : 16);
            uint32_t changed = system_event(applet, 0, 0), output_event = system_event(applet, 105, 0);
            satchel_event_close(system_event(applet, 106, 0));
            assert(system_call(applet, 30, 0, NULL, 0) == SATCHEL_AM_UNAVAILABLE);
            assert(!system_call(applet, 60, 0, NULL, 0));
            assert(system_call(applet, 104, 0, NULL, 0) == SATCHEL_AM_UNAVAILABLE);
            for (unsigned i = 0; i < 2; i++) {
                storage = system_child(creator, 10, &size, 8);
                accessor = system_child(storage, 0, NULL, 0);
                uint8_t synthetic[] = {0x41,0x42,0x43,0x44}; system_write(accessor, synthetic, sizeof synthetic);
                memset(buffer, 0xcc, sizeof buffer);
                assert(!system_call(accessor, 11, 1, &zero, 8) && !memcmp(buffer, synthetic, sizeof synthetic) && buffer[64] == 0xcc);
                system_close(accessor);
                assert(!system_push(applet, i ? 103 : 100, storage));
                if (domain) system_close(storage);
                else assert(satchel_session_slot(storage.h) == -1);
            }
            assert(!system_call(applet, 10, 0, NULL, 0));
            assert(test_event_signaled(changed) == 1);
            assert(!system_call(applet, 20, 0, NULL, 0)); assert(!system_call(applet, 30, 0, NULL, 0));
            assert(system_call(applet, 110, 0, NULL, 0) == ((999u << 9) | 128u));
            assert(system_call(applet, 150, 0, NULL, 0) == ((999u << 9) | 128u));
            if (applet_id == 0x11) {
                assert(test_event_signaled(output_event) == 1);
                storage = system_child(applet, 101, NULL, 0); accessor = system_child(storage, 0, NULL, 0);
                assert(test_event_signaled(output_event) == 0);
                assert(!system_call(accessor, 0, 0, NULL, 0) && satchel_u64(out) == 0x7d8);
                assert(!system_call(accessor, 11, 2, &zero, 8) && satchel_u32(buffer) == 1);
                for (unsigned i = 4; i < sizeof buffer; i++) assert(!buffer[i]);
                system_close(storage); system_close(accessor);
            }
            assert(system_call(applet, 101, 0, NULL, 0) == SATCHEL_AM_UNAVAILABLE);
            satchel_event_close(changed); satchel_event_close(output_event); system_close(applet);
        }
        uint32_t unsupported[2] = {0x13,0}; assert(system_call(creator, 0, 0, unsupported, 8) == ((35u << 9) | 128u));
        unsupported[0] = 0x11; unsupported[1] = 1;
        assert(system_call(creator, 0, 0, unsupported, 8) == ((35u << 9) | 128u));
        system_close(creator); system_close(common); system_close(functions); system_close(proxy); satchel_session_close(root.h);
        for (unsigned c = 200; c <= 201; c++) {
            root = system_root("appletAE", domain); proxy = system_child(root, c, &zero, 8);
            const unsigned commands[] = {0,1,11};
            for (unsigned i = 0; i < 3; i++) system_close(system_child(proxy, commands[i], NULL, 0));
            system_close(proxy); satchel_session_close(root.h);
        }
        root = system_root("vi:u", domain); value = 1;
        assert(system_call(root, 0, 0, &value, 4) == ((5u << 9) | 114u));
        SystemObject display = system_child(root, 0, &zero, 4);
        uint64_t enable[] = {1,0}; assert(!system_call(display, 1101, 0, enable, 16));
        assert(!system_call(display, 1102, 0, &zero, 8) && satchel_u64(out) == 1280 && satchel_u64(out + 8) == 720);
        for (value = 0; value <= 5; value++) {
            uint32_t result = system_call(display, 2102, 0, &value, 4);
            assert(result == (value > 4 ? ((1u << 9) | 114u) : value == 2 || value == 4 ? 0 : ((6u << 9) | 114u)));
            if (!result) assert(satchel_u64(out) == (value == 2 ? 1u : 4u));
        }
        system_close(display); satchel_session_close(root.h);
        root = system_root("audren:u", domain);
        uint64_t audio_args[2] = {0,0};
        for (unsigned factory = 2; factory <= 4; factory += 2) {
            SystemObject device = system_child(root, factory, audio_args, factory == 2 ? 8 : 16);
            for (unsigned c = 15; c <= 20; c++) assert(system_call(device, c, 0, &one, c < 19 ? 8 : c == 19 ? 1 : 0) == ((513u << 9) | 153u));
            system_close(device);
        }
        satchel_session_close(root.h);
        root = system_root("pctl", domain);
        for (unsigned c = 0; c < 2; c++) {
            SystemObject control = system_child(root, c, &zero, 8);
            assert(!system_call(control, 1013, 0, NULL, 0)); assert(!system_call(control, 1064, 0, NULL, 0));
            assert(!system_call(control, 1031, 0, NULL, 0) && !out[0]);
            assert(!system_call(control, 1065, 0, NULL, 0) && out[0] == 1);
            system_close(control);
        }
        satchel_session_close(root.h);
        root = system_root("time:u", domain); SystemObject zone = system_child(root, 3, NULL, 0);
        assert(system_call(zone, 1, 0, NULL, 0) == SATCHEL_TIME_PERMISSION);
        assert(!system_call(zone, 2, 0, NULL, 0) && !satchel_u32(out));
        memset(buffer, 0xcc, sizeof buffer);
        assert(!system_call(zone, 3, 2, &zero, 4) && satchel_u32(out) == 1 && !strcmp((char *)buffer, "UTC") && buffer[36] == 0xcc);
        assert(!system_call(zone, 3, 2, &one, 4) && !satchel_u32(out));
        system_close(zone); satchel_session_close(root.h);
        assert(test_event_count() == 0);
        satchel_sessions_reset();
    }
    // Failed output allocation must leave a cancellation queued for retry.
    for (int domain = 0; domain < 2; domain++) {
        satchel_sessions_reset(); uint64_t zero = 0, size = 1;
        SystemObject root = system_root("appletOE", domain);
        SystemObject proxy = system_child(root, 0, &zero, 8);
        SystemObject creator = system_child(proxy, 11, NULL, 0);
        uint32_t args[] = {0x11,0}; SystemObject applet = system_child(creator, 0, args, 8);
        uint32_t event = system_event(applet, 105, 0);
        assert(!system_call(applet, 20, 0, NULL, 0)); assert(test_event_signaled(event) == 1);
        SystemObject holders[SATCHEL_SF_MAX]; unsigned count = 0;
        for (;;) {
            uint32_t result = system_call(creator, 10, 0, &size, 8);
            if (result) { assert(result == SATCHEL_SF_FULL); break; }
            uint32_t child = satchel_u32(message + (domain ? 48 : 12));
            holders[count++] = (SystemObject){domain ? creator.h : child, domain ? child : 1, domain};
        }
        assert(count == (domain ? SATCHEL_DOMAIN_MAX : SATCHEL_SF_MAX) - 4);
        assert(system_call(applet, 101, 0, NULL, 0) == SATCHEL_SF_FULL);
        assert(test_event_signaled(event) == 1);
        system_close(holders[--count]);
        SystemObject output = system_child(applet, 101, NULL, 0);
        assert(test_event_signaled(event) == 0); system_close(output);
        while (count) system_close(holders[--count]);
        SystemObject wrong = system_child(proxy, 1, NULL, 0);
        assert(system_push(applet, 100, wrong) == SATCHEL_AM_OBJECT);
        if (domain) system_close(wrong);
        SystemObject storage = system_child(creator, 10, &size, 8);
        SystemObject accessor = system_child(storage, 0, NULL, 0);
        // Decode all high pointer-address bits using the reference descriptor layout.
        uint64_t high_address = 0x6f12340000ull;
        make(10, domain, 0, &zero, 8);
        satchel_put32(message, 4 | (1u << 16));
        satchel_put32(message + 8, (1u << 16) | (uint32_t)((high_address & 0xf00000000ull) >> 20) | (uint32_t)((high_address & 0x7000000000ull) >> 30));
        satchel_put32(message + 12, (uint32_t)high_address);
        // Raw data starts at 16 instead of 8; preserve its original end.
        satchel_put32(message + 4, satchel_u32(message + 4) - 2);
        if (domain) satchel_put32(message + 20, accessor.id);
        SatchelIPCRequest parsed = {0}; assert(satchel_ipc_parse(message, sizeof message, domain, &parsed));
        assert(parsed.pointer.address == high_address && parsed.pointer.size == 1);
        // A valid pointer descriptor can write synthetic storage in either transport.
        satchel_put32(message + 8, 1u << 16); satchel_put32(message + 12, 0x12340000); buffer[0] = 0x5a;
        assert(!satchel_session_send(accessor.h, message, sizeof message, NULL));
        assert(!satchel_u32(message + (domain ? 40 : 24)));
        memset(buffer, 0xcc, sizeof buffer);
        assert(!system_call(accessor, 11, 1, &zero, 8) && buffer[0] == 0x5a && buffer[1] == 0xcc);
        // AutoSelect prefers a non-null pointer/list over its map-alias fallback.
        memset(message, 0, sizeof message);
        satchel_put32(message, 4 | (1u << 16) | (1u << 20));
        unsigned cm = domain ? 48 : 32, end = cm + 32;
        satchel_put32(message + 4, (end - 28) / 4);
        satchel_put32(message + 8, 1u << 16); satchel_put32(message + 12, 0x12340000);
        satchel_put32(message + 16, 1); satchel_put32(message + 20, 1); // Invalid unused map address.
        if (domain) { message[32] = 1; message[34] = 24; satchel_put32(message + 36, accessor.id); }
        satchel_put32(message + cm, 0x49434653); satchel_put32(message + cm + 8, 10);
        buffer[0] = 0x7b; assert(!satchel_session_send(accessor.h, message, sizeof message, NULL));
        assert(!satchel_u32(message + (domain ? 40 : 24)));
        end = (unsigned)make(11, domain, 2, &zero, 8);
        satchel_put32(message + 4, satchel_u32(message + 4) | (2u << 10));
        satchel_put32(message + 8, 1); satchel_put32(message + 12, 1);
        uint64_t list = 0x12340000ull | (1ull << 48); memcpy(message + end, &list, 8);
        if (domain) satchel_put32(message + 36, accessor.id);
        buffer[0] = 0; assert(!satchel_session_send(accessor.h, message, sizeof message, NULL));
        assert(!satchel_u32(message + (domain ? 40 : 24)) && buffer[0] == 0x7b);
        uint64_t offset = UINT64_MAX;
        assert(system_call(accessor, 11, 2, &offset, 8) == SATCHEL_AM_BOUNDS);
        assert(system_call(accessor, 10, 2, &offset, 8) == SATCHEL_AM_BOUNDS);
        make(11, domain, 2, &zero, 8); satchel_put32(message + 12, 1);
        if (domain) satchel_put32(message + 36, accessor.id);
        assert(!satchel_session_send(accessor.h, message, sizeof message, NULL));
        assert(satchel_u32(message + (domain ? 40 : 24)) == SATCHEL_IPC_INVALID);
        // Reset releases open storage, accessors and owner event handles, but not client copies.
        satchel_sessions_reset(); assert(test_event_count() == 1); satchel_event_close(event);
        assert(!test_event_count());
    }
    for (unsigned i = 0; i < sizeof system_contracts / sizeof *system_contracts; i++)
        assert(system_contracts[i].hits[0] && system_contracts[i].hits[1]);
    printf("PASS: all %zu round 7 inventory contracts exercised in session and domain mode\n", sizeof system_contracts / sizeof *system_contracts);
    puts("PASS: round 7 applet trees, launch storage, clean keyboard cancellation, error dismissal, display, audio failures, parental queries and timezone fallback in both transports");
}
