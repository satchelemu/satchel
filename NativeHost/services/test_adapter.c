// Exercise the actual engine overlay hooks and native memory writer with synthetic data.
#include <assert.h>
#include <sys/mman.h>
#include <time.h>
#include "hle_sf.c"
#include "hle_time.c"
int dm_guest_as_contains(uintptr_t p) { (void)p; return 0; }
// apm and csrng sources (hle_oe.c and hle_os.c in the engine), synthetic here.
int dm_opmode(void) { return 1; }
uint64_t dm_entropy64(void) { return 0x0123456789abcdefull; }
static Result send_hook(uint32_t h, uint32_t *m, uint64_t size) {
    return ((Result (*)(uint32_t, uint32_t *, uint64_t))hle_force_h_sf_Send.fn)(h, m, size);
}
static uint64_t test_frame = 37;
uint64_t dm_nvn_frame_count(void) { return test_frame; }
uint64_t guest_ns(void) { return clock_gettime_nsec_np(CLOCK_MONOTONIC); }
module_t *module_of(const void *p) { (void)p; return NULL; }
static _Alignas(16) uint8_t msg[256];
static size_t read_summary(const char *path, char *bytes, size_t capacity) {
    FILE *f = fopen(path, "r"); assert(f);
    size_t n = fread(bytes, 1, capacity, f);
    assert(n < capacity && !ferror(f) && feof(f)); fclose(f); return n;
}
static void request(uint32_t command, int domain) {
    memset(msg, 0, sizeof msg); satchel_put32(msg, 4); satchel_put32(msg + 4, domain ? 12 : 8);
    size_t cm = domain ? 32 : 16;
    if (domain) { msg[16] = 1; msg[18] = 16; satchel_put32(msg + 20, 1); }
    satchel_put32(msg + cm, 0x49434653); satchel_put32(msg + cm + 8, command);
}
int main(int argc, char **argv) {
    assert(argc == 2); setenv("DM_SERVICE_REPORT", argv[1], 1);
    char summary[PATH_MAX]; snprintf(summary, sizeof summary, "%s.json", argv[1]);
    setenv("DM_MISSING_SERVICES", summary, 1); satchel_missing_reset();
    unsetenv("DM_AOC_DIR"); unsetenv("DM_AOC_OWNED");
    uint32_t handle, duplicate, object;
    assert(!h_sf_Connect(&handle, "set")); assert(!h_sf_Dup(&duplicate, handle));
    request(6, 0); assert(!send_hook(handle, (uint32_t *)msg, sizeof msg)); assert(satchel_u32(msg + 32) == 18);
    request(1234567, 0); assert(!send_hook(handle, (uint32_t *)msg, sizeof msg)); assert(satchel_u32(msg + 24) == SATCHEL_IPC_UNKNOWN);
    test_frame = 42;
    request(99, 0); assert(!send_hook(handle, (uint32_t *)msg, sizeof msg));
    request(1234567, 0); assert(!send_hook(duplicate, (uint32_t *)msg, sizeof msg));
    assert(!h_sf_ToDomain(&object, duplicate) && object == 1);
    request(0, 1); assert(!send_hook(duplicate, (uint32_t *)msg, sizeof msg)); assert(!memcmp(msg + 48, "en-US", 5));
    h_sf_Close(handle); assert(send_hook(handle, (uint32_t *)msg, sizeof msg) == 0xe401u);
    h_sf_Close(duplicate);
    assert(h_sf_Connect(&handle, "absent") == 0xe15u); assert(h_sf_Connect(&handle, "absent") == 0xe15u);
    assert(!h_sf_Connect(&handle, "time:u")); request(0, 0);
    assert(!send_hook(handle, (uint32_t *)msg, sizeof msg));
    assert(satchel_u32(msg + 4) >> 31 && satchel_u32(msg + 8) == 32);
    uint32_t clock = satchel_u32(msg + 12);
    int64_t before, after; satchel_host_clock_now(&before);
    request(0, 0); assert(!send_hook(clock, (uint32_t *)msg, sizeof msg));
    satchel_host_clock_now(&after);
    assert((int64_t)satchel_u64(msg + 32) >= before && (int64_t)satchel_u64(msg + 32) <= after);
    uint8_t context[32]; assert(!satchel_host_clock_context(context));
    request(2, 0); assert(!send_hook(clock, (uint32_t *)msg, sizeof msg));
    assert(!memcmp(context, msg + 32, sizeof context));
    h_sf_Close(clock); h_sf_Close(handle);
    // The actual SDK and privileged IPC paths share local-clock adjustments,
    // steady offsets, correction flags and snapshot fields.
    for (int domain = 0; domain < 2; domain++) {
        assert(!h_sf_Connect(&handle, "time:a"));
        if (domain) assert(!h_sf_ToDomain(&object, handle));
        request(4, domain); assert(!send_hook(handle, (uint32_t *)msg, sizeof msg));
        uint32_t local = satchel_u32(msg + (domain ? 48 : 12));
        request(1, domain);
        satchel_put32(msg + 4, domain ? 14 : 10);
        if (domain) { msg[18] = 24; satchel_put32(msg + 20, local); }
        int64_t desired = 1800000000; memcpy(msg + (domain ? 48 : 32), &desired, 8);
        assert(!send_hook(domain ? handle : local, (uint32_t *)msg, sizeof msg));
        assert(!satchel_u32(msg + (domain ? 40 : 24)));
        int64_t now; assert(!h_time_GetCurrentTime(&now) && now >= desired && now <= desired + 2);
        assert(h_time_now() >= desired && h_time_now() <= desired + 2);
        int64_t ts[2]; assert(!h_time_StdTimespec(ts) && ts[0] >= desired && ts[0] <= desired + 2);
        uint8_t ctx[32]; assert(!h_time_GetContext(ctx));
        request(2, domain); if (domain) satchel_put32(msg + 20, local);
        assert(!send_hook(domain ? handle : local, (uint32_t *)msg, sizeof msg));
        assert(!memcmp(ctx, msg + (domain ? 48 : 32), 32));
        request(101, domain); satchel_put32(msg + 4, domain ? 14 : 10);
        if (domain) msg[18] = 17;
        msg[domain ? 48 : 32] = 1;
        assert(!send_hook(handle, (uint32_t *)msg, sizeof msg));
        assert(!satchel_u32(msg + (domain ? 40 : 24)));
        uint8_t snapshot[0xd0]; assert(!h_time_Snapshot(snapshot) && snapshot[0xcc] == 1);
        uint8_t steady_before[24], steady_after[24]; assert(!h_time_SteadyNow(steady_before));
        int64_t offset = 5000000000ll;
        assert(!satchel_host_clock_change(1, 3, (uint8_t *)&offset, NULL));
        assert(!h_time_SteadyNow(steady_after));
        assert(satchel_u64(steady_after) >= satchel_u64(steady_before) + 5 && satchel_u64(steady_after) <= satchel_u64(steady_before) + 6);
        assert(!h_time_MonoTimespec(ts));
        assert(ts[0] >= (int64_t)satchel_u64(steady_after) && ts[0] <= (int64_t)satchel_u64(steady_after) + 1);
        assert(!h_time_GetCurrentTime(&now) && now >= desired + 5 && now <= desired + 7);
        offset = 0; assert(!satchel_host_clock_change(1, 3, (uint8_t *)&offset, NULL));
        memset(ctx + 16, 0, 16); assert(!satchel_host_clock_change(0, 3, ctx, NULL));
        assert(h_time_GetCurrentTime(&now) == ((102u << 9) | 116u));
        assert(h_time_Snapshot(snapshot) == ((102u << 9) | 116u));
        assert(h_time_AdjSnapshot(snapshot) == ((102u << 9) | 116u));
        assert(h_time_StdTimespec(ts) == -1 && h_time_AdjTimespec(ts) == -1);
        satchel_time_control(0, NULL, NULL); // Preserve earlier diagnostic summary rows.
        int64_t wall = time(NULL), shifted;
        offset = 5000000000ll; assert(!satchel_host_clock_change(1, 3, (uint8_t *)&offset, NULL));
        assert(!h_time_GetCurrentTime(&shifted) && shifted >= wall + 5 && shifted <= wall + 7);
        assert(!h_time_StdTimespec(ts) && ts[0] >= wall + 5 && ts[0] <= wall + 7);
        satchel_time_control(0, NULL, NULL);
        if (!domain) h_sf_Close(local); h_sf_Close(handle);
    }
    puts("PASS: native SDK time, IPC time, contexts, timespecs, snapshots, offsets and mismatch errors agree");
    // Send an account list to an actual native C-descriptor buffer.
    assert(!h_sf_Connect(&handle, "acc:u0")); request(2, 0);
    uint8_t *out = mmap(NULL, 16384, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0); assert(out != MAP_FAILED);
    memset(out, 0xcc, 64); satchel_put32(msg + 4, 8 | (2u << 10));
    uint64_t packed = (uint64_t)(uintptr_t)out | (64ull << 48); memcpy(msg + 40, &packed, 8);
    assert(!send_hook(handle, (uint32_t *)msg, sizeof msg)); assert(!satchel_u32(msg + 24)); assert(!memcmp(out, satchel_uid, 16));
    for (unsigned i = 16; i < 64; i++) assert(!out[i]);
    uint8_t readback[16]; assert(satchel_ipc_read((uint64_t)(uintptr_t)out, readback, 16) && !memcmp(readback, satchel_uid, 16));
    assert(!satchel_ipc_read(1, readback, 16));
    // Native pointer descriptor, including the high address bits of the mmap region.
    uint32_t applet_root, proxy_handle, creator_handle, storage_handle, accessor_handle;
    assert(!h_sf_Connect(&applet_root, "appletOE"));
    request(0, 0); assert(!send_hook(applet_root, (uint32_t *)msg, sizeof msg)); proxy_handle = satchel_u32(msg + 12);
    request(11, 0); assert(!send_hook(proxy_handle, (uint32_t *)msg, sizeof msg)); creator_handle = satchel_u32(msg + 12);
    request(10, 0); satchel_put32(msg + 32, 16);
    assert(!send_hook(creator_handle, (uint32_t *)msg, sizeof msg)); storage_handle = satchel_u32(msg + 12);
    request(0, 0); assert(!send_hook(storage_handle, (uint32_t *)msg, sizeof msg)); accessor_handle = satchel_u32(msg + 12);
    request(10, 0); satchel_put32(msg, 4 | (1u << 16));
    uint64_t address = (uint64_t)(uintptr_t)out;
    satchel_put32(msg + 8, (16u << 16) | (uint32_t)((address & 0xf00000000ull) >> 20) | (uint32_t)((address & 0x7000000000ull) >> 30));
    satchel_put32(msg + 12, (uint32_t)address);
    assert(!send_hook(accessor_handle, (uint32_t *)msg, sizeof msg) && !satchel_u32(msg + 24));
    h_sf_Close(storage_handle); h_sf_Close(creator_handle); h_sf_Close(proxy_handle); h_sf_Close(applet_root);
    memset(out, 0, 16); request(11, 0); satchel_put32(msg + 4, 8 | (2u << 10)); memcpy(msg + 40, &packed, 8);
    assert(!send_hook(accessor_handle, (uint32_t *)msg, sizeof msg) && !satchel_u32(msg + 24) && !memcmp(out, satchel_uid, 16));
    h_sf_Close(accessor_handle);
    assert(!mprotect(out, 16384, PROT_READ)); assert(!satchel_ipc_write((uint64_t)(uintptr_t)out, satchel_uid, 16));
    assert(!satchel_ipc_write(1, satchel_uid, 16)); munmap(out, 16384); h_sf_Close(handle);
    assert(!h_sf_Connect(&handle, "acc:u0")); request(5, 0);
    satchel_put32(msg + 4, 12); memcpy(msg + 32, satchel_uid, 16);
    assert(!send_hook(handle, (uint32_t *)msg, sizeof msg));
    uint32_t profile = satchel_u32(msg + 12);
    request(1, 0); assert(!send_hook(profile, (uint32_t *)msg, sizeof msg));
    assert(!satchel_u32(msg + 24) && !memcmp(msg + 32, satchel_uid, 16));
    assert(!memcmp(msg + 56, "Player", 6));
    h_sf_Close(profile); h_sf_Close(handle);
    FILE *log = fopen(argv[1], "r"); assert(log); unsigned lines = 0; char line[512];
    while (fgets(line, sizeof line, log)) { assert(strstr(line, "ServiceIPC service=")); assert(strstr(line, "caller=host+0x0")); lines++; }
    fclose(log); assert(lines == 3);
    // Real kernel-event helpers, including copied handles surviving object close.
    extern int satchel_test_event_state(uint32_t h);
    for (int domain = 0; domain < 2; domain++) {
        assert(!h_sf_Connect(&handle, "aoc:u"));
        if (domain) assert(!h_sf_ToDomain(&object, handle));
        request(2, domain); assert(!send_hook(handle, (uint32_t *)msg, sizeof msg));
        assert(!satchel_u32(msg + (domain ? 48 : 32)));
        request(8, domain); assert(!send_hook(handle, (uint32_t *)msg, sizeof msg));
        assert(satchel_u32(msg + 4) >> 31 && satchel_u32(msg + 8) == 2);
        uint32_t event = satchel_u32(msg + 12); assert(satchel_test_event_state(event) == 0);
        h_sf_Close(handle); assert(satchel_test_event_state(event) == 0);
        satchel_host_event_close(event); assert(satchel_test_event_state(event) == -1);
    }
    // Same child command through moved sessions and domains has one summary row.
    for (int domain = 0; domain < 2; domain++) {
        assert(!h_sf_Connect(&handle, "acc:u0"));
        if (domain) assert(!h_sf_ToDomain(&object, handle));
        request(5, domain); satchel_put32(msg + 4, domain ? 16 : 12);
        if (domain) msg[18] = 32;
        memcpy(msg + (domain ? 48 : 32), satchel_uid, 16);
        assert(!send_hook(handle, (uint32_t *)msg, sizeof msg));
        uint32_t child = satchel_u32(msg + (domain ? 48 : 12));
        for (int i = 0; i < 2; i++) {
            request(10, domain); if (domain) satchel_put32(msg + 20, child);
            assert(!send_hook(domain ? handle : child, (uint32_t *)msg, sizeof msg));
            assert(satchel_u32(msg + (domain ? 40 : 24)) == SATCHEL_IPC_UNKNOWN);
        }
        request(UINT32_MAX, domain); if (domain) satchel_put32(msg + 20, child);
        assert(!send_hook(domain ? handle : child, (uint32_t *)msg, sizeof msg));
        assert(satchel_u32(msg + (domain ? 40 : 24)) == SATCHEL_IPC_UNKNOWN);
        if (!domain) h_sf_Close(child);
        h_sf_Close(handle);
    }
    for (int domain = 0; domain < 2; domain++) {
        assert(!h_sf_Connect(&handle, "audren:u"));
        if (domain) assert(!h_sf_ToDomain(&object, handle));
        request(2, domain); satchel_put32(msg + 4, domain ? 14 : 10);
        if (domain) msg[18] = 24;
        assert(!send_hook(handle, (uint32_t *)msg, sizeof msg));
        uint32_t child = satchel_u32(msg + (domain ? 48 : 12));
        request(20, domain); if (domain) satchel_put32(msg + 20, child);
        assert(!send_hook(domain ? handle : child, (uint32_t *)msg, sizeof msg));
        assert(satchel_u32(msg + (domain ? 40 : 24)) == ((513u << 9) | 153u));
        if (!domain) h_sf_Close(child);
        h_sf_Close(handle);
    }
    char before_reset[8192], after_reset[8192];
    size_t before_size = read_summary(summary, before_reset, sizeof before_reset);
    satchel_sessions_reset(); assert(satchel_missing_count == 0);
    size_t after_size = read_summary(summary, after_reset, sizeof after_reset);
    assert(before_size == after_size && !memcmp(before_reset, after_reset, before_size));
    char reset_summary[PATH_MAX]; snprintf(reset_summary, sizeof reset_summary, "%s.reset.json", argv[1]);
    // Match SessionBridge: a new launch initializes its own empty summary.
    FILE *reset = fopen(reset_summary, "w"); assert(reset);
    assert(fputs("[]\n", reset) >= 0 && !fclose(reset));
    setenv("DM_MISSING_SERVICES", reset_summary, 1); satchel_sessions_reset();
    char empty[16] = {0}; assert(read_summary(reset_summary, empty, sizeof empty) == 3);
    assert(!strcmp(empty, "[]\n"));
    test_frame = 99;
    assert(!h_sf_Connect(&handle, "set"));
    request(1234567, 0); assert(!send_hook(handle, (uint32_t *)msg, sizeof msg));
    assert(satchel_u32(msg + 24) == SATCHEL_IPC_UNKNOWN); h_sf_Close(handle);
    satchel_sessions_reset(); assert(satchel_missing_count == 0);
    puts("PASS: teardown retains summary bytes; the next launch starts fresh counts and first frame");
    // Round 11 raw IPC through the real hooks: apm answers the engine's performance mode.
    assert(!h_sf_Connect(&handle, "apm")); request(1, 0); assert(!send_hook(handle, (uint32_t *)msg, sizeof msg));
    assert(satchel_u32(msg + 24) == 0 && satchel_u32(msg + 32) == (dm_profile()->perf_boost ? 1u : 1u)); h_sf_Close(handle);
    // Round 12 through the real hooks: every application port that was open now connects. The ports
    // Ryujinx registers without commands report each command missing under the reference interface.
    char round12_summary[PATH_MAX]; snprintf(round12_summary, sizeof round12_summary, "%s.round12.json", argv[1]);
    setenv("DM_MISSING_SERVICES", round12_summary, 1); satchel_sessions_reset();
    const char *const round12_ports[] = {"audrec:u", "grc:c", "hidbus", "lp2p:app", "mnpp:app", "notif:a", "npns:u", "ntc"};
    for (unsigned i = 0; i < 8; i++) { assert(!h_sf_Connect(&handle, round12_ports[i])); h_sf_Close(handle); }
    const char *const bare[][2] = {{"grc:c", "IGrcService"}, {"notif:a", "INotificationServicesForApplication"}, {"npns:u", "INpnsUser"}};
    for (unsigned i = 0; i < 3; i++) {
        assert(!h_sf_Connect(&handle, bare[i][0])); request(0, 0); assert(!send_hook(handle, (uint32_t *)msg, sizeof msg));
        assert(satchel_u32(msg + 24) == SATCHEL_IPC_UNKNOWN); h_sf_Close(handle);
        assert(satchel_missing_count == i + 1 && !strcmp(satchel_missing[i].service, bare[i][0]) &&
               !strcmp(satchel_missing[i].interface, bare[i][1]) && !satchel_missing[i].command && !satchel_missing[i].method);
    }
    assert(!h_sf_Connect(&handle, "hidbus")); request(1, 0); satchel_put32(msg + 4, 14);   // 24 argument bytes
    assert(!send_hook(handle, (uint32_t *)msg, sizeof msg) && !satchel_u32(msg + 24)); h_sf_Close(handle);
    satchel_sessions_reset(); assert(satchel_missing_count == 0);
    puts("PASS: round 12 ports connect through the real hooks; bare ports report missing commands by reference interface");
    // Round 11 SDK takeovers (SdkFamilies.inc), registered under their SDK names.
    assert(!strcmp(hle_force_h_oe_SatchelRecordingSupported.name, "_ZN2nn2oe28IsGamePlayRecordingSupportedEv"));
    h_oe_SatchelUserActive(); h_oe_SatchelWireless(1); h_oe_SatchelMediaBegin(); h_oe_SatchelMediaEnd();
    h_oe_SatchelCrashOn(); h_oe_SatchelCrashOff(); h_oe_SatchelBlockShortSpan(1000); h_oe_SatchelBlockShort(); h_oe_SatchelUnblockShort();
    assert(!h_oe_SatchelRecordingSupported() && !h_oe_SatchelHdcp());
    assert(!h_pctl_SatchelRestricted() && !h_pctl_SatchelStereoPermitted());
    assert(h_pctl_SatchelStereoConfirm() == ((104u << 9) | 142u)); h_pctl_SatchelStereoReset();
    assert(h_aoc_SatchelBaseId() == satchel_host_program_id() + 0x1000);
    assert(h_aoc_SatchelPrepare(3) == RES_OK && h_aoc_SatchelMountStatus() == RES_OK);
    assert(h_bcat_SatchelSync(NULL) == ((8u << 9) | 122u) && h_bcat_SatchelSyncApp(NULL, 1, 2) == ((8u << 9) | 122u));
    assert(h_bcat_SatchelSyncAppDir(NULL, 1, 2, "d") == ((8u << 9) | 122u) && h_bcat_SatchelSyncDir(NULL, "d") == ((8u << 9) | 122u));
    int dirs = 7; h_bcat_SatchelCancel(); assert(h_bcat_SatchelEnumerate(&dirs, NULL, 4) == ((5u << 9) | 122u) && dirs == 0);
    assert(h_album_SatchelSaveData(msg, 16, 0, 0, 0, NULL, 0) == RES_FAIL && h_album_SatchelShareData(msg, 16, 0, 0, 0, NULL, "", NULL, 0) == RES_FAIL);
    assert(h_album_SatchelSaveUsers(msg, 16, 0, 0, 0, NULL, 0) == RES_FAIL && h_album_SatchelShowAll() == RES_FAIL);
    assert(h_album_SatchelShare(msg, 16, 0, 0, NULL) == RES_FAIL && h_album_SatchelShareId(NULL, msg, 16, 0, 0, NULL) == RES_FAIL);
    puts("PASS: round 11 SDK takeovers for oe, pctl, aoc, bcat and album answer their reference or engine policy");
    puts("PASS: real engine hooks, duplicate and domain sessions, missing connect, close, native buffer writes, read-only rejection, once-only sidecar");
}
