static int events[DM_PLAYERS + 1];
static dm_haptic_state rumble[DM_PLAYERS];
static void sink(dm_haptic_state s) { rumble[s.player] = s; }
int main(void) {
    pad_slots slots = {0}; int xbox = 0, dualsense = 0, pro = 0;
    CHECK(pad_slot_take(&slots, &xbox, "Xbox") == 0, "first pad is player 1");
    CHECK(pad_slot_take(&slots, &dualsense, "DualSense") == 1, "second pad is player 2");
    CHECK(pad_slot_free(&slots, &dualsense) == 1, "disconnect frees player 2");
    CHECK(pad_slot_take(&slots, &pro, "Pro Controller") == 1, "replacement second pad remains player 2");
    CHECK(pad_slot_of(&slots, &xbox) == 0, "player 1 stays stable");
    uint32_t hh = 0x20, p1 = 0, p2 = 1, supported[] = {0, 1, 0x20};
    dm_haptics_init(sink, 1);
    dm_haptics_route(0, 1); dm_haptics_route(1, 1);
    h_hid_SetIds(supported, 3); h_hid_SetStyles(STYLE_FULLKEY | STYLE_HANDHELD);
    satchel_hid_controllers(0);
    CHECK(h_hid_GetStyle(&hh) == STYLE_HANDHELD && !h_hid_GetStyle(&p1), "touch uses Handheld even with a docked render profile");
    h_hid_BindStyleEv(&hh, &events[8], 0);
    h_hid_BindStyleEv(&p1, &events[0], 0);
    h_hid_BindStyleEv(&p2, &events[1], 0);
    uint32_t phone[2], pad[2], second[2];
    CHECK(h_hid_VibHandles(phone, 2, &hh, STYLE_HANDHELD) == 2, "phone gets two vibration handles");
    dm_vibration v = {0.5f, 160, 0.25f, 320};
    h_hid_SendVib(&phone[0], &v);
    CHECK(rumble[0].motor[0].amplitude_low == .5f, "phone receives game vibration");
    int signals = g_signals;
    satchel_hid_controllers(1);
    CHECK(g_signals == signals + 2, "Handheld and No1 style events fire on connect");
    CHECK(!h_hid_GetStyle(&hh) && h_hid_GetStyle(&p1) == STYLE_FULLKEY, "controller moves player 1 to FullKey");
    CHECK(dm_haptics_actual(0, 0).amplitude_low == 0, "style change clears prior rumble");
    h_hid_SendVib(&phone[0], &v);
    CHECK(dm_haptics_actual(0, 0).amplitude_low == 0, "stale handheld handle cannot vibrate controller");
    CHECK(h_hid_VibHandles(pad, 2, &p1, STYLE_FULLKEY) == 2, "FullKey handles");
    satchel_hid_controllers(3);
    CHECK(h_hid_GetStyle(&p2) == STYLE_FULLKEY, "second controller reports player 2");
    dm_pad_set_player(0, DM_BTN_A, 100, 0, 0, 0);
    dm_pad_set_player(1, DM_BTN_B, -100, 0, 0, 0);
    CHECK(buttons_of(0, STYLE_FULLKEY) == DM_BTN_A && buttons_of(1, STYLE_FULLKEY) == DM_BTN_B, "players have independent input");
    // Keep ordinary short taps, but discard the pending first chord edge at takeover.
    dm_pad_set_player(0, DM_BTN_PLUS, 0, 0, 0, 0);
    dm_pad_set_player(0, 0, 0, 0, 0, 0);
    CHECK(buttons_of(0, STYLE_FULLKEY) == DM_BTN_PLUS, "short Plus reaches game through latch");
    dm_pad_set_player(0, DM_BTN_MINUS, 0, 0, 0, 0);
    dm_pad_set_player(0, 0, 0, 0, 0, 0);
    CHECK(buttons_of(0, STYLE_FULLKEY) == DM_BTN_MINUS, "short Minus reaches game through latch");
    for (int first = 0; first < 2; first++) {
        dm_pad_set_player(0, (first ? DM_BTN_MINUS : DM_BTN_PLUS) | DM_BTN_A, 0, 0, 0, 0);
        satchel_hid_suppress_player(0, DM_BTN_PLUS | DM_BTN_MINUS);
        dm_pad_set_player(0, DM_BTN_A, 0, 0, 0, 0);
        CHECK(buttons_of(0, STYLE_FULLKEY) == DM_BTN_A, "chord takeover clears pending edge and keeps unrelated input");
        CHECK(buttons_of(0, STYLE_FULLKEY) == DM_BTN_A, "held chord remains suppressed");
        CHECK(buttons_of(1, STYLE_FULLKEY) == DM_BTN_B, "chord takeover does not affect another player");
    }
    int32_t info[2]; int8_t arg[2] = {2, 2};
    CHECK(!ctrl_support(info, arg) && info[0] == 2, "controller applet reports two players");
    CHECK(h_hid_VibHandles(second, 2, &p2, STYLE_FULLKEY) == 2, "player 2 vibration handles");
    uint32_t handles[] = {pad[0], pad[1], second[0], second[1]};
    dm_vibration values[] = {{.1f,160,0,320}, {.2f,160,0,320}, {.3f,160,0,320}, {.4f,160,0,320}};
    CHECK(!h_hid_SendVibs(handles, values, 4), "batch succeeds");
    CHECK(rumble[0].motor[1].amplitude_low == .2f && rumble[1].motor[1].amplitude_low == .4f, "batch routes independently");
    dm_vibration hostile = {INFINITY, NAN, 9, -20}; h_hid_SendVib(&pad[0], &hostile);
    dm_vibration actual; h_hid_VibActual(&actual, &pad[0]);
    CHECK(actual.amplitude_low == 0 && actual.amplitude_high == 1 && actual.frequency_low == 160 && actual.frequency_high == 320, "nonfinite and excessive signals bounded");
    uint32_t invalid[] = {0xff000003, 0x020003, 0x0303, 0xff, 0x2003};
    for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; i++) CHECK(!h_hid_VibMounted(&invalid[i]), "invalid handle not mounted");
    for (unsigned reason = 1; reason <= 16; reason *= 2) {
        dm_haptics_suspend(reason, 1); h_hid_SendVibs(handles, values, 4);
        CHECK(!dm_haptics_actual(0, 0).amplitude_low && !dm_haptics_actual(1, 1).amplitude_low, "pause, quit, setting and background silence all players");
        dm_haptics_suspend(reason, 0);
        CHECK(!dm_haptics_actual(0, 0).amplitude_low, "resume needs a fresh send");
    }
    dm_pad_set_player(0, DM_BTN_X, 100, 0, 0, 0);
    satchel_hid_controllers(2);
    CHECK(h_hid_GetStyle(&hh) == STYLE_HANDHELD && h_hid_GetStyle(&p2) == STYLE_FULLKEY, "player 1 disconnect retains player 2 and touch player 1");
    CHECK(!buttons_of(hh, STYLE_HANDHELD), "disconnect discards press latch and held input");
    satchel_hid_controllers(3);
    CHECK(!buttons_of(0, STYLE_FULLKEY), "reconnect has no stale press");
    dm_haptics_route(1, 0); h_hid_SendVib(&second[0], &v);
    CHECK(!h_hid_VibMounted(&second[0]) && !dm_haptics_actual(1, 0).amplitude_low, "controller without haptics is harmless");
    h_hid_SetIds(&p1, 1); satchel_hid_controllers(0);
    CHECK(h_hid_GetStyle(&p1) == STYLE_FULLKEY && !h_hid_GetStyle(&hh), "touch fallback for games supporting only No1");
    h_hid_SetStyles(STYLE_HANDHELD);
    CHECK(!h_hid_GetStyle(&p1), "unsupported style not reported");
    h_hid_SetIds(supported, 3);
    CHECK(h_hid_GetStyle(&hh) == STYLE_HANDHELD, "ID change reselects supported handheld");
    dm_input_set(DM_BTN_Y, 500, 0, 0, 0); satchel_hid_release();
    CHECK(!buttons_of(hh, STYLE_HANDHELD), "pause clears touch latch");
    printf("controllers: %d checks, %d failures\n", checks, fails);
    return fails != 0;
}
