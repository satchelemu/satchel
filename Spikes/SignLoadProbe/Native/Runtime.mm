#import "SignerBridge.h"
#include <dlfcn.h>
#include <mach/mach.h>
#include <mach-o/dyld.h>
#include <os/proc.h>
#include <sys/sysctl.h>
#include <atomic>
#include <vector>
#include <memory>
extern "C" int csops(pid_t, unsigned int, void *, size_t);

static NSString *machError(kern_return_t kr) { return @(mach_error_string(kr)); }
static NSDictionary *failure(NSString *error) { return @{ @"status":@"fail", @"error":error }; }
static NSString *loaderError(void) { const char *s = dlerror(); return s ? @(s) : @"dlerror returned NULL"; }
NSDictionary *SPLoad(NSString *path) {
    dlerror();
    void *handle = dlopen(path.fileSystemRepresentation, RTLD_NOW | RTLD_LOCAL);
    if (!handle) return failure(loaderError());
    void *symbol = dlsym(handle, "probe_value");
    if (!symbol) { NSString *e = loaderError(); dlclose(handle); return failure(e); }
    int value = ((int (*)(void))symbol)();
    dlclose(handle);
    return @{ @"status":value == 42 ? @"pass" : @"fail", @"value":@(value), @"path":path,
              @"error":value == 42 ? @"" : @"probe_value did not return 42" };
}
NSDictionary *SPRemap(NSString *path, BOOL copy) {
    dlerror();
    void *handle = dlopen(path.fileSystemRepresentation, RTLD_NOW | RTLD_LOCAL);
    if (!handle) return failure(loaderError());
    void *symbol = dlsym(handle, "probe_page_value");
    if (!symbol) { NSString *e = loaderError(); dlclose(handle); return failure(e); }
    vm_address_t source = (vm_address_t)symbol;
    if (vm_page_size != 16384 || (source % 16384)) { dlclose(handle); return failure(@"probe_page_value is not isolated on a 16 KiB aligned page"); }
    int originalValue = ((int (*)(void))symbol)();
    vm_address_t dest = 0;
    kern_return_t kr = vm_allocate(mach_task_self(), &dest, 16384, VM_FLAGS_ANYWHERE);
    if (kr != KERN_SUCCESS) { dlclose(handle); return failure([NSString stringWithFormat:@"vm_allocate: %d %s", kr, mach_error_string(kr)]); }
    vm_address_t reserved = dest;
    vm_prot_t current = 0, maximum = 0;
    kr = vm_remap(mach_task_self(), &dest, 16384, 0, VM_FLAGS_FIXED | VM_FLAGS_OVERWRITE,
                  mach_task_self(), source, copy, &current, &maximum, VM_INHERIT_NONE);
    NSMutableDictionary *r = [@{ @"status":@"fail", @"copy":@(copy), @"kernReturn":@(kr),
        @"machError":machError(kr), @"currentProtection":@(current), @"maximumProtection":@(maximum),
        @"source":@(source), @"destination":@(dest), @"originalValue":@(originalValue) } mutableCopy];
    if (kr != KERN_SUCCESS) r[@"error"] = [NSString stringWithFormat:@"vm_remap: %d %s", kr, mach_error_string(kr)];
    else if (dest != reserved || !(current & VM_PROT_EXECUTE)) r[@"error"] = @"vm_remap did not preserve the requested address and executable protection";
    else {
        int value = ((int (*)(void))dest)();
        r[@"value"] = @(value); r[@"status"] = value == 42 ? @"pass" : @"fail";
        r[@"error"] = value == 42 ? @"" : @"remapped function did not return 42";
    }
    vm_deallocate(mach_task_self(), reserved, 16384);
    dlclose(handle);
    return r;
}
static uint64_t physicalHeadroom(kern_return_t *status) {
    vm_statistics64_data_t stats = {};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    mach_port_t host = mach_host_self();
    *status = host_statistics64(host, HOST_VM_INFO64, (host_info64_t)&stats, &count);
    mach_port_deallocate(mach_task_self(), host);
    if (*status != KERN_SUCCESS) return 0;
    return ((uint64_t)stats.free_count + stats.inactive_count) * vm_page_size;
}
static NSDictionary *footprint(void) {
    task_vm_info_data_t info = {}; mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    kern_return_t kr = task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count);
    return @{ @"kernReturn":@(kr), @"physicalFootprint":@(info.phys_footprint), @"residentSize":@(info.resident_size) };
}
NSDictionary *SPMemory(void (^progress)(NSDictionary *)) {
    const uint64_t MiB = 1024 * 1024, step = 256 * MiB, margin = 512 * MiB;
    uint64_t initial = os_proc_available_memory();
    NSMutableDictionary *r = [@{ @"status":@"running", @"availableAtStart":@(initial),
        @"physicalMemory":@(NSProcessInfo.processInfo.physicalMemory), @"margin":@(margin), @"initialFootprint":footprint() } mutableCopy];
    NSMutableArray *reservations = [NSMutableArray array];
    uint64_t largest = 0;
    for (uint64_t gib : {4, 8, 16, 32, 64}) {
        vm_address_t address = 0; uint64_t size = gib << 30;
        kern_return_t kr = vm_allocate(mach_task_self(), &address, size, VM_FLAGS_ANYWHERE);
        [reservations addObject:@{ @"GiB":@(gib), @"status":kr == KERN_SUCCESS ? @"pass" : @"fail",
            @"kernReturn":@(kr), @"error":kr == KERN_SUCCESS ? @"" : machError(kr) }];
        if (kr == KERN_SUCCESS) { largest = gib; vm_deallocate(mach_task_self(), address, size); }
    }
    r[@"reservations"] = reservations; r[@"largestReservationGiB"] = @(largest);
    progress(r);
    auto pressure = std::make_shared<std::atomic_bool>(false);
    dispatch_source_t source = dispatch_source_create(DISPATCH_SOURCE_TYPE_MEMORYPRESSURE, 0,
        DISPATCH_MEMORYPRESSURE_WARN | DISPATCH_MEMORYPRESSURE_CRITICAL, dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0));
    dispatch_source_set_event_handler(source, ^{ pressure->store(true); }); dispatch_resume(source);
    std::vector<vm_address_t> regions;
    uint64_t touched = 0, complete = 0;
    NSMutableArray *steps = [NSMutableArray array];
    NSString *stop = @"initial available-memory budget reached";
    uint64_t budget = initial > margin ? (initial - margin) / step * step : 0;
    while (touched + step <= budget) {
        kern_return_t hs;
        uint64_t physical = physicalHeadroom(&hs), available = os_proc_available_memory();
        if (hs != KERN_SUCCESS) { stop = [NSString stringWithFormat:@"host_statistics64: %d %s; stopped because system headroom could not be checked", hs, mach_error_string(hs)]; break; }
        if (pressure->load() || available < margin + step || physical < margin + step) {
            stop = @"headroom or memory-pressure guard reached before the next 256 MiB step"; break;
        }
        vm_address_t address = 0;
        kern_return_t kr = vm_allocate(mach_task_self(), &address, step, VM_FLAGS_ANYWHERE);
        if (kr != KERN_SUCCESS) { stop = [NSString stringWithFormat:@"vm_allocate touch step: %d %s", kr, mach_error_string(kr)]; break; }
        regions.push_back(address);
        uint64_t inStep = 0;
        while (inStep < step) {
            physical = physicalHeadroom(&hs); available = os_proc_available_memory();
            if (hs != KERN_SUCCESS || pressure->load() || available < margin + 32 * MiB || physical < margin + 32 * MiB) {
                stop = @"headroom or memory-pressure guard reached during a 256 MiB step"; break;
            }
            // Incompressible bytes force real page backing, including every cache line.
            arc4random_buf((void *)(address + inStep), 16 * MiB);
            inStep += 16 * MiB; touched += 16 * MiB;
        }
        if (inStep == step) complete++;
        [steps addObject:@{ @"touchedBytes":@(touched), @"availableNow":@(os_proc_available_memory()),
            @"physicalHeadroom":@(physical), @"footprint":footprint() }];
        r[@"steps"] = steps; r[@"touchedBytes"] = @(touched); progress(r);
        if (inStep < step) break;
    }
    r[@"peakFootprint"] = footprint();
    for (auto address : regions) vm_deallocate(mach_task_self(), address, step);
    dispatch_source_cancel(source);
    r[@"status"] = @"pass"; r[@"touchedBytes"] = @(touched); r[@"completed256MiBSteps"] = @(complete);
    r[@"stopReason"] = stop; r[@"availableAfterRelease"] = @(os_proc_available_memory());
    return r;
}
NSDictionary *SPProcessInfo(void) {
    uint32_t flags = 0; int rc = csops(getpid(), 0, &flags, sizeof(flags)); int savedErrno = errno;
    struct kinfo_proc proc = {}; size_t size = sizeof(proc); int mib[] = {CTL_KERN, KERN_PROC, KERN_PROC_PID, getpid()};
    int sys = sysctl(mib, 4, &proc, &size, NULL, 0);
    return @{ @"pid":@(getpid()), @"availableAtLaunch":@(os_proc_available_memory()), @"pageSize":@(vm_page_size),
        @"osVersion":NSProcessInfo.processInfo.operatingSystemVersionString,
        @"csopsReturn":@(rc), @"csopsError":rc ? @(strerror(savedErrno)) : @"", @"codeSigningFlags":@(flags),
        @"csDebugged":@((flags & 0x10000000) != 0), @"sysctlReturn":@(sys), @"ptraced":@((proc.kp_proc.p_flag & P_TRACED) != 0) };
}
BOOL SPDumpInstalledSignature(NSString *directory, NSError **error) {
    NSFileManager *fm = NSFileManager.defaultManager;
    for (NSString *name in @[@"installed-executable", @"installed.mobileprovision"]) {
        NSString *source = [name isEqualToString:@"installed-executable"] ? NSBundle.mainBundle.executablePath : [NSBundle.mainBundle pathForResource:@"embedded" ofType:@"mobileprovision"];
        NSString *destination = [directory stringByAppendingPathComponent:name];
        if ([fm fileExistsAtPath:destination]) continue;
        if (!source || ![fm copyItemAtPath:source toPath:destination error:error]) return NO;
    }
    return YES;
}
