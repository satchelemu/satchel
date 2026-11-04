// Actual decoder queues and worker lifetime, with a deliberately blocked codec
// adapter and synthetic guest event callback. No compressed game media is used.
#include "hle.h"
#undef HLE_FORCE
#define HLE_FORCE(mangled, why, ret, cname, ...) static ret cname(__VA_ARGS__)
#include "hle_movie.c"
#include "media_probe.h"
#include <assert.h>

extern void fixture_spin(void *);
static media_fixture event;
static void *events[4], *decoder[2];
static _Atomic int codec_entered, codec_release, job_cleaned;
struct mavc { int unused; };
mavc *mavc_new(void) { return calloc(1, sizeof(mavc)); }
void mavc_set_rgba(mavc *a) { (void)a; }
void mavc_free(mavc *a) { free(a); }
int mavc_decode(mavc *a, const uint8_t *p, int n, int64_t pts) {
    (void)a; (void)pts;
    assert(n == 1 && p[0] == 0x5a);
    atomic_store(&codec_entered, 1);
    while (!atomic_load(&codec_release)) usleep(1000);
    assert(p[0] == 0x5a); // Packet remains readable until the real worker acknowledges completion.
    return 0;
}
int mavc_take(mavc *a, int flush, uint8_t *p, int w, int h, int64_t *pts) {
    (void)a; (void)flush; (void)p; (void)w; (void)h; (void)pts; return 0;
}
int mavc_queued(const mavc *a) { (void)a; return 0; }
void mavc_drop(mavc *a) { (void)a; }

// SDK CreateThread adapter for this fixture. The engine's actual guest thread
// bookkeeping is exercised separately by stop_probe's SVC cases.
void *dm_guest_thread_spawn(void (*fn)(void *), void *arg, int priority, size_t size) {
    (void)priority; (void)size; media_start_guest(fn, arg); return (void *)1;
}
void dm_guest_thread_join(void *tt) { (void)tt; abort(); }
static void job_body(void *unused) {
    (void)unused; mdec_t *d = decoder[1];
    dm_host_lock(&d->dmu);
    d->jp = d->in[0]; d->jn = 1; d->jslot = 0;
    md_job(d, MDJ_DECODE);
    atomic_store(&job_cleaned, 1);
    pthread_mutex_unlock(&d->dmu);
}
void media_movie_start(int cycle) {
    memset(&event, 0, sizeof event); memset(events, 0, sizeof events);
    atomic_store(&codec_entered, 0); atomic_store(&codec_release, 0); atomic_store(&job_cleaned, 0);
    G.signal = fixture_spin;
    h_movie_Decoder(decoder, MD_TYPE_AVC, MD_FMT_NV12, 1, 0);
    mdec_t *d = decoder[1]; assert(d && session_decoders == d);
    d->w = d->h = 16; d->started = 1; d->evc = events; events[0] = &event;
    for (int i = 0; i < MD_NIN; ++i) memset(d->in[i], 0x5a, MD_INCAP);
    md_defer_at(d, 0, now_ns() + (cycle & 1 ? 0 : 100000000000ull));
    if (cycle & 1) media_await(&event.ready);
    media_start_guest(job_body, NULL); media_await(&codec_entered);
}
void media_movie_release(void) { assert(!atomic_load(&job_cleaned)); atomic_store(&codec_release, 1); }
void media_movie_verify(void) {
    assert(atomic_load(&job_cleaned));
    mdec_t *d = decoder[1];
    assert(!pthread_mutex_trylock(&d->dmu)); pthread_mutex_unlock(&d->dmu);
    assert(!pthread_mutex_trylock(&g_dfmu)); pthread_mutex_unlock(&g_dfmu);
    assert(!satchel_movie_session_reset());
    assert(!session_decoders && !g_dfthr && !g_ndfq && !G.signal);
    decoder[0] = decoder[1] = NULL;
}
