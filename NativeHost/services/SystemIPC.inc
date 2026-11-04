// Applet, device and parental-control contracts adapted from Ryujinx 1.3.3.
// Copyright (c) Ryujinx Team and Contributors. MIT, see RYUJINX-LICENSE.txt.
#define SATCHEL_AM_UNAVAILABLE ((2u << 9) | 128u)
#define SATCHEL_AM_OBJECT ((500u << 9) | 128u)
#define SATCHEL_AM_BOUNDS ((503u << 9) | 128u)
static uint32_t satchel_storage_child(SatchelIPCRequest *r, const char *kind, size_t size) {
    if (!size || size > 65536) return SATCHEL_AM_OBJECT;
    SatchelIPCStorage *s = calloc(1, sizeof *s + size);
    if (!s) return ((600u << 9) | 128u);
    s->refs = 1; s->size = size; r->seed.storage = s; r->child = kind; return 0;
}
static uint32_t satchel_event_index(SatchelIPCRequest *r, unsigned index, int signaled) {
    if (!r->state || index >= 5) return SATCHEL_IPC_INVALID;
    uint32_t *event = &r->state->events[index];
    if (!*event) *event = satchel_event_create(signaled);
    if (!*event) return ((105u << 9) | 1u);
    uint32_t copy = satchel_event_copy(*event);
    if (!copy) return ((105u << 9) | 1u);
    r->copies[r->copy_count++] = copy; return 0;
}
static void satchel_applet_finish(SatchelIPCObject *o) {
    o->values[1] = 1;
    if (o->events[0]) satchel_event_set(o->events[0], 1);
    if (o->values[0] == 0x11 && !o->values[2] && o->events[1]) satchel_event_set(o->events[1], 1);
}
static uint32_t satchel_system_dispatch(const char *service, SatchelIPCRequest *r, uint8_t *out, size_t *n, void *caller) {
    uint32_t cmd = r->command;
    SatchelIPCObject *o = r->state;
    if (!strcmp(service, "appletOE")) {
        if (cmd == 0) { r->child = "IApplicationProxy"; return 0; }
    } else if (!strcmp(service, "appletAE")) {
        if (cmd == 200 || cmd == 201) { r->child = "ILibraryAppletProxy"; return 0; }
    } else if (!strcmp(service, "IApplicationProxy") || !strcmp(service, "ILibraryAppletProxy")) {
        if (cmd == 0) { r->child = "ICommonStateGetter"; return 0; }
        if (cmd == 1) { r->child = "ISelfController"; return 0; }
        if (cmd == 11) { r->child = "ILibraryAppletCreator"; return 0; }
        if (cmd == 20 && !strcmp(service, "IApplicationProxy")) { r->child = "IApplicationFunctions"; return 0; }
    } else if (!strcmp(service, "IApplicationFunctions") && o) {
        switch (cmd) {
        case 1: {
            if (r->args_size < 4) return SATCHEL_IPC_INVALID;
            uint32_t kind = satchel_u32(r->args);
            if (kind == 1) return SATCHEL_AM_UNAVAILABLE; // No previous program's user channel.
            if (kind != 2) return SATCHEL_AM_OBJECT;
            uint32_t result = satchel_storage_child(r, "IStorage", 0x88);
            if (result) return result;
            satchel_put32(r->seed.storage->bytes, 0xc79497ca); satchel_put32(r->seed.storage->bytes + 4, 1);
            memcpy(r->seed.storage->bytes + 8, satchel_uid, 16); return 0;
        }
        case 22:
            if (r->args_size < 4) return SATCHEL_IPC_INVALID;
            o->values[0] = satchel_u32(r->args); return 0;
        case 40: out[0] = 1; *n = 1; return 0;
        case 60: case 90:
            if (!r->args_size) return SATCHEL_IPC_INVALID;
            o->values[cmd == 60 ? 2 : 4] = !!r->args[0];
            satchel_ipc_once(service, cmd, "stub", caller); return 0;
        case 65: *n = 1; return 0; // System video capture is unavailable.
        case 121: return 0; // The user channel is empty throughout this single-program session.
        case 130: case 140: case 150: case 160: case 210:
            return satchel_event_index(r, cmd == 130 ? 0 : cmd == 140 ? 1 : cmd == 150 ? 2 : cmd == 160 ? 3 : 4, 0);
        case 141: return SATCHEL_AM_UNAVAILABLE;
        }
    } else if (!strcmp(service, "ISelfController") && o) {
        switch (cmd) {
        case 1: case 2: satchel_ipc_once(service, cmd, "stub", caller); o->values[0] = cmd == 1; return 0;
        case 3:
            if (o->values[1] == UINT32_MAX) return SATCHEL_IPC_INVALID;
            o->values[1]++; return 0;
        case 4:
            if (!o->values[1]) return ((512u << 9) | 128u);
            o->values[1]--; return 0;
        case 9: return satchel_event_index(r, 0, 1);
        case 14: case 50: {
            if (r->args_size < 1) return SATCHEL_IPC_INVALID;
            const unsigned ids[] = {14,50};
            for (unsigned i = 0; i < 2; i++) if (cmd == ids[i]) o->values[3 + i] = !!r->args[0];
            satchel_ipc_once(service, cmd, "stub", caller); return 0;
        }
        case 65: satchel_ipc_once(service, cmd, "stub", caller); return 0;
        case 90: *n = 8; satchel_ipc_once(service, cmd, "stub", caller); return 0;
        case 91: return satchel_event_index(r, 1, 1);
        }
    } else if (!strcmp(service, "ICommonStateGetter") && o) {
        switch (cmd) {
        case 8: *n = 1; satchel_ipc_once(service, cmd, "stub", caller); return 0;
        case 10: satchel_ipc_once(service, cmd, "stub", caller); return 0;
        case 13: return satchel_event_index(r, 1, 0);
        case 50: out[0] = !!o->values[0]; *n = 1; return 0;
        case 51: case 52:
            if (!r->args_size) return SATCHEL_IPC_INVALID;
            o->values[cmd == 51 ? 0 : 1] = !!r->args[0];
            satchel_ipc_once(service, cmd, "stub", caller); return 0;
        case 53: case 54:
            o->values[0] = cmd == 53; satchel_ipc_once(service, cmd, "stub", caller); return 0;
        case 62: *n = 4; satchel_ipc_once(service, cmd, "stub", caller); return 0;
        case 300: *n = 1; return 0; // The host's settings policy uses the global platform.
        case 900: o->values[2] = 1; return 0;
        }
    } else if (!strcmp(service, "ILibraryAppletCreator")) {
        if (cmd == 0 || cmd == 3) {
            if (r->args_size < (cmd == 0 ? 8u : 16u)) return SATCHEL_IPC_INVALID;
            uint32_t id = satchel_u32(r->args), mode = satchel_u32(r->args + 4);
            // Only foreground keyboard cancellation and error dismissal are supported.
            // Inline keyboard and other applets retain an explicit launch failure.
            if ((id != 0xe && id != 0x11) || mode != 0) return ((35u << 9) | 128u);
            r->child = "ILibraryAppletAccessor"; r->seed.values[0] = id;
            satchel_ipc_once(service, cmd, "stub", caller); return 0;
        }
        if (cmd == 10) {
            if (r->args_size < 8) return SATCHEL_IPC_INVALID;
            uint64_t size = satchel_u64(r->args);
            if (size > 65536) return SATCHEL_AM_OBJECT;
            return satchel_storage_child(r, "IStorage", (size_t)size);
        }
    } else if (!strcmp(service, "IStorage") && o && o->storage) {
        if (cmd == 0) {
            r->child = "IStorageAccessor"; r->seed.storage = o->storage; o->storage->refs++; return 0;
        }
    } else if (!strcmp(service, "IStorageAccessor") && o && o->storage) {
        if (cmd == 0) { uint64_t size = o->storage->size; memcpy(out, &size, 8); *n = 8; return 0; }
        if (cmd == 10 || cmd == 11) {
            if (r->args_size < 8) return SATCHEL_IPC_INVALID;
            uint64_t offset = satchel_u64(r->args);
            if (offset > o->storage->size) return SATCHEL_AM_BOUNDS;
            SatchelIPCBuffer b = cmd == 10 ? (r->pointer.address ? r->pointer : r->send) : (r->list.address ? r->list : r->receive);
            if (!b.present) return SATCHEL_IPC_INVALID;
            size_t count = o->storage->size - offset;
            if (b.size < count) count = (size_t)b.size;
            if (!count) return 0;
            if (b.address > UINT64_MAX - count) return SATCHEL_IPC_INVALID;
            return (cmd == 10 ? satchel_ipc_read(b.address, o->storage->bytes + offset, count) :
                    satchel_write_buffer(b, o->storage->bytes + offset, count)) ? 0 : SATCHEL_IPC_INVALID;
        }
    } else if (!strcmp(service, "ILibraryAppletAccessor") && o) {
        switch (cmd) {
        case 0: return satchel_event_index(r, 0, !!o->values[1]);
        case 10: case 20:
            satchel_applet_finish(o); satchel_ipc_once(service, cmd, "stub", caller); return 0;
        case 30: return o->values[1] ? 0 : SATCHEL_AM_UNAVAILABLE;
        case 60: satchel_ipc_once(service, cmd, "stub", caller); return 0;
        case 100: case 103:
            if (!r->input || !r->input->kind || strcmp(r->input->kind, "IStorage")) return SATCHEL_AM_OBJECT;
            // Cancellation does not inspect the keyboard text or the error payload.
            satchel_ipc_once(service, cmd, "stub", caller); return 0;
        case 101: {
            if (o->values[0] != 0x11 || !o->values[1] || o->values[2]) return SATCHEL_AM_UNAVAILABLE;
            uint32_t result = satchel_storage_child(r, "IStorage", 0x7d8);
            if (result) return result;
            satchel_put32(r->seed.storage->bytes, 1); // Ryujinx foreground KeyboardResult.Cancel.
            r->consume_applet_output = 1;
            return 0;
        }
        case 104: return SATCHEL_AM_UNAVAILABLE;
        case 105: return satchel_event_index(r, 1, o->values[0] == 0x11 && o->values[1] && !o->values[2]);
        case 106: return satchel_event_index(r, 2, 0);
        case 110: case 150: return ((999u << 9) | 128u);
        }
    } else if (!strcmp(service, "vi:u")) {
        if (cmd == 0) {
            if (r->args_size < 4) return SATCHEL_IPC_INVALID;
            if (satchel_u32(r->args)) return ((5u << 9) | 114u);
            r->child = "IApplicationDisplayService"; return 0;
        }
    } else if (!strcmp(service, "IApplicationDisplayService") && o) {
        switch (cmd) {
        case 1101:
            if (r->args_size < 16) return SATCHEL_IPC_INVALID;
            satchel_ipc_once(service, cmd, "stub", caller); return 0;
        case 1102:
            if (r->args_size < 8) return SATCHEL_IPC_INVALID;
            { uint64_t size[] = {1280,720}; memcpy(out, size, 16); *n = 16; }
            satchel_ipc_once(service, cmd, "stub", caller); return 0;
        case 2102:
            if (r->args_size < 4) return SATCHEL_IPC_INVALID;
            { uint32_t mode = satchel_u32(r->args);
              if (mode > 4) return ((1u << 9) | 114u);
              if (mode != 2 && mode != 4) return ((6u << 9) | 114u);
              uint64_t converted = mode == 2 ? 1 : 4; memcpy(out, &converted, 8); *n = 8; }
            return 0;
        }
    } else if (!strcmp(service, "audren:u")) {
        if (cmd == 2 || cmd == 4) {
            if (r->args_size < (cmd == 2 ? 8u : 16u)) return SATCHEL_IPC_INVALID;
            r->child = "AudioDevice"; return 0;
        }
    } else if (!strcmp(service, "AudioDevice")) {
        // These newer commands are explicit failures in the pinned MIT reference.
        if (cmd >= 15 && cmd <= 20) {
            if (r->args_size < (cmd < 19 ? 8u : cmd == 19 ? 1u : 0u)) return SATCHEL_IPC_INVALID;
            return ((513u << 9) | 153u);
        }
    } else if (!strcmp(service, "pctl")) {
        if (cmd == 0 || cmd == 1) { r->child = "IParentalControlService"; return 0; }
    } else if (!strcmp(service, "IParentalControlService")) {
        if (cmd == 1013 || cmd == 1064) return 0;
        if (cmd == 1031) { *n = 1; return 0; }
        if (cmd == 1065) { out[0] = 1; *n = 1; return 0; }
    } else if (!strcmp(service, "time:u")) {
        if (cmd == 3) { r->child = "ITimeZoneServiceForGlue"; return 0; }
    } else if (!strcmp(service, "ITimeZoneServiceForGlue")) {
        if (cmd == 1) return SATCHEL_TIME_PERMISSION;
        // No firmware timezone catalogue is installed. Match the reference fallback:
        // its initialized manager has count zero, while the glue cache contains UTC.
        if (cmd == 2) { *n = 4; return 0; }
        if (cmd == 3) {
            if (r->args_size < 4 || !r->receive.present) return SATCHEL_IPC_INVALID;
            if (satchel_u32(r->args) == 0 && r->receive.size >= 36) {
                char name[36] = "UTC";
                if (!satchel_write_buffer(r->receive, name, sizeof name)) return SATCHEL_IPC_INVALID;
                satchel_put32(out, 1);
            }
            *n = 4; return 0;
        }
    }
    return SATCHEL_IPC_UNKNOWN;
}
