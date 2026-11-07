// Synthetic command and malformed transport tests. No game inputs.
#include <assert.h>
#include <stdio.h>
#include "ServiceIPC.inc"
#include "SessionIPC.inc"
static void satchel_missing_record(const char *s, const char *i, uint32_t c) { (void)s; (void)i; (void)c; }
static void satchel_missing_reset(void) {}
int satchel_host_clock_override(uint8_t *out);
uint32_t satchel_host_steady_adjust(int64_t *seconds);
static uint32_t satchel_clock_now(int64_t *out) {
    uint8_t ctx[32], tp[24];
    if (!satchel_host_clock_override(ctx)) { *out = 1700000000; return satchel_host_steady_adjust(out); }
    uint32_t result = satchel_clock_steady(tp); if (result) return result;
    if (memcmp(ctx + 16, tp + 8, 16)) return ((102u << 9) | 116u);
    int64_t offset, steady; memcpy(&offset, ctx, 8); memcpy(&steady, tp, 8);
    __int128 now = (__int128)offset + steady;
    if (now < INT64_MIN || now > INT64_MAX) return ((201u << 9) | 116u);
    *out = (int64_t)now; return 0;
}
static uint32_t satchel_clock_steady(uint8_t *out) {
    int64_t seconds = 123; uint32_t result = satchel_host_steady_adjust(&seconds); if (result) return result; memcpy(out, &seconds, 8); memcpy(out + 8, "test-steady-0001", 16); return 0;
}
#define h_time_SteadyNow satchel_clock_steady
#include "ClockState.inc"
#undef h_time_SteadyNow
static uint32_t satchel_time_control(unsigned c, const uint8_t *in, uint8_t *out) { return satchel_host_time_control(c, in, out); }
static uint32_t satchel_clock_change(unsigned k, unsigned c, const uint8_t *in, uint8_t *out) { return satchel_host_clock_change(k, c, in, out); }
extern uint32_t satchel_host_fs_open(unsigned, uint32_t *);
extern uint32_t satchel_host_fs_query(uint32_t, unsigned, const char *, uint8_t *);
extern void satchel_test_fs_setup(const char *);
extern void satchel_test_fs_queries(void);
static uint32_t satchel_fs_open(unsigned t, uint32_t *i) { return satchel_host_fs_open(t, i); }
static uint32_t satchel_fs_query(uint32_t i, unsigned c, const char *p, uint8_t *o) { return satchel_host_fs_query(i, c, p, o); }
static int64_t satchel_clock_rtc(void) { return 1700000001; }
static uint32_t satchel_clock_context(uint8_t *out) {
    if (satchel_host_clock_override(out)) return 0;
    int64_t offset = 1700000000 - 123; memcpy(out, &offset, 8); return satchel_clock_steady(out + 8);
}
static struct { unsigned object; } test_handles[128];
static struct { unsigned refs; int signaled; } test_events[128];
static void satchel_event_set(uint32_t h, int signaled) {
    assert(h < 128 && test_handles[h].object); test_events[test_handles[h].object].signaled = !!signaled;
}
static uint32_t satchel_event_create(int signaled) {
    for (unsigned i = 1; i < 128; i++) if (!test_events[i].refs)
        for (unsigned h = 1; h < 128; h++) if (!test_handles[h].object) {
            test_events[i].refs = 1; test_events[i].signaled = signaled; test_handles[h].object = i; return h;
        }
    return 0;
}
static uint32_t satchel_event_copy(uint32_t h) {
    assert(h < 128 && test_handles[h].object);
    for (unsigned c = 1; c < 128; c++) if (!test_handles[c].object) {
        test_handles[c].object = test_handles[h].object; test_events[test_handles[h].object].refs++; return c;
    }
    return 0;
}
static void satchel_event_close(uint32_t h) {
    assert(h < 128 && test_handles[h].object);
    test_events[test_handles[h].object].refs--; test_handles[h].object = 0;
}
static unsigned test_event_count(void) {
    unsigned n = 0; for (unsigned h = 1; h < 128; h++) n += !!test_handles[h].object; return n;
}
static int test_event_signaled(uint32_t h) { assert(h < 128 && test_handles[h].object); return test_events[test_handles[h].object].signaled; }
static int test_event_same(uint32_t a, uint32_t b) { return test_handles[a].object == test_handles[b].object; }
static uint64_t satchel_program_id(void) { return 0x0100123400000000ull; }
static unsigned test_perf_mode = 1;
static unsigned satchel_perf_mode(void) { return test_perf_mode; }
static void satchel_random(void *bytes, size_t size) { static uint8_t next = 1; for (size_t i = 0; i < size; i++) ((uint8_t *)bytes)[i] = next++ | 1; }
static unsigned satchel_aoc_list(uint32_t *out, unsigned start, unsigned count) {
    const uint32_t indices[] = {1, 7, 2000};
    if (!out) return 3;
    unsigned n = 0;
    while (start < 3 && n < count) out[n++] = indices[start++];
    return n;
}
static const char *const expected_languages[] = {
    "ja", "en-US", "fr", "de", "it", "es", "zh-CN", "ko", "nl", "pt", "ru",
    "zh-TW", "en-GB", "fr-CA", "es-419", "zh-Hans", "zh-Hant", "pt-BR"
};
static uint8_t buffer[256];
static unsigned logs;
static int satchel_ipc_read(uint64_t address, void *bytes, size_t size) {
    if (address < 0x12340000 || address - 0x12340000 > sizeof buffer || size > sizeof buffer - (address - 0x12340000)) return 0;
    memcpy(bytes, buffer + (address - 0x12340000), size); return 1;
}
static int satchel_ipc_write(uint64_t address, const void *bytes, size_t size) {
    if (address < 0x12340000 || address - 0x12340000 > sizeof buffer || size > sizeof buffer - (address - 0x12340000)) return 0;
    memcpy(buffer + (address - 0x12340000), bytes, size); return 1;
}
static char last_emit[96];
static void satchel_ipc_emit(const char *service, uint32_t cmd, const char *kind, void *caller) {
    (void)service; (void)cmd; (void)caller; logs++; snprintf(last_emit, sizeof last_emit, "%s", kind);
}
static _Alignas(16) uint8_t message[256];
static size_t make(uint32_t cmd, int domain, unsigned descriptor, const void *args, size_t n) {
    memset(message, 0, sizeof message);
    satchel_put32(message, 4 | (descriptor == 2 ? 1u << 24 : 0));
    size_t raw = descriptor == 2 ? 20 : 8, cm = (raw + 15) & ~(size_t)15;
    size_t end = cm + (domain ? 16 : 0) + 16 + ((n + 3) & ~(size_t)3) + 8;
    satchel_put32(message + 4, (uint32_t)((end - raw) / 4) | (descriptor == 1 ? 2u << 10 : 0));
    if (descriptor == 2) { satchel_put32(message + 8, sizeof buffer); satchel_put32(message + 12, 0x12340000); }
    if (descriptor == 1) { uint64_t v = 0x12340000ull | ((uint64_t)sizeof buffer << 48); memcpy(message + end, &v, 8); }
    if (domain) { message[cm] = 1; uint16_t bytes = (uint16_t)(16 + n); memcpy(message + cm + 2, &bytes, 2); satchel_put32(message + cm + 4, 1); cm += 16; }
    satchel_put32(message + cm, 0x49434653); satchel_put32(message + cm + 4, 1); satchel_put32(message + cm + 8, cmd); satchel_put32(message + cm + 12, 0x1234);
    if (n) memcpy(message + cm + 16, args, n);
    return end;
}
static uint32_t send(const char *service, int domain) {
    assert(satchel_ipc_send(service, domain, message, sizeof message, NULL) == 0);
    size_t cm = domain ? 32 : 16;
    assert(satchel_u32(message + cm) == 0x4f434653 && satchel_u32(message + cm + 4) == 1);
    assert(satchel_u32(message + cm + 12) == 0x1234);
    return satchel_u32(message + cm + 8);
}
static void *race(void *arg) { for (unsigned i = 0; i < 1000; i++) satchel_ipc_once("race", 42, "unknown", arg); return NULL; }
#include "test_ports.inc"
#include "test_broad.inc"
#include "test_system.inc"
#include "test_round8.inc"
#include "test_round11.inc"
int main(int argc, char **argv) {
    assert(argc == 2); satchel_test_fs_setup(argv[1]); satchel_test_fs_queries();
    test_ports();
    test_profiles();
    test_broad();
    test_system();
    test_round8();
    test_round11();
    for (int dom = 0; dom < 2; dom++) {
        uint8_t *out = message + (dom ? 48 : 32);
        unsetenv("DM_LANG"); make(0, dom, 0, NULL, 0); assert(!send("set", dom)); assert(!memcmp(out, "en-US\0\0\0", 8));
        setenv("DM_LANG", "ja", 1); make(0, dom, 0, NULL, 0); assert(!send("set", dom)); assert(!memcmp(out, "ja\0\0\0\0\0\0", 8));
        make(4, dom, 0, NULL, 0); assert(!send("set", dom) && satchel_u32(out) == 0);
        const char *langs[] = {"en-US", "fr", "zh-Hant", "ko", "pt-BR"};
        const unsigned regions[] = {1, 2, 4, 4, 1};
        for (unsigned i = 0; i < 5; i++) { setenv("DM_LANG", langs[i], 1); make(4, dom, 0, NULL, 0); assert(!send("set", dom) && satchel_u32(out) == regions[i]); }
        for (uint32_t i = 0; i < 18; i++) { make(2, dom, 0, &i, 4); assert(!send("set", dom)); assert(!strcmp((char *)out, expected_languages[i])); }
        uint32_t bad = UINT32_MAX; make(2, dom, 0, &bad, 4); assert(send("set", dom) == ((625u << 9) | 105));
        make(3, dom, 0, NULL, 0); assert(!send("set", dom) && satchel_u32(out) == 15);
        make(6, dom, 0, NULL, 0); assert(!send("set", dom) && satchel_u32(out) == 18);
        for (unsigned cmd = 1; cmd <= 5; cmd += 4) {
            memset(buffer, 0xcc, sizeof buffer); make(cmd, dom, cmd == 1 ? 1 : 2, NULL, 0);
            assert(!send("set", dom)); unsigned count = cmd == 1 ? 15 : 18; assert(satchel_u32(out) == count);
            for (unsigned i = 0; i < count; i++) assert(!strcmp((char *)buffer + 8 * i, expected_languages[i]));
            assert(buffer[count * 8] == 0xcc);
        }
        make(8, dom, 0, NULL, 0); assert(!send("set", dom) && !out[0]);
        make(0, dom, 0, NULL, 0); assert(!send("acc:u0", dom) && satchel_u32(out) == 1);
        make(1, dom, 0, satchel_uid, 16); assert(!send("acc:u0", dom) && out[0] == 1);
        uint64_t other[2] = {3, 4}; make(1, dom, 0, other, 16); assert(!send("acc:u0", dom) && !out[0]);
        memset(other, 0, sizeof other); make(1, dom, 0, other, 16); assert(send("acc:u0", dom) == SATCHEL_ACC_NULL);
        const unsigned cmds[] = {2, 3, 141};
        for (unsigned i = 0; i < 3; i++) {
            memset(buffer, 0xcc, sizeof buffer); make(cmds[i], dom, 1, NULL, 0); assert(!send("acc:u0", dom)); assert(!memcmp(buffer, satchel_uid, 16));
            for (unsigned k = 16; k < sizeof buffer; k++) assert(buffer[k] == 0);
            make(cmds[i], dom, 0, NULL, 0); assert(send("acc:u0", dom) == SATCHEL_ACC_BUFFER);
        }
        make(4, dom, 0, NULL, 0); assert(!send("acc:u0", dom) && !memcmp(out, satchel_uid, 16));
        make(50, dom, 0, NULL, 0); assert(!send("acc:u0", dom) && !out[0]);
        for (uint8_t online = 0; online < 2; online++) { make(51, dom, 0, &online, 1); assert(!send("acc:u0", dom) && !memcmp(out, satchel_uid, 16)); }
        make(999, dom, 0, NULL, 0); assert(send("set", dom) == SATCHEL_IPC_UNKNOWN);
        make(123, dom, 0, NULL, 0); assert(!send("olsc:u", dom));
    }
    // Non-consecutive duplicates, service distinction, full 32-bit IDs and reset.
    satchel_ipc_reset(); logs = 0;
    satchel_ipc_once("set", 1, "unknown", NULL); satchel_ipc_once("set", 2, "unknown", NULL); satchel_ipc_once("set", 1, "unknown", NULL);
    satchel_ipc_once("set", 65537, "unknown", NULL); satchel_ipc_once("acc:u0", 1, "unknown", NULL); assert(logs == 4);
    pthread_t threads[4]; for (unsigned i = 0; i < 4; i++) assert(!pthread_create(&threads[i], NULL, race, NULL));
    for (unsigned i = 0; i < 4; i++) pthread_join(threads[i], NULL); assert(logs == 5);
    satchel_ipc_reset(); satchel_ipc_once("set", 1, "unknown", NULL); assert(logs == 6);
    for (unsigned i = 0; i < 2000; i++) satchel_ipc_once("set", i, "unknown", NULL); assert(satchel_seen_count == 1024);
    // Truncated descriptors, bad domain headers, invalid destination and canaries.
    make(1, 0, 1, NULL, 0); satchel_put32(message + 4, 0x3ff | (2 << 10)); assert(satchel_ipc_send("set", 0, message, 256, NULL) == SATCHEL_IPC_INVALID);
    make(0, 1, 0, NULL, 0); satchel_put32(message + 20, 9); assert(satchel_ipc_send("set", 1, message, 256, NULL) == SATCHEL_IPC_INVALID);
    make(0, 1, 0, NULL, 0); message[16] = 2; assert(!satchel_ipc_send("set", 1, message, 256, NULL));
    size_t end = make(1, 0, 1, NULL, 0); memset(message + end, 0xff, 8); assert(send("set", 0) == SATCHEL_IPC_INVALID);
    end = make(2, 0, 1, NULL, 0); uint64_t small = 0x12340000ull | (15ull << 48); memcpy(message + end, &small, 8);
    memset(buffer, 0xcc, sizeof buffer); assert(!send("acc:u0", 0)); for (unsigned i = 0; i < 15; i++) assert(buffer[i] == 0); assert(buffer[15] == 0xcc);
    make(0, 0, 0, NULL, 0); assert(satchel_ipc_send("set", 0, message, 4097, NULL) == SATCHEL_IPC_INVALID);
    // Exercise all short buffer lengths and deterministic arbitrary headers under ASan/UBSan.
    uint32_t random = 123;
    for (unsigned i = 0; i < 20000; i++) {
        for (size_t j = 0; j < sizeof message; j++) { random = random * 1664525u + 1013904223u; message[j] = random >> 24; }
        (void)satchel_ipc_send("set", i & 1, message, i % 257, NULL);
    }
    puts("PASS: all 16 ported commands in session and domain mode, bounds, deduplication, reset, concurrency, 20000 malformed messages");
}
