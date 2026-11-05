// Synthetic check of the session-scoped cloud save sender (save_sync_overlay.py over the
// pinned engine's save_sync.c). A plain folder stands in for iCloud Drive; the engine's
// weak plain-copy defaults move the file. No game, keys or real saves.
#include "save_sync.c"
#include <assert.h>
#include <stdlib.h>

static void put(const char *path, const char *text) { FILE *f = fopen(path, "w"); assert(f); fputs(text, f); assert(!fclose(f)); }
static void folder(const char *path) { char c[PATH_MAX]; snprintf(c, sizeof c, "%s", path); assert(!mkdirs(c)); }
static void cloud_meta(const char *root, const char *title, ss_meta *m) {
    char path[PATH_MAX]; snprintf(path, sizeof path, "%s/%s/save.dmsync", root, title);
    memset(m, 0, sizeof *m); assert(!ss_read(path, NULL, m));
}

int main(int argc, char **argv) {
    assert(argc == 2);
    char cloud[PATH_MAX], a[PATH_MAX], b[PATH_MAX], file[PATH_MAX];
    snprintf(cloud, sizeof cloud, "%s/cloud", argv[1]); folder(cloud);
    snprintf(a, sizeof a, "%s/titlea/save", argv[1]); folder(a);
    snprintf(b, sizeof b, "%s/titleb/save", argv[1]); folder(b);
    snprintf(file, sizeof file, "%s/slot0.bin", a); put(file, "first title, first save");
    snprintf(file, sizeof file, "%s/slot0.bin", b); put(file, "second title, first save");

    // Off: "0" never starts a sender and the stop is a no-op.
    setenv("DM_SAVE_SYNC", "0", 1);
    save_sync_mount(a, "FIXTURE-A");
    assert(!atomic_load(&g_on) && satchel_save_sync_session_stop() == 0 && !g_mounted);

    // Session 1: this device's save is not in the cloud yet, so it goes out by the stop.
    setenv("DM_SAVE_SYNC", cloud, 1);
    save_sync_mount(a, "FIXTURE-A");
    assert(atomic_load(&g_on) && g_mounted);
    assert(satchel_save_sync_session_stop() == 0);
    assert(!atomic_load(&g_on) && !g_mounted && !g_commits && !g_sent && !g_save[0]);
    ss_meta first; cloud_meta(cloud, "FIXTURE-A", &first);
    assert(first.files == 1 && !strcmp(first.title, "FIXTURE-A"));

    // Session 2 in the same process, another title: the mount runs again and a Commit is sent at stop.
    save_sync_mount(b, "FIXTURE-B");
    assert(atomic_load(&g_on) && !strcmp(g_title, "FIXTURE-B"));
    snprintf(file, sizeof file, "%s/slot1.bin", b); put(file, "second title, second save");
    save_sync_committed();
    assert(satchel_save_sync_session_stop() == 0 && !atomic_load(&g_on));
    ss_meta second; cloud_meta(cloud, "FIXTURE-B", &second);
    assert(second.files == 2 && !strcmp(second.title, "FIXTURE-B"));

    // Session 3: the first title again, unchanged, is in step and sends nothing new.
    save_sync_mount(a, "FIXTURE-A");
    assert(atomic_load(&g_on) && !g_want);
    assert(satchel_save_sync_session_stop() == 0);
    ss_meta again; cloud_meta(cloud, "FIXTURE-A", &again);
    assert(!strcmp(again.hash, first.hash));

    // Session 4: another device replaced the cloud's copy; it comes in at mount and this
    // device's previous save is kept in the engine's own backups beside the save.
    char other[PATH_MAX], packed[PATH_MAX];
    snprintf(other, sizeof other, "%s/other/save", argv[1]); folder(other);
    snprintf(file, sizeof file, "%s/slot0.bin", other); put(file, "first title, from another device");
    snprintf(packed, sizeof packed, "%s/FIXTURE-A/save.dmsync", cloud);
    ss_meta theirs; assert(!ss_pack(other, "FIXTURE-A", packed, &theirs));
    save_sync_mount(a, "FIXTURE-A");
    snprintf(file, sizeof file, "%s/slot0.bin", a);
    char text[64] = {0}; FILE *f = fopen(file, "r"); assert(f && fread(text, 1, sizeof text - 1, f) > 0); fclose(f);
    assert(!strcmp(text, "first title, from another device"));
    char backups[PATH_MAX]; snprintf(backups, sizeof backups, "%s-backups", a);
    assert(has_entries(backups));
    assert(satchel_save_sync_session_stop() == 0 && satchel_save_sync_session_stop() == 0);
    puts("PASS: session-scoped cloud save sync: off, send at stop, same-process remount, in step, pull with backup");
    return 0;
}
