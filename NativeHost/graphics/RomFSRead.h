#pragma once
// Optional read-only RomFS windows. The image index and identity live until all
// guest threads have joined. Thread destruction releases its window. Reopened
// images get a new identity even when malloc and the fd table reuse old values.
// There is no global I/O lock and no heap staging copy. Large reads and mapping
// failures retain the pread path. The default remains pread until phone A/B.
#include <sys/mman.h>
#include <mach/mach.h>
#include <pthread.h>
#include <stdatomic.h>
#define SATCHEL_ROMFS_WINDOW (4u * 1024u * 1024u)
#define SATCHEL_ROMFS_READERS 64u
typedef struct { void *map; size_t length; uint64_t offset, identity; } satchel_romfs_reader;
static pthread_key_t satchel_romfs_key;
static pthread_once_t satchel_romfs_once = PTHREAD_ONCE_INIT;
static _Atomic unsigned satchel_romfs_readers;
static int satchel_romfs_mapped, satchel_romfs_key_ok;
static void satchel_romfs_destroy(void *p) {
    satchel_romfs_reader *r = p;
    if (!r) return;
    if (r->map) munmap(r->map, r->length);
    free(r); atomic_fetch_sub(&satchel_romfs_readers, 1);
}
static void satchel_romfs_init(void) {
    const char *e = getenv("DM_ROMFS_MAPPED");
    satchel_romfs_mapped = e && !strcmp(e, "1");
    if (satchel_romfs_mapped) satchel_romfs_key_ok = pthread_key_create(&satchel_romfs_key, satchel_romfs_destroy) == 0;
}
static int satchel_romfs_copy(const romfs_img *image, uint64_t offset, void *destination, size_t size) {
    pthread_once(&satchel_romfs_once, satchel_romfs_init);
    if (!satchel_romfs_mapped || !satchel_romfs_key_ok || !size || size > SATCHEL_ROMFS_WINDOW ||
        offset > image->loading_size || size > image->loading_size - offset) return 0;
    satchel_romfs_reader *r = pthread_getspecific(satchel_romfs_key);
    if (!r) {
        if (atomic_fetch_add(&satchel_romfs_readers, 1) >= SATCHEL_ROMFS_READERS) {
            atomic_fetch_sub(&satchel_romfs_readers, 1); return 0;
        }
        r = calloc(1, sizeof *r);
        if (!r) { atomic_fetch_sub(&satchel_romfs_readers, 1); return 0; }
        if (pthread_setspecific(satchel_romfs_key, r)) { satchel_romfs_destroy(r); return 0; }
    }
    if (!r->map || r->identity != image->loading_id || offset < r->offset ||
        offset - r->offset > r->length || size > r->length - (offset - r->offset)) {
        static satchel_pause_stat map_stat; SATCHEL_PAUSE_SCOPE(&map_stat, "ReadMap", 7);
        if (r->map) { munmap(r->map, r->length); r->map = NULL; }
        uint64_t page = (uint64_t)getpagesize();
        r->offset = offset & ~(page - 1);
        uint64_t remaining = image->loading_size - r->offset;
        r->length = remaining < SATCHEL_ROMFS_WINDOW ? (size_t)remaining : SATCHEL_ROMFS_WINDOW;
        if (size > r->length - (offset - r->offset)) return 0;
        void *map = mmap(NULL, r->length, PROT_READ, MAP_PRIVATE, image->fd, (off_t)r->offset);
        if (map == MAP_FAILED) return 0;
        r->map = map; r->identity = image->loading_id;
    }
    static satchel_pause_stat copy_stat; SATCHEL_PAUSE_SCOPE(&copy_stat, "ReadCopy", 6); satchel_scope.bytes = size;
    // A kernel copy preserves ReadFile's failure behavior for invalid or newly
    // write-protected destinations. On failure the original pread retry/error
    // path runs. Plain memcpy here would turn EFAULT into a guest process crash.
    vm_size_t copied = 0;
    kern_return_t result = vm_read_overwrite(mach_task_self(),
        (vm_address_t)((const uint8_t *)r->map + (offset - r->offset)),
        size, (vm_address_t)destination, &copied);
    return result == KERN_SUCCESS && copied == size;
}
