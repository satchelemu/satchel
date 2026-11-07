// Round 11 contracts adapted from Ryujinx 1.3.3: apm, psm, csrng, mm:u and fatal:u.
// Sources: HLE Services/Apm, Ptm/Psm, Spl/IRandomInterface.cs, Fatal/IService.cs and
// Horizon MmNv/Ipc/Request.cs with Sdk/MmNv/Session.cs.
// Copyright (c) Ryujinx Team and Contributors. MIT, see RYUJINX-LICENSE.txt.
#define SATCHEL_APM_INVALID ((1u << 9) | 148u)
#define SATCHEL_ROUND11_FULL ((105u << 9) | 1u)
// Defined by the engine adapter or the synthetic transport test.
static unsigned satchel_perf_mode(void);
static void satchel_random(void *bytes, size_t size);
// Ryujinx PerformanceState is system wide; here it lives for one game session.
static struct { uint32_t config[2]; int overclock; } satchel_apm = {{0x00020003u, 0x00020004u}, 0};
// Ryujinx MmNv keeps one session list for the port, appended in registration order.
#define SATCHEL_MM_MAX 64
static struct { uint32_t id, module, min; int32_t max; int used; } satchel_mm[SATCHEL_MM_MAX];
static uint32_t satchel_mm_next = 1;
static unsigned satchel_fatal_seen;
static void satchel_round11_reset(void) {
    satchel_apm.config[0] = 0x00020003u; satchel_apm.config[1] = 0x00020004u; satchel_apm.overclock = 0;
    memset(satchel_mm, 0, sizeof satchel_mm); satchel_mm_next = 1; satchel_fatal_seen = 0;
}
static int satchel_mm_find(int by_module, uint32_t key) {
    // Ryujinx GetSessionByModule / GetSessionById return the first match in list order.
    int best = -1;
    for (int i = 0; i < SATCHEL_MM_MAX; i++)
        if (satchel_mm[i].used && (by_module ? satchel_mm[i].module : satchel_mm[i].id) == key &&
            (best < 0 || satchel_mm[i].used < satchel_mm[best].used)) best = i;
    return best;
}
static uint32_t satchel_mm_register(uint32_t module, uint32_t *id) {
    static int order;
    for (int i = 0; i < SATCHEL_MM_MAX; i++) if (!satchel_mm[i].used) {
        if (order == INT32_MAX) order = 0;
        satchel_mm[i] = (typeof(satchel_mm[i])){satchel_mm_next++, module, 0, -1, ++order};
        *id = satchel_mm[i].id; return 0;
    }
    return SATCHEL_ROUND11_FULL;
}
static uint32_t satchel_round11_dispatch(const char *service, SatchelIPCRequest *r, uint8_t *out, size_t *n, void *caller) {
    uint32_t cmd = r->command;
    if (!strcmp(service, "apm") || !strcmp(service, "apm:am")) {
        switch (cmd) {
        case 0: r->child = "apm:session"; return 0;
        // The SDK's nn::oe::GetPerformanceMode takeover answers the same mode.
        case 1: satchel_put32(out, satchel_perf_mode()); *n = 4; return 0;
        case 6: out[0] = (uint8_t)satchel_apm.overclock; *n = 1; return 0;
        }
    } else if (!strcmp(service, "apm:session")) {
        switch (cmd) {
        case 0: {
            if (r->args_size < 8) return SATCHEL_IPC_INVALID;
            uint32_t mode = satchel_u32(r->args);
            if (mode > 1) return SATCHEL_APM_INVALID;
            // Recorded for GetPerformanceConfiguration; the host does not scale clocks.
            satchel_apm.config[mode] = satchel_u32(r->args + 4); return 0;
        }
        case 1: {
            if (r->args_size < 4) return SATCHEL_IPC_INVALID;
            uint32_t mode = satchel_u32(r->args);
            if (mode > 1) return SATCHEL_APM_INVALID;
            satchel_put32(out, satchel_apm.config[mode]); *n = 4; return 0;
        }
        case 2:
            if (!r->args_size) return SATCHEL_IPC_INVALID;
            satchel_apm.overclock = !!r->args[0]; return 0;
        }
    } else if (!strcmp(service, "psm")) {
        switch (cmd) {
        // Reference stubs: a full battery on a charger or dock.
        case 0: satchel_ipc_once(service, cmd, "stub", caller); satchel_put32(out, 100); *n = 4; return 0;
        case 1: satchel_ipc_once(service, cmd, "stub", caller); satchel_put32(out, 1); *n = 4; return 0;
        case 7: r->child = "IPsmSession"; return 0;
        }
    } else if (!strcmp(service, "IPsmSession")) {
        if (!r->state) return SATCHEL_IPC_INVALID;
        switch (cmd) {
        // One state-change event per session, never signaled: nothing about power changes.
        case 0: satchel_ipc_once(service, cmd, "stub", caller); return satchel_event_index(r, 0, 0);
        case 1:
            if (r->state->events[0]) { satchel_event_close(r->state->events[0]); r->state->events[0] = 0; }
            satchel_ipc_once(service, cmd, "stub", caller); return 0;
        case 2: case 3: case 4:
            if (!r->args_size) return SATCHEL_IPC_INVALID;
            satchel_ipc_once(service, cmd, "stub", caller); return 0;
        }
    } else if (!strcmp(service, "csrng")) {
        if (cmd == 0) {
            if (!r->receive.present) return SATCHEL_IPC_INVALID;
            uint8_t chunk[256];
            for (uint64_t done = 0; done < r->receive.size; done += sizeof chunk) {
                size_t part = r->receive.size - done < sizeof chunk ? (size_t)(r->receive.size - done) : sizeof chunk;
                SatchelIPCBuffer tail = {r->receive.address + done, part, 1};
                satchel_random(chunk, part);
                if (tail.address < r->receive.address || !satchel_write_buffer(tail, chunk, part)) return SATCHEL_IPC_INVALID;
            }
            return 0;
        }
    } else if (!strcmp(service, "mm:u")) {
        static const size_t need[8] = {12, 4, 12, 4, 12, 4, 12, 4};
        if (cmd > 7) return SATCHEL_IPC_UNKNOWN;
        if (r->args_size < need[cmd]) return SATCHEL_IPC_INVALID;
        uint32_t key = satchel_u32(r->args);
        // Clock requests are recorded, not applied: the host has no multimedia clock control.
        satchel_ipc_once(service, cmd, "stub", caller);
        int at = satchel_mm_find(cmd < 4, key);
        switch (cmd) {
        case 0: case 4: {
            uint32_t id, result = satchel_mm_register(key, &id);
            if (result) return result;
            if (cmd == 4) { satchel_put32(out, id); *n = 4; }
            return 0;
        }
        case 1: case 5: if (at >= 0) memset(&satchel_mm[at], 0, sizeof satchel_mm[at]); return 0;
        case 2: case 6:
            if (at >= 0) { satchel_mm[at].min = satchel_u32(r->args + 4); satchel_mm[at].max = (int32_t)satchel_u32(r->args + 8); }
            return 0;
        case 3: case 7: satchel_put32(out, at >= 0 ? satchel_mm[at].min : 0); *n = 4; return 0;
        }
    } else if (!strcmp(service, "fatal:u")) {
        if (cmd > 2) return SATCHEL_IPC_UNKNOWN;
        if (r->args_size < (cmd ? 12u : 8u)) return SATCHEL_IPC_INVALID;
        // Ryujinx prints an error report and returns success. Only the Result code and
        // policy are kept, never the CPU context buffer, addresses or registers.
        uint32_t code = satchel_u32(r->args), policy = cmd ? satchel_u32(r->args + 8) : 0;
        if (satchel_fatal_seen < 8) {
            satchel_fatal_seen++;
            char kind[48];
            snprintf(kind, sizeof kind, "fatal-%04u-%04u-policy%u", (code & 0x1ffu) + 2000u, (code >> 9) & 0x3fffu, policy);
            satchel_ipc_emit(service, cmd, kind, caller);
        }
        return 0;
    }
    return SATCHEL_IPC_UNKNOWN;
}
