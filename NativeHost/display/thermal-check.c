// Exercises the actual engine thermal functions, extracted verbatim by thermal-check.py.
// Synthetic clock, heat and movie state only. No game, GPU or device actions.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#include <assert.h>
static uint8_t state[16];
static uint8_t *dm_fps_state = state;
static uint64_t clock_ns;
static int heat, movie;
static uint64_t now_ns(void) { return clock_ns; }
static int dm_thermal_now(void) { return heat; }
static int dm_movie_playing(void) { return movie; }
#define LOG(...) ((void)0)
#include "thermal-production.h"
static int checks;
static void target(float fps) { memcpy(state, &fps, sizeof fps); }
static void check(int second, int temperature, int cap, int limited) {
    clock_ns = (uint64_t)second * 1000000000ull; heat = temperature;
    assert(fps_step_cap() == cap);
    assert(nvn_thermal_limited() == limited);
    checks++;
}
int main(void) {
    target(60);
    if (getenv("DM_THERMAL_CAP") && !strcmp(getenv("DM_THERMAL_CAP"), "0")) {
        check(1, 3, 60, 0);
        movie = 1; check(2, 3, 30, 0);
    } else {
        check(1, 0, 60, 0);
        movie = 1; check(2, 0, 30, 0); // A movie alone is not thermal throttling.
        movie = 0; check(3, 2, 30, 1);
        check(4, 1, 30, 1); check(33, 0, 30, 1);
        check(34, 1, 60, 0); // Indicator follows the entire 30-second recovery.
        check(35, 3, 30, 1);
        target(30); check(36, 3, 30, 0); // Graphics was already capped at 30.
        target(60); check(37, 2, 30, 1);
        target(0); check(38, 2, 0, 0);
        dm_fps_state = NULL; check(39, 2, 0, 0);
    }
    printf("PASS: %d production thermal cap and indicator checks\n", checks);
}
