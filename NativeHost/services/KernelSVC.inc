
// Satchel kernel objects and the remaining application supervisor calls.
// Appended to the pinned hle_svc.c by NativeHost/services/overlay.py; see the
// "Supervisor calls" section of NativeHost/services/README.md.
// Argument order, result codes and object lifetime follow Ryujinx 1.3.3
// src/Ryujinx.HLE/HOS/Kernel/SupervisorCall/Syscall.cs, Memory/KTransferMemory.cs
// and Memory/KPageTableBase.cs.
// Copyright (c) Ryujinx Team and Contributors. MIT, see RYUJINX-LICENSE.txt.
#include <mach/mach.h>

#define SATCHEL_K_INVALID_SIZE 0xCA01u        // 101
#define SATCHEL_K_INVALID_ADDRESS 0xCC01u     // 102
#define SATCHEL_K_OUT_OF_RESOURCE 0xCE01u     // 103
#define SATCHEL_K_HANDLE_TABLE_FULL 0xD201u   // 105
#define SATCHEL_K_INVALID_MEM_STATE 0xD401u   // 106
#define SATCHEL_K_INVALID_PERMISSION 0xD801u  // 108
#define SATCHEL_K_INVALID_MEM_RANGE 0xDC01u   // 110
#define SATCHEL_K_INVALID_HANDLE 0xE401u      // 114
#define SATCHEL_K_USER_COPY_FAILED 0xE601u    // 115
#define SATCHEL_K_MAXIMUM_EXCEEDED 0xEE01u    // 119
#define SATCHEL_K_INVALID_ENUM 0xF001u        // 120
#define SATCHEL_K_NOT_FOUND 0xF201u           // 121
#define SATCHEL_K_INVALID_STATE 0xFA01u       // 125
#define SATCHEL_K_LIMIT_EXCEEDED 0x10801u     // 132
#define SATCHEL_CURRENT_PROCESS 0xFFFF8001u
#define SATCHEL_APPLICATION_PID 0x51u         // Ryujinx KernelConstants.InitialProcessId: the first process created
#define SATCHEL_TMEM_LIMIT 128                // Ryujinx ProcessLoaderHelper: an application's TransferMemory limit
#define SATCHEL_O_TMEM 16                     // hle_thread.c overlay: kernel handle type of a transfer memory object

// hle_thread.c overlay: the kernel handle table, under its lock G. G is always
// taken before g_map_lock, and nothing takes G while holding g_map_lock.
void satchel_kernel_lock(void);
void satchel_kernel_unlock(void);
uint32_t satchel_kernel_handle_new_locked(int type, void *object);
void *satchel_kernel_handle_object_locked(uint32_t handle, int type);
int satchel_kernel_thread_handle(uint32_t handle);
// hle_sf.c overlay: the host's HIPC sessions, for SendSyncRequest without the SDK wrapper.
int satchel_sf_svc_session(uint32_t handle);
uint32_t satchel_sf_svc_send(uint32_t handle, void *message, size_t size, void *caller);

// One record per transfer memory. "object" means a handle still owns it and it
// counts toward the limit. The owner's range stays borrowed while the record is
// live: KTransferMemory.Destroy unborrows only an unmapped object, so closing
// the last handle of a mapped object keeps both the borrow and the mapping.
typedef struct {
    int live, object, mapped;
    uint32_t handle, perm;
    uint64_t addr, size;          // owner range (4 KiB pages)
    uint64_t map, map_lo, map_hi; // mapped guest range and the host pages that alias the owner's
} satchel_tmem_t;
static satchel_tmem_t satchel_tmem[SATCHEL_TMEM_LIMIT];

static int satchel_overlaps(uint64_t a, uint64_t n, uint64_t b, uint64_t m) { return a < b + m && b < a + n; }
static uint64_t satchel_host_mask(void) { return (uint64_t)getpagesize() - 1; }

// g_map_lock held. Any live record keeps its owner range borrowed.
static int satchel_tmem_source_busy(uint64_t addr, uint64_t size) {
    for (int i = 0; i < SATCHEL_TMEM_LIMIT; i++)
        if (satchel_tmem[i].live && satchel_overlaps(addr, size, satchel_tmem[i].addr, satchel_tmem[i].size)) return 1;
    return 0;
}
// g_map_lock held. A stack mapping whose host pages would share one with a
// transfer memory alias is refused: those host pages carry the owner's bytes.
static int satchel_tmem_mapping_busy(uint64_t addr, uint64_t size) {
    uint64_t mask = satchel_host_mask(), lo = addr & ~mask, hi = (addr + size + mask) & ~mask;
    for (int i = 0; i < SATCHEL_TMEM_LIMIT; i++)
        if (satchel_tmem[i].mapped && satchel_overlaps(lo, hi - lo, satchel_tmem[i].map_lo, satchel_tmem[i].map_hi - satchel_tmem[i].map_lo)) return 1;
    return 0;
}
// Narrow [lo, hi) around addr so it does not cross [start, end).
static void satchel_clip(uint64_t addr, unsigned __int128 *lo, unsigned __int128 *hi, uint64_t start, uint64_t end) {
    if (addr >= start && addr < end) { if (start > *lo) *lo = start; if (end < *hi) *hi = end; }
    else if (end <= addr) { if (end > *lo) *lo = end; }
    else if (start < *hi) *hi = start;
}
// g_map_lock held, after QueryMemory's own answer. A mapped range reports
// TransferMemory (0x0E) or, for permission None, TransferMemoryIsolated (0x0D),
// read and write, as MapPages maps it. The owner range reports Borrowed with the
// owner permission: None keeps the previous read and write permission.
static void satchel_tmem_query(uint64_t addr, uint64_t *base, uint64_t *size, uint64_t *state, uint32_t *attr, uint64_t *perm) {
    unsigned __int128 lo = *base, hi = (unsigned __int128)*base + *size;
    for (int i = 0; i < SATCHEL_TMEM_LIMIT; i++) {
        const satchel_tmem_t *t = &satchel_tmem[i];
        if (!t->mapped) continue;
        if (addr >= t->map && addr - t->map < t->size) {
            *base = t->map; *size = t->size; *state = t->perm ? 0x0E : 0x0D; *attr = 0; *perm = 3;
            return;
        }
        satchel_clip(addr, &lo, &hi, t->map, t->map + t->size);
    }
    for (int i = 0; i < SATCHEL_TMEM_LIMIT; i++) {
        const satchel_tmem_t *t = &satchel_tmem[i];
        if (!t->live) continue;
        if (addr >= t->addr && addr - t->addr < t->size) { *attr |= 1; *perm = t->perm ? t->perm : 3; }
        satchel_clip(addr, &lo, &hi, t->addr, t->addr + t->size);
    }
    *base = (uint64_t)lo; *size = (uint64_t)(hi - lo);
}
// Memory session reset (g_map_lock held): the address space is gone, and the
// kernel reset has already cleared every handle.
static void satchel_tmem_reset_locked(void) { memset(satchel_tmem, 0, sizeof satchel_tmem); }

// hle_thread.c CloseHandle, G held: KTransferMemory.Destroy.
void satchel_tmem_close_locked(void *object) {
    satchel_tmem_t *t = object;
    pthread_mutex_lock(&g_map_lock);
    if (t >= satchel_tmem && t < satchel_tmem + SATCHEL_TMEM_LIMIT && t->live && t->object) {
        t->object = 0; t->handle = 0;
        if (!t->mapped) memset(t, 0, sizeof *t);
    }
    pthread_mutex_unlock(&g_map_lock);
}

static uint32_t satchel_memory_arguments(uint64_t addr, uint64_t size) {
    if (addr & 0xfff) return SATCHEL_K_INVALID_ADDRESS;
    if (!size || (size & 0xfff)) return SATCHEL_K_INVALID_SIZE;
    if (addr + size <= addr) return SATCHEL_K_INVALID_MEM_STATE;
    return 0;
}
// Transfer memory permissions: None, Read or ReadAndWrite.
static int satchel_tmem_permission(uint32_t perm) { return perm == 0 || perm == 1 || perm == 3; }
// Ryujinx IsInvalidRegion / InsideHeapRegion / InsideAliasRegion for a mapping
// target. An address below the guest block cannot be free guest memory.
static uint32_t satchel_tmem_region(uint64_t addr, uint64_t size) {
    uint64_t as = (uintptr_t)g_as;
    if (addr + size - 1 > as + g_reserve - 1) return SATCHEL_K_INVALID_MEM_RANGE;
    if (satchel_overlaps(addr, size, (uintptr_t)g_heap, g_heap_cap) || satchel_overlaps(addr, size, (uintptr_t)g_alias, g_alias_cap)) return SATCHEL_K_INVALID_MEM_RANGE;
    if (addr < as) return SATCHEL_K_INVALID_MEM_STATE;
    return 0;
}
// G and g_map_lock held.
static satchel_tmem_t *satchel_tmem_lookup_locked(uint32_t handle) {
    satchel_tmem_t *t = satchel_kernel_handle_object_locked(handle, SATCHEL_O_TMEM);
    return t && t->live && t->object && t->handle == handle ? t : NULL;
}

// svcCreateTransferMemory(out handle, address, size, permission).
static uint32_t satchel_svc_create_tmem(uint64_t addr, uint64_t size, uint32_t perm, uint32_t *out) {
    uint32_t result = satchel_memory_arguments(addr, size);
    if (result) return result;
    if (!satchel_tmem_permission(perm)) return SATCHEL_K_INVALID_PERMISSION;
    satchel_kernel_lock();
    pthread_mutex_lock(&g_map_lock);
    int objects = 0; satchel_tmem_t *slot = NULL;
    for (int i = 0; i < SATCHEL_TMEM_LIMIT; i++) {
        objects += satchel_tmem[i].object;
        if (!satchel_tmem[i].live && !slot) slot = &satchel_tmem[i];
    }
    // BorrowTransferMemory: Heap, CodeMutable or ModCodeMutable, read and write,
    // no attribute. Here: the opened heap, committed alias pages, Unreal's heap
    // and loaded module data, not borrowed by a stack mapping or another object.
    if (objects >= SATCHEL_TMEM_LIMIT) result = SATCHEL_K_LIMIT_EXCEEDED;
    else if (!map_source_backed(addr, size) || map_source_busy(addr, size)) result = SATCHEL_K_INVALID_MEM_STATE;
    else if (!slot) result = SATCHEL_K_OUT_OF_RESOURCE;
    else if (!(*out = satchel_kernel_handle_new_locked(SATCHEL_O_TMEM, slot))) result = SATCHEL_K_HANDLE_TABLE_FULL;
    else *slot = (satchel_tmem_t){.live = 1, .object = 1, .handle = *out, .perm = perm, .addr = addr, .size = size};
    pthread_mutex_unlock(&g_map_lock);
    satchel_kernel_unlock();
    if (g_flag.svc) LOG("CreateTransferMemory %#llx+%#llx perm %u -> %#x handle %#x", (unsigned long long)addr, (unsigned long long)size, perm, result, result ? 0 : *out);
    return result;
}

// svcMapTransferMemory(handle, address, size, permission): the same pages at a
// second free address. Host pages are 16 KiB on the phone, so the target must
// have the owner's offset within a host page; the SDK picks another address
// after InvalidMemState, as it does for a target that is already in use.
static uint32_t satchel_svc_map_tmem(uint32_t handle, uint64_t addr, uint64_t size, uint32_t perm) {
    uint32_t result = satchel_memory_arguments(addr, size);
    if (result) return result;
    if (!satchel_tmem_permission(perm)) return SATCHEL_K_INVALID_PERMISSION;
    satchel_kernel_lock();
    pthread_mutex_lock(&g_map_lock);
    satchel_tmem_t *t = satchel_tmem_lookup_locked(handle);
    uint64_t mask = satchel_host_mask(), lo = 0, hi = 0;
    if (!t) result = SATCHEL_K_INVALID_HANDLE;
    else if ((result = satchel_tmem_region(addr, size))) {}
    else if (size != t->size) result = SATCHEL_K_INVALID_SIZE;
    else if (perm != t->perm || t->mapped) result = SATCHEL_K_INVALID_STATE;
    else if (map_find(addr, size) || satchel_tmem_mapping_busy(addr, size)) result = SATCHEL_K_INVALID_MEM_STATE;
    else if ((addr ^ t->addr) & mask) {
        static int once; if (!once++) LOG("[RED] MapTransferMemory target %#llx has a different host page offset from its owner range: InvalidMemState, the SDK picks another address", (unsigned long long)addr);
        result = SATCHEL_K_INVALID_MEM_STATE;
    } else {
        lo = addr & ~mask; hi = (addr + size + mask) & ~mask;
        // Every host page of the alias must lie in free guest memory outside the
        // heap and alias regions and share no host page with a stack mapping.
        if (satchel_tmem_region(lo, hi - lo) || lo < (uintptr_t)g_as) result = SATCHEL_K_INVALID_MEM_STATE;
        for (int i = 0; !result && i < g_nmap; i++) {
            uint64_t b = g_map[i].base & ~mask, e = (g_map[i].base + g_map[i].size + mask) & ~mask;
            if (satchel_overlaps(lo, hi - lo, b, e - b)) result = SATCHEL_K_INVALID_MEM_STATE;
        }
        if (!result) {
            vm_address_t target = (vm_address_t)lo; vm_prot_t cur, max;
            kern_return_t kr = vm_remap(mach_task_self(), &target, hi - lo, 0, VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE, mach_task_self(),
                                        (vm_address_t)(t->addr & ~mask), FALSE, &cur, &max, VM_INHERIT_NONE);
            if (kr == KERN_SUCCESS && target == lo && vm_protect(mach_task_self(), lo, hi - lo, FALSE, VM_PROT_READ | VM_PROT_WRITE) == KERN_SUCCESS) {
                t->mapped = 1; t->map = addr; t->map_lo = lo; t->map_hi = hi;
            } else {
                if (kr == KERN_SUCCESS) (void)guest_release(lo, hi);
                LOG("[RED] MapTransferMemory alias %#llx+%#llx failed (%d): OutOfResource", (unsigned long long)lo, (unsigned long long)(hi - lo), kr);
                result = SATCHEL_K_OUT_OF_RESOURCE;
            }
        }
    }
    pthread_mutex_unlock(&g_map_lock);
    satchel_kernel_unlock();
    if (g_flag.svc) LOG("MapTransferMemory %#x -> %#llx+%#llx perm %u: %#x", handle, (unsigned long long)addr, (unsigned long long)size, perm, result);
    return result;
}

// svcUnmapTransferMemory(handle, address, size).
static uint32_t satchel_svc_unmap_tmem(uint32_t handle, uint64_t addr, uint64_t size) {
    uint32_t result = satchel_memory_arguments(addr, size);
    if (result) return result;
    satchel_kernel_lock();
    pthread_mutex_lock(&g_map_lock);
    satchel_tmem_t *t = satchel_tmem_lookup_locked(handle);
    if (!t) result = SATCHEL_K_INVALID_HANDLE;
    else if ((result = satchel_tmem_region(addr, size))) {}
    else if (size != t->size) result = SATCHEL_K_INVALID_SIZE;
    else if (!t->mapped || t->map != addr) result = SATCHEL_K_INVALID_MEM_STATE;   // UnmapPages: not this object's mapping
    else {
        (void)guest_release(t->map_lo, t->map_hi);   // fresh inaccessible pages: the alias no longer reaches the owner's
        t->mapped = 0; t->map = t->map_lo = t->map_hi = 0;
    }
    pthread_mutex_unlock(&g_map_lock);
    satchel_kernel_unlock();
    return result;
}

// A guest string of at most `max` bytes, read without faulting (KernelTransfer.UserToKernelString).
static int satchel_guest_string(uint64_t addr, char *out, size_t max) {
    for (size_t i = 0; i < max; i++) {
        vm_size_t got = 0;
        if (vm_read_overwrite(mach_task_self(), (vm_address_t)(addr + i), 1, (vm_address_t)&out[i], &got) != KERN_SUCCESS || got != 1) return 0;
        if (!out[i]) return 1;
    }
    out[max] = 0; return 1;
}

// Returns 1 when the call is answered here. Register use follows the public
// Horizon ABI, as the rest of this file: inputs from x0 (x1 when x0 is an
// output pointer), w0 the Result, outputs from x1.
static int satchel_kernel_svc(int num, uint64_t *regs) {
    switch (num) {
    case 0x15: {  // CreateTransferMemory(→w1 handle; x1 address, x2 size, w3 permission)
        uint32_t handle = 0, result = satchel_svc_create_tmem(regs[1], regs[2], (uint32_t)regs[3], &handle);
        regs[0] = result; regs[1] = result ? 0 : handle; return 1;
    }
    case 0x51: regs[0] = satchel_svc_map_tmem((uint32_t)regs[0], regs[1], regs[2], (uint32_t)regs[3]); return 1;   // MapTransferMemory(w0 handle, x1 address, x2 size, w3 permission)
    case 0x52: regs[0] = satchel_svc_unmap_tmem((uint32_t)regs[0], regs[1], regs[2]); return 1;                     // UnmapTransferMemory(w0 handle, x1 address, x2 size)
    case 0x13: case 0x14: {  // MapSharedMemory(w0 handle, x1, x2, w3 permission) / UnmapSharedMemory(w0 handle, x1, x2)
        // The host gives out no shared memory objects (its services answer the SDK
        // directly), so after Ryujinx's argument checks no handle can name one.
        uint32_t result = satchel_memory_arguments(regs[1], regs[2]);
        if (!result && num == 0x13 && (((uint32_t)regs[3] | 2u) != 3u)) result = SATCHEL_K_INVALID_PERMISSION;
        regs[0] = result ? result : SATCHEL_K_INVALID_HANDLE; return 1;
    }
    case 0x1F: {  // ConnectToNamedPort(→w1 handle; x1 name)
        char name[13] = {0}; uint32_t result;
        if (!satchel_guest_string(regs[1], name, 12)) result = SATCHEL_K_USER_COPY_FAILED;
        else if (strlen(name) > 11) result = SATCHEL_K_MAXIMUM_EXCEEDED;
        else {
            // The host registers no named port: the SDK's own sm client is replaced by
            // the nn::sf::hipc hooks, so a direct connection finds nothing.
            for (char *c = name; *c; c++) if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == ':' || *c == '_' || *c == '-' || *c == '.')) *c = '?';
            static _Atomic int logged; if (atomic_fetch_add(&logged, 1) < 8) LOG("[RED] SVC 0x1f ConnectToNamedPort %s: the host has no named ports (NotFound)", name);
            result = SATCHEL_K_NOT_FOUND;
        }
        regs[0] = result; regs[1] = 0; return 1;
    }
    case 0x21: {  // SendSyncRequest(w0 session): the message is the thread's ThreadLocalRegion
        uint32_t handle = (uint32_t)regs[0]; void *tlr = pthread_getspecific(g_keytlr);
        regs[0] = tlr && satchel_sf_svc_session(handle) ? satchel_sf_svc_send(handle, tlr, 0x100, (void *)(uintptr_t)regs[30]) : SATCHEL_K_INVALID_HANDLE;
        return 1;
    }
    case 0x22: {  // SendSyncRequestWithUserBuffer(x1 message, x2 size, w3 session)
        uint64_t message = regs[1], size = regs[2]; uint32_t handle = (uint32_t)regs[3];
        uint32_t result = satchel_memory_arguments(message, size);
        if (!result) {   // BorrowIpcBuffer: mapped read and write memory without an attribute
            pthread_mutex_lock(&g_map_lock);
            if (!map_source_backed(message, size) || map_source_busy(message, size)) result = SATCHEL_K_INVALID_MEM_STATE;
            pthread_mutex_unlock(&g_map_lock);
        }
        if (!result) result = satchel_sf_svc_session(handle) ? satchel_sf_svc_send(handle, (void *)(uintptr_t)message, size, (void *)(uintptr_t)regs[30]) : SATCHEL_K_INVALID_HANDLE;
        regs[0] = result; return 1;
    }
    case 0x24: {  // GetProcessId(→x1 process ID; w1 process or thread handle)
        uint32_t handle = (uint32_t)regs[1];
        int found = handle == SATCHEL_CURRENT_PROCESS || satchel_kernel_thread_handle(handle);
        regs[0] = found ? 0 : SATCHEL_K_INVALID_HANDLE; regs[1] = found ? SATCHEL_APPLICATION_PID : 0; return 1;
    }
    case 0x30: case 0x31: case 0x37: {  // GetResourceLimit{Limit,Current,Peak}Value(→x1 value; w1 resource limit, w2 resource)
        // GetInfo(ResourceLimit) answers 0 here, so no handle names a resource limit.
        regs[0] = (uint32_t)regs[2] >= 5 ? SATCHEL_K_INVALID_ENUM : SATCHEL_K_INVALID_HANDLE; regs[1] = 0; return 1;
    }
    case 0x36: regs[0] = 0; return 1;   // SynchronizePreemptionState: host threads have no pinned preemption state to clear
    case 0x5F: regs[0] = 0; return 1;   // FlushProcessDataCache: Ryujinx answers success, host caches are coherent
    }
    return 0;
}
