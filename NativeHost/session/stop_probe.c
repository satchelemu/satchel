// Synthetic fixtures around the actual pinned kernel, gates, TLS and filesystem.
// The only substitute services are graphics, crash diagnostics and module lookup.
#include "SessionBoundary.h"
#include "media_probe.h"
#include "thread.c"
#include "hle_thread.c"
#include "hle_fs.c"
#include "hle_os.c"
#include <assert.h>
#include <mach/mach.h>
#include <malloc/malloc.h>
void owners_memory_start(int title);
void owners_memory_verify(void);
void owners_hid_start(const char *root, int cycle);
void owners_hid_verify(void);
void owners_native_verify(void);

#define RAW __attribute__((no_instrument_function))
pthread_key_t g_key18, g_keytls, g_keytlr, g_keygate;
struct dm_flags g_flag;
uint64_t g_t0_ns;
__thread _Atomic int dm_host_work;
extern char fixture_begin[], fixture_end[];
static pthread_once_t alt_once = PTHREAD_ONCE_INIT;
static pthread_key_t alt_key;
static void alt_destructor(void *unused) {
    (void)unused; stack_t state;
    assert(!sigaltstack(NULL, &state)); assert(!(state.ss_flags & SS_ONSTACK));
    stack_t off = {.ss_flags = SS_DISABLE, .ss_size = 65536}; assert(!sigaltstack(&off, NULL));
}
static void alt_init(void) { assert(!pthread_key_create(&alt_key, alt_destructor)); }
void crash_thread_init(void) {
    pthread_once(&alt_once, alt_init);
    void *m = mmap(NULL, 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(m != MAP_FAILED); satchel_session_own_mapping(m, 65536);
    stack_t stack = {.ss_sp = m, .ss_size = 65536}; assert(!sigaltstack(&stack, NULL));
    pthread_setspecific(alt_key, (void *)1);
}
void crash_watch_frame(void) {}
void *module_sym(module_t *m, const char *name) { (void)m; (void)name; return NULL; }
void *guest_sym(const char *name) { return media_probe_symbol(name); }
module_t *module_of(const void *p) {
    static module_t synthetic;
    return (uintptr_t)p >= (uintptr_t)fixture_begin && (uintptr_t)p < (uintptr_t)fixture_end ? &synthetic : NULL;
}
void dm_prof_add(int id, uint64_t ns, uint64_t bytes) { (void)id; (void)ns; (void)bytes; }
int dm_sdk_ounce(void) { return 0; } // the synthetic guest is a Switch 1 program
static void (*write_prepare)(void *, size_t);
void dm_guest_write_prep_register(void (*fn)(void *, size_t)) { write_prepare = fn; }
void dm_fault_hook_register(int (*fn)(void *, int, void *)) { (void)fn; }
void dm_guest_write_prepare(void *address, size_t bytes) { if (write_prepare) write_prepare(address, bytes); }
void unimpl_c(unsigned index, uint64_t *regs) { (void)index; (void)regs; abort(); }
extern void fixture_spin(void *), fixture_svc(void *);
void *RAW fixture_gate_address(void) { return pthread_getspecific(g_keygate); }
uint64_t gate_dispatch(uint64_t address, uint64_t *regs) {
    satchel_kernel_gate_enter(satchel_session_root_call());
    thr_svc_enter(regs, address);
    if (regs[8]) assert(svc_thread((int)regs[8], regs));
    else guest_clock_read_wait();
    thr_svc_leave();
    return 0;
}

typedef struct { _Atomic int ready, release; uint64_t args[4], number; } fixture;
_Static_assert(offsetof(fixture, args) == 8 && offsetof(fixture, number) == 40, "fixture ABI");
typedef struct { uint64_t rss; unsigned threads; int fds; } sample;
static sample measure(void) {
    mach_task_basic_info_data_t info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    assert(task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) == KERN_SUCCESS);
    thread_act_array_t threads; count = 0;
    assert(task_threads(mach_task_self(), &threads, &count) == KERN_SUCCESS);
    unsigned nthreads = count;
    for (unsigned i = 0; i < count; ++i) mach_port_deallocate(mach_task_self(), threads[i]);
    vm_deallocate(mach_task_self(), (vm_address_t)threads, count * sizeof(*threads));
    int descriptors = 0;
    for (int fd = 0; fd < getdtablesize(); ++fd) {
        errno = 0; if (fcntl(fd, F_GETFD) != -1 || errno != EBADF) ++descriptors;
    }
    return (sample){info.resident_size, nthreads, descriptors};
}
static void await_value(_Atomic int *value) {
    uint64_t until = now_ns() + 2000000000;
    while (!atomic_load(value)) { assert(now_ns() < until); usleep(1000); }
}
void media_await(_Atomic int *value) { await_value(value); }
typedef struct { void (*fn)(void *); void *arg; } media_entry_args;
static void RAW media_entry(void *p) { media_entry_args *a = p; a->fn(a->arg); }
static void *media_worker(void *p) {
    media_entry_args a = *(media_entry_args *)p; free(p);
    satchel_session_run_guest(media_entry, &a); return NULL;
}
void media_start_guest(void (*fn)(void *), void *arg) {
    media_entry_args *a = malloc(sizeof *a); assert(a); *a = (media_entry_args){fn, arg};
    pthread_t t; assert(!satchel_session_pthread_create(&t, NULL, media_worker, a));
}
static void start_boundary(void) {
    assert(!satchel_session_begin());
    assert(!satchel_session_guest_range(fixture_begin, fixture_end - fixture_begin));
}
static void reset_boundary(void) {
    assert(satchel_session_quiescent());
    assert(!satchel_session_live_threads()); assert(!satchel_session_live_mappings());
    assert(!satchel_kernel_session_reset());
    assert(!satchel_clock_session_reset());
    assert(!satchel_fs_session_reset());
    assert(!satchel_profile_session_reset());
    assert(!satchel_session_end());
}
static void *main_thread(void *argument) {
    crash_thread_init(); guest_thread_init(); guest_gate_stack(0); svc_thread_main_init();
    unsigned slot = (unsigned)(uintptr_t)pthread_getspecific(g_keygslot) - 1;
    memset((void *)(uintptr_t)(dm_gate_top[slot] - GATE_STACK_SIZE), 0x5a, GATE_STACK_SIZE);
    satchel_session_run_guest(fixture_spin, argument);
    satchel_kernel_thread_exit(); return NULL;
}
static unsigned child(fixture *f, void *stack, int started, int spin) {
    uint64_t regs[100] = {0, (uintptr_t)(spin ? fixture_spin : fixture_svc), (uintptr_t)f,
                            (uintptr_t)stack + (1 << 20), 44, 1};
    assert(svc_thread(0x08, regs) && !regs[0]); unsigned handle = (unsigned)regs[1];
    if (started) { regs[0] = handle; assert(svc_thread(0x09, regs) && !regs[0]); }
    return handle;
}
static void read_save(char *out, const char *name) {
    uint64_t handle; size_t got = 0;
    assert(!h_fs_OpenFile(&handle, name, MODE_READ));
    assert(!h_fs_ReadFile(&got, handle, 0, out, 63)); out[got] = 0;
    h_fs_CloseFile(handle);
}
static void write_save(const char *name, const char *value) {
    uint64_t handle;
    assert(!h_fs_OpenFile(&handle, name, MODE_WRITE));
    assert(!h_fs_SetFileSize(handle, strlen(value)));
    assert(!h_fs_WriteFile(handle, 0, value, strlen(value), NULL)); h_fs_CloseFile(handle);
}
static void init_save(const char *path) {
    assert(!mkdir(path, 0700));
    for (unsigned i = 0; i < 2; ++i) {
        char file[1200]; snprintf(file, sizeof file, "%s/%s", path, i ? "meta" : "value");
        FILE *f = fopen(file, "w"); assert(f); assert(fputs("initial", f) >= 0); assert(!fclose(f));
    }
}

// An interrupted guest callback must not abandon a native mutex or cleanup.
static pthread_mutex_t nested_lock = PTHREAD_MUTEX_INITIALIZER;
static fixture nested;
static _Atomic int unwound;
static void host_callback(void *arg) {
    pthread_mutex_lock(&nested_lock);
    fixture_spin(arg);
    pthread_mutex_unlock(&nested_lock);
    atomic_store(&unwound, 1);
}
static void RAW nested_guest(void *arg) { host_callback(arg); }
static void *nested_worker(void *arg) { satchel_session_run_guest(nested_guest, arg); return NULL; }
static pthread_mutex_t scoped_lock = PTHREAD_MUTEX_INITIALIZER;
static _Atomic int scoped_cleaned, scoped_inner_cleaned;
static void scoped_inner(void *arg) {
    pthread_mutex_lock(&scoped_lock);
    assert(!satchel_session_call_guest(fixture_spin, arg));
    atomic_store(&scoped_inner_cleaned, 1);
    pthread_mutex_unlock(&scoped_lock);
}
static void RAW scoped_nested(void *arg) { scoped_inner(arg); }
static void scoped_host(void *arg) {
    pthread_mutex_lock(&nested_lock);
    assert(!satchel_session_call_guest(scoped_nested, arg));
    atomic_store(&scoped_cleaned, 1);
    pthread_mutex_unlock(&nested_lock);
}
static void RAW scoped_guest(void *arg) { scoped_host(arg); }
static void *scoped_worker(void *arg) { satchel_session_run_guest(scoped_guest, arg); return NULL; }
static void scoped_wait(void *arg) {
    pthread_mutex_lock(&nested_lock);
    assert(!satchel_session_call_guest(fixture_svc, arg));
    atomic_store(&scoped_cleaned, 1);
    pthread_mutex_unlock(&nested_lock);
}
static void RAW scoped_wait_guest(void *arg) { scoped_wait(arg); }
static void *scoped_wait_worker(void *arg) {
    crash_thread_init(); guest_thread_init(); guest_gate_stack(0); svc_thread_main_init();
    satchel_session_run_guest(scoped_wait_guest, arg);
    satchel_kernel_thread_exit(); return NULL;
}
static void scoped_cases(void) {
    fixture f = {0}; pthread_t t; start_boundary();
    atomic_store(&scoped_cleaned, 0); atomic_store(&scoped_inner_cleaned, 0);
    assert(!satchel_session_pthread_create(&t, NULL, scoped_worker, &f)); await_value(&f.ready);
    satchel_session_request_stop(); assert(!satchel_session_join(2000));
    assert(atomic_load(&scoped_cleaned) && atomic_load(&scoped_inner_cleaned));
    assert(!pthread_mutex_trylock(&scoped_lock)); pthread_mutex_unlock(&scoped_lock);
    assert(!pthread_mutex_trylock(&nested_lock)); pthread_mutex_unlock(&nested_lock);
    reset_boundary();
    puts("nested scoped callbacks: cancellation returned through both native cleanups PASS");

    f = (fixture){0}; f.number = 0x34; f.args[0] = (uintptr_t)&f.release; f.args[1] = 2; f.args[3] = -1;
    atomic_store(&scoped_cleaned, 0); start_boundary();
    assert(!satchel_session_pthread_create(&t, NULL, scoped_wait_worker, &f)); await_value(&f.ready);
    uint64_t until = now_ns() + 2000000000;
    for (;;) {
        pthread_mutex_lock(&G); int waiting = g_wait != NULL; pthread_mutex_unlock(&G);
        if (waiting) break; assert(now_ns() < until); usleep(1000);
    }
    satchel_session_request_stop(); satchel_kernel_request_stop();
    assert(!satchel_session_join(2000) && atomic_load(&scoped_cleaned));
    assert(!pthread_mutex_trylock(&nested_lock)); pthread_mutex_unlock(&nested_lock);
    reset_boundary();
    puts("scoped callback through actual SVC gate: kernel wait cancelled, native mutex released PASS");
}
static pthread_key_t destructor_key;
static _Atomic int destructor_entered, destructor_release;
static void destructor(void *unused) {
    (void)unused; atomic_store(&destructor_entered, 1);
    while (!atomic_load(&destructor_release)) usleep(1000);
}
static void *destructor_worker(void *unused) {
    (void)unused;
    void *m = mmap(NULL, 1 << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    assert(m != MAP_FAILED); memset(m, 0x7a, 1 << 20); satchel_session_own_mapping(m, 1 << 20);
    pthread_setspecific(destructor_key, (void *)1); return NULL;
}
static void negative_cases(void) {
    pthread_t t; start_boundary();
    assert(!satchel_session_pthread_create(&t, NULL, nested_worker, &nested)); await_value(&nested.ready);
    satchel_session_request_stop();
    assert(satchel_session_join(20) == ETIMEDOUT);
    assert(satchel_session_end() == EBUSY && satchel_session_begin() == EBUSY);
    assert(satchel_kernel_session_reset() == EBUSY && satchel_fs_session_reset() == EBUSY);
    assert(pthread_mutex_trylock(&nested_lock) == EBUSY && !atomic_load(&unwound));
    atomic_store(&nested.release, 1);
    assert(!satchel_session_join(2000)); assert(atomic_load(&unwound));
    assert(!pthread_mutex_trylock(&nested_lock)); pthread_mutex_unlock(&nested_lock);
    reset_boundary();
    puts("nested native callback: timeout retained lock and session, normal unwind then join PASS");

    assert(!pthread_key_create(&destructor_key, destructor)); start_boundary();
    assert(!satchel_session_pthread_create(&t, NULL, destructor_worker, NULL)); await_value(&destructor_entered);
    satchel_session_request_stop(); assert(satchel_session_join(20) == ETIMEDOUT);
    assert(satchel_session_live_threads() == 1 && satchel_session_live_mappings() == 1);
    assert(!satchel_session_quiescent() && satchel_session_end() == EBUSY);
    atomic_store(&destructor_release, 1); assert(!satchel_session_join(2000)); reset_boundary();
    pthread_key_delete(destructor_key);
    puts("blocked TLS destructor: no early reclamation, join then unmap PASS");
}

static _Atomic int commit_entered;
static void RAW commit_guest(void *unused) {
    (void)unused; atomic_store(&commit_entered, 1); assert(!h_fs_Commit("save"));
}
static void *commit_worker(void *unused) { satchel_session_run_guest(commit_guest, unused); return NULL; }
static void commit_case(const char *root) {
    char path[1024]; snprintf(path, sizeof path, "%s/inflight-commit", root); init_save(path);
    setenv("DM_SAVE", path, 1); start_boundary(); assert(!h_fs_MountSave("save", NULL));
    write_save("save:/value", "commit-under-stop"); write_save("save:/meta", "commit-under-stop");
    pthread_mutex_lock(&g_path_lock);
    pthread_t thread; assert(!satchel_session_pthread_create(&thread, NULL, commit_worker, NULL));
    await_value(&commit_entered); satchel_session_request_stop();
    assert(satchel_session_join(20) == ETIMEDOUT && !satchel_session_quiescent());
    pthread_mutex_unlock(&g_path_lock);
    assert(!satchel_session_join(2000)); reset_boundary();
    for (int i = 0; i < 2; ++i) {
        char file[1200], content[64] = {0}; snprintf(file, sizeof file, "%s/%s", path, i ? "meta" : "value");
        FILE *f = fopen(file, "r"); assert(f); assert(fread(content, 1, 63, f) == 17); assert(!fclose(f));
        assert(!strcmp(content, "commit-under-stop"));
    }
    puts("in-flight guest Commit: stop waited for native call, atomic save durable before reset PASS");
}

int main(int argc, char **argv) {
    assert(argc == 2); setenv("DM_SAVE_SYNC", "0", 1);
    assert(!pthread_key_create(&g_key18, NULL)); assert(!pthread_key_create(&g_keytls, NULL));
    assert(!pthread_key_create(&g_keytlr, NULL)); assert(!pthread_key_create(&g_keygate, NULL));
    negative_cases(); scoped_cases(); commit_case(argv[1]); owners_native_verify();
    char saves[2][1024];
    for (int i = 0; i < 2; ++i) { snprintf(saves[i], sizeof saves[i], "%s/title-%d", argv[1], i); init_save(saves[i]); }
    sample cold = measure(), baseline = cold;
    printf("cold rss=%llu threads=%u fds=%d\n", cold.rss, cold.threads, cold.fds);
    // Cycle zero warms libc, the kernel's process-lifetime locks and malloc arenas.
    // Five same-title restarts, then an explicit A to B to A to B sequence.
    char expected[2][64] = {"initial", "initial"};
    for (int cycle = 0; cycle <= 8; ++cycle) {
        int title = cycle <= 5 ? 0 : !(cycle % 2);
        media_select_title(title);
        char profile[32]; snprintf(profile, sizeof profile, "synthetic-%d", title);
        setenv("DM_PROF_NAME", profile, 1); setenv("DM_SAVE", saves[title], 1);
        setenv("DM_YIELD_BACKOFF", title ? "100:0" : "200:0", 1);
        setenv("DM_PROF_MEM_RESERVE", title ? "0x120000000" : "0x140000000", 1);
        setenv("DM_PROF_MEM_ALIAS", "0x98000000", 1);
        setenv("DM_PROF_MEM_STACK", "0x10000000", 1);
        setenv("DM_PROF_APP_POOL", "0xcd500000", 1);
        setenv("DM_ALIAS_WINDOWS", "256", 1);
        start_boundary(); assert(!strcmp(dm_profile()->name, profile));
        owners_memory_start(title);
        owners_hid_start(argv[1], cycle);
        assert(yield_backoff_ns(8000000) == (title ? 100000 : 200000));
        assert(!h_fs_MountSave("save", NULL));
        char saved[64]; read_save(saved, "save:/value"); assert(!strcmp(saved, expected[title]));
        read_save(saved, "save:/meta"); assert(!strcmp(saved, expected[title]));
        snprintf(expected[title], sizeof expected[title], "committed-cycle-%d", cycle);
        write_save("save:/value", expected[title]); write_save("save:/meta", expected[title]);
        assert(!h_fs_Commit("save"));
        // Simulate stopping between two writes of the next logical transaction.
        write_save("save:/value", "unfinished-next-transaction");
        uint64_t abandoned_file, abandoned_dir;
        assert(!h_fs_OpenFile(&abandoned_file, "save:/value", MODE_READ));
        assert(!h_fs_OpenDirectory(&abandoned_dir, "save:/", DIRMODE_FILE));

        fixture main = {0}, f[10] = {0}; pthread_t t;
        assert(!satchel_session_pthread_create(&t, NULL, main_thread, &main)); await_value(&main.ready);
        media_audio_start(); media_audren_start(); media_movie_start(cycle); media_helper_start();
        assert(satchel_session_guest_range(fixture_begin, 4) == EBUSY);
        unsigned handles[10]; void *stacks[10];
        uint32_t address = 0, mutex = 0x1001 | WAITERS_BIT, cv_mutex = 0, cv_key = 0;
        uint64_t event[100] = {0}; assert(svc_thread(0x45, event) && !event[0]);
        uint32_t event_read = (uint32_t)event[2];
        f[1].number = 0x34; f[1].args[0] = (uintptr_t)&address; f[1].args[1] = 2; f[1].args[3] = -1;
        f[2].number = 0x1A; f[2].args[0] = 0x1001; f[2].args[1] = (uintptr_t)&mutex; f[2].args[2] = 0x1004;
        f[3].number = 0x1C; f[3].args[0] = (uintptr_t)&cv_mutex; f[3].args[1] = (uintptr_t)&cv_key; f[3].args[2] = 0x1005; f[3].args[3] = -1;
        f[4].number = 0x18; f[4].args[1] = (uintptr_t)&event_read; f[4].args[2] = 1; f[4].args[3] = -1;
        f[5].number = 0x0B; f[5].args[0] = 3600000000000ull;
        f[6].number = 0x34; f[6].args[0] = (uintptr_t)&address; f[6].args[1] = 2; f[6].args[3] = -1;
        f[9].number = 0x0A;
        for (int i = 0; i < 10; ++i) {
            if (i == 8) guest_clock_freeze_for(1, 1);
            stacks[i] = mmap(NULL, 1 << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
            assert(stacks[i] != MAP_FAILED); memset(stacks[i], 0x3c, 1 << 20);
            handles[i] = child(&f[i], stacks[i], i != 7, i == 0);
            if (i != 7) await_value(&f[i].ready);
        }
        // Exercise the real SIGUSR2 pause handler on a registered guest PC and
        // alternate signal stack, as well as normal ExitThread through the gate.
        pthread_mutex_lock(&G);
        thr_t *spin = h_peek(handles[0], O_THREAD); assert(spin); spin->pause_req = 1;
        pthread_t spin_thread = spin->pt; pthread_mutex_unlock(&G);
        uint64_t pause_until = now_ns() + 2000000000;
        for (;;) {
            pthread_kill(spin_thread, SIGUSR2);
            pthread_mutex_lock(&G);
            int done = spin->paused && ((thr_t *)h_peek(handles[9], O_THREAD))->exited;
            pthread_mutex_unlock(&G);
            if (done) break;
            assert(now_ns() < pause_until); usleep(1000);
        }
        uint64_t close_regs[100] = {handles[9]}; assert(svc_thread(0x16, close_regs) && !close_regs[0]);
        uint64_t until = now_ns() + 2000000000;
        for (;;) {
            pthread_mutex_lock(&G);
            int waiters = 0; for (thr_t *w = g_wait; w; w = w->next) ++waiters;
            if (waiters == 5) {
                thr_t *paused = h_peek(handles[6], O_THREAD); assert(paused);
                atomic_store(&paused->pause_req, 1); atomic_fetch_or(&paused->svc_state, 2);
                pthread_mutex_unlock(&G); break;
            }
            pthread_mutex_unlock(&G); assert(now_ns() < until); usleep(1000);
        }
        assert(satchel_kernel_session_reset() == EBUSY && satchel_fs_session_reset() == EBUSY);
        sample during = measure(); assert(during.rss > baseline.rss + 8 * 1024 * 1024);
        satchel_session_request_stop(); satchel_kernel_request_stop(); satchel_clock_request_stop();
        satchel_movie_request_stop(); satchel_audren_request_stop(); satchel_audio_request_stop();
        satchel_memory_request_stop();
        satchel_hid_request_stop();
        assert(satchel_session_pthread_create(&t, NULL, main_thread, &main) == EBUSY);
        assert(satchel_session_join(20) == ETIMEDOUT && !satchel_session_quiescent());
        media_movie_release();
        assert(!satchel_session_join(2000));
        media_movie_verify(); media_audren_verify(); media_audio_verify(); media_helper_verify();
        owners_hid_verify();
        owners_memory_verify();
        assert(g_gate_nfree == GATE_SLOTS);
        for (unsigned i = 0; i < GATE_SLOTS; ++i) assert(!dm_gate_top[i]);
        for (int i = 0; i < 10; ++i) assert(!munmap(stacks[i], 1 << 20));
        reset_boundary();
        sample after = measure();
        assert(after.threads == cold.threads && after.fds == cold.fds);
        if (!cycle) baseline = after;
        int64_t delta = (int64_t)after.rss - baseline.rss;
        assert(delta < 2 * 1024 * 1024);
        printf("cycle=%d synthetic_title=%d rss=%llu delta=%lld threads=%u fds=%d live_threads=%u live_mappings=%u save_readback=PASS media=PASS sdk_address_refresh=PASS restart=PASS\n",
               cycle, title, after.rss, delta, after.threads, after.fds, satchel_session_live_threads(), satchel_session_live_mappings());
    }
    puts("five same-title restarts plus A/B/A/B switching: zero restart failures; native media joined; committed saves survived dirty interrupted work");
    return 0;
}
