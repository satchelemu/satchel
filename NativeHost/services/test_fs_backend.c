// Real pinned filesystem and query overlay, with synthetic mounts only.
#include <assert.h>
#include <unistd.h>
static int satchel_test_fsync(int fd);
#define fsync satchel_test_fsync
#include "hle_fs.c"
#undef fsync
static unsigned flush_calls, flush_interrupts;
static int flush_error;
static int satchel_test_fsync(int fd) {
    flush_calls++;
    if (flush_interrupts) { flush_interrupts--; errno = EINTR; return -1; }
    if (flush_error) { errno = flush_error; return -1; }
    return fsync(fd);
}
static void satchel_test_fs_flush(void);
struct dm_flags g_flag;
uint64_t g_t0_ns;
uint64_t now_ns(void) { return 0; }
void dm_guest_write_prepare(void *a, size_t n) { (void)a; (void)n; }
module_t *g_mod; int g_nmod;
#ifndef SATCHEL_FS_EXTERNAL_GUEST
int dm_guest_as_contains(uintptr_t a) { (void)a; return 0; }
#endif
int dm_game_imports(const char *prefix) { (void)prefix; return 0; }
void dm_thread_service_request(void) {} // no guest thread gives up a core in this fixture
void *guest_sym(const char *name) { (void)name; return NULL; } // no SDK module: Unmount takes the host path
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
    snprintf(file, sizeof file, "%s/flush-file", path);
    fd = open(file, O_CREAT | O_RDWR | O_EXCL, 0600); assert(fd >= 0);
    assert(write(fd, "synthetic", 9) == 9); close(fd);
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
    satchel_test_fs_flush();
    puts("PASS: real mounted space/timestamps, missing paths, no symlink escape, shared save transaction");
}

static void satchel_test_fs_flush(void) {
    const char *path = "satchel_ipc_account:/flush-file";
    uint64_t handle; unsigned before = flush_calls;
    assert(h_fs_FlushFile(0) == RES_FAIL);
    assert(h_fs_FlushFile(FH_OUT(65536)) == RES_FAIL);
    assert(!h_fs_OpenFile(&handle, path, MODE_READ));
    assert(!h_fs_FlushFile(handle) && flush_calls == before);
    h_fs_CloseFile(handle);
    assert(h_fs_FlushFile(handle) == RES_FAIL);
    // Immutable image files have no writable backing buffers. Validate image
    // handles before inspecting an entry, and never sync the shared input fd.
    unsigned image_mount = g_nmnt++;
    img_ent entry = {.type = 1}; romfs_img image = {.n = 1, .e = &entry};
    g_mnt[image_mount].img = &image;
    assert(!h_fs_FlushFile(FH_OUT(IMG_HANDLE(image_mount, 0))));
    assert(h_fs_FlushFile(FH_OUT(IMG_HANDLE(image_mount, 1))) == RES_FAIL);
    assert(h_fs_FlushFile(FH_OUT(IMG_HANDLE(MAX_MOUNT, 0))) == RES_FAIL);
    entry.type = 0;
    assert(h_fs_FlushFile(FH_OUT(IMG_HANDLE(image_mount, 0))) == RES_FAIL);
    g_mnt[image_mount].img = NULL;
    assert(h_fs_FlushFile(FH_OUT(IMG_HANDLE(image_mount, 0))) == RES_FAIL);
    g_nmnt--; assert(flush_calls == before);

    char committed[1100], bytes[9];
    snprintf(committed, sizeof committed, "%s/flush-file", getenv("DM_SAVE"));
    int original = open(committed, O_RDONLY); assert(original > 0);
    assert(h_fs_FlushFile(FH_OUT(original)) == RES_FAIL);
    assert(read(original, bytes, sizeof bytes) == 9 && !memcmp(bytes, "synthetic", 9));
    close(original);
    assert(!h_fs_OpenFile(&handle, path, MODE_READ | MODE_WRITE));
    assert(!h_fs_WriteFile(handle, 0, "modified!", 9, NULL));
    assert(!h_fs_FlushFile(handle) && flush_calls == before + 1);
    original = open(committed, O_RDONLY); assert(original > 0);
    assert(read(original, bytes, sizeof bytes) == 9 && !memcmp(bytes, "synthetic", 9));
    close(original); // Flush must not publish the transaction.
    int txn = atomic_load(&g_ftxn[FH_IN(handle)]) - 1;
    assert(txn >= 0 && !atomic_load(&g_mnt[txn].poison));
    flush_error = EIO;
    assert(h_fs_FlushFile(handle) == RES_FAIL);
    assert(atomic_load(&g_mnt[txn].poison) == EIO);
    flush_error = 0; h_fs_CloseFile(handle);
    assert(h_fs_Commit("satchel_ipc_account") == RES_FAIL);
    original = open(committed, O_RDONLY); assert(original > 0);
    assert(read(original, bytes, sizeof bytes) == 9 && !memcmp(bytes, "synthetic", 9));
    close(original);
    // A new complete transaction recovers. EINTR is retried before success.
    assert(!h_fs_OpenFile(&handle, path, MODE_READ | MODE_WRITE));
    size_t got = 0;
    assert(!h_fs_ReadFile(&got, handle, 0, bytes, 9) && got == 9 && !memcmp(bytes, "synthetic", 9));
    assert(!h_fs_WriteFile(handle, 0, "modified!", 9, NULL));
    before = flush_calls; flush_interrupts = 1;
    assert(!h_fs_FlushFile(handle) && flush_calls == before + 2);
    h_fs_CloseFile(handle);
    assert(!h_fs_Commit("satchel_ipc_account"));
    original = open(committed, O_RDONLY); assert(original > 0);
    assert(read(original, bytes, sizeof bytes) == 9 && !memcmp(bytes, "modified!", 9));
    close(original);
    puts("PASS: Flush delegates fsync, retries EINTR, validates handles, preserves committed saves on failure and recovers");
}
