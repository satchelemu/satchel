// Exercise the production queue, silent sink and AudioUnit recreation with
// synthetic silence. Real device output stays suspended for this host probe.
#include "hle_audio.c"
#include <assert.h>
#include <signal.h>
static int16_t samples[48000 * 2];
int main(void) {
    setenv("DM_AUDIO", "0", 1);
    assert(!satchel_session_begin());
    uint64_t clock_before = guest_ns(); guest_clock_freeze_for(1, 1);
    uint64_t frozen = guest_ns(); usleep(30000); assert(guest_ns() == frozen);
    guest_clock_freeze_for(1, 0);
    assert(guest_ns() >= frozen && guest_ns() - clock_before < 10000000);
    uint32_t output[16] = {0}; uint8_t param[8]; h_au_InitParam(param);
    assert(!h_au_OpenDef0(output, param));
    satchel_audio_hold(1); dm_audio_suspend(1);
    gbuf buffer = {samples, sizeof samples, sizeof samples};
    assert(h_au_Append(output, &buffer)); h_au_Start(output);
    aout *a = ao(output); assert(a);
    guest_clock_freeze_for(1, 1); frozen = guest_ns();
    raise(SIGSTOP);
    assert(guest_ns() == frozen && a->pos == 0 && !atomic_load(&a->under_n));
    guest_clock_freeze_for(1, 0);
    for (int cycle = 0; cycle < 10; cycle++) {
        usleep(10000); uint64_t position = a->pos; uint32_t head = atomic_load(&a->q_head);
        dm_audio_reset();
        assert(a->au && a->backend == BK_NONE && !atomic_load(&a->null_run));
        usleep(20000);
        assert(a->pos == position && atomic_load(&a->q_head) == head);
        assert(!atomic_load(&a->under_n));
        satchel_audio_hold(0); usleep(20000); satchel_audio_hold(1); usleep(10000);
        assert(a->pos > position && !atomic_load(&a->under_n));
    }
    satchel_session_request_stop(); assert(!satchel_session_join(2000));
    assert(!satchel_audio_session_reset()); assert(!satchel_session_end());
    puts("PASS: ten AudioUnit route/reset recoveries, one silent consumer, held PCM preserved, zero underruns, clean retirement");
}
