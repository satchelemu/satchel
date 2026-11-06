// Synthetic checks of the transfer memory and remaining application SVCs in
// KernelSVC.inc. The production dispatcher is included from the overlaid
// hle_svc.c and linked with the overlaid hle_thread.c, so CloseHandle, the
// handle table and the memory SVCs are the real ones. No game data is used.
#include SATCHEL_SVC_SOURCE
#include <assert.h>
#include <sys/wait.h>

static module_t test_modules[1];
module_t *g_mod = test_modules;
int g_nmod;
pthread_key_t g_keytlr;
void hlecount_svc(int num) { (void)num; }
void dm_backtrace(uint64_t fp) { (void)fp; }
void guest_clock_read_wait(void) {}
uint64_t guest_ticks(void) { return 0; }
uint64_t dm_entropy64(void) { return 0; }
int dm_ue_heap_owns(uint64_t a, uint64_t n) { (void)a; (void)n; return 0; }
void gate_obs_set(int id, void (*fn)(uint64_t *, int)) { (void)id; (void)fn; }
void dm_guest_write_prepare(void *addr, size_t size) { (void)addr; (void)size; }
uint32_t svc_thread_main_init(void);

// The host session transport is the services adapter's own test; here only the routing.
#define SESSION 0xd0003u
static void *sent_message; static size_t sent_size; static unsigned sends;
int satchel_sf_svc_session(uint32_t handle) { return handle == SESSION; }
uint32_t satchel_sf_svc_send(uint32_t handle, void *message, size_t size, void *caller) {
    assert(handle == SESSION); (void)caller; sent_message = message; sent_size = size; sends++; return 0x1234;
}

enum { P = 0x1000 };
static __thread uint64_t r[31];   // per thread: the stress threads share this file's helpers
static uint64_t svc(unsigned num, uint64_t x0, uint64_t x1, uint64_t x2, uint64_t x3) {
    memset(r, 0, sizeof r); r[0] = x0; r[1] = x1; r[2] = x2; r[3] = x3; svc_dispatch(num, r, 0); return r[0];
}
static uint32_t create(uint64_t a, uint64_t n, uint32_t perm, uint32_t *h) {
    uint32_t result = (uint32_t)svc(0x15, 0xdead, a, n, perm); *h = (uint32_t)r[1];
    assert(result ? !*h : *h >= 0x1001); return result;
}
static uint32_t map(uint32_t h, uint64_t a, uint64_t n, uint32_t perm) { return (uint32_t)svc(0x51, h, a, n, perm); }
static uint32_t unmap(uint32_t h, uint64_t a, uint64_t n) { return (uint32_t)svc(0x52, h, a, n, 0); }
static void close_handle(uint32_t h) { assert(!svc(0x16, h, 0, 0, 0)); }
typedef struct { uint64_t base, size; uint32_t state, attr, perm, ipc, device, reserved; } memory_info;
static memory_info query(uint64_t a) {
    memory_info m; memset(&m, 0xa5, sizeof m);
    assert(!svc(6, (uintptr_t)&m, 0, a, 0));
    assert(m.size && a >= m.base && a - m.base < m.size);
    return m;
}
static void expect(uint64_t a, uint64_t base, uint64_t size, uint32_t state, uint32_t attr, uint32_t perm) {
    memory_info m = query(a);
    if (m.base != base || m.size != size || m.state != state || m.attr != attr || m.perm != perm) {
        fprintf(stderr, "query %#llx: %#llx+%#llx state %#x attr %u perm %u, expected %#llx+%#llx state %#x attr %u perm %u\n",
                (unsigned long long)a, (unsigned long long)m.base, (unsigned long long)m.size, m.state, m.attr, m.perm,
                (unsigned long long)base, (unsigned long long)size, state, attr, perm);
        abort();
    }
}
static int readable(uint64_t a) {
    uint8_t b; vm_size_t got = 0;
    return vm_read_overwrite(mach_task_self(), (vm_address_t)a, 1, (vm_address_t)&b, &got) == KERN_SUCCESS && got == 1;
}
static unsigned live_records(void) { unsigned n = 0; for (int i = 0; i < SATCHEL_TMEM_LIMIT; i++) n += satchel_tmem[i].live; return n; }

static void *stress(void *arg) {
    uint64_t base = (uintptr_t)arg, pg = (uint64_t)getpagesize();
    uint64_t target = (uintptr_t)g_as + ((base - (uintptr_t)g_heap) & ~(pg - 1)) + (base & (pg - 1)) + 0x4000000;
    for (int i = 0; i < 200; i++) {
        uint32_t h; assert(!create(base, 2 * P, 3, &h));
        assert(query(base).attr == 1);
        assert(!map(h, target, 2 * P, 3));
        *(volatile uint8_t *)target = (uint8_t)i; assert(*(volatile uint8_t *)base == (uint8_t)i);
        assert(!unmap(h, target, 2 * P));
        close_handle(h);
    }
    return NULL;
}

int main(void) {
    pthread_key_create(&g_keytlr, NULL);
    uint32_t main_thread = svc_thread_main_init();
    size_t pg = (size_t)getpagesize();
    assert(!svc(1, 0, 0x400000, 0, 0));   // SetHeapSize: 4 MiB
    uint64_t heap = (uintptr_t)g_heap, as = (uintptr_t)g_as, alias = (uintptr_t)g_alias, stack = (uintptr_t)g_stack;
    uint64_t end = heap + 0x400000, src = heap + 0x11000;   // not at a host page start
    assert(pg == 0x4000 || pg == P);
    uint32_t h, h2;

    // Arguments, in Ryujinx's order.
    assert(create(src + 1, P, 0, &h) == SATCHEL_K_INVALID_ADDRESS);
    assert(create(src, 0, 0, &h) == SATCHEL_K_INVALID_SIZE && create(src, P + 1, 0, &h) == SATCHEL_K_INVALID_SIZE);
    assert(create(UINT64_MAX - P + 1, 2 * P, 0, &h) == SATCHEL_K_INVALID_MEM_STATE);
    assert(create(src, P, 2, &h) == SATCHEL_K_INVALID_PERMISSION && create(src, P, 4, &h) == SATCHEL_K_INVALID_PERMISSION && create(src, P, 7, &h) == SATCHEL_K_INVALID_PERMISSION);
    // BorrowTransferMemory needs mapped heap, alias, Unreal heap or module data.
    assert(create(end, P, 0, &h) == SATCHEL_K_INVALID_MEM_STATE);
    assert(create(end - P, 2 * P, 0, &h) == SATCHEL_K_INVALID_MEM_STATE);
    assert(create(as, P, 0, &h) == SATCHEL_K_INVALID_MEM_STATE && create(stack, P, 0, &h) == SATCHEL_K_INVALID_MEM_STATE);
    assert(create(alias, P, 0, &h) == SATCHEL_K_INVALID_MEM_STATE);   // alias pages not committed
    assert(create((uintptr_t)&r & ~(uint64_t)0xfff, P, 0, &h) == SATCHEL_K_INVALID_MEM_STATE);   // host memory
    assert(!live_records());

    // A borrowed owner range: QueryMemory splits the heap and marks it Borrowed.
    memset((void *)src, 0x5a, 8 * P);
    assert(!create(src, 8 * P, 0, &h));
    expect(src + 3 * P, src, 8 * P, 5, 1, 3);
    expect(heap, heap, src - heap, 5, 0, 3);
    expect(src + 8 * P, src + 8 * P, end - src - 8 * P, 5, 0, 3);
    assert(create(src + 7 * P, 2 * P, 0, &h2) == SATCHEL_K_INVALID_MEM_STATE);   // already borrowed
    assert(svc(1, 0, 0x10000, 0, 0) == SATCHEL_K_INVALID_MEM_STATE && g_heap_cur == 0x400000);   // heap shrink over it
    assert(svc(4, stack + 0x100000, src, P, 0) == SATCHEL_K_INVALID_MEM_STATE);   // MapMemory source
    assert(svc(2, src, P, 1, 0) == SATCHEL_K_INVALID_MEM_STATE && svc(3, src, P, 0, 0) == SATCHEL_K_INVALID_MEM_STATE);
    assert(!svc(2, src + 8 * P, P, 1, 0));   // the neighbour is not borrowed
    assert(svc(0x22, 0, src, 8 * P, SESSION) == SATCHEL_K_INVALID_MEM_STATE);   // BorrowIpcBuffer refuses borrowed memory

    // A read-only owner permission is recorded and reported.
    uint32_t hr; assert(!create(src + 0x20000, 4 * P, 1, &hr));
    expect(src + 0x20000, src + 0x20000, 4 * P, 5, 1, 1);

    // MapTransferMemory: errors in Ryujinx's order.
    uint64_t target = as + 0x200000 + (src & (pg - 1));
    assert(map(h, target + 1, 8 * P, 0) == SATCHEL_K_INVALID_ADDRESS && map(h, target, 0, 0) == SATCHEL_K_INVALID_SIZE);
    assert(map(h, target, 8 * P, 2) == SATCHEL_K_INVALID_PERMISSION);
    assert(map(0x1fff, target, 8 * P, 0) == SATCHEL_K_INVALID_HANDLE && map(main_thread, target, 8 * P, 0) == SATCHEL_K_INVALID_HANDLE);
    assert(map(h, heap + 0x200000, 8 * P, 0) == SATCHEL_K_INVALID_MEM_RANGE && map(h, alias + 0x100000, 8 * P, 0) == SATCHEL_K_INVALID_MEM_RANGE);
    assert(map(h, as + g_reserve - 4 * P, 8 * P, 0) == SATCHEL_K_INVALID_MEM_RANGE);
    assert(map(h, as - 0x100000, 8 * P, 0) == SATCHEL_K_INVALID_MEM_STATE);
    assert(map(h, target, 4 * P, 0) == SATCHEL_K_INVALID_SIZE);
    assert(map(h, target, 8 * P, 3) == SATCHEL_K_INVALID_STATE);
    if (pg > P) assert(map(h, target + P, 8 * P, 0) == SATCHEL_K_INVALID_MEM_STATE);   // another host page offset
    assert(!map(h, target, 8 * P, 0));
    assert(map(h, target + 0x100000, 8 * P, 0) == SATCHEL_K_INVALID_STATE);   // already mapped
    assert(map(hr, target, 4 * P, 1) == SATCHEL_K_INVALID_MEM_STATE);          // target in use
    expect(target + 2 * P, target, 8 * P, 0x0D, 0, 3);
    memory_info below = query(target - P); assert(below.state == 0 && below.base + below.size == target);
    memory_info above = query(target + 8 * P); assert(above.state == 0 && above.base == target + 8 * P);
    // Both addresses name the same bytes.
    assert(((volatile uint8_t *)target)[5 * P] == 0x5a);
    ((volatile uint8_t *)target)[5 * P + 7] = 0x11; assert(((volatile uint8_t *)src)[5 * P + 7] == 0x11);
    ((volatile uint8_t *)src)[3] = 0x22; assert(((volatile uint8_t *)target)[3] == 0x22);
    assert(create(target, P, 0, &h2) == SATCHEL_K_INVALID_MEM_STATE);            // TransferMemory state is not borrowable

    // A read-only object maps with its own permission only, here into the stack region.
    uint64_t stack_target = stack + 0x200000 + ((src + 0x20000) & (pg - 1));
    assert(map(hr, stack_target, 4 * P, 3) == SATCHEL_K_INVALID_STATE);
    assert(!map(hr, stack_target, 4 * P, 1));
    expect(stack_target, stack_target, 4 * P, 0x0E, 0, 3);
    assert(svc(4, stack_target + 4 * P, src + 0x30000, P, 0) == SATCHEL_K_INVALID_MEM_STATE || pg == P);   // shares the alias's host page
    assert(!svc(4, stack + 0x300000, src + 0x30000, P, 0));   // an unrelated stack mapping still works
    assert(!svc(5, stack + 0x300000, src + 0x30000, P, 0));
    assert(svc(4, stack_target, src + 0x30000, P, 0) == SATCHEL_K_INVALID_MEM_STATE);

    // UnmapTransferMemory.
    assert(unmap(h, target + P, 8 * P) == SATCHEL_K_INVALID_MEM_STATE && unmap(h, target, 4 * P) == SATCHEL_K_INVALID_SIZE);
    assert(unmap(h, heap, 8 * P) == SATCHEL_K_INVALID_MEM_RANGE && unmap(0x1fff, target, 8 * P) == SATCHEL_K_INVALID_HANDLE);
    assert(!unmap(h, target, 8 * P));
    assert(!readable(target) && query(target).state == 0);
    assert(unmap(h, target, 8 * P) == SATCHEL_K_INVALID_MEM_STATE);
    assert(((volatile uint8_t *)src)[5 * P + 7] == 0x11);   // the owner keeps what the mapping wrote
    expect(src, src, 8 * P, 5, 1, 3);                        // still borrowed until its handle closes
    assert(!unmap(hr, stack_target, 4 * P));

    // Closing the handle unborrows an unmapped object.
    close_handle(h);
    expect(src, heap, src + 0x20000 - heap, 5, 0, 3);
    assert(map(h, target, 8 * P, 0) == SATCHEL_K_INVALID_HANDLE);
    assert(!svc(4, stack + 0x100000, src, P, 0) && !svc(5, stack + 0x100000, src, P, 0));
    assert(svc(0x22, 0, src, 8 * P, SESSION) == 0x1234 && sends == 1 && sent_message == (void *)src && sent_size == 8 * P);
    close_handle(hr);
    assert(!live_records());
    expect(src, heap, end - heap, 5, 0, 3);

    // Closing the last handle of a mapped object keeps the borrow and the mapping (KTransferMemory.Destroy).
    assert(!create(src, 2 * P, 3, &h));
    assert(!map(h, target, 2 * P, 3));
    close_handle(h);
    expect(target, target, 2 * P, 0x0E, 0, 3);
    expect(src, src, 2 * P, 5, 1, 3);
    assert(unmap(h, target, 2 * P) == SATCHEL_K_INVALID_HANDLE && live_records() == 1);
    satchel_tmem_reset_locked();   // the session memory reset
    assert(!live_records());
    (void)guest_release(target & ~(uint64_t)(pg - 1), (target + 2 * P + pg - 1) & ~(uint64_t)(pg - 1));

    // The application limit, then a full handle table.
    uint32_t handles[SATCHEL_TMEM_LIMIT];
    for (int i = 0; i < SATCHEL_TMEM_LIMIT; i++) assert(!create(heap + 0x100000 + (uint64_t)i * P, P, 0, &handles[i]));
    assert(create(heap + 0x300000, P, 0, &h) == SATCHEL_K_LIMIT_EXCEEDED);
    for (int i = 0; i < SATCHEL_TMEM_LIMIT; i++) close_handle(handles[i]);
    assert(!live_records());
    satchel_kernel_lock();
    uint32_t fillers[4096]; unsigned nfill = 0;
    for (;;) {
        struct { int signaled, refs; } *e = calloc(1, 8); e->refs = 1;
        uint32_t f = satchel_kernel_handle_new_locked(2 /* O_EVENT_R */, e);
        if (!f) { free(e); break; }
        fillers[nfill++] = f;
    }
    satchel_kernel_unlock();
    assert(create(src, P, 0, &h) == SATCHEL_K_HANDLE_TABLE_FULL && !live_records());
    for (unsigned i = 0; i < nfill; i++) close_handle(fillers[i]);
    assert(!create(src, P, 0, &h)); close_handle(h);

    // The other application calls.
    assert(svc(0x13, 0x1001, src + 1, P, 1) == SATCHEL_K_INVALID_ADDRESS && svc(0x13, 0x1001, src, P, 2) == SATCHEL_K_INVALID_PERMISSION);
    assert(svc(0x13, 0x1001, src, P, 1) == SATCHEL_K_INVALID_HANDLE && svc(0x13, 0x1001, src, P, 3) == SATCHEL_K_INVALID_HANDLE);
    assert(svc(0x14, 0x1001, src, 0, 0) == SATCHEL_K_INVALID_SIZE && svc(0x14, 0x1001, src, P, 0) == SATCHEL_K_INVALID_HANDLE);
    static const char sm[] = "sm:", twelve[12] = "abcdefghijkl";
    assert(svc(0x1F, 0, (uintptr_t)sm, 0, 0) == SATCHEL_K_NOT_FOUND && !r[1]);
    assert(svc(0x1F, 0, (uintptr_t)twelve, 0, 0) == SATCHEL_K_MAXIMUM_EXCEEDED);
    assert(svc(0x1F, 0, as, 0, 0) == SATCHEL_K_USER_COPY_FAILED);
    uint8_t tlr[0x200]; pthread_setspecific(g_keytlr, tlr);
    assert(svc(0x21, 0x1001, 0, 0, 0) == SATCHEL_K_INVALID_HANDLE && sends == 1);
    assert(svc(0x21, SESSION, 0, 0, 0) == 0x1234 && sends == 2 && sent_message == tlr && sent_size == 0x100);
    assert(svc(0x22, 0, src + 8, P, SESSION) == SATCHEL_K_INVALID_ADDRESS && svc(0x22, 0, src, P + 8, SESSION) == SATCHEL_K_INVALID_SIZE);
    assert(svc(0x22, 0, as, P, SESSION) == SATCHEL_K_INVALID_MEM_STATE && svc(0x22, 0, src, P, 0x1001) == SATCHEL_K_INVALID_HANDLE && sends == 2);
    assert(!svc(0x24, 0, SATCHEL_CURRENT_PROCESS, 0, 0) && r[1] == SATCHEL_APPLICATION_PID);
    assert(!svc(0x24, 0, 0xFFFF8000u, 0, 0) && r[1] == SATCHEL_APPLICATION_PID);
    assert(!svc(0x24, 0, main_thread, 0, 0) && r[1] == SATCHEL_APPLICATION_PID);
    assert(svc(0x24, 0, 0x1fff, 0, 0) == SATCHEL_K_INVALID_HANDLE && !r[1]);
    static const unsigned limits[] = {0x30, 0x31, 0x37};
    for (unsigned i = 0; i < 3; i++) assert(svc(limits[i], 0, 0, 5, 0) == SATCHEL_K_INVALID_ENUM && svc(limits[i], 0, 0x1001, 4, 0) == SATCHEL_K_INVALID_HANDLE);
    assert(!svc(0x36, 7, 0, 0, 0) && !svc(0x5F, SATCHEL_CURRENT_PROCESS, src, P, 0));

    // Concurrent owners in separate ranges, with handle reuse across threads.
    pthread_t threads[4];
    for (int i = 0; i < 4; i++) assert(!pthread_create(&threads[i], NULL, stress, (void *)(uintptr_t)(heap + 0x200000 + (uint64_t)i * 0x10000)));
    for (int i = 0; i < 4; i++) assert(!pthread_join(threads[i], NULL));
    assert(!live_records());
    puts("PASS: transfer memory create, map, unmap and close follow Ryujinx results; borrowed ranges block heap shrink, stack mappings, permission stubs and IPC buffers; aliases share bytes; limit, full table, close while mapped, reset and concurrency");
    puts("PASS: shared memory, named ports, direct session requests, process IDs, resource limits, preemption and cache flush calls");
    return 0;
}
