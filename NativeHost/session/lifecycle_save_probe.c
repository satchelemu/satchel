// Include the pinned MIT transaction fixture to retain its real FS and helpers.
// Only the synthetic crash points below are new.
#include <signal.h>
#include <assert.h>
static int crash_point;
#define renamex_np lifecycle_swap
#define main upstream_transaction_fixture
#include "tests/t_fs_commit_list.c"
#undef main
#undef renamex_np
int renamex_np(const char *, const char *, unsigned);
int lifecycle_swap(const char *a, const char *b, unsigned flags) {
    if (crash_point == 1) raise(SIGSTOP);
    int result = renamex_np(a, b, flags);
    if (!result && crash_point == 2) raise(SIGSTOP);
    return result;
}
int main(int argc, char **argv) {
    assert(argc == 4); setenv("DM_SAVE", argv[2], 1);
    assert(!h_fs_MountSave("save", NULL));
    char value[64];
    if (!strcmp(argv[1], "verify")) {
        assert(dir_index_get(argv[2]));
        assert(satchel_fs_memory_pressure() > 0);
        assert(read_save("save:/a", value, sizeof value) && !strcmp(value, argv[3]));
        assert(read_save("save:/b", value, sizeof value) && !strcmp(value, argv[3]));
        puts("PASS: fresh mount recovered one complete committed save"); return 0;
    }
    assert(!write_save("save:/a", "new")); assert(!write_save("save:/b", "new"));
    assert(!satchel_fs_lifecycle_flush());
    char committed[4096]; snprintf(committed, sizeof committed, "%s/a", argv[2]);
    assert(same(committed, "old"));
    crash_point = atoi(argv[3]);
    if (!crash_point) raise(SIGSTOP);
    assert(!h_fs_Commit("save"));
    raise(SIGSTOP); return 2;
}
