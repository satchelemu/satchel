// Satchel guest thread ownership and termination boundary.
// Compile this file without function instrumentation. Guest-facing host C/ObjC
// code must be instrumented so a nested guest callback cannot abandon host locks.
#include "SessionBoundary.h"
#include <assert.h>
#include <mach/mach.h>
#include <mach/semaphore.h>
#include <mach/sync_policy.h>
#include <errno.h>
#include <setjmp.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/ucontext.h>
#include <time.h>

_Static_assert(ATOMIC_INT_LOCK_FREE == 2, "The signal handler needs lock-free stop loads");

typedef struct mapping { void *base; size_t size; struct mapping *next; } mapping;
typedef struct worker {
    pthread_t thread;
    void *(*function)(void *);
    void *argument;
    int finished;
    semaphore_t resume;
    mapping *mappings;
    struct worker *next;
} worker;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static worker *workers;
static pthread_t reaper;
static int phase, reaper_done, joining, reaper_joined, ranges_sealed;
static _Atomic int stopping, paused;
static _Atomic unsigned live_threads, live_mappings;
static struct { uintptr_t first, last; } ranges[1024];
static unsigned range_count;
static struct sigaction old_signal;
// The phone shader translator (NativeAOT) takes SIGUSR1 for its own thread activation
// on its first call, so guest interrupts use a signal nothing else in the app installs.
#define SATCHEL_SESSION_SIGNAL SIGVTALRM

// Accessed by the current thread and its signal handler only. The thread-local
// block is touched before signals are unblocked, so handler access cannot lazily
// allocate it. A signal may escape only pure guest code with no active host call.
static _Thread_local worker *current;
static _Thread_local volatile sig_atomic_t armed, native_depth;
typedef struct guest_scope {
    sigjmp_buf escape;
    sig_atomic_t depth;
    struct guest_scope *previous;
} guest_scope;
static _Thread_local guest_scope *volatile scope;

// Mach semaphore traps do not allocate or acquire libc locks in a signal handler.
// Only pure guest code and the outer host return boundary park. A native call
// may finish with its locks intact, then parks before returning to guest code.
static void pause_checkpoint(void) {
    while (current && atomic_load_explicit(&paused, memory_order_acquire) && !atomic_load(&stopping))
        semaphore_wait(current->resume);
}
static void checkpoint(void) {
    if (armed && scope && native_depth == 0 && scope->depth == 0) pause_checkpoint();
    if (armed && scope && native_depth == scope->depth &&
        atomic_load_explicit(&stopping, memory_order_acquire))
        siglongjmp(scope->escape, 1);
}
void __cyg_profile_func_enter(void *function, void *caller) {
    (void)function; (void)caller;
    if (armed) ++native_depth;
}
void __cyg_profile_func_exit(void *function, void *caller) {
    (void)function; (void)caller;
    if (armed) { assert(native_depth > 0); --native_depth; checkpoint(); }
}
int satchel_session_root_call(void) { return armed && scope && native_depth == scope->depth + 1; }

static void interrupt_guest(int signal, siginfo_t *info, void *context) {
    (void)signal; (void)info;
    if (!armed || !scope || native_depth != scope->depth ||
        (!atomic_load_explicit(&stopping, memory_order_relaxed) && !atomic_load(&paused))) return;
    ucontext_t *uc = context;
    uintptr_t pc = __darwin_arm_thread_state64_get_pc(uc->uc_mcontext->__ss);
    // These ranges are immutable from the first worker until the last join.
    // No module lookup, allocation, logging, mutex or pthread API in the handler.
    for (unsigned i = 0; i < range_count; ++i)
        if (pc >= ranges[i].first && pc < ranges[i].last) {
            if (native_depth == 0 && scope->depth == 0) pause_checkpoint();
            if (atomic_load(&stopping)) siglongjmp(scope->escape, 1);
            return;
        }
}

int satchel_session_call_guest(void (*function)(void *), void *argument) {
    if (!armed) { function(argument); return 1; }
    guest_scope next = {.depth = native_depth, .previous = scope};
    int completed = !sigsetjmp(next.escape, 1);
    if (completed) {
        scope = &next;
        checkpoint();
        function(argument);
    }
    // The interrupted guest may own guest locks. It must never resume. Host
    // callers regain control here to release their own locks and native buffers.
    scope = next.previous;
    native_depth = next.depth;
    return completed;
}

void satchel_session_run_guest(void (*function)(void *), void *argument) {
    if (!satchel_session_active()) { function(argument); return; }
    assert(current && !armed);
    pthread_mutex_lock(&lock); ranges_sealed = 1; pthread_mutex_unlock(&lock);
    native_depth = 0;
    armed = 1;
    satchel_session_call_guest(function, argument);
    armed = 0;
    scope = NULL;
    native_depth = 0;
}

static void completed(void *argument) {
    worker *w = argument;
    armed = 0;
    scope = NULL;
    native_depth = 0;
    pthread_mutex_lock(&lock);
    w->finished = 1;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&lock);
    // TLS destructors still run after this point. Only the reaper may reclaim
    // mappings, and it does so only after pthread_join has completed.
}
static void *start(void *argument) {
    worker *w = argument;
    current = w; armed = 0; native_depth = 0;
    sigset_t mask;
    sigemptyset(&mask); sigaddset(&mask, SATCHEL_SESSION_SIGNAL);
    pthread_sigmask(SIG_UNBLOCK, &mask, NULL);
    void *result;
    pthread_cleanup_push(completed, w);
    result = w->function(w->argument);
    pthread_cleanup_pop(1);
    return result;
}
static void *reap(void *unused) {
    (void)unused;
    pthread_mutex_lock(&lock);
    for (;;) {
        worker *w = workers;
        while (w && !w->finished) w = w->next;
        if (!w) {
            if (atomic_load(&stopping) && !workers) break;
            pthread_cond_wait(&changed, &lock);
            continue;
        }
        pthread_mutex_unlock(&lock);
        int error = pthread_join(w->thread, NULL);
        // A failed join cannot be treated as permission to reclaim memory.
        if (error) abort();
        for (mapping *m = w->mappings; m;) {
            mapping *next = m->next;
            if (munmap(m->base, m->size)) abort();
            atomic_fetch_sub(&live_mappings, 1);
            free(m); m = next;
        }
        pthread_mutex_lock(&lock);
        worker **p = &workers;
        while (*p != w) p = &(*p)->next;
        *p = w->next;
        atomic_fetch_sub(&live_threads, 1);
        semaphore_destroy(mach_task_self(), w->resume);
        free(w);
        pthread_cond_broadcast(&changed);
    }
    reaper_done = 1;
    pthread_cond_broadcast(&changed);
    pthread_mutex_unlock(&lock);
    return NULL;
}
int satchel_session_begin(void) {
    pthread_mutex_lock(&lock);
    if (phase) { pthread_mutex_unlock(&lock); return EBUSY; }
    struct sigaction action = {.sa_sigaction = interrupt_guest, .sa_flags = SA_SIGINFO | SA_RESTART};
    sigemptyset(&action.sa_mask);
    if (sigaction(SATCHEL_SESSION_SIGNAL, NULL, &old_signal)) { int e = errno; pthread_mutex_unlock(&lock); return e; }
    if (old_signal.sa_handler != SIG_DFL && old_signal.sa_handler != SIG_IGN) {
        pthread_mutex_unlock(&lock); return EBUSY;
    }
    if (sigaction(SATCHEL_SESSION_SIGNAL, &action, NULL)) { int e = errno; pthread_mutex_unlock(&lock); return e; }
    phase = 1; range_count = 0; ranges_sealed = 0; reaper_done = reaper_joined = joining = 0;
    atomic_store(&stopping, 0); atomic_store(&paused, 0);
    int error = pthread_create(&reaper, NULL, reap, NULL);
    if (error) { phase = 0; sigaction(SATCHEL_SESSION_SIGNAL, &old_signal, NULL); }
    pthread_mutex_unlock(&lock);
    return error;
}
int satchel_session_active(void) {
    pthread_mutex_lock(&lock); int active = phase != 0; pthread_mutex_unlock(&lock); return active;
}
int satchel_session_stopping(void) { return atomic_load_explicit(&stopping, memory_order_acquire); }
int satchel_session_quiescent(void) {
    pthread_mutex_lock(&lock);
    int result = phase == 2 && reaper_joined && !workers;
    pthread_mutex_unlock(&lock); return result;
}
int satchel_session_guest_range(const void *base, size_t size) {
    uintptr_t first = (uintptr_t)base;
    pthread_mutex_lock(&lock);
    int error = phase != 1 || ranges_sealed || atomic_load(&stopping) ? EBUSY :
        !size || size > UINTPTR_MAX - first || range_count == 1024 ? EINVAL : 0;
    if (!error) { ranges[range_count].first = first; ranges[range_count++].last = first + size; }
    pthread_mutex_unlock(&lock); return error;
}
int satchel_session_pthread_create(pthread_t *thread, const pthread_attr_t *attributes,
                              void *(*function)(void *), void *argument) {
    pthread_mutex_lock(&lock);
    if (!phase) { pthread_mutex_unlock(&lock); return pthread_create(thread, attributes, function, argument); }
    if (phase != 1 || atomic_load(&stopping)) { pthread_mutex_unlock(&lock); return EBUSY; }
    int detached = PTHREAD_CREATE_JOINABLE;
    if (attributes) pthread_attr_getdetachstate(attributes, &detached);
    if (detached != PTHREAD_CREATE_JOINABLE) { pthread_mutex_unlock(&lock); return EINVAL; }
    worker *w = calloc(1, sizeof *w);
    if (!w) { pthread_mutex_unlock(&lock); return ENOMEM; }
    if (semaphore_create(mach_task_self(), &w->resume, SYNC_POLICY_FIFO, 0) != KERN_SUCCESS) {
        free(w); pthread_mutex_unlock(&lock); return ENOMEM;
    }
    w->function = function; w->argument = argument;
    // The child inherits this mask. Its TLS must exist before the first stop
    // signal, including when a stop races the pthread startup machinery.
    sigset_t block, previous;
    sigemptyset(&block); sigaddset(&block, SATCHEL_SESSION_SIGNAL);
    int error = pthread_sigmask(SIG_BLOCK, &block, &previous);
    if (!error) {
        error = pthread_create(&w->thread, attributes, start, w);
        if (pthread_sigmask(SIG_SETMASK, &previous, NULL)) abort();
    }
    if (error) { semaphore_destroy(mach_task_self(), w->resume); free(w); }
    else { *thread = w->thread; w->next = workers; workers = w; atomic_fetch_add(&live_threads, 1); }
    pthread_mutex_unlock(&lock); return error;
}
void satchel_session_own_mapping(void *base, size_t size) {
    assert(current && base && size);
    mapping *m = malloc(sizeof *m); if (!m) abort();
    *m = (mapping){base, size, current->mappings}; current->mappings = m;
    atomic_fetch_add(&live_mappings, 1);
}
void satchel_session_pause(int on) {
    pthread_mutex_lock(&lock);
    atomic_store_explicit(&paused, on && !atomic_load(&stopping), memory_order_release);
    for (worker *w = workers; w; w = w->next) if (!w->finished) {
        if (atomic_load(&paused)) pthread_kill(w->thread, SATCHEL_SESSION_SIGNAL);
        else semaphore_signal(w->resume);
    }
    pthread_mutex_unlock(&lock);
}
void satchel_session_request_stop(void) {
    pthread_mutex_lock(&lock);
    if (phase == 1) { phase = 2; atomic_store_explicit(&stopping, 1, memory_order_release); }
    atomic_store(&paused, 0);
    for (worker *w = workers; w; w = w->next) semaphore_signal(w->resume);
    pthread_cond_broadcast(&changed); pthread_mutex_unlock(&lock);
}
int satchel_session_join(unsigned timeout_ms) {
    if (current) return EDEADLK;
    uint64_t deadline = clock_gettime_nsec_np(CLOCK_MONOTONIC) + (uint64_t)timeout_ms * 1000000;
    pthread_mutex_lock(&lock);
    if (phase != 2 || joining) { pthread_mutex_unlock(&lock); return EBUSY; }
    if (reaper_joined) { pthread_mutex_unlock(&lock); return 0; }
    joining = 1;
    while (!reaper_done) {
        for (worker *w = workers; w; w = w->next)
            if (!w->finished) pthread_kill(w->thread, SATCHEL_SESSION_SIGNAL);
        uint64_t now = clock_gettime_nsec_np(CLOCK_MONOTONIC);
        if (now >= deadline) { joining = 0; pthread_mutex_unlock(&lock); return ETIMEDOUT; }
        struct timespec delay = {0, (long)(deadline - now < 1000000 ? deadline - now : 1000000)};
        pthread_cond_timedwait_relative_np(&changed, &lock, &delay);
    }
    pthread_mutex_unlock(&lock);
    int error = pthread_join(reaper, NULL);
    pthread_mutex_lock(&lock); joining = 0; reaper_joined = !error; pthread_mutex_unlock(&lock);
    return error;
}
int satchel_session_end(void) {
    pthread_mutex_lock(&lock);
    if (phase != 2 || workers || !reaper_joined) { pthread_mutex_unlock(&lock); return EBUSY; }
    int error = sigaction(SATCHEL_SESSION_SIGNAL, &old_signal, NULL) ? errno : 0;
    if (!error) { phase = 0; range_count = 0; atomic_store(&stopping, 0); }
    pthread_mutex_unlock(&lock); return error;
}
unsigned satchel_session_live_threads(void) { return atomic_load(&live_threads); }
unsigned satchel_session_live_mappings(void) { return atomic_load(&live_mappings); }
