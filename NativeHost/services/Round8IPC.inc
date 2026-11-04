// Contracts adapted from Ryujinx 1.3.3, MIT. See RYUJINX-LICENSE.txt.
#define SATCHEL_TIME_NOT_IMPLEMENTED ((990u << 9) | 116u)
static uint32_t satchel_round8_dispatch(const char *service, SatchelIPCRequest *r, uint8_t *out, size_t *n, void *caller) {
    unsigned cmd = r->command;
    if (!strcmp(service, "hid")) {
        if (!r->state) return SATCHEL_IPC_INVALID;
        uint32_t *v = r->state->values;
        size_t need;
        switch (cmd) {
        case 60: case 61: case 63: case 64: need = 4; break;
        case 73: need = 24; break;
        case 74: case 75: case 77: case 78: case 84: case 85: need = 16; break;
        case 76: need = 20; break;
        case 92: need = 2; break;
        case 131: need = 12; break;
        case 132: need = 13; break;
        case 134: need = 20; break;
        case 207: need = 20; break;
        case 208: need = 12; break;
        case 209: case 300: case 303: case 309: need = 8; break;
        case 210: case 400: need = 0; break;
        case 301: case 302: case 308: need = 12; break;
        case 401: need = 1; break;
        case 1002: case 1004: need = 16; break;
        default: return SATCHEL_IPC_UNKNOWN;
        }
        if (r->args_size < need) return SATCHEL_IPC_INVALID;
        // These are explicitly marked stubs by the reference. They configure no
        // extra sensor, haptic output or USB device on the host.
        satchel_ipc_once(service, cmd, "stub", caller);
        switch (cmd) {
        case 73: v[0] = satchel_u32(r->args + 8); v[1] = satchel_u32(r->args + 12); break;
        case 74: memcpy(out, v, 8); *n = 8; break;
        case 75: v[0] = v[1] = 0; break;
        case 76: v[2] = satchel_u32(r->args + 8); break;
        case 77: satchel_put32(out, v[2]); *n = 4; break;
        case 78: v[2] = 0; break;
        case 84: v[3] = satchel_u32(r->args) != 0; break;
        case 85: out[0] = v[3]; *n = 1; break;
        case 131: out[0] = v[4]; *n = 1; break;
        case 132: v[4] = r->args[0] != 0; break;
        case 134: v[5] = satchel_u32(r->args + 8) != 0; break;
        case 208: *n = 8; break; // Reference always reports the default GC ERM command.
        case 308: v[6] = satchel_u32(r->args); break;
        case 309: satchel_put32(out, v[6]); *n = 4; break;
        case 400: out[0] = v[7]; *n = 1; break;
        case 401: v[7] = r->args[0] != 0; break;
        }
        return 0;
    }
    if (!strcmp(service, "fsp-srv") && cmd == 8) {
        if (r->args_size < 12) return SATCHEL_IPC_INVALID;
        SatchelIPCBuffer path = r->pointer.present ? r->pointer : r->send;
        char name[0x301];
        if (!path.present || !path.size || path.size > sizeof name || !path.address || path.address > UINT64_MAX - path.size) return SATCHEL_IPC_INVALID;
        if (!satchel_ipc_read(path.address, name, (size_t)path.size) || !memchr(name, 0, (size_t)path.size)) return SATCHEL_IPC_INVALID;
        // Prepared packages expose extracted RomFS through the existing SDK.
        // They contain no guest-addressable NCA/NSP archive filesystem. Match
        // the reference's absent path branch without consulting host paths.
        return 0x202u;
    }
    if (!strcmp(service, "fsp-srv") && (cmd == 51 || cmd == 53)) {
        if (r->args_size < 72) return SATCHEL_IPC_INVALID;
        const uint8_t *a = r->args + 8;
        uint64_t program = satchel_u64(a);
        // Only the current program's account/device save is backed by this host.
        // Attribute layout is LibHac SaveDataAttribute, 0x40 bytes.
        if (r->args[0] != 1 || (program && program != satchel_program_id()) || satchel_u64(a + 24) || a[33] || a[34] || a[35]) return 0x7d402u;
        if (a[32] == 1) { if (memcmp(a + 8, satchel_uid, 16)) return 0x7d402u; }
        else if (a[32] == 3) { if (satchel_u64(a + 8) || satchel_u64(a + 16)) return 0x7d402u; }
        else return 0x7d402u;
        uint32_t index; uint32_t result = satchel_fs_open(a[32], &index);
        if (result) return result;
        r->seed.values[0] = index; r->seed.values[1] = cmd == 53;
        r->child = "IFileSystem"; return 0;
    }
    if (!strcmp(service, "IFileSystem") && (cmd == 11 || cmd == 12 || cmd == 14)) {
        SatchelIPCBuffer path = r->pointer.present ? r->pointer : r->send;
        char name[0x301];
        if (!r->state || !path.present || !path.size || path.size > sizeof name || !path.address || path.address > UINT64_MAX - path.size) return SATCHEL_IPC_INVALID;
        memset(name, 0xff, sizeof name);
        if (!satchel_ipc_read(path.address, name, (size_t)path.size) || !memchr(name, 0, (size_t)path.size)) return SATCHEL_IPC_INVALID;
        if (name[0] != '/' || strchr(name, ':') || strchr(name, '\\')) return ((6001u << 9) | 2u);
        uint32_t result = satchel_fs_query(r->state->values[0], cmd, name, out);
        if (!result) *n = cmd == 14 ? 32 : 8;
        return result;
    }
    int admin = !strcmp(service, "time:a"), repair = !strcmp(service, "time:r");
    int psc = !strcmp(service, "time:s"), user = !strcmp(service, "time:u");
    if (admin || repair || psc || user) {
        switch (cmd) {
        case 0: case 4:
            if (user) return SATCHEL_IPC_UNKNOWN;
            r->child = admin ? (cmd == 0 ? "time:adminuser" : "time:adminlocal") : "time:local";
            return 0;
        case 1: case 5:
            if (user) return SATCHEL_IPC_UNKNOWN;
            r->child = psc ? "time:systemnetwork" : "time:network"; return 0;
        case 2:
            if (user) return SATCHEL_IPC_UNKNOWN;
            r->child = repair ? "time:repairsteady" : "time:steady"; return 0;
        case 3:
            if (user) return SATCHEL_IPC_UNKNOWN; // Existing round 7 factory.
            r->child = psc ? "ITimeZoneServiceForPsc" : "ITimeZoneServiceForGlue";
            r->seed.values[15] = admin;
            return 0;
        case 50:
            if (user) return SATCHEL_IPC_UNKNOWN;
            if (psc) return SATCHEL_TIME_NOT_IMPLEMENTED;
            if (!repair) return SATCHEL_TIME_PERMISSION;
            if (r->args_size < 8) return SATCHEL_IPC_INVALID;
            satchel_ipc_once(service, cmd, "stub", caller); return 0; // Reference TODO.
        case 51:
            if (psc) return SATCHEL_TIME_NOT_IMPLEMENTED;
            { int64_t now = satchel_clock_rtc(); memcpy(out, &now, 8); *n = 8; return 0; }
        case 100: *n = 1; return satchel_time_control(100, NULL, out);
        case 101:
            if (!admin) return SATCHEL_TIME_PERMISSION;
            if (r->args_size < 1) return SATCHEL_IPC_INVALID;
            return satchel_time_control(101, r->args, NULL);
        case 102:
            if (psc) return SATCHEL_TIME_NOT_IMPLEMENTED;
            satchel_put32(out, 2019); *n = 4; return 0;
        case 201: *n = 24; return satchel_time_control(201, NULL, out);
        }
    }
    if (!strcmp(service, "time:adminuser") || !strcmp(service, "time:adminlocal")) {
        if (cmd == 1 || cmd == 3) {
            if (r->args_size < (cmd == 1 ? 8u : 32u)) return SATCHEL_IPC_INVALID;
            if (!strcmp(service, "time:adminuser")) return SATCHEL_TIME_NOT_IMPLEMENTED;
            return satchel_clock_change(0, cmd, r->args, NULL);
        }
        if (cmd == 0) { int64_t now; uint32_t result = satchel_clock_now(&now); if (!result) { memcpy(out, &now, 8); *n = 8; } return result; }
        if (cmd == 2) { *n = 32; return satchel_clock_context(out); }
    }
    if (!strcmp(service, "time:systemnetwork") && cmd <= 3) {
        // The network clock is uninitialized. Writable system access cannot
        // bypass initialization, and no external time is invented.
        return SATCHEL_TIME_UNINITIALIZED;
    }
    if (!strcmp(service, "time:repairsteady") || !strcmp(service, "time:steady")) {
        int writable = !strcmp(service, "time:repairsteady");
        if (cmd == 2 || cmd == 200) { *n = 8; return satchel_clock_change(1, cmd, NULL, out); }
        if (cmd == 3 || cmd == 201) {
            if (!writable) return SATCHEL_TIME_PERMISSION;
            if (r->args_size < 8) return SATCHEL_IPC_INVALID;
            return satchel_clock_change(1, cmd, r->args, NULL);
        }
        if (writable) {
            if (cmd == 0) { *n = 24; return satchel_clock_steady(out); }
            if (cmd == 101) { *n = 1; return 0; }
            if (cmd == 102) { *n = 4; return 0; }
        }
    }
    if (!strcmp(service, "ITimeZoneServiceForPsc")) {
        if (cmd == 1) return SATCHEL_TIME_PERMISSION;
        if (cmd == 2) { *n = 4; return 0; }
        if (cmd == 3) return SATCHEL_TIME_NOT_IMPLEMENTED;
    }
    if (!strcmp(service, "ITimeZoneServiceForGlue") && cmd == 1 && r->state && r->state->values[15]) {
        if (r->args_size < 36) return SATCHEL_IPC_INVALID;
        // No timezone archive exists. The content manager refuses a location
        // without a rule file, including UTC in its fallback name catalogue.
        return ((989u << 9) | 116u);
    }
    return SATCHEL_IPC_UNKNOWN;
}
