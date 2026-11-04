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
#include <signal.h>
#include <execinfo.h>
#include <dlfcn.h>
#include <libproc.h>

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
    fflush(stdout);
}
void satchel_session_phase(const char *phase, int result) { snapshot(phase, result); }
void satchel_retire_checkpoint(unsigned stage) { snapshot("retire-checkpoint", (int)stage); }
static void host_fault(int sig) {
    void *frames[32]; int n = backtrace(frames, 32);
    for (int i = 0; i < n; i++) {
        Dl_info info = {0};
        if (dladdr(frames[i], &info) && info.dli_sname && info.dli_fname &&
            (strstr(info.dli_fname, "mac-stop-probe") || strstr(info.dli_fname, "/System/Library/") || strstr(info.dli_fname, "/usr/lib/")))
            fprintf(stdout, "[session-host-stack] %s\n", info.dli_sname);
    }
    fflush(stdout); _exit(128 + sig);
}
static int run_session(int argc, char **argv) {
    if (argc != 2) return 64;
    int result = satchel_host_session_prepare(); snapshot("begin", result); if (result) return 1;
    satchel_host_session_load(argc, argv); snapshot("loaded", 0);
    void satchel_memstat_start(void); satchel_memstat_start();
    signal(SIGTRAP, host_fault); signal(SIGABRT, host_fault); signal(SIGSEGV, host_fault);
    pthread_attr_t attr; pthread_attr_init(&attr); pthread_attr_setstacksize(&attr, 64u << 20);
    pthread_t guest;
    result = satchel_session_pthread_create(&guest, &attr, satchel_host_session_enter, NULL);
    pthread_attr_destroy(&attr); snapshot("started", result); if (result) return 1;
    for (int i = 0; i < 6; i++) {
        uint64_t deadline = now_ns() + UINT64_C(5000000000);
        while (now_ns() < deadline) {
            CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.01, false); usleep(10000);
        }
        snapshot("running", 0);
    }
    uint8_t *arena; size_t arena_size;
    void svc_guest_range(uint8_t **, size_t *); svc_guest_range(&arena, &arena_size);
    assert(arena && arena_size >= (UINT64_C(4) << 30));
    result = satchel_host_session_stop(); if (result) return result;
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
    if (argc == 2) return run_session(argc, argv);
    if (argc < 5 || (argc - 1) % 4) return 64;
    snapshot("cold", 0);
    for (int cycle = 0; cycle < (argc - 1) / 4; cycle++) {
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
        if (chdir(paths[3])) return 65;
        snapshot("cycle", cycle);
        char *game[] = {argv[0], paths[0], NULL};
        int result = run_session(2, game); if (result) return result;
    }
    return 0;
}
