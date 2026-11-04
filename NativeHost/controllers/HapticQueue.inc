// Bound queued guest vibration while preserving normal pulse edges. A flood
// retains the latest state of each player after the first 128 pending changes.
#define SATCHEL_RUMBLE_PENDING 128
static pthread_mutex_t satchel_rumble_lock = PTHREAD_MUTEX_INITIALIZER;
static dm_haptic_state satchel_rumble_pending[SATCHEL_RUMBLE_PENDING], satchel_rumble_latest[DM_PLAYERS];
static unsigned satchel_rumble_head, satchel_rumble_count, satchel_rumble_overflow;
static int satchel_rumble_scheduled;
static void satchel_rumble_drain(void) {
    // Yield after a bounded batch so a disconnect or suspend can stop hardware.
    for (unsigned i = 0; i < SATCHEL_RUMBLE_PENDING; i++) {
        dm_haptic_state state;
        pthread_mutex_lock(&satchel_rumble_lock);
        if (satchel_rumble_count) {
            state = satchel_rumble_pending[satchel_rumble_head];
            satchel_rumble_head = (satchel_rumble_head + 1) % SATCHEL_RUMBLE_PENDING; satchel_rumble_count--;
        } else if (satchel_rumble_overflow) {
            int player = __builtin_ctz(satchel_rumble_overflow);
            state = satchel_rumble_latest[player]; satchel_rumble_overflow &= ~(1u << player);
        } else {
            satchel_rumble_scheduled = 0; pthread_mutex_unlock(&satchel_rumble_lock); return;
        }
        pthread_mutex_unlock(&satchel_rumble_lock);
        @autoreleasepool { satchel_rumble_apply_one(state); }
    }
    dispatch_async(queue, ^{ satchel_rumble_drain(); });
}
static void apply(dm_haptic_state state) {
    pthread_mutex_lock(&satchel_rumble_lock);
    if (satchel_rumble_count == SATCHEL_RUMBLE_PENDING || satchel_rumble_overflow) {
        satchel_rumble_latest[state.player] = state; satchel_rumble_overflow |= 1u << state.player;
    } else {
        satchel_rumble_pending[(satchel_rumble_head + satchel_rumble_count) % SATCHEL_RUMBLE_PENDING] = state;
        satchel_rumble_count++;
    }
    if (!satchel_rumble_scheduled) {
        satchel_rumble_scheduled = 1; dispatch_async(queue, ^{ satchel_rumble_drain(); });
    }
    pthread_mutex_unlock(&satchel_rumble_lock);
}
