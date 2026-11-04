#include "SessionLifecycle.h"
#include <assert.h>
#include <stdio.h>
static int pause_count, resume_count, flush_count, warnings, audio, resets, held, audio_fail;
static void pause_guest(int value) { held = value; if (value) pause_count++; else resume_count++; }
static int audio_guest(int off, int reset) {
    if (!off && audio_fail) { audio = 1; return 0; }
    audio = off; resets += reset; return 1;
}
static void flush(void) { assert(held); flush_count++; }
static void pressure(void) { warnings++; }
int main(void) {
    SatchelLifecycleHooks hooks = {pause_guest, audio_guest, flush, pressure};
    satchel_lifecycle_begin(hooks);
    for (int i = 0; i < 50; i++) {
        satchel_lifecycle_focus(0); assert(held && audio);
        satchel_lifecycle_focus(1); satchel_lifecycle_focus(2); assert(held && audio);
        satchel_lifecycle_focus(3); assert(!held && !audio);
    }
    assert(pause_count == 50 && resume_count == 50 && flush_count == 50);
    satchel_lifecycle_menu(1); satchel_lifecycle_focus(0); satchel_lifecycle_focus(1);
    satchel_lifecycle_focus(2); satchel_lifecycle_focus(3); assert(held && audio);
    satchel_lifecycle_menu(0); assert(!held && !audio);
    satchel_lifecycle_focus(0); satchel_lifecycle_menu(1); satchel_lifecycle_menu(0); assert(held);
    satchel_lifecycle_focus(3); assert(!held);
    satchel_lifecycle_interruption(1, 0); assert(audio && !held);
    satchel_lifecycle_focus(0); satchel_lifecycle_route(); int before = resets;
    satchel_lifecycle_interruption(0, 1); assert(audio && resets == before);
    satchel_lifecycle_focus(3); assert(!audio && resets == before + 1);
    satchel_lifecycle_interruption(1, 0); satchel_lifecycle_interruption(0, 0); assert(audio);
    satchel_lifecycle_route(); assert(audio);
    satchel_lifecycle_focus(3); assert(!audio);
    satchel_lifecycle_route(); assert(!audio && !held);
    audio_fail = 1; satchel_lifecycle_route(); assert(audio);
    satchel_lifecycle_retry_audio(); assert(audio);
    satchel_lifecycle_focus(0); audio_fail = 0; satchel_lifecycle_retry_audio(); assert(audio && held);
    satchel_lifecycle_focus(3); assert(!audio && !held);
    satchel_lifecycle_focus(0); satchel_lifecycle_pressure(); assert(held && warnings == 1);
    satchel_lifecycle_focus(3); satchel_lifecycle_pressure(); assert(!held && warnings == 2);
    int old = pause_count; satchel_lifecycle_end();
    satchel_lifecycle_focus(0); satchel_lifecycle_route(); satchel_lifecycle_interruption(1, 0); satchel_lifecycle_pressure();
    assert(pause_count == old && warnings == 2);
    satchel_lifecycle_begin(hooks); satchel_lifecycle_focus(3); assert(!held && !audio);
    puts("PASS: 50 background cycles, inactive-only return, menu overlap, interruption options, deferred route reset, memory warning and ended session");
}
