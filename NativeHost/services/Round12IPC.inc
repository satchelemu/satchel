// Round 12 contracts adapted from Ryujinx 1.3.3: the application services that were still open.
// Sources: Horizon Audio/AudioUserIpcServer.cs with Sdk/Audio/Detail/FinalOutputRecorder*.cs and
// Sdk/Sf/RawDataOffsetCalculator.cs (audrec:u), and HLE Services/Hid/IHidbusServer.cs,
// Ldn/Lp2p/IServiceCreator.cs, ISfService.cs and ISfServiceMonitor.cs, Mnpp/IServiceForApplication.cs
// with Mnpp/ResultCode.cs, Nim/Ntc/IStaticService.cs with StaticService/IEnsureNetworkClockAvailabilityService.cs
// and Time/ResultCode.cs. grc:c (Grc/IGrcService.cs), notif:a (Notification/INotificationServicesForApplication.cs)
// and npns:u (Npns/INpnsUser.cs) are registered there without commands: their sessions open and every
// command reaches the missing-command report, as Ryujinx reports it not implemented.
// Copyright (c) Ryujinx Team and Contributors. MIT, see RYUJINX-LICENSE.txt.
#define SATCHEL_MNPP_ARGUMENT ((100u << 9) | 239u)
#define SATCHEL_MNPP_BUFFER_SIZE ((101u << 9) | 239u)
#define SATCHEL_NTC_UNAVAILABLE ((1000u << 9) | 116u)   // Time NetworkTimeNotAvailable
#define SATCHEL_NTC_CANCELED ((1003u << 9) | 116u)      // Time NetworkTimeTaskCanceled
static uint32_t satchel_round12_dispatch(const char *service, SatchelIPCRequest *r, uint8_t *out, size_t *n, void *caller) {
    uint32_t cmd = r->command;
    if (!strcmp(service, "audrec:u")) {
        if (cmd == 0) {
            // OpenFinalOutputRecorder(FinalOutputRecorderParameter at 0, AppletResourceUserId at 8, copy
            // process handle): the internal parameter keeps the rate with two channels and normal frames.
            if (r->args_size < 16) return SATCHEL_IPC_INVALID;
            satchel_put32(out, satchel_u32(r->args)); satchel_put32(out + 4, 2); *n = 16;
            r->child = "FinalOutputRecorder"; return 0;
        }
    } else if (!strcmp(service, "FinalOutputRecorder")) {
        // Every command is a reference stub: nothing is recorded, no buffer is ever released.
        static const size_t need[12] = {0, 0, 0, 8, 0, 0, 8, 8, 8, 0, 0, 16};
        if (cmd > 11) return SATCHEL_IPC_UNKNOWN;
        if (r->args_size < need[cmd]) return SATCHEL_IPC_INVALID;
        satchel_ipc_once(service, cmd, "stub", caller);
        switch (cmd) {
        case 0: *n = 4; return 0;                             // state 0
        case 4: return satchel_event_index(r, 0, 0);          // one buffer event per recorder, never signaled
        case 5: case 9: *n = 16; return 0;                    // count 0 at 0, released 0 at 8
        case 6: case 10: *n = 1; return 0;                    // false
        case 7: *n = 8; return 0;                             // end time 0
        default: return 0;                                    // Start, Stop, the appends and AttachWorkBuffer
        }
    } else if (!strcmp(service, "hidbus")) {
        if (cmd == 1) {
            // GetBusHandle(NpadIdType, padding, BusType, AppletResourceUserId): no rail device, so no
            // handle; the zeroed BusHandle follows the bool and its padding.
            if (r->args_size < 24) return SATCHEL_IPC_INVALID;
            satchel_ipc_once(service, cmd, "stub", caller); *n = 16; return 0;
        }
    } else if (!strcmp(service, "lp2p:app")) {
        // The reference reads no argument here.
        if (cmd == 0) { r->child = "ISfService"; return 0; }
        if (cmd == 8) { r->child = "ISfServiceMonitor"; return 0; }
    } else if (!strcmp(service, "ISfService")) {
        switch (cmd) {
        case 0: *n = 4; return 0;                             // Initialize writes 0
        case 768: case 776: case 1536: case 1544: satchel_ipc_once(service, cmd, "stub", caller); return 0;
        }
    } else if (!strcmp(service, "ISfServiceMonitor")) {
        switch (cmd) {
        case 0: *n = 4; return 0;
        // A new copy of the monitor's own event each time; neither is ever signaled.
        case 256: return satchel_event_index(r, 0, 0);
        case 328: return satchel_event_index(r, 1, 0);
        case 288: case 296: case 312: satchel_ipc_once(service, cmd, "stub", caller); return 0;
        }
    } else if (!strcmp(service, "mnpp:app")) {
        switch (cmd) {
        case 0:   // Initialize(pid): the reference skips the China-only registration
            if (r->args_size < 8) return SATCHEL_IPC_INVALID;
            satchel_ipc_once(service, cmd, "stub", caller); return 0;
        case 1:   // SendRawTelemetryData(Uid, buffer<bytes, 5> title ID): checked, never stored or sent
            if (!r->send.present || r->args_size < 16) return SATCHEL_IPC_INVALID;
            if (!(satchel_u64(r->args) | satchel_u64(r->args + 8))) return SATCHEL_MNPP_ARGUMENT;
            satchel_ipc_once(service, cmd, "stub", caller);
            return r->send.size <= 64 ? 0 : SATCHEL_MNPP_BUFFER_SIZE;
        }
    } else if (!strcmp(service, "ntc")) {
        if (cmd == 0) {   // OpenEnsureNetworkClockAvailabilityService(u64)
            if (r->args_size < 8) return SATCHEL_IPC_INVALID;
            satchel_ipc_once(service, cmd, "stub", caller);
            r->child = "IEnsureNetworkClockAvailabilityService"; return 0;
        }
    } else if (!strcmp(service, "IEnsureNetworkClockAvailabilityService")) {
        if (!r->state) return SATCHEL_IPC_INVALID;
        uint32_t *event = &r->state->events[0];
        switch (cmd) {
        // Ryujinx's branch with internet access off (its default), the only one an offline host has:
        // the task never starts and the finish event stays clear.
        case 0: return SATCHEL_NTC_UNAVAILABLE;
        case 1: return satchel_event_index(r, 0, 0);
        case 2: return r->state->values[0];                   // the task result, Success until canceled
        case 3:
            if (!*event) *event = satchel_event_create(0);
            if (!*event) return (105u << 9) | 1u;
            satchel_event_set(*event, 1);
            r->state->values[0] = SATCHEL_NTC_CANCELED;
            satchel_ipc_once(service, cmd, "stub", caller); return 0;
        }
    }
    return SATCHEL_IPC_UNKNOWN;
}
