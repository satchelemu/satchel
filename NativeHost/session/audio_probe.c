#include "hle.h"
#include <AudioToolbox/AudioToolbox.h>
static OSStatus fixture_stop(AudioUnit unit) {
    return unit == (AudioUnit)(uintptr_t)1 ? -50 : AudioOutputUnitStop(unit);
}
#define AudioOutputUnitStop fixture_stop
#undef HLE_FORCE
#undef HLE_FORCE_BIG
#define HLE_FORCE(mangled, why, ret, cname, ...) static ret cname(__VA_ARGS__)
#define HLE_FORCE_BIG HLE_FORCE
#include "hle_audio.c"
#include "media_probe.h"
#include <assert.h>

static uint32_t output[16];
static int16_t pcm[4800 * 2];
static gbuf buffer;
void media_audio_start(void) {
    setenv("DM_AUDIO", "0", 1);
    uint8_t param[8]; h_au_InitParam(param);
    assert(!h_au_OpenDef0(output, param));
    buffer = (gbuf){pcm, sizeof pcm, sizeof pcm};
    assert(h_au_Append(output, &buffer)); h_au_Start(output);
    // Exercise the actual watchdog owner, without opening a sound device.
    au_watchdog_start();
    assert(session_watchdog_started && ao(output)->backend == BK_NULL);
}
void media_audio_verify(void) {
    aout *a = ao(output); a->au = (AudioUnit)(uintptr_t)1;
    assert(satchel_audio_session_reset() == EIO);
    assert(objmap_get(output) == a && atomic_load(&a->null_run) && atomic_load(&a->notify_run));
    a->au = NULL; // Synthetic driver recovery, followed by the real silent-sink close.
    assert(!satchel_audio_session_reset());
    assert(!session_watchdog_started && !objmap_get(output));
    for (int i = 0; i < AOMAX; ++i) assert(!g_aos[i]);
}
