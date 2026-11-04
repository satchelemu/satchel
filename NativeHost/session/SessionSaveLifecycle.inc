// Lifecycle flushing never promotes an unfinished game transaction.
static int satchel_sync_tree(const char *path) {
    DIR *dir = opendir(path);
    if (!dir) return errno;
    int error = 0; struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char child[4096]; struct stat st;
        if (snprintf(child, sizeof child, "%s/%s", path, entry->d_name) >= sizeof child) { error = ENAMETOOLONG; break; }
        if (lstat(child, &st)) { error = errno; break; }
        if (S_ISDIR(st.st_mode)) error = satchel_sync_tree(child);
        else if (S_ISREG(st.st_mode)) {
            int fd = open(child, O_RDONLY | O_NOFOLLOW);
            if (fd < 0) error = errno;
            else { if (fsync(fd)) error = errno; close(fd); }
        }
        if (error) break;
    }
    if (!error && fsync(dirfd(dir))) error = errno;
    closedir(dir); return error;
}
int satchel_fs_lifecycle_flush(void) {
    PATH_LOCK(); pthread_mutex_lock(&g_lock);
    int error = 0, count = 0;
    for (int i = 0; i < g_nmnt; i++) if (g_mnt[i].commit[0]) {
        int duplicate = 0;
        for (int j = 0; j < i; j++) if (!strcmp(g_mnt[i].commit, g_mnt[j].commit)) duplicate = 1;
        if (duplicate) continue;
        int result = satchel_sync_tree(g_mnt[i].commit); count++;
        if (!error) error = result;
    }
    pthread_mutex_unlock(&g_lock);
    LOG("Satchel lifecycle: save flush mounts=%d error=%d", count, error);
    return error;
}
unsigned satchel_fs_memory_pressure(void) {
    PATH_LOCK(); unsigned count = 0;
    for (unsigned i = 0; i < DIR_INDEX_SLOTS; i++) {
        while (g_dir_index[i]) {
            dir_index *index = g_dir_index[i]; g_dir_index[i] = index->next;
            free(index->path); free(index->e); free(index->pool); free(index); count++;
        }
    }
    return count;
}
