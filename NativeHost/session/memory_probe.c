#include "hle_svc.c"
#include <assert.h>
#include <mach/mach_vm.h>
void nvn_dirty_add_range(const void *, size_t);
void nvn_dirty_protect(const void *, size_t);
void nvn_dirty_remove_range(const void *);

void hlecount_svc(int n) { (void)n; }
void dm_backtrace(uint64_t p) { (void)p; }
void gate_obs_set(int id, void (*fn)(uint64_t *, int)) { (void)id; (void)fn; }
int dm_ue_heap_owns(uint64_t a, uint64_t n) { (void)a; (void)n; return 0; }

void owners_memory_start(int title) {
    uint64_t r[100] = {0, 64 * 1024 * 1024};
    svc_dispatch(0x01, r, 0); assert(!r[0]);
    assert(g_reserve == (title ? UINT64_C(0x120000000) : UINT64_C(0x140000000)));
    assert(g_heap_cur == 64 * 1024 * 1024);
    for (size_t i = 0; i < g_heap_cur; i += getpagesize()) assert(!g_heap[i]);
    memset(g_heap, 0x7b, g_heap_cur);
    r[0] = (uintptr_t)g_alias; r[1] = 16 * 1024 * 1024;
    svc_dispatch(0x2c, r, 0); assert(!r[0]);
    for (size_t i = 0; i < 16 * 1024 * 1024; i += getpagesize()) assert(!g_alias[i]);
    memset(g_alias, 0x6a, 16 * 1024 * 1024);
    r[0] = (uintptr_t)g_stack; r[1] = (uintptr_t)g_heap; r[2] = 4 * 1024 * 1024;
    svc_dispatch(0x04, r, 0); assert(!r[0]);
    assert(g_nmap == 1 && g_stack[0] == 0x7b);
    // A remaining NVN dirty-page borrower must veto arena release.
    nvn_dirty_add_range(g_alias, 16 * 1024 * 1024);
    nvn_dirty_protect(g_alias, 16 * 1024 * 1024);
    assert(satchel_memory_session_reset() == EBUSY);
}

void owners_memory_verify(void) {
    assert(!satchel_memory_join(2000));
    uintptr_t address = (uintptr_t)g_as;
    size_t bytes = g_reserve;
    assert(satchel_memory_session_reset() == EBUSY && g_as);
    nvn_dirty_remove_range(g_alias);
    assert(!satchel_memory_session_reset());
    assert(!g_as && !g_reserve && !g_heap && !g_alias && !g_stack);
    assert(!g_alias_bits && !g_alias_exposed && !g_rgn && !g_nmap);
    assert(!dm_guest_as_contains(address));
    // Darwin mincore succeeds for an unmapped page and sets a vector flag.
    // Query VM mappings instead, checking the entire former reservation.
    mach_vm_address_t region = address; mach_vm_size_t size = 0;
    vm_region_basic_info_data_64_t info;
    mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
    mach_port_t object = MACH_PORT_NULL;
    kern_return_t rc = mach_vm_region(mach_task_self(), &region, &size, VM_REGION_BASIC_INFO_64,
                                     (vm_region_info_t)&info, &count, &object);
    if (object) mach_port_deallocate(mach_task_self(), object);
    assert(rc == KERN_INVALID_ADDRESS || (rc == KERN_SUCCESS && region >= address + bytes));
}
