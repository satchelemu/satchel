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
        for (int i = 0; i < BLK; i++) { free(block[i].state); free(block[i].gen); }
        free(block); atomic_store(&g_blk[b], NULL);
    }
    g_nr = g_live = g_live_peak = g_overflow_logged = g_busy_red = 0;
    atomic_fetch_add(&g_now, 1);
    pthread_mutex_unlock(&g_slot_mu);
    atomic_store(&satchel_dirty_closed, 0);
    return 0;
}
'''
    destination.write_text(text)
    return destination
