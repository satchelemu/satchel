#include "hle.h"
#undef HLE_FORCE
#define HLE_FORCE(mangled, why, ret, cname, ...) static ret cname(__VA_ARGS__)
#include "hle_audren.c"
#include "media_probe.h"
#include <assert.h>

#define RAW __attribute__((no_instrument_function))
extern void fixture_spin(void *), fixture_spin_second(void *);
static int selected_title;
void media_select_title(int title) { selected_title = title; }
static media_fixture execute;
static _Alignas(16) uint8_t system_fixture[0x400];
static _Atomic int render_cleaned;
static int RAW mode_fixture(void *p) { (void)p; return 1; }
static void RAW unused_fixture(void *p) { (void)p; abort(); }
void *media_probe_symbol(const char *name) {
    static uint8_t vtable[2][64];
    void *entry = selected_title ? fixture_spin_second : fixture_spin;
    if (!strcmp(name, "_ZTVN5movie7DecoderE")) return vtable[selected_title];
    if (!strcmp(name, "_ZN2nn5audio29ExecuteAudioRendererRenderingENS0_19AudioRendererHandleE")) return entry;
    if (strstr(name, "GetExecutionMode") || strstr(name, "GetRenderingDevice")) return mode_fixture;
    if (strstr(name, "SdkMutexType4Lock")) return entry;
    if (strstr(name, "GenerateCommandENS_") || strstr(name, "AudioRenderSystem6Update")) return NULL;
    if (strstr(name, "GetCommandBuffer") || strstr(name, "GenerateCommandEP") ||
        strstr(name, "SdkMutexType6Unlock") || strstr(name, "CommandListProcessor")) return unused_fixture;
    return NULL;
}
static void render_body(void *unused) {
    (void)unused;
    assert(render_once(&g_ar[0], system_fixture, NULL, NULL) == 0x40299);
    atomic_store(&render_cleaned, 1);
}
void media_audren_start(void) {
    memset(&execute, 0, sizeof execute); memset(system_fixture, 0, sizeof system_fixture);
    atomic_store(&render_cleaned, 0);
    // One renderer slot, set up as the open path does before it starts the render thread.
    ar_t *a = &g_ar[0]; memset(a, 0, sizeof *a);
    pthread_mutex_init(&a->cq_mu, NULL); pthread_cond_init(&a->cq_cv, NULL);
    a->handle[0] = &execute; a->handle[1] = NULL; atomic_store(&a->open, 1);
    media_start_guest(render_main, a); media_await(&execute.ready);
    media_start_guest(render_body, NULL);
    media_await(&((media_fixture *)(system_fixture + 0x1f0))->ready);
    assert(atomic_load(&execute.ready) == selected_title + 1);
    assert(atomic_load(&((media_fixture *)(system_fixture + 0x1f0))->ready) == selected_title + 1);
}
void media_audren_verify(void) {
    assert(atomic_load(&render_cleaned));
    assert(!pthread_mutex_trylock(&g_render_mu)); pthread_mutex_unlock(&g_render_mu);
    assert(!atomic_load(&g_ar[0].open));
    assert(!satchel_audren_session_reset());
    for (int i = 0; i < AR_MAX; i++) assert(!g_ar[i].handle[0] && !g_ar[i].sys && !atomic_load(&g_ar[i].cq_tail));
    assert(!session_works);
}
