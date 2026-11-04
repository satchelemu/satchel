"""Own optional memory and CPU samplers across per-game environment changes."""
from pathlib import Path
import runpy
replace = runpy.run_path(str(Path(__file__).with_name('owners_overlay.py')))['replace']


def prepare(source, destination):
    text = source.read_text()
    text = '#include "SessionNativeWorkers.h"\n#include <errno.h>\nstatic SatchelNativeWorkers satchel_diagnostics = SATCHEL_NATIVE_WORKERS_INIT;\nstatic void satchel_thrcpu_start(void);\n' + text
    text = text.replace('__attribute__((constructor)) static void memstat_start(void)', 'void satchel_memstat_start(void)')
    text = replace(text, 'void satchel_memstat_start(void) {', 'void satchel_memstat_start(void) {\n    satchel_thrcpu_start();')
    text = replace(text, '__attribute__((constructor)) static void thrcpu_start(void)', 'static void satchel_thrcpu_start(void)')
    text = replace(text, 'if (!f) { LOG("memstat: cannot open %s", (const char *)p); return NULL; }',
        'if (!f) { LOG("memstat: cannot open %s", (const char *)p); free(p); return NULL; }')
    text = replace(text, 'for (int sec = 0;; sec++) { sample(f, (now_ns() - t0) / 1e9, dev); if (sec == dump_at) dump_regions(dp); sleep(1); }',
        'for (int sec = 0; !satchel_native_stopping(&satchel_diagnostics); sec++) { @autoreleasepool { sample(f, (now_ns() - t0) / 1e9, dev); if (sec == dump_at) dump_regions(dp); } if (satchel_native_wait(&satchel_diagnostics, 1000)) break; }\n    fclose(f); free(p);')
    text = replace(text, 'pthread_t th; pthread_create(&th, NULL, memstat_thread, path); pthread_detach(th);',
        'if (satchel_native_create(&satchel_diagnostics, NULL, memstat_thread, path)) free(path);')
    text = replace(text, 'FILE *f = fopen(path, "w"); if (!f) return NULL;',
        'FILE *f = fopen(path, "w"); if (!f) { free(p); return NULL; }')
    text = replace(text, '        sleep(iv); uint64_t t = now_ns();',
        '        if (satchel_native_wait(&satchel_diagnostics, iv > 0 ? (unsigned)iv * 1000 : 1000)) break; uint64_t t = now_ns();')
    text = replace(text, '    return NULL;\n}\nstatic void satchel_thrcpu_start',
        '    fclose(f); free(p); return NULL;\n}\nstatic void satchel_thrcpu_start')
    text = replace(text, 'pthread_t th; pthread_create(&th, NULL, thrcpu_thread, spec); pthread_detach(th);',
        'if (satchel_native_create(&satchel_diagnostics, NULL, thrcpu_thread, spec)) free(spec);')
    text = text.replace('path = malloc(n); snprintf(path,', 'free(path); path = malloc(n); snprintf(path,')
    text = text.replace('spec = malloc(n); snprintf(spec,', 'free(spec); spec = malloc(n); snprintf(spec,')
    text += '''
void satchel_diagnostics_request_stop(void) { satchel_native_stop(&satchel_diagnostics); }
int satchel_diagnostics_join(unsigned ms) { return satchel_native_join(&satchel_diagnostics, ms); }
int satchel_diagnostics_session_reset(void) { return satchel_native_reset(&satchel_diagnostics); }
'''
    destination.write_text(text)
    return destination
