// Filesystem query behavior adapted from Ryujinx 1.3.3, MIT.
// See RYUJINX-LICENSE.txt. Uses the existing SDK mounts and save transactions.
#include <sys/mount.h>
static uint32_t satchel_fs_error(void) {
    return errno == ENOENT || errno == ENOTDIR || errno == ELOOP ? 0x202u : ((6001u << 9) | 2u);
}
// Open each resolved physical component below its mount without following links.
// No guest path, metadata or host path is logged by these queries.
static int satchel_fs_open_beneath(const char *root, const char *physical) {
    size_t length = strlen(root);
    while (length > 1 && root[length - 1] == '/') length--;
    if (strncmp(root, physical, length) || (physical[length] && physical[length] != '/')) { errno = ENOENT; return -1; }
    int fd = open(root, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) return -1;
    const char *p = physical + length;
    while (*p) {
        while (*p == '/') p++;
        if (!*p) break;
        const char *end = strchr(p, '/'); size_t n = end ? (size_t)(end - p) : strlen(p);
        char component[0x301];
        if (!n || n >= sizeof component || (n == 1 && p[0] == '.') || (n == 2 && p[0] == '.' && p[1] == '.')) { close(fd); errno = ENOENT; return -1; }
        memcpy(component, p, n); component[n] = 0;
        int next = openat(fd, component, O_RDONLY | O_NOFOLLOW | O_CLOEXEC | (end ? O_DIRECTORY : 0));
        int error = errno; close(fd); errno = error;
        if (next < 0) return -1;
        fd = next; p += n;
    }
    return fd;
}
static uint32_t satchel_fs_query_path(unsigned command, const char *path, uint8_t *out) {
    PATH_LOCK();
    char forward[1024]; path = fwd_slashes(path, forward, sizeof forward);
    const char *colon = path ? strchr(path, ':') : NULL;
    if (!colon) return 0x202u;
    int mi = -1;
    for (int i = 0; i < g_nmnt; i++)
        if (strlen(g_mnt[i].name) == (size_t)(colon - path) && !strncmp(g_mnt[i].name, path, colon - path)) { mi = i; break; }
    if (mi < 0) return 0x202u;
    char normalized[0x301];
    if ((colon[1] && colon[1] != '/') || strchr(colon + 1, ':')) return ((6001u << 9) | 2u);
    if (!norm_rest(colon + 1, normalized, sizeof normalized)) return satchel_fs_error();
    // RomFS has no writable space and no source timestamps in the package index.
    // Do not substitute the enclosing host volume or fabricate timestamps.
    if (g_mnt[mi].img) return ((6300u << 9) | 2u);
    int ro = g_mnt[mi].ro, fd;
    if (command == 14) {
        char physical[1024];
        if (!host_path_ex(path, physical, sizeof physical, &ro, 0)) return satchel_fs_error();
        fd = satchel_fs_open_beneath(g_mnt[mi].dir, physical);
    } else {
        // Space is a property of the mounted filesystem. A caller may query it
        // before creating a save file or directory. Never resolve the leaf here.
        fd = open(g_mnt[mi].dir, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    }
    if (fd < 0) return satchel_fs_error();
    uint32_t result = 0;
    if (command == 14) {
        struct stat st;
        if (fstat(fd, &st)) result = satchel_fs_error();
        else {
            int64_t times[4] = {st.st_birthtimespec.tv_sec, st.st_mtimespec.tv_sec, st.st_atimespec.tv_sec, 1};
            memcpy(out, times, sizeof times);
        }
    } else {
        struct statfs st;
        if (fstatfs(fd, &st)) result = satchel_fs_error();
        else {
            uint64_t blocks = command == 11 ? st.f_bavail : st.f_blocks;
            uint64_t size = blocks > INT64_MAX / (uint64_t)st.f_bsize ? INT64_MAX : blocks * st.f_bsize;
            if (ro && command == 11) size = 0;
            memcpy(out, &size, 8);
        }
    }
    close(fd); return result;
}
// The SDK accepts a registered mount name. IPC already supplies a filesystem
// object and a path relative to it. Keep that distinction at the SDK boundary.
static uint32_t satchel_fs_sdk_free_space(int64_t *out, const char *name) {
    char full[0x340]; const char *path = name;
    int bare = name && !strchr(name, ':');
    if (bare) {
        if (snprintf(full, sizeof full, "%s:/", name) >= (int)sizeof full) return ((6001u << 9) | 2u);
        path = full;
    }
    uint8_t bytes[8]; uint32_t result = satchel_fs_query_path(11, path, bytes);
    if (!result && out) memcpy(out, bytes, 8);
    satchel_round8_trace(bare ? "SDK.GetFreeSpaceSize.mount-name" : "SDK.GetFreeSpaceSize.path", 11, result);
    return result;
}
uint32_t satchel_host_fs_open(unsigned type, uint32_t *index) {
    const char *save = getenv("DM_SAVE");
    struct stat st;
    if (!save || !*save || stat(save, &st) || !S_ISDIR(st.st_mode)) return 0x7d402u;
    const char *name = type == 1 ? "satchel_ipc_account" : "satchel_ipc_device";
    // The patched SDK mount checks capacity under its existing locks.
    uint32_t result = type == 1 ? h_fs_MountSave(name, NULL) : h_fs_MountDeviceSave(name);
    if (result) return result;
    PATH_LOCK();
    for (int i = 0; i < g_nmnt; i++) if (!strcmp(g_mnt[i].name, name)) { *index = (uint32_t)i; return 0; }
    return 0x7d402u;
}
uint32_t satchel_host_fs_query(uint32_t index, unsigned command, const char *path, uint8_t *out) {
    char full[0x340];
    { PATH_LOCK();
      if (index >= (unsigned)g_nmnt) return 0x7d402u;
      if (snprintf(full, sizeof full, "%s:%s", g_mnt[index].name, path) >= (int)sizeof full) return ((6001u << 9) | 2u);
    }
    return satchel_fs_query_path(command, full, out);
}
