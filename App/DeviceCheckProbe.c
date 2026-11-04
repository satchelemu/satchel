#include "DeviceCheckProbe.h"
#include <CoreFoundation/CoreFoundation.h>
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach/vm_map.h>
#include <os/proc.h>

// SecTask is Security SPI on iOS. Resolve it at runtime and report unknown when
// unavailable. Read the running task, never the profile's allowed entitlements.
// ABI: apple-oss-distributions/Security, sectask/SecTask.h.
static int entitlement(CFStringRef key) {
    void *library = dlopen("/System/Library/Frameworks/Security.framework/Security", RTLD_LAZY);
    if (!library) return -1;
    CFTypeRef (*create)(CFAllocatorRef) = dlsym(library, "SecTaskCreateFromSelf");
    CFTypeRef (*copy)(CFTypeRef, CFStringRef, CFErrorRef *) = dlsym(library, "SecTaskCopyValueForEntitlement");
    CFTypeRef task = create && copy ? create(NULL) : NULL;
    int result = -1;
    if (task) {
        CFErrorRef error = NULL;
        CFTypeRef value = copy(task, key, &error);
        if (!error) {
            if (!value) result = 0;
            else if (CFGetTypeID(value) == CFBooleanGetTypeID())
                result = CFBooleanGetValue((CFBooleanRef)value) ? 1 : 0;
        }
        if (value) CFRelease(value);
        if (error) CFRelease(error);
        CFRelease(task);
    }
    dlclose(library);
    return result;
}

SatchelDeviceMeasurements SatchelMeasureDevice(void) {
    SatchelDeviceMeasurements result = {0};
    result.increased_memory = entitlement(CFSTR("com.apple.developer.kernel.increased-memory-limit"));
    result.extended_addressing = entitlement(CFSTR("com.apple.developer.kernel.extended-virtual-addressing"));
    result.available_memory = os_proc_available_memory();
    task_vm_info_data_t info = {0};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count) == KERN_SUCCESS) {
        if (count >= TASK_VM_INFO_REV1_COUNT) {
            result.footprint = info.phys_footprint;
            result.has_footprint = true;
        }
        if (count >= TASK_VM_INFO_REV4_COUNT && info.limit_bytes_remaining > 0 &&
            info.phys_footprint <= UINT64_MAX - info.limit_bytes_remaining) {
            result.process_limit = info.phys_footprint + info.limit_bytes_remaining;
            result.has_limit = true;
        }
        if (count >= TASK_VM_INFO_REV2_COUNT && info.max_address > info.min_address) {
            result.address_span = info.max_address - info.min_address;
            result.has_address_span = true;
        }
    }
    // A lower bound on currently reservable contiguous VM, not physical RAM or
    // total free address space. Seven attempts at most, no reads or writes.
    for (uint64_t size = 64ULL << 30; size >= (1ULL << 30); size >>= 1) {
        vm_address_t address = 0;
        if (vm_map(mach_task_self(), &address, (vm_size_t)size, 0, VM_FLAGS_ANYWHERE,
                        MEMORY_OBJECT_NULL, 0, FALSE, VM_PROT_NONE, VM_PROT_NONE,
                        VM_INHERIT_NONE) == KERN_SUCCESS) {
            kern_return_t released = vm_deallocate(mach_task_self(), address, (vm_size_t)size);
            if (released == KERN_SUCCESS) result.reserved_bytes = size;
            break;
        }
    }
    return result;
}
