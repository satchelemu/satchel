#include "SessionLifecycle.h"
static SatchelLifecycleHooks hooks;
static int live, inactive, menu, interrupted, audio_denied, paused, audio_off, reset;
static void apply(void) {
    if (!live) return;
    int next = inactive || menu;
    int off = next || interrupted || audio_denied;
    // Silence first. Drain graphics before publishing a paused application.
    if (off && !audio_off) { hooks.audio(1, 0); audio_off = 1; }
    if (next != paused) {
        hooks.pause(next); paused = next;
        if (next) hooks.flush();
    }
    if (!off && (audio_off || reset)) {
        if (hooks.audio(0, reset)) { audio_off = 0; reset = 0; }
        else audio_off = 1;
    }
}
void satchel_lifecycle_begin(SatchelLifecycleHooks value) {
    hooks = value; live = 1;
    inactive = menu = interrupted = audio_denied = paused = audio_off = reset = 0;
}
void satchel_lifecycle_end(void) { live = 0; }
void satchel_lifecycle_focus(int event) {
    if (!live) return;
    if (event == 0 || event == 1) inactive = 1;
    // Foreground is still inactive. Only didBecomeActive releases the gate.
    if (event == 3) { inactive = 0; audio_denied = 0; }
    apply();
}
void satchel_lifecycle_menu(int value) { menu = value != 0; apply(); }
void satchel_lifecycle_interruption(int began, int should_resume) {
    if (!live) return;
    interrupted = began != 0;
    if (!began) { audio_denied = !should_resume; reset = 1; }
    apply();
}
void satchel_lifecycle_route(void) { if (live) { reset = 1; apply(); } }
void satchel_lifecycle_retry_audio(void) { apply(); }
void satchel_lifecycle_pressure(void) { if (live) hooks.pressure(); }
