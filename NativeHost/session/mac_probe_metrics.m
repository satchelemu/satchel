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

// Query only counts, never guest memory or port names. Release the query arrays.
void satchel_probe_resources(const char *phase) {
    @autoreleasepool {
        mach_port_name_array_t names = NULL;
        mach_port_type_array_t types = NULL;
        mach_msg_type_number_t names_count = 0, types_count = 0;
        kern_return_t ports = mach_port_names(mach_task_self(), &names, &names_count,
                                             &types, &types_count);
        task_vm_info_data_t vm = {0}; mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
        kern_return_t memory = task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vm, &count);
        id<MTLDevice> device = MTLCreateSystemDefaultDevice();
        malloc_statistics_t stats = {0}; malloc_zone_statistics(NULL, &stats);
        printf("[soak-resources] phase=%s ports=%u ports_ok=%d footprint=%llu compressed=%llu metal=%llu malloc=%llu memory_ok=%d\n",
               phase, names_count, ports == KERN_SUCCESS,
               (unsigned long long)vm.phys_footprint, (unsigned long long)vm.compressed,
               (unsigned long long)device.currentAllocatedSize,
               (unsigned long long)stats.size_in_use, memory == KERN_SUCCESS);
        if (names) vm_deallocate(mach_task_self(), (vm_address_t)names, names_count * sizeof(*names));
        if (types) vm_deallocate(mach_task_self(), (vm_address_t)types, types_count * sizeof(*types));
    }
}
