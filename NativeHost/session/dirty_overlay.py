"""Close fault-handler admission before freeing dirty tracking descriptors."""
from pathlib import Path
import runpy
HERE = Path(__file__).resolve().parent
replace = runpy.run_path(str(HERE / 'owners_overlay.py'))['replace']


def prepare(source, destination):
    text = '#include "SessionBoundary.h"\n#include <errno.h>\n' + source.read_text()
    text = replace(text, 'static int on_fault(void *addr, int is_write, void *uc) {',
                   'static _Atomic unsigned satchel_dirty_readers;\nstatic _Atomic int satchel_dirty_closed;\nstatic int satchel_on_fault(void *addr, int is_write, void *uc) {')
    text = replace(text, '// Unprotect pages p..q of a range', '''static int on_fault(void *addr, int is_write, void *uc) {
    atomic_fetch_add(&satchel_dirty_readers, 1);
    int result = atomic_load(&satchel_dirty_closed) ? 0 : satchel_on_fault(addr, is_write, uc);
    atomic_fetch_sub(&satchel_dirty_readers, 1);
    return result;
}
// Unprotect pages p..q of a range''')
    text = replace(text, 'void nvn_dirty_add_range(const void *mem, size_t size) {', '''typedef struct SatchelDirtyRetired {
    void *state, *gen;
    struct SatchelDirtyRetired *next;
} SatchelDirtyRetired;
static SatchelDirtyRetired *satchel_dirty_retired;
void nvn_dirty_add_range(const void *mem, size_t size) {''')
    text = replace(text, '        // The old array is not freed: the signal handler may still have a very brief stale reader; leak = 9 bytes per page.', '''        // Keep replaced arrays alive for stale fault readers. The session
        // coordinator frees them only after all readers and workers retire.
        if (r->state || r->gen) {
            SatchelDirtyRetired *old = malloc(sizeof *old);
            if (!old) die("dirty-page retired table allocation failed");
            *old = (SatchelDirtyRetired){(void *)r->state, (void *)r->gen, satchel_dirty_retired};
            satchel_dirty_retired = old;
        }''')
    text += '''
int satchel_dirty_session_reset(void) {
    if (!satchel_session_quiescent()) return EBUSY;
    atomic_store(&satchel_dirty_closed, 1);
    // The fault hook remains installed. New calls cannot read the old table.
    // Retry rather than waiting indefinitely for a handler already executing.
    if (atomic_load(&satchel_dirty_readers)) return EBUSY;
    pthread_mutex_lock(&g_slot_mu);
    for (int i = 0; i < g_nr; i++) {
        range_t *r = rg(i);
        if (r->live && mprotect((void *)r->base, r->end - r->base, PROT_READ | PROT_WRITE)) {
            int result = errno; pthread_mutex_unlock(&g_slot_mu); return result;
        }
    }
    for (int b = 0; b < NBLK; b++) {
        range_t *block = atomic_load(&g_blk[b]); if (!block) continue;
        for (int i = 0; i < BLK; i++) { free(block[i].state); free(block[i].gen); free((void *)atomic_load(&block[i].sparse)); }   // sparse: the placement sparse tile pages of the f8a5618d pin
        free(block); atomic_store(&g_blk[b], NULL);
    }
    while (satchel_dirty_retired) {
        SatchelDirtyRetired *old = satchel_dirty_retired;
        satchel_dirty_retired = old->next;
        free(old->state); free(old->gen); free(old);
    }
    g_nr = g_live = g_live_peak = g_overflow_logged = g_busy_red = 0;
    atomic_store(&g_span_last, 0); atomic_store(&g_sparse_writes, 0);
    atomic_fetch_add(&g_now, 1);
    pthread_mutex_unlock(&g_slot_mu);
    atomic_store(&satchel_dirty_closed, 0);
    return 0;
}
'''
    destination.write_text(text)
    return destination
