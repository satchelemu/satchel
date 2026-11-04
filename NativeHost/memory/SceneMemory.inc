// Sample on the serial submission thread. Weak-table enumeration has a bounded
// autorelease pool so observing resources cannot extend their lifetime.
// Values are counts or sizes only, never guest addresses or payloads.
static void satchel_scene_memory_sample(void) { @autoreleasepool {
    const char *path = getenv("SATCHEL_SCENE_MEMORY");
    if (!path || !*path) return;
    static uint64_t previous = UINT64_MAX;
    uint64_t frame = atomic_load(&g_cur_frame);
    if (previous != UINT64_MAX && (frame >= previous && frame - previous < 60)) return;
    previous = frame;
    task_vm_info_data_t vm = {0}; mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&vm, &count) != KERN_SUCCESS) return;
    uint64_t textures = 0, buffers = 0, heaps = 0, texture_bytes = 0, buffer_bytes = 0;
    pthread_mutex_lock(&g_res_mu);
    for (id object in g_res_objects) {
        if ([object conformsToProtocol:@protocol(MTLTexture)]) { textures++; texture_bytes += [(id<MTLTexture>)object allocatedSize]; }
        else if ([object conformsToProtocol:@protocol(MTLBuffer)]) { buffers++; buffer_bytes += [(id<MTLBuffer>)object allocatedSize]; }
        else if ([object conformsToProtocol:@protocol(MTLHeap)]) heaps++;
    }
    pthread_mutex_unlock(&g_res_mu);
    uint64_t arenas = g_arenas.count, arena_bytes = 0;
    for (NvArena *arena in g_arenas) arena_bytes += arena->len;
    pthread_mutex_lock(&g_smu);
    uint64_t serials = g_by_serial.n, aliases = g_alias.n, ghosts = g_ghost.n, live = g_tex_live.n;
    pthread_mutex_unlock(&g_smu);
    pthread_mutex_lock(&g_mu);
    uint64_t pools = g_npool, dead_pools = g_dead_pools.count, dead_bytes = 0;
    for (id<MTLBuffer> buffer in g_dead_pools) dead_bytes += buffer.length;
    pthread_mutex_unlock(&g_mu);
    pthread_mutex_lock(&g_deferred_mu); uint64_t deferred = g_deferred.count; pthread_mutex_unlock(&g_deferred_mu);
    uint64_t upload_bytes = 0, upload_buffers = 0;
    for (int i = 0; i < 3; i++) {
        if (g_up_slot_blk[i]) { upload_buffers++; upload_bytes += g_up_slot_blk[i].length; }
        if (g_up_slot_over[i]) { upload_buffers++; upload_bytes += g_up_slot_over[i].length; }
    }
    os_unfair_lock_lock(&g_pbase_lock);
    uint64_t base_pipelines = g_pbase.n;
    os_unfair_lock_unlock(&g_pbase_lock);
    FILE *file = fopen(path, "a"); if (!file) return;
    if (ftell(file) == 0) fprintf(file, "frame\tfootprint_bytes\tmetal_bytes\ttextures\ttexture_bytes\tbuffers\tbuffer_bytes\theaps\tserials\taliases\tghosts\tlive_descriptors\tpools\tdead_pools\tdead_pool_bytes\tdeferred\tarenas\tarena_bytes\trender_pipelines\tcompute_pipelines\tbase_pipelines\tshaders\tupload_buffers\tupload_bytes\n");
    fprintf(file, "%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\t%llu\n",
        (unsigned long long)frame, (unsigned long long)vm.phys_footprint, (unsigned long long)g_dev.currentAllocatedSize,
        textures, texture_bytes, buffers, buffer_bytes, heaps, serials, aliases, ghosts, live, pools, dead_pools, dead_bytes, deferred,
        arenas, arena_bytes, (uint64_t)(g_pso.n + g_pso_hy.n + g_pso_mesh.n), (uint64_t)g_cps.n, base_pipelines, (uint64_t)g_shcache.count, upload_buffers, upload_bytes);
    fclose(file);
} }
