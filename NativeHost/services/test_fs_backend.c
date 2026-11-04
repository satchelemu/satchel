// Real pinned filesystem and query overlay, with synthetic mounts only.
#include <assert.h>
#include "hle_fs.c"
struct dm_flags g_flag;
uint64_t g_t0_ns;
uint64_t now_ns(void) { return 0; }
void dm_guest_write_prepare(void *a, size_t n) { (void)a; (void)n; }
module_t *g_mod; int g_nmod;
#ifndef SATCHEL_FS_EXTERNAL_GUEST
int dm_guest_as_contains(uintptr_t a) { (void)a; return 0; }
#endif
int dm_main_imports(const char *prefix) { (void)prefix; return 0; }
int dm_mii_standin(const char *dir) { (void)dir; return 0; }
const struct dm_profile *dm_profile(void) {
    static const struct dm_profile p = {.id = "synthetic", .program_id = 0x0100123400000000ull}; return &p;
}
void satchel_test_fs_setup(const char *parent) {
    char path[1024]; snprintf(path, sizeof path, "%s/fs-fixture.XXXXXX", parent); assert(mkdtemp(path));
    assert(!setenv("DM_SAVE", path, 1)); unsetenv("DM_SAVE_SYNC");
    char file[1100]; snprintf(file, sizeof file, "%s/file", path);
    int fd = open(file, O_CREAT | O_RDWR | O_EXCL, 0600); assert(fd >= 0);
    assert(write(fd, "synthetic", 9) == 9);
    struct timespec times[2] = {{1600000000,0},{1700000000,0}};
    assert(!futimens(fd, times)); close(fd);
    char link[1100]; snprintf(link, sizeof link, "%s/link", path); assert(!symlink("/", link));
}
void satchel_test_fs_queries(void) {
    uint32_t index; uint8_t bytes[32];
    assert(!satchel_host_fs_open(1, &index));
    assert(!satchel_host_fs_query(index, 14, "/file", bytes));
    int64_t times[4]; memcpy(times, bytes, 32);
    assert(times[0] > 0 && times[1] == 1700000000 && times[2] == 1600000000 && times[3] == 1);
    assert(satchel_host_fs_query(index, 14, "/absent", bytes) == 0x202);
    assert(satchel_host_fs_query(index, 14, "/../outside", bytes) == 0x202);
    assert(satchel_host_fs_query(index, 14, "/link/etc", bytes) == 0x202);
    assert(h_fs_FreeSpace((int64_t *)bytes, "absent:/") == 0x202);
    struct statfs st; assert(!statfs(getenv("DM_SAVE"), &st));
    uint64_t expected = st.f_blocks * (uint64_t)st.f_bsize, got;
    assert(!satchel_host_fs_query(index, 12, "/", bytes)); memcpy(&got, bytes, 8); assert(got == expected);
    assert(!h_fs_FreeSpace((int64_t *)bytes, "satchel_ipc_account:/")); memcpy(&got, bytes, 8);
    assert(got > (35ull << 30) && got <= expected);
    assert(!h_fs_FreeSpace((int64_t *)bytes, "satchel_ipc_account"));
    memcpy(&got, bytes, 8); assert(got > (35ull << 30) && got <= expected);
    assert(h_fs_FreeSpace((int64_t *)bytes, "absent") == 0x202);
    // Host aliases such as iOS /var must not change the volume query.
    char alias[1100]; snprintf(alias, sizeof alias, "%s/volume-alias", getenv("DM_SAVE"));
    assert(!symlink(getenv("DM_SAVE"), alias));
    assert(!satchel_fs_mount("synthetic_alias", alias, 0));
    assert(!h_fs_FreeSpace((int64_t *)bytes, "synthetic_alias"));
    memcpy(&got, bytes, 8); assert(got > (35ull << 30) && got <= expected);
    // Space belongs to the mounted filesystem, even before a save path exists.
    // Timestamp queries still require the entry, as checked above.
    assert(!satchel_host_fs_query(index, 11, "/not-created/slot", bytes));
    memcpy(&got, bytes, 8); assert(got > (35ull << 30) && got <= expected);
    assert(!satchel_host_fs_query(index, 12, "/not-created/slot", bytes));
    memcpy(&got, bytes, 8); assert(got == expected);
    assert(!h_fs_FreeSpace((int64_t *)bytes, "satchel_ipc_account:/not-created/slot"));
    assert(!h_fs_FreeSpace((int64_t *)bytes, "satchel_ipc_account:\\not-created\\slot"));
    assert(satchel_host_fs_query(index, 11, "/../outside", bytes) == 0x202);
    // A space query never follows the supplied leaf or a symlink below the root.
    assert(!satchel_host_fs_query(index, 12, "/link/etc", bytes));
    memcpy(&got, bytes, 8); assert(got == expected);
    // The factory shares the SDK transaction and observes a new pending file.
    assert(!h_fs_CreateFile("satchel_ipc_account:/pending", 7));
    assert(!satchel_host_fs_query(index, 14, "/pending", bytes));
    uint32_t again; assert(!satchel_host_fs_open(1, &again) && again == index);
    assert(!satchel_host_fs_query(again, 14, "/pending", bytes));
    // Mount exhaustion fails inside the same locked SDK operation, including
    // callers that race the IPC factory. Restore the synthetic table afterward.
    int saved_count = g_nmnt; g_nmnt = MAX_MOUNT;
    assert(satchel_host_fs_open(3, &again) == ((105u << 9) | 1u));
    assert(satchel_fs_mount("synthetic_capacity", getenv("DM_SAVE"), 0) == ((105u << 9) | 1u));
    g_nmnt = saved_count;
    puts("PASS: real mounted space/timestamps, missing paths, no symlink escape, shared save transaction");
}
