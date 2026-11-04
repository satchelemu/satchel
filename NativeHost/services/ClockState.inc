// Clock semantics adapted from Ryujinx 1.3.3, MIT. See RYUJINX-LICENSE.txt.
// SDK and IPC share these session-scoped settings. Host wall time is never changed.
static pthread_mutex_t satchel_time_mu = PTHREAD_MUTEX_INITIALIZER;
static struct { int automatic, made, local; int64_t test, internal; uint8_t updated[24], context[32]; } satchel_time_state;
static void satchel_time_init_locked(void) {
    if (!satchel_time_state.made) { arc4random_buf(satchel_time_state.updated + 8, 16); satchel_time_state.made = 1; }
}
int satchel_host_clock_override(uint8_t *context) {
    pthread_mutex_lock(&satchel_time_mu);
    int set = satchel_time_state.local;
    if (set) memcpy(context, satchel_time_state.context, 32);
    pthread_mutex_unlock(&satchel_time_mu); return set;
}
uint32_t satchel_host_steady_adjust(int64_t *seconds) {
    pthread_mutex_lock(&satchel_time_mu);
    __int128 value = (__int128)*seconds + satchel_time_state.test / 1000000000 + satchel_time_state.internal / 1000000000;
    pthread_mutex_unlock(&satchel_time_mu);
    if (value < INT64_MIN || value > INT64_MAX) return ((201u << 9) | 116u);
    *seconds = (int64_t)value; return 0;
}
uint32_t satchel_host_time_control(unsigned command, const uint8_t *in, uint8_t *out) {
    uint8_t tp[24] = {0};
    if (command == 101) { uint32_t result = h_time_SteadyNow(tp); if (result) return result; }
    pthread_mutex_lock(&satchel_time_mu); satchel_time_init_locked();
    if (command == 100) out[0] = satchel_time_state.automatic;
    else if (command == 101) {
        // The reference records this flag even when the network clock is unset.
        satchel_time_state.automatic = in[0] != 0; memcpy(satchel_time_state.updated, tp, 24);
    } else if (command == 201) memcpy(out, satchel_time_state.updated, 24);
    else if (command == 0) memset(&satchel_time_state, 0, sizeof satchel_time_state);
    pthread_mutex_unlock(&satchel_time_mu); return 0;
}
uint32_t satchel_host_clock_change(unsigned kind, unsigned command, const uint8_t *in, uint8_t *out) {
    if (kind == 0) {
        uint8_t ctx[32];
        if (command == 1) {
            uint32_t result = h_time_SteadyNow(ctx + 8); if (result) return result;
            int64_t now, steady; memcpy(&now, in, 8); memcpy(&steady, ctx + 8, 8);
            __int128 offset = (__int128)now - steady;
            if (offset < INT64_MIN || offset > INT64_MAX) return ((201u << 9) | 116u);
            int64_t value = (int64_t)offset; memcpy(ctx, &value, 8);
        } else memcpy(ctx, in, 32);
        pthread_mutex_lock(&satchel_time_mu);
        memcpy(satchel_time_state.context, ctx, 32); satchel_time_state.local = 1;
        pthread_mutex_unlock(&satchel_time_mu); return 0;
    }
    pthread_mutex_lock(&satchel_time_mu);
    int64_t *value = command == 2 || command == 3 ? &satchel_time_state.test : &satchel_time_state.internal;
    if (command == 3 || command == 201) memcpy(value, in, 8);
    else memcpy(out, value, 8);
    pthread_mutex_unlock(&satchel_time_mu); return 0;
}
