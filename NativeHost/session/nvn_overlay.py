"""Retire descriptor side tables and sparse guest aliases after Metal retires."""
from pathlib import Path
import runpy
HERE = Path(__file__).resolve().parent
replace = runpy.run_path(str(HERE / 'owners_overlay.py'))['replace']
scalars = runpy.run_path(str(HERE / 'state_overlay.py'))['scalar_caches']


def prepare(source, destination):
    text = source.read_text()
    # The constructor wraps semantic entries with large-stack/profiling thunks
    # and installs empty implementations. Reinitializing only the raw table on
    # the next title discards those wrappers and overflows small guest stacks.
    # This is process dispatch state, not a per-session scalar cache.
    text = replace(text, 'static void nvn_init_slots(void) {\n    static int done; if (done) return; done = 1;',
                   'static int satchel_nvn_slots_ready;\nstatic void nvn_init_slots(void) {\n    if (satchel_nvn_slots_ready) return; satchel_nvn_slots_ready = 1;')
    text = replace(text, 'void nvn_ft_poll(void) {',
                   'static tex_t *satchel_ft_hit; static size_t satchel_ft_chit;\nvoid nvn_ft_poll(void) {')
    text = replace(text, '    static tex_t *hit; static size_t chit; size_t nhit = 0;',
                   '    #define hit satchel_ft_hit\n    #define chit satchel_ft_chit\n    size_t nhit = 0;')
    text = replace(text, 'static void ft_flush(const uint8_t *a, uint64_t n) {',
                   '#undef hit\n#undef chit\nstatic void ft_flush(const uint8_t *a, uint64_t n) {')
    text = '#include "SessionBoundary.h"\n#include "SessionState.h"\n#include <errno.h>\n' + scalars(text, 'SATCHEL_STATE_NVN')
    text = runpy.run_path(str(HERE / 'suspend_overlay.py'))['bounded_suspend'](text)
    retirement = (HERE / 'SessionNVNReset.inc').read_text()
    if 'void *capture[6];' in text:
        retirement = retirement.replace('free((void *)value);', 'prog_keys_t *keys = (prog_keys_t *)value; for (unsigned i = 0; i < 6; i++) free(keys->capture[i]); free(keys);')
    if 'static _Atomic int g_thermal_limited;' in text:
        retirement = retirement.replace('g_frame = 0;', 'atomic_store(&g_thermal_limited, 0); g_frame = 0;')
    text += retirement
    destination.write_text(text)
    return destination
