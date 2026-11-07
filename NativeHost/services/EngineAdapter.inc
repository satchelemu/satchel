// Satchel native HIPC transport adapter. No guest payload is logged.
#include <mach/mach.h>
#include <mach/vm_map.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <time.h>
#include "ServiceIPC.inc"
extern uint32_t satchel_host_time_control(unsigned command, const uint8_t *in, uint8_t *out);
static uint32_t satchel_time_control(unsigned c, const uint8_t *in, uint8_t *out) { return satchel_host_time_control(c, in, out); }
extern uint32_t satchel_host_clock_change(unsigned kind, unsigned cmd, const uint8_t *in, uint8_t *out);
static uint32_t satchel_clock_change(unsigned k, unsigned c, const uint8_t *in, uint8_t *out) { return satchel_host_clock_change(k, c, in, out); }
extern uint32_t satchel_host_fs_open(unsigned type, uint32_t *index);
extern uint32_t satchel_host_fs_query(uint32_t index, unsigned command, const char *path, uint8_t *out);
static uint32_t satchel_fs_open(unsigned type, uint32_t *index) { return satchel_host_fs_open(type, index); }
static uint32_t satchel_fs_query(uint32_t i, unsigned c, const char *p, uint8_t *out) { return satchel_host_fs_query(i, c, p, out); }
static int64_t satchel_clock_rtc(void) { return time(NULL); }
// Share the SDK clock implementation, including fixed-time mode and stable contexts.
extern uint32_t satchel_host_clock_now(int64_t *out);
extern uint32_t satchel_host_clock_context(uint8_t *out);
extern uint32_t satchel_host_clock_steady(uint8_t *out);
static uint32_t satchel_clock_now(int64_t *out) { return satchel_host_clock_now(out); }
static uint32_t satchel_clock_context(uint8_t *out) { return satchel_host_clock_context(out); }
static uint32_t satchel_clock_steady(uint8_t *out) { return satchel_host_clock_steady(out); }
extern uint32_t satchel_host_event_create(int signaled);
extern uint32_t satchel_host_event_copy(uint32_t handle);
extern void satchel_host_event_close(uint32_t handle);
extern unsigned satchel_host_aoc_list(uint32_t *out, unsigned start, unsigned count);
extern uint64_t satchel_host_program_id(void);
extern void satchel_host_event_set(uint32_t handle, int signaled);
static void satchel_event_set(uint32_t h, int s) { satchel_host_event_set(h, s); }
static int satchel_ipc_read(uint64_t address, void *bytes, size_t size) {
    vm_size_t got = 0;
    return vm_read_overwrite(mach_task_self(), address, size, (vm_address_t)bytes, &got) == KERN_SUCCESS && got == size;
}
static uint32_t satchel_event_create(int signaled) { return satchel_host_event_create(signaled); }
static uint32_t satchel_event_copy(uint32_t handle) { return satchel_host_event_copy(handle); }
static void satchel_event_close(uint32_t handle) { satchel_host_event_close(handle); }
static unsigned satchel_aoc_list(uint32_t *out, unsigned start, unsigned count) { return satchel_host_aoc_list(out, start, count); }
static uint64_t satchel_program_id(void) { return satchel_host_program_id(); }
// apm answers the mode of the nn::oe::GetPerformanceMode takeover (hle_oe.c), csrng the engine's entropy
// source (hle_os.c), which is a fixed-seed stream under DM_DETCLOCK.
extern uint64_t dm_entropy64(void);
static unsigned satchel_perf_mode(void) { return dm_profile()->perf_boost ? 1u : (unsigned)dm_opmode(); }
static void satchel_random(void *bytes, size_t size) {
    for (size_t i = 0; i < size; i += 8) { uint64_t v = dm_entropy64(); memcpy((uint8_t *)bytes + i, &v, size - i < 8 ? size - i : 8); }
}
static int satchel_ipc_write(uint64_t address, const void *bytes, size_t size) {
    _Static_assert(sizeof(vm_address_t) == sizeof(address), "IPC buffers require a 64-bit host");
    return vm_write(mach_task_self(), (vm_address_t)address, (vm_offset_t)bytes, (mach_msg_type_number_t)size) == KERN_SUCCESS;
}
static void satchel_safe_name(char *out, size_t capacity, const char *in) {
    size_t i = 0;
    for (; i + 1 < capacity && in[i]; i++) {
        unsigned char c = (unsigned char)in[i];
        out[i] = isalnum(c) || c == '_' || c == ':' || c == '.' || c == '-' ? c : '_';
    }
    out[i] = 0;
}
static void satchel_ipc_emit(const char *service, uint32_t cmd, const char *kind, void *caller) {
    module_t *module = module_of(caller);
    char name[80], source[64], line[384];
    satchel_safe_name(name, sizeof name, service);
    satchel_safe_name(source, sizeof source, module ? module->name : "host");
    uint64_t offset = module ? (uint64_t)((uint8_t *)caller - module->base) : 0;
    snprintf(line, sizeof line, "ServiceIPC service=%s command=%u status=%s caller=%s+0x%llx",
             name, cmd, kind, source, (unsigned long long)offset);
    LOG("%s", line);
    const char *path = getenv("DM_SERVICE_REPORT");
    if (path && *path) {
        int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_NOFOLLOW, 0600);
        if (fd >= 0) { strlcat(line, "\n", sizeof line); (void)write(fd, line, strlen(line)); close(fd); }
    }
}

#include "MissingServices.inc"
