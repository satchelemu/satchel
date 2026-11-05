// Horizon service behavior adapted from Ryujinx 1.3.3.
// Copyright (c) Ryujinx Team and Contributors. MIT, see RYUJINX-LICENSE.txt.
// The native transport, bounds checks and diagnostic sink are Satchel glue.
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SATCHEL_IPC_INVALID 0xf601u
#define SATCHEL_IPC_UNKNOWN ((221u << 9) | 10u)
#define SATCHEL_ACC_NULL ((20u << 9) | 124u)
#define SATCHEL_ACC_NOT_FOUND ((100u << 9) | 124u)
#define SATCHEL_ACC_BUFFER ((32u << 9) | 124u)
static const char *const satchel_languages[] = {
    "ja", "en-US", "fr", "de", "it", "es", "zh-CN", "ko", "nl", "pt", "ru",
    "zh-TW", "en-GB", "fr-CA", "es-419", "zh-Hans", "zh-Hant", "pt-BR"
};
// Match hle_account.c. Do not create a second identity for IPC clients.
static const uint64_t satchel_uid[2] = {0x78626c0000000001ull, 0x53574e4154495645ull};
static uint32_t satchel_u32(const void *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t satchel_u64(const void *p) { uint64_t v; memcpy(&v, p, 8); return v; }
static void satchel_put32(void *p, uint32_t v) { memcpy(p, &v, 4); }

// Defined by the engine adapter or the synthetic transport test.
static int satchel_ipc_read(uint64_t address, void *bytes, size_t size);
static void satchel_event_set(uint32_t handle, int signaled);
static int satchel_ipc_write(uint64_t address, const void *bytes, size_t size);
static void satchel_ipc_emit(const char *service, uint32_t cmd, const char *kind, void *caller);
static uint32_t satchel_time_control(unsigned command, const uint8_t *in, uint8_t *out);
static int64_t satchel_clock_rtc(void);
static uint32_t satchel_fs_open(unsigned type, uint32_t *index);
static uint32_t satchel_fs_query(uint32_t index, unsigned command, const char *path, uint8_t *out);
static uint32_t satchel_clock_change(unsigned kind, unsigned cmd, const uint8_t *in, uint8_t *out);
static uint32_t satchel_clock_now(int64_t *out);
static uint32_t satchel_clock_context(uint8_t *out);
static uint32_t satchel_clock_steady(uint8_t *out);
static uint32_t satchel_event_create(int signaled);
static uint32_t satchel_event_copy(uint32_t handle);
static void satchel_event_close(uint32_t handle);
static unsigned satchel_aoc_list(uint32_t *out, unsigned start, unsigned count);
static uint64_t satchel_program_id(void);
static void satchel_missing_record(const char *service, const char *interface, uint32_t command);
static void satchel_missing_reset(void);
typedef struct { unsigned refs; size_t size; uint8_t bytes[]; } SatchelIPCStorage;
typedef struct {
    const char *kind;
    const char *service;
    uint32_t events[5];
    uint32_t values[16];
    SatchelIPCStorage *storage;
    int transmit, agreement;
    uint64_t system_id;
} SatchelIPCObject;
static void satchel_object_clear(SatchelIPCObject *o) {
    for (unsigned i = 0; i < 5; i++) if (o->events[i]) satchel_event_close(o->events[i]);
    if (o->storage && !--o->storage->refs) free(o->storage);
    memset(o, 0, sizeof *o);
}
static struct { char service[80]; uint32_t cmd; unsigned kind; } satchel_seen[1024];
static unsigned satchel_seen_count;
static pthread_mutex_t satchel_seen_mu = PTHREAD_MUTEX_INITIALIZER;
static void satchel_ipc_once(const char *service, uint32_t cmd, const char *kind, void *caller) {
    unsigned k = !strcmp(kind, "stub") ? 1 : !strcmp(kind, "missing") ? 2 : 0;
    pthread_mutex_lock(&satchel_seen_mu);
    for (unsigned i = 0; i < satchel_seen_count; i++)
        if (satchel_seen[i].cmd == cmd && satchel_seen[i].kind == k && !strcmp(satchel_seen[i].service, service)) {
            pthread_mutex_unlock(&satchel_seen_mu); return;
        }
    // Bounded memory and output. Once full, retain the first 1024 distinct gaps.
    if (satchel_seen_count < 1024) {
        unsigned i = satchel_seen_count++;
        strncpy(satchel_seen[i].service, service, sizeof satchel_seen[i].service - 1); satchel_seen[i].cmd = cmd; satchel_seen[i].kind = k;
        satchel_ipc_emit(service, cmd, kind, caller);
    }
    pthread_mutex_unlock(&satchel_seen_mu);
}
static void satchel_ipc_reset(void) {
    pthread_mutex_lock(&satchel_seen_mu);
    memset(satchel_seen, 0, sizeof satchel_seen); satchel_seen_count = 0;
    satchel_missing_reset();
    satchel_time_control(0, NULL, NULL);
    pthread_mutex_unlock(&satchel_seen_mu);
}

typedef struct { uint64_t address, size; int present; } SatchelIPCBuffer;
typedef struct {
    uint32_t command, token, version, object;
    const uint8_t *args; size_t args_size;
    SatchelIPCBuffer receive, list, send, pointer;
    uint32_t input_object, input_handle;
    SatchelIPCObject *input;
    SatchelIPCObject seed;
    int consume_applet_output;
    int domain;
    const char *child;
    SatchelIPCObject *state;
    uint32_t copies[2]; unsigned copy_count;
} SatchelIPCRequest;

static int satchel_ipc_parse(const uint8_t *base, size_t size, int domain, SatchelIPCRequest *r) {
    if (!base || size < 0x40 || size > 0x1000) return 0;
    uint32_t h0 = satchel_u32(base), h1 = satchel_u32(base + 4);
    unsigned type = h0 & 0xffff, nx = h0 >> 16 & 15, na = h0 >> 20 & 15;
    unsigned nb = h0 >> 24 & 15, nw = h0 >> 28 & 15;
    if (type != 4 && type != 6) return 0;
    size_t p = 8;
    if (h1 >> 31) {
        uint32_t special = satchel_u32(base + p); p += 4;
        unsigned copy = special >> 1 & 15, move = special >> 5 & 15;
        size_t handles = p + (special & 1 ? 8 : 0);
        if (handles + (copy + move) * 4 > size) return 0;
        if (move == 1) r->input_handle = satchel_u32(base + handles + copy * 4);
        p = handles + (copy + move) * 4;
    }
    if (p + nx * 8 + na * 12 > size) return 0;
    if (nx) {
        uint64_t a = satchel_u32(base + p), b = satchel_u32(base + p + 4);
        r->pointer = (SatchelIPCBuffer){b | ((a << 20) & 0xf00000000ull) | ((a << 30) & 0x7000000000ull), a >> 16, 1};
    }
    p += nx * 8;
    if (na) {
        uint64_t a = satchel_u32(base + p), b = satchel_u32(base + p + 4), c = satchel_u32(base + p + 8);
        r->send = (SatchelIPCBuffer){b | ((c << 4) & 0xf00000000ull) | ((c << 34) & 0x7000000000ull), a | ((c << 8) & 0xf00000000ull), 1};
    }
    p += na * 12;
    if (p > size || (nb + nw) * 12 > size - p) return 0;
    if (nb) {
        uint64_t w0 = satchel_u32(base + p), w1 = satchel_u32(base + p + 4), w2 = satchel_u32(base + p + 8);
        r->receive = (SatchelIPCBuffer){w1 | ((w2 << 4) & 0xf00000000ull) | ((w2 << 34) & 0x7000000000ull),
            w0 | ((w2 << 8) & 0xf00000000ull), 1};
    }
    p += (nb + nw) * 12;
    size_t bytes = (h1 & 0x3ff) * 4, end = p + bytes;
    if (bytes > size - p) return 0;
    unsigned flags = h1 >> 10 & 15, lists = flags < 2 ? 0 : flags == 2 ? 1 : flags - 2;
    if (lists * 8 > size - end) return 0;
    if (lists) {
        uint64_t packed = satchel_u64(base + end);
        r->list = (SatchelIPCBuffer){packed & 0xffffffffffffull, packed >> 48, 1};
    }
    // Padding is relative to the actual HIPC buffer address, not its allocation.
    size_t cm = p + ((16 - (((uintptr_t)base + p) & 15)) & 15);
    r->domain = domain;
    if (domain) {
        if (cm > end || end - cm < 16) return 0;
        unsigned dtype = base[cm], objects = base[cm + 1];
        uint16_t data_size; memcpy(&data_size, base + cm + 2, 2);
        r->object = satchel_u32(base + cm + 4);
        if (!r->object || objects > 1) return 0;
        if (dtype == 2) return 2;
        if (dtype != 1 || data_size < 16 || data_size > end - cm - 16) return 0;
        if (objects) {
            if (end - cm - 16 - data_size < 4) return 0;
            r->input_object = satchel_u32(base + cm + 16 + data_size);
        }
        cm += 16; end = cm + data_size;
    }
    if (cm > end || end - cm < 16 || satchel_u32(base + cm) != 0x49434653) return 0;
    r->version = satchel_u32(base + cm + 4); r->command = satchel_u32(base + cm + 8); r->token = satchel_u32(base + cm + 12);
    if (r->version > 1) return 0;
    r->args = base + cm + 16; r->args_size = end - cm - 16;
    return 1;
}
static int satchel_write_buffer(SatchelIPCBuffer b, const void *p, size_t n) {
    return b.present && n <= b.size && (!n || (b.address && b.address <= UINT64_MAX - n && satchel_ipc_write(b.address, p, n)));
}
static uint32_t satchel_event_reply(SatchelIPCRequest *r, unsigned count, int signaled) {
    if (!r->state) return SATCHEL_IPC_INVALID;
    for (unsigned i = 0; i < count; i++) {
        if (!r->state->events[i]) r->state->events[i] = satchel_event_create(signaled);
        if (!r->state->events[i]) return ((105u << 9) | 1u);
        uint32_t copy = satchel_event_copy(r->state->events[i]);
        if (!copy) return ((105u << 9) | 1u);
        r->copies[r->copy_count++] = copy;
    }
    return 0;
}
#define SATCHEL_AOC_BUFFER ((200u << 9) | 166u)
#define SATCHEL_FRIEND_ARGUMENT ((2u << 9) | 121u)
#define SATCHEL_FRIEND_EMPTY ((15u << 9) | 121u)
#define SATCHEL_TIME_PERMISSION ((1u << 9) | 116u)
#define SATCHEL_TIME_UNINITIALIZED ((103u << 9) | 116u)
#define SATCHEL_NO_INTERNET ((300u << 9) | 110u)
#include "SystemIPC.inc"
#include "Round8IPC.inc"
#include "Round8Trace.inc"
static uint32_t satchel_ipc_dispatch(const char *service, SatchelIPCRequest *r, uint8_t *out, size_t *n, void *caller) {
    uint32_t cmd = r->command; *n = 0;
    uint32_t system_result = satchel_round8_dispatch(service, r, out, n, caller);
    if (system_result != SATCHEL_IPC_UNKNOWN) satchel_round8_trace(service, cmd, system_result);
    if (system_result == SATCHEL_IPC_UNKNOWN) system_result = satchel_system_dispatch(service, r, out, n, caller);
    if (system_result != SATCHEL_IPC_UNKNOWN) {
        if (system_result == ((513u << 9) | 153u) || system_result == ((999u << 9) | 128u)) {
            satchel_missing_record(r->state && r->state->service ? r->state->service : service, service, cmd);
            satchel_ipc_once(service, cmd, "stub", caller);
        }
        return system_result;
    }
    if (!strcmp(service, "aoc:u")) {
        size_t offset = (cmd == 0 || cmd == 1 || cmd == 4 || cmd == 6) ? 8 : 0;
        if (r->args_size < offset) return SATCHEL_IPC_INVALID;
        switch (cmd) {
        case 0: case 2: {
            // DLC packages belong to the running application, never a host-wide catalogue.
            unsigned count = offset && satchel_u64(r->args) != satchel_program_id() ? 0 : satchel_aoc_list(NULL, 0, 0);
            satchel_put32(out, count); *n = 4; return 0;
        }
        case 1: case 3: {
            if (r->args_size < offset + 8 || !r->receive.present) return SATCHEL_IPC_INVALID;
            unsigned start = satchel_u32(r->args + offset), count = satchel_u32(r->args + offset + 4);
            if (count > r->receive.size / 4) return SATCHEL_AOC_BUFFER;
            if (count > 2000) count = 2000;
            uint32_t *indices = calloc(count ? count : 1, sizeof *indices);
            if (!indices) return ((105u << 9) | 1u);
            unsigned got = offset && satchel_u64(r->args) != satchel_program_id() ? 0 : satchel_aoc_list(indices, start, count);
            int written = satchel_write_buffer(r->receive, indices, got * 4); free(indices);
            if (!written) return SATCHEL_IPC_INVALID;
            satchel_put32(out, got); *n = 4; return 0;
        }
        case 4: case 5: {
            // Prepared packages currently use the standard program-id based DLC namespace.
            uint64_t base = (offset ? satchel_u64(r->args) : satchel_program_id()) + 0x1000;
            memcpy(out, &base, 8); *n = 8; return 0;
        }
        case 6: case 7:
            if (r->args_size < offset + 4) return SATCHEL_IPC_INVALID;
            satchel_ipc_once(service, cmd, "stub", caller); return 0; // Reference prepare is a no-op.
        case 8: case 10: return satchel_event_reply(r, 1, 0);
        case 9: {
            uint64_t error = (1000ull << 32) | 2164; memcpy(out, &error, 8); *n = 8; return 0;
        }
        case 50: satchel_ipc_once(service, cmd, "stub", caller); return 0;
        }
    } else if (!strcmp(service, "friend:u")) {
        if (cmd == 0) { r->child = "friend:service"; return 0; }
        if (cmd == 1) {
            if (r->args_size < 16) return SATCHEL_IPC_INVALID;
            if (!(satchel_u64(r->args) | satchel_u64(r->args + 8))) return SATCHEL_FRIEND_ARGUMENT;
            r->child = "friend:notify"; return 0;
        }
    } else if (!strcmp(service, "friend:notify")) {
        if (cmd == 0) return satchel_event_reply(r, 1, 0);
        if (cmd == 1) return 0; // Offline notification queue stays empty.
        if (cmd == 2) return SATCHEL_FRIEND_EMPTY;
    } else if (!strcmp(service, "friend:service")) {
        if (cmd == 0) return satchel_event_reply(r, 1, 1); // Reference completion starts signaled.
        if (cmd == 1) { satchel_ipc_once(service, cmd, "stub", caller); return 0; }
        switch (cmd) {
        case 10100: case 10101: case 10400: {
            // CMIF packs the 32-bit offset before the aligned Uid and filter.
            size_t required = cmd == 10400 ? 24 : 48;
            if (r->args_size < required) return SATCHEL_IPC_INVALID;
            SatchelIPCBuffer b = cmd == 10101 ? r->receive : r->list;
            if (!b.present) return SATCHEL_IPC_INVALID;
            if (cmd != 10400 && !(satchel_u64(r->args + 8) | satchel_u64(r->args + 16))) return SATCHEL_FRIEND_ARGUMENT;
            satchel_ipc_once(service, cmd, "stub", caller); *n = 4; return 0;
        }
        case 10120: case 10121: case 10420: case 10421:
            if (r->args_size < 16) return SATCHEL_IPC_INVALID;
            satchel_ipc_once(service, cmd, "stub", caller);
            if (cmd == 10120 || cmd == 10420) { out[0] = 1; *n = 1; }
            return 0;
        }
    } else if (!strcmp(service, "prepo:u")) {
        if (!r->state) return SATCHEL_IPC_INVALID;
        switch (cmd) {
        // Reports are deliberately discarded, never logged, parsed or transmitted.
        // Like Ryujinx SaveReport, validation inside ProcessPlayReport is not propagated.
        case 10100: case 10102: case 10104:
        case 10101: case 10103: case 10105:
            if ((cmd & 1) && r->args_size < 16) return SATCHEL_IPC_INVALID;
            satchel_ipc_once(service, cmd, "stub", caller); return 0;
        case 10200: r->state->transmit = 1; return 0;
        case 10300: satchel_put32(out, r->state->transmit && r->state->agreement); *n = 4; return 0;
        case 10400:
            if (!r->state->system_id) r->state->system_id = ((uint64_t)arc4random() << 32) | arc4random() | 1;
            memcpy(out, &r->state->system_id, 8); *n = 8; return 0;
        case 40100: out[0] = r->state->agreement; *n = 1; return 0;
        case 40101:
            if (!r->args_size) return SATCHEL_IPC_INVALID;
            r->state->agreement = !!r->args[0]; return 0;
        }
    } else if (!strcmp(service, "nifm:request")) {
        switch (cmd) {
        case 0: satchel_ipc_once(service, cmd, "stub", caller); satchel_put32(out, 1); *n = 4; return 0;
        case 1: return SATCHEL_NO_INTERNET; // Offline host policy, never claim network success.
        case 2: return satchel_event_reply(r, 2, 0);
        case 3: case 4: satchel_ipc_once(service, cmd, "stub", caller); return 0;
        case 11:
            if (!r->args_size) return SATCHEL_IPC_INVALID;
            satchel_ipc_once(service, cmd, "stub", caller); return 0;
        case 21:
            if (r->args_size < 4) return SATCHEL_IPC_INVALID;
            return (180u << 9) | 110u; // Reference has no network applet information.
        }
    } else if (!strcmp(service, "time:u")) {
        switch (cmd) {
        case 0: case 4: r->child = "time:local"; return 0;
        case 1: case 5: r->child = "time:network"; return 0;
        case 2: r->child = "time:steady"; return 0;
        case 50: case 101: return SATCHEL_TIME_PERMISSION;
        // Automatic correction and network calibration are unavailable offline.
        case 100: case 200: *n = 1; return 0;
        case 102: satchel_put32(out, 2019); *n = 4; return 0;
        }
    } else if (!strcmp(service, "time:local") || !strcmp(service, "time:network")) {
        switch (cmd) {
        case 1: case 3: return SATCHEL_TIME_PERMISSION;
        case 0: case 2:
            if (!strcmp(service, "time:network")) return SATCHEL_TIME_UNINITIALIZED;
            if (cmd == 2) { *n = 32; return satchel_clock_context(out); }
            else {
                int64_t now; uint32_t result = satchel_clock_now(&now);
                if (!result) { memcpy(out, &now, 8); *n = 8; }
                return result;
            }
        }
    } else if (!strcmp(service, "time:steady")) {
        switch (cmd) {
        case 0: *n = 24; return satchel_clock_steady(out);
        case 2: case 200: *n = 8; return 0; // Test and internal offsets start at zero.
        case 3: case 201: return SATCHEL_TIME_PERMISSION;
        case 101: *n = 1; return 0; // No emulated RTC reset.
        case 102: *n = 4; return 0; // Host monotonic clock is initialized.
        }
    } else if (!strcmp(service, "nifm:u")) {
        if (cmd == 4 || cmd == 5) {
            if (cmd == 5 && r->args_size < 8) return SATCHEL_IPC_INVALID;
            r->child = "nifm:general"; return 0;
        }
    } else if (!strcmp(service, "nifm:general")) {
        switch (cmd) {
        // Match the reference's no-interface branch. The engine disables BSD networking.
        // Do not report the Mac or phone's IP address as a working guest connection.
        case 5: case 12: case 15: case 18: return SATCHEL_NO_INTERNET;
        case 4:
            if (r->args_size < 4) return SATCHEL_IPC_INVALID;
            r->child = "nifm:request"; return 0;
        }
    } else if (!strcmp(service, "set")) {
        switch (cmd) {
        case 0: {
            const char *lang = getenv("DM_LANG"); if (!lang || !*lang) lang = "en-US";
            strncpy((char *)out, lang, 7); *n = 8; return 0;
        }
        case 1: case 5: {
            SatchelIPCBuffer b = cmd == 1 ? r->list : r->receive;
            if (!b.present) return SATCHEL_IPC_INVALID;
            size_t count = b.size / 8, max = cmd == 1 ? 15 : 18;
            if (count > max) count = max;
            char codes[18][8] = {{0}};
            for (size_t i = 0; i < count; i++) strncpy(codes[i], satchel_languages[i], 7);
            if (!satchel_write_buffer(b, codes, count * 8)) return SATCHEL_IPC_INVALID;
            satchel_put32(out, (uint32_t)count); *n = 4; return 0;
        }
        case 2: {
            if (r->args_size < 4) return SATCHEL_IPC_INVALID;
            uint32_t index = satchel_u32(r->args);
            if (index >= 18) return (625u << 9) | 105u;
            strncpy((char *)out, satchel_languages[index], 7); *n = 8; return 0;
        }
        case 3: case 6: satchel_put32(out, cmd == 3 ? 15 : 18); *n = 4; return 0;
        case 4: {
            // Satchel has no firmware settings save. Use the same language policy as hle_oe.c.
            const char *e = getenv("DM_LANG"); if (!e || !*e) e = "en-US";
            uint32_t region = !strncmp(e, "ja", 2) ? 0 : !strncmp(e, "ko", 2) || !strncmp(e, "zh", 2) ? 4 :
                !strcmp(e, "en-US") || !strcmp(e, "fr-CA") || !strcmp(e, "es-419") || !strcmp(e, "pt-BR") ? 1 : 2;
            satchel_put32(out, region); *n = 4; return 0;
        }
        case 8: satchel_ipc_once(service, cmd, "stub", caller); *n = 1; return 0;
        }
    } else if (!strcmp(service, "acc:profile")) {
        switch (cmd) {
        case 0: {
            uint8_t data[0x80] = {0}; satchel_put32(data + 4, 1); data[8] = 1;
            if (!satchel_write_buffer(r->list, data, sizeof data)) return SATCHEL_ACC_BUFFER;
            satchel_ipc_once(service, cmd, "stub", caller); // Reference UserData icon policy.
            // Fall through to the same ProfileBase returned by GetBase.
        }
        __attribute__((fallthrough));
        case 1: {
            memcpy(out, satchel_uid, 16);
            memset(out + 16, 0, 8); // Fixed local profile, no persisted modification time.
            const char *name = getenv("DM_NICK"); if (!name || !*name) name = "Player";
            strncpy((char *)out + 24, name, 32); *n = 56; return 0;
        }
        }
    } else if (!strcmp(service, "acc:u0")) {
        switch (cmd) {
        case 0: satchel_put32(out, 1); *n = 4; return 0;
        case 1:
            if (r->args_size < 16) return SATCHEL_IPC_INVALID;
            if (!(satchel_u64(r->args) | satchel_u64(r->args + 8))) return SATCHEL_ACC_NULL;
            out[0] = !memcmp(r->args, satchel_uid, 16); *n = 1; return 0;
        case 2: case 3: case 141: {
            SatchelIPCBuffer b = r->list;
            if (!b.present || b.size > 0xffff) return SATCHEL_ACC_BUFFER;
            // Ryujinx clears the entire array and writes only complete Uids.
            uint8_t block[128] = {0};
            for (uint64_t offset = 0; offset < b.size; offset += sizeof block) {
                size_t part = b.size - offset < sizeof block ? (size_t)(b.size - offset) : sizeof block;
                SatchelIPCBuffer tail = {b.address + offset, part, 1};
                if (tail.address < b.address || !satchel_write_buffer(tail, block, part)) return SATCHEL_ACC_BUFFER;
            }
            if (b.size >= 16 && !satchel_write_buffer(b, satchel_uid, 16)) return SATCHEL_ACC_BUFFER;
            return 0;
        }
        case 4: memcpy(out, satchel_uid, 16); *n = 16; return 0;
        case 5:
            if (r->args_size < 16) return SATCHEL_IPC_INVALID;
            if (!(satchel_u64(r->args) | satchel_u64(r->args + 8))) return SATCHEL_ACC_NULL;
            if (memcmp(r->args, satchel_uid, 16)) return SATCHEL_ACC_NOT_FOUND;
            r->child = "acc:profile"; return 0;
        case 50: *n = 1; return 0; // Application service cannot register users.
        case 51:
            if (!r->args_size) return SATCHEL_IPC_INVALID;
            if (r->args[0]) satchel_ipc_once(service, cmd, "stub", caller); // Ryujinx's offline selection policy.
            memcpy(out, satchel_uid, 16); *n = 16; return 0;
        }
    } else if (!strcmp(service, "olsc:u")) {
        // Preserve the pinned engine's cloud-backup-off compatibility behavior.
        satchel_ipc_once(service, cmd, "stub", caller); *n = 16; return 0;
    } else if (!strncmp(service, "caps:", 5)) {
        // The pinned engine's capture service sessions (nxnative df7fa667): every request succeeds with
        // zeroed output, so a game that reaches the album or screenshot service at start does not abort.
        satchel_ipc_once(service, cmd, "stub", caller); *n = 16; return 0;
    }
    satchel_missing_record(r->state && r->state->service ? r->state->service : service, service, cmd);
    satchel_ipc_once(service, cmd, "unknown", caller);
    return SATCHEL_IPC_UNKNOWN;
}
static uint32_t satchel_ipc_reply(SatchelIPCRequest *r, void *message, size_t size, uint32_t result,
                             const uint8_t *output, size_t n, uint32_t child) {
    uint8_t *base = message; int domain = r->domain;
    if (result) { n = 0; child = 0; }
    unsigned copies = result ? 0 : r->copy_count;
    int special = copies || (child && !domain);
    size_t raw = special ? 12 + copies * 4 + (child && !domain ? 4 : 0) : 8;
    size_t cm = raw + ((16 - (((uintptr_t)base + raw) & 15)) & 15) + (domain ? 16 : 0);
    // Include the CMIF pre/post padding in the HIPC raw-word count.
    size_t end = raw + 16 + (domain ? 16 : 0) + 16 + ((n + 3) & ~(size_t)3) + (child && domain ? 4 : 0);
    if (end > size) return SATCHEL_IPC_INVALID;
    memset(base, 0, size);
    satchel_put32(base + 4, (uint32_t)((end - raw) / 4) | (special ? 1u << 31 : 0));
    if (special) {
        satchel_put32(base + 8, (copies << 1) | (child && !domain ? 1u << 5 : 0));
        for (unsigned i = 0; i < copies; i++) satchel_put32(base + 12 + i * 4, r->copies[i]);
        if (child && !domain) satchel_put32(base + 12 + copies * 4, child);
    }
    if (child && domain) {
        satchel_put32(base + cm - 16, 1);
        satchel_put32(base + cm + 16 + ((n + 3) & ~(size_t)3), child);
    }
    satchel_put32(base + cm, 0x4f434653); satchel_put32(base + cm + 4, r->version);
    satchel_put32(base + cm + 8, result); satchel_put32(base + cm + 12, r->token);
    memcpy(base + cm + 16, output, n);
    return 0; // Transport success, command result lives in SFCO.
}
static uint32_t satchel_ipc_send(const char *service, int domain, void *message, size_t size, void *caller) {
    SatchelIPCRequest r = {0};
    int parsed = satchel_ipc_parse(message, size, domain, &r);
    if (!parsed || (domain && r.object != 1)) return SATCHEL_IPC_INVALID;
    if (parsed == 2) { memset(message, 0, 16); return 0; }
    uint8_t output[64] = {0}; size_t n = 0;
    uint32_t result = satchel_ipc_dispatch(service, &r, output, &n, caller);
    if (r.child) result = SATCHEL_IPC_INVALID; // Object creation requires a session owner.
    satchel_object_clear(&r.seed);
    return satchel_ipc_reply(&r, message, size, result, output, n, 0);
}
