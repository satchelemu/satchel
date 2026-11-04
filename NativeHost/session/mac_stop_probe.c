// Real-title stop proof. Full restart is deliberately gated on resource owners.
#include "dm.h"
#include "hle.h"
#include "SessionBoundary.h"
#include <assert.h>
#include <mach/mach_vm.h>
#include "SessionState.h"
#include <CoreFoundation/CoreFoundation.h>
#include <errno.h>
#include <mach/mach.h>
#include <unistd.h>
#include <libproc.h>
#include <mach-o/getsect.h>
#include <mach-o/ldsyms.h>
#include "nvn_count.h"

static int dispatch_check(void) {
    extern struct { _Atomic uint64_t count; void *function; } nvn_slot[NVN_NFUNC];
    void *before[NVN_NFUNC];
    for (unsigned i = 0; i < NVN_NFUNC; i++) before[i] = nvn_slot[i].function;
    unsigned long bytes = 0;
    const hle_force_ent *entries = (const hle_force_ent *)getsectiondata(&_mh_execute_header, "__DATA", "swforce", &bytes);
    void *(*lookup)(const char *) = NULL;
    for (unsigned i = 0; i < bytes / sizeof(*entries); i++)
        if (!strcmp(entries[i].name, "nvnBootstrapLoader")) lookup = entries[i].fn;
    assert(lookup);
    for (unsigned cycle = 0; cycle < 5; cycle++) {
        assert(!satchel_session_begin()); satchel_session_request_stop(); assert(!satchel_session_join(2000));
        assert(!satchel_state_restore(SATCHEL_STATE_NVN)); assert(!satchel_session_end());
        assert(lookup("nvnQueueSubmitCommands"));
        unsigned changed = 0;
        for (unsigned i = 0; i < NVN_NFUNC; i++) changed += before[i] != nvn_slot[i].function;
        printf("[dispatch-check] cycle=%u changed=%u slots=%u\n", cycle, changed, NVN_NFUNC);
        if (changed) return 1;
    }
    return 0;
}

static void snapshot(const char *phase, int result) {
    mach_task_basic_info_data_t info = {0};
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count);
    printf("[session-proof] phase=%s result=%d guest_threads=%u guest_mappings=%u rss=%llu frames=%llu\n",
           phase, result, satchel_session_live_threads(), satchel_session_live_mappings(),
           (unsigned long long)info.resident_size, (unsigned long long)dm_guest_frame());
    struct proc_taskinfo task = {0};
    int count_fds = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, NULL, 0);
    void *fds = count_fds > 0 ? malloc((size_t)count_fds) : NULL;
    if (fds) { count_fds = proc_pidinfo(getpid(), PROC_PIDLISTFDS, 0, fds, count_fds); free(fds); }
    proc_pidinfo(getpid(), PROC_PIDTASKINFO, 0, &task, sizeof task);
    printf("[session-metrics] phase=%s threads=%u fds=%u\n", phase, task.pti_threadnum,
           count_fds > 0 ? (unsigned)(count_fds / sizeof(struct proc_fdinfo)) : 0);
    void satchel_probe_resources(const char *); satchel_probe_resources(phase);
    fflush(stdout);
}
void satchel_session_phase(const char *phase, int result) { snapshot(phase, result); }
void satchel_retire_checkpoint(unsigned stage) { snapshot("retire-checkpoint", (int)stage); }
static uint64_t soak_slot_end;
static int run_session(int argc, char **argv) {
    if (argc != 2) return 64;
    int result = satchel_host_session_prepare(); snapshot("begin", result); if (result) return 1;
    satchel_host_session_load(argc, argv); snapshot("loaded", 0);
    void satchel_memstat_start(void); satchel_memstat_start();
    pthread_attr_t attr; pthread_attr_init(&attr); pthread_attr_setstacksize(&attr, 64u << 20);
    pthread_t guest;
    result = satchel_session_pthread_create(&guest, &attr, satchel_host_session_enter, NULL);
    pthread_attr_destroy(&attr); snapshot("started", result); if (result) return 1;
    const char *duration = getenv("SATCHEL_SOAK_SECONDS");
    unsigned seconds = duration ? (unsigned)strtoul(duration, NULL, 10) : 30;
    if (!seconds || seconds > 7200) return 64;
    if (getenv("SATCHEL_SOAK_READY")) {
        char ready[4096]; snprintf(ready, sizeof ready, "%s/play.ready", getenv("DM_ROOT"));
        uint64_t limit = now_ns() + UINT64_C(900000000000);
        uint64_t sample = now_ns();
        while (access(ready, F_OK)) {
            if (now_ns() >= limit) { satchel_host_session_stop(); return 65; }
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, false); usleep(10000);
            if (now_ns() >= sample) { snapshot("warming", 0); sample = now_ns() + UINT64_C(5000000000); }
        }
        snapshot("play-start", 0);
    }
    uint64_t end = now_ns() + (uint64_t)seconds * UINT64_C(1000000000);
    if (soak_slot_end) end = soak_slot_end;
    if (end <= now_ns()) return 66;
    while (now_ns() < end) {
        uint64_t deadline = now_ns() + UINT64_C(5000000000);
        if (deadline > end) deadline = end;
        while (now_ns() < deadline) {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, false); usleep(10000);
        }
        snapshot("running", 0);
    }
    uint8_t *arena; size_t arena_size;
    void svc_guest_range(uint8_t **, size_t *); svc_guest_range(&arena, &arena_size);
    assert(arena && arena_size >= (UINT64_C(4) << 30));
    snapshot("stop-request", 0);
    result = satchel_host_session_stop();
    if (result) {
        snapshot("stop-failed", result);
        if (getenv("SATCHEL_SOAK_FAILURE_HOLD")) sleep(20);
        return result;
    }
    mach_vm_address_t region = (uintptr_t)arena; mach_vm_size_t length = 0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64; mach_port_t object = MACH_PORT_NULL;
    kern_return_t rc = mach_vm_region(mach_task_self(), &region, &length, VM_REGION_BASIC_INFO_64,
        (vm_region_info_t)&info, &count, &object);
    if (object) mach_port_deallocate(mach_task_self(), object);
    assert(rc == KERN_INVALID_ADDRESS || (rc == KERN_SUCCESS && region >= (uintptr_t)arena + arena_size));
    snapshot("arena-unmapped", 0);
    for (int i = 0; i < 100; i++) { CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, false); usleep(10000); }
    void satchel_probe_memory(void); satchel_probe_memory();
    snapshot("settled", 0);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && !strcmp(argv[1], "dispatch-check")) return dispatch_check();
    if (argc == 2) return run_session(argc, argv);
    if (argc < 5 || (argc - 1) % 4) return 64;
    snapshot("cold", 0);
    uint64_t slot_origin = now_ns();
    for (int cycle = 0; cycle < (argc - 1) / 4; cycle++) {
        if (getenv("SATCHEL_SOAK_FIXED_SLOTS")) {
            const char *duration = getenv("SATCHEL_SOAK_SECONDS");
            unsigned seconds = duration ? (unsigned)strtoul(duration, NULL, 10) : 30;
            if (!seconds || seconds > 7200 || getenv("SATCHEL_SOAK_READY")) return 64;
            soak_slot_end = slot_origin + (uint64_t)(cycle + 1) * seconds * UINT64_C(1000000000);
        }
        char **paths = argv + 1 + cycle * 4;
        char profile[4096], romfs[4096], shmap[4096], save[4096], temp[4096], aoc[4096], archive[4096];
        snprintf(profile, sizeof profile, "%s/profile.tsv", paths[1]);
        snprintf(romfs, sizeof romfs, "%s/romfs.img", paths[1]);
        snprintf(shmap, sizeof shmap, "%s/dm/shader_map.tsv", paths[1]);
        snprintf(save, sizeof save, "%s/save", paths[3]);
        snprintf(temp, sizeof temp, "%s/temp", paths[3]);
        snprintf(aoc, sizeof aoc, "%s/aoc", paths[3]);
        snprintf(archive, sizeof archive, "%s/archive", paths[3]);
        setenv("DM_PROFILE", profile, 1); setenv("DM_ROMFS", romfs, 1); setenv("DM_NVN_SHMAP", shmap, 1);
        setenv("DM_MSL_DIR", paths[2], 1); setenv("DM_ROOT", paths[3], 1); setenv("DM_SAVE", save, 1);
        setenv("DM_TEMP", temp, 1); setenv("DM_AOC_DIR", aoc, 1); setenv("DM_PSO_ARCHIVE", archive, 1);
        if (getenv("SATCHEL_SOAK_CAPTURE")) {
            char capture[4096]; snprintf(capture, sizeof capture, "%s/capture", paths[3]);
            setenv("DM_NVN_SHDUMP", capture, 1);
        }
        char ctl[4096]; snprintf(ctl, sizeof ctl, "%s/ctl.txt", paths[3]);
        FILE *control = fopen(ctl, "w"); if (!control) return 65; fclose(control);
        setenv("DM_CTL", ctl, 1);
        if (chdir(paths[3])) return 65;
        snapshot("cycle", cycle);
        char *game[] = {argv[0], paths[0], NULL};
        int result = run_session(2, game); if (result) return result;
    }
    if (getenv("SATCHEL_SOAK_LEAK_CHECK")) {
        // The runner scans only after the last session has fully retired.
        uint64_t end = now_ns() + UINT64_C(45000000000);
        while (now_ns() < end) {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, false); usleep(10000);
        }
    }
    return 0;
}
