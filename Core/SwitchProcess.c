#include "SwitchProcess.h"
#include "HorizonIPC.h"
#include "SwitchStorageIPC.h"
#include "SwitchResources.h"
#include "SwitchEvents.h"
#include <stdio.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

/* Independent implementation of published loader/SVC layout, not copied OS
 * code. Source anchors and unsupported process features: PHONE_RUNTIME.md. */
enum { MAX_MODULES = SATCHEL_SWITCH_PROCESS_MODULE_LIMIT, MAX_MAPS = 48, MAX_REGIONS = 28 };
static const uint64_t code_base = 0x08000000, legacy_stack_base = 0x30000000, tls_base = 0x31000000;
typedef struct { uint64_t address, size; } ProcessRegion;
enum { HANDLE_EMPTY, HANDLE_THREAD, HANDLE_SM_SESSION, HANDLE_LM_SESSION, HANDLE_LOGGER, HANDLE_FSP_SESSION, HANDLE_STORAGE, HANDLE_APM_ROOT, HANDLE_APM_SESSION,
       HANDLE_APPLET_ROOT, HANDLE_APPLICATION_PROXY, HANDLE_COMMON_STATE,
       HANDLE_SELF_CONTROLLER, HANDLE_WINDOW_CONTROLLER, HANDLE_AUDIO_CONTROLLER,
       HANDLE_DISPLAY_CONTROLLER, HANDLE_LIBRARY_APPLET_CREATOR,
       HANDLE_APPLICATION_FUNCTIONS, HANDLE_DEBUG_FUNCTIONS, HANDLE_READABLE_EVENT, HANDLE_AOC_ROOT, HANDLE_ECTX_APPLICATION, HANDLE_PCTL_ROOT, HANDLE_PCTL_CHILD, HANDLE_PCTL_SYSTEM_ROOT, HANDLE_PCTL_SYSTEM_CHILD, HANDLE_PCTL_ADMIN_ROOT, HANDLE_PCTL_ADMIN_CHILD, HANDLE_PCTL_READONLY_ROOT, HANDLE_PCTL_READONLY_CHILD, HANDLE_TIME_USER_ROOT, HANDLE_TIME_USER_CLOCK, HANDLE_TIME_NETWORK_CLOCK, HANDLE_TIME_STEADY_CLOCK, HANDLE_TIME_ZONE, HANDLE_TIME_LOCAL_CLOCK, HANDLE_TIME_SHARED_MEMORY };
typedef struct { uint32_t token; unsigned type; uint64_t client_process_id; uint32_t domain_slot; uint64_t event_id; uint32_t service_slot; } ProcessHandle;
typedef struct { uint32_t id, type; uint64_t client_process_id; uint32_t service_slot; } DomainObject;
// Domain clones reference this owned table rather than copying mutable child
// state. The final endpoint releases each registered object reference exactly once.
typedef struct {
    uint32_t references, next_id;
    DomainObject objects[SATCHEL_SWITCH_DOMAIN_OBJECT_LIMIT];
} ProcessDomain;
typedef struct {
    uint32_t references, capabilities, initialized, metadata_bound;
    uint64_t client_process_id;
    SatchelSwitchApplicationControl metadata;
} ProcessParentalChild;
// Guest thread identities have their own monotonically allocated namespace;
// they are neither host thread identifiers nor the 32-bit handle-table token.
static _Atomic(uint64_t) next_thread_id = UINT64_C(0x100000000);
static _Atomic(uint64_t) next_identity_generation = 1;
static _Atomic(uint64_t) next_process_id = UINT64_C(0x1000);
static _Atomic(uint64_t) next_applet_resource_id = UINT64_C(0x1000000000000000);
typedef struct { unsigned kind; uint64_t id; } ProcessWaitObject;
typedef struct {
    SatchelCPUProbe *cpu;
    uint32_t handle; uint8_t priority, cpu_id, effective_priority;
    uint64_t id, affinity_mask, stack_address, stack_size, tls_address, instructions;
    int32_t ideal_core_id;
    unsigned state, handle_references, wait_references, run_reference;
    int wait_cancelled;
    uint64_t deadline;
    uint64_t mutex_owner, mutex_owner_reference, mutex_address, mutex_order;
    uint32_t mutex_tag;
    size_t wait_count;
    ProcessWaitObject wait[SATCHEL_SWITCH_THREAD_WAIT_LIMIT];
} ProcessThread;
struct SatchelSwitchProcess {
    SatchelCPUProbe *cpu;
    SatchelSwitchIdentityReservation identity;
    int identity_claimed;
    SatchelSwitchMemoryInfo *maps;
    SatchelSwitchMemoryInfo fallback_maps[MAX_MAPS];
    SatchelSwitchResources *resources;
    size_t map_count, module_count;
    uint64_t module_addresses[MAX_MODULES], address_space_size, queries;
    uint64_t random_entropy[4], info_requests, info_serviced, last_info_sub_id;
    uint64_t thread_queries, alias_region_extra_size;
    uint64_t allowed_core_mask, allowed_priority_mask;
    uint64_t user_memory_limit, user_memory_used, heap_size, heap_requests;
    uint64_t physical_size, physical_map_requests, physical_unmap_requests, physical_map_serviced, physical_unmap_serviced;
    uint64_t alias_size, alias_map_requests, alias_unmap_requests, alias_map_serviced, alias_unmap_serviced;
    uint64_t permission_requests, permission_serviced, permission_changed_bytes;
    uint64_t attribute_requests, attribute_serviced, permission_locked_bytes;
    int has_core_priority;
    ProcessHandle handles[SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT];
    ProcessDomain domains[SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT];
    ProcessParentalChild parental_children[SATCHEL_SWITCH_PARENTAL_CHILD_LIMIT];
    uint64_t parental_created, parental_initialize_requests, parental_initialize_serviced;
    uint32_t next_handle, closed_session_mask;
    uint64_t session_closes;
    uint64_t port_requests, process_id, applet_resource_id, ipc_requests, ipc_serviced;
    SatchelSwitchIPCInfo last_ipc;
    int has_last_ipc;
    struct { uint8_t bytes[SATCHEL_SWITCH_LOG_PACKET_LIMIT]; size_t size; } log_packets[SATCHEL_SWITCH_LOG_PACKET_SLOTS];
    size_t log_next;
    uint64_t log_count, log_retained_bytes, log_dropped;
    uint32_t log_destination, performance_mode;
    int exit_to_library_on_execute_next;
    SatchelSwitchEvents *events;
    uint64_t applet_message_event, suspended_tick_event, suspended_ticks, gpu_error_event;
    uint32_t applet_phase, applet_messages[SATCHEL_SWITCH_APPLET_MESSAGE_LIMIT], applet_message_head, applet_message_count;
    uint64_t applet_messages_enqueued, applet_messages_received;
    int gpu_error_detected;
    SatchelSwitchAppletState applet;
    SatchelSwitchStorage storage;
    int has_storage, has_run;
    SatchelSwitchTimeSeed time_seed;
    SatchelSwitchTimeInfo time_info;
    uint64_t time_shared_token;
    int64_t time_base_ns, time_offset, time_seed_steady;
    uint8_t time_source_id[16];
    uint64_t program_id, control_attachments;
    SatchelSwitchApplicationControl application_control;
    uint64_t storage_reads, storage_bytes;
    uint32_t last_info_type, last_info_handle;
    ProcessRegion aslr_region, kernel_region, stack_region, alias_region, heap_region, dispatch_region;
    ProcessThread threads[SATCHEL_SWITCH_THREAD_LIMIT];
    unsigned thread_count, active_thread, schedule_core_cursor, schedule_cursor[4];
    uint32_t tls_pages; uint8_t initial_cpu_id;
    int scheduler_switch;
    SatchelCPUProbeResult last_thread_stop;
    uint64_t last_thread_stop_id;
    uint64_t mutex_sequence, mutex_lock_requests, mutex_unlock_requests, mutex_handoffs, mutex_waits;
};
_Static_assert(SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT <= 32, "Bounded session endpoint mask");
_Static_assert(sizeof(SatchelSwitchIdentityReservation)==40, "Opaque identity descriptor ABI");
_Static_assert(sizeof(SatchelSwitchTimeSeed)==64, "Host time seed ABI");
_Static_assert(sizeof(SatchelSwitchMemoryInfo) == 40, "Horizon MemoryInfo ABI");
_Static_assert(MAX_MAPS * sizeof(SatchelSwitchMemoryInfo) <= 4096, "Bounded map metadata fits one resource page");
static uint64_t page_up(uint64_t n) { return (n + 4095) & ~UINT64_C(4095); }
/* Published19.2MHz timeline: overflow-checked floor(ticks*625/12)ns.
 * Signed floor division is required when the sampled subsecond phase is less
 * than the process counter phase. No host clock is read or changed here. */
static int time_tick_ns(uint64_t tick, int64_t *out) {
    uint64_t quotient=tick/12, remainder=tick%12;
    if(quotient>(uint64_t)INT64_MAX/625) return 0;
    uint64_t value=quotient*625+remainder*625/12;
    if(value>INT64_MAX) return 0;
    *out=(int64_t)value; return 1;
}
static int64_t time_floor_seconds(int64_t ns) {
    int64_t result=ns/INT64_C(1000000000);
    return result-(ns%INT64_C(1000000000)<0);
}
static int time_values(int64_t base, int64_t offset, uint64_t tick, int64_t *steady, int64_t *wall) {
    int64_t ns, phased;
    if(!time_tick_ns(tick,&ns) || __builtin_add_overflow(ns,base,&phased)) return 0;
    int64_t value=time_floor_seconds(phased), result;
    if(__builtin_add_overflow(value,offset,&result)) return 0;
    *steady=value; *wall=result; return 1;
}
static void time_word64(uint8_t *out, uint64_t value) {
    for(unsigned i=0;i<8;i++) out[i]=(uint8_t)(value>>(8*i));
}
static void time_context(const SatchelSwitchProcess *p, uint8_t out[32]) {
    time_word64(out,(uint64_t)p->time_offset); time_word64(out+8,(uint64_t)p->time_seed_steady);
    memcpy(out+16,p->time_source_id,16);
}
#if defined(SATCHEL_CPU_TESTING)
int satchel_switch_process_test_time_values(int64_t base,int64_t offset,uint64_t tick,int64_t *steady,int64_t *wall) {
    return time_values(base,offset,tick,steady,wall);
}
#endif
static uint64_t allocate_thread_id(void) {
    uint64_t value = atomic_load_explicit(&next_thread_id, memory_order_relaxed);
    while (value != UINT64_MAX) {
        if (atomic_compare_exchange_weak_explicit(&next_thread_id, &value, value + 1,
                memory_order_relaxed, memory_order_relaxed)) return value;
    }
    return 0; // Exhaustion cannot wrap and reuse a live identity.
}
static uint64_t allocate_identity_generation(void) {
    uint64_t value = atomic_load_explicit(&next_identity_generation, memory_order_relaxed);
    while (value != UINT64_MAX) {
        if (atomic_compare_exchange_weak_explicit(&next_identity_generation, &value, value + 1,
                memory_order_relaxed, memory_order_relaxed)) return value;
    }
    return 0;
}
static uint64_t allocate_process_id(void) {
    uint64_t value = atomic_load_explicit(&next_process_id, memory_order_relaxed);
    while (value != UINT64_MAX) {
        if (atomic_compare_exchange_weak_explicit(&next_process_id, &value, value + 1,
                memory_order_relaxed, memory_order_relaxed)) return value;
    }
    return 0;
}
static uint64_t allocate_applet_resource_id(void) {
    uint64_t value = atomic_load_explicit(&next_applet_resource_id, memory_order_relaxed);
    while (value != UINT64_MAX) {
        if (atomic_compare_exchange_weak_explicit(&next_applet_resource_id, &value, value + 1,
                memory_order_relaxed, memory_order_relaxed)) return value;
    }
    return 0;
}
static void message(char *error, size_t capacity, const char *text) {
    if (error && capacity) snprintf(error, capacity, "%s", text);
}
static int module_order(const char *name) {
    if (!name) return -1;
    if (!strcmp(name, "rtld")) return 0;
    if (!strcmp(name, "main")) return 1;
    if (!strcmp(name, "sdk")) return 12;
    if (strlen(name) == 7 && !memcmp(name, "subsdk", 6) && name[6] >= '0' && name[6] <= '9') return 2 + name[6] - '0';
    return -1;
}
static void map(SatchelSwitchProcess *p, uint64_t address, uint64_t size, uint32_t state, uint32_t permissions) {
    if (size) p->maps[p->map_count++] = (SatchelSwitchMemoryInfo){address, size, state, 0, permissions, 0, 0, 0};
}
static int contains(ProcessRegion region, uint64_t address, uint64_t size) {
    return address >= region.address && address - region.address <= region.size &&
        size <= region.size - (address - region.address);
}
static int layout(SatchelSwitchProcess *p, uint8_t address_bits, uint64_t code_end) {
    const uint64_t gib = UINT64_C(1) << 30;
    // Address-sanitizer layouts would add address-space/8 to the alias region.
    // This profile rejects that NPDM flag, so its owned extra reservation is 0.
    // Atmosphere 6b0c077b kern_k_page_table_base.cpp InitializeForProcess.
    p->alias_region_extra_size = 0;
    p->aslr_region = (ProcessRegion){code_base, p->address_space_size - code_base};
    // Independent implementation of published virtual-region sizes and the
    // non-ASLR placement contract, not copies of the kernel implementation.
    // Atmosphere 6b0c077b: svc_memory_map.hpp and
    // kern_k_page_table_base.cpp InitializeForProcess, RegionAlignment=2MiB.
    if (address_bits == 39) {
        uint64_t start = (code_end + 0x1FFFFF) & ~UINT64_C(0x1FFFFF);
        p->kernel_region = (ProcessRegion){start, 64 * gib};
        p->stack_region = (ProcessRegion){start + 64 * gib, 2 * gib};
        p->alias_region = (ProcessRegion){start + 66 * gib, 64 * gib};
        p->heap_region = (ProcessRegion){start + 130 * gib, 8 * gib};
        p->threads[0].stack_address = p->stack_region.address;
    } else if (address_bits == 36) {
        p->kernel_region = (ProcessRegion){code_base, 2 * gib - code_base};
        p->stack_region = p->kernel_region;
        p->alias_region = (ProcessRegion){2 * gib, 6 * gib};
        p->heap_region = (ProcessRegion){8 * gib, 8 * gib};
        p->threads[0].stack_address = legacy_stack_base;
        if (code_end > legacy_stack_base) return 0;
    } else {
        return 0;
    }
    ProcessRegion address_space = {0, p->address_space_size};
    return contains(address_space, p->heap_region.address, p->heap_region.size) &&
        contains(p->kernel_region, tls_base, 4096) &&
        contains(p->stack_region, p->threads[0].stack_address, p->threads[0].stack_size) &&
        code_end <= tls_base;
}
static int contiguous_mask(uint64_t mask) {
    if (!mask) return 0;
    while (!(mask & 1)) mask >>= 1;
    return !(mask & (mask + 1));
}
static const ProcessHandle *lookup_handle(const SatchelSwitchProcess *p, uint32_t token) {
    if (!p || !token) return NULL;
    for (size_t i = 0; i < SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT; i++)
        if (p->handles[i].type != HANDLE_EMPTY && p->handles[i].token == token) return p->handles + i;
    return NULL;
}
static void reap_threads(SatchelSwitchProcess *p);
static int refresh_waits(SatchelSwitchProcess *p);
static uint32_t connect_named_port(SatchelSwitchProcess *p, uint64_t address, uint32_t *out) {
    // Primary ABI: Atmosphere pin above, kern_svc_port.cpp GetPortName and
    // ConnectToNamedPort; KObjectName::NameLengthMax=12. No names are logged.
    p->port_requests++;
    char name[12] = {0};
    size_t length = 0;
    for (; length < sizeof(name); length++) {
        if (address > UINT64_MAX - length ||
            !satchel_cpu_probe_read(p->cpu, address + length, name + length, 1)) return 0xE601;
        if (!name[length]) break;
    }
    if (length == sizeof(name)) return 0xEE01;
    if (length != 3 || memcmp(name, "sm:", 3)) return 0xF201;
    // The SM endpoint exists as an emulated owned port; service commands are
    // deliberately absent. A session is a real typed object in this namespace.
    size_t slot = 0;
    while (slot < SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT && p->handles[slot].type != HANDLE_EMPTY) slot++;
    if (slot == SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT || p->next_handle >= 0xFFFF8000) return 0xD201;
    uint32_t token = p->next_handle;
    p->handles[slot] = (ProcessHandle){token, HANDLE_SM_SESSION, 0, 0, 0, 0};
    p->next_handle++;
    *out = token;
    return 0;
}
static void parental_release(SatchelSwitchProcess *p, uint32_t slot) {
    if (!slot || slot > SATCHEL_SWITCH_PARENTAL_CHILD_LIMIT) return;
    ProcessParentalChild *child = p->parental_children + slot - 1;
    if (child->references && --child->references == 0) memset(child, 0, sizeof(*child));
}
static void domain_release(SatchelSwitchProcess *p, uint32_t slot) {
    ProcessDomain *domain = p->domains + slot - 1;
    if (domain->references && --domain->references == 0) {
        for (size_t i = 0; i < SATCHEL_SWITCH_DOMAIN_OBJECT_LIMIT; i++) parental_release(p, domain->objects[i].service_slot);
        memset(domain, 0, sizeof(*domain));
    }
}
static uint32_t close_handle(SatchelSwitchProcess *p, uint32_t token) {
    // Pseudo handles are object lookup conveniences, never closable references.
    // Removal invalidates only this token. Its numeric value is never recycled.
    const ProcessHandle *entry = lookup_handle(p, token);
    if (!entry) return 0xE401;
    if (entry->type == HANDLE_THREAD) {
        if (entry->event_id >= p->thread_count || !p->threads[entry->event_id].handle_references) return 0xe401;
        p->threads[entry->event_id].handle_references--;
    }
    if (entry->type == HANDLE_READABLE_EVENT &&
        satchel_switch_event_release(p->events, entry->event_id) != SATCHEL_SWITCH_EVENT_OK) return 0xE401;
    if (entry->domain_slot) {
        domain_release(p, entry->domain_slot);
    }
    parental_release(p, entry->service_slot);
    size_t slot = (size_t)(entry - p->handles);
    p->closed_session_mask &= ~(UINT32_C(1) << slot);
    p->handles[slot] = (ProcessHandle){0};
    reap_threads(p); return 0;
}
static uint32_t ipc_read_word(const uint8_t *bytes) {
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 | (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}
// Diagnostic allowlist only: none of these labels creates a service or grants
// access. Public endpoint names: libnx b156b477 nx/source/services/{csrng,psc,
// spl,fatal,ro,apm,acc,ns,bpc,audren,audout,audin,nifm,bsd,ssl,pl,pctl,friends,
// sfdnsres}.c; Atmosphere6b0c077b lm_service_name.hpp; Switchbrew PPC_services
// revision12421 for legacyapm:p/currentapm:am/apm:sys andfgm. Additional exact
// names: libnx b156b477 services/{nv,mm,capsu,capsa,capsc,capsdc,capssc,capssu}.c;
// original Ryujinx mirrored at05b4bd8c {Psc,Prepo}IpcServer.cs registration tables.
// Broader endpoint catalog: literal libnx b156b4774fb2df7b93402169509266da7547fc4c
// nx/source/services requests (including explicit pm/ldr macro expansions), and
// original Ryujinx 05b4bd8c61b9ee9b1d471f0ccf5309d9f478dcba HLE Service
// attributes / Horizon IpcServer registrations. Public names are ABI facts; no
// reference implementation code is copied. Diagnostic IDs remain uint32_t.
// Every alias has its own diagnostic label; no guest text is ever returned.
static const struct { char name[9]; uint32_t kind; } known_services[] = {
    {"fsp-srv", SATCHEL_SWITCH_IPC_SERVICE_FSP}, {"nvdrv", SATCHEL_SWITCH_IPC_SERVICE_NVDRV},
    {"nvdrv:a", SATCHEL_SWITCH_IPC_SERVICE_NVDRV_A}, {"hid", SATCHEL_SWITCH_IPC_SERVICE_HID},
    {"appletOE", SATCHEL_SWITCH_IPC_SERVICE_APPLET}, {"appletAE", SATCHEL_SWITCH_IPC_SERVICE_APPLET_AE},
    {"vi:u", SATCHEL_SWITCH_IPC_SERVICE_VI}, {"vi:m", SATCHEL_SWITCH_IPC_SERVICE_VI_M},
    {"set", SATCHEL_SWITCH_IPC_SERVICE_SET}, {"set:sys", SATCHEL_SWITCH_IPC_SERVICE_SET_SYS},
    {"time:u", SATCHEL_SWITCH_IPC_SERVICE_TIME}, {"time:s", SATCHEL_SWITCH_IPC_SERVICE_TIME_S},
    {"csrng", SATCHEL_SWITCH_IPC_SERVICE_CSRNG}, {"psc:m", SATCHEL_SWITCH_IPC_SERVICE_PSC_M},
    {"lm", SATCHEL_SWITCH_IPC_SERVICE_LM}, {"spl:", SATCHEL_SWITCH_IPC_SERVICE_SPL},
    {"spl:mig", SATCHEL_SWITCH_IPC_SERVICE_SPL_MIG}, {"spl:ssl", SATCHEL_SWITCH_IPC_SERVICE_SPL_SSL},
    {"spl:fs", SATCHEL_SWITCH_IPC_SERVICE_SPL_FS}, {"spl:es", SATCHEL_SWITCH_IPC_SERVICE_SPL_ES},
    {"spl:manu", SATCHEL_SWITCH_IPC_SERVICE_SPL_MANU}, {"fatal:u", SATCHEL_SWITCH_IPC_SERVICE_FATAL},
    {"ldr:ro", SATCHEL_SWITCH_IPC_SERVICE_LDR_RO}, {"ro:1", SATCHEL_SWITCH_IPC_SERVICE_RO_1},
    {"apm", SATCHEL_SWITCH_IPC_SERVICE_APM}, {"apm:p", SATCHEL_SWITCH_IPC_SERVICE_APM_P},
    {"acc:u0", SATCHEL_SWITCH_IPC_SERVICE_ACC_U0}, {"ns:am", SATCHEL_SWITCH_IPC_SERVICE_NS_AM},
    {"ns:am2", SATCHEL_SWITCH_IPC_SERVICE_NS_AM2}, {"bpc", SATCHEL_SWITCH_IPC_SERVICE_BPC},
    {"bpc:c", SATCHEL_SWITCH_IPC_SERVICE_BPC_C}, {"audren:u", SATCHEL_SWITCH_IPC_SERVICE_AUDREN},
    {"audout:u", SATCHEL_SWITCH_IPC_SERVICE_AUDOUT}, {"audin:u", SATCHEL_SWITCH_IPC_SERVICE_AUDIN},
    {"nifm:u", SATCHEL_SWITCH_IPC_SERVICE_NIFM}, {"nifm:s", SATCHEL_SWITCH_IPC_SERVICE_NIFM_S},
    {"nifm:a", SATCHEL_SWITCH_IPC_SERVICE_NIFM_A}, {"bsd:u", SATCHEL_SWITCH_IPC_SERVICE_BSD},
    {"bsd:s", SATCHEL_SWITCH_IPC_SERVICE_BSD_S}, {"ssl", SATCHEL_SWITCH_IPC_SERVICE_SSL},
    {"ssl:s", SATCHEL_SWITCH_IPC_SERVICE_SSL_S}, {"pl:u", SATCHEL_SWITCH_IPC_SERVICE_PL},
    {"pl:s", SATCHEL_SWITCH_IPC_SERVICE_PL_S}, {"pctl", SATCHEL_SWITCH_IPC_SERVICE_PCTL},
    {"pctl:a", SATCHEL_SWITCH_IPC_SERVICE_PCTL_A}, {"pctl:s", SATCHEL_SWITCH_IPC_SERVICE_PCTL_S},
    {"pctl:r", SATCHEL_SWITCH_IPC_SERVICE_PCTL_R}, {"friend:u", SATCHEL_SWITCH_IPC_SERVICE_FRIENDS},
    {"friend:s", SATCHEL_SWITCH_IPC_SERVICE_FRIENDS_S}, {"sfdnsres", SATCHEL_SWITCH_IPC_SERVICE_SFDNSRES},
    {"apm:am", SATCHEL_SWITCH_IPC_SERVICE_APM_AM}, {"apm:sys", SATCHEL_SWITCH_IPC_SERVICE_APM_SYS},
    {"fgm", SATCHEL_SWITCH_IPC_SERVICE_FGM}, {"fgm:0", SATCHEL_SWITCH_IPC_SERVICE_FGM_0},
    {"fgm:9", SATCHEL_SWITCH_IPC_SERVICE_FGM_9},
    {"nvdrv:s", SATCHEL_SWITCH_IPC_SERVICE_NVDRV_S},
    {"nvdrv:t", SATCHEL_SWITCH_IPC_SERVICE_NVDRV_T},
    {"mm:u", SATCHEL_SWITCH_IPC_SERVICE_MM_U},
    {"caps:u", SATCHEL_SWITCH_IPC_SERVICE_CAPS_U},
    {"caps:a", SATCHEL_SWITCH_IPC_SERVICE_CAPS_A},
    {"caps:c", SATCHEL_SWITCH_IPC_SERVICE_CAPS_C},
    {"caps:dc", SATCHEL_SWITCH_IPC_SERVICE_CAPS_DC},
    {"caps:sc", SATCHEL_SWITCH_IPC_SERVICE_CAPS_SC},
    {"caps:su", SATCHEL_SWITCH_IPC_SERVICE_CAPS_SU},
    {"prepo:u", SATCHEL_SWITCH_IPC_SERVICE_PREPO_U},
    {"prepo:s", SATCHEL_SWITCH_IPC_SERVICE_PREPO_S},
    {"prepo:m", SATCHEL_SWITCH_IPC_SERVICE_PREPO_M},
    {"prepo:a", SATCHEL_SWITCH_IPC_SERVICE_PREPO_A},
    {"prepo:a2", SATCHEL_SWITCH_IPC_SERVICE_PREPO_A2},
    {"prepo:d", SATCHEL_SWITCH_IPC_SERVICE_PREPO_D},
    {"psc:c", SATCHEL_SWITCH_IPC_SERVICE_PSC_C},
    {"psc:l", SATCHEL_SWITCH_IPC_SERVICE_PSC_L},
    {"acc:aa", SATCHEL_SWITCH_IPC_SERVICE_ACC_AA},
    {"acc:su", SATCHEL_SWITCH_IPC_SERVICE_ACC_SU},
    {"acc:u1", SATCHEL_SWITCH_IPC_SERVICE_ACC_U1},
    {"aoc:u", SATCHEL_SWITCH_IPC_SERVICE_AOC_U},
    {"arp:r", SATCHEL_SWITCH_IPC_SERVICE_ARP_R},
    {"arp:w", SATCHEL_SWITCH_IPC_SERVICE_ARP_W},
    {"aud:a", SATCHEL_SWITCH_IPC_SERVICE_AUD_A},
    {"aud:d", SATCHEL_SWITCH_IPC_SERVICE_AUD_D},
    {"audctl", SATCHEL_SWITCH_IPC_SERVICE_AUDCTL},
    {"audout:a", SATCHEL_SWITCH_IPC_SERVICE_AUDOUT_A},
    {"audout:d", SATCHEL_SWITCH_IPC_SERVICE_AUDOUT_D},
    {"audrec:u", SATCHEL_SWITCH_IPC_SERVICE_AUDREC_U},
    {"avm", SATCHEL_SWITCH_IPC_SERVICE_AVM},
    {"bcat:a", SATCHEL_SWITCH_IPC_SERVICE_BCAT_A},
    {"bcat:m", SATCHEL_SWITCH_IPC_SERVICE_BCAT_M},
    {"bcat:s", SATCHEL_SWITCH_IPC_SERVICE_BCAT_S},
    {"bcat:u", SATCHEL_SWITCH_IPC_SERVICE_BCAT_U},
    {"bgtc:sc", SATCHEL_SWITCH_IPC_SERVICE_BGTC_SC},
    {"bgtc:t", SATCHEL_SWITCH_IPC_SERVICE_BGTC_T},
    {"bpc:r", SATCHEL_SWITCH_IPC_SERVICE_BPC_R},
    {"bsdcfg", SATCHEL_SWITCH_IPC_SERVICE_BSDCFG},
    {"bt", SATCHEL_SWITCH_IPC_SERVICE_BT},
    {"btdrv", SATCHEL_SWITCH_IPC_SERVICE_BTDRV},
    {"btm", SATCHEL_SWITCH_IPC_SERVICE_BTM},
    {"btm:dbg", SATCHEL_SWITCH_IPC_SERVICE_BTM_DBG},
    {"btm:sys", SATCHEL_SWITCH_IPC_SERVICE_BTM_SYS},
    {"btm:u", SATCHEL_SWITCH_IPC_SERVICE_BTM_U},
    {"capmtp", SATCHEL_SWITCH_IPC_SERVICE_CAPMTP},
    {"caps:ss", SATCHEL_SWITCH_IPC_SERVICE_CAPS_SS},
    {"cec-mgr", SATCHEL_SWITCH_IPC_SERVICE_CEC_MGR},
    {"clkrst", SATCHEL_SWITCH_IPC_SERVICE_CLKRST},
    {"clkrst:a", SATCHEL_SWITCH_IPC_SERVICE_CLKRST_A},
    {"clkrst:i", SATCHEL_SWITCH_IPC_SERVICE_CLKRST_I},
    {"dauth:0", SATCHEL_SWITCH_IPC_SERVICE_DAUTH_0},
    {"ectx:aw", SATCHEL_SWITCH_IPC_SERVICE_ECTX_AW},
    {"ectx:r", SATCHEL_SWITCH_IPC_SERVICE_ECTX_R},
    {"ectx:w", SATCHEL_SWITCH_IPC_SERVICE_ECTX_W},
    {"erpt:c", SATCHEL_SWITCH_IPC_SERVICE_ERPT_C},
    {"erpt:r", SATCHEL_SWITCH_IPC_SERVICE_ERPT_R},
    {"es", SATCHEL_SWITCH_IPC_SERVICE_ES},
    {"ethc:c", SATCHEL_SWITCH_IPC_SERVICE_ETHC_C},
    {"ethc:i", SATCHEL_SWITCH_IPC_SERVICE_ETHC_I},
    {"eupld:c", SATCHEL_SWITCH_IPC_SERVICE_EUPLD_C},
    {"eupld:r", SATCHEL_SWITCH_IPC_SERVICE_EUPLD_R},
    {"fan", SATCHEL_SWITCH_IPC_SERVICE_FAN},
    {"fatal:p", SATCHEL_SWITCH_IPC_SERVICE_FATAL_P},
    {"fgm:dbg", SATCHEL_SWITCH_IPC_SERVICE_FGM_DBG},
    {"friend:a", SATCHEL_SWITCH_IPC_SERVICE_FRIEND_A},
    {"friend:m", SATCHEL_SWITCH_IPC_SERVICE_FRIEND_M},
    {"friend:v", SATCHEL_SWITCH_IPC_SERVICE_FRIEND_V},
    {"fsp-ldr", SATCHEL_SWITCH_IPC_SERVICE_FSP_LDR},
    {"fsp-pr", SATCHEL_SWITCH_IPC_SERVICE_FSP_PR},
    {"gpio", SATCHEL_SWITCH_IPC_SERVICE_GPIO},
    {"grc:c", SATCHEL_SWITCH_IPC_SERVICE_GRC_C},
    {"grc:d", SATCHEL_SWITCH_IPC_SERVICE_GRC_D},
    {"hid:dbg", SATCHEL_SWITCH_IPC_SERVICE_HID_DBG},
    {"hid:sys", SATCHEL_SWITCH_IPC_SERVICE_HID_SYS},
    {"hidbus", SATCHEL_SWITCH_IPC_SERVICE_HIDBUS},
    {"hshl:set", SATCHEL_SWITCH_IPC_SERVICE_HSHL_SET},
    {"hshl:sys", SATCHEL_SWITCH_IPC_SERVICE_HSHL_SYS},
    {"htcs", SATCHEL_SWITCH_IPC_SERVICE_HTCS},
    {"hwopus", SATCHEL_SWITCH_IPC_SERVICE_HWOPUS},
    {"i2c", SATCHEL_SWITCH_IPC_SERVICE_I2C},
    {"idle:sys", SATCHEL_SWITCH_IPC_SERVICE_IDLE_SYS},
    {"ins:r", SATCHEL_SWITCH_IPC_SERVICE_INS_R},
    {"ins:s", SATCHEL_SWITCH_IPC_SERVICE_INS_S},
    {"irs", SATCHEL_SWITCH_IPC_SERVICE_IRS},
    {"irs:sys", SATCHEL_SWITCH_IPC_SERVICE_IRS_SYS},
    {"lbl", SATCHEL_SWITCH_IPC_SERVICE_LBL},
    {"ldn:m", SATCHEL_SWITCH_IPC_SERVICE_LDN_M},
    {"ldn:s", SATCHEL_SWITCH_IPC_SERVICE_LDN_S},
    {"ldn:u", SATCHEL_SWITCH_IPC_SERVICE_LDN_U},
    {"ldr:dmnt", SATCHEL_SWITCH_IPC_SERVICE_LDR_DMNT},
    {"ldr:pm", SATCHEL_SWITCH_IPC_SERVICE_LDR_PM},
    {"ldr:shel", SATCHEL_SWITCH_IPC_SERVICE_LDR_SHEL},
    {"lp2p:app", SATCHEL_SWITCH_IPC_SERVICE_LP2P_APP},
    {"lp2p:sys", SATCHEL_SWITCH_IPC_SERVICE_LP2P_SYS},
    {"lr", SATCHEL_SWITCH_IPC_SERVICE_LR},
    {"mig:usr", SATCHEL_SWITCH_IPC_SERVICE_MIG_USR},
    {"mii:e", SATCHEL_SWITCH_IPC_SERVICE_MII_E},
    {"mii:u", SATCHEL_SWITCH_IPC_SERVICE_MII_U},
    {"miiimg", SATCHEL_SWITCH_IPC_SERVICE_MIIIMG},
    {"mnpp:app", SATCHEL_SWITCH_IPC_SERVICE_MNPP_APP},
    {"ncm", SATCHEL_SWITCH_IPC_SERVICE_NCM},
    {"news:a", SATCHEL_SWITCH_IPC_SERVICE_NEWS_A},
    {"news:c", SATCHEL_SWITCH_IPC_SERVICE_NEWS_C},
    {"news:m", SATCHEL_SWITCH_IPC_SERVICE_NEWS_M},
    {"news:p", SATCHEL_SWITCH_IPC_SERVICE_NEWS_P},
    {"news:v", SATCHEL_SWITCH_IPC_SERVICE_NEWS_V},
    {"nfc:am", SATCHEL_SWITCH_IPC_SERVICE_NFC_AM},
    {"nfc:mf:u", SATCHEL_SWITCH_IPC_SERVICE_NFC_MF_U},
    {"nfc:sys", SATCHEL_SWITCH_IPC_SERVICE_NFC_SYS},
    {"nfc:user", SATCHEL_SWITCH_IPC_SERVICE_NFC_USER},
    {"nfp:dbg", SATCHEL_SWITCH_IPC_SERVICE_NFP_DBG},
    {"nfp:sys", SATCHEL_SWITCH_IPC_SERVICE_NFP_SYS},
    {"nfp:user", SATCHEL_SWITCH_IPC_SERVICE_NFP_USER},
    {"ngc:u", SATCHEL_SWITCH_IPC_SERVICE_NGC_U},
    {"ngct:s", SATCHEL_SWITCH_IPC_SERVICE_NGCT_S},
    {"ngct:u", SATCHEL_SWITCH_IPC_SERVICE_NGCT_U},
    {"nim", SATCHEL_SWITCH_IPC_SERVICE_NIM},
    {"nim:eca", SATCHEL_SWITCH_IPC_SERVICE_NIM_ECA},
    {"nim:ecas", SATCHEL_SWITCH_IPC_SERVICE_NIM_ECAS},
    {"nim:shp", SATCHEL_SWITCH_IPC_SERVICE_NIM_SHP},
    {"notif:a", SATCHEL_SWITCH_IPC_SERVICE_NOTIF_A},
    {"notif:s", SATCHEL_SWITCH_IPC_SERVICE_NOTIF_S},
    {"npns:s", SATCHEL_SWITCH_IPC_SERVICE_NPNS_S},
    {"npns:u", SATCHEL_SWITCH_IPC_SERVICE_NPNS_U},
    {"ns:dev", SATCHEL_SWITCH_IPC_SERVICE_NS_DEV},
    {"ns:ec", SATCHEL_SWITCH_IPC_SERVICE_NS_EC},
    {"ns:rid", SATCHEL_SWITCH_IPC_SERVICE_NS_RID},
    {"ns:rt", SATCHEL_SWITCH_IPC_SERVICE_NS_RT},
    {"ns:su", SATCHEL_SWITCH_IPC_SERVICE_NS_SU},
    {"ns:vm", SATCHEL_SWITCH_IPC_SERVICE_NS_VM},
    {"ns:web", SATCHEL_SWITCH_IPC_SERVICE_NS_WEB},
    {"nsd:a", SATCHEL_SWITCH_IPC_SERVICE_NSD_A},
    {"nsd:u", SATCHEL_SWITCH_IPC_SERVICE_NSD_U},
    {"ntc", SATCHEL_SWITCH_IPC_SERVICE_NTC},
    {"nvdrvdbg", SATCHEL_SWITCH_IPC_SERVICE_NVDRVDBG},
    {"nvgem:c", SATCHEL_SWITCH_IPC_SERVICE_NVGEM_C},
    {"nvgem:cd", SATCHEL_SWITCH_IPC_SERVICE_NVGEM_CD},
    {"olsc:s", SATCHEL_SWITCH_IPC_SERVICE_OLSC_S},
    {"olsc:u", SATCHEL_SWITCH_IPC_SERVICE_OLSC_U},
    {"omm", SATCHEL_SWITCH_IPC_SERVICE_OMM},
    {"ovln:rcv", SATCHEL_SWITCH_IPC_SERVICE_OVLN_RCV},
    {"ovln:snd", SATCHEL_SWITCH_IPC_SERVICE_OVLN_SND},
    {"pcie", SATCHEL_SWITCH_IPC_SERVICE_PCIE},
    {"pcie:log", SATCHEL_SWITCH_IPC_SERVICE_PCIE_LOG},
    {"pcm", SATCHEL_SWITCH_IPC_SERVICE_PCM},
    {"pcv", SATCHEL_SWITCH_IPC_SERVICE_PCV},
    {"pdm:ntfy", SATCHEL_SWITCH_IPC_SERVICE_PDM_NTFY},
    {"pdm:qry", SATCHEL_SWITCH_IPC_SERVICE_PDM_QRY},
    {"pgl", SATCHEL_SWITCH_IPC_SERVICE_PGL},
    {"pm:bm", SATCHEL_SWITCH_IPC_SERVICE_PM_BM},
    {"pm:dmnt", SATCHEL_SWITCH_IPC_SERVICE_PM_DMNT},
    {"pm:info", SATCHEL_SWITCH_IPC_SERVICE_PM_INFO},
    {"pm:shell", SATCHEL_SWITCH_IPC_SERVICE_PM_SHELL},
    {"psm", SATCHEL_SWITCH_IPC_SERVICE_PSM},
    {"rgltr", SATCHEL_SWITCH_IPC_SERVICE_RGLTR},
    {"ro:dmnt", SATCHEL_SWITCH_IPC_SERVICE_RO_DMNT},
    {"rtc", SATCHEL_SWITCH_IPC_SERVICE_RTC},
    {"set:cal", SATCHEL_SWITCH_IPC_SERVICE_SET_CAL},
    {"set:fd", SATCHEL_SWITCH_IPC_SERVICE_SET_FD},
    {"sm:m", SATCHEL_SWITCH_IPC_SERVICE_SM_M},
    {"spsm", SATCHEL_SWITCH_IPC_SERVICE_SPSM},
    {"srepo:a", SATCHEL_SWITCH_IPC_SERVICE_SREPO_A},
    {"srepo:u", SATCHEL_SWITCH_IPC_SERVICE_SREPO_U},
    {"tc", SATCHEL_SWITCH_IPC_SERVICE_TC},
    {"tcap", SATCHEL_SWITCH_IPC_SERVICE_TCAP},
    {"time:a", SATCHEL_SWITCH_IPC_SERVICE_TIME_A},
    {"time:al", SATCHEL_SWITCH_IPC_SERVICE_TIME_AL},
    {"time:m", SATCHEL_SWITCH_IPC_SERVICE_TIME_M},
    {"time:p", SATCHEL_SWITCH_IPC_SERVICE_TIME_P},
    {"time:r", SATCHEL_SWITCH_IPC_SERVICE_TIME_R},
    {"time:su", SATCHEL_SWITCH_IPC_SERVICE_TIME_SU},
    {"ts", SATCHEL_SWITCH_IPC_SERVICE_TS},
    {"uart", SATCHEL_SWITCH_IPC_SERVICE_UART},
    {"usb:ds", SATCHEL_SWITCH_IPC_SERVICE_USB_DS},
    {"usb:hs", SATCHEL_SWITCH_IPC_SERVICE_USB_HS},
    {"usb:hs:a", SATCHEL_SWITCH_IPC_SERVICE_USB_HS_A},
    {"usb:obsv", SATCHEL_SWITCH_IPC_SERVICE_USB_OBSV},
    {"usb:pd", SATCHEL_SWITCH_IPC_SERVICE_USB_PD},
    {"usb:pd:c", SATCHEL_SWITCH_IPC_SERVICE_USB_PD_C},
    {"usb:pd:m", SATCHEL_SWITCH_IPC_SERVICE_USB_PD_M},
    {"usb:pm", SATCHEL_SWITCH_IPC_SERVICE_USB_PM},
    {"usb:qdb", SATCHEL_SWITCH_IPC_SERVICE_USB_QDB},
    {"vi:s", SATCHEL_SWITCH_IPC_SERVICE_VI_S},
    {"wlan", SATCHEL_SWITCH_IPC_SERVICE_WLAN},
    {"wlan:dtc", SATCHEL_SWITCH_IPC_SERVICE_WLAN_DTC},
    {"wlan:inf", SATCHEL_SWITCH_IPC_SERVICE_WLAN_INF},
    {"wlan:lcl", SATCHEL_SWITCH_IPC_SERVICE_WLAN_LCL},
    {"wlan:lg", SATCHEL_SWITCH_IPC_SERVICE_WLAN_LG},
    {"wlan:lga", SATCHEL_SWITCH_IPC_SERVICE_WLAN_LGA},
    {"wlan:nd", SATCHEL_SWITCH_IPC_SERVICE_WLAN_ND},
    {"wlan:p", SATCHEL_SWITCH_IPC_SERVICE_WLAN_P},
    {"wlan:sg", SATCHEL_SWITCH_IPC_SERVICE_WLAN_SG},
    {"wlan:soc", SATCHEL_SWITCH_IPC_SERVICE_WLAN_SOC},
    {"xcd:sys", SATCHEL_SWITCH_IPC_SERVICE_XCD_SYS},
};
static uint32_t classify_service(const uint8_t name[8]) {
    for (size_t i = 0; i < sizeof(known_services) / sizeof(known_services[0]); i++)
        if (!memcmp(name, known_services[i].name, 8)) return known_services[i].kind;
    return SATCHEL_SWITCH_IPC_SERVICE_OTHER;
}
const char *satchel_switch_process_service_name(uint32_t kind) {
    if (kind == SATCHEL_SWITCH_IPC_SERVICE_NONE) return "none";
    for (size_t i = 0; i < sizeof(known_services) / sizeof(known_services[0]); i++)
        if (known_services[i].kind == kind) return known_services[i].name;
    return "other";
}
static int valid_service_name(const uint8_t name[8]) {
    if (!name[0]) return 0;
    int terminated = 0;
    for (unsigned i = 0; i < 8; i++) {
        if (!name[i]) terminated = 1;
        else if (terminated) return 0;
    }
    return 1;
}
static int prepare_object_handle(SatchelSwitchProcess *p, size_t *slot, uint32_t *token) {
    for (*slot = 0; *slot < SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT; (*slot)++)
        if (p->handles[*slot].type == HANDLE_EMPTY) break;
    if (*slot == SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT || p->next_handle >= 0xffff8000) {
        p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_RESOURCE; return 0;
    }
    *token = p->next_handle;
    return 1;
}
static SatchelHIPCStatus encode_ipc_reply(const SatchelHIPCRequest *request, uint32_t result,
                         const void *data, size_t size, const uint32_t *move, SatchelHIPCReply *reply) {
    if (request->kind == SATCHEL_HIPC_TIPC_REQUEST) {
        SatchelTIPCReplyFormat format = {.result = result, .data = data, .data_size = size,
            .move_handles = move, .move_handle_count = move ? 1 : 0};
        return satchel_tipc_encode_reply(&format, reply);
    }
    int domain = request->kind == SATCHEL_HIPC_DOMAIN_REQUEST;
    SatchelCMIFReplyFormat format = {.mode = domain ? SATCHEL_HIPC_DOMAIN : SATCHEL_HIPC_SESSION,
        .result = result, .token = domain ? 0 : request->token,
        .data = data, .data_size = size,
        .move_handles = domain ? NULL : move, .move_handle_count = !domain && move ? 1 : 0,
        .objects = domain ? move : NULL, .object_count = domain && move ? 1 : 0};
    return satchel_cmif_encode_reply(&format, reply);
}
#if defined(SATCHEL_CPU_TESTING)
static int test_fail_ipc_reply;
void satchel_switch_process_test_fail_next_ipc_reply(void) { test_fail_ipc_reply=1; }
#endif
static int write_ipc_reply(SatchelSwitchProcess *p, const SatchelHIPCRequest *request, uint32_t result,
                         const void *data, size_t size, const uint32_t *move) {
    SatchelHIPCReply reply;
    if (encode_ipc_reply(request, result, data, size, move, &reply) != SATCHEL_HIPC_OK) {
        p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SCHEMA; return 0;
    }
#if defined(SATCHEL_CPU_TESTING)
    if (test_fail_ipc_reply) { test_fail_ipc_reply=0; p->last_ipc.blocked_reason=SATCHEL_SWITCH_IPC_BLOCK_TLS; return 0; }
#endif
    if (!satchel_cpu_probe_write(p->cpu, p->threads[p->active_thread].tls_address, reply.snapshot, reply.message_size)) {
        p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_TLS; return 0;
    }
    return 1;
}
static int finish_ipc(SatchelSwitchProcess *p) {
    satchel_cpu_probe_set_x(p->cpu, 0, 0); p->ipc_serviced++; return 1;
}
static int no_ipc_attachments(const SatchelHIPCRequest *request) {
    return !request->copy_handle_count && !request->move_handle_count && !request->input_object_count;
}
static int lm_payload(const SatchelHIPCRequest *request, size_t size) {
    if (request->kind == SATCHEL_HIPC_DOMAIN_REQUEST)
        return request->payload_size_exact && request->payload_capacity == size && request->raw_size == 48 + size;
    return request->raw_size == 32 + size && request->payload_capacity >= size;
}
static int return_object_with_state(SatchelSwitchProcess *p, const ProcessHandle *parent,
                         const SatchelHIPCRequest *request, unsigned type, uint64_t client_id, uint32_t service_slot) {
    if (request->kind == SATCHEL_HIPC_DOMAIN_REQUEST) {
        ProcessDomain *domain = p->domains + parent->domain_slot - 1;
        size_t slot = 0;
        while (slot < SATCHEL_SWITCH_DOMAIN_OBJECT_LIMIT && domain->objects[slot].id) slot++;
        if (slot == SATCHEL_SWITCH_DOMAIN_OBJECT_LIMIT || domain->next_id == UINT32_MAX) {
            p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_RESOURCE; return 0;
        }
        uint32_t id = domain->next_id;
        if (!write_ipc_reply(p, request, 0, NULL, 0, &id)) return 0;
        domain->objects[slot] = (DomainObject){id, type, client_id, service_slot}; domain->next_id++;
        return finish_ipc(p);
    }
    size_t slot; uint32_t token;
    if (!prepare_object_handle(p, &slot, &token) || !write_ipc_reply(p, request, 0, NULL, 0, &token)) return 0;
    p->handles[slot] = (ProcessHandle){token, type, client_id, 0, 0, service_slot}; p->next_handle++;
    return finish_ipc(p);
}
static int return_object(SatchelSwitchProcess *p, const ProcessHandle *parent,
                         const SatchelHIPCRequest *request, unsigned type, uint64_t client_id) {
    return return_object_with_state(p, parent, request, type, client_id, 0);
}
static int time_shared_handle(SatchelSwitchProcess *p,const SatchelHIPCRequest *request) {
    p->time_info.shared_handle_requests++;
    size_t slot; uint32_t token; SatchelHIPCReply reply;
    if(!prepare_object_handle(p,&slot,&token)) return 0;
    int domain=request->kind==SATCHEL_HIPC_DOMAIN_REQUEST;
    SatchelCMIFReplyFormat format={.mode=domain?SATCHEL_HIPC_DOMAIN:SATCHEL_HIPC_SESSION,
        .token=domain?0:request->token,.copy_handles=&token,.copy_handle_count=1};
    if(satchel_cmif_encode_reply(&format,&reply)!=SATCHEL_HIPC_OK) {
        p->last_ipc.blocked_reason=SATCHEL_SWITCH_IPC_BLOCK_SCHEMA;return 0;
    }
#if defined(SATCHEL_CPU_TESTING)
    if(test_fail_ipc_reply) {test_fail_ipc_reply=0;p->last_ipc.blocked_reason=SATCHEL_SWITCH_IPC_BLOCK_TLS;return 0;}
#endif
    if(!satchel_cpu_probe_write(p->cpu,p->threads[p->active_thread].tls_address,reply.snapshot,reply.message_size)) {
        p->last_ipc.blocked_reason=SATCHEL_SWITCH_IPC_BLOCK_TLS;return 0;
    }
    p->handles[slot]=(ProcessHandle){token,HANDLE_TIME_SHARED_MEMORY,0,0,0,0};p->next_handle++;
    return finish_ipc(p);
}
static int time_request(SatchelSwitchProcess *p,const ProcessHandle *object,const SatchelHIPCRequest *request) {
    // User persona0, immutable sampled local/user epoch, uninitialized network.
    // Primary ABI: libnx b156b477 time.c; Ryujinx05b4bd8c time interfaces.
    // No timezone rules, RTC adjustments or automatic/network correction.
    if(!p->time_info.configured) {p->last_ipc.blocked_reason=SATCHEL_SWITCH_IPC_BLOCK_COMMAND;return 0;}
    unsigned type=object->type;uint32_t command=request->command_id;
    int system=type==HANDLE_TIME_USER_CLOCK||type==HANDLE_TIME_LOCAL_CLOCK||type==HANDLE_TIME_NETWORK_CLOCK;
    size_t input=0,output=0;uint32_t result=0;uint8_t bytes[36]={0};
    if(system && (command==1||command==3)) {input=command==1?8:32;result=0x274;}
    else if(system && (command==0||command==2)) output=command==0?8:32;
    else if(type==HANDLE_TIME_STEADY_CLOCK && command==0) output=24;
    else if(type==HANDLE_TIME_ZONE && command==0) output=36;
    else {p->last_ipc.blocked_reason=SATCHEL_SWITCH_IPC_BLOCK_COMMAND;return 0;}
    if(request->send_pid||request->buffer_count||!no_ipc_attachments(request)||!lm_payload(request,input)) {
        p->last_ipc.blocked_reason=SATCHEL_SWITCH_IPC_BLOCK_SCHEMA;return 0;
    }
    if(!result) {
        if(type==HANDLE_TIME_NETWORK_CLOCK) {result=0xce74;output=0;}
        else if(type==HANDLE_TIME_ZONE) memcpy(bytes,p->time_seed.location_name,36);
        else if(system && command==2) time_context(p,bytes);
        else {
            uint64_t tick;int64_t steady,wall;
            if(!satchel_switch_process_get_system_tick(p,&tick)) {p->last_ipc.blocked_reason=SATCHEL_SWITCH_IPC_BLOCK_RESOURCE;return 0;}
            if(!time_values(p->time_base_ns,p->time_offset,tick,&steady,&wall)) {result=(201u<<9)|116u;output=0;}
            else {time_word64(bytes,(uint64_t)(system?wall:steady));if(!system)memcpy(bytes+8,p->time_source_id,16);}
        }
    }
    if(!write_ipc_reply(p,request,result,output?bytes:NULL,output,NULL))return 0;
    if(!result){if(type==HANDLE_TIME_ZONE)p->time_info.timezone_reads++;else p->time_info.clock_reads++;}
    return finish_ipc(p);
}
static int parental_initialize(SatchelSwitchProcess *p, const ProcessHandle *object, const SatchelHIPCRequest *request) {
    p->parental_initialize_requests++;
    if (request->send_pid || request->buffer_count || !no_ipc_attachments(request) || !lm_payload(request, 0)) {
        p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SCHEMA; return 0;
    }
    if (!object->service_slot || object->service_slot > SATCHEL_SWITCH_PARENTAL_CHILD_LIMIT) {
        p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_OBJECT; return 0;
    }
    ProcessParentalChild *child = p->parental_children + object->service_slot - 1;
    if (!child->references || child->client_process_id != object->client_process_id) {
        p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_OBJECT; return 0;
    }
    // Published Ryujinx05b4bd8c Initialize: capability0x8001, a bound PID,
    // then title/control lookup unless persona0x40. No restriction decision.
    uint32_t result = !(child->capabilities & 0x8001) ? ((133u << 9) | 142u) :
        (!child->client_process_id ? ((131u << 9) | 142u) : 0);
    ProcessParentalChild candidate = *child;
    if (!result) {
        if (!(child->capabilities & 0x40)) {
            if (!p->control_attachments || p->application_control.application_id != p->program_id) {
                p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SERVICE; return 0;
            }
            candidate.metadata = p->application_control; candidate.metadata_bound = 1;
        }
        candidate.initialized = 1;
    }
    // Immutable metadata makes repeated Initialize idempotent. Shared mutable
    // child state commits only after the complete response is written. No
    // policy approval, private publication event or auxiliary API is implied.
    if (!write_ipc_reply(p, request, result, NULL, 0, NULL)) return 0;
    if (!result) *child = candidate;
    p->parental_initialize_serviced++;
    return finish_ipc(p);
}
static int storage_request(SatchelSwitchProcess *p, const SatchelHIPCRequest *request) {
    if (!p->has_storage) { p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SERVICE; return 0; }
    SatchelStoragePlan plan;
    SatchelStoragePlanStatus status = satchel_switch_storage_plan(request, p->storage.size, &plan);
    p->last_ipc.storage_plan_status = status;
    if (status != SATCHEL_STORAGE_PLAN_OK) { p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SCHEMA; return 0; }
    if (plan.command_id == 4) {
        uint8_t size[8];
        if (!satchel_switch_storage_size_bytes(p->storage.size, size) ||
            !write_ipc_reply(p, request, 0, size, sizeof(size), NULL)) return 0;
        return finish_ipc(p);
    }
    // Validate the WHOLE advertised output, not just the requested prefix.
    // No output may alias any part of the owned TLS page, including thread
    // metadata beyond the reply. No provider is called for an invalid mapping.
    if (plan.output_capacity) {
        SatchelSwitchMemoryInfo mapping;
        int overlaps_tls = 0;
        for (unsigned page = 0; page < SATCHEL_SWITCH_THREAD_LIMIT / 8; page++) if (p->tls_pages & (1u << page)) {
            uint64_t tls = tls_base + page * 4096;
            if (plan.output_address < tls + 4096 && tls < plan.output_address + plan.output_capacity) overlaps_tls = 1;
        }
        if (!satchel_cpu_probe_check_range(p->cpu, plan.output_address, plan.output_capacity, SATCHEL_CPU_WRITE) ||
            !satchel_switch_process_query(p, plan.output_address, &mapping) || !(mapping.permissions & 2) ||
            plan.output_capacity > mapping.size - (plan.output_address - mapping.address) ||
            overlaps_tls) {
            p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_BUFFER; return 0;
        }
    }
    SatchelHIPCReply reply;
    if (encode_ipc_reply(request, 0, NULL, 0, NULL, &reply) != SATCHEL_HIPC_OK) {
        p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SCHEMA; return 0;
    }
    size_t count = (size_t)plan.count;
    uint8_t *staging = count ? malloc(count) : NULL;
    if (count && !staging) { p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_RESOURCE; return 0; }
    SatchelSwitchStorageStatus fetched = satchel_switch_storage_fetch(&p->storage, &plan, staging, count);
    p->last_ipc.storage_status = fetched;
    if (fetched != SATCHEL_SWITCH_STORAGE_OK) {
        free(staging); p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_STORAGE; return 0;
    }
    SatchelCPUProbeWrite writes[2] = {{p->threads[p->active_thread].tls_address, reply.snapshot, reply.message_size},
                                {plan.output_address, staging, count}};
    SatchelCPUWriteResult written = satchel_cpu_probe_writev(p->cpu, writes, count ? 2 : 1);
    free(staging); p->last_ipc.storage_write_status = written;
    if (written != SATCHEL_CPU_WRITE_OK) { p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_STORAGE; return 0; }
    p->storage_reads++; p->storage_bytes += count;
    return finish_ipc(p);
}
static int fsp_request(SatchelSwitchProcess *p, const ProcessHandle *object, const SatchelHIPCRequest *request) {
    if (!p->has_storage) { p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SERVICE; return 0; }
    // Public libnx b156b477 fs.c: Initialize1(PID+u64 placeholder), then
    // OpenDataStorageByCurrentProcess200. No paths or arbitrary program IDs.
    if (request->command_id == 1) {
        if (!request->send_pid || request->buffer_count || !lm_payload(request, 8)) {
            p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SCHEMA; return 0;
        }
        if (!write_ipc_reply(p, request, 0, NULL, 0, NULL)) return 0;
        if (object->domain_slot) {
            ProcessDomain *domain = p->domains + object->domain_slot - 1;
            for (size_t i = 0; i < SATCHEL_SWITCH_DOMAIN_OBJECT_LIMIT; i++)
                if (domain->objects[i].id == request->object_id) domain->objects[i].client_process_id = p->process_id;
        } else p->handles[(size_t)(object - p->handles)].client_process_id = p->process_id;
        return finish_ipc(p);
    }
    if (request->command_id != 200) { p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_COMMAND; return 0; }
    if (request->send_pid || request->buffer_count || !lm_payload(request, 0)) {
        p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SCHEMA; return 0;
    }
    if (!object->client_process_id) { p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SERVICE; return 0; }
    return return_object(p, object, request, HANDLE_STORAGE, object->client_process_id);
}
static int apm_request(SatchelSwitchProcess *p, const ProcessHandle *object, const SatchelHIPCRequest *request) {
    // Published ABI: pinned libnx b156b477 apm.c OpenSession0/GetPerformanceMode1.
    // This is an explicitly handheld emulator profile, not a host frequency,
    // battery, docking or iOS power-management assertion.
    if (object->type != HANDLE_APM_ROOT || request->command_id > 1) {
        // ISession configuration calls have public schemas but no validated
        // initial configuration-ID registry in this slice. Do not fabricate a
        // default ID or claim to apply an unknown performance configuration.
        p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_COMMAND; return 0;
    }
    if (request->send_pid || request->buffer_count || !lm_payload(request, 0)) {
        p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SCHEMA; return 0;
    }
    if (request->command_id == 0) return return_object(p, object, request, HANDLE_APM_SESSION, p->process_id);
    uint8_t mode[4];
    for (unsigned i = 0; i < 4; i++) mode[i] = (uint8_t)(p->performance_mode >> (8 * i));
    if (!write_ipc_reply(p, request, 0, mode, sizeof(mode), NULL)) return 0;
    return finish_ipc(p);
}
static int applet_event_handle(SatchelSwitchProcess *p, const SatchelHIPCRequest *request, uint64_t *owned_event, int initial_signal) {
    size_t slot; uint32_t token;
    if (!prepare_object_handle(p, &slot, &token)) return 0;
    SatchelSwitchEvents *events = p->events;
    int new_pool = events == NULL, new_event = *owned_event == 0, retained = 0;
    uint64_t id = *owned_event;
    if (new_pool) events = satchel_switch_events_create();
    if (!events) { p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_RESOURCE; return 0; }
    if (new_event && satchel_switch_event_create(events, &id) != SATCHEL_SWITCH_EVENT_OK) goto resource_failure;
    if (new_event && initial_signal && satchel_switch_event_signal(events, id) != SATCHEL_SWITCH_EVENT_OK) goto resource_failure;
    if (satchel_switch_event_retain(events, id) != SATCHEL_SWITCH_EVENT_OK) goto resource_failure;
    retained = 1;
    // Published GetEventHandle returns COPY, not MOVE or a domain object ID.
    // The context keeps its initial reference; this guest handle owns another.
    SatchelHIPCReply reply;
    int domain = request->kind == SATCHEL_HIPC_DOMAIN_REQUEST;
    SatchelCMIFReplyFormat format = {.mode = domain ? SATCHEL_HIPC_DOMAIN : SATCHEL_HIPC_SESSION,
        .token = domain ? 0 : request->token, .copy_handles = &token, .copy_handle_count = 1};
    if (satchel_cmif_encode_reply(&format, &reply) != SATCHEL_HIPC_OK) {
        p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SCHEMA; goto failed;
    }
    if (!satchel_cpu_probe_write(p->cpu, p->threads[p->active_thread].tls_address, reply.snapshot, reply.message_size)) {
        p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_TLS; goto failed;
    }
    p->events = events; *owned_event = id;
    p->handles[slot] = (ProcessHandle){token, HANDLE_READABLE_EVENT, 0, 0, id, 0}; p->next_handle++;
    return finish_ipc(p);
resource_failure:
    p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_RESOURCE;
failed:
    if (retained) satchel_switch_event_release(events, id);
    if (new_event && id) satchel_switch_event_release(events, id);
    if (new_pool) satchel_switch_events_destroy(events);
    return 0;
}
static int applet_payload(const SatchelHIPCRequest *request, size_t size) {
    // HIPC stores whole words; domain data_size still records exact argument
    // bytes. Padding is transport padding, not another boolean or field.
    size_t rounded = (size + 3) & ~(size_t)3;
    if (request->kind == SATCHEL_HIPC_DOMAIN_REQUEST)
        return request->payload_size_exact && request->payload_capacity == size && request->raw_size == 48 + rounded;
    return request->raw_size == 32 + rounded && request->payload_capacity >= size;
}
static int applet_request(SatchelSwitchProcess *p, const ProcessHandle *object, const SatchelHIPCRequest *request) {
    // Independent implementation of the published libnx b156b477 applet.c
    // application proxy graph. No host applet, audio/display or event operation
    // is implied by owning a typed interface; unsupported methods still stop.
    if (object->type == HANDLE_APPLET_ROOT && request->command_id == 0) {
        if (!request->send_pid || request->copy_handle_count != 1 || request->move_handle_count ||
            request->input_object_count || request->buffer_count || !lm_payload(request, 8)) {
            p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SCHEMA; return 0;
        }
        // Only the current-process pseudo reference is implemented. The PID
        // claim and inline placeholder are untrusted; bind the owned identity.
        // Other process references/types stop instead of inventing AM errors.
        if (request->copy_handles[0] != 0xFFFF8001) {
            p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_HANDLE; return 0;
        }
        return return_object(p, object, request, HANDLE_APPLICATION_PROXY, p->process_id);
    }
    if (object->type == HANDLE_APPLICATION_PROXY) {
        if (request->send_pid || request->buffer_count || !lm_payload(request, 0)) {
            p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SCHEMA; return 0;
        }
        unsigned child;
        switch (request->command_id) {
            case 0: child = HANDLE_COMMON_STATE; break;
            case 1: child = HANDLE_SELF_CONTROLLER; break;
            case 2: child = HANDLE_WINDOW_CONTROLLER; break;
            case 3: child = HANDLE_AUDIO_CONTROLLER; break;
            case 4: child = HANDLE_DISPLAY_CONTROLLER; break;
            case 11: child = HANDLE_LIBRARY_APPLET_CREATOR; break;
            case 20: child = HANDLE_APPLICATION_FUNCTIONS; break;
            case 1000: child = HANDLE_DEBUG_FUNCTIONS; break;
            default: p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_COMMAND; return 0;
        }
        return return_object(p, object, request, child, object->client_process_id);
    }
    if (object->type == HANDLE_COMMON_STATE && request->command_id == 1) {
        if (request->send_pid || request->buffer_count || !lm_payload(request, 0)) {
            p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SCHEMA; return 0;
        }
        // Public libnx b156b477: ReceiveMessage returns u32; an empty queue is
        // AM NoMessages (module128,description3). Commit consumption only after
        // the full reply is written. The event reflects remaining queued work.
        if (!p->applet_message_count) {
            if (!write_ipc_reply(p,request,0x680,NULL,0,NULL)) return 0;
            return finish_ipc(p);
        }
        int signaled;
        if (p->applet_message_event && satchel_switch_event_query(p->events,p->applet_message_event,&signaled) != SATCHEL_SWITCH_EVENT_OK) {
            p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_RESOURCE; return 0;
        }
        uint32_t message = p->applet_messages[p->applet_message_head];
        uint8_t data[4]; for (unsigned i=0;i<4;i++) data[i]=(uint8_t)(message>>(8*i));
        if (!write_ipc_reply(p,request,0,data,sizeof(data),NULL)) return 0;
        unsigned remaining = p->applet_message_count - 1;
        if (p->applet_message_event && (remaining ? satchel_switch_event_signal(p->events,p->applet_message_event) :
            satchel_switch_event_clear(p->events,p->applet_message_event)) != SATCHEL_SWITCH_EVENT_OK) {
            // Private event invariant failure: do not resume the guest with an
            // invented consumed notification or fabricate another event.
            p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_RESOURCE; return 0;
        }
        p->applet_messages[p->applet_message_head] = 0;
        p->applet_message_head = (p->applet_message_head + 1) % SATCHEL_SWITCH_APPLET_MESSAGE_LIMIT;
        p->applet_message_count = remaining; p->applet_messages_received++;
        return finish_ipc(p);
    }
    if (object->type == HANDLE_COMMON_STATE &&
        (request->command_id == 5 || request->command_id == 6 || request->command_id == 9)) {
        if (request->send_pid || request->buffer_count || !lm_payload(request, 0)) {
            p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SCHEMA; return 0;
        }
        uint8_t value[4] = {0}; size_t size = 1;
        if (request->command_id == 5) value[0] = p->applet.operation_mode;
        else if (request->command_id == 9) value[0] = p->applet.focus_state;
        else { size = 4; for (unsigned i = 0; i < 4; i++) value[i] = (uint8_t)(p->performance_mode >> (8 * i)); }
        if (!write_ipc_reply(p, request, 0, value, size, NULL)) return 0;
        return finish_ipc(p);
    }
    if (object->type == HANDLE_SELF_CONTROLLER && (request->command_id == 11 || request->command_id == 12 ||
        request->command_id == 13 || request->command_id == 16)) {
        size_t size = request->command_id == 13 ? 3 : 1;
        uint8_t values[3] = {0};
        if (request->send_pid || request->buffer_count || !applet_payload(request, size) ||
            !satchel_hipc_copy_payload(request, 0, values, size)) {
            p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SCHEMA; return 0;
        }
        for (size_t i = 0; i < size; i++) if (values[i] > 1) {
            p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SCHEMA; return 0;
        }
        if (!write_ipc_reply(p, request, 0, NULL, 0, NULL)) return 0;
        switch (request->command_id) {
            case 11: p->applet.operation_notifications = values[0]; break;
            case 12: p->applet.performance_notifications = values[0]; break;
            case 13: memcpy(p->applet.focus_handling, values, 3); p->applet.focus_handling_set = 1; break;
            case 16: p->applet.out_of_focus_suspending = values[0]; p->applet.out_of_focus_suspending_set = 1; break;
        }
        // Policy updates alone neither change focus/mode nor enqueue a message.
        return finish_ipc(p);
    }
    if (object->type == HANDLE_WINDOW_CONTROLLER && request->command_id == 10) {
        if (request->send_pid || request->buffer_count || !lm_payload(request, 0)) {
            p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SCHEMA; return 0;
        }
        if (!write_ipc_reply(p, request, 0, NULL, 0, NULL)) return 0;
        p->applet.foreground_rights = 1; return finish_ipc(p);
    }
    if (object->type == HANDLE_APPLICATION_FUNCTIONS && request->command_id == 130) {
        if (request->send_pid || request->buffer_count || !lm_payload(request, 0)) {
            p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SCHEMA; return 0;
        }
        // Public GetGpuErrorDetectedSystemEvent returns COPY/autoclear=false.
        return applet_event_handle(p, request, &p->gpu_error_event, p->gpu_error_detected);
    }
    if (object->type == HANDLE_COMMON_STATE && (request->command_id == 0 || request->command_id == 900)) {
        if (request->send_pid || request->buffer_count || !lm_payload(request, 0)) {
            p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SCHEMA; return 0;
        }
        if (request->command_id == 0) return applet_event_handle(p, request, &p->applet_message_event, p->applet_message_count != 0);
        // libnx applet.c command900 is a no-IO policy setter, not a platform
        // region query. Future ExecuteProgram must honor this policy or stop.
        if (!write_ipc_reply(p, request, 0, NULL, 0, NULL)) return 0;
        p->exit_to_library_on_execute_next = 1; return finish_ipc(p);
    }
    if (object->type == HANDLE_SELF_CONTROLLER && (request->command_id == 90 || request->command_id == 91)) {
        if (request->send_pid || request->buffer_count || !lm_payload(request, 0)) {
            p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SCHEMA; return 0;
        }
        // Published libnx applet.c:90 returns accumulated suspended ticks;
        //91 returns its change event via COPY. Client-side autoclear does not
        // change the kernel event's manual level semantics. This bounded
        // process has no suspend/resume operation: cancellation ends the run,
        // so no interval is charged and no change event is manufactured.
        if (request->command_id == 91) return applet_event_handle(p, request, &p->suspended_tick_event, 0);
        uint8_t ticks[8];
        for (unsigned i = 0; i < 8; i++) ticks[i] = (uint8_t)(p->suspended_ticks >> (8 * i));
        if (!write_ipc_reply(p, request, 0, ticks, sizeof(ticks), NULL)) return 0;
        return finish_ipc(p);
    }
    if (object->type == HANDLE_WINDOW_CONTROLLER && request->command_id == 1) {
        if (request->send_pid || request->buffer_count || !lm_payload(request, 0)) {
            p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SCHEMA; return 0;
        }
        uint8_t id[8];
        for (unsigned i = 0; i < 8; i++) id[i] = (uint8_t)(p->applet_resource_id >> (8 * i));
        if (!write_ipc_reply(p, request, 0, id, sizeof(id), NULL)) return 0;
        return finish_ipc(p);
    }
    // No fabricated events, focus transitions, frames, audio or launch result.
    p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_COMMAND; return 0;
}
static int service_request(SatchelSwitchProcess *p, const ProcessHandle *object, const SatchelHIPCRequest *request) {
    if (request->kind == SATCHEL_HIPC_CONTROL && request->command_id == 0) {
        if (request->send_pid || request->buffer_count || !no_ipc_attachments(request) || request->raw_size != 32) {
            p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SCHEMA; return 0;
        }
        // ABI facts: pinned Atmosphere6b0c077b server_domain_session_manager
        // ConvertCurrentObjectToDomain; libnx b156b477 cmif.h control0.
        // Independent bounded implementation, no upstream source copied.
        // Conversion returns a normal session reply. Commit mode and root ID
        // only after the reply write; no speculative table/handle mutations.
        if (object->domain_slot) { p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_COMMAND; return 0; }
        size_t slot = 0;
        while (slot < SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT && p->domains[slot].references) slot++;
        if (slot == SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT) { p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_RESOURCE; return 0; }
        const uint8_t root_id[4] = {1, 0, 0, 0};
        if (!write_ipc_reply(p, request, 0, root_id, sizeof(root_id), NULL)) return 0;
        ProcessDomain *domain = p->domains + slot;
        domain->references = 1; domain->next_id = 2;
        domain->objects[0] = (DomainObject){1, object->type, object->client_process_id, object->service_slot};
        p->handles[(size_t)(object - p->handles)].domain_slot = (uint32_t)slot + 1;
        p->handles[(size_t)(object - p->handles)].service_slot = 0;
        return finish_ipc(p);
    }
    if (request->kind == SATCHEL_HIPC_CONTROL && request->command_id == 2) {
        if (request->send_pid || request->buffer_count || !no_ipc_attachments(request) || request->raw_size != 32) {
            p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SCHEMA; return 0;
        }
        if (!object->domain_slot) { p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_COMMAND; return 0; }
        size_t slot; uint32_t token;
        if (!prepare_object_handle(p, &slot, &token) || !write_ipc_reply(p, request, 0, NULL, 0, &token)) return 0;
        // libnx sessionmgrCreate uses this exact control2 path. The domain
        // holder is shared; conversions, child IDs and initialization are not
        // copied into diverging tables. Closing one handle retains its peers.
        p->handles[slot] = *object; p->handles[slot].token = token;
        p->domains[object->domain_slot - 1].references++; p->next_handle++;
        return finish_ipc(p);
    }
    if (request->kind == SATCHEL_HIPC_CONTROL && request->command_id == 3) {
        if (request->send_pid || request->buffer_count || !no_ipc_attachments(request) || request->raw_size != 32) {
            p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SCHEMA; return 0;
        }
        // No server pointer buffer is allocated; automatic clients must choose
        // their mapped input. This is an owned capacity report, not a stub size.
        const uint8_t pointer_size[2] = {0};
        if (!write_ipc_reply(p, request, 0, pointer_size, sizeof(pointer_size), NULL)) return 0;
        return finish_ipc(p);
    }
    if ((request->kind != SATCHEL_HIPC_REQUEST && request->kind != SATCHEL_HIPC_DOMAIN_REQUEST) ||
        (!no_ipc_attachments(request) && !(object->type == HANDLE_APPLET_ROOT && request->command_id == 0))) {
        p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_COMMAND; return 0;
    }
    if (object->type >= HANDLE_APPLET_ROOT && object->type <= HANDLE_DEBUG_FUNCTIONS) return applet_request(p, object, request);
    if (object->type == HANDLE_PCTL_ROOT || object->type == HANDLE_PCTL_CHILD ||
        object->type == HANDLE_PCTL_SYSTEM_ROOT || object->type == HANDLE_PCTL_SYSTEM_CHILD ||
        object->type == HANDLE_PCTL_ADMIN_ROOT || object->type == HANDLE_PCTL_ADMIN_CHILD ||
        object->type == HANDLE_PCTL_READONLY_ROOT || object->type == HANDLE_PCTL_READONLY_CHILD) {
        // libnx b156b477 pctl.c: CreateServiceWithoutInitialize1 has a PID
        // descriptor and u64 reserved argument. Exact pctl is the application
        // persona (public Ryujinx05b4bd8c factory capability0x303). Exact pctl:s
        // has separate root/child types and persona0x838E; it is never silently
        // converted to application pctl. Exact pctl:a has separate admin root/child
        // types and persona0x83BE. Exact pctl:r has distinct read-only root/child
        // types and persona0x8040. No persona is interchangeable with another.
        // Factory0 implicitly initializes and remains unsupported. Factory1
        // owns a shared child record; only explicit Initialize1 binds metadata.
        int root = object->type == HANDLE_PCTL_ROOT || object->type == HANDLE_PCTL_SYSTEM_ROOT ||
            object->type == HANDLE_PCTL_ADMIN_ROOT || object->type == HANDLE_PCTL_READONLY_ROOT;
        if (!root) {
            if (request->command_id == 1) return parental_initialize(p, object, request);
            p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_COMMAND; return 0;
        }
        if (request->command_id != 1) { p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_COMMAND; return 0; }
        if (!request->send_pid || request->buffer_count || !lm_payload(request, 8)) {
            p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SCHEMA; return 0;
        }
        size_t slot = 0;
        while (slot < SATCHEL_SWITCH_PARENTAL_CHILD_LIMIT && p->parental_children[slot].references) slot++;
        if (slot == SATCHEL_SWITCH_PARENTAL_CHILD_LIMIT) { p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_RESOURCE; return 0; }
        unsigned type = object->type == HANDLE_PCTL_SYSTEM_ROOT ? HANDLE_PCTL_SYSTEM_CHILD :
            (object->type == HANDLE_PCTL_ADMIN_ROOT ? HANDLE_PCTL_ADMIN_CHILD :
            (object->type == HANDLE_PCTL_READONLY_ROOT ? HANDLE_PCTL_READONLY_CHILD : HANDLE_PCTL_CHILD));
        uint32_t capabilities = object->type == HANDLE_PCTL_SYSTEM_ROOT ? 0x838E :
            (object->type == HANDLE_PCTL_ADMIN_ROOT ? 0x83BE : (object->type == HANDLE_PCTL_READONLY_ROOT ? 0x8040 : 0x303));
        // Reply failure cannot reserve an object or leak a reference. The
        // following assignments are non-failing and precede guest resumption.
        if (!return_object_with_state(p, object, request, type, p->process_id, (uint32_t)slot + 1)) return 0;
        p->parental_children[slot] = (ProcessParentalChild){.references=1,.capabilities=capabilities,.client_process_id=p->process_id};
        p->parental_created++;
        return 1;
    }

    if (object->type == HANDLE_TIME_USER_ROOT) {
        // Exact public libnx b156b477 time.c / Ryujinx05b4bd8c static getters.
        // User permissions are0: returned views all retain this process-owned
        // service identity. Owning a clock view does not initialize its data.
        if (request->command_id > 4 && !(request->command_id==20 && p->time_info.configured)) { p->last_ipc.blocked_reason=SATCHEL_SWITCH_IPC_BLOCK_COMMAND; return 0; }
        if (request->send_pid || request->buffer_count || !no_ipc_attachments(request) || !lm_payload(request,0)) {
            p->last_ipc.blocked_reason=SATCHEL_SWITCH_IPC_BLOCK_SCHEMA; return 0;
        }
        if(request->command_id==20)return time_shared_handle(p,request);
        static const unsigned types[5]={HANDLE_TIME_USER_CLOCK,HANDLE_TIME_NETWORK_CLOCK,
            HANDLE_TIME_STEADY_CLOCK,HANDLE_TIME_ZONE,HANDLE_TIME_LOCAL_CLOCK};
        return return_object(p,object,request,types[request->command_id],object->client_process_id);
    }
    if (object->type >= HANDLE_TIME_USER_CLOCK && object->type <= HANDLE_TIME_LOCAL_CLOCK)
        return time_request(p,object,request);
    if (object->type == HANDLE_ECTX_APPLICATION) {
        // Public Ryujinx05b4bd8c Ectx/IWriterForApplication registers ectx:aw
        // but provides no command implementation. Own the session, never claim
        // successful error-context retention without a verified command contract.
        // No guest error payload is retained or exported by this observer slice.
        p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_COMMAND; return 0;
    }
    if (object->type == HANDLE_AOC_ROOT) {
        // Owned manager connection only. Public Ryujinx05b4bd8c Aoc/
        // IAddOnContentManager confirms this interface; commands additionally
        // require exact schema and mounted-inventory/NACP state. Connection
        // does not assert the source package has no DLC, return an empty count,
        // grant entitlement, prepare content, or synthesize an event.
        p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_COMMAND; return 0;
    }
    if (object->type == HANDLE_APM_ROOT || object->type == HANDLE_APM_SESSION) return apm_request(p, object, request);
    if (object->type == HANDLE_STORAGE) return storage_request(p, request);
    if (object->type == HANDLE_FSP_SESSION) return fsp_request(p, object, request);
    if (object->type == HANDLE_LM_SESSION && request->command_id == 0) {
        if (!request->send_pid || request->buffer_count || !lm_payload(request, 8)) {
            p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SCHEMA; return 0;
        }
        return return_object(p, object, request, HANDLE_LOGGER, p->process_id);
    }
    if (object->type != HANDLE_LOGGER || request->command_id > 1) {
        p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_COMMAND; return 0;
    }
    if (request->command_id == 1) {
        uint8_t argument[4];
        if (request->send_pid || request->buffer_count || !lm_payload(request, 4) ||
            !satchel_hipc_copy_payload(request, 0, argument, sizeof(argument))) {
            p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SCHEMA; return 0;
        }
        uint32_t destination = ipc_read_word(argument);
        if (destination > 1) { p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_COMMAND; return 0; }
        if (!write_ipc_reply(p, request, 0, NULL, 0, NULL)) return 0;
        p->log_destination = destination;
        return finish_ipc(p);
    }
    // ILogger Log's InAutoSelectBuffer may include an empty X companion, but
    // populated pointer descriptors require a server pointer buffer we lack.
    if (request->send_pid || !lm_payload(request, 0) || request->buffer_count < 1 || request->buffer_count > 2) {
        p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SCHEMA; return 0;
    }
    const SatchelHIPCBuffer *input = NULL;
    unsigned empty_static = 0;
    for (uint32_t i = 0; i < request->buffer_count; i++) {
        const SatchelHIPCBuffer *buffer = request->buffers + i;
        if (buffer->kind == SATCHEL_HIPC_SEND_STATIC && !buffer->address && !buffer->size && !buffer->index && !empty_static) {
            empty_static++; continue;
        }
        if (buffer->kind != SATCHEL_HIPC_SEND_BUFFER || buffer->mode || input) {
            p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_BUFFER; return 0;
        }
        input = buffer;
    }
    if (!input || input->size < 24 || input->size > SATCHEL_SWITCH_LOG_PACKET_LIMIT) {
        p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_BUFFER; return 0;
    }
    // Original bounded whole-packet sink. Published LM header is24 bytes;
    // payload length at20. Packet contents never leave private process memory.
    uint8_t packet[SATCHEL_SWITCH_LOG_PACKET_LIMIT];
    size_t length = (size_t)input->size;
    if (!satchel_cpu_probe_read(p->cpu, input->address, packet, length) ||
        (packet[16] & ~7u) || !(packet[16] & 4) || packet[17] ||
        ipc_read_word(packet + 20) != length - 24) {
        p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_BUFFER; return 0;
    }
    for (unsigned i = 0; i < 8; i++) packet[i] = (uint8_t)(object->client_process_id >> (8 * i));
    if (!write_ipc_reply(p, request, 0, NULL, 0, NULL)) return 0;
    p->log_count++;
    if (p->log_destination == 1) {
        size_t slot = p->log_next;
        if (p->log_packets[slot].size) {
            p->log_retained_bytes -= p->log_packets[slot].size; p->log_dropped++;
        }
        // Clear evicted data, including any old bytes beyond the shorter packet.
        memset(p->log_packets[slot].bytes, 0, sizeof(p->log_packets[slot].bytes));
        memcpy(p->log_packets[slot].bytes, packet, length); p->log_packets[slot].size = length;
        p->log_retained_bytes += length; p->log_next = (slot + 1) % SATCHEL_SWITCH_LOG_PACKET_SLOTS;
    } else p->log_dropped++; // Explicitly disabled destination, validated drop.
    memset(packet, 0, sizeof(packet));
    return finish_ipc(p);
}
static uint32_t object_kind(unsigned type) {
    if (type >= HANDLE_APPLET_ROOT && type <= HANDLE_DEBUG_FUNCTIONS)
        return SATCHEL_SWITCH_IPC_OBJECT_APPLET + (type - HANDLE_APPLET_ROOT);
    switch (type) {
        case HANDLE_SM_SESSION: return SATCHEL_SWITCH_IPC_OBJECT_SM;
        case HANDLE_LM_SESSION: return SATCHEL_SWITCH_IPC_OBJECT_LM;
        case HANDLE_LOGGER: return SATCHEL_SWITCH_IPC_OBJECT_LOGGER;
        case HANDLE_FSP_SESSION: return SATCHEL_SWITCH_IPC_OBJECT_FSP;
        case HANDLE_STORAGE: return SATCHEL_SWITCH_IPC_OBJECT_STORAGE;
        case HANDLE_TIME_USER_ROOT: return SATCHEL_SWITCH_IPC_OBJECT_TIME_USER;
        case HANDLE_TIME_USER_CLOCK: return SATCHEL_SWITCH_IPC_OBJECT_TIME_USER_CLOCK;
        case HANDLE_TIME_NETWORK_CLOCK: return SATCHEL_SWITCH_IPC_OBJECT_TIME_NETWORK_CLOCK;
        case HANDLE_TIME_STEADY_CLOCK: return SATCHEL_SWITCH_IPC_OBJECT_TIME_STEADY_CLOCK;
        case HANDLE_TIME_ZONE: return SATCHEL_SWITCH_IPC_OBJECT_TIME_ZONE;
        case HANDLE_TIME_LOCAL_CLOCK: return SATCHEL_SWITCH_IPC_OBJECT_TIME_LOCAL_CLOCK;
        case HANDLE_PCTL_READONLY_ROOT: return SATCHEL_SWITCH_IPC_OBJECT_PCTL_READONLY;
        case HANDLE_PCTL_READONLY_CHILD: return SATCHEL_SWITCH_IPC_OBJECT_PCTL_READONLY_CHILD;
        case HANDLE_PCTL_ADMIN_ROOT: return SATCHEL_SWITCH_IPC_OBJECT_PCTL_ADMIN;
        case HANDLE_PCTL_ADMIN_CHILD: return SATCHEL_SWITCH_IPC_OBJECT_PCTL_ADMIN_CHILD;
        case HANDLE_PCTL_SYSTEM_ROOT: return SATCHEL_SWITCH_IPC_OBJECT_PCTL_SYSTEM;
        case HANDLE_PCTL_SYSTEM_CHILD: return SATCHEL_SWITCH_IPC_OBJECT_PCTL_SYSTEM_CHILD;
        case HANDLE_PCTL_ROOT: return SATCHEL_SWITCH_IPC_OBJECT_PCTL;
        case HANDLE_PCTL_CHILD: return SATCHEL_SWITCH_IPC_OBJECT_PCTL_CHILD;
        case HANDLE_ECTX_APPLICATION: return SATCHEL_SWITCH_IPC_OBJECT_ECTX_APPLICATION;
        case HANDLE_AOC_ROOT: return SATCHEL_SWITCH_IPC_OBJECT_AOC;
        case HANDLE_APM_ROOT: return SATCHEL_SWITCH_IPC_OBJECT_APM;
        case HANDLE_APM_SESSION: return SATCHEL_SWITCH_IPC_OBJECT_APM_SESSION;
        default: return SATCHEL_SWITCH_IPC_OBJECT_NONE;
    }
}
static uint32_t object_service(unsigned type) {
    if (type >= HANDLE_TIME_USER_ROOT && type <= HANDLE_TIME_LOCAL_CLOCK) return SATCHEL_SWITCH_IPC_SERVICE_TIME;
    if (type == HANDLE_PCTL_READONLY_ROOT || type == HANDLE_PCTL_READONLY_CHILD) return SATCHEL_SWITCH_IPC_SERVICE_PCTL_R;
    if (type == HANDLE_PCTL_ADMIN_ROOT || type == HANDLE_PCTL_ADMIN_CHILD) return SATCHEL_SWITCH_IPC_SERVICE_PCTL_A;
    if (type == HANDLE_PCTL_SYSTEM_ROOT || type == HANDLE_PCTL_SYSTEM_CHILD) return SATCHEL_SWITCH_IPC_SERVICE_PCTL_S;
    if (type == HANDLE_PCTL_ROOT || type == HANDLE_PCTL_CHILD) return SATCHEL_SWITCH_IPC_SERVICE_PCTL;
    if (type == HANDLE_ECTX_APPLICATION) return SATCHEL_SWITCH_IPC_SERVICE_ECTX_AW;
    if (type == HANDLE_AOC_ROOT) return SATCHEL_SWITCH_IPC_SERVICE_AOC_U;
    if (type >= HANDLE_APPLET_ROOT && type <= HANDLE_DEBUG_FUNCTIONS) return SATCHEL_SWITCH_IPC_SERVICE_APPLET;
    return type == HANDLE_LM_SESSION || type == HANDLE_LOGGER ? SATCHEL_SWITCH_IPC_SERVICE_LM :
        (type == HANDLE_FSP_SESSION || type == HANDLE_STORAGE ? SATCHEL_SWITCH_IPC_SERVICE_FSP :
        (type == HANDLE_APM_ROOT || type == HANDLE_APM_SESSION ? SATCHEL_SWITCH_IPC_SERVICE_APM : SATCHEL_SWITCH_IPC_SERVICE_NONE));
}
// HIPC Close2 destroys this server endpoint, not the client's handle and not
// its sibling session. Atmosphere6b0c077b ServerSessionManager::ProcessRequest
// closes without a reply; KServerSession::CleanupRequests wakes a synchronous
// client with ResultSessionClosed. No TLS bytes are replaced with fake SFCO.
static int close_service_endpoint(SatchelSwitchProcess *p, const ProcessHandle *found) {
    size_t slot = (size_t)(found - p->handles);
    if (found->domain_slot) {
        ProcessDomain *domain = p->domains + found->domain_slot - 1;
        if (!domain->references) { p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_RESOURCE; return 0; }
        domain_release(p, found->domain_slot);
    }
    parental_release(p, found->service_slot);
    p->handles[slot].service_slot = 0;
    p->handles[slot].domain_slot = 0;
    p->handles[slot].client_process_id = 0;
    p->closed_session_mask |= UINT32_C(1) << slot;
    p->session_closes++; p->ipc_serviced++;
    satchel_cpu_probe_set_x(p->cpu, 0, 0xF601);
    return 1;
}
static int send_sync_request(SatchelSwitchProcess *p, uint32_t handle) {
    p->ipc_requests++;
    const ProcessHandle *found = lookup_handle(p, handle);
    if (!found || object_kind(found->type) == SATCHEL_SWITCH_IPC_OBJECT_NONE) {
        // No guest message is read or changed for an invalid/wrong-type handle.
        satchel_cpu_probe_set_x(p->cpu, 0, 0xE401);
        return 1;
    }
    // A closed server endpoint rejects before looking at guest TLS. Its client
    // handle remains valid/occupied until SVC CloseHandle; no token is reused.
    if (p->closed_session_mask & (UINT32_C(1) << (size_t)(found - p->handles))) {
        satchel_cpu_probe_set_x(p->cpu, 0, 0xF601); return 1;
    }
    p->last_ipc = (SatchelSwitchIPCInfo){0}; p->has_last_ipc = 1;
    p->last_ipc.object_kind = object_kind(found->type);
    p->last_ipc.service_kind = object_service(found->type);
    uint8_t message[SATCHEL_HIPC_MESSAGE_LIMIT];
    if (!satchel_cpu_probe_read(p->cpu, p->threads[p->active_thread].tls_address, message, sizeof(message))) {
        p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_TLS; return 0;
    }
    // Header bitfields are safe protocol metadata even when parsing fails.
    // Never retain/export arbitrary message bytes, names, PID claims or arguments.
    uint32_t header0 = ipc_read_word(message), header1 = ipc_read_word(message + 4);
    p->last_ipc.wire_type = header0 & 0xFFFF;
    p->last_ipc.receive_list_mode = (header1 >> 10) & 15;
    p->last_ipc.receive_list_offset = (header1 >> 20) & 0x7FF;
    p->last_ipc.reserved_header_bits = (header1 >> 14) & 63;
    p->last_ipc.raw_word_count = header1 & 1023;
    p->last_ipc.has_special_header = header1 >> 31;
    SatchelHIPCRequest request;
    SatchelHIPCStatus parsed = satchel_hipc_parse_request(message, sizeof(message), p->address_space_size,
        found->domain_slot ? SATCHEL_HIPC_DOMAIN : SATCHEL_HIPC_SESSION, &request);
    p->last_ipc.parser_status = (uint32_t)parsed;
    if (parsed != SATCHEL_HIPC_OK) { p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_PARSE; return 0; }
    p->last_ipc.kind = request.kind; p->last_ipc.command_id = request.command_id;
    if (request.kind == SATCHEL_HIPC_CLOSE) {
        // The validated codec admits only type2 with a zero second header word:
        // no raw words, PID, transferred handles, buffers or receive list.
        return close_service_endpoint(p, found);
    }
    if (found->domain_slot && (request.kind == SATCHEL_HIPC_DOMAIN_REQUEST || request.kind == SATCHEL_HIPC_DOMAIN_CLOSE)) {
        ProcessDomain *domain = p->domains + found->domain_slot - 1;
        DomainObject *object = NULL;
        for (size_t i = 0; i < SATCHEL_SWITCH_DOMAIN_OBJECT_LIMIT; i++)
            if (domain->objects[i].id == request.object_id) { object = domain->objects + i; break; }
        p->last_ipc.object_kind = object ? object_kind(object->type) : SATCHEL_SWITCH_IPC_OBJECT_NONE;
        if (object) p->last_ipc.service_kind = object_service(object->type);
        if (request.kind == SATCHEL_HIPC_DOMAIN_CLOSE) {
            // Close has no CMIF service response. Pinned Atmosphere6b0c077b
            // sf_cmif_domain_service_object.cpp returns without building one;
            // ProcessRequest uses the same input/output buffer. Preserve that
            // validated request envelope; libnx ignores this close reply.
            if (request.send_pid || request.buffer_count || !no_ipc_attachments(&request) || request.raw_size != 32) {
                p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SCHEMA; return 0;
            }
            if (!satchel_cpu_probe_write(p->cpu, p->threads[p->active_thread].tls_address, request.snapshot, request.consumed_size)) {
                p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_TLS; return 0;
            }
            // Public domain close unregisters an ID without requiring it to
            // exist. Repeated close is therefore an idempotent removal.
            if (object) { parental_release(p, object->service_slot); *object = (DomainObject){0}; }
            return finish_ipc(p);
        }
        if (!object) { p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_OBJECT; return 0; }
        ProcessHandle target = {0, object->type, object->client_process_id, found->domain_slot, 0, object->service_slot};
        return service_request(p, &target, &request);
    }
    if (found->type != HANDLE_SM_SESSION) return service_request(p, found, &request);
    int tipc = request.kind == SATCHEL_HIPC_TIPC_REQUEST;
    if ((!tipc && request.kind != SATCHEL_HIPC_REQUEST) || request.command_id > 1) {
        p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_COMMAND; return 0;
    }
    // Exact supported no-buffer SM schemas: v0/v1 CMIF has one8-byte
    // argument/raw count10. TIPC Initialize has PID+zero raw bytes; GetService
    // has no PID+eight raw bytes. TIPC has no CMIF header/alignment/placeholder.
    // Neither path accepts buffers or transferred handles.
    // Primary ABI: pinned libnx sm.c _smCmifCmdInPid/GetServiceOriginal and
    // Atmosphere sm_user_service.cpp expected request headers. Other schemas
    // stop without consuming handles, overwriting TLS or fabricating responses.
    if (request.buffer_count || request.copy_handle_count || request.move_handle_count ||
        request.input_object_count || request.raw_size != (tipc ? (request.command_id == 0 ? 0 : 8) : 40) ||
        request.send_pid != (request.command_id == 0 ? 1u : 0u)) {
        p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SCHEMA; return 0;
    }
    uint8_t argument[8] = {0};
    if ((!tipc || request.command_id == 1) && !satchel_hipc_copy_payload(&request, 0, argument, sizeof(argument))) {
        p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SCHEMA; return 0;
    }
    uint32_t service_result = 0;
    if (request.command_id == 1) {
        // SM InvalidClient takes precedence over name validation. A live SM
        // session is not an initialized client until its Initialize reply commits.
        if (!found->client_process_id) service_result = 0x415;
        else if (!valid_service_name(argument)) service_result = 0xC15;
        else {
            p->last_ipc.service_kind = classify_service(argument);
            if (p->last_ipc.service_kind == SATCHEL_SWITCH_IPC_SERVICE_LM || p->last_ipc.service_kind == SATCHEL_SWITCH_IPC_SERVICE_APM ||
                p->last_ipc.service_kind == SATCHEL_SWITCH_IPC_SERVICE_AOC_U ||
                p->last_ipc.service_kind == SATCHEL_SWITCH_IPC_SERVICE_ECTX_AW ||
                p->last_ipc.service_kind == SATCHEL_SWITCH_IPC_SERVICE_PCTL ||
                p->last_ipc.service_kind == SATCHEL_SWITCH_IPC_SERVICE_PCTL_S ||
                p->last_ipc.service_kind == SATCHEL_SWITCH_IPC_SERVICE_PCTL_A ||
                p->last_ipc.service_kind == SATCHEL_SWITCH_IPC_SERVICE_PCTL_R ||
                p->last_ipc.service_kind == SATCHEL_SWITCH_IPC_SERVICE_TIME ||
                !memcmp(argument, "appletOE", 8) ||
                (p->last_ipc.service_kind == SATCHEL_SWITCH_IPC_SERVICE_FSP && p->has_storage)) {
                size_t slot; uint32_t token;
                if (!prepare_object_handle(p, &slot, &token) || !write_ipc_reply(p, &request, 0, NULL, 0, &token)) return 0;
                unsigned type = p->last_ipc.service_kind == SATCHEL_SWITCH_IPC_SERVICE_LM ? HANDLE_LM_SESSION :
                    (p->last_ipc.service_kind == SATCHEL_SWITCH_IPC_SERVICE_APM ? HANDLE_APM_ROOT :
                    (p->last_ipc.service_kind == SATCHEL_SWITCH_IPC_SERVICE_AOC_U ? HANDLE_AOC_ROOT :
                    (p->last_ipc.service_kind == SATCHEL_SWITCH_IPC_SERVICE_ECTX_AW ? HANDLE_ECTX_APPLICATION :
                    (p->last_ipc.service_kind == SATCHEL_SWITCH_IPC_SERVICE_PCTL ? HANDLE_PCTL_ROOT :
                    (p->last_ipc.service_kind == SATCHEL_SWITCH_IPC_SERVICE_PCTL_S ? HANDLE_PCTL_SYSTEM_ROOT :
                    (p->last_ipc.service_kind == SATCHEL_SWITCH_IPC_SERVICE_PCTL_A ? HANDLE_PCTL_ADMIN_ROOT :
                    (p->last_ipc.service_kind == SATCHEL_SWITCH_IPC_SERVICE_PCTL_R ? HANDLE_PCTL_READONLY_ROOT :
                    (p->last_ipc.service_kind == SATCHEL_SWITCH_IPC_SERVICE_TIME ? HANDLE_TIME_USER_ROOT :
                    (!memcmp(argument, "appletOE", 8) ? HANDLE_APPLET_ROOT : HANDLE_FSP_SESSION)))))))));
                p->handles[slot] = (ProcessHandle){token, type, type == HANDLE_FSP_SESSION ? 0 : p->process_id, 0, 0, 0}; p->next_handle++;
                return finish_ipc(p);
            }
            // Only explicitly typed LM/APM/appletOE/AOC/ECTX/PCTL/TIME personas and attached read-only
            // current-program storage are registered. Other services stop.
            p->last_ipc.blocked_reason = SATCHEL_SWITCH_IPC_BLOCK_SERVICE; return 0;
        }
    }
    if (!write_ipc_reply(p, &request, service_result, NULL, 0, NULL)) return 0;
    if (request.command_id == 0) {
        // The kernel supplies the process identity. Neither the special-header
        // PID claim nor the8-byte user placeholder can impersonate another client.
        p->handles[(size_t)(found - p->handles)].client_process_id = p->process_id;
    }
    return finish_ipc(p);
}
// SetHeapSize ABI and validation: pinned Atmosphere kern_svc_physical_memory.cpp
// lines22..32, NX kern_k_memory_layout.hpp MainMemorySizeMax=8GiB, and
// kern_k_page_table_base.cpp SetHeapSize. Locked source pages cannot be discarded;
// valid resizing of a retained allocation stops until that CPU operation exists.
static int set_heap_size(SatchelSwitchProcess *p, uint64_t size, uint32_t *status) {
    p->heap_requests++;
    if ((size & UINT64_C(0x1FFFFF)) || size >= (UINT64_C(8) << 30)) {
        *status = 0xCA01; return 1;
    }
    uint64_t fixed_bytes = p->user_memory_used - p->heap_size;
    if (size > p->heap_region.size || size > p->user_memory_limit - fixed_bytes) {
        *status = 0xD001; return 1;
    }
    // Empty -> empty is a valid kernel no-op. The CPU deliberately rejects a
    // removal when no dynamic mapping exists, so do not ask it to remove one.
    if (size == p->heap_size) { *status = 0; return 1; }
    int heap_locked = 0;
    for (size_t i = 0; i < p->map_count; i++) {
        const SatchelSwitchMemoryInfo *entry = p->maps + i;
        if (!contains(p->heap_region, entry->address, entry->size) || !(entry->attributes & 1)) continue;
        heap_locked = 1;
        if (size < p->heap_size && entry->address + entry->size > p->heap_region.address + size) {
            *status = 0xD401; return 1; // Removed source pages are locked.
        }
    }
    // CPU resizing of an allocation retaining other live alias views is not
    // implemented. Preserve a valid request as an explicit SVC stop, not an
    // invented kernel error or a detached copy of the backing.
    if (heap_locked) return -1;
    for (size_t i = 0; i < p->map_count; i++) {
        const SatchelSwitchMemoryInfo *entry = p->maps + i;
        if (contains(p->heap_region, entry->address, entry->size) && entry->state == 5 &&
            entry->permissions != 3) return -1;
    }
    size_t index = 0;
    while (index < p->map_count && p->maps[index].address < p->heap_region.address) index++;
    if (p->heap_size) {
        if (index == p->map_count || p->maps[index].address != p->heap_region.address ||
            p->maps[index].size != p->heap_size || p->maps[index].state != 5 ||
            p->maps[index].permissions != 3) return 0;
    } else if (p->map_count == MAX_MAPS) {
        *status = 0xCE01; return 1; // OutOfResource: no mapping metadata slot.
    }
    size_t next = index + (p->heap_size ? 1 : 0);
    if ((index && p->maps[index - 1].address + p->maps[index - 1].size > p->heap_region.address) ||
        (next < p->map_count && p->heap_region.address + size > p->maps[next].address)) return 0;
    SatchelCPUMapResult result = satchel_cpu_probe_resize_rw(p->cpu, p->heap_region.address, size);
    if (result == SATCHEL_CPU_MAP_LIMIT || result == SATCHEL_CPU_MAP_ALLOCATION) {
        *status = 0xD001; return 1;
    }
    if (result != SATCHEL_CPU_MAP_OK) return 0;
    // All potentially failing work is complete. Publish the map and accounting
    // together, without any allocation or fallible operation after this point.
    if (!size) {
        memmove(p->maps + index, p->maps + index + 1, (p->map_count - index - 1) * sizeof(*p->maps));
        p->map_count--;
    } else if (p->heap_size) {
        p->maps[index].size = size;
    } else {
        memmove(p->maps + index + 1, p->maps + index, (p->map_count - index) * sizeof(*p->maps));
        p->maps[index] = (SatchelSwitchMemoryInfo){p->heap_region.address, size, 5, 0, 3, 0, 0, 0};
        p->map_count++;
    }
    p->heap_size = size;
    p->user_memory_used = fixed_bytes + size;
    *status = 0;
    return 1;
}
static int physical_entry(const SatchelSwitchProcess *p, const SatchelSwitchMemoryInfo *entry) {
    return entry->state == 5 && entry->permissions == 3 && !entry->attributes &&
        !entry->ipc_refcount && !entry->device_refcount &&
        contains(p->alias_region, entry->address, entry->size);
}
static int append_physical_plan(SatchelSwitchMemoryInfo planned[MAX_MAPS], size_t *count, SatchelSwitchMemoryInfo entry) {
    if (!entry.size) return 1;
    if (*count == MAX_MAPS) return 0;
    planned[(*count)++] = entry; return 1;
}
// Published Map/UnmapPhysicalMemory ABI: Atmosphere6b0c077b
// kern_svc_physical_memory.cpp and KPageTableBase Map/UnmapPhysicalMemory.
// This explicit application profile uses4KiB pages (the actual observed
// request is4KiB), owns private resource backing, and exposes only Normal RW
// physical allocations inside its reserved alias region. No host physical
// address, MMIO or existing-user-page alias is accepted by these SVCs.
static int shared_memory(SatchelSwitchProcess *p,uint32_t handle,uint64_t address,uint64_t size,uint32_t permission,int unmap,uint32_t *status) {
    if(unmap)p->time_info.shared_unmap_requests++;else p->time_info.shared_map_requests++;
    *status=0;
    // Atmosphere6b0c077b kern_svc_shared_memory.cpp validation order.
    if(address&4095){*status=0xcc01;return 1;}
    if(!size||(size&4095)){*status=0xca01;return 1;}
    if(address>UINT64_MAX-size){*status=0xd401;return 1;}
    if(!unmap && permission!=1 && permission!=3){*status=0xd801;return 1;}
    const ProcessHandle *object=lookup_handle(p,handle);
    if(!object||object->type!=HANDLE_TIME_SHARED_MEMORY){*status=0xe401;return 1;}
    // Both supported profiles' Shared/alias-code outer region equals ASLR.
    // Shared must additionally exclude Heap and physical Alias reservations.
    if(!contains(p->aslr_region,address,size)||
       (address<p->heap_region.address+p->heap_region.size&&p->heap_region.address<address+size)||
       (address<p->alias_region.address+p->alias_region.size&&p->alias_region.address<address+size)) {
        *status=0xdc01;return 1;
    }
    if(size!=4096){*status=0xca01;return 1;}
    if(!unmap && permission!=1){*status=0xd801;return 1;}
    size_t index=0;while(index<p->map_count&&p->maps[index].address<address)index++;
    if(unmap) {
        if(index==p->map_count||p->maps[index].address!=address||p->maps[index].size!=4096||
           p->maps[index].state!=6||p->maps[index].permissions!=1||p->maps[index].attributes) {
            *status=0xd401;return 1;
        }
    } else {
        if((index&&p->maps[index-1].address+p->maps[index-1].size>address)||
           (index<p->map_count&&p->maps[index].address<address+size)){*status=0xd401;return 1;}
        if(p->map_count==MAX_MAPS){*status=0xce01;return 1;}
    }
    SatchelCPUMapResult result=unmap?satchel_cpu_probe_shared_readonly_unmap(p->cpu,p->time_shared_token,address,size):
        satchel_cpu_probe_shared_readonly_map(p->cpu,p->time_shared_token,address,size);
    if(result!=SATCHEL_CPU_MAP_OK) {
        if(result==SATCHEL_CPU_MAP_ALLOCATION){*status=0xd001;return 1;}
        if(result==SATCHEL_CPU_MAP_LIMIT){*status=0xce01;return 1;}
        return 0; // Validated owned mappings must agree with CPU token/backing.
    }
    if(unmap) {
        memmove(p->maps+index,p->maps+index+1,(p->map_count-index-1)*sizeof(*p->maps));p->map_count--;
        p->time_info.shared_unmaps_serviced++;p->time_info.shared_mappings--;
    } else {
        memmove(p->maps+index+1,p->maps+index,(p->map_count-index)*sizeof(*p->maps));p->map_count++;
        p->maps[index]=(SatchelSwitchMemoryInfo){address,size,6,0,1,0,0,0};
        p->time_info.shared_maps_serviced++;p->time_info.shared_mappings++;
    }
    return 1;
}
static int physical_memory(SatchelSwitchProcess *p, uint64_t address, uint64_t size, int unmap, uint32_t *status) {
    if (unmap) p->physical_unmap_requests++; else p->physical_map_requests++;
    if (address & 4095) { *status = 0xCC01; return 1; }
    if (!size || (size & 4095)) { *status = 0xCA01; return 1; }
    if (address > UINT64_MAX - size) { *status = 0xDC01; return 1; }
    if (!satchel_switch_process_system_resource_total(p)) { *status = 0xFA01; return 1; }
    if (!contains(p->alias_region, address, size)) { *status = 0xDC01; return 1; }
    const uint64_t end = address + size;
    uint64_t already_mapped = 0; int protected_span = 0;
    for (size_t i = 0; i < p->map_count; i++) {
        const SatchelSwitchMemoryInfo *entry = p->maps + i;
        uint64_t entry_end = entry->address + entry->size;
        if (entry->address >= end || entry_end <= address) continue;
        if (entry->state == 5 && !entry->attributes && !entry->ipc_refcount && !entry->device_refcount &&
            (entry->permissions == 0 || entry->permissions == 1)) {
            protected_span = 1;
        } else if (!physical_entry(p, entry)) {
            if (unmap) { *status = 0xD401; return 1; }
            // MapPhysicalMemory counts already allocated pages even when a
            // MapMemory source is locked. A wholly mapped range is a real
            // no-op; filling gaps around live aliases remains unsupported.
            if (entry->state != 5 || entry->permissions || entry->attributes != 1 ||
                entry->ipc_refcount || entry->device_refcount) return 0;
        }
        uint64_t lo = entry->address > address ? entry->address : address;
        uint64_t hi = entry_end < end ? entry_end : end;
        already_mapped += hi - lo;
    }
    uint64_t changed = unmap ? already_mapped : size - already_mapped;
    if (!changed) { *status = 0; return 1; } // Real unchanged state, not fabricated allocation.
    if (protected_span) return -1; // No truthful protected-allocation reshape yet.
    // Dynamic backing operations cannot replace an allocation with surviving
    // alias views. Expand across its contiguous Normal metadata fragments;
    // an otherwise valid operation on an unlocked fragment stops explicitly.
    uint64_t allocation_start = address, allocation_end = end;
    int locked_neighbor = 0;
    for (size_t pass = 0; pass < p->map_count; pass++) for (size_t i = 0; i < p->map_count; i++) {
        const SatchelSwitchMemoryInfo *entry = p->maps + i;
        if (entry->state != 5 || !contains(p->alias_region, entry->address, entry->size) ||
            entry->address > allocation_end || entry->address + entry->size < allocation_start) continue;
        if (entry->address < allocation_start) allocation_start = entry->address;
        if (entry->address + entry->size > allocation_end) allocation_end = entry->address + entry->size;
        if ((entry->attributes & 1) || entry->permissions != 3) locked_neighbor = 1;
    }
    if (locked_neighbor) return -1;
    if (!unmap && changed > p->user_memory_limit - p->user_memory_used) { *status = 0x10801; return 1; }
    if (unmap && changed > p->physical_size) return 0;
    SatchelSwitchMemoryInfo planned[MAX_MAPS]; size_t count = 0;
    uint64_t map_start = address, map_end = end;
    if (unmap) {
        for (size_t i = 0; i < p->map_count; i++) {
            SatchelSwitchMemoryInfo entry = p->maps[i];
            uint64_t entry_end = entry.address + entry.size;
            if (entry.address >= end || entry_end <= address) {
                if (!append_physical_plan(planned, &count, entry)) { *status = 0xCE01; return 1; }
            } else {
                if (entry.address < address) {
                    SatchelSwitchMemoryInfo left = entry; left.size = address - entry.address;
                    if (!append_physical_plan(planned, &count, left)) { *status = 0xCE01; return 1; }
                }
                if (entry_end > end) {
                    entry.address = end; entry.size = entry_end - end;
                    if (!append_physical_plan(planned, &count, entry)) { *status = 0xCE01; return 1; }
                }
            }
        }
    } else {
        // Expand only over adjacent/overlapping OWNED physical spans. The CPU
        // coalesces this explicit range so valid cross-page guest accesses work;
        // never absorb a separately resized heap beyond the alias boundary.
        for (size_t i = 0; i < p->map_count; i++) {
            const SatchelSwitchMemoryInfo *entry = p->maps + i;
            if (physical_entry(p, entry) && entry->address <= map_end && entry->address + entry->size >= map_start) {
                if (entry->address < map_start) map_start = entry->address;
                if (entry->address + entry->size > map_end) map_end = entry->address + entry->size;
            }
        }
        int inserted = 0;
        for (size_t i = 0; i < p->map_count; i++) {
            SatchelSwitchMemoryInfo entry = p->maps[i];
            if (!inserted && entry.address >= map_start) {
                SatchelSwitchMemoryInfo added = {map_start, map_end - map_start, 5, 0, 3, 0, 0, 0};
                if (!append_physical_plan(planned, &count, added)) { *status = 0xCE01; return 1; }
                inserted = 1;
            }
            if (entry.address >= map_start && entry.address < map_end) continue;
            if (!append_physical_plan(planned, &count, entry)) { *status = 0xCE01; return 1; }
        }
        if (!inserted) {
            SatchelSwitchMemoryInfo added = {map_start, map_end - map_start, 5, 0, 3, 0, 0, 0};
            if (!append_physical_plan(planned, &count, added)) { *status = 0xCE01; return 1; }
        }
    }
    uint64_t actual = UINT64_MAX;
    SatchelCPUMapResult result = unmap ? satchel_cpu_probe_unmap_rw(p->cpu, address, size, &actual) :
        satchel_cpu_probe_map_missing_rw(p->cpu, map_start, map_end - map_start, &actual);
    if (result == SATCHEL_CPU_MAP_ALLOCATION) { *status = 0xD001; return 1; }
    if (result == SATCHEL_CPU_MAP_LIMIT) { *status = 0xCE01; return 1; }
    if (result != SATCHEL_CPU_MAP_OK || actual != changed) return 0;
    // Every fallible operation completed. Publish actual backing and accounting
    // together using the live arena map table, with no further allocation.
    memcpy(p->maps, planned, count * sizeof(*p->maps));
    memset(p->maps + count, 0, (MAX_MAPS - count) * sizeof(*p->maps)); p->map_count = count;
    if (unmap) { p->physical_size -= changed; p->user_memory_used -= changed; }
    else { p->physical_size += changed; p->user_memory_used += changed; }
    *status = 0; return 1;
}
static int memory_properties_equal(const SatchelSwitchMemoryInfo *a, const SatchelSwitchMemoryInfo *b) {
    return a->state == b->state && a->permissions == b->permissions && a->attributes == b->attributes &&
        a->ipc_refcount == b->ipc_refcount && a->device_refcount == b->device_refcount;
}
static int alias_span(const SatchelSwitchProcess *p, uint64_t address, uint64_t size, int source, int unmap) {
    uint64_t cursor = address, end = address + size; uint32_t state = 0;
    for (size_t i = 0; i < p->map_count && cursor < end; i++) {
        const SatchelSwitchMemoryInfo *entry = p->maps + i;
        uint64_t entry_end = entry->address + entry->size;
        if (entry_end <= cursor) continue;
        if (entry->address > cursor) return 0;
        if (source) {
            if ((entry->state != 4 && entry->state != 5) || (state && state != entry->state) ||
                entry->permissions != (unmap ? 0u : 3u) || entry->attributes != (unmap ? 1u : 0u)) return 0;
            state = entry->state;
        } else if (entry->state != 0xB || entry->permissions != 3 || entry->attributes) return 0;
        if (entry->ipc_refcount || entry->device_refcount) return 0;
        cursor = entry_end < end ? entry_end : end;
    }
    return cursor == end;
}
static int alias_mergeable(const SatchelSwitchProcess *p, const SatchelSwitchMemoryInfo *a, const SatchelSwitchMemoryInfo *b) {
    if (a->address + a->size != b->address || !memory_properties_equal(a, b)) return 0;
    // Restore source allocation shape without absorbing a separate heap into
    // adjacent physical allocation, or distinct stack allocations into one map.
    if (a->state == 4) return 1;
    return a->state == 5 && ((contains(p->heap_region, a->address, a->size) && contains(p->heap_region, b->address, b->size)) ||
        (contains(p->alias_region, a->address, a->size) && contains(p->alias_region, b->address, b->size)));
}
// SVC2, independently implemented from Atmosphere6b0c077b kern_svc_memory.cpp
// and KPageTableBase::SetMemoryPermission/CheckMemoryState. Unlike process-code
// reprotection, this call never grants execution or changes memory state.
static int set_memory_permission(SatchelSwitchProcess *p, uint64_t address, uint64_t size,
                                 uint32_t permission, uint32_t *status) {
    p->permission_requests++;
    if (address & 4095) { *status = 0xCC01; return 1; }
    if (!size || (size & 4095)) { *status = 0xCA01; return 1; }
    if (address > UINT64_MAX - size) { *status = 0xD401; return 1; }
    if (permission != 0 && permission != 1 && permission != 3) { *status = 0xD801; return 1; }
    if (address >= p->address_space_size || size > p->address_space_size - address) {
        *status = 0xD401; return 1;
    }
    uint64_t end = address + size, cursor = address;
    uint32_t state = 0, old_permission = 0;
    for (size_t i = 0; i < p->map_count && cursor < end; i++) {
        const SatchelSwitchMemoryInfo *entry = p->maps + i;
        uint64_t entry_end = entry->address + entry->size;
        if (entry_end <= cursor) continue;
        if (entry->address > cursor || (entry->state != 4 && entry->state != 5) ||
            entry->attributes || entry->ipc_refcount || entry->device_refcount ||
            (state && (entry->state != state || entry->permissions != old_permission))) {
            *status = 0xD401; return 1;
        }
        state = entry->state; old_permission = entry->permissions;
        cursor = entry_end < end ? entry_end : end;
    }
    if (cursor != end) { *status = 0xD401; return 1; }
    if (old_permission == permission) { p->permission_serviced++; *status = 0; return 1; }
    SatchelSwitchMemoryInfo plan[MAX_MAPS + 2]; size_t count = 0;
    for (size_t i = 0; i < p->map_count; i++) {
        SatchelSwitchMemoryInfo entry = p->maps[i]; uint64_t entry_end = entry.address + entry.size;
        if (entry.address < end && entry_end > address) {
            uint64_t lo = entry.address > address ? entry.address : address;
            uint64_t hi = entry_end < end ? entry_end : end;
            if (entry.address < lo) { SatchelSwitchMemoryInfo left = entry; left.size = lo - entry.address; plan[count++] = left; }
            SatchelSwitchMemoryInfo middle = entry; middle.address = lo; middle.size = hi - lo;
            middle.permissions = permission; plan[count++] = middle;
            if (hi < entry_end) { entry.address = hi; entry.size = entry_end - hi; plan[count++] = entry; }
        } else plan[count++] = entry;
    }
    size_t compact = 0;
    for (size_t i = 0; i < count; i++) {
        if (compact && alias_mergeable(p, plan + compact - 1, plan + i)) plan[compact - 1].size += plan[i].size;
        else plan[compact++] = plan[i];
    }
    if (compact > MAX_MAPS) { *status = 0xCE01; return 1; }
    SatchelCPUMapResult result = satchel_cpu_probe_set_permissions(p->cpu, address, size, old_permission, permission);
    if (result == SATCHEL_CPU_MAP_LIMIT) { *status = 0xCE01; return 1; }
    if (result == SATCHEL_CPU_MAP_ALLOCATION) { *status = 0xCE01; return 1; } // Metadata allocation, not new backing.
    if (result != SATCHEL_CPU_MAP_OK) return 0;
    memcpy(p->maps, plan, compact * sizeof(*plan));
    memset(p->maps + compact, 0, (MAX_MAPS - compact) * sizeof(*p->maps)); p->map_count = compact;
    p->permission_serviced++; p->permission_changed_bytes += size;
    *status = 0; return 1;
}
// SVC3 PermissionLocked is a one-way memory-block attribute. The pinned
// kernel changes page-table/cache properties only for an Uncached mask. This
// slice enforces the lock through every owned permission/alias preflight; it
// does not invent uncached host backing or cache-flush success.
static int set_memory_attribute(SatchelSwitchProcess *p, uint64_t address, uint64_t size,
                                uint32_t mask, uint32_t attributes, uint32_t *status) {
    p->attribute_requests++;
    if (address & 4095) { *status = 0xCC01; return 1; }
    if (!size || (size & 4095)) { *status = 0xCA01; return 1; }
    if (address > UINT64_MAX - size) { *status = 0xD401; return 1; }
    if ((mask | attributes) != mask || ((mask | attributes) & ~UINT32_C(24)) ||
        (mask & 16) != (attributes & 16)) { *status = 0xE801; return 1; }
    if (address >= p->address_space_size || size > p->address_space_size - address) {
        *status = 0xD401; return 1;
    }
    uint64_t end = address + size, cursor = address;
    uint32_t state = 0, permission = 0; int first = 1;
    while (cursor < end) {
        SatchelSwitchMemoryInfo entry;
        if (!satchel_switch_process_query(p, cursor, &entry)) return 0;
        if ((!first && (entry.state != state || entry.permissions != permission)) ||
            (entry.attributes & ~UINT32_C(28)) ||
            ((mask & 16) && entry.state != 4) ||
            ((mask & 8) && entry.state != 4 && entry.state != 5)) {
            *status = 0xD401; return 1;
        }
        // SetMask and DeviceShared differences are ignored by this kernel
        // preflight; the transaction preserves each block's unmasked bits.
        state = entry.state; permission = entry.permissions; first = 0;
        uint64_t entry_end = entry.address + entry.size;
        if (entry_end <= cursor) return 0;
        cursor = entry_end < end ? entry_end : end;
    }
    if (mask & 8) return -1; // Valid cache-attribute requests remain explicit stops.
    if (!mask) { p->attribute_serviced++; *status = 0; return 1; }
    SatchelSwitchMemoryInfo plan[MAX_MAPS + 2]; size_t count = 0; uint64_t newly_locked = 0;
    for (size_t i = 0; i < p->map_count; i++) {
        SatchelSwitchMemoryInfo entry = p->maps[i]; uint64_t entry_end = entry.address + entry.size;
        if (entry.address < end && entry_end > address) {
            uint64_t lo = entry.address > address ? entry.address : address;
            uint64_t hi = entry_end < end ? entry_end : end;
            if (entry.address < lo) { SatchelSwitchMemoryInfo left = entry; left.size = lo - entry.address; plan[count++] = left; }
            SatchelSwitchMemoryInfo middle = entry; middle.address = lo; middle.size = hi - lo;
            if (!(middle.attributes & 16)) newly_locked += hi - lo;
            middle.attributes = (middle.attributes & ~mask) | (attributes & mask); plan[count++] = middle;
            if (hi < entry_end) { entry.address = hi; entry.size = entry_end - hi; plan[count++] = entry; }
        } else plan[count++] = entry;
    }
    size_t compact = 0;
    for (size_t i = 0; i < count; i++) {
        if (compact && alias_mergeable(p, plan + compact - 1, plan + i)) plan[compact - 1].size += plan[i].size;
        else plan[compact++] = plan[i];
    }
    if (compact > MAX_MAPS) { *status = 0xCE01; return 1; }
    // No fallible operations follow the bounded metadata plan. No CPU backing,
    // permissions, reservations, guest bytes or memory charges change.
    memcpy(p->maps, plan, compact * sizeof(*plan));
    memset(p->maps + compact, 0, (MAX_MAPS - compact) * sizeof(*p->maps)); p->map_count = compact;
    p->attribute_serviced++; p->permission_locked_bytes += newly_locked;
    *status = 0; return 1;
}
// Original bounded metadata transaction based on published Map/UnmapMemory ABI
// (Atmosphere6b0c077b kern_svc_memory.cpp/KPageTableBase). CPU retains backing
// identity and performs the alias operation atomically; no bytes are copied
// into a replacement allocation and no additional physical memory is charged.
static int alias_memory(SatchelSwitchProcess *p, uint64_t destination, uint64_t source, uint64_t size,
                        int unmap, uint32_t *status) {
    if (unmap) p->alias_unmap_requests++; else p->alias_map_requests++;
    if ((destination | source) & 4095) { *status = 0xCC01; return 1; }
    if (!size || (size & 4095)) { *status = 0xCA01; return 1; }
    if (source > UINT64_MAX - size || destination > UINT64_MAX - size ||
        source >= p->address_space_size || size > p->address_space_size - source) {
        *status = 0xD401; return 1;
    }
    if (!contains(p->stack_region, destination, size)) { *status = 0xDC01; return 1; }
    if (!alias_span(p, source, size, 1, unmap)) { *status = 0xD401; return 1; }
    uint64_t source_end = source + size, destination_end = destination + size;
    if (unmap) {
        if (!alias_span(p, destination, size, 0, 1)) { *status = 0xD401; return 1; }
        if (size > p->alias_size) { *status = 0xDC01; return 1; }
    } else {
        for (size_t i = 0; i < p->map_count; i++) if (p->maps[i].address < destination_end &&
            p->maps[i].address + p->maps[i].size > destination) { *status = 0xD401; return 1; }
    }
    SatchelSwitchMemoryInfo plan[MAX_MAPS + 3]; size_t count = 0;
    for (size_t i = 0; i < p->map_count; i++) {
        SatchelSwitchMemoryInfo entry = p->maps[i]; uint64_t end = entry.address + entry.size;
        if (entry.address < source_end && end > source) {
            uint64_t lo = entry.address > source ? entry.address : source;
            uint64_t hi = end < source_end ? end : source_end;
            if (entry.address < lo) { SatchelSwitchMemoryInfo part = entry; part.size = lo - entry.address; plan[count++] = part; }
            SatchelSwitchMemoryInfo middle = entry; middle.address = lo; middle.size = hi - lo;
            middle.permissions = unmap ? 3 : 0; middle.attributes = unmap ? 0 : 1; plan[count++] = middle;
            if (hi < end) { entry.address = hi; entry.size = end - hi; plan[count++] = entry; }
        } else if (unmap && entry.address < destination_end && end > destination) {
            if (entry.address < destination) { SatchelSwitchMemoryInfo part = entry; part.size = destination - entry.address; plan[count++] = part; }
            if (end > destination_end) { entry.address = destination_end; entry.size = end - destination_end; plan[count++] = entry; }
        } else plan[count++] = entry;
    }
    if (!unmap) {
        size_t insert = 0; while (insert < count && plan[insert].address < destination) insert++;
        memmove(plan + insert + 1, plan + insert, (count - insert) * sizeof(*plan));
        plan[insert] = (SatchelSwitchMemoryInfo){destination, size, 0xB, 0, 3, 0, 0, 0}; count++;
    }
    size_t compact = 0;
    for (size_t i = 0; i < count; i++) {
        if (compact && alias_mergeable(p, plan + compact - 1, plan + i)) plan[compact - 1].size += plan[i].size;
        else plan[compact++] = plan[i];
    }
    if (compact > MAX_MAPS) { *status = 0xCE01; return 1; }
    SatchelCPUMapResult result = unmap ? satchel_cpu_probe_unmap_alias(p->cpu, destination, source, size) :
        satchel_cpu_probe_map_alias(p->cpu, destination, source, size);
    if (result == SATCHEL_CPU_MAP_LIMIT) { *status = 0xCE01; return 1; }
    if (result == SATCHEL_CPU_MAP_ALLOCATION) { *status = 0xD001; return 1; }
    if (result == SATCHEL_CPU_MAP_INVALID && unmap) { *status = 0xDC01; return 1; }
    if (result != SATCHEL_CPU_MAP_OK) return 0;
    memcpy(p->maps, plan, compact * sizeof(*plan)); p->map_count = compact;
    if (unmap) { p->alias_size -= size; p->alias_unmap_serviced++; }
    else { p->alias_size += size; p->alias_map_serviced++; }
    *status = 0; return 1;
}
int satchel_switch_process_metadata_supported(const SatchelProcessMetadata *metadata) {
    return metadata && metadata->is_64_bit &&
        ((metadata->address_space_type == 1 && metadata->address_space_bits == 36) ||
         (metadata->address_space_type == 3 && metadata->address_space_bits == 39)) &&
        (metadata->flags0 & 15) == (1 | (metadata->address_space_type << 1)) &&
        !(metadata->flags0 & 0xC0) &&
        metadata->flags1 == 0 &&
        metadata->address_space_size == (UINT64_C(1) << metadata->address_space_bits) &&
        metadata->main_thread_stack_size && !(metadata->main_thread_stack_size & 4095) &&
        metadata->main_thread_stack_size <= SATCHEL_SWITCH_PROCESS_STACK_LIMIT &&
        metadata->system_resource_size % (2 * 1024 * 1024) == 0 &&
        metadata->system_resource_size < SATCHEL_SWITCH_PROCESS_MEMORY_LIMIT &&
        metadata->main_thread_priority <= 63 && metadata->default_cpu_id <= 3 &&
        (metadata->has_core_priority ?
            (contiguous_mask(metadata->allowed_core_mask) && !(metadata->allowed_core_mask & ~UINT64_C(15)) &&
             contiguous_mask(metadata->allowed_priority_mask) && !(metadata->allowed_priority_mask & 15) &&
             (metadata->allowed_core_mask & (UINT64_C(1) << metadata->default_cpu_id)) &&
             (metadata->allowed_priority_mask & (UINT64_C(1) << metadata->main_thread_priority))) :
            (!metadata->allowed_core_mask && !metadata->allowed_priority_mask));
}
static SatchelSwitchProcess *create_process(const SatchelSwitchProcessModule *, size_t,
    const SatchelProcessMetadata *, const SatchelSwitchDispatchCode *, int, char *, size_t);
SatchelSwitchProcess *satchel_switch_process_create(const SatchelSwitchProcessModule *modules, size_t count,
        const SatchelProcessMetadata *metadata, char *error, size_t capacity) {
    return satchel_switch_process_create_with_dispatch(modules, count, metadata, NULL, error, capacity);
}
SatchelSwitchProcess *satchel_switch_process_create_with_dispatch(const SatchelSwitchProcessModule *modules, size_t count,
        const SatchelProcessMetadata *metadata, const SatchelSwitchDispatchCode *dispatch, char *error, size_t capacity) {
    return create_process(modules, count, metadata, dispatch, 0, error, capacity);
}
SatchelSwitchProcess *satchel_switch_process_create_with_dispatch_and_identity(const SatchelSwitchProcessModule *modules, size_t count,
        const SatchelProcessMetadata *metadata, const SatchelSwitchDispatchCode *dispatch, char *error, size_t capacity) {
    return create_process(modules, count, metadata, dispatch, 1, error, capacity);
}
static SatchelSwitchProcess *create_process(const SatchelSwitchProcessModule *modules, size_t count,
        const SatchelProcessMetadata *metadata, const SatchelSwitchDispatchCode *dispatch, int identity, char *error, size_t capacity) {
    if (!modules || count < 2 || count > MAX_MODULES || !satchel_switch_process_metadata_supported(metadata)) {
        message(error, capacity, "Process metadata exceeds the supported 64-bit diagnostic profile."); return NULL;
    }
    if (dispatch && (!dispatch->bytes || dispatch->size < 4 || (dispatch->size & 3) ||
            dispatch->size > SATCHEL_SWITCH_PROCESS_DISPATCH_LIMIT)) {
        message(error, capacity, "The optional guest dispatch code exceeds its bounded code format."); return NULL;
    }
    SatchelSwitchProcess *p = calloc(1, sizeof(*p));
    if (!p) { message(error, capacity, "Unable to allocate the process diagnostic."); return NULL; }
    p->maps = p->fallback_maps;
    p->program_id = metadata->program_id;
    p->address_space_size = metadata->address_space_size; p->module_count = count;
    p->log_destination = 1;
    p->performance_mode = SATCHEL_SWITCH_PERFORMANCE_NORMAL;
    // Explicit created/background application, handheld profile. Host launch
    // sets InFocus before guest startup/AcquireForegroundRights. iOS scene
    // deactivation cancels this bounded run instead of inventing guest resume.
    p->applet.focus_state = 3; p->applet.operation_mode = 0;
    p->has_core_priority = metadata->has_core_priority;
    p->allowed_core_mask = metadata->allowed_core_mask;
    p->allowed_priority_mask = metadata->allowed_priority_mask;
    SatchelCPUProbeRegion regions[MAX_REGIONS]; size_t region_count = 0;
    uint64_t base = code_base, total = 0, backed_code = 0;
    int previous_order = -1;
    for (size_t m = 0; m < count; m++) {
        const SatchelSwitchProcessModule *module = modules + m;
        int order = module_order(module->name);
        if (order <= previous_order || (m == 0 && order != 0) || (m == 1 && order != 1) ||
            !module->image || module->image_size != module->info.image_size || !module->image_size ||
            module->image_size > SATCHEL_NSO_IMAGE_LIMIT || (module->info.flags & ~63u)) goto invalid;
        previous_order = order; p->module_addresses[m] = base;
        uint64_t module_size = page_up(module->image_size), cursor = 0;
        if (module_size > SATCHEL_SWITCH_PROCESS_MEMORY_LIMIT - total) goto invalid;
        total += module_size;
        for (unsigned s = 0; s < 3; s++) {
            SatchelNSOSegment segment = module->info.segments[s];
            uint64_t length = segment.size + (s == 2 ? (uint64_t)module->info.bss_size : 0);
            if ((segment.memory_offset & 4095) || segment.memory_offset < cursor ||
                segment.memory_offset > module->image_size || length > module->image_size - segment.memory_offset ||
                (s == 0 && (segment.memory_offset || segment.size < 4 || (segment.size & 3)))) goto invalid;
            if (segment.memory_offset > cursor) map(p, base + cursor, segment.memory_offset - cursor, 3, 0);
            uint64_t rounded = page_up(length);
            if (rounded) {
                backed_code += rounded;
                uint32_t permissions = s == 0 ? 5 : (s == 1 ? 1 : 3);
                map(p, base + segment.memory_offset, rounded, s == 2 ? 4 : 3, permissions);
                regions[region_count++] = (SatchelCPUProbeRegion){base + segment.memory_offset,
                    module->image + segment.memory_offset, segment.size, (size_t)rounded, permissions};
            }
            cursor = segment.memory_offset + rounded;
        }
        if (cursor != module_size) goto invalid;
        base += module_size;
    }
    const size_t stack_size = (size_t)page_up(metadata->main_thread_stack_size);
    p->threads[0].stack_size = stack_size;
    uint64_t dispatch_size = dispatch ? page_up(dispatch->size) : 0;
    const uint64_t identity_size = identity ? SATCHEL_SWITCH_IDENTITY_SIZE : 0;
    if (total + stack_size + 4096 + dispatch_size + identity_size > SATCHEL_SWITCH_PROCESS_MEMORY_LIMIT - metadata->system_resource_size ||
            !layout(p, metadata->address_space_bits, base)) goto invalid;
    // Explicit emulator resource policy, not host RSS: NPDM system-resource
    // arena capacity is withheld from the fixed guest backing budget without
    // creating guest memory. No-access NSO holes are also withheld,
    // because constructor/preflight conservatively charge whole decoded images.
    // Only CPU-backed page-rounded segments (including BSS/padding), stack, TLS
    // and generated code/opaque identity backing count as used guest bytes.
    // Unbacked virtual-region reservations are not usage.
    p->user_memory_limit = SATCHEL_SWITCH_PROCESS_MEMORY_LIMIT - metadata->system_resource_size - (total - backed_code);
    p->user_memory_used = backed_code + stack_size + 4096 + dispatch_size + identity_size;
    uint64_t stack_base = p->threads[0].stack_address;
    if (dispatch) {
        // Page-table policy chooses the address; untrusted sizes/callers cannot
        // request an overlap or rewrite an NSO. No original module is moved.
        p->dispatch_region = (ProcessRegion){(base + 0x1FFFFF) & ~UINT64_C(0x1FFFFF), dispatch_size};
        uint64_t dispatch_end = p->dispatch_region.address + dispatch_size;
        if (p->dispatch_region.address < base || !contains(p->kernel_region, p->dispatch_region.address, dispatch_size) ||
                (p->dispatch_region.address < tls_base + 4096 && tls_base < dispatch_end) ||
                (p->dispatch_region.address < stack_base + stack_size && stack_base < dispatch_end)) goto invalid;
        // This is generated emulator-owned guest code, not an NSO Code block.
        // Report the real RX allocation as GeneratedCode (public state 0x14),
        // matching the generated-code mapping category in Atmosphere 6b0c077b,
        // kern_k_code_memory.cpp MapToOwner. Code (3) would incorrectly invite
        // runtime linkers to interpret these instructions as an NSO/MOD0 image.
        // We do not emulate CodeMemory handles, writable aliases or its SVCs.
        map(p, p->dispatch_region.address, dispatch_size, 0x14, 5);
        regions[region_count++] = (SatchelCPUProbeRegion){p->dispatch_region.address, dispatch->bytes,
            dispatch->size, (size_t)dispatch_size, SATCHEL_CPU_READ | SATCHEL_CPU_EXECUTE};
    }
    if (identity) {
        uint64_t address = dispatch ? p->dispatch_region.address + dispatch_size :
            (base + 0x1FFFFF) & ~UINT64_C(0x1FFFFF);
        uint64_t end = address + identity_size;
        if (address < base || end < address || !contains(p->kernel_region, address, identity_size) ||
                (address < tls_base + (SATCHEL_SWITCH_THREAD_LIMIT / 8) * 4096 && tls_base < end) ||
                (address < stack_base + stack_size && stack_base < end) ||
                (address < p->alias_region.address + p->alias_region.size && p->alias_region.address < end) ||
                (address < p->heap_region.address + p->heap_region.size && p->heap_region.address < end) ||
                p->map_count == MAX_MAPS || region_count == MAX_REGIONS) goto invalid;
        p->identity = (SatchelSwitchIdentityReservation){SATCHEL_SWITCH_IDENTITY_VERSION, sizeof(p->identity),
            0, allocate_identity_generation(), address, identity_size};
        if (!p->identity.generation) goto invalid;
        // CPU construction cannot accept NONE. This unpublished temporary RW
        // allocation is protected before the Process can escape the constructor.
        map(p, address, identity_size, 0x10, 0);
        regions[region_count++] = (SatchelCPUProbeRegion){address, NULL, 0, (size_t)identity_size, 3};
    }
    // Reservations are not allocations: only actual TLS and main-thread stack
    // pages enter QueryMemory/CPU maps. Keep those maps ordered by address.
    if (tls_base < stack_base) map(p, tls_base, 4096, 0xC, 3);
    map(p, stack_base, stack_size, 0xB, 3);
    if (tls_base >= stack_base) map(p, tls_base, 4096, 0xC, 3);
    regions[region_count++] = (SatchelCPUProbeRegion){stack_base, NULL, 0, stack_size, 3};
    // Initial diagnostic thread handle in a zeroed guest TLS region. Guest
    // thread registers contain only guest addresses, never a host TLS pointer.
    uint8_t tls[4096] = {0};
    p->threads[0].handle = 0x80;
    p->handles[0] = (ProcessHandle){p->threads[0].handle, HANDLE_THREAD, 0, 0, 0, 0};
    p->next_handle = 0x81;
    p->threads[0].priority = metadata->main_thread_priority;
    p->threads[0].effective_priority = metadata->main_thread_priority;
    p->threads[0].cpu_id = metadata->default_cpu_id; p->threads[0].stack_size = stack_size;
    // KThread's initial virtual affinity is the one ideal CPU, not the wider
    // process permission mask. No KAC-derived allowed-core mask is claimed.
    p->threads[0].affinity_mask = UINT64_C(1) << p->threads[0].cpu_id;
    p->threads[0].ideal_core_id = p->threads[0].cpu_id;
    p->threads[0].id = allocate_thread_id();
    p->process_id = allocate_process_id(); p->applet_resource_id = allocate_applet_resource_id();
    if (identity) p->identity.process_id = p->process_id;
    if (!p->threads[0].id || !p->process_id || !p->applet_resource_id) {
        message(error, capacity, "The owned process identity namespace is exhausted."); free(p); return NULL;
    }
    p->threads[0].tls_address = tls_base;
    memcpy(tls + 0x110, &p->threads[0].handle, sizeof(p->threads[0].handle));
    regions[region_count++] = (SatchelCPUProbeRegion){tls_base, tls, sizeof(tls), sizeof(tls), 3};
    if (metadata->system_resource_size) {
        // Actual emulator-owned system-resource backing, allocated only after
        // all layout/budget validation. The first page owns the map metadata
        // used by QueryMemory and heap mutations, not an accounting placeholder.
        // No guest address, hardware page table or host RSS is claimed.
        p->resources = satchel_switch_resources_create(metadata->system_resource_size);
        SatchelSwitchMemoryInfo *owned_maps = satchel_switch_resources_allocate(p->resources, sizeof(p->fallback_maps));
        if (!owned_maps) {
            satchel_switch_resources_destroy(p->resources);
            message(error, capacity, "Unable to allocate the owned process system-resource arena.");
            free(p); return NULL;
        }
        memcpy(owned_maps, p->fallback_maps, sizeof(p->fallback_maps));
        memset(p->fallback_maps, 0, sizeof(p->fallback_maps)); p->maps = owned_maps;
    }
    p->cpu = satchel_cpu_probe_create(regions, region_count, code_base, error, capacity);
    if (!p->cpu) { satchel_switch_resources_destroy(p->resources); free(p); return NULL; }
    if (identity && satchel_cpu_probe_set_permissions(p->cpu, p->identity.address, identity_size, 3, 0) != SATCHEL_CPU_MAP_OK) {
        satchel_cpu_probe_destroy(p->cpu); satchel_switch_resources_destroy(p->resources); free(p);
        message(error, capacity, "Unable to protect the owned opaque identity reservation."); return NULL;
    }
    // The kernel's initial user context has zeroed SIMD registers. Keep VIXL's
    // diagnostic NaNs for generic CPU clients; initialize only this owned
    // Horizon main thread here, before any guest instruction can execute.
    const uint8_t zero_q[16] = {0};
    for (unsigned i = 0; i < 32; i++) if (!satchel_cpu_probe_set_q(p->cpu, i, zero_q)) {
        satchel_cpu_probe_destroy(p->cpu); satchel_switch_resources_destroy(p->resources); free(p);
        message(error, capacity, "Unable to initialize the owned thread SIMD context."); return NULL;
    }
    p->thread_count = 1; p->threads[0].cpu = p->cpu;
    p->tls_pages = 1; p->initial_cpu_id = metadata->default_cpu_id;
    p->threads[0].state = SATCHEL_SWITCH_THREAD_RUNNABLE; p->threads[0].handle_references = 1;
    p->threads[0].run_reference = 1; p->threads[0].deadline = UINT64_MAX;
    satchel_cpu_probe_set_tls(p->cpu, p->threads[0].tls_address, 0);
    satchel_cpu_probe_set_sp(p->cpu, stack_base + stack_size);
    satchel_cpu_probe_set_x(p->cpu, 0, 0); satchel_cpu_probe_set_x(p->cpu, 1, p->threads[0].handle);
    uint64_t cfi_cookie; arc4random_buf(&cfi_cookie, sizeof(cfi_cookie));
    satchel_cpu_probe_set_x(p->cpu, 18, cfi_cookie | 1);
    // Four independent words are generated once for this process. Repeated
    // GetInfo(RandomEntropy, subtype) calls return that same process-owned word.
    arc4random_buf(p->random_entropy, sizeof(p->random_entropy));
    p->last_thread_stop = satchel_cpu_probe_run(p->cpu,0,NULL,NULL);
    p->last_thread_stop_id = p->threads[0].id;
    message(error, capacity, ""); return p;
invalid:
    message(error, capacity, "NSO process layout is invalid or exceeds the bounded diagnostic limits.");
    satchel_switch_resources_destroy(p->resources); free(p); return NULL;
}
_Static_assert(sizeof(SatchelSwitchContextIdentity) == 24, "context identity ABI");
static const ProcessThread *current_context(const SatchelSwitchProcess *p, const SatchelCPUProbe *cpu) {
    if (!p || !cpu || p->cpu != cpu || p->active_thread >= p->thread_count ||
            p->active_thread >= SATCHEL_SWITCH_THREAD_LIMIT) return NULL;
    const ProcessThread *thread = p->threads + p->active_thread;
    if (thread->cpu != cpu || thread->state != SATCHEL_SWITCH_THREAD_RUNNABLE ||
            !thread->run_reference || !thread->id || !p->process_id) return NULL;
    return thread;
}
int satchel_switch_process_context_identity(const SatchelSwitchProcess *p, SatchelCPUProbe *cpu, SatchelSwitchContextIdentity *out) {
    if (!out || (uintptr_t)out % _Alignof(SatchelSwitchContextIdentity)) return 0;
    const ProcessThread *thread = current_context(p, cpu);
    if (!thread || !satchel_cpu_probe_read(cpu, 0, NULL, 0)) return 0;
    const SatchelSwitchContextIdentity identity = {SATCHEL_SWITCH_CONTEXT_IDENTITY_VERSION,
        sizeof(SatchelSwitchContextIdentity), p->process_id, thread->id};
    *out = identity; return 1;
}
int satchel_switch_process_validate_context(const SatchelSwitchProcess *p, SatchelCPUProbe *cpu, const SatchelSwitchContextIdentity *identity) {
    if (!identity || (uintptr_t)identity % _Alignof(SatchelSwitchContextIdentity) ||
            identity->version != SATCHEL_SWITCH_CONTEXT_IDENTITY_VERSION || identity->size != sizeof(*identity)) return 0;
    const ProcessThread *thread = current_context(p, cpu);
    // Reject stale numeric identity BEFORE touching even an address-reused CPU.
    return thread && identity->process_id == p->process_id && identity->context_generation == thread->id &&
        satchel_cpu_probe_read(cpu, 0, NULL, 0);
}
int satchel_switch_process_identity_reservation(const SatchelSwitchProcess *p, SatchelSwitchIdentityReservation *out) {
    if (!p || !out || (uintptr_t)out % _Alignof(SatchelSwitchIdentityReservation) || !p->identity.generation) return 0;
    *out = p->identity; return 1;
}
int satchel_switch_process_validate_identity(SatchelSwitchProcess *p, SatchelCPUProbe *cpu, const SatchelSwitchIdentityReservation *descriptor) {
    if (!p || !cpu || !descriptor || (uintptr_t)descriptor % _Alignof(SatchelSwitchIdentityReservation) ||
            !p->identity.generation || memcmp(descriptor, &p->identity, sizeof(*descriptor))) return 0;
    int owned = 0;
    for (size_t i = 0; i < SATCHEL_SWITCH_THREAD_LIMIT; i++) if (p->threads[i].cpu == cpu) { owned = 1; break; }
    if (!owned) return 0; // Never dereference an unowned or expired CPU pointer.
    int mapped = 0;
    for (size_t i = 0; i < p->map_count; i++) {
        const SatchelSwitchMemoryInfo *entry = p->maps + i;
        if (entry->address == p->identity.address && entry->size == p->identity.length &&
                entry->state == 0x10 && !entry->permissions && !entry->attributes &&
                !entry->ipc_refcount && !entry->device_refcount) { mapped = 1; break; }
    }
    // Exact no-op returns after CPU whole-span/provenance/alias checks, before
    // allocation, coalescing or reservation invalidation. This is stronger than
    // failed reads: a host-created locked source alias would otherwise look NONE.
    return mapped && satchel_cpu_probe_set_permissions(cpu, p->identity.address, p->identity.length, 0, 0) == SATCHEL_CPU_MAP_OK;
}
int satchel_switch_process_claim_identity(SatchelSwitchProcess *p, SatchelCPUProbe *cpu, const SatchelSwitchIdentityReservation *descriptor) {
    if (!p || p->identity_claimed || !satchel_switch_process_validate_identity(p, cpu, descriptor)) return 0;
    p->identity_claimed = 1; return 1;
}
uint64_t satchel_switch_process_session_closes(const SatchelSwitchProcess *p) { return p ? p->session_closes : 0; }
uint32_t satchel_switch_process_closed_sessions(const SatchelSwitchProcess *p) {
    if (!p) return 0;
    uint32_t bits = p->closed_session_mask, count = 0;
    while (bits) { count += bits & 1; bits >>= 1; }
    return count;
}
int satchel_switch_process_is_session_closed(const SatchelSwitchProcess *p, uint32_t handle) {
    const ProcessHandle *entry = lookup_handle(p, handle);
    return entry && (p->closed_session_mask & (UINT32_C(1) << (size_t)(entry - p->handles))) != 0;
}
uint64_t satchel_switch_process_attribute_requests(const SatchelSwitchProcess *p) { return p ? p->attribute_requests : 0; }
uint64_t satchel_switch_process_attribute_serviced(const SatchelSwitchProcess *p) { return p ? p->attribute_serviced : 0; }
uint64_t satchel_switch_process_permission_locked_bytes(const SatchelSwitchProcess *p) { return p ? p->permission_locked_bytes : 0; }
uint64_t satchel_switch_process_permission_requests(const SatchelSwitchProcess *p) { return p ? p->permission_requests : 0; }
uint64_t satchel_switch_process_permission_serviced(const SatchelSwitchProcess *p) { return p ? p->permission_serviced : 0; }
uint64_t satchel_switch_process_permission_changed_bytes(const SatchelSwitchProcess *p) { return p ? p->permission_changed_bytes : 0; }
uint64_t satchel_switch_process_alias_memory_size(const SatchelSwitchProcess *p) { return p ? p->alias_size : 0; }
uint64_t satchel_switch_process_alias_map_requests(const SatchelSwitchProcess *p) { return p ? p->alias_map_requests : 0; }
uint64_t satchel_switch_process_alias_unmap_requests(const SatchelSwitchProcess *p) { return p ? p->alias_unmap_requests : 0; }
uint64_t satchel_switch_process_alias_map_serviced(const SatchelSwitchProcess *p) { return p ? p->alias_map_serviced : 0; }
uint64_t satchel_switch_process_alias_unmap_serviced(const SatchelSwitchProcess *p) { return p ? p->alias_unmap_serviced : 0; }
uint64_t satchel_switch_process_system_resource_total(const SatchelSwitchProcess *p) {
    return p ? satchel_switch_resources_capacity(p->resources) : 0;
}
uint64_t satchel_switch_process_system_resource_used(const SatchelSwitchProcess *p) {
    return p ? satchel_switch_resources_used(p->resources) : 0;
}
int satchel_switch_process_parental_info(const SatchelSwitchProcess *p, SatchelSwitchParentalInfo *out) {
    if (!p || !out) return 0;
    SatchelSwitchParentalInfo info = {.policy=SATCHEL_SWITCH_PARENTAL_POLICY_UNCONFIGURED,
        .created=p->parental_created,.initialize_requests=p->parental_initialize_requests,.initialize_serviced=p->parental_initialize_serviced};
    for (size_t i=0;i<SATCHEL_SWITCH_PARENTAL_CHILD_LIMIT;i++) {
        const ProcessParentalChild *child=p->parental_children+i;
        if (!child->references) continue;
        info.live_children++; info.initialized_children+=child->initialized; info.metadata_bound_children+=child->metadata_bound;
    }
    *out=info; return 1;
}
#if defined(SATCHEL_CPU_TESTING)
int satchel_switch_process_test_parental_matches(const SatchelSwitchProcess *p,uint32_t handle,uint32_t object_id,
                                            const SatchelSwitchApplicationControl *metadata,uint32_t initialized,uint32_t capabilities) {
    const ProcessHandle *entry=lookup_handle(p,handle);if(!entry)return 0;
    uint32_t slot=entry->service_slot;
    if(entry->domain_slot) {slot=0;const ProcessDomain *domain=p->domains+entry->domain_slot-1;
        for(size_t i=0;i<SATCHEL_SWITCH_DOMAIN_OBJECT_LIMIT;i++)if(domain->objects[i].id==object_id)slot=domain->objects[i].service_slot;
    }
    if(!slot || slot>SATCHEL_SWITCH_PARENTAL_CHILD_LIMIT)return 0;
    const ProcessParentalChild *child=p->parental_children+slot-1;
    return child->references && child->initialized==initialized && child->capabilities==capabilities &&
        (metadata ? child->metadata_bound && !memcmp(&child->metadata,metadata,sizeof(*metadata)) : !child->metadata_bound);
}
#endif
_Static_assert(sizeof(SatchelSwitchApplicationControl) == 56, "Bounded application-control numeric record");
int satchel_switch_process_attach_time_seed(SatchelSwitchProcess *p,const SatchelSwitchTimeSeed *seed) {
    if(!p||!seed||p->has_run||p->time_info.configured||!p->cpu||
       seed->version!=SATCHEL_SWITCH_TIME_SEED_VERSION||seed->size!=sizeof(*seed)||seed->unix_nanoseconds>=1000000000)return 0;
    size_t length=0;while(length<sizeof(seed->location_name)&&seed->location_name[length]) {
        unsigned char ch=(unsigned char)seed->location_name[length];if(ch<0x21||ch>0x7e)return 0;length++;
    }
    if(!length||length==sizeof(seed->location_name))return 0;
    for(size_t i=length;i<sizeof(seed->location_name);i++)if(seed->location_name[i])return 0;
    uint64_t now;int64_t ns,base,offset,steady,wall;
    if(!satchel_switch_process_get_system_tick(p,&now)||seed->system_tick>now||now-seed->system_tick>SATCHEL_CPU_SYSTEM_TICK_FREQUENCY||
       !time_tick_ns(seed->system_tick,&ns))return 0;
    base=(int64_t)seed->unix_nanoseconds-ns%INT64_C(1000000000);
    if(__builtin_sub_overflow(seed->unix_seconds,ns/INT64_C(1000000000),&offset)||
       !time_values(base,offset,now,&steady,&wall))return 0;
    if(p->user_memory_used>p->user_memory_limit||4096>p->user_memory_limit-p->user_memory_used)return 0;
    uint8_t page[4096]={0},source[16];arc4random_buf(source,sizeof(source));
    source[6]=(uint8_t)((source[6]&0x0f)|0x40);source[8]=(uint8_t)((source[8]&0x3f)|0x80);
    uint8_t steady_data[24],context[32],continuous[56]={0};
    time_word64(steady_data,(uint64_t)base);memcpy(steady_data+8,source,16);
    time_word64(context,(uint64_t)offset);time_word64(context+8,(uint64_t)(ns/INT64_C(1000000000)));memcpy(context+16,source,16);
    // Seqlocks stay0 and both immutable slots agree. The guest adds its shared
    // CPU counter. Network slots remain uninitialized (zero unmatched source),
    // automatic-correction flag at0xc8 is false, all reserved bytes are zero.
    memcpy(page+8,steady_data,24);memcpy(page+32,steady_data,24);
    memcpy(page+0x40,context,32);memcpy(page+0x60,context,32);
    // Newer primary time implementation d45561ac: rtc_offset,diff_scale,shift,
    // lower,upper,UUID. No-adjustment scale0 and equal lower/upper=boot phase
    // matches the ordinary steady record; it is NOT the old mislabeled scale1.
    time_word64(continuous,(uint64_t)ns);time_word64(continuous+24,(uint64_t)base);
    time_word64(continuous+32,(uint64_t)base);memcpy(continuous+40,source,16);
    memcpy(page+0xd8,continuous,56);memcpy(page+0x110,continuous,56);
    uint64_t token;
    if(satchel_cpu_probe_shared_readonly_create(p->cpu,page,sizeof(page),&token)!=SATCHEL_CPU_MAP_OK)return 0;
    p->time_seed=*seed;p->time_shared_token=token;p->time_base_ns=base;p->time_offset=offset;
    p->time_seed_steady=ns/INT64_C(1000000000);memcpy(p->time_source_id,source,16);
    p->user_memory_used+=4096;
    p->time_info=(SatchelSwitchTimeInfo){.configured=1,.timezone_profile=SATCHEL_SWITCH_TIME_ZONE_HOST_NAME_ONLY,
        .seed_attachments=1,.shared_backing_bytes=4096};
    return 1;
}
int satchel_switch_process_time_info(const SatchelSwitchProcess *p,SatchelSwitchTimeInfo *out) {
    if(!p||!out)return 0;
    SatchelSwitchTimeInfo info=p->time_info;
    for(size_t i=0;i<SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT;i++)info.shared_handles+=p->handles[i].type==HANDLE_TIME_SHARED_MEMORY;
    *out=info;return 1;
}
int satchel_switch_process_attach_application_control(SatchelSwitchProcess *p, const SatchelSwitchApplicationControl *metadata) {
    if (!p || !metadata || p->has_run || p->control_attachments ||
        !metadata->application_id || metadata->application_id != p->program_id) return 0;
    p->application_control = *metadata;
    p->control_attachments = 1;
    return 1;
}
int satchel_switch_process_application_control_info(const SatchelSwitchProcess *p, SatchelSwitchApplicationControlInfo *out) {
    if (!p || !out) return 0;
    *out = (SatchelSwitchApplicationControlInfo){p->control_attachments != 0,
        SATCHEL_SWITCH_PARENTAL_POLICY_UNCONFIGURED, p->control_attachments};
    return 1;
}
#if defined(SATCHEL_CPU_TESTING)
/* Host-only ownership assertion; no metadata getter exists in the iOS API. */
int satchel_switch_process_test_application_control_matches(const SatchelSwitchProcess *p, const SatchelSwitchApplicationControl *expected) {
    return p && expected && p->control_attachments && !memcmp(&p->application_control, expected, sizeof(*expected));
}
#endif
int satchel_switch_process_attach_storage(SatchelSwitchProcess *p, const SatchelSwitchStorage *storage) {
    if (!p || !storage || !storage->read || storage->size > INT64_MAX || p->has_storage || p->has_run) return 0;
    p->storage = *storage; p->has_storage = 1; return 1;
}
uint64_t satchel_switch_process_storage_reads(const SatchelSwitchProcess *p) { return p ? p->storage_reads : 0; }
uint64_t satchel_switch_process_storage_bytes(const SatchelSwitchProcess *p) { return p ? p->storage_bytes : 0; }
void satchel_switch_process_destroy(SatchelSwitchProcess *p) {
    if (p) {
        for (unsigned i = 0; i < p->thread_count; i++) satchel_cpu_probe_destroy(p->threads[i].cpu);
        satchel_switch_resources_destroy(p->resources); satchel_switch_events_destroy(p->events); free(p);
    }
}
SatchelCPUProbe *satchel_switch_process_cpu(SatchelSwitchProcess *p) { return p ? p->cpu : NULL; }
uint64_t satchel_switch_process_queries(const SatchelSwitchProcess *p) { return p ? p->queries : 0; }
uint64_t satchel_switch_process_info_requests(const SatchelSwitchProcess *p) { return p ? p->info_requests : 0; }
uint64_t satchel_switch_process_info_serviced(const SatchelSwitchProcess *p) { return p ? p->info_serviced : 0; }
uint64_t satchel_switch_process_thread_queries(const SatchelSwitchProcess *p) { return p ? p->thread_queries : 0; }
uint64_t satchel_switch_process_physical_memory_size(const SatchelSwitchProcess *p) { return p ? p->physical_size : 0; }
uint64_t satchel_switch_process_physical_map_serviced(const SatchelSwitchProcess *p) { return p ? p->physical_map_serviced : 0; }
uint64_t satchel_switch_process_physical_unmap_serviced(const SatchelSwitchProcess *p) { return p ? p->physical_unmap_serviced : 0; }
uint64_t satchel_switch_process_physical_map_requests(const SatchelSwitchProcess *p) { return p ? p->physical_map_requests : 0; }
uint64_t satchel_switch_process_physical_unmap_requests(const SatchelSwitchProcess *p) { return p ? p->physical_unmap_requests : 0; }
uint64_t satchel_switch_process_heap_requests(const SatchelSwitchProcess *p) { return p ? p->heap_requests : 0; }
uint64_t satchel_switch_process_heap_size(const SatchelSwitchProcess *p) { return p ? p->heap_size : 0; }
int satchel_switch_process_is_sm_session(const SatchelSwitchProcess *p, uint32_t handle) {
    const ProcessHandle *entry = lookup_handle(p, handle);
    return entry && entry->type == HANDLE_SM_SESSION;
}
uint64_t satchel_switch_process_sm_sessions(const SatchelSwitchProcess *p) {
    uint64_t count = 0;
    if (p) for (size_t i = 0; i < SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT; i++)
        if (p->handles[i].type == HANDLE_SM_SESSION) count++;
    return count;
}
uint64_t satchel_switch_process_port_requests(const SatchelSwitchProcess *p) { return p ? p->port_requests : 0; }
int satchel_switch_process_is_sm_initialized(const SatchelSwitchProcess *p, uint32_t handle) {
    const ProcessHandle *entry = lookup_handle(p, handle);
    return entry && entry->type == HANDLE_SM_SESSION && entry->client_process_id != 0 &&
        !(p->closed_session_mask & (UINT32_C(1) << (size_t)(entry - p->handles)));
}
uint64_t satchel_switch_process_ipc_requests(const SatchelSwitchProcess *p) { return p ? p->ipc_requests : 0; }
uint64_t satchel_switch_process_ipc_serviced(const SatchelSwitchProcess *p) { return p ? p->ipc_serviced : 0; }
#if defined(SATCHEL_CPU_TESTING)
// Private original-fixture assertion seam, absent from iOS. Exposes no packet
// bytes or PID: checks exact retained payload, owned identity and cleared tail.
int satchel_switch_process_test_log_matches(const SatchelSwitchProcess *p, unsigned slot,
                                      const uint8_t *original, size_t size) {
    if (!p || slot >= SATCHEL_SWITCH_LOG_PACKET_SLOTS || !original || size < 24 ||
        size > SATCHEL_SWITCH_LOG_PACKET_LIMIT || p->log_packets[slot].size != size) return 0;
    const uint8_t *packet = p->log_packets[slot].bytes;
    for (unsigned i = 0; i < 8; i++) if (packet[i] != (uint8_t)(p->process_id >> (i * 8))) return 0;
    if (memcmp(packet + 8, original + 8, size - 8)) return 0;
    for (size_t i = size; i < SATCHEL_SWITCH_LOG_PACKET_LIMIT; i++) if (packet[i]) return 0;
    return 1;
}
#endif
int satchel_switch_process_is_lm_session(const SatchelSwitchProcess *p, uint32_t handle) {
    const ProcessHandle *entry = lookup_handle(p, handle); return entry && entry->type == HANDLE_LM_SESSION;
}
int satchel_switch_process_is_domain_session(const SatchelSwitchProcess *p, uint32_t handle) {
    const ProcessHandle *entry = lookup_handle(p, handle); return entry && entry->domain_slot != 0;
}
uint32_t satchel_switch_process_domain_objects(const SatchelSwitchProcess *p, uint32_t handle) {
    const ProcessHandle *entry = lookup_handle(p, handle);
    if (!entry || !entry->domain_slot) return 0;
    const ProcessDomain *domain = p->domains + entry->domain_slot - 1;
    uint32_t count = 0;
    for (size_t i = 0; i < SATCHEL_SWITCH_DOMAIN_OBJECT_LIMIT; i++) count += domain->objects[i].id != 0;
    return count;
}
int satchel_switch_process_is_logger(const SatchelSwitchProcess *p, uint32_t handle) {
    const ProcessHandle *entry = lookup_handle(p, handle); return entry && entry->type == HANDLE_LOGGER;
}
int satchel_switch_process_applet_exit_to_library_enabled(const SatchelSwitchProcess *p) {
    return p && p->exit_to_library_on_execute_next;
}
int satchel_switch_process_is_readable_event(const SatchelSwitchProcess *p, uint32_t handle) {
    const ProcessHandle *entry = lookup_handle(p, handle); return entry && entry->type == HANDLE_READABLE_EVENT;
}
uint64_t satchel_switch_process_readable_event_handles(const SatchelSwitchProcess *p) {
    if (!p) return 0;
    uint64_t count = 0;
    for (size_t i = 0; i < SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT; i++) count += p->handles[i].type == HANDLE_READABLE_EVENT;
    return count;
}
int satchel_switch_process_applet_event_signaled(const SatchelSwitchProcess *p) {
    int signaled = 0;
    if (!p || !p->applet_message_event || satchel_switch_event_query(p->events, p->applet_message_event, &signaled) != SATCHEL_SWITCH_EVENT_OK) return -1;
    return signaled;
}
int satchel_switch_process_begin_foreground_launch(SatchelSwitchProcess *p) {
    if (!p) return 0;
    if (p->applet_phase == SATCHEL_SWITCH_APPLET_FOREGROUND) return 1;
    if (p->has_run || p->applet_message_count > SATCHEL_SWITCH_APPLET_MESSAGE_LIMIT - 2) return 0;
    // One real host-requested activation of this virtual application. The
    // ordered15/1 notification pair follows original Ryujinx AppletStateMgr
    // (public mirror05b4bd8c), not a claim about every Nintendo firmware's
    // startup ordering. No upstream implementation code is copied here.
    if (p->applet_message_event && satchel_switch_event_signal(p->events,p->applet_message_event) != SATCHEL_SWITCH_EVENT_OK) return 0;
    unsigned tail = (p->applet_message_head + p->applet_message_count) % SATCHEL_SWITCH_APPLET_MESSAGE_LIMIT;
    p->applet_messages[tail] = 15;
    p->applet_messages[(tail + 1) % SATCHEL_SWITCH_APPLET_MESSAGE_LIMIT] = 1;
    p->applet_message_count += 2; p->applet_messages_enqueued += 2;
    p->applet.focus_state = 1; p->applet_phase = SATCHEL_SWITCH_APPLET_FOREGROUND;
    return 1;
}
int satchel_switch_process_applet_message_info(const SatchelSwitchProcess *p, SatchelSwitchAppletMessageInfo *out) {
    if (!p || !out) return 0;
    *out = (SatchelSwitchAppletMessageInfo){p->applet_phase,p->applet_message_count,p->applet_messages_enqueued,p->applet_messages_received};
    return 1;
}
int satchel_switch_process_applet_state(const SatchelSwitchProcess *p, SatchelSwitchAppletState *out) {
    if (!p || !out) return 0;
    *out = p->applet; return 1;
}
int satchel_switch_process_report_gpu_error(SatchelSwitchProcess *p) {
    if (!p || !refresh_waits(p)) return 0;
    if (p->gpu_error_event && satchel_switch_event_signal(p->events, p->gpu_error_event) != SATCHEL_SWITCH_EVENT_OK) return 0;
    p->gpu_error_detected = 1; return refresh_waits(p);
}
int satchel_switch_process_gpu_error_detected(const SatchelSwitchProcess *p) { return p && p->gpu_error_detected; }
int satchel_switch_process_is_gpu_error_event(const SatchelSwitchProcess *p, uint32_t handle) {
    const ProcessHandle *entry = lookup_handle(p, handle);
    return entry && entry->type == HANDLE_READABLE_EVENT && entry->event_id == p->gpu_error_event;
}
int satchel_switch_process_gpu_error_event_signaled(const SatchelSwitchProcess *p) {
    int signaled = 0;
    if (!p || !p->gpu_error_event || satchel_switch_event_query(p->events, p->gpu_error_event, &signaled) != SATCHEL_SWITCH_EVENT_OK) return -1;
    return signaled;
}
uint64_t satchel_switch_process_suspended_ticks(const SatchelSwitchProcess *p) { return p ? p->suspended_ticks : 0; }
int satchel_switch_process_is_suspended_tick_event(const SatchelSwitchProcess *p, uint32_t handle) {
    const ProcessHandle *entry = lookup_handle(p, handle);
    return entry && entry->type == HANDLE_READABLE_EVENT && entry->event_id == p->suspended_tick_event;
}
int satchel_switch_process_suspended_tick_event_signaled(const SatchelSwitchProcess *p) {
    int signaled = 0;
    if (!p || !p->suspended_tick_event || satchel_switch_event_query(p->events, p->suspended_tick_event, &signaled) != SATCHEL_SWITCH_EVENT_OK) return -1;
    return signaled;
}
uint64_t satchel_switch_process_applet_resource_id(const SatchelSwitchProcess *p) {
    return p ? p->applet_resource_id : 0;
}
uint32_t satchel_switch_process_performance_mode(const SatchelSwitchProcess *p) {
    return p ? p->performance_mode : UINT32_MAX;
}
uint64_t satchel_switch_process_log_packets(const SatchelSwitchProcess *p) { return p ? p->log_count : 0; }
uint64_t satchel_switch_process_log_retained_bytes(const SatchelSwitchProcess *p) { return p ? p->log_retained_bytes : 0; }
uint64_t satchel_switch_process_log_dropped_packets(const SatchelSwitchProcess *p) { return p ? p->log_dropped : 0; }
uint32_t satchel_switch_process_log_destination(const SatchelSwitchProcess *p) { return p ? p->log_destination : 0; }
int satchel_switch_process_last_ipc(const SatchelSwitchProcess *p, SatchelSwitchIPCInfo *info) {
    if (!p || !info || !p->has_last_ipc) return 0;
    *info = p->last_ipc; return 1;
}
int satchel_switch_process_last_info(const SatchelSwitchProcess *p, uint32_t *type, uint32_t *handle, uint64_t *sub_id) {
    if (!p || !type || !handle || !sub_id || !p->info_requests) return 0;
    *type = p->last_info_type; *handle = p->last_info_handle; *sub_id = p->last_info_sub_id;
    return 1;
}
uint64_t satchel_switch_process_module_address(const SatchelSwitchProcess *p, size_t index) {
    return p && index < p->module_count ? p->module_addresses[index] : 0;
}
uint64_t satchel_switch_process_dispatch_address(const SatchelSwitchProcess *p) { return p ? p->dispatch_region.address : 0; }
uint64_t satchel_switch_process_dispatch_size(const SatchelSwitchProcess *p) { return p ? p->dispatch_region.size : 0; }
int satchel_switch_process_query(SatchelSwitchProcess *p, uint64_t address, SatchelSwitchMemoryInfo *info) {
    if (!p || !info) return 0;
    memset(info, 0, sizeof(*info));
    if (address >= p->address_space_size) {
        info->address = p->address_space_size; info->size = 0 - p->address_space_size; info->state = 0x10; return 1;
    }
    uint64_t previous_end = 0;
    for (size_t i = 0; i < p->map_count; i++) {
        const SatchelSwitchMemoryInfo *entry = p->maps + i;
        if (address < entry->address) { info->address = previous_end; info->size = entry->address - previous_end; return 1; }
        if (address - entry->address < entry->size) {
            *info = *entry;
            // Adjacent blocks with identical state/permissions form one query
            // extent; allocation boundaries are not fabricated memory gaps.
            for (size_t j = i; j > 0; j--) {
                const SatchelSwitchMemoryInfo *before = p->maps + j - 1;
                if (before->address + before->size != info->address || !memory_properties_equal(before, info)) break;
                info->address = before->address; info->size += before->size;
            }
            for (size_t j = i + 1; j < p->map_count; j++) {
                const SatchelSwitchMemoryInfo *after = p->maps + j;
                if (info->address + info->size != after->address || !memory_properties_equal(after, info)) break;
                info->size += after->size;
            }
            return 1;
        }
        previous_end = entry->address + entry->size;
    }
    info->address = previous_end; info->size = p->address_space_size - previous_end; return 1;
}
static ProcessThread *thread_from_handle(SatchelSwitchProcess *p, uint32_t handle, int pseudo) {
    if (pseudo && handle == 0xffff8000) return p->threads + p->active_thread;
    const ProcessHandle *entry = lookup_handle(p, handle);
    if (!entry || entry->type != HANDLE_THREAD || entry->event_id >= p->thread_count) return NULL;
    ProcessThread *thread = p->threads + entry->event_id;
    return thread->cpu ? thread : NULL;
}
static ProcessThread *thread_from_id(SatchelSwitchProcess *p, uint64_t id) {
    for (unsigned i = 0; i < p->thread_count; i++) if (p->threads[i].cpu && p->threads[i].id == id) return p->threads + i;
    return NULL;
}
/* Original bounded user-mutex model from the public ABI/behavior in pinned
 * Atmosphere 6b0c077b: kern_svc_lock.cpp, KConditionVariable and KThread.
 * Calls run only between serialized guest slices; guest accesses still use
 * the CPU's checked shared mappings. No mutex payload is exposed in reports. */
static uint64_t mutex_next_order(SatchelSwitchProcess *p) {
    if (p->mutex_sequence == UINT64_MAX) {
        uint64_t order[SATCHEL_SWITCH_THREAD_LIMIT] = {0}; unsigned count = 0;
        for (unsigned i = 0; i < p->thread_count; i++) if (p->threads[i].cpu &&
            p->threads[i].state == SATCHEL_SWITCH_THREAD_MUTEX_WAIT) {
            order[i] = 1; count++;
            for (unsigned j = 0; j < p->thread_count; j++) if (p->threads[j].cpu &&
                p->threads[j].state == SATCHEL_SWITCH_THREAD_MUTEX_WAIT &&
                p->threads[j].mutex_order < p->threads[i].mutex_order) order[i]++;
        }
        for (unsigned i = 0; i < p->thread_count; i++) if (order[i]) p->threads[i].mutex_order = order[i];
        p->mutex_sequence = count;
    }
    return ++p->mutex_sequence;
}
static void mutex_priorities(SatchelSwitchProcess *p) {
    uint8_t old[SATCHEL_SWITCH_THREAD_LIMIT];
    for (unsigned i = 0; i < p->thread_count; i++) {
        old[i] = p->threads[i].effective_priority;
        p->threads[i].effective_priority = p->threads[i].priority;
    }
    // At most N-1 edges on a simple path. Cycles converge to their minimum
    // actual donor priority without fabricating a wake or running indefinitely.
    for (unsigned pass = 0; pass < SATCHEL_SWITCH_THREAD_LIMIT; pass++) {
        int changed = 0;
        for (unsigned i = 0; i < p->thread_count; i++) {
            ProcessThread *waiter = p->threads + i;
            if (!waiter->cpu || waiter->state != SATCHEL_SWITCH_THREAD_MUTEX_WAIT) continue;
            ProcessThread *owner = thread_from_id(p,waiter->mutex_owner);
            if (owner && owner->effective_priority > waiter->effective_priority) {
                owner->effective_priority = waiter->effective_priority; changed = 1;
            }
        }
        if (!changed) break;
    }
    // Equal-priority queue order is FIFO; a priority change reinserts a waiter
    // at its new priority, matching the public ordered-waiter behavior.
    for (unsigned i = 0; i < p->thread_count; i++) if (p->threads[i].cpu &&
        p->threads[i].state == SATCHEL_SWITCH_THREAD_MUTEX_WAIT && old[i] != p->threads[i].effective_priority)
        p->threads[i].mutex_order = mutex_next_order(p);
}
static void mutex_complete(SatchelSwitchProcess *p, ProcessThread *thread, uint32_t status) {
    ProcessThread *retained = thread_from_id(p,thread->mutex_owner_reference);
    if (retained && retained->wait_references) retained->wait_references--;
    thread->mutex_owner = thread->mutex_owner_reference = thread->mutex_address = thread->mutex_order = 0;
    thread->mutex_tag = 0; thread->deadline = UINT64_MAX;
    thread->state = SATCHEL_SWITCH_THREAD_RUNNABLE;
    satchel_cpu_probe_set_x(thread->cpu,0,status);
}
static void mutex_finalize_owner(SatchelSwitchProcess *p, const ProcessThread *owner) {
    // Finalization (not merely ExitThread) cancels remaining waiters. Original
    // owner references can deliberately keep a terminated object alive.
    int changed = 0;
    for (unsigned i = 0; i < p->thread_count; i++) {
        ProcessThread *waiter = p->threads + i;
        if (waiter->cpu && waiter->state == SATCHEL_SWITCH_THREAD_MUTEX_WAIT && waiter->mutex_owner == owner->id) {
            mutex_complete(p,waiter,0xfa01); changed = 1;
        }
    }
    if (changed) mutex_priorities(p);
}
static uint32_t mutex_address_status(uint64_t address) {
    // This profile has no shadow stack. Kernel-address dereferences and
    // otherwise inaccessible aligned user words fail the checked CPU access.
    if (address >= UINT64_C(0xffffff8000000000) && address < UINT64_C(0xffffffffffe00000)) return 0xd401;
    return (address & 3) ? 0xcc01 : 0;
}
static int mutex_lock(SatchelSwitchProcess *p, uint32_t handle, uint64_t address, uint32_t tag, uint32_t *status) {
    p->mutex_lock_requests++;
    *status = mutex_address_status(address); if (*status) return 1;
    uint32_t word;
    if (!satchel_cpu_probe_read(p->cpu,address,&word,sizeof(word))) { *status = 0xd401; return 1; }
    if (word != (handle | UINT32_C(0x40000000))) return 1;
    ProcessThread *owner = thread_from_handle(p,handle,0);
    if (!owner) { *status = 0xe401; return 1; }
    ProcessThread *thread = p->threads + p->active_thread;
    owner->wait_references++;
    thread->mutex_owner = thread->mutex_owner_reference = owner->id;
    thread->mutex_address = address; thread->mutex_tag = tag;
    thread->mutex_order = mutex_next_order(p);
    thread->deadline = UINT64_MAX; thread->state = SATCHEL_SWITCH_THREAD_MUTEX_WAIT;
    mutex_priorities(p); p->scheduler_switch = 1; p->mutex_waits++;
    return 2; // No completion until a real handoff or owned-object finalization.
}
static uint32_t mutex_unlock(SatchelSwitchProcess *p, uint64_t address) {
    p->mutex_unlock_requests++;
    uint32_t status = mutex_address_status(address); if (status) return status;
    const uint64_t owner = p->threads[p->active_thread].id;
    ProcessThread *next = NULL; unsigned waiters = 0;
    for (unsigned i = 0; i < p->thread_count; i++) {
        ProcessThread *thread = p->threads + i;
        if (!thread->cpu || thread->state != SATCHEL_SWITCH_THREAD_MUTEX_WAIT ||
            thread->mutex_owner != owner || thread->mutex_address != address) continue;
        waiters++;
        if (!next || thread->effective_priority < next->effective_priority ||
            (thread->effective_priority == next->effective_priority && thread->mutex_order < next->mutex_order)) next = thread;
    }
    uint32_t value = next ? next->mutex_tag | (waiters > 1 ? UINT32_C(0x40000000) : 0) : 0;
    // The kernel transfers its waiter list before writing userspace. A failed
    // write still completes the selected waiter with InvalidCurrentMemory;
    // it must not silently roll back to a mutex that no longer has that queue.
    if (next) for (unsigned i = 0; i < p->thread_count; i++) {
        ProcessThread *thread = p->threads + i;
        if (thread->cpu && thread->state == SATCHEL_SWITCH_THREAD_MUTEX_WAIT &&
            thread->mutex_owner == owner && thread->mutex_address == address) thread->mutex_owner = next->id;
    }
    atomic_thread_fence(memory_order_seq_cst); // Public SignalToAddress orders prior stores before publishing ownership.
    if (!satchel_cpu_probe_write(p->cpu,address,&value,sizeof(value))) status = 0xd401;
    if (next) { mutex_complete(p,next,status); p->mutex_handoffs++; }
    mutex_priorities(p); p->scheduler_switch = 1;
    return status;
}

static void release_wait_objects(SatchelSwitchProcess *p, ProcessThread *thread) {
    for (size_t i = 0; i < thread->wait_count; i++) {
        ProcessWaitObject object = thread->wait[i];
        if (object.kind == HANDLE_READABLE_EVENT) satchel_switch_event_release(p->events, object.id);
        else {
            ProcessThread *target = thread_from_id(p, object.id);
            if (target && target->wait_references) target->wait_references--;
        }
    }
    thread->wait_count = 0;
}
static void reap_threads(SatchelSwitchProcess *p) {
    for (unsigned i = 0; i < p->thread_count; i++) {
        ProcessThread *thread = p->threads + i;
        if (!thread->cpu || thread->handle_references || thread->wait_references || thread->run_reference) continue;
        // An unreachable, never-started object can be destroyed; no waiter can
        // observe an invented termination signal because every ref is absent.
        if (thread->state == SATCHEL_SWITCH_THREAD_CREATED) thread->state = SATCHEL_SWITCH_THREAD_EXITED;
        if (thread->state != SATCHEL_SWITCH_THREAD_EXITED) continue;
        mutex_finalize_owner(p,thread);
        // The final existing context retains process RAM until destruction,
        // including backend teardown after EXITED. No extra context is created
        // and an exited holder is never selected for guest execution.
        unsigned retained = 0;
        for (unsigned j = 0; j < p->thread_count; j++) retained += p->threads[j].cpu != NULL;
        if (retained == 1) continue;
        SatchelCPUProbe *cpu = thread->cpu;
        if (!satchel_cpu_probe_try_destroy(cpu)) continue;
        if (p->cpu == cpu) p->cpu = NULL;
        memset(thread, 0, sizeof(*thread));
    }
}
static int thread_clock(SatchelSwitchProcess *p, uint64_t *ticks) {
    if (!p || !ticks) return 0;
    for (unsigned i = 0; i < p->thread_count; i++) if (p->threads[i].cpu &&
        satchel_cpu_probe_get_system_tick(p->threads[i].cpu, ticks)) return 1;
    return 0;
}
int satchel_switch_process_get_system_tick(SatchelSwitchProcess *p, uint64_t *ticks) { return thread_clock(p,ticks); }
static uint64_t nanoseconds_to_ticks(uint64_t ns) { return (ns / 625) * 12 + (ns % 625) * 12 / 625; }
static int wait_ready(SatchelSwitchProcess *p, const ProcessThread *thread) {
    for (size_t i = 0; i < thread->wait_count; i++) {
        ProcessWaitObject object = thread->wait[i]; int signaled = 0;
        if (object.kind == HANDLE_READABLE_EVENT) {
            if (satchel_switch_event_query(p->events, object.id, &signaled) != SATCHEL_SWITCH_EVENT_OK) return -2;
        } else {
            ProcessThread *target = thread_from_id(p, object.id);
            if (!target) return -2;
            signaled = target->state == SATCHEL_SWITCH_THREAD_EXITED;
        }
        if (signaled) return (int)i;
    }
    return -1;
}
static void complete_wait(SatchelSwitchProcess *p, ProcessThread *thread, uint32_t result, int index) {
    satchel_cpu_probe_set_x(thread->cpu, 0, result);
    satchel_cpu_probe_set_x(thread->cpu, 1, (uint32_t)index);
    release_wait_objects(p, thread);
    thread->deadline = UINT64_MAX; thread->state = SATCHEL_SWITCH_THREAD_RUNNABLE;
}
static int refresh_waits(SatchelSwitchProcess *p) {
    uint64_t now = 0;
    if (!thread_clock(p, &now)) return 0;
    for (unsigned i = 0; i < p->thread_count; i++) {
        ProcessThread *thread = p->threads + i;
        if (!thread->cpu) continue;
        if (thread->state == SATCHEL_SWITCH_THREAD_SLEEPING && thread->deadline <= now) {
            thread->state = SATCHEL_SWITCH_THREAD_RUNNABLE; thread->deadline = UINT64_MAX;
        } else if (thread->state == SATCHEL_SWITCH_THREAD_WAITING) {
            int ready = wait_ready(p, thread);
            if (ready < -1) return 0;
            if (ready >= 0) complete_wait(p, thread, 0, ready);
            else if (thread->deadline <= now) complete_wait(p, thread, 0xea01, -1);
        }
    }
    reap_threads(p); return 1;
}
// All typed wait references are acquired before readiness, zero-timeout or
// cancellation checks. Closing a guest handle cannot destroy a queued object.
static int wait_synchronization(SatchelSwitchProcess *p, uint64_t address, int32_t count, int64_t ns, uint32_t *status) {
    ProcessThread *thread = p->threads + p->active_thread;
    if (count < 0 || count > SATCHEL_SWITCH_THREAD_WAIT_LIMIT) { *status = 0xee01; return 1; }
    uint32_t handles[SATCHEL_SWITCH_THREAD_WAIT_LIMIT];
    if (count && !satchel_cpu_probe_read(p->cpu, address, handles, (size_t)count * 4)) { *status = 0xe601; return 1; }
    thread->wait_count = 0;
    for (int i = 0; i < count; i++) {
        const ProcessHandle *entry = lookup_handle(p, handles[i]);
        if (!entry || (entry->type != HANDLE_READABLE_EVENT && entry->type != HANDLE_THREAD)) {
            release_wait_objects(p, thread); *status = 0xe401; return 1;
        }
        if (entry->type == HANDLE_READABLE_EVENT) {
            if (satchel_switch_event_retain(p->events, entry->event_id) != SATCHEL_SWITCH_EVENT_OK) {
                release_wait_objects(p, thread); *status = 0xce01; return 1;
            }
            thread->wait[thread->wait_count++] = (ProcessWaitObject){HANDLE_READABLE_EVENT, entry->event_id};
        } else {
            ProcessThread *target = thread_from_handle(p, handles[i], 0);
            if (!target) { release_wait_objects(p, thread); *status = 0xe401; return 1; }
            target->wait_references++;
            thread->wait[thread->wait_count++] = (ProcessWaitObject){HANDLE_THREAD, target->id};
        }
    }
    int ready = wait_ready(p, thread);
    if (ready < -1) { release_wait_objects(p, thread); return 0; }
    if (ready >= 0) {
        release_wait_objects(p, thread); satchel_cpu_probe_set_x(p->cpu, 1, (uint32_t)ready); *status = 0; return 1;
    }
    if (!ns) { release_wait_objects(p, thread); *status = 0xea01; return 1; }
    if (thread->wait_cancelled) {
        thread->wait_cancelled = 0; release_wait_objects(p, thread); *status = 0xec01; return 1;
    }
    uint64_t now;
    if (!thread_clock(p, &now)) { release_wait_objects(p, thread); return 0; }
    thread->deadline = ns < 0 ? UINT64_MAX : now + nanoseconds_to_ticks((uint64_t)ns) + 2;
    thread->state = SATCHEL_SWITCH_THREAD_WAITING; p->scheduler_switch = 1;
    return 2; // No completion registers until a real ready/timeout/cancel result.
}
static int create_thread(SatchelSwitchProcess *p, uint64_t entry, uint64_t argument, uint64_t stack,
                         int32_t priority, int32_t core, uint32_t *status, uint32_t *handle) {
    if (!p->has_core_priority) return -1; // No invented allowed masks.
    if (core == -2) core = p->initial_cpu_id;
    if (core < 0 || core > 3 || !(p->allowed_core_mask & (UINT64_C(1) << core))) { *status = 0xe201; return 1; }
    if (priority < 0 || priority > 63 || !(p->allowed_priority_mask & (UINT64_C(1) << priority))) { *status = 0xe001; return 1; }
    // Admitted virtual cores retain their own priority queues; execution is
    // serialized with bounded quanta across cores, never host parallelism.
    entry &= ~UINT64_C(1);
    if (!satchel_cpu_probe_check_range(p->cpu, entry, 4, SATCHEL_CPU_EXECUTE) || (entry & 3)) return -1;
    uint32_t fpcr, fpsr;
    if (satchel_cpu_probe_get_fp_status(p->cpu, &fpcr, &fpsr) != SATCHEL_CPU_FP_OK) return -1;
    reap_threads(p);
    unsigned slot = 0;
    while (slot < SATCHEL_SWITCH_THREAD_LIMIT && p->threads[slot].cpu) slot++;
    if (slot == SATCHEL_SWITCH_THREAD_LIMIT) { *status = 0x10801; return 1; }
    size_t handle_slot = 0;
    while (handle_slot < SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT && p->handles[handle_slot].type != HANDLE_EMPTY) handle_slot++;
    if (handle_slot == SATCHEL_SWITCH_PROCESS_HANDLE_LIMIT || p->next_handle >= 0xffff8000) { *status = 0xd201; return 1; }
    unsigned page = slot / 8;
    uint64_t page_address = tls_base + page * 4096, tls = page_address + (slot % 8) * 0x200;
    int new_page = !(p->tls_pages & (1u << page));
    if (new_page && (p->map_count == MAX_MAPS || p->user_memory_limit - p->user_memory_used < 4096)) {
        *status = 0xce01; return 1;
    }
    if (new_page) {
        SatchelSwitchMemoryInfo info;
        if (!satchel_switch_process_query(p, page_address, &info) || info.state || info.size - (page_address - info.address) < 4096) return -1;
    }
    uint64_t id = allocate_thread_id();
    if (!id) { *status = 0xce01; return 1; }
    char error[160]; SatchelCPUProbe *cpu = satchel_cpu_probe_create_shared(p->cpu, entry, error, sizeof(error));
    if (!cpu) { *status = 0xce01; return 1; }
    uint8_t zero_q[16] = {0}, tls_data[0x200] = {0};
    uint32_t token = p->next_handle; memcpy(tls_data + 0x110, &token, 4);
    int initialized = satchel_cpu_probe_copy_fp_status(cpu, p->cpu) == SATCHEL_CPU_FP_OK;
    for (unsigned i = 0; i < 32 && initialized; i++) initialized = satchel_cpu_probe_set_q(cpu, i, zero_q);
    uint64_t cookie; arc4random_buf(&cookie, sizeof(cookie));
    initialized = initialized && satchel_cpu_probe_set_x(cpu, 0, argument) && satchel_cpu_probe_set_x(cpu, 18, cookie | 1) &&
        satchel_cpu_probe_set_sp(cpu, stack & ~UINT64_C(15)) && satchel_cpu_probe_set_tls(cpu, tls, 0);
    if (!initialized) { satchel_cpu_probe_destroy(cpu); return -1; }
    if (new_page) {
        SatchelCPUMapResult mapped = satchel_cpu_probe_resize_rw(p->cpu, page_address, 4096);
        if (mapped != SATCHEL_CPU_MAP_OK) {
            satchel_cpu_probe_destroy(cpu);
            if (mapped == SATCHEL_CPU_MAP_ALLOCATION || mapped == SATCHEL_CPU_MAP_LIMIT) { *status = mapped == SATCHEL_CPU_MAP_LIMIT ? 0xce01 : 0xd001; return 1; }
            return 0;
        }
    }
    if (!satchel_cpu_probe_write(cpu, tls, tls_data, sizeof(tls_data))) {
        if (new_page) satchel_cpu_probe_resize_rw(p->cpu, page_address, 0);
        satchel_cpu_probe_destroy(cpu); return 0;
    }
    if (new_page) {
        size_t at = 0; while (at < p->map_count && p->maps[at].address < page_address) at++;
        memmove(p->maps + at + 1, p->maps + at, (p->map_count - at) * sizeof(*p->maps));
        p->maps[at] = (SatchelSwitchMemoryInfo){page_address,4096,0xc,0,3,0,0,0}; p->map_count++;
        p->tls_pages |= 1u << page; p->user_memory_used += 4096;
    }
    ProcessThread thread = {.cpu=cpu,.handle=token,.priority=(uint8_t)priority,.effective_priority=(uint8_t)priority,.cpu_id=(uint8_t)core,.id=id,
        .ideal_core_id=core,.affinity_mask=UINT64_C(1)<<core,.tls_address=tls,.state=SATCHEL_SWITCH_THREAD_CREATED,.handle_references=1,.deadline=UINT64_MAX};
    p->threads[slot] = thread;
    if (slot >= p->thread_count) p->thread_count = slot + 1;
    p->handles[handle_slot] = (ProcessHandle){token,HANDLE_THREAD,0,0,slot, 0}; p->next_handle++;
    *status = 0; *handle = token; return 1;
}

static SatchelCPUProbeResult run_thread_slice(SatchelSwitchProcess *p, uint64_t budget,
        SatchelCPUProbeCancelled cancelled, void *context) {
    if (!p) return (SatchelCPUProbeResult){.reason = SATCHEL_CPU_INTERNAL_FAULT};
    p->has_run = 1;
    uint64_t steps = 0;
    SatchelCPUProbeResult result;
    int serviced = 0;
    do {
        serviced = 0;
        result = satchel_cpu_probe_run(p->cpu, budget - steps, cancelled, context); steps += result.steps;
        if (result.reason != SATCHEL_CPU_SVC) break;
        // Settle elapsed deadlines before a syscall can signal/terminate or
        // cancel an object. A later event cannot replace an already due timeout.
        if (!refresh_waits(p)) { result.reason = SATCHEL_CPU_INTERNAL_FAULT; break; }
        if (result.svc == 8) {
            uint64_t entry, argument, stack, priority, core; uint32_t status = 0, handle = 0;
            satchel_cpu_probe_get_x(p->cpu,1,&entry); satchel_cpu_probe_get_x(p->cpu,2,&argument);
            satchel_cpu_probe_get_x(p->cpu,3,&stack); satchel_cpu_probe_get_x(p->cpu,4,&priority); satchel_cpu_probe_get_x(p->cpu,5,&core);
            int handled = create_thread(p,entry,argument,stack,(int32_t)priority,(int32_t)core,&status,&handle);
            if (handled < 0) break;
            if (!handled) { result.reason = SATCHEL_CPU_INTERNAL_FAULT; break; }
            satchel_cpu_probe_set_x(p->cpu,0,status); satchel_cpu_probe_set_x(p->cpu,1,status?0:handle);
        } else if (result.svc == 0xf) {
            uint64_t handle, raw_core, mask;
            satchel_cpu_probe_get_x(p->cpu,0,&handle); satchel_cpu_probe_get_x(p->cpu,1,&raw_core);
            satchel_cpu_probe_get_x(p->cpu,2,&mask);
            ProcessThread *thread = thread_from_handle(p,(uint32_t)handle,1);
            int32_t ideal = (int32_t)raw_core;
            uint32_t status = thread ? 0 : 0xe401;
            // Public ABI/policy: Atmosphere 6b0c077b kern_svc_thread.cpp,
            // KThread::SetCoreMask. No pinned-thread/migration-disable state is
            // admitted by this serialized profile. Assignment and ideal differ.
            if (!status) {
                if (!p->has_core_priority) break;
                if (ideal == -2) { ideal = p->initial_cpu_id; mask = UINT64_C(1) << ideal; }
                else if (mask & ~p->allowed_core_mask) status = 0xe201;
                else if (!mask) status = 0xe801;
                else if (ideal >= 0 && ideal < 64) {
                    if (!(mask & (UINT64_C(1) << ideal))) status = 0xe801;
                } else if (ideal != -1 && ideal != -3) status = 0xe201;
                if (!status && ideal == -3) {
                    ideal = thread->ideal_core_id;
                    if (ideal < 0 || !(mask & (UINT64_C(1) << ideal))) status = 0xe801;
                }
                if (!status) {
                    thread->ideal_core_id = ideal; thread->affinity_mask = mask;
                    if (!(mask & (UINT64_C(1) << thread->cpu_id))) {
                        unsigned assigned = 3;
                        if (ideal >= 0) assigned = (unsigned)ideal;
                        else while (!(mask & (UINT64_C(1) << assigned))) assigned--;
                        thread->cpu_id = (uint8_t)assigned;
                    }
                    p->scheduler_switch = 1;
                }
            }
            satchel_cpu_probe_set_x(p->cpu,0,status);
        } else if (result.svc == 9) {
            uint64_t handle; satchel_cpu_probe_get_x(p->cpu,0,&handle);
            ProcessThread *thread = thread_from_handle(p,(uint32_t)handle,1);
            uint32_t status = !thread ? 0xe401 : thread->state != SATCHEL_SWITCH_THREAD_CREATED ? 0xfa01 : 0;
            if (!status) { thread->state = SATCHEL_SWITCH_THREAD_RUNNABLE; thread->run_reference = 1; p->scheduler_switch = 1; }
            satchel_cpu_probe_set_x(p->cpu,0,status);
        } else if (result.svc == 0xa) {
            ProcessThread *thread = p->threads + p->active_thread;
            thread->state = SATCHEL_SWITCH_THREAD_EXITED; thread->run_reference = 0;
            p->scheduler_switch = 1;
        } else if (result.svc == 0x1a) {
            uint64_t owner,address,tag; uint32_t status;
            satchel_cpu_probe_get_x(p->cpu,0,&owner); satchel_cpu_probe_get_x(p->cpu,1,&address); satchel_cpu_probe_get_x(p->cpu,2,&tag);
            if (mutex_lock(p,(uint32_t)owner,address,(uint32_t)tag,&status) == 1) satchel_cpu_probe_set_x(p->cpu,0,status);
        } else if (result.svc == 0x1b) {
            uint64_t address; satchel_cpu_probe_get_x(p->cpu,0,&address);
            satchel_cpu_probe_set_x(p->cpu,0,mutex_unlock(p,address));
        } else if (result.svc == 0x18) {
            uint64_t address,count,timeout; uint32_t status = 0;
            satchel_cpu_probe_get_x(p->cpu,1,&address); satchel_cpu_probe_get_x(p->cpu,2,&count); satchel_cpu_probe_get_x(p->cpu,3,&timeout);
            int handled = wait_synchronization(p,address,(int32_t)count,(int64_t)timeout,&status);
            if (!handled) { result.reason = SATCHEL_CPU_INTERNAL_FAULT; break; }
            if (handled == 1) satchel_cpu_probe_set_x(p->cpu,0,status);
        } else if (result.svc == 0x19) {
            uint64_t handle; satchel_cpu_probe_get_x(p->cpu,0,&handle);
            ProcessThread *thread = thread_from_handle(p,(uint32_t)handle,1);
            if (thread) {
                if (thread->state == SATCHEL_SWITCH_THREAD_WAITING) { thread->wait_cancelled = 0; complete_wait(p,thread,0xec01,-1); }
                else thread->wait_cancelled = 1;
            }
            satchel_cpu_probe_set_x(p->cpu,0,thread?0:0xe401);
        } else if (result.svc == 0x17) {
            uint64_t handle; satchel_cpu_probe_get_x(p->cpu,0,&handle);
            if ((uint32_t)handle == 0xffff8001) break; // Current-process Reset is not this event-only slice.
            const ProcessHandle *object = lookup_handle(p,(uint32_t)handle);
            uint32_t status = 0xe401;
            if (object && object->type == HANDLE_READABLE_EVENT) {
                SatchelSwitchEventResult reset = satchel_switch_event_reset(p->events,object->event_id);
                status = reset == SATCHEL_SWITCH_EVENT_OK ? 0 : reset == SATCHEL_SWITCH_EVENT_INVALID_STATE ? 0xfa01 : 0xe401;
            }
            satchel_cpu_probe_set_x(p->cpu,0,status);
        } else if (result.svc == 0xb) {
            uint64_t raw; satchel_cpu_probe_get_x(p->cpu,0,&raw); int64_t ns = (int64_t)raw;
            ProcessThread *thread = p->threads + p->active_thread;
            if (ns > 0) {
                uint64_t now; if (!thread_clock(p,&now)) { result.reason = SATCHEL_CPU_INTERNAL_FAULT; break; }
                uint64_t offset = nanoseconds_to_ticks((uint64_t)ns);
                thread->deadline = !offset || now > INT64_MAX - offset - 2 ? INT64_MAX : now + offset + 2;
                thread->state = SATCHEL_SWITCH_THREAD_SLEEPING; p->scheduler_switch = 1;
            } else if (!ns) p->scheduler_switch = 1;
            else if (ns == -1 || ns == -2) break; // Migration/yield-any policies not modeled.
            // Other negative values are an explicit no-op in the pinned kernel.
        } else if (result.svc == 0x1e) {
            uint64_t tick; if (!satchel_cpu_probe_get_system_tick(p->cpu,&tick)) { result.reason = SATCHEL_CPU_INTERNAL_FAULT; break; }
            satchel_cpu_probe_set_x(p->cpu,0,tick);
        } else if (result.svc == 0x21) {
            uint64_t handle = 0;
            satchel_cpu_probe_get_x(p->cpu, 0, &handle);
            if (!send_sync_request(p, (uint32_t)handle)) break;
        } else if (result.svc == 0x1F) {
            uint64_t address = 0; uint32_t handle = 0;
            satchel_cpu_probe_get_x(p->cpu, 1, &address);
            uint32_t status = connect_named_port(p, address, &handle);
            satchel_cpu_probe_set_x(p->cpu, 0, status);
            satchel_cpu_probe_set_x(p->cpu, 1, status ? 0 : handle);
        } else if (result.svc==0x13||result.svc==0x14) {
            uint64_t handle,address,size,permission=0;uint32_t status;
            satchel_cpu_probe_get_x(p->cpu,0,&handle);satchel_cpu_probe_get_x(p->cpu,1,&address);satchel_cpu_probe_get_x(p->cpu,2,&size);
            if(result.svc==0x13)satchel_cpu_probe_get_x(p->cpu,3,&permission);
            if(!shared_memory(p,(uint32_t)handle,address,size,(uint32_t)permission,result.svc==0x14,&status)) {
                result.reason=SATCHEL_CPU_INTERNAL_FAULT;
                snprintf(result.detail,sizeof(result.detail),"Shared backing disagrees with the owned process map.");break;
            }
            satchel_cpu_probe_set_x(p->cpu,0,status);
        } else if (result.svc == 0x16) {
            uint64_t token = 0;
            satchel_cpu_probe_get_x(p->cpu, 0, &token);
            satchel_cpu_probe_set_x(p->cpu, 0, close_handle(p, (uint32_t)token));
        } else if (result.svc == 4 || result.svc == 5) {
            uint64_t destination = 0, source = 0, size = 0; uint32_t status = 0;
            satchel_cpu_probe_get_x(p->cpu, 0, &destination); satchel_cpu_probe_get_x(p->cpu, 1, &source);
            satchel_cpu_probe_get_x(p->cpu, 2, &size);
            if (!alias_memory(p, destination, source, size, result.svc == 5, &status)) {
                result.reason = SATCHEL_CPU_INTERNAL_FAULT;
                snprintf(result.detail, sizeof(result.detail), "Alias backing disagrees with the owned process map."); break;
            }
            satchel_cpu_probe_set_x(p->cpu, 0, status);
        } else if (result.svc == 0x2C || result.svc == 0x2D) {
            uint64_t address = 0, size = 0; uint32_t status = 0;
            satchel_cpu_probe_get_x(p->cpu, 0, &address); satchel_cpu_probe_get_x(p->cpu, 1, &size);
            int handled = physical_memory(p, address, size, result.svc == 0x2D, &status);
            if (handled < 0) break;
            if (!handled) {
                result.reason = SATCHEL_CPU_INTERNAL_FAULT;
                snprintf(result.detail, sizeof(result.detail), "Physical backing disagrees with the owned process map.");
                break;
            }
            satchel_cpu_probe_set_x(p->cpu, 0, status);
            if (!status) {
                if (result.svc == 0x2D) p->physical_unmap_serviced++; else p->physical_map_serviced++;
            }
        } else if (result.svc == 1) {
            uint64_t size = 0;
            satchel_cpu_probe_get_x(p->cpu, 1, &size);
            uint32_t status = 0;
            int handled = set_heap_size(p, size, &status);
            if (handled < 0) break;
            if (!handled) {
                result.reason = SATCHEL_CPU_INTERNAL_FAULT;
                snprintf(result.detail, sizeof(result.detail), "The heap backing disagrees with the owned process map.");
                break;
            }
            satchel_cpu_probe_set_x(p->cpu, 0, status);
            satchel_cpu_probe_set_x(p->cpu, 1, status ? 0 : p->heap_region.address);
        } else if (result.svc == 2) {
            uint64_t address = 0, size = 0, permission = 0; uint32_t status = 0;
            satchel_cpu_probe_get_x(p->cpu, 0, &address); satchel_cpu_probe_get_x(p->cpu, 1, &size);
            satchel_cpu_probe_get_x(p->cpu, 2, &permission);
            if (!set_memory_permission(p, address, size, (uint32_t)permission, &status)) {
                result.reason = SATCHEL_CPU_INTERNAL_FAULT;
                snprintf(result.detail, sizeof(result.detail), "Permission backing disagrees with the owned process map."); break;
            }
            satchel_cpu_probe_set_x(p->cpu, 0, status);
        } else if (result.svc == 3) {
            uint64_t address = 0, size = 0, mask = 0, attributes = 0; uint32_t status = 0;
            satchel_cpu_probe_get_x(p->cpu, 0, &address); satchel_cpu_probe_get_x(p->cpu, 1, &size);
            satchel_cpu_probe_get_x(p->cpu, 2, &mask); satchel_cpu_probe_get_x(p->cpu, 3, &attributes);
            int handled = set_memory_attribute(p, address, size, (uint32_t)mask, (uint32_t)attributes, &status);
            if (handled < 0) break;
            if (!handled) {
                result.reason = SATCHEL_CPU_INTERNAL_FAULT;
                snprintf(result.detail, sizeof(result.detail), "Attribute range disagrees with the owned process map."); break;
            }
            satchel_cpu_probe_set_x(p->cpu, 0, status);
        } else if (result.svc == 6) {
            uint64_t output = 0, address = 0;
            satchel_cpu_probe_get_x(p->cpu, 0, &output); satchel_cpu_probe_get_x(p->cpu, 2, &address);
            SatchelSwitchMemoryInfo info; satchel_switch_process_query(p, address, &info);
            // QueryMemory's x1 is an output register (PageInfo), not an input pointer.
            // A bad output pointer causes no partial write and returns InvalidPointer.
            int written = satchel_cpu_probe_write(p->cpu, output, &info, sizeof(info));
            satchel_cpu_probe_set_x(p->cpu, 0, written ? 0 : 0xE601); satchel_cpu_probe_set_x(p->cpu, 1, 0);
            p->queries++;
        } else if (result.svc == 0xC || result.svc == 0xE || result.svc == 0x25) {
            // libnx svc.s: priority/ID take W1 handle, core-mask takes W2.
            // All outputs are registers; saved output pointers belong to the
            // guest wrapper. Primary behavior: kern_svc_thread.cpp and KThread
            // initial affinity in kern_k_thread.cpp (Atmosphere pin above).
            uint64_t raw_handle = 0;
            satchel_cpu_probe_get_x(p->cpu, result.svc == 0xE ? 2 : 1, &raw_handle);
            uint32_t handle = (uint32_t)raw_handle;
            ProcessThread *thread = thread_from_handle(p,handle,1);
            uint64_t value = thread ? (result.svc == 0xC ? thread->effective_priority : result.svc == 0xE ? (uint32_t)thread->ideal_core_id : thread->id) : 0;
            satchel_cpu_probe_set_x(p->cpu, 0, thread ? 0 : 0xE401);
            satchel_cpu_probe_set_x(p->cpu, 1, value);
            if (result.svc == 0xE) satchel_cpu_probe_set_x(p->cpu, 2, thread ? thread->affinity_mask : 0);
            p->thread_queries++;
        } else if (result.svc == 0x29) {
            // Public ABI: libnx svc.s svcGetInfo (b156b4774fb2df7b93402169509266da7547fc4c).
            // W1 type, W2 handle, X3 subtype -> W0 result, X1 value. The wrapper
            // owns any guest output pointer; the kernel never dereferences X0.
            uint64_t type = 0, handle = 0, subtype = 0;
            satchel_cpu_probe_get_x(p->cpu, 1, &type); satchel_cpu_probe_get_x(p->cpu, 2, &handle);
            satchel_cpu_probe_get_x(p->cpu, 3, &subtype);
            p->last_info_type = (uint32_t)type; p->last_info_handle = (uint32_t)handle;
            p->last_info_sub_id = subtype; p->info_requests++;
            // Validation/order: Atmosphere 6b0c077b4c608a3a2ee712b8dc35fee87ceb62e8,
            // libraries/libmesosphere/source/svc/kern_svc_info.cpp GetInfo.
            uint32_t status;
            uint64_t value = 0;
            if (p->last_info_type == 11) {
                status = p->last_info_handle != 0 ? 0xE401 : (subtype >= 4 ? 0xE801 : 0);
                if (!status) value = p->random_entropy[subtype];
            } else if (p->last_info_type <= 1) {
                // These are validated requested guest process limits, not host
                // scheduler permissions or a claim of additional runnable threads.
                if (!p->has_core_priority) goto unsupported_info;
                status = subtype ? 0xE801 : (p->last_info_handle != 0xFFFF8001 ? 0xE401 : 0);
                if (!status) value = p->last_info_type ? p->allowed_priority_mask : p->allowed_core_mask;
            } else if (p->last_info_type == 6 || p->last_info_type == 7) {
                // Explicit normal-application emulator profile. In pinned
                // KSystemControlBase the required non-applet secure charge is
                // its full size, NOT the dynamic-manager-used pages (Info17).
                // The existing fixed budget already withholds that capacity.
                status = subtype ? 0xE801 : (p->last_info_handle != 0xFFFF8001 ? 0xE401 : 0);
                if (!status) value = (p->last_info_type == 6 ? p->user_memory_limit : p->user_memory_used) +
                    satchel_switch_process_system_resource_total(p);
            } else if (p->last_info_type == 16 || p->last_info_type == 17) {
                // Pinned Atmosphere KProcess GetTotal/UsedSystemResourceSize:
                // non-default capacity versus actual used resource pages.
                // Our original arena owns the real map metadata allocation;
                // default/zero resource uses fallback storage and reports0.
                status = subtype ? 0xE801 : (p->last_info_handle != 0xFFFF8001 ? 0xE401 : 0);
                if (!status) value = p->last_info_type == 16 ?
                    satchel_switch_process_system_resource_total(p) : satchel_switch_process_system_resource_used(p);
            } else if (p->last_info_type == 21 || p->last_info_type == 22) {
                // KProcess separates non-system user memory (21/22) from the
                // full secure-system allocation charge (6/7). No virtual
                // heap/alias reservation is counted as allocated guest memory.
                status = subtype ? 0xE801 : (p->last_info_handle != 0xFFFF8001 ? 0xE401 : 0);
                if (!status) value = p->last_info_type == 21 ? p->user_memory_limit : p->user_memory_used;
            } else if (p->last_info_type == 28) {
                status = subtype ? 0xE801 : (p->last_info_handle != 0xFFFF8001 ? 0xE401 : 0);
                if (!status) value = p->alias_region_extra_size;
            } else {
                ProcessRegion region;
                switch (p->last_info_type) {
                    case 2: case 3: region = p->alias_region; break;
                    case 4: case 5: region = p->heap_region; break;
                    case 12: case 13: region = p->aslr_region; break;
                    case 14: case 15: region = p->stack_region; break;
                    default: goto unsupported_info;
                }
                // Process queries validate subtype before looking up the handle.
                // Only the current-process pseudo handle names this process;
                // no process-handle table or debug-process aliases are invented.
                status = subtype ? 0xE801 : (p->last_info_handle != 0xFFFF8001 ? 0xE401 : 0);
                if (!status) value = p->last_info_type & 1 ? region.size : region.address;
            }
            satchel_cpu_probe_set_x(p->cpu, 0, status); satchel_cpu_probe_set_x(p->cpu, 1, value);
            p->info_serviced++;
        } else {
            break;
        }
        serviced = 1;
        if (p->scheduler_switch) break;
    } while (steps < budget);
    if (serviced && steps == budget && !p->scheduler_switch) {
        result = satchel_cpu_probe_run(p->cpu, 0, cancelled, context);
    }
unsupported_info:
    result.steps = steps; return result;
}

int satchel_switch_process_mutex_info(const SatchelSwitchProcess *p, SatchelSwitchMutexInfo *out) {
    if (!p || !out) return 0;
    SatchelSwitchMutexInfo value = {p->mutex_lock_requests,p->mutex_unlock_requests,p->mutex_waits,p->mutex_handoffs,0};
    for (unsigned i = 0; i < p->thread_count; i++) value.current_waiters += p->threads[i].cpu && p->threads[i].state == SATCHEL_SWITCH_THREAD_MUTEX_WAIT;
    *out = value; return 1;
}
uint32_t satchel_switch_process_thread_count(const SatchelSwitchProcess *p) {
    uint32_t count = 0;
    if (p) for (unsigned i = 0; i < p->thread_count; i++) count += p->threads[i].cpu != NULL;
    return count;
}
int satchel_switch_process_thread_info(const SatchelSwitchProcess *p, uint32_t index, SatchelSwitchThreadInfo *out) {
    if (!p || !out || index >= p->thread_count || !p->threads[index].cpu) return 0;
    const ProcessThread *thread = p->threads + index;
    *out = (SatchelSwitchThreadInfo){thread->id,thread->tls_address,thread->affinity_mask,thread->instructions,
        thread->handle,thread->priority,thread->cpu_id,thread->state,thread->handle_references,thread->wait_references,thread->ideal_core_id,thread->effective_priority};
    return 1;
}
int satchel_switch_process_thread_wait_info(const SatchelSwitchProcess *p, uint32_t index, SatchelSwitchThreadWaitInfo *out) {
    if (!p || !out || index >= p->thread_count || !p->threads[index].cpu) return 0;
    SatchelSwitchThreadWaitInfo value = {0};
    const ProcessThread *thread = p->threads + index;
    if (thread->state == SATCHEL_SWITCH_THREAD_WAITING) for (size_t i = 0; i < thread->wait_count; i++) {
        const ProcessWaitObject object = thread->wait[i];
        if (object.kind == HANDLE_THREAD) value.threads++;
        else if (object.id == p->applet_message_event) value.applet_message_events++;
        else if (object.id == p->gpu_error_event) value.gpu_error_events++;
        else if (object.id == p->suspended_tick_event) value.suspension_events++;
        else value.other_events++;
    }
    *out = value; return 1;
}
SatchelSwitchRunResult satchel_switch_process_run_scheduled(SatchelSwitchProcess *p, uint64_t budget,
        SatchelCPUProbeCancelled cancelled, void *context) {
    SatchelSwitchRunResult output = {.state=SATCHEL_SWITCH_RUN_CPU_STOP,.cpu={.reason=SATCHEL_CPU_INTERNAL_FAULT},.next_deadline_tick=UINT64_MAX};
    if (!p) return output;
    p->has_run = 1;
    output.thread_id = p->last_thread_stop_id;
    uint64_t steps = 0;
    for (unsigned zero_steps = 0; zero_steps < 128;) {
        if (cancelled && cancelled(context)) {
            output.cpu = p->cpu ? satchel_cpu_probe_run(p->cpu,0,NULL,NULL) : p->last_thread_stop;
            output.cpu.reason = SATCHEL_CPU_CANCELLED;
            output.thread_id = p->cpu ? p->threads[p->active_thread].id : p->last_thread_stop_id;
            output.cpu.steps = steps; return output;
        }
        if (steps == budget) {
            output.cpu = p->cpu ? satchel_cpu_probe_run(p->cpu,0,NULL,NULL) : p->last_thread_stop;
            output.cpu.reason = SATCHEL_CPU_BUDGET;
            output.cpu.steps = steps;
            output.thread_id = p->cpu ? p->threads[p->active_thread].id : p->last_thread_stop_id;
            return output;
        }
        int any = 0, live = 0;
        for (unsigned i = 0; i < p->thread_count; i++) if (p->threads[i].cpu) {
            any = 1; if (p->threads[i].state != SATCHEL_SWITCH_THREAD_EXITED) live = 1;
        }
        if (!any || !live) {
            output.state = SATCHEL_SWITCH_RUN_EXITED; output.cpu = p->last_thread_stop; output.cpu.steps = steps;
            reap_threads(p); return output;
        }
        if (!refresh_waits(p)) {
            output.cpu.steps = steps;
            snprintf(output.cpu.detail,sizeof(output.cpu.detail),"Owned scheduler clock or wait references are invalid."); return output;
        }
        unsigned selected = SATCHEL_SWITCH_THREAD_LIMIT, selected_core = 0;
        for (unsigned c = 0; c < 4 && selected == SATCHEL_SWITCH_THREAD_LIMIT; c++) {
            unsigned core = (p->schedule_core_cursor + c) % 4, priority = 64;
            for (unsigned n = 0; n < p->thread_count; n++) {
                unsigned i = (p->schedule_cursor[core] + n) % p->thread_count;
                ProcessThread *thread = p->threads + i;
                if (thread->cpu && thread->state == SATCHEL_SWITCH_THREAD_RUNNABLE && thread->cpu_id == core && thread->effective_priority < priority) {
                    selected = i; selected_core = core; priority = thread->effective_priority;
                }
            }
        }
        if (selected == SATCHEL_SWITCH_THREAD_LIMIT) {
            output.state = SATCHEL_SWITCH_RUN_BLOCKED; output.cpu = p->last_thread_stop; output.cpu.steps = steps;
            for (unsigned i = 0; i < p->thread_count; i++) if (p->threads[i].cpu &&
                (p->threads[i].state == SATCHEL_SWITCH_THREAD_WAITING || p->threads[i].state == SATCHEL_SWITCH_THREAD_SLEEPING) &&
                p->threads[i].deadline < output.next_deadline_tick) output.next_deadline_tick = p->threads[i].deadline;
            return output;
        }
        p->active_thread = selected; p->cpu = p->threads[selected].cpu;
        p->schedule_cursor[selected_core] = (selected + 1) % p->thread_count;
        p->schedule_core_cursor = (selected_core + 1) % 4; p->scheduler_switch = 0;
        uint64_t quantum = budget - steps;
        if (quantum > SATCHEL_SWITCH_THREAD_QUANTUM) quantum = SATCHEL_SWITCH_THREAD_QUANTUM;
        SatchelCPUProbeResult result = run_thread_slice(p,quantum,cancelled,context);
        p->threads[selected].instructions += result.steps;
        steps += result.steps; p->last_thread_stop = result;
        p->last_thread_stop_id = p->threads[selected].id;
        output.thread_id = p->last_thread_stop_id;
        if (result.steps) zero_steps = 0; else zero_steps++;
        if (p->scheduler_switch) { reap_threads(p); continue; }
        if (result.reason == SATCHEL_CPU_BUDGET) continue;
        output.cpu = result; output.cpu.steps = steps; return output;
    }
    output.cpu.steps = steps;
    snprintf(output.cpu.detail,sizeof(output.cpu.detail),"Scheduler exhausted its bounded zero-instruction transition budget.");
    return output;
}
SatchelCPUProbeResult satchel_switch_process_run(SatchelSwitchProcess *p, uint64_t budget,
        SatchelCPUProbeCancelled cancelled, void *context) {
    // Legacy CPU-only result preserves the last genuine SVC stop when the
    // scheduler blocks; multithreaded callers must use the explicit wrapper.
    return satchel_switch_process_run_scheduled(p,budget,cancelled,context).cpu;
}
