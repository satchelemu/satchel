// Bounded session and domain ownership for Satchel's HIPC services.
// Domain IDs and moved session handles follow Ryujinx IpcService/ServerBase.
// Copyright (c) Ryujinx Team and Contributors. MIT, see RYUJINX-LICENSE.txt.
#define SATCHEL_SF_BASE 0xd0000u
#define SATCHEL_SF_MAX 64
#define SATCHEL_DOMAIN_MAX 32
#define SATCHEL_SF_BAD_HANDLE 0xe401u
#define SATCHEL_SF_FULL ((105u << 9) | 1u)
static struct { unsigned tree; int used; } satchel_sessions[SATCHEL_SF_MAX];
static struct { unsigned refs; int domain; SatchelIPCObject objects[SATCHEL_DOMAIN_MAX]; } satchel_domains[SATCHEL_SF_MAX];
static pthread_mutex_t satchel_session_mu = PTHREAD_MUTEX_INITIALIZER;
static int satchel_session_slot(uint32_t handle) {
    unsigned slot = handle - SATCHEL_SF_BASE;
    return slot < SATCHEL_SF_MAX && satchel_sessions[slot].used ? (int)slot : -1;
}
// Call with satchel_session_mu held. New sessions own a root object; duplicates share its domain.
static uint32_t satchel_session_new(const char *kind, int duplicate, uint32_t *handle) {
    unsigned slot = 0;
    while (slot < SATCHEL_SF_MAX && satchel_sessions[slot].used) slot++;
    if (slot == SATCHEL_SF_MAX) return SATCHEL_SF_FULL;
    unsigned tree = 0;
    if (duplicate >= 0) tree = satchel_sessions[duplicate].tree;
    else {
        while (tree < SATCHEL_SF_MAX && satchel_domains[tree].refs) tree++;
        if (tree == SATCHEL_SF_MAX) return SATCHEL_SF_FULL;
        memset(&satchel_domains[tree], 0, sizeof satchel_domains[tree]);
        satchel_domains[tree].objects[0] = (SatchelIPCObject){.kind = kind, .service = kind, .agreement = 1};
    }
    satchel_domains[tree].refs++;
    satchel_sessions[slot].tree = tree; satchel_sessions[slot].used = 1;
    *handle = SATCHEL_SF_BASE + slot; return 0;
}
static uint32_t satchel_session_connect(uint32_t *out, const char *name, void *caller) {
    static const char *const served[] = {"olsc:u", "set", "acc:u0", "time:u", "time:a", "time:r", "time:s", "hid", "fsp-srv", "nifm:u", "aoc:u", "friend:u", "prepo:u", "appletOE", "appletAE", "vi:u", "audren:u", "pctl",
        "caps:su", "caps:u", "caps:a", "caps:c", "caps:sc", "caps:ss",   // the capture services, as the pinned engine serves them (nxnative df7fa667)
        "apm", "apm:am", "psm", "csrng", "mm:u", "fatal:u"};   // round 11 (Round11IPC.inc)
    for (size_t k = 0; k < sizeof served / sizeof *served; k++) {
        if (!strncmp(name, served[k], 8)) {
            pthread_mutex_lock(&satchel_session_mu);
            uint32_t result = satchel_session_new(served[k], -1, out);
            pthread_mutex_unlock(&satchel_session_mu); return result;
        }
    }
    char service[9] = {0}; memcpy(service, name, strnlen(name, 8));
    satchel_missing_record(service, NULL, UINT32_MAX);
    satchel_ipc_once(service, UINT32_MAX, "missing", caller); return 0xe15u;
}
static void satchel_session_close_locked(int slot) {
    unsigned tree = satchel_sessions[slot].tree;
    memset(&satchel_sessions[slot], 0, sizeof satchel_sessions[slot]);
    if (!--satchel_domains[tree].refs) {
        for (unsigned i = 0; i < SATCHEL_DOMAIN_MAX; i++) satchel_object_clear(&satchel_domains[tree].objects[i]);
        memset(&satchel_domains[tree], 0, sizeof satchel_domains[tree]);
    }
}
static void satchel_session_close(uint32_t handle) {
    pthread_mutex_lock(&satchel_session_mu);
    int slot = satchel_session_slot(handle);
    if (slot >= 0) satchel_session_close_locked(slot);
    pthread_mutex_unlock(&satchel_session_mu);
}
static uint32_t satchel_session_control(uint32_t handle, unsigned command, uint32_t *out) {
    pthread_mutex_lock(&satchel_session_mu);
    int slot = satchel_session_slot(handle); uint32_t result = SATCHEL_SF_BAD_HANDLE;
    if (slot >= 0) {
        result = 0;
        if (command == 0) *out = 0; // No server pointer buffer.
        if (command == 1) { satchel_domains[satchel_sessions[slot].tree].domain = 1; *out = 1; }
        if (command == 2) result = satchel_session_new(NULL, slot, out);
    }
    pthread_mutex_unlock(&satchel_session_mu); return result;
}
static uint32_t satchel_session_send(uint32_t handle, void *message, size_t size, void *caller) {
    pthread_mutex_lock(&satchel_session_mu);
    int slot = satchel_session_slot(handle); uint32_t result = SATCHEL_SF_BAD_HANDLE;
    if (slot < 0) goto done;
    int domain = satchel_domains[satchel_sessions[slot].tree].domain;
    unsigned tree = satchel_sessions[slot].tree;
    SatchelIPCRequest r = {0};
    int parsed = satchel_ipc_parse(message, size, domain, &r);
    result = SATCHEL_IPC_INVALID;
    if (!parsed) goto done;
    unsigned object = domain ? r.object : 1;
    if (!object || object > SATCHEL_DOMAIN_MAX || !satchel_domains[tree].objects[object - 1].kind) goto done;
    if (parsed == 2) {
        satchel_object_clear(&satchel_domains[tree].objects[object - 1]);
        memset(message, 0, 16); result = 0; goto done;
    }
    uint8_t output[64] = {0}; size_t n = 0; uint32_t child = 0;
    r.state = &satchel_domains[tree].objects[object - 1];
    if (r.input_object) {
        if (!domain || r.input_object > SATCHEL_DOMAIN_MAX) goto done;
        r.input = &satchel_domains[tree].objects[r.input_object - 1];
    } else if (r.input_handle) {
        int input_slot = satchel_session_slot(r.input_handle);
        if (domain || input_slot < 0 || satchel_domains[satchel_sessions[input_slot].tree].domain) goto done;
        r.input = &satchel_domains[satchel_sessions[input_slot].tree].objects[0];
    }
    uint32_t command_result = satchel_ipc_dispatch(r.state->kind, &r, output, &n, caller);
    if (!command_result && r.child) {
        if (domain) {
            unsigned index = 1;
            while (index < SATCHEL_DOMAIN_MAX && satchel_domains[tree].objects[index].kind) index++;
            if (index == SATCHEL_DOMAIN_MAX) command_result = SATCHEL_SF_FULL;
            else { satchel_domains[tree].objects[index] = (SatchelIPCObject){.kind = r.child, .service = r.state->service, .agreement = 1}; child = index + 1; }
        } else {
            command_result = satchel_session_new(r.child, -1, &child);
            if (!command_result) satchel_domains[satchel_sessions[child - SATCHEL_SF_BASE].tree].objects[0].service = r.state->service;
        }
    }
    if (child) {
        SatchelIPCObject *created = domain ? &satchel_domains[tree].objects[child - 1] : &satchel_domains[satchel_sessions[child - SATCHEL_SF_BASE].tree].objects[0];
        memcpy(created->values, r.seed.values, sizeof created->values);
        created->storage = r.seed.storage; r.seed.storage = NULL;
    }
    satchel_object_clear(&r.seed);
    result = satchel_ipc_reply(&r, message, size, command_result, output, n, child);
    if (!result && !command_result && r.consume_applet_output) {
        r.state->values[2] = 1;
        if (r.state->events[1]) satchel_event_set(r.state->events[1], 0);
    }
    if (r.input_handle && r.input_handle != handle) {
        int input_slot = satchel_session_slot(r.input_handle);
        if (input_slot >= 0) satchel_session_close_locked(input_slot);
    }
    if (result && child) {
        if (domain) satchel_object_clear(&satchel_domains[tree].objects[child - 1]);
        else satchel_session_close_locked(satchel_session_slot(child));
    }
    if (result || command_result) for (unsigned i = 0; i < r.copy_count; i++) satchel_event_close(r.copies[i]);
done:
    pthread_mutex_unlock(&satchel_session_mu); return result;
}
static void satchel_sessions_reset(void) {
    pthread_mutex_lock(&satchel_session_mu);
    for (unsigned t = 0; t < SATCHEL_SF_MAX; t++)
        for (unsigned i = 0; i < SATCHEL_DOMAIN_MAX; i++) satchel_object_clear(&satchel_domains[t].objects[i]);
    memset(satchel_sessions, 0, sizeof satchel_sessions); memset(satchel_domains, 0, sizeof satchel_domains);
    satchel_ipc_reset();
    pthread_mutex_unlock(&satchel_session_mu);
}
