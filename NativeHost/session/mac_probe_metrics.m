#import <Metal/Metal.h>
#include <malloc/malloc.h>
#include <mach/mach.h>
#include <stdio.h>
void satchel_probe_memory(void) {
    @autoreleasepool {
        malloc_statistics_t stats = {0}; malloc_zone_statistics(NULL, &stats);
        task_vm_info_data_t vm = {0}; mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
        task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vm, &count);
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        printf("[native-memory] malloc_used=%llu malloc_allocated=%llu metal=%llu internal=%llu external=%llu reusable=%llu compressed=%llu footprint=%llu\n",
            (unsigned long long)stats.size_in_use, (unsigned long long)stats.size_allocated,
            (unsigned long long)device.currentAllocatedSize, (unsigned long long)vm.internal,
            (unsigned long long)vm.external, (unsigned long long)vm.reusable,
            (unsigned long long)vm.compressed, (unsigned long long)vm.phys_footprint);
    }
}
